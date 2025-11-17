//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/tuple.hpp"
#include "common/types.hpp"

#include <string>
#include <vector>

// Volcano-style operator. Parents pull rows from children one Next() at a time.
// Executors start ready to produce rows; Init() rewinds to the beginning (joins
// use it to rescan their inner side).
class Executor {
public:
    virtual ~Executor() = default;

    virtual void Init() = 0;

    // Produce the next row; returns false when exhausted. Operators that
    // compute new rows (joins, aggregates) return an invalid RID.
    virtual bool Next(Tuple &out, RID &rid) = 0;

    // Short description of this operator, e.g. "SeqScan(emp)"
    virtual std::string Name() const = 0;

    virtual std::vector<const Executor *> Children() const { return {}; }
};

// Indented operator tree, one operator per line (used by EXPLAIN)
std::string Explain(const Executor &root);
