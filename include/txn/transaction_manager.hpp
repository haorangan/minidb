//
// Created by Haoran Gan on 11/23/25.
//

#pragma once

#include "log/log_manager.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/disk_manager.hpp"

#include <cstdint>

// One transaction at a time (single session, so execution is serial).
//
// Atomicity: no-steal buffer pool + in-memory before-images for rollback.
// Durability: at commit, after-images of modified pages and a COMMIT record
// are appended to the WAL and fsync'd before Commit() returns.
class TransactionManager {
public:
    TransactionManager(BufferPoolManager *bpm, DiskManager *disk, LogManager *log,
                       uint64_t checkpoint_bytes)
        : bpm_(bpm), disk_(disk), log_(log), checkpoint_bytes_(checkpoint_bytes) {}

    void Begin();

    // Durable once this returns. May run a checkpoint afterwards.
    void Commit();

    // Restores every modified page. The caller must rebuild anything cached
    // from those pages (the catalog).
    void Rollback();

    bool InTransaction() const { return active_; }

    // Write all committed pages to the data file, fsync it, empty the WAL.
    // Only valid between transactions.
    void Checkpoint();

private:
    BufferPoolManager *bpm_;
    DiskManager *disk_;
    LogManager *log_;
    uint64_t checkpoint_bytes_;

    bool active_{false};
    uint64_t txn_id_{0};
    uint64_t next_txn_id_{1};
};
