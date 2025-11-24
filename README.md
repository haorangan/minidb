# MiniDB — A Minimal Relational Database Engine in C++

MiniDB is a compact relational database engine implemented in modern C++17.
It includes:

- Disk-backed slotted pages

- Buffer pool manager (LRU, no-steal)

- Write-ahead logging (WAL) + crash recovery

- Transactions (BEGIN / COMMIT / ROLLBACK)

- B+-tree indexing

- Persistent catalog + schemas

- Iterator-based execution engine with joins and aggregation

- SQL parser + planner

- Interactive CLI

## Features

### Storage Engine
- Slotted-page layout for variable-size tuples; a table's pages form a linked list

- Page-based access via DiskManager (`pread`/`pwrite`)

- BufferPoolManager with LRU replacement, pin/unpin, dirty page tracking

- Table definitions persist on page 0, so tables and indexes survive restarts

### Transactions, WAL & Recovery

- Statements autocommit, or group them with `BEGIN; ... COMMIT;` / `ROLLBACK;`

- No-steal buffer pool: uncommitted changes never reach the data file; rollback restores in-memory before-images

- Redo-only WAL of full page images: at commit, every modified page plus a COMMIT record is appended and fsync'd

- Each log record carries a CRC32, so torn or partially persisted writes are detected

- Recovery on startup replays only committed transactions; checkpoints flush pages and truncate the log

- Single session, so transactions run serially

### Indexing

- B+-tree index on the first column of each table (internal nodes, splits, linked leaves)

- Supports point lookups and range scans; duplicate keys allowed

### SQL Support

```CREATE TABLE name (col INT, ...)```

```INSERT INTO name VALUES(...)```

```SELECT ... FROM name [JOIN other ON ...]```

```WHERE, AND, OR, NOT, BETWEEN```

```COUNT, SUM, MIN, MAX, AVG, GROUP BY```

```ORDER BY col [ASC|DESC], ...```

```LIMIT n```

```BEGIN, COMMIT, ROLLBACK```

```EXPLAIN SELECT ...```

### Execution Engine

- Volcano-style `Next()` iterators sharing one `Executor` interface

- SeqScan, IndexScan, Filter, HashJoin, NestedLoopJoin, HashAggregate, Sort, Projection, Limit

- Planner picks an index scan when the WHERE clause matches the indexed column, and a hash join for equi-joins

## Build and Run
```
mkdir build && cd build
cmake ..
make
./minidb_demo
```
Example:
```
minidb> CREATE TABLE t (a INT, b INT);
minidb> INSERT INTO t VALUES(1, 10);
minidb> SELECT * FROM t ORDER BY b DESC LIMIT 1;
(1, 10)
minidb> .exit
```

## Tests and Benchmark
```
ctest                 # B+ tree, query, and crash-recovery tests
./minidb_bench        # point-query latency on 10^6 rows
```

The crash tests fork a child that runs transactions and kills itself at injected
points (mid-commit, torn or reordered WAL writes, torn data pages, mid-checkpoint,
during recovery) or by SIGKILL at random moments, then check that recovery keeps
every acknowledged commit, never exposes partial or rolled-back transactions, and
leaves the index consistent with the table.
