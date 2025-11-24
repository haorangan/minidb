// Transaction and crash-recovery tests.
//
// Each crash trial forks a child that runs a fixed workload of transactions
// and dies partway through: either at an injected crash point (mid-commit,
// torn or reordered WAL writes, torn data page, mid-checkpoint, between heap
// and index insert, during recovery) or from a SIGKILL at a random moment. The child
// reports each commit over a pipe before it dies. The parent then reopens the
// database (running recovery) and checks that:
//   - every acknowledged commit is present          (durability)
//   - every transaction is all-or-nothing, and rolled-back ones never appear
//   - only a prefix of transactions is present      (no gaps)
//   - the B+ tree is valid and finds every row      (heap/index consistency)
//   - the database still accepts writes and survives another reopen

#include "common/crash_point.hpp"
#include "db/database.hpp"
#include "test_util.hpp"

#include <csignal>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static const char *DB_FILE = "crash_test.db";
static const char *WAL_FILE = "crash_test.wal";

static void RemoveFiles() {
    std::remove(DB_FILE);
    std::remove(WAL_FILE);
}

// Small pool and WAL threshold so evictions and checkpoints happen constantly
static Database::Options CrashOptions() {
    Database::Options o;
    o.pool_size = 16;
    o.checkpoint_bytes = 64 * 1024;
    return o;
}

// ---- Workload: table t(id, txn, seq); transaction n inserts RowsIn(n) rows ----

constexpr int NUM_TXNS = 40;
static int RowsIn(int txn) { return 1 + (txn * 37) % 60; }
static bool RolledBack(int txn) { return txn % 6 == 0; }
static int64_t Key(int txn, int seq) {
    return (static_cast<uint32_t>(txn) * 2654435761u + static_cast<uint32_t>(seq) * 40503u) & 0x7fffffff;
}

static void Ack(int fd, int value) {
    (void)!write(fd, &value, sizeof(value));
}

// Runs in the child. Acks 0 once the table exists, then n after transaction n.
static void RunWorkload(int ack_fd) {
    Database db(DB_FILE, WAL_FILE, CrashOptions());
    db.Execute("CREATE TABLE t (id INT, txn INT, seq INT);");
    Ack(ack_fd, 0);
    for (int txn = 1; txn <= NUM_TXNS; ++txn) {
        db.Execute("BEGIN;");
        for (int seq = 0; seq < RowsIn(txn); ++seq) {
            db.Execute("INSERT INTO t VALUES (" + std::to_string(Key(txn, seq)) + ", " +
                       std::to_string(txn) + ", " + std::to_string(seq) + ");");
        }
        db.Execute(RolledBack(txn) ? "ROLLBACK;" : "COMMIT;");
        Ack(ack_fd, txn);
    }
}

// ---- Verification (parent) ----

// Checks the recovered state; returns false (and reports) on any violation.
// Sets `rows_after` to the row count it leaves behind (0 if there is no table).
static bool VerifyRecovered(int acked, const std::string &label, size_t &rows_after) {
    rows_after = 0;
    int failures_before = g_failures;
    auto fail = [&](const std::string &what) {
        std::cerr << "  [" << label << "] " << what << "\n";
        ++g_failures;
    };

    Database db(DB_FILE, WAL_FILE, CrashOptions());
    if (db.GetCatalog().GetTable("t") == nullptr) {
        if (acked >= 0) fail("table t missing although its creation was acknowledged");
        return g_failures == failures_before;
    }

    std::map<int64_t, std::set<int64_t>> seqs_by_txn;
    auto rows = db.Execute("SELECT * FROM t;").rows;
    for (const Tuple &r : rows) {
        if (r.GetInt(0) != Key(static_cast<int>(r.GetInt(1)), static_cast<int>(r.GetInt(2)))) {
            fail("row with wrong key");
        }
        if (!seqs_by_txn[r.GetInt(1)].insert(r.GetInt(2)).second) fail("duplicate row");
    }

    int last_present = 0;
    for (const auto &[txn, seqs] : seqs_by_txn) {
        if (RolledBack(static_cast<int>(txn))) fail("rolled-back txn " + std::to_string(txn) + " visible");
        if (static_cast<int>(seqs.size()) != RowsIn(static_cast<int>(txn))) {
            fail("txn " + std::to_string(txn) + " is partial: " + std::to_string(seqs.size()) +
                 " of " + std::to_string(RowsIn(static_cast<int>(txn))) + " rows");
        }
        last_present = std::max(last_present, static_cast<int>(txn));
    }
    // The transaction after the last ack may have committed just before the crash
    if (last_present > acked + 1) fail("txn " + std::to_string(last_present) + " present beyond last ack");
    for (int txn = 1; txn <= std::max(acked, last_present); ++txn) {
        if (!RolledBack(txn) && seqs_by_txn.count(txn) == 0) {
            fail("committed txn " + std::to_string(txn) + " lost (acked through " + std::to_string(acked) + ")");
        }
    }

    // Index must agree with the heap
    size_t indexed = 0;
    if (!db.GetCatalog().GetIndex("t", "id")->Validate(&indexed)) fail("B+ tree invariants violated");
    if (indexed != rows.size()) fail("index has " + std::to_string(indexed) + " entries, heap has " +
                                     std::to_string(rows.size()));
    for (const Tuple &r : rows) {
        auto found = db.Execute("SELECT txn, seq FROM t WHERE id = " + std::to_string(r.GetInt(0)) + ";").rows;
        bool ok = false;
        for (const Tuple &f : found) ok |= f.GetInt(0) == r.GetInt(1) && f.GetInt(1) == r.GetInt(2);
        if (!ok) {
            fail("row not reachable through the index");
            break;
        }
    }

    // Still writable, and the write survives a clean reopen
    db.Execute("INSERT INTO t VALUES (" + std::to_string(Key(999, 0)) + ", 999, 0);");
    rows_after = rows.size() + 1;
    return g_failures == failures_before;
}

