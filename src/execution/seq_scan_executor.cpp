//
// Created by Haoran Gan on 11/15/25.
//

#include "execution/seq_scan_executor.hpp"

SeqScanExecutor::SeqScanExecutor(BufferPoolManager *bpm, TableHeap *table, std::string table_name)
    : bpm_(bpm),
      table_(table),
      table_name_(std::move(table_name)),
      pages_(table_->GetPages()),
      page_idx_(0),
      slot_idx_(0) {}

void SeqScanExecutor::Init() {
    page_idx_ = 0;
    slot_idx_ = 0;
}

bool SeqScanExecutor::Next(Tuple &out, RID &rid) {
    char buf[PAGE_SIZE];  // a tuple can never be larger than a page
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

            // New: interpret as N int32_t values
            if (sz % sizeof(int32_t) != 0) {
                continue; // skip malformed
            }
            size_t count = sz / sizeof(int32_t);
            auto *vals = reinterpret_cast<int32_t *>(buf);

            out = Tuple::FromRawInts(vals, count);
            rid = r;
            bpm_->UnpinPage(pid, /*dirty=*/false);
            return true;
        }

        bpm_->UnpinPage(pid, /*dirty=*/false);
        ++page_idx_;
        slot_idx_ = 0;
    }

    return false; // no more pages
}
