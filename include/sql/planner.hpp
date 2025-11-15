//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "sql/ast.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "catalog/table_heap.hpp"
#include "common/tuple.hpp"

#include <memory>

struct PlanResult {
    // one of these is non-null:
    std::unique_ptr<SeqScanExecutor> seq_scan;
    std::unique_ptr<FilterExecutor> filter;
    std::unique_ptr<IndexScanExecutor> index_scan;
    bool use_index = false;
};

// Planner/Executor builder
class Planner {
public:
    Planner(BufferPoolManager *bpm, TableHeap *table, BPlusTreeIndex *index);

    // Given a parsed SELECT statement, build an executor tree
    PlanResult PlanSelect(const SelectStatement &stmt);

    // Evaluate WHERE predicate on a Tuple for runtime filtering
    // (Only used for SeqScan + Filter; IndexScan already prunes via index)
    std::function<bool(const Tuple&)> BuildPredicate(const Expr *expr);

private:
    BufferPoolManager *bpm_;
    TableHeap *table_;
    BPlusTreeIndex *index_;

    // For index selection
    struct IndexHint {
        bool point = false;
        bool range = false;
        int32_t point_value = 0;
        int32_t low_value = 0;
        int32_t high_value = 0;
    };

    IndexHint AnalyzeForIndex(const Expr *expr);
    int64_t EvalExprInt(const Expr *expr, const Tuple &t) const;
    bool EvalExprBool(const Expr *expr, const Tuple &t) const;
};
