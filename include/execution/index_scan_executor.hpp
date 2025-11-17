//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "execution/executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "catalog/table_heap.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <string>
#include <vector>

class IndexScanExecutor : public Executor {
public:
    enum class ScanType {
        POINT,
        RANGE
    };

    // Point lookup: key == value
    IndexScanExecutor(BufferPoolManager *bpm,
                      TableHeap *table,
                      BPlusTreeIndex *index,
                      int32_t key,
                      std::string label = "");

    // Range lookup: low <= key <= high
    IndexScanExecutor(BufferPoolManager *bpm,
                      TableHeap *table,
                      BPlusTreeIndex *index,
                      int32_t low_key,
                      int32_t high_key,
                      std::string label = "");

    void Init() override;

    // Get next matching tuple; returns false when exhausted
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "IndexScan(" + label_ + ")"; }

private:
    BufferPoolManager *bpm_;
    BPlusTreeIndex *index_;
    std::string label_;

    ScanType scan_type_;
    int32_t point_key_;
    int32_t low_key_;
    int32_t high_key_;

    bool initialized_{false};
    std::vector<RID> matches_;
    size_t match_idx_{0};

    void InitMatches();
};

