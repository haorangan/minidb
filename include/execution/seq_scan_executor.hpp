//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "catalog/table_heap.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <vector>

class SeqScanExecutor {
public:
    SeqScanExecutor(BufferPoolManager *bpm, TableHeap *table);

    // Get next tuple; returns false when no more
    bool Next(Tuple &out, RID &rid);

private:
    BufferPoolManager *bpm_;
    TableHeap *table_;
    const std::vector<page_id_t> &pages_;
    size_t page_idx_;
    uint16_t slot_idx_;
};
