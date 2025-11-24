//
// Created by Haoran Gan on 11/23/25.
//

#pragma once

#include "catalog/catalog.hpp"
#include "catalog/schema.hpp"
#include "common/tuple.hpp"
#include "log/log_manager.hpp"
#include "sql/planner.hpp"
#include "storage/buffer_pool_manager.hpp"
#include "storage/disk_manager.hpp"
#include "txn/transaction_manager.hpp"

#include <string>
#include <vector>

struct QueryResult {
    std::vector<Tuple> rows;  // SELECT output
    Schema schema;            // names of the SELECT output columns
    std::string message;      // for other statements, e.g. "Table 't' created."
};

// A database on disk: data file + WAL. Opening it runs crash recovery.
//
// Statements outside BEGIN ... COMMIT run in their own transaction
// (autocommit). If a statement fails, its transaction is rolled back; inside
// an explicit transaction that means the whole transaction.
class Database {
public:
    struct Options {
        size_t pool_size = 256;                 // buffer pool frames
        uint64_t checkpoint_bytes = 16u << 20;  // checkpoint once the WAL is this big
    };

    Database(const std::string &db_file, const std::string &wal_file);
    Database(const std::string &db_file, const std::string &wal_file, Options options);

    // Rolls back an open transaction, then checkpoints
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Run one SQL statement. Throws (after rolling back) on error.
    QueryResult Execute(const std::string &sql);

    bool InTransaction() const { return txn_.InTransaction(); }
    size_t RecoveredTransactions() const { return recovered_; }

    Catalog &GetCatalog() { return catalog_; }
    BufferPoolManager &GetBufferPool() { return bpm_; }

private:
    DiskManager disk_;
    LogManager log_;
    size_t recovered_;
    BufferPoolManager bpm_;
    Catalog catalog_;
    TransactionManager txn_;
    Planner planner_;
    bool explicit_txn_{false};

    template <typename Fn>
    void RunWrite(Fn &&fn);

    void RollbackAndReload();
};
