//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "execution/seq_scan_executor.hpp"
#include "common/tuple.hpp"
#include "common/types.hpp"

#include <functional>

class FilterExecutor {
public:
    using Predicate = std::function<bool(const Tuple&)>;

    FilterExecutor(SeqScanExecutor *child, Predicate pred)
        : child_(child), pred_(std::move(pred)) {}

    bool Next(Tuple &out, RID &rid);

private:
    SeqScanExecutor *child_;
    Predicate pred_;
};