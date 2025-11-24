//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "catalog/schema.hpp"
#include "catalog/table_heap.hpp"
#include "index/bplus_tree_index.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

struct TableEntry {
    std::string name;
    Schema schema;
    std::unique_ptr<TableHeap> table;        // owns the heap
    std::unique_ptr<BPlusTreeIndex> index;   // B+ tree on the first column
};

// Table definitions persist on page 0, so they go through the same buffer
// pool / WAL path as data and are covered by transactions and recovery.
//
// Page 0 layout:
//   [0..3] magic   [4..5] num_tables
//   then per table: name_len:u8 name | first_heap_page:i32 | index_root:i32
//                   | num_cols:u8 | (col_len:u8 col_name)*
class Catalog {
public:
    static constexpr page_id_t CATALOG_PAGE_ID = 0;

    explicit Catalog(BufferPoolManager *bpm) : bpm_(bpm) {}

    // Format page 0 of a brand-new database (must be its first page)
    void Initialize();

    // Rebuild the in-memory tables from page 0 (at startup and after rollback)
    void Load();

    // Create a table with an empty heap and an index on its first column
    TableHeap *CreateTable(const std::string &name, const Schema &schema);

    TableHeap *GetTable(const std::string &name);
    const Schema *GetSchema(const std::string &name) const;

    // Only the first column is indexed; returns nullptr for any other column
    BPlusTreeIndex *GetIndex(const std::string &table_name,
                             const std::string &column_name) const;

    std::vector<std::string> TableNames() const { return order_; }

private:
    static constexpr uint32_t MAGIC = 0x4D444243;  // "MDBC"

    BufferPoolManager *bpm_;
    std::unordered_map<std::string, TableEntry> tables_;
    std::vector<std::string> order_;  // creation order, as stored on page 0

    void Save();
};
