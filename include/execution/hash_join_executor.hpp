//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"

#include <memory>
#include <unordered_map>

// Inner equi-join: left.col[left_key] = right.col[right_key].
// Build phase hashes the whole right child on the first Next(); probe phase
// streams the left child and emits (left ++ right) for every match.
class HashJoinExecutor : public Executor {
public:
    HashJoinExecutor(std::unique_ptr<Executor> left,
                     std::unique_ptr<Executor> right,
                     size_t left_key,
                     size_t right_key,
                     std::string label)
        : left_(std::move(left)), right_(std::move(right)),
          left_key_(left_key), right_key_(right_key), label_(std::move(label)) {}

    void Init() override;
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "HashJoin(" + label_ + ")"; }
    std::vector<const Executor *> Children() const override { return {left_.get(), right_.get()}; }

private:
    using HashTable = std::unordered_multimap<int64_t, Tuple>;

    std::unique_ptr<Executor> left_;
    std::unique_ptr<Executor> right_;
    size_t left_key_;
    size_t right_key_;
    std::string label_;

    bool built_{false};
    HashTable table_;

    // Matches for the current left row
    Tuple left_tuple_;
    HashTable::const_iterator match_it_;
    HashTable::const_iterator match_end_;

    void Build();
};
