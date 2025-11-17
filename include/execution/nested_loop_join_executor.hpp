//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"

#include <functional>
#include <memory>

// Inner join on an arbitrary predicate: for each left row, rescan the right
// child and emit every combined (left ++ right) row the predicate accepts.
class NestedLoopJoinExecutor : public Executor {
public:
    using Predicate = std::function<bool(const Tuple &)>;

    NestedLoopJoinExecutor(std::unique_ptr<Executor> left,
                           std::unique_ptr<Executor> right,
                           Predicate pred)
        : left_(std::move(left)), right_(std::move(right)), pred_(std::move(pred)) {}

    void Init() override;
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "NestedLoopJoin"; }
    std::vector<const Executor *> Children() const override { return {left_.get(), right_.get()}; }

private:
    std::unique_ptr<Executor> left_;
    std::unique_ptr<Executor> right_;
    Predicate pred_;

    Tuple left_tuple_;
    bool has_left_{false};
};
