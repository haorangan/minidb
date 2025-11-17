//
// Created by Haoran Gan on 11/16/25.
//

#pragma once

#include "execution/executor.hpp"
#include "sql/ast.hpp"

#include <memory>
#include <unordered_map>

struct AggregateSpec {
    AggType type;
    int column;  // input column index; -1 for COUNT(*)
};

// Hash aggregation. On the first Next() it consumes the whole child, grouping
// rows by the group-by columns. Output rows are the group-by values followed by
// one value per aggregate. With no GROUP BY there is exactly one output row,
// even for empty input (COUNT = 0; other aggregates are 0 since there are no NULLs).
class AggregationExecutor : public Executor {
public:
    AggregationExecutor(std::unique_ptr<Executor> child,
                        std::vector<size_t> group_by,
                        std::vector<AggregateSpec> aggregates,
                        std::string label)
        : child_(std::move(child)), group_by_(std::move(group_by)),
          aggregates_(std::move(aggregates)), label_(std::move(label)) {}

    void Init() override;
    bool Next(Tuple &out, RID &rid) override;

    std::string Name() const override { return "HashAggregate(" + label_ + ")"; }
    std::vector<const Executor *> Children() const override { return {child_.get()}; }

private:
    struct AggState {
        int64_t count = 0;
        int64_t sum = 0;
        int64_t min = 0;
        int64_t max = 0;
    };

    struct KeyHash {
        size_t operator()(const std::vector<int64_t> &key) const;
    };

    std::unique_ptr<Executor> child_;
    std::vector<size_t> group_by_;
    std::vector<AggregateSpec> aggregates_;
    std::string label_;

    bool computed_{false};
    std::vector<Tuple> results_;
    size_t result_idx_{0};

    void Compute();
    int64_t Finalize(const AggregateSpec &spec, const AggState &state) const;
};
