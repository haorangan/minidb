//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include <cstdint>
#include <vector>
#include <iostream>

class Tuple {
public:
    Tuple() = default;

    explicit Tuple(std::vector<int32_t> values)
        : values_(std::move(values)) {}

    // Helper: construct from raw int32 buffer
    static Tuple FromRawInts(const int32_t *data, size_t count) {
        std::vector<int32_t> vals(count);
        for (size_t i = 0; i < count; ++i) {
            vals[i] = data[i];
        }
        return Tuple(std::move(vals));
    }

    size_t GetSize() const { return values_.size(); }

    int32_t GetInt(size_t idx) const {
        return values_.at(idx);
    }

    void PrintAll() const {
        std::cout << "(";
        for (size_t i = 0; i < values_.size(); ++i) {
            std::cout << values_[i];
            if (i + 1 < values_.size()) std::cout << ", ";
        }
        std::cout << ")";
    }

private:
    std::vector<int32_t> values_;
};
