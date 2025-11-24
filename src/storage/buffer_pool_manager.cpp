//
// Created by Haoran Gan on 11/14/25.
//

#include "storage/buffer_pool_manager.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk)
    : pool_size_(pool_size), disk_(disk), pages_(pool_size) {
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(i);
        lru_pos_[i] = std::prev(lru_list_.end());
    }
}

// Least recently used unpinned frame whose page isn't held by the open
// transaction (no-steal).
size_t BufferPoolManager::PickVictimFrame() {
    for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it) {
        size_t frame = *it;
        page_id_t pid = pages_[frame].GetPageId();
        if (pid != -1 && before_images_.count(pid) != 0) {
            continue;
        }
        lru_list_.erase(it);
        lru_pos_.erase(frame);
        assert(pages_[frame].GetPinCount() == 0);
        return frame;
    }
    throw std::runtime_error(lru_list_.empty()
        ? "No free unpinned frames"
        : "Transaction too large: every evictable page is held by the open transaction");
}

void BufferPoolManager::TouchFrame(size_t frame_id) {
    auto it = lru_pos_.find(frame_id);
    if (it != lru_pos_.end()) {
        lru_list_.erase(it->second);
    }
    lru_list_.push_back(frame_id);
    lru_pos_[frame_id] = std::prev(lru_list_.end());
}

// Evict a victim (writing it back if dirty) and pin `page_id` in its frame
size_t BufferPoolManager::LoadIntoFrame(page_id_t page_id, bool read_from_disk) {
    size_t frame = PickVictimFrame();
    Page &victim = pages_[frame];

    if (victim.GetPageId() != -1) {
        // Only committed data can be dirty here; it is already in the WAL
        if (victim.IsDirty()) {
            disk_->WritePage(victim.GetPageId(), victim.GetData());
            victim.SetDirty(false);
        }
        page_table_.erase(victim.GetPageId());
    }

    victim.ResetMemory();
    if (read_from_disk) {
        disk_->ReadPage(page_id, victim.GetData());
    }
    victim.SetPageId(page_id);
    victim.SetDirty(false);
    victim.Pin();  // pinned, so NOT in LRU

    page_table_[page_id] = frame;
    return frame;
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    Page *page;
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        size_t frame = it->second;
        page = &pages_[frame];
        page->Pin();

        // Remove from LRU because it's now in use (pinned)
        auto pos_it = lru_pos_.find(frame);
        if (pos_it != lru_pos_.end()) {
            lru_list_.erase(pos_it->second);
            lru_pos_.erase(pos_it);
        }
    } else {
        page = &pages_[LoadIntoFrame(page_id, /*read_from_disk=*/true)];
    }

    // The caller may modify the page: remember how it looked first
    if (tracking_ && before_images_.count(page_id) == 0 && candidates_.count(page_id) == 0) {
        PageImage copy(new char[PAGE_SIZE]);
        std::memcpy(copy.get(), page->GetData(), PAGE_SIZE);
        candidates_.emplace(page_id, std::move(copy));
    }
    return page;
}

Page *BufferPoolManager::NewPage(page_id_t &page_id) {
    page_id = disk_->AllocatePage();
    Page *page = &pages_[LoadIntoFrame(page_id, /*read_from_disk=*/false)];

    // A brand-new page's before-image is all zeros
    if (tracking_ && before_images_.count(page_id) == 0) {
        PageImage zeros(new char[PAGE_SIZE]());
        before_images_.emplace(page_id, std::move(zeros));
    }
    return page;
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

    if (tracking_) {
        auto cand = candidates_.find(page_id);
        if (is_dirty && cand == candidates_.end() && before_images_.count(page_id) == 0) {
            throw std::logic_error("Page modified in a transaction without a saved before-image");
        }
        if (is_dirty && cand != candidates_.end() && before_images_.count(page_id) == 0) {
            before_images_.emplace(page_id, std::move(cand->second));
            candidates_.erase(cand);
        } else if (cand != candidates_.end() && page.GetPinCount() == 0) {
            candidates_.erase(cand);  // read-only use; nothing to undo
        }
    }

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

void BufferPoolManager::FlushAllPages() {
    if (tracking_) {
        throw std::logic_error("FlushAllPages during a transaction would write uncommitted data");
    }
    for (const auto &entry : page_table_) {
        if (pages_[entry.second].IsDirty()) {
            FlushPage(entry.first);
        }
    }
}

// ---- Transaction support ----

void BufferPoolManager::BeginTracking() {
    // A page pinned across the boundary could be modified without a before-image
    for (const Page &page : pages_) {
        if (page.GetPinCount() != 0) {
            throw std::logic_error("BeginTracking with pinned pages");
        }
    }
    tracking_ = true;
    before_images_.clear();
    candidates_.clear();
}

void BufferPoolManager::EndTracking() {
    tracking_ = false;
    before_images_.clear();
    candidates_.clear();
}

void BufferPoolManager::RestoreBeforeImages() {
    for (const auto &[pid, image] : before_images_) {
        auto it = page_table_.find(pid);
        assert(it != page_table_.end());  // no-steal: tracked pages stay resident
        Page &page = pages_[it->second];
        std::memcpy(page.GetData(), image.get(), PAGE_SIZE);
        // Content now matches the last committed state, which is either on
        // disk or in the WAL; writing it back later is harmless.
        page.SetDirty(true);
    }
    EndTracking();
}

void BufferPoolManager::ForEachTrackedPage(const std::function<void(page_id_t, const char *)> &fn) {
    for (const auto &entry : before_images_) {
        auto it = page_table_.find(entry.first);
        assert(it != page_table_.end());
        fn(entry.first, pages_[it->second].GetData());
    }
}
