//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "execution/executor.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <functional>
#include <memory>

class FilterExecutor : public Executor {
public:
    using Predicate = std::function<bool(const Tuple&)>;

    FilterExecutor(std::unique_ptr<Executor> child, Predicate pred)
        : child_(std::move(child)), pred_(std::move(pred)) {}

    void Init() override { child_->Init(); }

    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "Filter"; }
    std::vector<const Executor *> Children() const override { return {child_.get()}; }

private:
    std::unique_ptr<Executor> child_;
    Predicate pred_;
};
