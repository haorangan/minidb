//
// Created by Haoran Gan on 11/15/25.
//
#pragma once

#include "sql/ast.hpp"
#include "execution/executor.hpp"
#include "index/bplus_tree_index.hpp"
#include "catalog/table_heap.hpp"
#include "catalog/catalog.hpp"
#include "common/tuple.hpp"

#include <memory>
#include <functional>

struct PlanResult {
    std::unique_ptr<Executor> root;  // pull rows from here
    Schema output_schema;            // names of the columns root produces
    bool use_index = false;
};

// Turns a SelectStatement into an operator tree:
//
//   scan (IndexScan or SeqScan) -> joins -> Filter (WHERE) -> HashAggregate
//   -> Sort (ORDER BY) -> Projection -> Limit
//
// Column names are resolved once here; a bad name fails at plan time.
class Planner {
public:
    Planner(BufferPoolManager *bpm, Catalog *catalog);

    PlanResult PlanSelect(const SelectStatement &stmt);

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

    IndexHint AnalyzeForIndex(const Expr *expr, const Schema &schema);

    TableHeap *LookupTable(const std::string &name, Schema &qualified_schema);

    using IntFn = std::function<int64_t(const Tuple &)>;
    using BoolFn = std::function<bool(const Tuple &)>;

    static IntFn CompileInt(const Expr *expr, const Schema &schema);
    static BoolFn CompileBool(const Expr *expr, const Schema &schema);
};
