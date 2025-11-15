//
// Created by Haoran Gan on 11/14/25.
//

#include "storage/buffer_pool_manager.hpp"

#include <cassert>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk)
    : pool_size_(pool_size), disk_(disk), pages_(pool_size) {
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(i);
        lru_pos_[i] = std::prev(lru_list_.end());
    }
}

size_t BufferPoolManager::PickVictimFrame() {
    if (lru_list_.empty()) {
        throw std::runtime_error("No free unpinned frames");
    }

    size_t frame = lru_list_.front();
    lru_list_.pop_front();
    lru_pos_.erase(frame);

    assert(pages_[frame].GetPinCount() == 0);

    return frame;
}

void BufferPoolManager::TouchFrame(size_t frame_id) {
    auto it = lru_pos_.find(frame_id);
    if (it != lru_pos_.end()) {
        lru_list_.erase(it->second);
    }
    lru_list_.push_back(frame_id);
    lru_pos_[frame_id] = std::prev(lru_list_.end());
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        size_t frame = it->second;
        Page &page = pages_[frame];

        page.Pin();

        // Remove from LRU because it's now in use (pinned)
        auto pos_it = lru_pos_.find(frame);
        if (pos_it != lru_pos_.end()) {
            lru_list_.erase(pos_it->second);
            lru_pos_.erase(pos_it);
        }

        return &page;
    }

    // Need to bring page into memory: pick a victim frame that is unpinned
    size_t frame = PickVictimFrame();
    Page &victim = pages_[frame];

    if (victim.GetPageId() != -1) {
        if (victim.IsDirty()) {
            disk_->WritePage(victim.GetPageId(), victim.GetData());
        }
        page_table_.erase(victim.GetPageId());
    }

    victim.ResetMemory();
    disk_->ReadPage(page_id, victim.GetData());
    victim.SetPageId(page_id);
    victim.SetDirty(false);
    victim.Pin();  // pinned, so NOT in LRU

    page_table_[page_id] = frame;

    return &victim;
}

Page *BufferPoolManager::NewPage(page_id_t &page_id) {
    size_t frame = PickVictimFrame();
    Page &victim = pages_[frame];

    if (victim.GetPageId() != -1) {
        if (victim.IsDirty()) {
            disk_->WritePage(victim.GetPageId(), victim.GetData());
        }
        page_table_.erase(victim.GetPageId());
    }

    victim.ResetMemory();
    page_id = disk_->AllocatePage();
    victim.SetPageId(page_id);
    victim.SetDirty(false);
    victim.Pin();  // pinned, so keep it out of LRU

    page_table_[page_id] = frame;

    return &victim;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    size_t frame = it->second;
    Page &page = pages_[frame];

    if (page.GetPinCount() <= 0) {
        return false;
    }

    page.Unpin();
    if (is_dirty) page.SetDirty(true);


    if (page.GetPinCount() == 0) {
        if (lru_pos_.find(frame) == lru_pos_.end()) {
            lru_list_.push_back(frame);
            lru_pos_[frame] = std::prev(lru_list_.end());
        }
    }

    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page &page = pages_[it->second];
    disk_->WritePage(page_id, page.GetData());
    page.SetDirty(false);
    return true;
}
