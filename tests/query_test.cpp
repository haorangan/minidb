// End-to-end query tests for joins, aggregation, sort/limit, and planning.
// Each SQL result is compared with the same answer computed directly in C++.

#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "index/bplus_tree_index.hpp"
#include "catalog/catalog.hpp"
#include "execution/create_table_executor.hpp"
#include "execution/insert_executor.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using Row = std::vector<int64_t>;
using Rows = std::vector<Row>;

// A throwaway database: tables get a B+ tree on their first column, like the CLI
class TestDb {
public:
    TestDb() {
        std::remove(file_.c_str());
        disk_ = std::make_unique<DiskManager>(file_);
        bpm_ = std::make_unique<BufferPoolManager>(1024, disk_.get(), nullptr);
        planner_ = std::make_unique<Planner>(bpm_.get(), &catalog_);
    }
    ~TestDb() {
        planner_.reset();
        bpm_.reset();
        disk_.reset();
        std::remove(file_.c_str());
    }

    void CreateTable(const std::string &name, const std::vector<std::string> &cols) {
        CreateTableStatement stmt{name, {}};
        for (const auto &c : cols) stmt.columns.push_back({c, "INT"});
        CHECK(CreateTableExecutor(&catalog_, bpm_.get(), stmt).Execute());

        auto index = std::make_unique<BPlusTreeIndex>(bpm_.get());
        index->Init();
        catalog_.RegisterIndex(name, cols[0], index.get());
        indexes_.push_back(std::move(index));
    }

    void Insert(const std::string &table, const Row &values) {
        InsertStatement stmt{table, values};
        CHECK(InsertExecutor(&catalog_, bpm_.get(), stmt).Execute());
    }

    PlanResult Plan(const std::string &sql) {
        Parser parser{Lexer(sql)};
        SelectStatement stmt = parser.ParseSelect();
        return planner_->PlanSelect(stmt);
    }

    Rows Query(const std::string &sql) {
        PlanResult plan = Plan(sql);
        Rows rows;
        Tuple t;
        RID rid;
        while (plan.root->Next(t, rid)) rows.push_back(t.GetValues());
        return rows;
    }

    std::string ExplainOf(const std::string &sql) { return Explain(*Plan(sql).root); }

    // True if planning or running the query throws
    bool Fails(const std::string &sql) {
        try {
            Query(sql);
        } catch (const std::exception &) {
            return true;
        }
        return false;
    }

private:
    std::string file_ = "query_test.db";
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPoolManager> bpm_;
    Catalog catalog_;
    std::unique_ptr<Planner> planner_;
    std::vector<std::unique_ptr<BPlusTreeIndex>> indexes_;
};

static Rows Sorted(Rows rows) {
    std::sort(rows.begin(), rows.end());
    return rows;
}

static bool Contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

// ---- Test data ----
//   emp(id, dept, salary)   500 rows, dept in 0..6
//   dept(id, budget)        ids 0..9 (7, 8, 9 have no employees)
//   region(dept_id, code)   two rows per even dept id (duplicate join keys)
struct Emp { int64_t id, dept, salary; };
struct Dept { int64_t id, budget; };
struct Region { int64_t dept_id, code; };

static std::vector<Emp> g_emp;
static std::vector<Dept> g_dept;
static std::vector<Region> g_region;

static void Load(TestDb &db) {
    db.CreateTable("emp", {"id", "dept", "salary"});
    db.CreateTable("dept", {"id", "budget"});
    db.CreateTable("region", {"dept_id", "code"});
    db.CreateTable("empty", {"x", "y"});

    for (int64_t i = 0; i < 500; ++i) g_emp.push_back({i, i % 7, (i * 37) % 1000});
    for (int64_t d = 0; d < 10; ++d) g_dept.push_back({d, d * 100});
    for (int64_t d = 0; d < 7; d += 2) {
        g_region.push_back({d, d * 10 + 1});
        g_region.push_back({d, d * 10 + 2});
    }

    for (const auto &e : g_emp) db.Insert("emp", {e.id, e.dept, e.salary});
    for (const auto &d : g_dept) db.Insert("dept", {d.id, d.budget});
    for (const auto &r : g_region) db.Insert("region", {r.dept_id, r.code});
}

// ---- Joins ----

