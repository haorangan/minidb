//
// Created by Haoran Gan on 11/15/25.
//

#include "execution/seq_scan_executor.hpp"

SeqScanExecutor::SeqScanExecutor(BufferPoolManager *bpm, TableHeap *table)
    : bpm_(bpm),
      table_(table),
      pages_(table_->GetPages()),
      page_idx_(0),
      slot_idx_(0) {}

bool SeqScanExecutor::Next(Tuple &out, RID &rid) {
    char buf[256];
    uint16_t sz = 0;

    while (page_idx_ < pages_.size()) {
        page_id_t pid = pages_[page_idx_];

        Page *page = bpm_->FetchPage(pid);
        if (!page) {
            return false;
        }

        SlottedPage sp(page);
        uint16_t num_slots = sp.GetNumSlots();

        while (slot_idx_ < num_slots) {
            RID r(pid, slot_idx_);
            ++slot_idx_;  // advance for next call

            if (!sp.GetTuple(r, buf, sz)) {
                continue;  // deleted/invalid slot
            }

            // For now, expect exactly two int32_t values
            if (sz == sizeof(int32_t) * 2) {
                auto *vals = reinterpret_cast<int32_t *>(buf);
                out = Tuple(vals[0], vals[1]);
                rid = r;
                bpm_->UnpinPage(pid, /*dirty=*/false);
                return true;
            }
        }

        // Done with this page, move to next
        bpm_->UnpinPage(pid, /*dirty=*/false);
        ++page_idx_;
        slot_idx_ = 0;
    }

    return false; // no more pages
}
