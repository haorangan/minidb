#include "storage/disk_manager.hpp"
#include "log/log_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "catalog/table_heap.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <iostream>
#include <vector>

int main() {
    DiskManager dm("data.db");
    LogManager log("wal.log", &dm);
    log.Recover();

    BufferPoolManager bpm(10, &dm, &log);

    // Create table
    TableHeap *table = TableHeap::Create(&bpm);

    // Insert rows
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

    // Build index on column a
    BPlusTreeIndex index(&bpm);
    index.Init();
    for (int i = 0; i < 20; ++i) {
        index.Insert(i, inserted_rids[i]);
    }

    std::cout << "--- IndexScanExecutor POINT (a = 7) ---\n";
    {
        IndexScanExecutor idx_scan(&bpm, table, &index, /*key=*/7);
        Tuple t;
        RID rid;
        while (idx_scan.Next(t, rid)) {
            std::cout << "RID(page=" << rid.page_id
                      << ", slot=" << rid.slot_id << ") -> ";
            t.Print();
            std::cout << "\n";
        }
    }

    std::cout << "\n--- IndexScanExecutor RANGE (5 <= a <= 12) ---\n";
    {
        IndexScanExecutor idx_scan(&bpm, table, &index, /*low=*/5, /*high=*/12);
        Tuple t;
        RID rid;
        while (idx_scan.Next(t, rid)) {
            std::cout << "RID(page=" << rid.page_id
                      << ", slot=" << rid.slot_id << ") -> ";
            t.Print();
            std::cout << "\n";
        }
    }

    return 0;
}
