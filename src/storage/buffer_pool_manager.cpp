//
// Created by Haoran Gan on 11/14/25.
//

#include "../include/storage/buffer_pool_manager.hpp"

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk)
    : pool_size_(pool_size), disk_(disk), pages_(pool_size) {
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(i);
        lru_pos_[i] = std::prev(lru_list_.end());
    }
}

size_t BufferPoolManager::PickVictimFrame() {
    if (lru_list_.empty()) {
        throw std::runtime_error("No free frames");
    }
    size_t frame = lru_list_.front();
    lru_list_.pop_front();
    lru_pos_.erase(frame);
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
