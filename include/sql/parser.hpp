//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "sql/lexer.hpp"
#include "sql/ast.hpp"
#include <stdexcept>

class SqlParseError : public std::runtime_error {
public:
    explicit SqlParseError(const std::string &msg)
        : std::runtime_error("SQL parse error: " + msg) {}
};

class Parser {
public:
    explicit Parser(Lexer lexer);

    SelectStatement ParseSelect();
    InsertStatement ParseInsert();

private:
    Lexer lexer_;
    Token current_;

    void Advance();
    bool Match(TokenType t);
    void Expect(TokenType t, const std::string &msg);

    // Parsing helpers
    SelectStatement ParseSelectInternal();

    // WHERE expression:
    // expr := or_expr
    // or_expr := and_expr (OR and_expr)*
    // and_expr := unary_expr (AND unary_expr)*
    // unary_expr := [NOT] primary
    // primary := comparison | '(' expr ')'
    ExprPtr ParseExpr();
    ExprPtr ParseOrExpr();
    ExprPtr ParseAndExpr();
    ExprPtr ParseUnaryExpr();
    ExprPtr ParsePrimaryExpr();

    ExprPtr ParseComparison(); // column op literal OR literal op column
    CompareOp ParseCompareOp(const Token &tok);
};
