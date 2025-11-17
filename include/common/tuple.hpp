//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include <cstdint>
#include <vector>
#include <iostream>

// In-memory row. Stored columns are INT (int32) on disk; values are widened to
// int64 here so computed columns (e.g. SUM) don't overflow.
class Tuple {
public:
    Tuple() = default;

    explicit Tuple(std::vector<int64_t> values)
        : values_(std::move(values)) {}

    // Helper: construct from raw int32 buffer
    static Tuple FromRawInts(const int32_t *data, size_t count) {
        std::vector<int64_t> vals(count);
        for (size_t i = 0; i < count; ++i) {
            vals[i] = data[i];
        }
        return Tuple(std::move(vals));
    }

    // Left columns followed by right columns (join output)
    static Tuple Concat(const Tuple &left, const Tuple &right) {
        std::vector<int64_t> vals;
        vals.reserve(left.values_.size() + right.values_.size());
        vals.insert(vals.end(), left.values_.begin(), left.values_.end());
        vals.insert(vals.end(), right.values_.begin(), right.values_.end());
        return Tuple(std::move(vals));
    }

    size_t GetSize() const { return values_.size(); }

    int64_t GetInt(size_t idx) const {
        return values_.at(idx);
    }

    const std::vector<int64_t> &GetValues() const { return values_; }

    void PrintAll() const {
        std::cout << "(";
        for (size_t i = 0; i < values_.size(); ++i) {
            std::cout << values_[i];
            if (i + 1 < values_.size()) std::cout << ", ";
        }
        std::cout << ")";
    }

private:
    std::vector<int64_t> values_;
};
