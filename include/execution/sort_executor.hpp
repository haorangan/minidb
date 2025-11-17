//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"

#include <memory>

struct SortKey {
    size_t column;
    bool ascending;
};

// In-memory sort: materializes the child on the first Next(), then emits rows
// ordered by the keys (stable, so ties keep their input order).
class SortExecutor : public Executor {
public:
    SortExecutor(std::unique_ptr<Executor> child, std::vector<SortKey> keys, std::string label)
        : child_(std::move(child)), keys_(std::move(keys)), label_(std::move(label)) {}

    void Init() override;
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "Sort(" + label_ + ")"; }
    std::vector<const Executor *> Children() const override { return {child_.get()}; }

private:
    std::unique_ptr<Executor> child_;
    std::vector<SortKey> keys_;
    std::string label_;

    bool sorted_{false};
    std::vector<std::pair<Tuple, RID>> rows_;
    size_t row_idx_{0};
};
