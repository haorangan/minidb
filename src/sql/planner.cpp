//
// Created by Haoran Gan on 11/15/25.
//

#include "sql/planner.hpp"

#include "execution/aggregation_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/hash_join_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "execution/limit_executor.hpp"
#include "execution/nested_loop_join_executor.hpp"
#include "execution/projection_executor.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/sort_executor.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

// ---- Planner constructor ----

Planner::Planner(BufferPoolManager *bpm, Catalog *catalog)
    : bpm_(bpm), catalog_(catalog) {}

static size_t ResolveOrThrow(const Schema &schema, const std::string &name) {
    int idx = schema.ResolveColumn(name);
    if (idx == -1) {
        throw std::runtime_error("Unknown column: " + name);
    }
    return static_cast<size_t>(idx);
}

TableHeap *Planner::LookupTable(const std::string &name, Schema &qualified_schema) {
    TableHeap *table = catalog_->GetTable(name);
    const Schema *schema = catalog_->GetSchema(name);
    if (!table || !schema) {
        throw std::runtime_error("Unknown table: " + name);
    }
    qualified_schema = schema->Qualified(name);
    return table;
}

// ---- Index analysis ----

// Try to recognize, on the indexed (first) column:
//  - col = constant
//  - col >= low AND col <= high (in either order, strict or not)
// Bounds are computed in 64 bits and only used if they fit the int32 key.
Planner::IndexHint Planner::AnalyzeForIndex(const Expr *expr, const Schema &schema) {
    IndexHint hint;
    if (!expr) return hint;

    constexpr int64_t KEY_MIN = std::numeric_limits<int32_t>::min();
    constexpr int64_t KEY_MAX = std::numeric_limits<int32_t>::max();

    auto is_indexed_col = [&schema](const Expr *e) -> bool {
        auto col = dynamic_cast<const ColumnRefExpr *>(e);
        if (!col) return false;
        return schema.ResolveColumn(col->name) == 0; // the index is on the first column
    };

    auto is_int_lit = [](const Expr *e, int64_t &out) -> bool {
        auto lit = dynamic_cast<const IntLiteralExpr *>(e);
        if (!lit) return false;
        out = lit->value;
        return true;
    };

    // Simple equality: col = const or const = col
    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        int64_t val = 0;
        bool matched = cmp->op == CompareOp::EQ &&
            ((is_indexed_col(cmp->left.get()) && is_int_lit(cmp->right.get(), val)) ||
             (is_indexed_col(cmp->right.get()) && is_int_lit(cmp->left.get(), val)));
        if (matched && val >= KEY_MIN && val <= KEY_MAX) {
            hint.point = true;
            hint.point_value = static_cast<int32_t>(val);
        }
        return hint;
    }

    // Range: (col >= low AND col <= high) etc.
    auto b = dynamic_cast<const BoolExpr *>(expr);
    if (b && b->op == BoolOp::AND) {
        const auto *left_cmp  = dynamic_cast<const CompareExpr *>(b->left.get());
        const auto *right_cmp = dynamic_cast<const CompareExpr *>(b->right.get());
        if (!left_cmp || !right_cmp) return hint;

        int64_t low = 0, high = 0;
        bool got_low = false, got_high = false;

        auto process_cmp = [&](const CompareExpr *cmp) {
            int64_t v = 0;
            if (is_indexed_col(cmp->left.get()) && is_int_lit(cmp->right.get(), v)) {
                if (cmp->op == CompareOp::GE || cmp->op == CompareOp::GT) {
                    low = v + (cmp->op == CompareOp::GT ? 1 : 0);
                    got_low = true;
                } else if (cmp->op == CompareOp::LE || cmp->op == CompareOp::LT) {
                    high = v - (cmp->op == CompareOp::LT ? 1 : 0);
                    got_high = true;
                }
            } else if (is_indexed_col(cmp->right.get()) && is_int_lit(cmp->left.get(), v)) {
                if (cmp->op == CompareOp::GE || cmp->op == CompareOp::GT) {
                    high = v - (cmp->op == CompareOp::GT ? 1 : 0);
                    got_high = true;
                } else if (cmp->op == CompareOp::LE || cmp->op == CompareOp::LT) {
                    low = v + (cmp->op == CompareOp::LT ? 1 : 0);
                    got_low = true;
                }
            }
        };

        process_cmp(left_cmp);
        process_cmp(right_cmp);

        // Every key is in [KEY_MIN, KEY_MAX], so clamping the bounds is exact
        low = std::max(low, KEY_MIN);
        high = std::min(high, KEY_MAX);
        if (got_low && got_high && low <= high) {
            hint.range = true;
            hint.low_value = static_cast<int32_t>(low);
            hint.high_value = static_cast<int32_t>(high);
        }
    }

    return hint;
}

// ---- Expression compilation ----
//
// Each AST node becomes a closure; column names are resolved to indices here,
// once, instead of on every row.

