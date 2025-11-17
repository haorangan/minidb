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
    std::string name;  // "col" or "table.col"
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

enum class AggType {
    COUNT, SUM, MIN, MAX, AVG
};

// One entry in the SELECT list: a column, or an aggregate over a column / *
struct SelectItem {
    std::string column;         // column name, or the aggregate's argument ("" = COUNT(*))
    bool is_aggregate = false;
    AggType agg = AggType::COUNT;
    std::string alias;          // optional "AS name"
};

inline std::string AggName(AggType t) {
    switch (t) {
        case AggType::COUNT: return "COUNT";
        case AggType::SUM:   return "SUM";
        case AggType::MIN:   return "MIN";
        case AggType::MAX:   return "MAX";
        case AggType::AVG:   return "AVG";
    }
    return "?";
}

// How an item is named in output / ORDER BY when it has no alias, e.g. "COUNT(*)"
inline std::string DisplayName(const SelectItem &item) {
    if (!item.is_aggregate) return item.column;
    return AggName(item.agg) + "(" + (item.column.empty() ? "*" : item.column) + ")";
}

struct OrderByItem {
    std::string column_name;  // column, alias, or aggregate like "COUNT(*)"
    bool ascending;
};

struct JoinClause {
    std::string table_name;
    ExprPtr on;
};

struct SelectStatement {
    std::vector<SelectItem> select_list; // empty = *
    std::string table_name;
    std::vector<JoinClause> joins;       // FROM t [JOIN u ON ...]*
    ExprPtr where; // may be nullptr

    std::vector<std::string> group_by;   // optional GROUP BY
    std::vector<OrderByItem> order_by; // optional ORDER BY
    int64_t limit = -1;                // -1 = no LIMIT
};

struct InsertStatement {
    std::string table_name;
    std::vector<int64_t> values; // for now, all INT literals
};

struct ColumnDef {
    std::string name;
    std::string type_name;  // e.g. "INT"
};

struct CreateTableStatement {
    std::string table_name;
    std::vector<ColumnDef> columns;
};
