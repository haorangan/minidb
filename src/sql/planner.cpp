//
// Created by Haoran Gan on 11/15/25.
//

#include "sql/planner.hpp"

#include <stdexcept>

// ---- Planner constructor ----

Planner::Planner(BufferPoolManager *bpm, TableHeap *table, BPlusTreeIndex *index)
    : bpm_(bpm), table_(table), index_(index) {}

// ---- Index analysis ----

// Try to recognize:
//  - a = constant
//  - a >= low AND a <= high (in either order)
Planner::IndexHint Planner::AnalyzeForIndex(const Expr *expr) {
    IndexHint hint;

    if (!expr) return hint;

    // Helper lambdas
    auto is_col_a = [](const Expr *e) -> bool {
        auto col = dynamic_cast<const ColumnRefExpr *>(e);
        if (!col) return false;
        // case-insensitive compare "a"
        if (col->name.size() == 1 || col->name == "a" || col->name == "A") {
            return true;
        }
        return false;
    };

    auto is_int_lit = [](const Expr *e, int32_t &out) -> bool {
        auto lit = dynamic_cast<const IntLiteralExpr *>(e);
        if (!lit) return false;
        out = static_cast<int32_t>(lit->value);
        return true;
    };

    // Case 1: simple comparison
    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        int32_t val = 0;

        // a = constant
        if (is_col_a(cmp->left.get()) && is_int_lit(cmp->right.get(), val) &&
            cmp->op == CompareOp::EQ) {
            hint.point = true;
            hint.point_value = val;
        }
        // constant = a
        else if (is_col_a(cmp->right.get()) && is_int_lit(cmp->left.get(), val) &&
                 cmp->op == CompareOp::EQ) {
            hint.point = true;
            hint.point_value = val;
        }
        // a >= low OR a <= high we'll cover via BoolExpr below
        return hint;
    }

    // Case 2: range: (a >= low AND a <= high) or swapped
    auto boolExpr = dynamic_cast<const BoolExpr *>(expr);
    if (boolExpr && boolExpr->op == BoolOp::AND) {
        const auto *left_cmp  = dynamic_cast<const CompareExpr *>(boolExpr->left.get());
        const auto *right_cmp = dynamic_cast<const CompareExpr *>(boolExpr->right.get());
        if (!left_cmp || !right_cmp) return hint;

        int32_t low = 0, high = 0;
        bool got_low = false, got_high = false;

        // Check left side
        {
            int32_t v = 0;
            if (is_col_a(left_cmp->left.get()) && is_int_lit(left_cmp->right.get(), v)) {
                if (left_cmp->op == CompareOp::GE || left_cmp->op == CompareOp::GT) {
                    low = v + (left_cmp->op == CompareOp::GT ? 1 : 0);
                    got_low = true;
                } else if (left_cmp->op == CompareOp::LE || left_cmp->op == CompareOp::LT) {
                    high = v - (left_cmp->op == CompareOp::LT ? 1 : 0);
                    got_high = true;
                }
            } else if (is_col_a(left_cmp->right.get()) && is_int_lit(left_cmp->left.get(), v)) {
                // constant >= a  → a <= constant
                if (left_cmp->op == CompareOp::GE || left_cmp->op == CompareOp::GT) {
                    high = v - (left_cmp->op == CompareOp::GT ? 1 : 0);
                    got_high = true;
                } else if (left_cmp->op == CompareOp::LE || left_cmp->op == CompareOp::LT) {
                    low = v + (left_cmp->op == CompareOp::LT ? 1 : 0);
                    got_low = true;
                }
            }
        }

        // Check right side
        {
            int32_t v = 0;
            if (is_col_a(right_cmp->left.get()) && is_int_lit(right_cmp->right.get(), v)) {
                if (right_cmp->op == CompareOp::GE || right_cmp->op == CompareOp::GT) {
                    low = v + (right_cmp->op == CompareOp::GT ? 1 : 0);
                    got_low = true;
                } else if (right_cmp->op == CompareOp::LE || right_cmp->op == CompareOp::LT) {
                    high = v - (right_cmp->op == CompareOp::LT ? 1 : 0);
                    got_high = true;
                }
            } else if (is_col_a(right_cmp->right.get()) && is_int_lit(right_cmp->left.get(), v)) {
                if (right_cmp->op == CompareOp::GE || right_cmp->op == CompareOp::GT) {
                    high = v - (right_cmp->op == CompareOp::GT ? 1 : 0);
                    got_high = true;
                } else if (right_cmp->op == CompareOp::LE || right_cmp->op == CompareOp::LT) {
                    low = v + (right_cmp->op == CompareOp::LT ? 1 : 0);
                    got_low = true;
                }
            }
        }

        if (got_low && got_high && low <= high) {
            hint.range = true;
            hint.low_value = low;
            hint.high_value = high;
        }
    }

    return hint;
}

// ---- Expression evaluation for filter predicate ----

int64_t Planner::EvalExprInt(const Expr *expr, const Tuple &t) const {
    if (auto lit = dynamic_cast<const IntLiteralExpr *>(expr)) {
        return lit->value;
    }
    if (auto col = dynamic_cast<const ColumnRefExpr *>(expr)) {
        // map "a" -> t.a, "b" -> t.b
        std::string n = col->name;
        for (char &c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (n == "a") return t.a;
        if (n == "b") return t.b;
        throw std::runtime_error("Unknown column: " + col->name);
    }
    throw std::runtime_error("Expected int expr");
}

bool Planner::EvalExprBool(const Expr *expr, const Tuple &t) const {
    if (!expr) return true;

    if (auto cmp = dynamic_cast<const CompareExpr *>(expr)) {
        int64_t lv = EvalExprInt(cmp->left.get(), t);
        int64_t rv = EvalExprInt(cmp->right.get(), t);
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
        bool left = EvalExprBool(b->left.get(), t);
        bool right = EvalExprBool(b->right.get(), t);
        if (b->op == BoolOp::AND) return left && right;
        return left || right;
    }

    if (auto n = dynamic_cast<const NotExpr *>(expr)) {
        return !EvalExprBool(n->child.get(), t);
    }

    throw std::runtime_error("Unsupported boolean expr");
}

std::function<bool(const Tuple&)> Planner::BuildPredicate(const Expr *expr) {
    if (!expr) {
        return [](const Tuple &) { return true; };
    }

    return [this, expr](const Tuple &t) {
        return EvalExprBool(expr, t);
    };
}

// ---- PlanSelect ----

PlanResult Planner::PlanSelect(const SelectStatement &stmt) {
    PlanResult res;

    // For now we assume there's only one table and it's "t" or whatever;
    // Can later validate stmt.table_name here.

    // Decide whether to use index on column a
    IndexHint hint = AnalyzeForIndex(stmt.where.get());

    if (index_ != nullptr && (hint.point || hint.range)) {
        // Use IndexScanExecutor
        res.use_index = true;
        if (hint.point) {
            res.index_scan = std::make_unique<IndexScanExecutor>(
                bpm_, table_, index_, hint.point_value);
        } else {
            res.index_scan = std::make_unique<IndexScanExecutor>(
                bpm_, table_, index_, hint.low_value, hint.high_value);
        }
        return res;
    }

    // Else: SeqScan + optional Filter
    res.seq_scan = std::make_unique<SeqScanExecutor>(bpm_, table_);
    if (stmt.where) {
        auto pred = BuildPredicate(stmt.where.get());
        res.filter = std::make_unique<FilterExecutor>(res.seq_scan.get(), std::move(pred));
    }

    return res;
}
