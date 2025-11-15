#include "storage/disk_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "catalog/table_heap.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <iostream>
#include <vector>

int main() {
    DiskManager dm("data.db");

    // WAL manager, bound to this disk + wal.log
    LogManager log("wal.log", &dm);

    // Crash recovery: replay any logged page writes
    log.Recover();

    // Now create buffer pool, wired to both disk and log
    BufferPoolManager bpm(10, &dm, &log);

    // Create a new table
    TableHeap *table = TableHeap::Create(&bpm);

    // Insert some rows: (i, i*10)
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

    // Build a simple B+tree-style index on column a
    BPlusTreeIndex index(&bpm);
    index.Init();

    // Insert (key=a, rid) into the index
    for (int i = 0; i < 20; ++i) {
        int32_t key = i;           // column a
        const RID &rid = inserted_rids[i];
        bool ok = index.Insert(key, rid);
        if (!ok) {
            std::cerr << "Index insert failed at key " << key << "\n";
        }
    }

    // ---- Point lookup ----
    std::cout << "--- Index point lookup: key = 7 ---\n";
    std::vector<RID> matches;
    index.SearchEqual(7, matches);

    for (const RID &rid : matches) {
        Page *page = bpm.FetchPage(rid.page_id);
        if (!page) continue;
        SlottedPage sp(page);
        char buf[64];
        uint16_t sz = 0;
        if (sp.GetTuple(rid, buf, sz) && sz == sizeof(int32_t) * 2) {
            auto *vals = reinterpret_cast<int32_t *>(buf);
            Tuple t(vals[0], vals[1]);
            std::cout << "RID(page=" << rid.page_id
                      << ", slot=" << rid.slot_id << ") -> ";
            t.Print();
            std::cout << "\n";
        }
        bpm.UnpinPage(rid.page_id, /*is_dirty=*/false);
    }

    // ---- Range lookup ----
    std::cout << "\n--- Index range lookup: 5 <= key <= 12 ---\n";
    matches.clear();
    index.SearchRange(5, 12, matches);

    for (const RID &rid : matches) {
        Page *page = bpm.FetchPage(rid.page_id);
        if (!page) continue;
        SlottedPage sp(page);
        char buf[64];
        uint16_t sz = 0;
        if (sp.GetTuple(rid, buf, sz) && sz == sizeof(int32_t) * 2) {
            auto *vals = reinterpret_cast<int32_t *>(buf);
            Tuple t(vals[0], vals[1]);
            std::cout << "RID(page=" << rid.page_id
                      << ", slot=" << rid.slot_id << ") -> ";
            t.Print();
            std::cout << "\n";
        }
        bpm.UnpinPage(rid.page_id, /*is_dirty=*/false);
    }

    return 0;
}