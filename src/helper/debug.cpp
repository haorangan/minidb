//
// Created by Haoran Gan on 11/14/25.
//
#include <iostream>
#include "storage/slotted_page.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "common/types.hpp"

void DumpPage(BufferPoolManager *bpm, page_id_t pid) {
    Page *page = bpm->FetchPage(pid);
    if (!page) {
        std::cerr << "DumpPage: failed to fetch page " << pid << "\n";
        return;
    }

    SlottedPage sp(page);

    uint16_t num_slots = sp.GetNumSlots();
    uint16_t free_off  = sp.GetFreeSpaceOffset();
    uint16_t free_end  = sp.GetFreeSpaceEnd();

    std::cout << "=== Dumping page " << pid << " ===\n";
    std::cout << "num_slots       = " << num_slots << "\n";
    std::cout << "slot_dir_end    = " << free_off << "\n";
    std::cout << "free_space_end  = " << free_end << "\n";
    std::cout << "free_space_size = " << (free_end > free_off ? (free_end - free_off) : 0)
              << " bytes\n";

    char buf[256];
    uint16_t sz = 0;

    for (uint16_t slot_id = 0; slot_id < num_slots; ++slot_id) {
        RID rid(pid, slot_id);
        if (!sp.GetTuple(rid, buf, sz)) {
            std::cout << "  slot " << slot_id << " : <deleted or invalid>\n";
            continue;
        }

        std::cout << "  slot " << slot_id
                  << " : size=" << sz;

        // If you know tuples are two int32_t values, decode them:
        if (sz == sizeof(int32_t) * 2) {
            auto *vals = reinterpret_cast<int32_t *>(buf);
            std::cout << "  values=(" << vals[0] << ", " << vals[1] << ")";
        }

        std::cout << "\n";
    }

    std::cout << "=== End page " << pid << " ===\n\n";

    bpm->UnpinPage(pid, /*is_dirty=*/false);
}