//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"

#include <memory>

// Stops after `limit` rows without pulling any more from the child
class LimitExecutor : public Executor {
public:
    LimitExecutor(std::unique_ptr<Executor> child, int64_t limit)
        : child_(std::move(child)), limit_(limit) {}

    void Init() override {
        child_->Init();
        emitted_ = 0;
    }

    bool Next(Tuple &out, RID &rid) override {
        if (emitted_ >= limit_ || !child_->Next(out, rid)) {
            return false;
        }
        ++emitted_;
        return true;
    }

    std::string Name() const override { return "Limit(" + std::to_string(limit_) + ")"; }
    std::vector<const Executor *> Children() const override { return {child_.get()}; }

private:
    std::unique_ptr<Executor> child_;
    int64_t limit_;
    int64_t emitted_{0};
};
