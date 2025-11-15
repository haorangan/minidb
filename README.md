# MiniDB — A Minimal Relational Database Engine in C++

MiniDB is a compact relational database engine implemented in modern C++.
It includes:

- Disk-backed slotted pages

- Buffer pool manager

- Write-ahead logging (WAL) + recovery

- B+-tree indexing

- Catalog + schemas

- Iterator-based execution engine

- SQL parser + planner

- Interactive CLI

## Features

### Storage Engine
- Slotted-page layout for variable-size tuples

- Page-based access via DiskManager

- BufferPoolManager with LRU replacement

- Pin/unpin + dirty page tracking

### WAL & Recovery

- Redo logging in wal.log

- Recovery on startup

- Ensures no lost committed writes

### Indexing

- B+-tree index on first column of each table

- Supports point lookups and range scans

### SQL Support

```CREATE TABLE name (col INT, ...)```

```INSERT INTO name VALUES(...)```

```SELECT ... FROM name```

```WHERE, AND, OR, BETWEEN```

```ORDER BY col [ASC|DESC]```

```LIMIT n```

```SELECT *```

### Execution Engine

- Volcano-style Next() iterators

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