static void TestHashJoin(TestDb &db) {
    std::cout << "hash_join" << std::endl;
    Rows want;
    for (const auto &e : g_emp)
        for (const auto &d : g_dept)
            if (e.dept == d.id) want.push_back({e.id, e.dept, e.salary, d.id, d.budget});

    const std::string sql = "SELECT * FROM emp JOIN dept ON emp.dept = dept.id;";
    CHECK(Contains(db.ExplainOf(sql), "HashJoin(emp.dept = dept.id)"));
    CHECK(Sorted(db.Query(sql)) == Sorted(want));

    // Operand order in ON doesn't matter; INNER JOIN is the same thing
    CHECK(Sorted(db.Query("SELECT * FROM emp INNER JOIN dept ON dept.id = emp.dept;")) == Sorted(want));
}

static void TestNestedLoopJoin(TestDb &db) {
    std::cout << "nested_loop_join" << std::endl;
    Rows want;
    for (const auto &e : g_emp)
        for (const auto &d : g_dept)
            if (e.salary < d.budget && d.id != e.dept) want.push_back({e.id, d.id});

    const std::string sql =
        "SELECT emp.id, dept.id FROM emp JOIN dept ON emp.salary < dept.budget AND dept.id != emp.dept;";
    CHECK(Contains(db.ExplainOf(sql), "NestedLoopJoin"));
    CHECK(Sorted(db.Query(sql)) == Sorted(want));
}

static void TestJoinWithFilterSortLimit(TestDb &db) {
    std::cout << "join_filter_sort_limit" << std::endl;
    Rows want;
    for (const auto &e : g_emp)
        for (const auto &d : g_dept)
            if (e.dept == d.id && e.salary > 900) want.push_back({e.id, d.budget});
    std::sort(want.begin(), want.end(), [](const Row &a, const Row &b) { return a[0] > b[0]; });
    want.resize(5);

    // `budget` is unqualified but unambiguous; ORDER BY uses a non-selected column
    CHECK(db.Query("SELECT emp.id, budget FROM emp JOIN dept ON emp.dept = dept.id "
                   "WHERE salary > 900 ORDER BY emp.id DESC LIMIT 5;") == want);
}

static void TestThreeWayJoin(TestDb &db) {
    std::cout << "three_way_join" << std::endl;
    Rows want;
    for (const auto &e : g_emp)
        for (const auto &d : g_dept)
            for (const auto &r : g_region)
                if (e.dept == d.id && d.id == r.dept_id) want.push_back({e.id, d.budget, r.code});

    const std::string sql =
        "SELECT emp.id, dept.budget, region.code FROM emp "
        "JOIN dept ON emp.dept = dept.id JOIN region ON dept.id = region.dept_id;";
    std::string plan = db.ExplainOf(sql);
    CHECK(Contains(plan, "HashJoin(emp.dept = dept.id)"));
    CHECK(Contains(plan, "HashJoin(dept.id = region.dept_id)"));
    CHECK(Sorted(db.Query(sql)) == Sorted(want));
    CHECK(want.size() == 2 * 286);  // employees in an even dept, two regions each
}

// ---- Aggregation ----

static void TestGroupBy(TestDb &db) {
    std::cout << "group_by" << std::endl;
    std::map<int64_t, std::vector<int64_t>> salaries;
    for (const auto &e : g_emp) salaries[e.dept].push_back(e.salary);

    Rows want;
    for (const auto &[dept, s] : salaries) {
        int64_t sum = 0;
        for (int64_t v : s) sum += v;
        want.push_back({dept, static_cast<int64_t>(s.size()), sum,
                        *std::min_element(s.begin(), s.end()),
                        *std::max_element(s.begin(), s.end()),
                        sum / static_cast<int64_t>(s.size())});
    }

    const std::string sql =
        "SELECT dept, COUNT(*), SUM(salary), MIN(salary), MAX(salary), AVG(salary) "
        "FROM emp GROUP BY dept;";
    CHECK(Contains(db.ExplainOf(sql), "HashAggregate(group by emp.dept;"));
    CHECK(Sorted(db.Query(sql)) == Sorted(want));

    // Select list order can differ from GROUP BY order
    Rows reordered;
    for (const auto &r : want) reordered.push_back({r[1], r[0]});
    CHECK(Sorted(db.Query("SELECT COUNT(*), dept FROM emp GROUP BY dept;")) == Sorted(reordered));
}

