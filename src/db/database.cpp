//
// Created by Haoran Gan on 11/23/25.
//

#include "db/database.hpp"

#include "execution/create_table_executor.hpp"
#include "execution/insert_executor.hpp"
#include "sql/lexer.hpp"
#include "sql/parser.hpp"

#include <stdexcept>

Database::Database(const std::string &db_file, const std::string &wal_file)
    : Database(db_file, wal_file, Options{}) {}

Database::Database(const std::string &db_file, const std::string &wal_file, Options options)
    : disk_(db_file),
      log_(wal_file),
      recovered_(log_.Recover(&disk_)),  // redo committed work before anything reads pages
      bpm_(options.pool_size, &disk_),
      catalog_(&bpm_),
      txn_(&bpm_, &disk_, &log_, options.checkpoint_bytes),
      planner_(&bpm_, &catalog_) {
    if (disk_.NumPages() == 0) {
        // Brand-new database: format the catalog page, durably
        txn_.Begin();
        catalog_.Initialize();
        txn_.Commit();
    } else {
        catalog_.Load();
    }
}

Database::~Database() {
    try {
        if (txn_.InTransaction()) {
            txn_.Rollback();
        }
        txn_.Checkpoint();
    } catch (const std::exception &) {
        // Nothing committed is lost: recovery replays the WAL on next open
    }
}

void Database::RollbackAndReload() {
    if (txn_.InTransaction()) {
        txn_.Rollback();
    }
    explicit_txn_ = false;
    catalog_.Load();  // tables created or extended by the transaction are undone
}

// Run a modifying statement inside the open transaction, or its own one
template <typename Fn>
void Database::RunWrite(Fn &&fn) {
    const bool autocommit = !explicit_txn_;
    try {
        if (autocommit) txn_.Begin();
        fn();
        if (autocommit) txn_.Commit();
    } catch (const std::exception &e) {
        RollbackAndReload();
        if (!autocommit) {
            throw std::runtime_error(std::string(e.what()) + " (transaction rolled back)");
        }
        throw;
    }
}

QueryResult Database::Execute(const std::string &sql) {
    QueryResult result;
    Lexer lex(sql);
    const TokenType first = lex.PeekToken().type;

    switch (first) {
        case TokenType::BEGIN:
            if (explicit_txn_) throw std::runtime_error("Already in a transaction");
            txn_.Begin();
            explicit_txn_ = true;
            result.message = "BEGIN";
            return result;

        case TokenType::COMMIT:
            if (!explicit_txn_) throw std::runtime_error("No transaction in progress");
            try {
                txn_.Commit();
            } catch (const std::exception &) {
                RollbackAndReload();
                throw;
            }
            explicit_txn_ = false;
            result.message = "COMMIT";
            return result;

        case TokenType::ROLLBACK:
            if (!explicit_txn_) throw std::runtime_error("No transaction in progress");
            RollbackAndReload();
            result.message = "ROLLBACK";
            return result;

        case TokenType::CREATE: {
            CreateTableStatement stmt = Parser(std::move(lex)).ParseCreateTable();
            RunWrite([&] {
                if (!CreateTableExecutor(&catalog_, stmt).Execute()) {
                    throw std::runtime_error("CREATE TABLE failed");
                }
            });
            result.message = "Table '" + stmt.table_name + "' created.";
            return result;
        }

        case TokenType::INSERT: {
            InsertStatement stmt = Parser(std::move(lex)).ParseInsert();
            RunWrite([&] {
                if (!InsertExecutor(&catalog_, stmt).Execute()) {
                    throw std::runtime_error("INSERT failed");
                }
            });
            result.message = "1 row inserted into " + stmt.table_name + ".";
            return result;
        }

        case TokenType::SELECT: {
            SelectStatement stmt = Parser(std::move(lex)).ParseSelect();
            PlanResult plan = planner_.PlanSelect(stmt);
            Tuple t;
            RID rid;
            while (plan.root->Next(t, rid)) {
                result.rows.push_back(t);
            }
            result.schema = plan.output_schema;
            return result;
        }

        case TokenType::EXPLAIN: {
            lex.NextToken();  // consume EXPLAIN
            SelectStatement stmt = Parser(std::move(lex)).ParseSelect();
            PlanResult plan = planner_.PlanSelect(stmt);
            result.message = Explain(*plan.root);
            return result;
        }

        default:
            throw std::runtime_error("Unrecognized statement type");
    }
}