Planner::IntFn Planner::CompileInt(const Expr *expr, const Schema &schema) {
    if (auto lit = dynamic_cast<const IntLiteralExpr *>(expr)) {
        int64_t v = lit->value;
        return [v](const Tuple &) { return v; };
    }
    if (auto col = dynamic_cast<const ColumnRefExpr *>(expr)) {
        size_t idx = ResolveOrThrow(schema, col->name);
        return [idx](const Tuple &t) { return t.GetInt(idx); };
    }
    throw std::runtime_error("Expected column or integer literal");
}

Planner::BoolFn Planner::CompileBool(const Expr *expr, const Schema &schema) {
    if (!expr) {
        return [](const Tuple &) { return true; };
    }

    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        IntFn l = CompileInt(cmp->left.get(), schema);
        IntFn r = CompileInt(cmp->right.get(), schema);
        switch (cmp->op) {
            case CompareOp::EQ:  return [l, r](const Tuple &t) { return l(t) == r(t); };
            case CompareOp::NEQ: return [l, r](const Tuple &t) { return l(t) != r(t); };
            case CompareOp::LT:  return [l, r](const Tuple &t) { return l(t) < r(t); };
            case CompareOp::LE:  return [l, r](const Tuple &t) { return l(t) <= r(t); };
            case CompareOp::GT:  return [l, r](const Tuple &t) { return l(t) > r(t); };
            case CompareOp::GE:  return [l, r](const Tuple &t) { return l(t) >= r(t); };
        }
    }

    if (auto b = dynamic_cast<const BoolExpr *>(expr)) {
        BoolFn l = CompileBool(b->left.get(), schema);
        BoolFn r = CompileBool(b->right.get(), schema);
        if (b->op == BoolOp::AND) {
            return [l, r](const Tuple &t) { return l(t) && r(t); };
        }
        return [l, r](const Tuple &t) { return l(t) || r(t); };
    }

    if (auto n = dynamic_cast<const NotExpr *>(expr)) {
        BoolFn child = CompileBool(n->child.get(), schema);
        return [child](const Tuple &t) { return !child(t); };
    }

    throw std::runtime_error("Unsupported boolean expr");
}

// ---- PlanSelect ----

