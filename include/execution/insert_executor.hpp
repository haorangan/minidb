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
    InsertExecutor(Catalog *catalog, InsertStatement stmt)
        : catalog_(catalog), stmt_(std::move(stmt)) {}

    // Executes the insert; returns true if OK, false on basic failure.
    // On failure the heap and index may disagree: the caller's transaction
    // must roll back.
    bool Execute();

private:
    Catalog *catalog_;
    InsertStatement stmt_;
};