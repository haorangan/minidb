//
// Created by Haoran Gan on 11/15/25.
//

#include "execution/create_table_executor.hpp"

#include <iostream>
#include <stdexcept>

TypeId CreateTableExecutor::ParseType(const std::string &type_name) {
    std::string upper = type_name;
    for (char &c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper == "INT") {
        return TypeId::INT;
    }
    throw std::runtime_error("Unsupported type in CREATE TABLE: " + type_name);
}

bool CreateTableExecutor::Execute() {
    if (!catalog_) {
        std::cerr << "CreateTableExecutor: missing catalog\n";
        return false;
    }

    if (stmt_.columns.empty()) {
        std::cerr << "CreateTableExecutor: table must have at least one column\n";
        return false;
    }

    std::vector<Column> cols;
    cols.reserve(stmt_.columns.size());
    for (const auto &cdef : stmt_.columns) {
        try {
            TypeId t = ParseType(cdef.type_name);
            cols.emplace_back(cdef.name, t);
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            return false;
        }
    }

    Schema schema(std::move(cols));
    try {
        catalog_->CreateTable(stmt_.table_name, schema);
    } catch (const std::exception &e) {
        std::cerr << "CreateTableExecutor: " << e.what() << "\n";
        return false;
    }

    return true;
}