static bool VerifyAfterReopen(size_t expected_rows) {
    Database db(DB_FILE, WAL_FILE, CrashOptions());
    size_t n = db.Execute("SELECT * FROM t;").rows.size();
    if (n != expected_rows) {
        std::cerr << "  row count changed across clean reopen: " << n << " vs " << expected_rows << "\n";
        ++g_failures;
        return false;
    }
    return true;
}

// ---- Trials ----

struct Trial {
    std::string crash_point;   // empty = none
    int nth = 0;
    int kill_after_acks = -1;  // >= 0: SIGKILL after this many acks (+ random delay)
    int kill_delay_us = 0;
    bool crash_in_recovery = false;
};

static std::string Describe(const Trial &t) {
    if (t.kill_after_acks >= 0) {
        return "SIGKILL after ack " + std::to_string(t.kill_after_acks) + " +" + std::to_string(t.kill_delay_us) + "us";
    }
    std::string s = t.crash_point.empty() ? "no crash" : t.crash_point + " #" + std::to_string(t.nth);
    return t.crash_in_recovery ? s + ", then crash during recovery" : s;
}

static int g_injected = 0, g_sigkilled = 0, g_completed = 0;

static bool RunTrial(const Trial &trial) {
    RemoveFiles();

    int fds[2];
    if (pipe(fds) != 0) return false;
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        if (!trial.crash_point.empty()) ArmCrashPoint(trial.crash_point, trial.nth);
        try {
            RunWorkload(fds[1]);
        } catch (const std::exception &e) {
            std::cerr << "  workload error: " << e.what() << "\n";
            _exit(1);
        }
        _exit(0);
    }
    close(fds[1]);

    int acked = -1, value = 0;
    bool killed = false;
    while (read(fds[0], &value, sizeof(value)) == sizeof(value)) {
        acked = value;
        if (trial.kill_after_acks >= 0 && !killed && acked >= trial.kill_after_acks) {
            usleep(static_cast<useconds_t>(trial.kill_delay_us));
            kill(pid, SIGKILL);
            killed = true;
        }
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    bool clean_exit = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    bool crashed = (WIFEXITED(status) && WEXITSTATUS(status) == 99) ||
                   (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    if (!clean_exit && !crashed) {
        std::cerr << "  [" << Describe(trial) << "] child failed with status " << status << "\n";
        ++g_failures;
        return false;
    }
    if (clean_exit && !trial.crash_point.empty()) {
        std::cerr << "  [" << Describe(trial) << "] crash point never reached\n";
        ++g_failures;
        return false;
    }
    if (WIFSIGNALED(status)) ++g_sigkilled;
    else if (crashed) ++g_injected;
    else ++g_completed;

    // Crash again while recovering, before the log is truncated
    if (trial.crash_in_recovery) {
        pid_t rpid = fork();
        if (rpid == 0) {
            ArmCrashPoint("recovery.before_truncate", 1);
            try {
                Database db(DB_FILE, WAL_FILE, CrashOptions());
            } catch (...) {
                _exit(1);
            }
            _exit(0);
        }
        waitpid(rpid, &status, 0);
    }

    // Recovery bugs can leave a database that won't even open: count that as a failure
    try {
        size_t rows_after = 0;
        if (!VerifyRecovered(acked, Describe(trial), rows_after)) return false;
        return rows_after == 0 || VerifyAfterReopen(rows_after);
    } catch (const std::exception &e) {
        std::cerr << "  [" << Describe(trial) << "] error after recovery: " << e.what() << "\n";
        ++g_failures;
        return false;
    }
}

// ---- Transaction semantics (no crashes) ----

static void TestTransactions() {
    std::cout << "transaction_semantics" << std::endl;
    RemoveFiles();
    {
        Database db(DB_FILE, WAL_FILE);
        db.Execute("CREATE TABLE a (x INT, y INT);");
        db.Execute("INSERT INTO a VALUES (1, 10);");

        // ROLLBACK undoes inserts, page splits, new heap pages, and CREATE TABLE
        db.Execute("BEGIN;");
        db.Execute("CREATE TABLE b (x INT);");
        for (int i = 0; i < 2000; ++i) db.Execute("INSERT INTO a VALUES (" + std::to_string(i + 100) + ", 0);");
        db.Execute("ROLLBACK;");
        CHECK(db.Execute("SELECT * FROM a;").rows.size() == 1);
        CHECK(db.GetCatalog().GetTable("b") == nullptr);
        size_t indexed = 0;
        CHECK(db.GetCatalog().GetIndex("a", "x")->Validate(&indexed) && indexed == 1);

        // A failing statement aborts the whole explicit transaction
        db.Execute("BEGIN;");
        db.Execute("INSERT INTO a VALUES (2, 20);");
        bool threw = false;
        try {
            db.Execute("INSERT INTO a VALUES (3);");  // wrong column count
        } catch (const std::exception &) {
            threw = true;
        }
        CHECK(threw && !db.InTransaction());
        CHECK(db.Execute("SELECT * FROM a;").rows.size() == 1);

        // Autocommit: each statement is its own transaction
        db.Execute("INSERT INTO a VALUES (4, 40);");
        CHECK(db.Execute("SELECT * FROM a;").rows.size() == 2);

        db.Execute("BEGIN;");
        db.Execute("INSERT INTO a VALUES (5, 50);");
        db.Execute("COMMIT;");
    }
    {
        // Tables, rows, and indexes survive a restart
        Database db(DB_FILE, WAL_FILE);
        auto rows = db.Execute("SELECT x FROM a ORDER BY x;").rows;
        CHECK(rows.size() == 3 && rows[0].GetInt(0) == 1 && rows[2].GetInt(0) == 5);
        CHECK(db.Execute("EXPLAIN SELECT * FROM a WHERE x = 4;").message.find("IndexScan") != std::string::npos);
        CHECK(db.Execute("SELECT y FROM a WHERE x = 4;").rows.at(0).GetInt(0) == 40);
    }
    RemoveFiles();
}

int main() {
    TestTransactions();

    std::vector<Trial> trials;
    trials.push_back({});  // full run, no crash
    const std::vector<std::pair<std::string, std::vector<int>>> points = {
        {"txn.before_commit_record", {1, 2, 5, 12, 25}},  // log has page images, no COMMIT
        {"txn.mid_log", {2, 7, 20, 50}},                  // some of a txn's page images logged
        {"wal.before_sync", {2, 6, 20}},                  // COMMIT written, not yet fsync'd
        {"wal.lost_write", {2, 8, 20}},                   // COMMIT on disk, earlier sector lost
        {"txn.after_commit", {1, 3, 9, 30}},              // durable, not yet acknowledged
        {"wal.torn_write", {1, 3, 10, 40, 100}},          // half a log record on disk
        {"disk.torn_write", {1, 4, 12, 24}},              // half a data page on disk
        {"checkpoint.after_flush", {1, 2, 4}},
        {"checkpoint.before_truncate", {1, 3}},
        {"insert.after_heap", {1, 50, 300, 700}},         // row in heap, not in index
    };
    for (const auto &[point, nths] : points) {
        for (int nth : nths) trials.push_back({point, nth});
    }
    for (int nth : {3, 10, 25}) {
        trials.push_back({"txn.after_commit", nth, -1, 0, /*crash_in_recovery=*/true});
    }
    std::mt19937 rng(2025);
    for (int i = 0; i < 60; ++i) {
        Trial t;
        t.kill_after_acks = std::uniform_int_distribution<int>(0, NUM_TXNS - 5)(rng);
        t.kill_delay_us = std::uniform_int_distribution<int>(0, 400)(rng);
        trials.push_back(t);
    }

    // Watchdog: a recovery bug that corrupts the database into a loop should
    // fail the run, not hang it
    int passed = 0;
    for (const Trial &t : trials) {
        alarm(30);
        passed += RunTrial(t) ? 1 : 0;
        alarm(0);
    }
    RemoveFiles();
    std::cout << "crash_recovery: " << passed << "/" << trials.size() << " trials passed ("
              << g_injected << " injected crashes, " << g_sigkilled << " SIGKILLs, "
              << g_completed << " ran to completion)" << std::endl;

    return TestSummary("crash");
}
