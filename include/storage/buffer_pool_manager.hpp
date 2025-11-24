//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/disk_manager.hpp"
#include "storage/page.hpp"
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk);

    Page *FetchPage(page_id_t page_id);
    Page *NewPage(page_id_t &page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);

    // helper
    void FlushAllPages();

    // ---- Transaction support (no-steal) ----
    //
    // While tracking, the first time a page is dirtied its before-image is
    // saved, and the page can't be evicted until tracking ends. Uncommitted
    // changes therefore never reach the data file, and rollback is just
    // copying the before-images back.

    void BeginTracking();
    void EndTracking();                          // commit: pages become evictable
    void RestoreBeforeImages();                  // rollback, then EndTracking()
    bool IsTracking() const { return tracking_; }

    // Visit every page the current transaction modified (all are resident)
    void ForEachTrackedPage(const std::function<void(page_id_t, const char *)> &fn);
    size_t NumTrackedPages() const { return before_images_.size(); }

private:
    using PageImage = std::unique_ptr<char[]>;

    size_t pool_size_;
    DiskManager *disk_;
    std::vector<Page> pages_;
    std::unordered_map<page_id_t, size_t> page_table_;

    // Simple LRU: list of frame_ids
    std::list<size_t> lru_list_;
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_pos_;

    bool tracking_{false};
    std::unordered_map<page_id_t, PageImage> before_images_;  // pages this txn modified
    std::unordered_map<page_id_t, PageImage> candidates_;     // pinned, maybe about to be modified

    size_t PickVictimFrame();
    size_t LoadIntoFrame(page_id_t page_id, bool read_from_disk);
    void TouchFrame(size_t frame_id);
};
