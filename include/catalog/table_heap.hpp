//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/buffer_pool_manager.hpp"
#include "storage/slotted_page.hpp"
#include "common/types.hpp"

#include <vector>

class TableHeap {
public:
    // Create a brand-new empty table (allocates first page)
    static TableHeap *Create(BufferPoolManager *bpm);

    // Open an existing table by walking its page chain from the first page
    static TableHeap *Open(BufferPoolManager *bpm, page_id_t first_page_id);

    TableHeap(BufferPoolManager *bpm, const std::vector<page_id_t> &pages);

    // Insert raw tuple bytes; returns false if it somehow can’t insert
    bool InsertTuple(const char *data, uint16_t size, RID &rid);

    // Simple full scan: expose list of page_ids so an iterator can scan them
    const std::vector<page_id_t> &GetPages() const { return pages_; }
    page_id_t GetFirstPageId() const { return pages_.front(); }

private:
    BufferPoolManager *bpm_;
    std::vector<page_id_t> pages_;  // page_ids that belong to this table
};

