//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

enum class CompareOp {
    EQ, NEQ, LT, LE, GT, GE
};

struct Expr {
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

struct ColumnRefExpr : Expr {
    std::string name;  // "a" or "b" for now
    explicit ColumnRefExpr(std::string n) : name(std::move(n)) {}
};

struct IntLiteralExpr : Expr {
    int64_t value;
    explicit IntLiteralExpr(int64_t v) : value(v) {}
};

struct CompareExpr : Expr {
    ExprPtr left;
    CompareOp op;
    ExprPtr right;
    CompareExpr(ExprPtr l, CompareOp o, ExprPtr r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

enum class BoolOp {
    AND,
    OR
};

struct BoolExpr : Expr {
    ExprPtr left;
    BoolOp op;
    ExprPtr right;
    BoolExpr(ExprPtr l, BoolOp o, ExprPtr r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

struct NotExpr : Expr {
    ExprPtr child;
    explicit NotExpr(ExprPtr c) : child(std::move(c)) {}
};

struct ColumnRef {
    std::string name;
};

struct SelectStatement {
    std::vector<ColumnRef> select_list; // empty = *
    std::string table_name;
    ExprPtr where; // may be nullptr
};
