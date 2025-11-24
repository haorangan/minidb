// Point-query latency benchmark.
//
// Loads N rows (shuffled keys) into `t (id INT, val INT)` with SQL INSERTs,
// committed in batches (WAL + fsync per commit), then times SQL point queries
// end to end: lex, parse, plan, index scan, and tuple fetch from the heap.
// Each result is checked.
//
// Usage: minidb_bench [--rows N] [--queries Q] [--pool FRAMES] [--batch ROWS] [--scans S]

#include "db/database.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double MicrosSince(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

static void PrintLatencies(const std::string &label, std::vector<double> us) {
    std::sort(us.begin(), us.end());
    auto pct = [&](double p) { return us[static_cast<size_t>(p * (us.size() - 1))]; };
    double mean = std::accumulate(us.begin(), us.end(), 0.0) / us.size();
    std::cout << std::fixed << std::setprecision(2)
              << label << " (" << us.size() << " queries, microseconds)\n"
              << "  mean " << mean << "   p50 " << pct(0.50) << "   p95 " << pct(0.95)
              << "   p99 " << pct(0.99) << "   max " << us.back() << "\n";
}

int main(int argc, char **argv) {
    int64_t rows = 1000000;
    int queries = 10000;
    size_t pool = 8192;
    int64_t batch = 0;  // 0 = pick from pool size
    int scans = 5;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::atoll(argv[++i]) : 0; };
        if (arg == "--rows") rows = next();
        else if (arg == "--queries") queries = static_cast<int>(next());
        else if (arg == "--pool") pool = static_cast<size_t>(next());
        else if (arg == "--scans") scans = static_cast<int>(next());
        else if (arg == "--batch") batch = next();
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    // A transaction's pages stay in the buffer pool until it commits (no-steal),
    // and each row can touch a different leaf, so a batch must fit in the pool
    if (batch <= 0) batch = std::max<int64_t>(1, std::min<int64_t>(5000, static_cast<int64_t>(pool) / 2 - 16));

    const std::string db_file = "bench.db";
    const std::string wal_file = "bench_wal.log";
    std::remove(db_file.c_str());
    std::remove(wal_file.c_str());

    {
        Database::Options options;
        options.pool_size = pool;
        options.checkpoint_bytes = 64u << 20;
        Database db(db_file, wal_file, options);

        std::cout << "MiniDB point-query benchmark\n"
                  << "  rows " << rows << ", buffer pool " << pool << " frames ("
                  << pool * PAGE_SIZE / (1024 * 1024) << " MiB), " << batch
                  << " rows per committed transaction\n\n";

        db.Execute("CREATE TABLE t (id INT, val INT);");

        // ---- Load ----
        std::vector<int32_t> keys(static_cast<size_t>(rows));
        std::iota(keys.begin(), keys.end(), 0);
        std::mt19937 rng(12345);
        std::shuffle(keys.begin(), keys.end(), rng);

        auto load_start = Clock::now();
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i % batch == 0) db.Execute("BEGIN;");
            db.Execute("INSERT INTO t VALUES (" + std::to_string(keys[i]) + ", " +
                       std::to_string(static_cast<int64_t>(keys[i]) * 7) + ");");
            if ((i + 1) % batch == 0 || i + 1 == keys.size()) db.Execute("COMMIT;");
        }
        double load_s = MicrosSince(load_start) / 1e6;

        BPlusTreeIndex *index = db.GetCatalog().GetIndex("t", "id");
        size_t indexed = 0;
        bool valid = index->Validate(&indexed);
        std::cout << std::fixed << std::setprecision(2)
                  << "Load: " << load_s << " s (" << static_cast<int64_t>(rows / load_s)
                  << " rows/s)\n"
                  << "Index: height " << index->GetHeight() << ", " << indexed
                  << " entries, invariants " << (valid ? "OK" : "VIOLATED") << "\n\n";
        if (!valid || indexed != static_cast<size_t>(rows)) return 1;

        auto uses_index = [&](const std::string &sql) {
            return db.Execute("EXPLAIN " + sql).message.find("IndexScan") != std::string::npos;
        };
        if (!uses_index("SELECT * FROM t WHERE id = 1;") || uses_index("SELECT * FROM t WHERE val = 7;")) {
            std::cerr << "Unexpected query plan\n";
            return 1;
        }

        // ---- Indexed point queries ----
        std::uniform_int_distribution<int32_t> pick(0, static_cast<int32_t>(rows - 1));
        std::vector<double> latencies;
        latencies.reserve(queries);
        int wrong = 0;
        for (int q = 0; q < queries; ++q) {
            int32_t k = pick(rng);
            std::string sql = "SELECT * FROM t WHERE id = " + std::to_string(k) + ";";

            auto start = Clock::now();
            auto result = db.Execute(sql).rows;
            latencies.push_back(MicrosSince(start));

            if (result.size() != 1 || result[0].GetInt(0) != k ||
                result[0].GetInt(1) != static_cast<int32_t>(static_cast<int64_t>(k) * 7)) {
                ++wrong;
            }
        }
        PrintLatencies("Point query via index:  SELECT * FROM t WHERE id = ?", latencies);
        std::cout << "  incorrect results: " << wrong << "\n\n";

        // ---- Same lookup on an unindexed column, for comparison ----
        if (scans > 0) {
            latencies.clear();
            int wrong_scan = 0;
            for (int q = 0; q < scans; ++q) {
                int32_t k = pick(rng);
                std::string sql = "SELECT * FROM t WHERE val = " +
                                  std::to_string(static_cast<int64_t>(k) * 7) + ";";
                auto start = Clock::now();
                auto result = db.Execute(sql).rows;
                latencies.push_back(MicrosSince(start));
                if (result.size() != 1 || result[0].GetInt(0) != k) ++wrong_scan;
            }
            PrintLatencies("Full scan (no index):   SELECT * FROM t WHERE val = ?", latencies);
            std::cout << "  incorrect results: " << wrong_scan << "\n";
            wrong += wrong_scan;
        }

        if (wrong != 0) return 1;
    }

    std::remove(db_file.c_str());
    std::remove(wal_file.c_str());
    return 0;
}
