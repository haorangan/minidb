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
#include "catalog/catalog.hpp"
#include "common/tuple.hpp"

#include <memory>
#include <functional>

struct PlanResult {
    std::unique_ptr<SeqScanExecutor> seq_scan;
    std::unique_ptr<FilterExecutor> filter;
    std::unique_ptr<IndexScanExecutor> index_scan;
    bool use_index = false;

    // convenience
    TableHeap *table = nullptr;
    const Schema *schema = nullptr;
};

class Planner {
public:
    Planner(BufferPoolManager *bpm, Catalog *catalog);

    PlanResult PlanSelect(const SelectStatement &stmt);

    std::function<bool(const Tuple&)> BuildPredicate(const Expr *expr,
                                                     const Schema *schema);

private:
    BufferPoolManager *bpm_;
    Catalog *catalog_;

    struct IndexHint {
        bool point = false;
        bool range = false;
        int32_t point_value = 0;
        int32_t low_value = 0;
        int32_t high_value = 0;
    };

    IndexHint AnalyzeForIndex(const Expr *expr, const Schema *schema);

    int64_t EvalExprInt(const Expr *expr, const Tuple &t, const Schema *schema) const;
    bool EvalExprBool(const Expr *expr, const Tuple &t, const Schema *schema) const;
};
