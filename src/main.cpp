#include "storage/disk_manager.hpp"
#include "log/log_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "catalog/table_heap.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"
#include <sql/parser.hpp>
#include <sql/planner.hpp>

#include <iostream>
#include <vector>

int main() {
    DiskManager dm("data.db");
    LogManager log("wal.log", &dm);
    log.Recover();
    BufferPoolManager bpm(10, &dm, &log);

    Catalog catalog;

    Schema t_schema({
        Column("a", TypeId::INT),
        Column("b", TypeId::INT)
    });

    TableHeap *table = catalog.CreateTable("t", t_schema, &bpm);


    std::vector<RID> inserted_rids;
    for (int i = 0; i < 20; ++i) {
        int32_t row[2] = {i, i * 10};
        RID rid;
        bool ok = table->InsertTuple(reinterpret_cast<const char *>(row),
                                     sizeof(row),
                                     rid);
        if (!ok) {
            std::cerr << "Insert failed at i = " << i << "\n";
            return 1;
        }
        inserted_rids.push_back(rid);
    }

    BPlusTreeIndex index(&bpm);
    index.Init();
    for (int i = 0; i < 20; ++i) {
        index.Insert(i, inserted_rids[i]);
    }
    catalog.RegisterIndex("t", "a", &index);

    // Planner now uses catalog instead of direct table/index pointers
    Planner planner(&bpm, &catalog);

    // Some example queries
    std::vector<std::string> queries = {
        "SELECT * FROM t;",
        "SELECT a, b FROM t WHERE a = 7;",
        "SELECT a FROM t WHERE a BETWEEN 5 AND 12;",
        "SELECT * FROM t WHERE a >= 5 AND a <= 12;",
        "SELECT * FROM t WHERE a > 10 OR b < 50;"
    };

    for (const auto &sql : queries) {
        std::cout << "\nSQL> " << sql << "\n";
        try {
            Lexer lex(sql);
            Parser parser(lex);
            SelectStatement stmt = parser.ParseSelect();

            PlanResult plan = planner.PlanSelect(stmt);

            Tuple t;
            RID rid;

            auto print_tuple = [&](const Tuple &tup,
                       const SelectStatement &stmt,
                       const Schema *schema) {
                if (!schema) {
                    tup.PrintAll();
                    return;
                }

                if (stmt.select_list.empty()) {
                    // SELECT *
                    tup.PrintAll();
                } else {
                    std::cout << "(";
                    for (size_t i = 0; i < stmt.select_list.size(); ++i) {
                        const auto &col_ref = stmt.select_list[i];
                        int idx = schema->GetColumnIndex(col_ref.name);
                        if (idx == -1) {
                            std::cout << "NULL";
                        } else {
                            std::cout << tup.GetInt(static_cast<size_t>(idx));
                        }
                        if (i + 1 < stmt.select_list.size()) std::cout << ", ";
                    }
                    std::cout << ")";
                }
            };

            if (plan.use_index && plan.index_scan) {
                while (plan.index_scan->Next(t, rid)) {
                    print_tuple(t, stmt, &t_schema);
                    std::cout << "\n";
                }
            } else if (plan.filter) {
                while (plan.filter->Next(t, rid)) {
                    print_tuple(t, stmt, &t_schema);
                    std::cout << "\n";
                }
            } else if (plan.seq_scan) {
                while (plan.seq_scan->Next(t, rid)) {
                    print_tuple(t, stmt, &t_schema);
                    std::cout << "\n";
                }
            }

        } catch (const SqlParseError &e) {
            std::cerr << e.what() << "\n";
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

    return 0;
}
