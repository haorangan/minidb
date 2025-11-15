//
// Created by Haoran Gan on 11/14/25.
//

#include "log/log_manager.hpp"

#include <iostream>
#include <utility>
#include <vector>

LogManager::LogManager(std::string log_filename, DiskManager *disk)
    : filename_(std::move(log_filename)), disk_(disk) {
    // Open the log in read/write, appending mode. Create if missing.
    log_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!log_io_.is_open()) {
        // Create new empty file
        log_io_.clear();
        std::ofstream create_file(filename_, std::ios::binary);
        create_file.close();
        log_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
}

LogManager::~LogManager() {
    if (log_io_.is_open()) {
        log_io_.flush();
        log_io_.close();
    }
}

void LogManager::AppendPageWrite(page_id_t page_id, const char *page_data) {
    if (!log_io_.is_open()) {
        return;
    }

    // Seek to end (append)
    log_io_.seekp(0, std::ios::end);

    // Write page_id
    log_io_.write(reinterpret_cast<const char *>(&page_id), sizeof(page_id_t));

    // Write full page image
    log_io_.write(page_data, PAGE_SIZE);

    // Ensure it's on disk (for a toy DB, flush is fine)
    log_io_.flush();
}

void LogManager::Recover() {
    // Close the append stream and reopen for reading from start
    if (log_io_.is_open()) {
        log_io_.flush();
        log_io_.close();
    }

    std::ifstream in(filename_, std::ios::binary);
    if (!in.is_open()) {
        // No log file, nothing to do
        log_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
        return;
    }

    std::cout << "[WAL] Starting recovery from log: " << filename_ << "\n";

    while (true) {
        page_id_t pid;
        if (!in.read(reinterpret_cast<char *>(&pid), sizeof(pid))) {
            break;  // EOF
        }

        std::vector<char> buf(PAGE_SIZE);
        if (!in.read(buf.data(), PAGE_SIZE)) {
            break;  // truncated? ignore partial
        }

        // Redo: write this page image into the data file
        disk_->WritePage(pid, buf.data());
    }

    in.close();

    // Truncate the log (we've replayed everything)
    std::ofstream trunc(filename_, std::ios::binary | std::ios::trunc);
    trunc.close();

    // Reopen log for append after recovery
    log_io_.open(filename_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    std::cout << "[WAL] Recovery complete; log truncated.\n";
}
