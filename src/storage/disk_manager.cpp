//
// Created by Haoran Gan on 11/14/25.
//


#include "storage/disk_manager.hpp"

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

void DiskManager::EnsureFileSize(std::streamoff size) {
    db_io_.seekp(0, std::ios::end);
    if (auto cur = db_io_.tellp(); cur < size) {
        const std::string zeros(PAGE_SIZE, '\0');
        while (cur < size) {
            db_io_.write(zeros.data(), PAGE_SIZE);
            cur += PAGE_SIZE;
        }
        db_io_.flush();
    }
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    db_io_.seekg(0, std::ios::end);
    auto size = db_io_.tellg();
    if (offset >= size) {
        std::memset(page_data, 0, PAGE_SIZE);
        return;
    }
    db_io_.seekg(offset);
    db_io_.read(page_data, PAGE_SIZE);
    if (db_io_.gcount() < PAGE_SIZE) {
        std::memset(page_data + db_io_.gcount(), 0, PAGE_SIZE - db_io_.gcount());
    }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    EnsureFileSize(offset + PAGE_SIZE);
    db_io_.seekp(offset);
    db_io_.write(page_data, PAGE_SIZE);
    db_io_.flush();
}

page_id_t DiskManager::AllocatePage() {
    return next_page_id_++;
}