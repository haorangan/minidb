//
// Created by Haoran Gan on 11/23/25.
//

#include "txn/transaction_manager.hpp"

#include "common/crash_point.hpp"

#include <stdexcept>

void TransactionManager::Begin() {
    if (active_) {
        throw std::logic_error("Transaction already in progress");
    }
    txn_id_ = next_txn_id_++;
    active_ = true;
    bpm_->BeginTracking();
}

void TransactionManager::Commit() {
    if (!active_) {
        throw std::logic_error("No transaction in progress");
    }

    // Read-only transactions have nothing to log
    if (bpm_->NumTrackedPages() > 0) {
        bpm_->ForEachTrackedPage([this](page_id_t pid, const char *data) {
            log_->AppendPageImage(txn_id_, pid, data);
            CRASH_POINT("txn.mid_log");
        });
        CRASH_POINT("txn.before_commit_record");
        log_->AppendCommit(txn_id_);  // fsync: the commit point
        CRASH_POINT("txn.after_commit");
    }

    // Committed pages may now be evicted (they are safe in the WAL)
    bpm_->EndTracking();
    active_ = false;

    if (log_->SizeBytes() >= checkpoint_bytes_) {
        Checkpoint();
    }
}

void TransactionManager::Rollback() {
    if (!active_) {
        throw std::logic_error("No transaction in progress");
    }
    bpm_->RestoreBeforeImages();
    active_ = false;
}

void TransactionManager::Checkpoint() {
    if (active_) {
        throw std::logic_error("Checkpoint during a transaction");
    }
    bpm_->FlushAllPages();
    CRASH_POINT("checkpoint.after_flush");
    disk_->Sync();
    CRASH_POINT("checkpoint.before_truncate");
    log_->Truncate();
}
