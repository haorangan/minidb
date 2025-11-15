//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/types.hpp"
#include <fstream>
#include <string>

class DiskManager {
public:
    explicit DiskManager(std::string filename);
    ~DiskManager();

    void ReadPage(page_id_t page_id, char *page_data);
    void WritePage(page_id_t page_id, const char *page_data);

    page_id_t AllocatePage();

private:
    std::fstream db_io_;
    std::string filename_;
    page_id_t next_page_id_{0};

    void EnsureFileSize(std::streamoff size);
};
