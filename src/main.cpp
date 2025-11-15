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
#include "execution/create_table_executor.hpp"

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

static void Trim(std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        s.clear();
        return;
    }
    s = s.substr(start, end - start + 1);
}

struct ResultRow {
    Tuple tuple;
    RID rid;
};

int main() {
    // Storage + WAL
    DiskManager dm("data.db");
    LogManager log("wal.log", &dm);
    log.Recover();

    BufferPoolManager bpm(10, &dm, &log);

    Catalog catalog;

    std::cout << "=== MiniDB demo: CREATE TABLE + INSERT + SELECT ===\n";

    // --- 1. CREATE TABLE via SQL ---
    {
        std::string sql = "CREATE TABLE t (a INT, b INT);";
        std::cout << "SQL> " << sql << "\n";
        try {
            Lexer lex(sql);
            Token first = lex.PeekToken();
            if (first.type != TokenType::CREATE) {
                std::cerr << "Expected CREATE statement\n";
                return 1;
            }
            Parser parser(std::move(lex));
            CreateTableStatement cstmt = parser.ParseCreateTable();

            CreateTableExecutor cexec(&catalog, &bpm, cstmt);
            if (!cexec.Execute()) {
                std::cerr << "CREATE TABLE failed\n";
                return 1;
            }
        } catch (const SqlParseError &e) {
            std::cerr << e.what() << "\n";
            return 1;
        } catch (const std::exception &e) {
            std::cerr << "Error during CREATE TABLE: " << e.what() << "\n";
            return 1;
        }
    }

    // Get table + schema from catalog
    TableHeap *table_t = catalog.GetTable("t");
    const Schema *t_schema = catalog.GetSchema("t");
    if (!table_t || !t_schema) {
        std::cerr << "Failed to retrieve table t after CREATE TABLE\n";
        return 1;
    }

    // --- 2. Build B+tree index on t.a and register it ---
    BPlusTreeIndex index_t_a(&bpm);
    index_t_a.Init();
    catalog.RegisterIndex("t", "a", &index_t_a);

    // Planner
    Planner planner(&bpm, &catalog);

    // --- 3. INSERT INTO t VALUES (..., ...) via SQL ---
    for (int i = 0; i < 20; ++i) {
        std::ostringstream oss;
        oss << "INSERT INTO t VALUES(" << i << ", " << (i * 10) << ");";
        std::string sql = oss.str();
        std::cout << "SQL> " << sql << "\n";

        try {
            Lexer lex(sql);
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

    // --- 4. Run SELECT queries as before ---
    std::vector<std::string> queries = {
        "SELECT * FROM t;",
        "SELECT a, b FROM t WHERE a = 7;",
        "SELECT a FROM t WHERE a BETWEEN 5 AND 12;",
        "SELECT * FROM t WHERE a >= 5 AND a <= 12;",
        "SELECT * FROM t WHERE a > 10 OR b < 50;",

        // NEW:
        "SELECT * FROM t ORDER BY b DESC LIMIT 5;",
        "SELECT a FROM t ORDER BY a ASC LIMIT 3;",
        "SELECT * FROM t WHERE a >= 5 ORDER BY a DESC;",
    };

    for (const auto &sql_raw : queries) {
        std::string sql = sql_raw;
        Trim(sql);
        if (sql.empty()) continue;

        std::cout << "\nSQL> " << sql << "\n";

        try {
            Lexer lex(sql);
            Token first = lex.PeekToken();
            if (first.type != TokenType::SELECT) {
                std::cerr << "Only SELECT supported in this query batch\n";
                continue;
            }

            Parser parser(std::move(lex));
            SelectStatement stmt = parser.ParseSelect();

            PlanResult plan = planner.PlanSelect(stmt);
            Tuple t;
            RID rid;

            std::vector<ResultRow> rows;

            auto print_tuple = [&](const Tuple &tup,
                                   const SelectStatement &s,
                                   const Schema *schema) {
                if (!schema) {
                    tup.PrintAll();
                    return;
                }

                if (s.select_list.empty()) {
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

            if (plan.use_index && plan.index_scan) {
                while (plan.index_scan->Next(t, rid)) {
                    rows.push_back(ResultRow{t, rid});
                }
            } else if (plan.filter) {
                while (plan.filter->Next(t, rid)) {
                    rows.push_back(ResultRow{t, rid});
                }
            } else if (plan.seq_scan) {
                while (plan.seq_scan->Next(t, rid)) {
                    rows.push_back(ResultRow{t, rid});
                }
            }

            // Apply ORDER BY (if any)
            const Schema *schema = plan.schema;
            if (!stmt.order_by.empty() && schema != nullptr) {
                // For now, only honor the first ORDER BY item
                const OrderByItem &item = stmt.order_by[0];
                int col_idx = schema->GetColumnIndex(item.column_name);
                if (col_idx != -1) {
                    bool asc = item.ascending;
                    std::sort(rows.begin(), rows.end(),
                              [col_idx, asc](const ResultRow &lhs, const ResultRow &rhs) {
                                  int32_t lv = lhs.tuple.GetInt(static_cast<size_t>(col_idx));
                                  int32_t rv = rhs.tuple.GetInt(static_cast<size_t>(col_idx));
                                  if (asc) {
                                      return lv < rv;
                                  } else {
                                      return lv > rv;
                                  }
                              });
                } else {
                    std::cerr << "Warning: ORDER BY column " << item.column_name
                              << " not found in schema; ignoring ORDER BY\n";
                }
            }

            // Apply LIMIT
            int64_t limit = stmt.limit;
            int64_t to_print = (limit < 0)
                               ? static_cast<int64_t>(rows.size())
                               : std::min<int64_t>(limit, static_cast<int64_t>(rows.size()));

            for (int64_t i = 0; i < to_print; ++i) {
                const Tuple &tup = rows[i].tuple;
                print_tuple(tup, stmt, schema);
                std::cout << "\n";
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