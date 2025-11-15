//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/disk_manager.hpp"
#include "storage/page.hpp"
#include "log/log_manager.hpp"
#include <list>
#include <unordered_map>
#include <vector>

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk, LogManager *log);

    Page *FetchPage(page_id_t page_id);
    Page *NewPage(page_id_t &page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);

    // helper
    void FlushAllPages();

private:
    size_t pool_size_;
    DiskManager *disk_;
    std::vector<Page> pages_;
    std::unordered_map<page_id_t, size_t> page_table_;

    LogManager *log_;

    // Simple LRU: list of frame_ids
    std::list<size_t> lru_list_;
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_pos_;

    size_t PickVictimFrame();
    void TouchFrame(size_t frame_id);
};
