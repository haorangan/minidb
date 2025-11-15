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

struct TableEntry {
    std::string name;
    Schema schema;
    std::unique_ptr<TableHeap> table;              // owns the heap
    std::unordered_map<std::string, BPlusTreeIndex*> indexes; // col_name -> index ptr (not owned)
};

class Catalog {
public:
    Catalog() = default;

    // Create a new table; returns pointer to its TableHeap.
    // For now this only creates heap in-memory; persistence is future work.
    TableHeap *CreateTable(const std::string &name,
                           const Schema &schema,
                           BufferPoolManager *bpm) {
        auto it = tables_.find(name);
        if (it != tables_.end()) {
            // just throw, could also add "IF NOT EXISTS" later
            throw std::runtime_error("Table already exists: " + name);
        }

        // Use your existing TableHeap::Create(bpm) factory but we want a ctor
        auto heap = std::unique_ptr<TableHeap>(TableHeap::Create(bpm));
        TableEntry entry;
        entry.name = name;
        entry.schema = schema;
        entry.table = std::move(heap);

        TableHeap *raw_ptr = entry.table.get();
        tables_.emplace(name, std::move(entry));
        return raw_ptr;
    }

    TableHeap *GetTable(const std::string &name) {
        auto it = tables_.find(name);
        if (it == tables_.end()) return nullptr;
        return it->second.table.get();
    }

    const Schema *GetSchema(const std::string &name) const {
        auto it = tables_.find(name);
        if (it == tables_.end()) return nullptr;
        return &it->second.schema;
    }

    void RegisterIndex(const std::string &table_name,
                       const std::string &column_name,
                       BPlusTreeIndex *index) {
        auto it = tables_.find(table_name);
        if (it == tables_.end()) {
            throw std::runtime_error("Table not found for index: " + table_name);
        }
        it->second.indexes[column_name] = index;
    }

    BPlusTreeIndex *GetIndex(const std::string &table_name,
                             const std::string &column_name) const {
        auto it = tables_.find(table_name);
        if (it == tables_.end()) return nullptr;
        auto jt = it->second.indexes.find(column_name);
        if (jt == it->second.indexes.end()) return nullptr;
        return jt->second;
    }

private:
    std::unordered_map<std::string, TableEntry> tables_;
};
