#include "storage/disk_manager.hpp"
#include "log/log_manager.hpp"
#include "storage/buffer_pool_manager.hpp"

#include "catalog/schema.hpp"
#include "catalog/catalog.hpp"
#include "catalog/table_heap.hpp"

#include "index/bplus_tree_index.hpp"

#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "execution/insert_executor.hpp"

#include "sql/lexer.hpp"
#include "sql/parser.hpp"
#include "sql/planner.hpp"
#include "sql/ast.hpp"

#include "common/tuple.hpp"
#include "common/types.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Helper to trim whitespace
static void Trim(std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        s.clear();
        return;
    }
    s = s.substr(start, end - start + 1);
}

int main() {
    // --- Storage + WAL setup ---
    DiskManager dm("data.db");
    LogManager log("wal.log", &dm);
    log.Recover();

    BufferPoolManager bpm(10, &dm, &log);

    // --- Build catalog and schema ---
    Catalog catalog;

    // Schema: table t(a INT, b INT)
    Schema t_schema({
        Column("a", TypeId::INT),
        Column("b", TypeId::INT)
    });

    // Create table 't' via catalog
    TableHeap *table_t = catalog.CreateTable("t", t_schema, &bpm);

    // Create B+tree index on column 'a' for table t, and register in catalog.
    BPlusTreeIndex index_t_a(&bpm);
    index_t_a.Init();
    catalog.RegisterIndex("t", "a", &index_t_a);

    // Planner (uses catalog + bpm)
    Planner planner(&bpm, &catalog);

    std::cout << "=== MiniDB demo: INSERT + SELECT with catalog/schema ===\n";

    // --- 1. Use INSERT INTO via SQL parser + InsertExecutor ---

    // Insert 20 rows: (a=i, b=10*i) using SQL INSERT.
    for (int i = 0; i < 20; ++i) {
        std::ostringstream oss;
        oss << "INSERT INTO t VALUES(" << i << ", " << (i * 10) << ");";
        std::string sql = oss.str();
        std::cout << "SQL> " << sql << "\n";

        try {
            Lexer lex(sql);
            // Peek first token to know it's INSERT
            Token first = lex.PeekToken();
            if (first.type != TokenType::INSERT) {
                std::cerr << "Expected INSERT statement\n";
                continue;
            }
            Parser parser(std::move(lex));
            InsertStatement istmt = parser.ParseInsert();

            InsertExecutor ins_exec(&catalog, &bpm, istmt);
            if (!ins_exec.Execute()) {
                std::cerr << "Insert failed for: " << sql << "\n";
            }
        } catch (const SqlParseError &e) {
            std::cerr << e.what() << "\n";
        } catch (const std::exception &e) {
            std::cerr << "Error during INSERT: " << e.what() << "\n";
        }
    }

    // --- 2. Run some SELECT queries via SQL parser + planner + executors ---

    std::vector<std::string> queries = {
        "SELECT * FROM t;",
        "SELECT a, b FROM t WHERE a = 7;",
        "SELECT a FROM t WHERE a BETWEEN 5 AND 12;",
        "SELECT * FROM t WHERE a >= 5 AND a <= 12;",
        "SELECT * FROM t WHERE a > 10 OR b < 50;"
    };

    for (const auto &sql_raw : queries) {
        std::string sql = sql_raw;
        Trim(sql);
        if (sql.empty()) continue;

        std::cout << "\nSQL> " << sql << "\n";

        try {
            Lexer lex(sql);
            // determine if it's SELECT (we only handle SELECT here)
            Token first = lex.PeekToken();
            if (first.type != TokenType::SELECT) {
                std::cerr << "Only SELECT supported in this query batch\n";
                continue;
            }

            Parser parser(std::move(lex));
            SelectStatement stmt = parser.ParseSelect();

            // Plan
            PlanResult plan = planner.PlanSelect(stmt);
            Tuple t;
            RID rid;

            auto print_tuple = [&](const Tuple &tup,
                                   const SelectStatement &s,
                                   const Schema *schema) {
                if (!schema) {
                    tup.PrintAll();
                    return;
                }

                if (s.select_list.empty()) {
                    // SELECT *
                    tup.PrintAll();
                } else {
                    std::cout << "(";
                    for (size_t i = 0; i < s.select_list.size(); ++i) {
                        const auto &col_ref = s.select_list[i];
                        int idx = schema->GetColumnIndex(col_ref.name);
                        if (idx == -1) {
                            std::cout << "NULL";
                        } else {
                            std::cout << tup.GetInt(static_cast<size_t>(idx));
                        }
                        if (i + 1 < s.select_list.size()) std::cout << ", ";
                    }
                    std::cout << ")";
                }
            };

            // Execute
            if (plan.use_index && plan.index_scan) {
                while (plan.index_scan->Next(t, rid)) {
                    print_tuple(t, stmt, plan.schema);
                    std::cout << "\n";
                }
            } else if (plan.filter) {
                while (plan.filter->Next(t, rid)) {
                    print_tuple(t, stmt, plan.schema);
                    std::cout << "\n";
                }
            } else if (plan.seq_scan) {
                while (plan.seq_scan->Next(t, rid)) {
                    print_tuple(t, stmt, plan.schema);
                    std::cout << "\n";
                }
            }

        } catch (const SqlParseError &e) {
            std::cerr << e.what() << "\n";
        } catch (const std::exception &e) {
            std::cerr << "Error during SELECT: " << e.what() << "\n";
        }
    }

    std::cout << "\n=== Done ===\n";
    return 0;
}