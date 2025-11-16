// Point-query latency benchmark.
//
// Loads N rows (shuffled keys) into `t (id INT, val INT)` through the normal
// insert path, then times SQL point queries end to end: lex, parse, plan,
// index scan, and tuple fetch from the heap. Each result is checked.
//
// Usage: minidb_bench [--rows N] [--queries Q] [--pool FRAMES] [--scans S] [--no-wal]

#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "log/log_manager.hpp"
#include "index/bplus_tree_index.hpp"
#include "catalog/catalog.hpp"
#include "execution/create_table_executor.hpp"
#include "execution/insert_executor.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"

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

static std::vector<Tuple> RunSelect(Planner &planner, const std::string &sql, bool &used_index) {
    Parser parser{Lexer(sql)};
    SelectStatement stmt = parser.ParseSelect();
    PlanResult plan = planner.PlanSelect(stmt);
    used_index = plan.use_index;

    std::vector<Tuple> rows;
    Tuple t;
    RID rid;
    if (plan.index_scan) {
        while (plan.index_scan->Next(t, rid)) rows.push_back(t);
    } else if (plan.filter) {
        while (plan.filter->Next(t, rid)) rows.push_back(t);
    } else if (plan.seq_scan) {
        while (plan.seq_scan->Next(t, rid)) rows.push_back(t);
    }
    return rows;
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
    size_t pool = 4096;
    int scans = 5;
    bool wal = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::atoll(argv[++i]) : 0; };
        if (arg == "--rows") rows = next();
        else if (arg == "--queries") queries = static_cast<int>(next());
        else if (arg == "--pool") pool = static_cast<size_t>(next());
        else if (arg == "--scans") scans = static_cast<int>(next());
        else if (arg == "--no-wal") wal = false;
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    const std::string db_file = "bench.db";
    const std::string wal_file = "bench_wal.log";
    std::remove(db_file.c_str());
    std::remove(wal_file.c_str());

    {
        DiskManager disk(db_file);
        std::unique_ptr<LogManager> log;
        if (wal) log = std::make_unique<LogManager>(wal_file, &disk);
        BufferPoolManager bpm(pool, &disk, log.get());
        Catalog catalog;
        Planner planner(&bpm, &catalog);

        std::cout << "MiniDB point-query benchmark\n"
                  << "  rows " << rows << ", buffer pool " << pool << " frames ("
                  << pool * PAGE_SIZE / (1024 * 1024) << " MiB), WAL " << (wal ? "on" : "off")
                  << "\n\n";

        CreateTableStatement create{"t", {{"id", "INT"}, {"val", "INT"}}};
        if (!CreateTableExecutor(&catalog, &bpm, create).Execute()) return 1;
        BPlusTreeIndex index(&bpm);
        index.Init();
        catalog.RegisterIndex("t", "id", &index);

        // ---- Load ----
        std::vector<int32_t> keys(static_cast<size_t>(rows));
        std::iota(keys.begin(), keys.end(), 0);
        std::mt19937 rng(12345);
        std::shuffle(keys.begin(), keys.end(), rng);

        auto load_start = Clock::now();
        for (int32_t k : keys) {
            InsertStatement ins{"t", {k, static_cast<int64_t>(k) * 7}};
            if (!InsertExecutor(&catalog, &bpm, ins).Execute()) {
                std::cerr << "Insert failed at key " << k << "\n";
                return 1;
            }
        }
        double load_s = MicrosSince(load_start) / 1e6;

        size_t indexed = 0;
        bool valid = index.Validate(&indexed);
        std::cout << std::fixed << std::setprecision(2)
                  << "Load: " << load_s << " s (" << static_cast<int64_t>(rows / load_s)
                  << " rows/s)\n"
                  << "Index: height " << index.GetHeight() << ", " << indexed
                  << " entries, invariants " << (valid ? "OK" : "VIOLATED") << "\n\n";
        if (!valid || indexed != static_cast<size_t>(rows)) return 1;

        // ---- Indexed point queries ----
        std::uniform_int_distribution<int32_t> pick(0, static_cast<int32_t>(rows - 1));
        std::vector<double> latencies;
        latencies.reserve(queries);
        int wrong = 0;
        for (int q = 0; q < queries; ++q) {
            int32_t k = pick(rng);
            std::string sql = "SELECT * FROM t WHERE id = " + std::to_string(k) + ";";

            bool used_index = false;
            auto start = Clock::now();
            auto result = RunSelect(planner, sql, used_index);
            latencies.push_back(MicrosSince(start));

            if (!used_index || result.size() != 1 || result[0].GetInt(0) != k ||
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
                bool used_index = true;
                auto start = Clock::now();
                auto result = RunSelect(planner, sql, used_index);
                latencies.push_back(MicrosSince(start));
                if (used_index || result.size() != 1 || result[0].GetInt(0) != k) ++wrong_scan;
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
