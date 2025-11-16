// B+ tree correctness tests. Every result is checked against std::multimap.

#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
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
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "  CHECK failed: " #cond " (" << __FILE__ << ":"      \
                      << __LINE__ << ")\n";                                    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using Reference = std::multimap<int32_t, RID>;

static bool RidLess(const RID &a, const RID &b) {
    return a.page_id != b.page_id ? a.page_id < b.page_id : a.slot_id < b.slot_id;
}

static bool SameRids(std::vector<RID> got, std::vector<RID> want) {
    if (got.size() != want.size()) return false;
    std::sort(got.begin(), got.end(), RidLess);
    std::sort(want.begin(), want.end(), RidLess);
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i].page_id != want[i].page_id || got[i].slot_id != want[i].slot_id) return false;
    }
    return true;
}

static std::vector<RID> RefRange(const Reference &ref, int32_t lo, int32_t hi) {
    std::vector<RID> out;
    if (lo > hi) return out;
    for (auto it = ref.lower_bound(lo); it != ref.end() && it->first <= hi; ++it) {
        out.push_back(it->second);
    }
    return out;
}

// Fresh on-disk file + buffer pool per test, removed afterwards
struct Env {
    std::string file;
    std::unique_ptr<DiskManager> disk;
    std::unique_ptr<BufferPoolManager> bpm;

    Env(const std::string &name, size_t pool_size) : file(name) {
        std::remove(file.c_str());
        disk = std::make_unique<DiskManager>(file);
        bpm = std::make_unique<BufferPoolManager>(pool_size, disk.get(), nullptr);
    }
    ~Env() {
        bpm.reset();
        disk.reset();
        std::remove(file.c_str());
    }
};

// Insert `keys` (RID derived from insertion order), then verify every lookup.
static void RunAgainstReference(const std::string &name, const std::vector<int32_t> &keys,
                                uint16_t leaf_max, uint16_t internal_max, size_t pool_size,
                                int min_height) {
    std::cout << name << std::flush;
    auto start = std::chrono::steady_clock::now();
    Env env(name + ".db", pool_size);
    BPlusTreeIndex index(env.bpm.get(), leaf_max, internal_max);
    index.Init();

    Reference ref;
    for (size_t i = 0; i < keys.size(); ++i) {
        RID rid(static_cast<page_id_t>(i / 1000), static_cast<uint16_t>(i % 1000));
        CHECK(index.Insert(keys[i], rid));
        ref.emplace(keys[i], rid);
    }

    size_t count = 0;
    CHECK(index.Validate(&count));
    CHECK(count == keys.size());
    CHECK(index.GetHeight() >= min_height);

    int32_t lo = *std::min_element(keys.begin(), keys.end());
    int32_t hi = *std::max_element(keys.begin(), keys.end());

    // Point lookups, including keys just outside the inserted range
    std::mt19937 rng(7);
    std::vector<RID> got;
    for (int q = 0; q < 2000; ++q) {
        int32_t k = std::uniform_int_distribution<int32_t>(lo - 5, hi + 5)(rng);
        index.SearchEqual(k, got);
        CHECK(SameRids(got, RefRange(ref, k, k)));
    }

    // Range lookups of varying widths
    for (int q = 0; q < 500; ++q) {
        int32_t a = std::uniform_int_distribution<int32_t>(lo - 5, hi + 5)(rng);
        int32_t w = std::uniform_int_distribution<int32_t>(0, (hi - lo) / 4 + 1)(rng);
        index.SearchRange(a, a + w, got);
        CHECK(SameRids(got, RefRange(ref, a, a + w)));
    }

    // Whole range and an empty (inverted) range
    index.SearchRange(lo, hi, got);
    CHECK(got.size() == keys.size());
    index.SearchRange(hi, lo, got);
    CHECK(lo == hi || got.empty());

    std::cout << "  height " << index.GetHeight() << ", "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " s" << std::endl;
}

static void TestSqlUsesIndexOnFirstColumn() {
    std::cout << "sql_index_on_first_column" << std::endl;
    Env env("sql_index_test.db", 64);
    Catalog catalog;
    Planner planner(env.bpm.get(), &catalog);

    CreateTableStatement create{"users", {{"id", "INT"}, {"score", "INT"}}};
    CHECK(CreateTableExecutor(&catalog, env.bpm.get(), create).Execute());
    BPlusTreeIndex index(env.bpm.get());
    index.Init();
    catalog.RegisterIndex("users", "id", &index);

    for (int i = 0; i < 5000; ++i) {
        InsertStatement ins{"users", {i, i * 10}};
        CHECK(InsertExecutor(&catalog, env.bpm.get(), ins).Execute());
    }

    auto run = [&](const std::string &sql, bool &used_index) {
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
        }
        return rows;
    };

    bool used_index = false;
    auto rows = run("SELECT * FROM users WHERE id = 4321;", used_index);
    CHECK(used_index);
    CHECK(rows.size() == 1 && rows[0].GetInt(0) == 4321 && rows[0].GetInt(1) == 43210);

    rows = run("SELECT * FROM users WHERE id BETWEEN 100 AND 199;", used_index);
    CHECK(used_index);
    CHECK(rows.size() == 100);

    rows = run("SELECT * FROM users WHERE score = 43210;", used_index);
    CHECK(!used_index);
    CHECK(rows.size() == 1 && rows[0].GetInt(0) == 4321);
}

int main() {
    std::mt19937 rng(42);

    // Random keys with many duplicates, tiny nodes => deep tree, many splits
    {
        std::vector<int32_t> keys(5000);
        for (auto &k : keys) k = std::uniform_int_distribution<int32_t>(-500, 500)(rng);
        RunAgainstReference("random_dups_small_fanout", keys, 3, 3, 8192, 5);
    }

    // Monotonic inserts: always split the rightmost / leftmost path
    {
        std::vector<int32_t> keys(20000);
        for (int i = 0; i < 20000; ++i) keys[i] = i;
        RunAgainstReference("ascending_small_fanout", keys, 4, 4, 16384, 5);
        std::reverse(keys.begin(), keys.end());
        RunAgainstReference("descending_small_fanout", keys, 4, 4, 16384, 5);
    }

    // One key repeated: every separator is equal, lookups rely on the leaf chain
    {
        std::vector<int32_t> keys(3000, 7);
        RunAgainstReference("all_same_key", keys, 3, 3, 4096, 4);
    }

    // Full-size pages with a tiny buffer pool: constant eviction and re-reads
    {
        std::vector<int32_t> keys(300000);
        for (int i = 0; i < 300000; ++i) keys[i] = i;
        std::shuffle(keys.begin(), keys.end(), rng);
        RunAgainstReference("random_full_fanout_small_pool", keys, 0, 0, 8, 3);
    }

    TestSqlUsesIndexOnFirstColumn();

    if (g_failures == 0) {
        std::cout << "All B+ tree tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}
