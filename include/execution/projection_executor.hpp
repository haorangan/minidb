//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"

#include <memory>

// Emits the selected columns of each child row, in the given order
class ProjectionExecutor : public Executor {
public:
    ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<size_t> columns, std::string label)
        : child_(std::move(child)), columns_(std::move(columns)), label_(std::move(label)) {}

    void Init() override { child_->Init(); }
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "Projection(" + label_ + ")"; }
    std::vector<const Executor *> Children() const override { return {child_.get()}; }

private:
    std::unique_ptr<Executor> child_;
    std::vector<size_t> columns_;
    std::string label_;
};
