#include "storage/disk_manager.hpp"
#include "log/log_manager.hpp"
#include "storage/buffer_pool_manager.hpp"

#include "catalog/schema.hpp"
#include "catalog/catalog.hpp"
#include "catalog/table_heap.hpp"

#include "index/bplus_tree_index.hpp"

#include "execution/executor.hpp"
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

            // ---- EXPLAIN SELECT ... ----
            if (first.type == TokenType::EXPLAIN) {
                lex.NextToken();  // consume EXPLAIN
                Parser parser(std::move(lex));
                SelectStatement stmt = parser.ParseSelect();
                PlanResult plan = planner.PlanSelect(stmt);
                std::cout << Explain(*plan.root);
                continue;
            }

            // ---- SELECT ----
            if (first.type == TokenType::SELECT) {
                Parser parser(std::move(lex));
                SelectStatement stmt = parser.ParseSelect();

                // The plan does all the work (joins, WHERE, GROUP BY, ORDER BY,
                // projection, LIMIT); we just pull rows from its root.
                PlanResult plan = planner.PlanSelect(stmt);
                Tuple t;
                RID rid;
                while (plan.root->Next(t, rid)) {
                    t.PrintAll();
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