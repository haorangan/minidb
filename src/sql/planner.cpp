//
// Created by Haoran Gan on 11/15/25.
//

#include "sql/planner.hpp"

#include <stdexcept>

// ---- Planner constructor ----

Planner::Planner(BufferPoolManager *bpm, Catalog *catalog)
    : bpm_(bpm), catalog_(catalog) {}

// ---- Index analysis ----

// Try to recognize:
//  - a = constant
//  - a >= low AND a <= high (in either order)
Planner::IndexHint Planner::AnalyzeForIndex(const Expr *expr, const Schema *schema) {
    IndexHint hint;
    if (!expr || !schema) return hint;

    auto is_indexed_col = [schema](const Expr *e) -> bool {
        auto col = dynamic_cast<const ColumnRefExpr *>(e);
        if (!col) return false;
        int idx = schema->GetColumnIndex(col->name);
        return idx == 0; // assume first column has the index
    };

    auto is_int_lit = [](const Expr *e, int32_t &out) -> bool {
        auto lit = dynamic_cast<const IntLiteralExpr *>(e);
        if (!lit) return false;
        out = static_cast<int32_t>(lit->value);
        return true;
    };

    // The rest of your `AnalyzeForIndex` logic stays nearly the same,
    // but replace is_col_a() with is_indexed_col().

    // Simple equality: col = const or const = col
    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        int32_t val = 0;

        if (is_indexed_col(cmp->left.get()) && is_int_lit(cmp->right.get(), val) &&
            cmp->op == CompareOp::EQ) {
            hint.point = true;
            hint.point_value = val;
            return hint;
        } else if (is_indexed_col(cmp->right.get()) && is_int_lit(cmp->left.get(), val) &&
                   cmp->op == CompareOp::EQ) {
            hint.point = true;
            hint.point_value = val;
            return hint;
        }
        // range handled below
    }

    // Range: (col >= low AND col <= high) etc.
    auto b = dynamic_cast<const BoolExpr *>(expr);
    if (b && b->op == BoolOp::AND) {
        const auto *left_cmp  = dynamic_cast<const CompareExpr *>(b->left.get());
        const auto *right_cmp = dynamic_cast<const CompareExpr *>(b->right.get());
        if (!left_cmp || !right_cmp) return hint;

        int32_t low = 0, high = 0;
        bool got_low = false, got_high = false;

        auto process_cmp = [&](const CompareExpr *cmp) {
            int32_t v = 0;
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

        if (got_low && got_high && low <= high) {
            hint.range = true;
            hint.low_value = low;
            hint.high_value = high;
        }
    }

    return hint;
}

// ---- Expression evaluation for filter predicate ----

int64_t Planner::EvalExprInt(const Expr *expr,
                             const Tuple &t,
                             const Schema *schema) const {
    if (auto lit = dynamic_cast<const IntLiteralExpr *>(expr)) {
        return lit->value;
    }
    if (auto col = dynamic_cast<const ColumnRefExpr *>(expr)) {
        int idx = schema->GetColumnIndex(col->name);
        if (idx == -1) {
            throw std::runtime_error("Unknown column in expression: " + col->name);
        }
        return t.GetInt(static_cast<size_t>(idx));
    }
    throw std::runtime_error("Expected int expr");
}

bool Planner::EvalExprBool(const Expr *expr,
                           const Tuple &t,
                           const Schema *schema) const {
    if (!expr) return true;

    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        int64_t lv = EvalExprInt(cmp->left.get(), t, schema);
        int64_t rv = EvalExprInt(cmp->right.get(), t, schema);
        switch (cmp->op) {
            case CompareOp::EQ:  return lv == rv;
            case CompareOp::NEQ: return lv != rv;
            case CompareOp::LT:  return lv < rv;
            case CompareOp::LE:  return lv <= rv;
            case CompareOp::GT:  return lv > rv;
            case CompareOp::GE:  return lv >= rv;
        }
    }

    if (auto b = dynamic_cast<const BoolExpr *>(expr)) {
        bool left = EvalExprBool(b->left.get(), t, schema);
        bool right = EvalExprBool(b->right.get(), t, schema);
        if (b->op == BoolOp::AND) return left && right;
        return left || right;
    }

    if (auto n = dynamic_cast<const NotExpr *>(expr)) {
        return !EvalExprBool(n->child.get(), t, schema);
    }

    throw std::runtime_error("Unsupported boolean expr");
}

std::function<bool(const Tuple&)> Planner::BuildPredicate(const Expr *expr,
                                                          const Schema *schema) {
    if (!expr) {
        return [](const Tuple &) { return true; };
    }

    // capture schema pointer
    return [this, expr, schema](const Tuple &t) {
        return EvalExprBool(expr, t, schema);
    };
}

// ---- PlanSelect ----

PlanResult Planner::PlanSelect(const SelectStatement &stmt) {
    PlanResult res;

    if (!catalog_) {
        throw std::runtime_error("Planner has no catalog");
    }

    TableHeap *table = catalog_->GetTable(stmt.table_name);
    if (!table) {
        throw std::runtime_error("Unknown table: " + stmt.table_name);
    }
    const Schema *schema = catalog_->GetSchema(stmt.table_name);
    if (!schema) {
        throw std::runtime_error("Missing schema for table: " + stmt.table_name);
    }

    res.table = table;
    res.schema = schema;

    // For now, we only consider index on column "a"
    BPlusTreeIndex *index = catalog_->GetIndex(stmt.table_name, "a");

    // Decide whether to use index on column a
    IndexHint hint = AnalyzeForIndex(stmt.where.get(), schema);

    if (index != nullptr && (hint.point || hint.range)) {
        res.use_index = true;
        if (hint.point) {
            res.index_scan = std::make_unique<IndexScanExecutor>(
                bpm_, table, index, hint.point_value);
        } else {
            res.index_scan = std::make_unique<IndexScanExecutor>(
                bpm_, table, index, hint.low_value, hint.high_value);
        }
        return res;
    }

    // Else: SeqScan + optional Filter
    res.seq_scan = std::make_unique<SeqScanExecutor>(bpm_, table);
    if (stmt.where) {
        auto pred = BuildPredicate(stmt.where.get(), schema);
        res.filter = std::make_unique<FilterExecutor>(res.seq_scan.get(), std::move(pred));
    }

    return res;
}
