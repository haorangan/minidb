//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include <stdexcept>
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

    // Like GetColumnIndex, but an unqualified name ("dept") also matches a
    // qualified column ("emp.dept"). Throws if that match is ambiguous.
    int ResolveColumn(const std::string &name) const {
        int exact = GetColumnIndex(name);
        if (exact != -1 || name.find('.') != std::string::npos) return exact;

        int found = -1;
        const std::string suffix = "." + name;
        for (size_t i = 0; i < columns_.size(); ++i) {
            const std::string &col = columns_[i].name;
            if (col.size() > suffix.size() &&
                col.compare(col.size() - suffix.size(), suffix.size(), suffix) == 0) {
                if (found != -1) throw std::runtime_error("Ambiguous column: " + name);
                found = static_cast<int>(i);
            }
        }
        return found;
    }

    const Column &GetColumn(size_t idx) const { return columns_.at(idx); }

    // Copy with every column renamed to "<table>.<column>"
    Schema Qualified(const std::string &table) const {
        std::vector<Column> cols = columns_;
        for (auto &c : cols) c.name = table + "." + c.name;
        return Schema(std::move(cols));
    }

    // Left columns followed by right columns (join output)
    static Schema Concat(const Schema &left, const Schema &right) {
        std::vector<Column> cols = left.columns_;
        cols.insert(cols.end(), right.columns_.begin(), right.columns_.end());
        return Schema(std::move(cols));
    }

private:
    std::vector<Column> columns_;
};