static void TestGlobalAggregates(TestDb &db) {
    std::cout << "global_aggregates" << std::endl;
    int64_t count = 0, sum = 0;
    for (const auto &e : g_emp) {
        if (e.salary >= 500) {
            ++count;
            sum += e.salary;
        }
    }
    CHECK((db.Query("SELECT COUNT(*), SUM(salary) FROM emp WHERE salary >= 500;") == Rows{{count, sum}}));

    // Global aggregate over no rows still yields one row
    CHECK((db.Query("SELECT COUNT(*), SUM(y) FROM empty;") == Rows{{0, 0}}));
    // ...but a grouped aggregate over no rows yields none
    CHECK(db.Query("SELECT x, COUNT(*) FROM empty GROUP BY x;").empty());
}

static void TestOrderByAggregate(TestDb &db) {
    std::cout << "order_by_aggregate" << std::endl;
    std::map<int64_t, int64_t> counts;
    for (const auto &e : g_emp) ++counts[e.dept];
    Rows want;
    for (const auto &[dept, n] : counts) want.push_back({dept, n});
    // COUNT desc, then dept asc
    std::sort(want.begin(), want.end(), [](const Row &a, const Row &b) {
        return std::tie(b[1], a[0]) < std::tie(a[1], b[0]);
    });
    want.resize(3);

    CHECK(db.Query("SELECT dept, COUNT(*) AS n FROM emp GROUP BY dept ORDER BY n DESC, dept LIMIT 3;") == want);
    CHECK(db.Query("SELECT dept, COUNT(*) FROM emp GROUP BY dept ORDER BY COUNT(*) DESC, dept LIMIT 3;") == want);
}

static void TestAggregateOverJoin(TestDb &db) {
    std::cout << "aggregate_over_join" << std::endl;
    std::map<int64_t, int64_t> total;
    for (const auto &e : g_emp)
        for (const auto &d : g_dept)
            if (e.dept == d.id) total[d.budget] += e.salary;
    Rows want;
    for (const auto &[budget, s] : total) want.push_back({budget, s});

    CHECK(db.Query("SELECT dept.budget, SUM(emp.salary) AS total FROM emp "
                   "JOIN dept ON emp.dept = dept.id GROUP BY dept.budget ORDER BY dept.budget;") == want);
}

// ---- Planning and errors ----

static void TestIndexStillUsed(TestDb &db) {
    std::cout << "index_still_used" << std::endl;
    PlanResult plan = db.Plan("SELECT * FROM emp WHERE id = 42;");
    CHECK(plan.use_index);
    CHECK(db.Query("SELECT salary FROM emp WHERE id = 42;") == Rows{{(42 * 37) % 1000}});

    // Literal outside int32 must not be truncated onto key 42
    CHECK(db.Query("SELECT * FROM emp WHERE id = 4294967338;").empty());
    CHECK(!db.Plan("SELECT * FROM emp WHERE id = 4294967338;").use_index);

    // Range bound past INT32_MAX is clamped, not overflowed
    CHECK(db.Query("SELECT COUNT(*) FROM emp WHERE id > 495 AND id <= 2147483647;") == Rows{{4}});
}

static void TestErrors(TestDb &db) {
    std::cout << "errors" << std::endl;
    CHECK(db.Fails("SELECT id, COUNT(*) FROM emp GROUP BY dept;"));          // id not grouped
    CHECK(db.Fails("SELECT * FROM emp GROUP BY dept;"));                     // * with GROUP BY
    CHECK(db.Fails("SELECT id FROM emp JOIN dept ON emp.dept = dept.id;"));  // id is ambiguous
    CHECK(db.Fails("SELECT nope FROM emp;"));                                // unknown column
    CHECK(db.Fails("SELECT * FROM emp JOIN nope ON emp.id = nope.id;"));     // unknown table
    CHECK(db.Fails("SELECT * FROM emp JOIN emp ON emp.id = emp.id;"));       // self-join needs aliases
    CHECK(db.Fails("SELECT SUM(*) FROM emp;"));                              // only COUNT takes *
    CHECK(db.Fails("SELECT * FROM emp ORDER BY nope;"));
}

int main() {
    TestDb db;
    Load(db);

    TestHashJoin(db);
    TestNestedLoopJoin(db);
    TestJoinWithFilterSortLimit(db);
    TestThreeWayJoin(db);
    TestGroupBy(db);
    TestGlobalAggregates(db);
    TestOrderByAggregate(db);
    TestAggregateOverJoin(db);
    TestIndexStillUsed(db);
    TestErrors(db);

    return TestSummary("query");
}
