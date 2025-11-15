//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "catalog/catalog.hpp"
#include "catalog/schema.hpp"
#include "catalog/table_heap.hpp"
#include "sql/ast.hpp"
#include "storage/buffer_pool_manager.hpp"

class CreateTableExecutor {
public:
    CreateTableExecutor(Catalog *catalog,
                        BufferPoolManager *bpm,
                        const CreateTableStatement &stmt)
        : catalog_(catalog), bpm_(bpm), stmt_(stmt) {}

    bool Execute();

private:
    Catalog *catalog_;
    BufferPoolManager *bpm_;
    CreateTableStatement stmt_;

    TypeId ParseType(const std::string &type_name);
};
