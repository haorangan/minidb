//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "catalog/catalog.hpp"
#include "catalog/schema.hpp"
#include "catalog/table_heap.hpp"
#include "sql/ast.hpp"

class CreateTableExecutor {
public:
    CreateTableExecutor(Catalog *catalog, const CreateTableStatement &stmt)
        : catalog_(catalog), stmt_(stmt) {}

    bool Execute();

private:
    Catalog *catalog_;
    CreateTableStatement stmt_;

    TypeId ParseType(const std::string &type_name);
};