PlanResult Planner::PlanSelect(const SelectStatement &stmt) {
    PlanResult res;

    if (!catalog_) {
        throw std::runtime_error("Planner has no catalog");
    }

    // `scope` names the columns of the rows flowing out of `root` so far
    Schema scope;
    TableHeap *table = LookupTable(stmt.table_name, scope);
    std::unique_ptr<Executor> root;
    bool where_handled = false;

    // ---- Access path ----
    // The only index is on the first column. Use it only for a single-table
    // query whose entire WHERE clause it can answer; otherwise scan.
    if (stmt.joins.empty() && stmt.where) {
        const Column &key_col = catalog_->GetSchema(stmt.table_name)->GetColumn(0);
        BPlusTreeIndex *index = catalog_->GetIndex(stmt.table_name, key_col.name);
        IndexHint hint = AnalyzeForIndex(stmt.where.get(), scope);

        if (index != nullptr && (hint.point || hint.range)) {
            std::string label = stmt.table_name + ", " + key_col.name;
            if (hint.point) {
                label += " = " + std::to_string(hint.point_value);
                root = std::make_unique<IndexScanExecutor>(
                    bpm_, table, index, hint.point_value, label);
            } else {
                label += " in [" + std::to_string(hint.low_value) + ", " +
                         std::to_string(hint.high_value) + "]";
                root = std::make_unique<IndexScanExecutor>(
                    bpm_, table, index, hint.low_value, hint.high_value, label);
            }
            res.use_index = true;
            where_handled = true;
        }
    }
    if (!root) {
        root = std::make_unique<SeqScanExecutor>(bpm_, table, stmt.table_name);
    }

    // ---- Joins (left-deep: each JOIN adds a table on the right) ----
    std::vector<std::string> tables_seen = {stmt.table_name};
    for (const JoinClause &join : stmt.joins) {
        if (std::find(tables_seen.begin(), tables_seen.end(), join.table_name) != tables_seen.end()) {
            throw std::runtime_error("Table appears twice in FROM (table aliases are not supported): " +
                                     join.table_name);
        }
        tables_seen.push_back(join.table_name);

        Schema right_scope;
        TableHeap *right_table = LookupTable(join.table_name, right_scope);
        auto right = std::make_unique<SeqScanExecutor>(bpm_, right_table, join.table_name);

        Schema combined = Schema::Concat(scope, right_scope);
        const size_t left_width = scope.GetColumnCount();

        // ON left_col = right_col  =>  hash join; anything else => nested loop
        bool planned = false;
        auto cmp = dynamic_cast<const CompareExpr *>(join.on.get());
        if (cmp && cmp->op == CompareOp::EQ) {
            auto l = dynamic_cast<const ColumnRefExpr *>(cmp->left.get());
            auto r = dynamic_cast<const ColumnRefExpr *>(cmp->right.get());
            if (l && r) {
                size_t a = ResolveOrThrow(combined, l->name);
                size_t b = ResolveOrThrow(combined, r->name);
                if ((a < left_width) != (b < left_width)) {
                    size_t left_key = std::min(a, b);
                    size_t right_key = std::max(a, b);
                    std::string label = combined.GetColumn(left_key).name + " = " +
                                        combined.GetColumn(right_key).name;
                    root = std::make_unique<HashJoinExecutor>(
                        std::move(root), std::move(right), left_key, right_key - left_width, label);
                    planned = true;
                }
            }
        }
        if (!planned) {
            root = std::make_unique<NestedLoopJoinExecutor>(
                std::move(root), std::move(right), CompileBool(join.on.get(), combined));
        }
        scope = std::move(combined);
    }

    // ---- WHERE ----
    if (stmt.where && !where_handled) {
        root = std::make_unique<FilterExecutor>(std::move(root),
                                                CompileBool(stmt.where.get(), scope));
    }

    // ---- Aggregation / select list ----
    // proj[i] = index in `scope` of the value for select item i
    std::vector<size_t> proj;
    bool has_aggregates = !stmt.group_by.empty() ||
        std::any_of(stmt.select_list.begin(), stmt.select_list.end(),
                    [](const SelectItem &item) { return item.is_aggregate; });

    if (has_aggregates) {
        if (stmt.select_list.empty()) {
            throw std::runtime_error("SELECT * cannot be used with GROUP BY or aggregates");
        }

        // Aggregate output: group-by columns, then one column per aggregate
        std::vector<size_t> group_cols;
        std::vector<Column> out_cols;
        std::string label;
        for (const std::string &name : stmt.group_by) {
            size_t idx = ResolveOrThrow(scope, name);
            group_cols.push_back(idx);
            out_cols.push_back(scope.GetColumn(idx));
            label += (label.empty() ? "group by " : ", ") + scope.GetColumn(idx).name;
        }

        std::vector<AggregateSpec> aggs;
        std::string agg_label;
        for (const SelectItem &item : stmt.select_list) {
            if (item.is_aggregate) {
                int col = item.column.empty() ? -1 : static_cast<int>(ResolveOrThrow(scope, item.column));
                proj.push_back(group_cols.size() + aggs.size());
                aggs.push_back({item.agg, col});
                out_cols.emplace_back(DisplayName(item), TypeId::INT);
                agg_label += (agg_label.empty() ? "" : ", ") + DisplayName(item);
            } else {
                size_t idx = ResolveOrThrow(scope, item.column);
                auto pos = std::find(group_cols.begin(), group_cols.end(), idx);
                if (pos == group_cols.end()) {
                    throw std::runtime_error("Column " + item.column +
                                             " must appear in GROUP BY or be inside an aggregate");
                }
                proj.push_back(static_cast<size_t>(pos - group_cols.begin()));
            }
        }
        if (!agg_label.empty()) {
            label += (label.empty() ? "" : "; ") + agg_label;
        }

        root = std::make_unique<AggregationExecutor>(std::move(root), std::move(group_cols),
                                                     std::move(aggs), label);
        scope = Schema(std::move(out_cols));
    } else {
        for (const SelectItem &item : stmt.select_list) {
            proj.push_back(ResolveOrThrow(scope, item.column));
        }
    }

    // ---- ORDER BY ----
    // Sorts before projection, so it may use columns that aren't selected.
    // A name can be a select alias, a column, or an aggregate like COUNT(*).
    if (!stmt.order_by.empty()) {
        std::vector<SortKey> keys;
        std::string label;
        for (const OrderByItem &ob : stmt.order_by) {
            int idx = -1;
            for (size_t i = 0; i < stmt.select_list.size(); ++i) {
                if (!stmt.select_list[i].alias.empty() && stmt.select_list[i].alias == ob.column_name) {
                    idx = static_cast<int>(proj[i]);
                }
            }
            if (idx == -1) {
                idx = scope.ResolveColumn(ob.column_name);
            }
            if (idx == -1) {
                throw std::runtime_error("Unknown column in ORDER BY: " + ob.column_name);
            }
            keys.push_back({static_cast<size_t>(idx), ob.ascending});
            label += (label.empty() ? "" : ", ") + scope.GetColumn(static_cast<size_t>(idx)).name +
                     (ob.ascending ? "" : " DESC");
        }
        root = std::make_unique<SortExecutor>(std::move(root), std::move(keys), label);
    }

    // ---- Projection ----
    if (!stmt.select_list.empty()) {
        std::vector<Column> out_cols;
        std::string label;
        for (size_t i = 0; i < stmt.select_list.size(); ++i) {
            const SelectItem &item = stmt.select_list[i];
            std::string name = !item.alias.empty() ? item.alias : scope.GetColumn(proj[i]).name;
            out_cols.emplace_back(name, TypeId::INT);
            label += (label.empty() ? "" : ", ") + name;
        }
        root = std::make_unique<ProjectionExecutor>(std::move(root), std::move(proj), label);
        res.output_schema = Schema(std::move(out_cols));
    } else {
        res.output_schema = scope;
    }

    // ---- LIMIT ----
    if (stmt.limit >= 0) {
        root = std::make_unique<LimitExecutor>(std::move(root), stmt.limit);
    }

    res.root = std::move(root);
    return res;
}
