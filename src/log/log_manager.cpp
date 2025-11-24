//
// Created by Haoran Gan on 11/14/25.
//

#include "log/log_manager.hpp"

#include "common/crash_point.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

constexpr size_t HEADER_SIZE = 4 + 4 + 8 + 4 + 4;

// Standard CRC-32 (IEEE, reflected), table-driven
uint32_t Crc32(const char *data, size_t len) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::runtime_error IoError(const std::string &what, const std::string &file) {
    return std::runtime_error(what + " " + file + ": " + std::strerror(errno));
}

void WriteAll(int fd, const char *data, size_t len, const std::string &file) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = ::write(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw IoError("LogManager: write failed on", file);
        }
        done += static_cast<size_t>(n);
    }
}

}  // namespace

LogManager::LogManager(std::string filename) : filename_(std::move(filename)) {
    fd_ = ::open(filename_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        throw IoError("LogManager: cannot open", filename_);
    }
    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        throw IoError("LogManager: cannot stat", filename_);
    }
    size_ = static_cast<uint64_t>(st.st_size);
}

LogManager::~LogManager() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void LogManager::AppendRecord(RecordType type, uint64_t txn_id, page_id_t page_id,
                              const char *payload, uint32_t payload_len) {
    std::vector<char> buf(HEADER_SIZE + payload_len);
    auto type_raw = static_cast<uint32_t>(type);
    std::memcpy(buf.data() + 4, &type_raw, 4);
    std::memcpy(buf.data() + 8, &txn_id, 8);
    std::memcpy(buf.data() + 16, &page_id, 4);
    std::memcpy(buf.data() + 20, &payload_len, 4);
    if (payload_len > 0) {
        std::memcpy(buf.data() + HEADER_SIZE, payload, payload_len);
    }
    uint32_t crc = Crc32(buf.data() + 4, buf.size() - 4);
    std::memcpy(buf.data(), &crc, 4);

    // Torn write: only part of the record reaches the log before the crash
    if (CrashPointHit("wal.torn_write")) {
        WriteAll(fd_, buf.data(), buf.size() / 2, filename_);
        CrashNow();
    }

    WriteAll(fd_, buf.data(), buf.size(), filename_);
    size_ += buf.size();
}

void LogManager::Sync() {
    if (::fsync(fd_) != 0) {
        throw IoError("LogManager: fsync failed on", filename_);
    }
}

void LogManager::AppendPageImage(uint64_t txn_id, page_id_t page_id, const char *page_data) {
    if (txn_id != txn_id_) {
        txn_id_ = txn_id;
        txn_start_ = size_;
    }
    AppendRecord(RecordType::PAGE, txn_id, page_id, page_data, PAGE_SIZE);
}

void LogManager::AppendCommit(uint64_t txn_id) {
    AppendRecord(RecordType::COMMIT, txn_id, -1, nullptr, 0);
    CRASH_POINT("wal.before_sync");

    // Power loss before fsync can persist writes out of order: here the COMMIT
    // record reached disk but a sector of an earlier page image did not. The
    // CRC must catch it (the commit was never acknowledged, so dropping it is fine).
    if (CrashPointHit("wal.lost_write")) {
        int fd = ::open(filename_.c_str(), O_WRONLY);  // fd_ is O_APPEND; pwrite needs a plain fd
        const char zeros[512] = {};
        (void)::pwrite(fd, zeros, sizeof(zeros), static_cast<off_t>(txn_start_ + HEADER_SIZE + 1024));
        CrashNow();
    }

    Sync();
}

void LogManager::Truncate() {
    if (::ftruncate(fd_, 0) != 0) {
        throw IoError("LogManager: truncate failed on", filename_);
    }
    Sync();
    size_ = 0;
}

size_t LogManager::Recover(DiskManager *disk) {
    // Read the whole log; it is bounded by the checkpoint threshold
    std::vector<char> log(size_);
    size_t got = 0;
    while (got < log.size()) {
        ssize_t n = ::pread(fd_, log.data() + got, log.size() - got, static_cast<off_t>(got));
        if (n < 0) {
            if (errno == EINTR) continue;
            throw IoError("LogManager: read failed on", filename_);
        }
        if (n == 0) break;
        got += static_cast<size_t>(n);
    }

    // Page images wait here until their transaction's COMMIT record shows up
    std::unordered_map<uint64_t, std::vector<std::pair<page_id_t, size_t>>> pending;
    size_t replayed = 0;
    size_t pos = 0;

    while (pos + HEADER_SIZE <= got) {
        uint32_t crc, type_raw, payload_len;
        uint64_t txn_id;
        page_id_t page_id;
        std::memcpy(&crc, log.data() + pos, 4);
        std::memcpy(&type_raw, log.data() + pos + 4, 4);
        std::memcpy(&txn_id, log.data() + pos + 8, 8);
        std::memcpy(&page_id, log.data() + pos + 16, 4);
        std::memcpy(&payload_len, log.data() + pos + 20, 4);

        // A record that runs past the end or fails its checksum is a torn
        // tail from a crash mid-append: everything from here on is ignored.
        if (payload_len > PAGE_SIZE || pos + HEADER_SIZE + payload_len > got) break;
        if (Crc32(log.data() + pos + 4, HEADER_SIZE - 4 + payload_len) != crc) break;

        auto type = static_cast<RecordType>(type_raw);
        if (type == RecordType::PAGE && payload_len == PAGE_SIZE) {
            pending[txn_id].emplace_back(page_id, pos + HEADER_SIZE);
        } else if (type == RecordType::COMMIT) {
            for (const auto &[pid, offset] : pending[txn_id]) {
                disk->WritePage(pid, log.data() + offset);
            }
            pending.erase(txn_id);
            ++replayed;
        } else {
            break;  // unknown record: treat as corruption
        }
        pos += HEADER_SIZE + payload_len;
    }

    // Make the redone pages durable before throwing the log away
    disk->Sync();
    CRASH_POINT("recovery.before_truncate");
    Truncate();
    return replayed;
}
