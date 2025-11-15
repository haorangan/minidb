//
// Created by Haoran Gan on 11/14/25.
//

#include <utility>

#include "../include/storage/disk_manager.hpp"

DiskManager::DiskManager(std::string filename) : filename_(std::move(filename)) {
    db_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
    if (!db_io_.is_open()) {
        db_io_.clear();
        db_io_.open(filename_, std::ios::out | std::ios::binary);
        db_io_.close();
        db_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
    }

    db_io_.seekg(0, std::ios::end);
    if (const auto size = db_io_.tellg(); size > 0) {
        next_page_id_ = static_cast<page_id_t>(size / PAGE_SIZE);
    }
}

DiskManager::~DiskManager() {
    db_io_.flush();
    db_io_.close();
}