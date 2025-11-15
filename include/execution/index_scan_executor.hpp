//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "index/bplus_tree_index.hpp"
#include "catalog/table_heap.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <vector>

class IndexScanExecutor {
public:
    enum class ScanType {
        POINT,
        RANGE
    };

    // Point lookup: key == value
    IndexScanExecutor(BufferPoolManager *bpm,
                      TableHeap *table,
                      BPlusTreeIndex *index,
                      int32_t key);

    // Range lookup: low <= key <= high
    IndexScanExecutor(BufferPoolManager *bpm,
                      TableHeap *table,
                      BPlusTreeIndex *index,
                      int32_t low_key,
                      int32_t high_key);

    // Get next matching tuple; returns false when exhausted
    bool Next(Tuple &out, RID &rid);

private:
    BufferPoolManager *bpm_;
    TableHeap *table_;
    BPlusTreeIndex *index_;

    ScanType scan_type_;
    int32_t point_key_;
    int32_t low_key_;
    int32_t high_key_;

    bool initialized_{false};
    std::vector<RID> matches_;
    size_t match_idx_{0};

    void InitMatches();
};

