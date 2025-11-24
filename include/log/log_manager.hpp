//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/types.hpp"
#include "storage/disk_manager.hpp"

#include <cstdint>
#include <string>

// Redo-only write-ahead log of full page images.
//
// A transaction commits by appending one PAGE record per page it modified
// (the page's after-image), then a COMMIT record, then fsync'ing the log.
// Recovery replays the pages of transactions whose COMMIT record made it to
// disk and ignores everything else. Every record carries a CRC32, so a torn
// write at the tail of the log is detected and treated as the end of the log.
//
// Record layout: [crc32:4][type:4][txn_id:8][page_id:4][payload_len:4][payload]
// (crc covers everything after the crc field)
class LogManager {
public:
    explicit LogManager(std::string filename);
    ~LogManager();

    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    void AppendPageImage(uint64_t txn_id, page_id_t page_id, const char *page_data);

    // Append the COMMIT record and fsync. Once this returns, the transaction
    // survives a crash.
    void AppendCommit(uint64_t txn_id);

    // Redo every committed transaction into `disk`, fsync it, then empty the
    // log. Safe to rerun if it crashes partway (page images are idempotent).
    // Returns the number of transactions replayed.
    size_t Recover(DiskManager *disk);

    // Drop all records (after a checkpoint has made them redundant)
    void Truncate();

    uint64_t SizeBytes() const { return size_; }

private:
    enum class RecordType : uint32_t { PAGE = 1, COMMIT = 2 };

    std::string filename_;
    int fd_{-1};
    uint64_t size_{0};

    // Where the current transaction's records start (for fault injection)
    uint64_t txn_id_{0};
    uint64_t txn_start_{0};

    void AppendRecord(RecordType type, uint64_t txn_id, page_id_t page_id,
                      const char *payload, uint32_t payload_len);
    void Sync();
};
