//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class TypeId {
    INT,   // 32-bit for now
    // TODO: add BIGINT, BOOL, VARCHAR, etc.
};

struct Column {
    std::string name;
    TypeId type;
    int32_t length;  // for fixed/var char later; unused for INT now
    bool nullable;

    Column(std::string name_, TypeId type_, int32_t length_ = 0, bool nullable_ = false)
        : name(std::move(name_)), type(type_), length(length_), nullable(nullable_) {}
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {}

    const std::vector<Column> &GetColumns() const { return columns_; }
    size_t GetColumnCount() const { return columns_.size(); }

    // Returns index of column name, or -1 if not found
    int GetColumnIndex(const std::string &name) const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    const Column &GetColumn(size_t idx) const { return columns_.at(idx); }

private:
    std::vector<Column> columns_;
};
