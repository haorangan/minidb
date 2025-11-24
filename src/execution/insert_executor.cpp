//
// Created by Haoran Gan on 11/15/25.
//


#include "execution/insert_executor.hpp"
#include "common/crash_point.hpp"
#include <cstdint>
#include <iostream>

bool InsertExecutor::Execute() {
    if (!catalog_) {
        std::cerr << "InsertExecutor: no catalog\n";
        return false;
    }

    TableHeap *table = catalog_->GetTable(stmt_.table_name);
    const Schema *schema = catalog_->GetSchema(stmt_.table_name);

    if (!table || !schema) {
        std::cerr << "InsertExecutor: unknown table " << stmt_.table_name << "\n";
        return false;
    }

    // Very simple type check: number of VALUES must match number of columns
    if (stmt_.values.size() != schema->GetColumnCount()) {
        std::cerr << "InsertExecutor: VALUES count (" << stmt_.values.size()
                  << ") does not match column count (" << schema->GetColumnCount()
                  << ") for table " << stmt_.table_name << "\n";
        return false;
    }

    // Encode values as int32_t array (for now all columns are INT)
    std::vector<int32_t> vals(stmt_.values.size());
    for (size_t i = 0; i < stmt_.values.size(); ++i) {
        if (stmt_.values[i] < INT32_MIN || stmt_.values[i] > INT32_MAX) {
            std::cerr << "InsertExecutor: value out of INT range: " << stmt_.values[i] << "\n";
            return false;
        }
        vals[i] = static_cast<int32_t>(stmt_.values[i]);
    }

    RID rid;
    bool ok = table->InsertTuple(reinterpret_cast<const char *>(vals.data()),
                                 static_cast<uint16_t>(vals.size() * sizeof(int32_t)),
                                 rid);
    if (!ok) {
        std::cerr << "InsertExecutor: InsertTuple failed\n";
        return false;
    }

    // The row is in the heap but not yet the index
    CRASH_POINT("insert.after_heap");

    // Update the index on the first column
    if (schema->GetColumnCount() > 0) {
        const std::string &first_col_name = schema->GetColumn(0).name;
        BPlusTreeIndex *idx = catalog_->GetIndex(stmt_.table_name, first_col_name);
        if (idx != nullptr) {
            int32_t key = vals[0]; // first column
            bool idx_ok = idx->Insert(key, rid);
            if (!idx_ok) {
                std::cerr << "InsertExecutor: index insert failed\n";
                return false;
            }
        }
    }

    return true;
}