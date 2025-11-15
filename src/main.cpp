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
#include <unordered_map>
#include <algorithm>

struct ResultRow {
    Tuple tuple;
    RID rid;
};

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

    // --- Catalog + Planner ---
    Catalog catalog;
    Planner planner(&bpm, &catalog);

    // Keep ownership of indexes we create (one per table, on column 0)
    std::unordered_map<std::string, std::unique_ptr<BPlusTreeIndex>> indexes;

    std::cout << "[WAL] Recovery complete.\n";
    std::cout << "MiniDB CLI\n";
    std::cout << "Commands:\n";
    std::cout << "  SQL statements ending with ';'\n";
    std::cout << "  .exit or .quit to leave\n\n";

    std::string line;
    std::string sql_buffer;

    while (true) {
        // prompt
        if (sql_buffer.empty()) {
            std::cout << "minidb> ";
        } else {
            std::cout << "   ...> ";
        }

        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        // meta commands (only when not accumulating a multi-line statement)
        if (sql_buffer.empty() && (line == ".exit" || line == ".quit")) {
            break;
        }

        sql_buffer += line;
        sql_buffer += "\n";

        // look for ';' that ends a statement
        auto pos = sql_buffer.find(';');
        if (pos == std::string::npos) {
            continue; // keep reading
        }

        // extract one statement
        std::string statement = sql_buffer.substr(0, pos + 1);
        std::string remainder = sql_buffer.substr(pos + 1);
        Trim(remainder);
        sql_buffer = remainder;

        Trim(statement);
        if (statement.empty()) {
            continue;
        }

        std::cout << "SQL> " << statement << "\n";

        try {
            Lexer lex(statement);
            Token first = lex.PeekToken();

            // ---- CREATE TABLE ----
            if (first.type == TokenType::CREATE) {
                Parser parser(std::move(lex));
                CreateTableStatement cstmt = parser.ParseCreateTable();

                CreateTableExecutor cexec(&catalog, &bpm, cstmt);
                if (!cexec.Execute()) {
                    std::cout << "CREATE TABLE failed.\n";
                    continue;
                }

                // After creating the table, automatically create a B+Tree index
                // on the first column (if any). This keeps your index-based
                // queries working without extra SQL.
                TableHeap *table = catalog.GetTable(cstmt.table_name);
                const Schema *schema = catalog.GetSchema(cstmt.table_name);
                if (!table || !schema) {
                    std::cout << "Warning: table/schema not found after CREATE TABLE.\n";
                } else if (schema->GetColumnCount() > 0) {
                    const std::string &col_name = schema->GetColumn(0).name;
                    auto idx = std::make_unique<BPlusTreeIndex>(&bpm);
                    idx->Init();
                    catalog.RegisterIndex(cstmt.table_name, col_name, idx.get());
                    indexes[cstmt.table_name] = std::move(idx);
                }

                std::cout << "Table '" << cstmt.table_name << "' created.\n";
                continue;
            }

            // ---- INSERT ----
            if (first.type == TokenType::INSERT) {
                Parser parser(std::move(lex));
                InsertStatement istmt = parser.ParseInsert();

                InsertExecutor ins_exec(&catalog, &bpm, istmt);
                if (!ins_exec.Execute()) {
                    std::cout << "INSERT failed.\n";
                } else {
                    std::cout << "1 row inserted into " << istmt.table_name << ".\n";
                }
                continue;
            }

            // ---- SELECT ----
            if (first.type == TokenType::SELECT) {
                Parser parser(std::move(lex));
                SelectStatement stmt = parser.ParseSelect();

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

                // gather rows from whichever executor is active
                std::vector<ResultRow> rows;
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

                const Schema *schema = plan.schema;

                // ORDER BY (first item only for now)
                if (!stmt.order_by.empty() && schema != nullptr) {
                    const OrderByItem &item = stmt.order_by[0];
                    int col_idx = schema->GetColumnIndex(item.column_name);
                    if (col_idx != -1) {
                        bool asc = item.ascending;
                        std::sort(rows.begin(), rows.end(),
                                  [col_idx, asc](const ResultRow &lhs, const ResultRow &rhs) {
                                      int32_t lv = lhs.tuple.GetInt(static_cast<size_t>(col_idx));
                                      int32_t rv = rhs.tuple.GetInt(static_cast<size_t>(col_idx));
                                      if (asc) return lv < rv;
                                      return lv > rv;
                                  });
                    } else {
                        std::cerr << "Warning: ORDER BY column "
                                  << item.column_name
                                  << " not found; ignoring ORDER BY.\n";
                    }
                }

                // LIMIT
                int64_t limit = stmt.limit;
                int64_t to_print = (limit < 0)
                    ? static_cast<int64_t>(rows.size())
                    : std::min<int64_t>(limit, static_cast<int64_t>(rows.size()));

                for (int64_t i = 0; i < to_print; ++i) {
                    const Tuple &tup = rows[i].tuple;
                    print_tuple(tup, stmt, schema);
                    std::cout << "\n";
                }

                continue;
            }

            std::cout << "Unrecognized statement type.\n";

        } catch (const SqlParseError &e) {
            std::cout << e.what() << "\n";
        } catch (const std::exception &e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    std::cout << "Bye.\n";
    return 0;
}