//
// Created by Haoran Gan on 11/14/25.
//


#include "storage/disk_manager.hpp"

#include "common/crash_point.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

static std::runtime_error IoError(const std::string &what, const std::string &file) {
    return std::runtime_error(what + " " + file + ": " + std::strerror(errno));
}

DiskManager::DiskManager(std::string filename) : filename_(std::move(filename)) {
    fd_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw IoError("DiskManager: cannot open", filename_);
    }

    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        throw IoError("DiskManager: cannot stat", filename_);
    }
    // Round up so a partially written last page is never handed out again
    next_page_id_ = static_cast<page_id_t>((st.st_size + PAGE_SIZE - 1) / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    size_t done = 0;
    while (done < PAGE_SIZE) {
        ssize_t n = ::pread(fd_, page_data + done, PAGE_SIZE - done, offset + static_cast<off_t>(done));
        if (n < 0) {
            if (errno == EINTR) continue;
            throw IoError("DiskManager: read failed on", filename_);
        }
        if (n == 0) break;  // end of file
        done += static_cast<size_t>(n);
    }
    // Unwritten pages (past EOF) read as zeros
    if (done < PAGE_SIZE) {
        std::memset(page_data + done, 0, PAGE_SIZE - done);
    }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    // Writing past EOF extends the file; any gap reads back as zeros
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;

    // Torn write: only half the page reaches the file before the crash
    if (CrashPointHit("disk.torn_write")) {
        (void)::pwrite(fd_, page_data, PAGE_SIZE / 2, offset);
        CrashNow();
    }

    size_t done = 0;
    while (done < PAGE_SIZE) {
        ssize_t n = ::pwrite(fd_, page_data + done, PAGE_SIZE - done, offset + static_cast<off_t>(done));
        if (n < 0) {
            if (errno == EINTR) continue;
            throw IoError("DiskManager: write failed on", filename_);
        }
        done += static_cast<size_t>(n);
    }

    // Recovery writes pages that may lie past the old end of file
    if (page_id >= next_page_id_) {
        next_page_id_ = page_id + 1;
    }
}

void DiskManager::Sync() {
    if (::fsync(fd_) != 0) {
        throw IoError("DiskManager: fsync failed on", filename_);
    }
}

page_id_t DiskManager::AllocatePage() {
    return next_page_id_++;
}
