//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/types.hpp"
#include "storage/disk_manager.hpp"

#include <fstream>
#include <string>

class LogManager {
public:
    LogManager(std::string log_filename, DiskManager *disk);
    ~LogManager();

    // Append a log record for a page write (page_id + full 4KB image).
    // Invariant (for WAL): call this BEFORE writing the page to disk.
    void AppendPageWrite(page_id_t page_id, const char *page_data);

    // Redo all logged page writes into the data file, then truncate the log.
    void Recover();

private:
    std::string filename_;
    DiskManager *disk_;
    std::fstream log_io_;
};
