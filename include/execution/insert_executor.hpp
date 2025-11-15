//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "catalog/catalog.hpp"
#include "index/bplus_tree_index.hpp"
#include "sql/ast.hpp"

#include <vector>
#include <string>

class InsertExecutor {
public:
    InsertExecutor(Catalog *catalog,
                   BufferPoolManager *bpm,
                   InsertStatement stmt)
        : catalog_(catalog), bpm_(bpm), stmt_(std::move(stmt)) {}

    // Executes the insert; returns true if OK, false on basic failure.
    bool Execute();

private:
    Catalog *catalog_;
    BufferPoolManager *bpm_;
    InsertStatement stmt_;
};