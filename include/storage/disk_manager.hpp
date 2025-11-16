//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/types.hpp"
#include <string>

// Page-granular access to the database file via POSIX pread/pwrite.
class DiskManager {
public:
    explicit DiskManager(std::string filename);
    ~DiskManager();

    DiskManager(const DiskManager &) = delete;
    DiskManager &operator=(const DiskManager &) = delete;

    // Pages past the end of the file read back as zeros
    void ReadPage(page_id_t page_id, char *page_data);
    void WritePage(page_id_t page_id, const char *page_data);

    page_id_t AllocatePage();

private:
    int fd_{-1};
    std::string filename_;
    page_id_t next_page_id_{0};
};
