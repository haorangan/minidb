#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "catalog/table_heap.hpp"
#include "catalog/table_iterator.hpp"
#include "common/types.hpp"
#include "helper/debug.cpp"

#include <iostream>

int main() {
    DiskManager dm("data.db");
    BufferPoolManager bpm(10, &dm);

    TableHeap *table = TableHeap::Create(&bpm);

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
    }

    // Dump all pages that belong to the table
    const auto &pages = table->GetPages();
    for (page_id_t pid : pages) {
        DumpPage(&bpm, pid);
    }

    return 0;
}
