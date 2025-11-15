#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "catalog/table_heap.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <iostream>

int main() {
    DiskManager dm("data.db");
    BufferPoolManager bpm(10, &dm);

    // Create a new table
    TableHeap *table = TableHeap::Create(&bpm);

    // Insert some rows: (i, i*10)
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

    std::cout << "--- SeqScan + Filter (a % 2 == 0) ---\n";

    SeqScanExecutor scan(&bpm, table);

    FilterExecutor filter(&scan, [](const Tuple &t) {
        return (t.a % 2) == 0;  // WHERE a is even
    });

    Tuple t;
    RID rid;
    while (filter.Next(t, rid)) {
        std::cout << "RID(page=" << rid.page_id
                  << ", slot=" << rid.slot_id << ") -> ";
        t.Print();
        std::cout << "\n";
    }

    return 0;
}