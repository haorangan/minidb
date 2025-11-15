//
// Created by Haoran Gan on 11/15/25.
//

#include "sql/parser.hpp"

Parser::Parser(Lexer lexer)
    : lexer_(std::move(lexer)) {
    current_ = lexer_.NextToken();
}

void Parser::Advance() {
    current_ = lexer_.NextToken();
}

bool Parser::Match(TokenType t) {
    if (current_.type == t) {
        Advance();
        return true;
    }
    return false;
}

void Parser::Expect(TokenType t, const std::string &msg) {
    if (!Match(t)) {
        throw SqlParseError(msg);
    }
}

SelectStatement Parser::ParseSelect() {
    return ParseSelectInternal();
}

SelectStatement Parser::ParseSelectInternal() {
    SelectStatement stmt;

    // SELECT
    Expect(TokenType::SELECT, "Expected SELECT");

    // select list: * or col[, col]*
    if (Match(TokenType::STAR)) {
        // leave select_list empty to mean "*"
    } else {
        // at least one identifier
        if (current_.type != TokenType::IDENT) {
            throw SqlParseError("Expected column name or * after SELECT");
        }
        while (true) {
            if (current_.type != TokenType::IDENT) {
                throw SqlParseError("Expected column name in SELECT list");
            }
            stmt.select_list.push_back(ColumnRef{current_.lexeme});
            Advance();
            if (!Match(TokenType::COMMA)) break;
        }
    }

    // FROM <ident>
    Expect(TokenType::FROM, "Expected FROM after select list");
    if (current_.type != TokenType::IDENT) {
        throw SqlParseError("Expected table name after FROM");
    }
    stmt.table_name = current_.lexeme;
    Advance();

    // Optional WHERE
    if (Match(TokenType::WHERE)) {
        stmt.where = ParseExpr();
    }

    // Optional semicolon
    if (current_.type == TokenType::SEMICOLON) {
        Advance();
    }

    // Must be EOF or semicolon
    if (current_.type != TokenType::END_OF_FILE &&
        current_.type != TokenType::SEMICOLON) {
        throw SqlParseError("Unexpected trailing tokens after SELECT");
    }

    return stmt;
}

// ---- Expression parsing ----

ExprPtr Parser::ParseExpr() {
    return ParseOrExpr();
}

ExprPtr Parser::ParseOrExpr() {
    ExprPtr left = ParseAndExpr();
    while (current_.type == TokenType::OR) {
        Advance();
        ExprPtr right = ParseAndExpr();
        left = std::make_unique<BoolExpr>(std::move(left), BoolOp::OR, std::move(right));
    }
    return left;
}

ExprPtr Parser::ParseAndExpr() {
    ExprPtr left = ParseUnaryExpr();
    while (current_.type == TokenType::AND) {
        Advance();
        ExprPtr right = ParseUnaryExpr();
        left = std::make_unique<BoolExpr>(std::move(left), BoolOp::AND, std::move(right));
    }
    return left;
}

ExprPtr Parser::ParseUnaryExpr() {
    if (current_.type == TokenType::NOT) {
        Advance();
        ExprPtr child = ParseUnaryExpr();
        return std::make_unique<NotExpr>(std::move(child));
    }
    return ParsePrimaryExpr();
}

ExprPtr Parser::ParsePrimaryExpr() {
    if (current_.type == TokenType::LPAREN) {
        Advance();
        ExprPtr e = ParseExpr();
        Expect(TokenType::RPAREN, "Expected )");
        return e;
    }
    // fall back to comparison
    return ParseComparison();
}

CompareOp Parser::ParseCompareOp(const Token &tok) {
    switch (tok.type) {
        case TokenType::EQ:  return CompareOp::EQ;
        case TokenType::NEQ: return CompareOp::NEQ;
        case TokenType::LT:  return CompareOp::LT;
        case TokenType::LE:  return CompareOp::LE;
        case TokenType::GT:  return CompareOp::GT;
        case TokenType::GE:  return CompareOp::GE;
        default:
            throw SqlParseError("Expected comparison operator");
    }
}

/*
 * comparison := column op literal
 *             | literal op column
 *             | column BETWEEN lit AND lit   (syntactic sugar for range)
 */
ExprPtr Parser::ParseComparison() {
    // handle BETWEEN specially: column BETWEEN low AND high
    // First, we expect either IDENT or NUMBER as left operand.
    Token left_tok = current_;
    if (left_tok.type != TokenType::IDENT && left_tok.type != TokenType::NUMBER) {
        throw SqlParseError("Expected column name or literal in comparison");
    }
    Advance();

    // Check for BETWEEN
    if (current_.type == TokenType::BETWEEN) {
        if (left_tok.type != TokenType::IDENT) {
            throw SqlParseError("BETWEEN left side must be a column");
        }
        std::string colname = left_tok.lexeme;
        Advance(); // consume BETWEEN

        if (current_.type != TokenType::NUMBER) {
            throw SqlParseError("Expected numeric low value after BETWEEN");
        }
        int64_t low = current_.number_value;
        Advance();

        Expect(TokenType::AND, "Expected AND in BETWEEN");

        if (current_.type != TokenType::NUMBER) {
            throw SqlParseError("Expected numeric high value after AND");
        }
        int64_t high = current_.number_value;
        Advance();

        // Build: (col >= low) AND (col <= high)
        ExprPtr col = std::make_unique<ColumnRefExpr>(colname);
        ExprPtr low_lit = std::make_unique<IntLiteralExpr>(low);
        ExprPtr high_lit = std::make_unique<IntLiteralExpr>(high);

        ExprPtr ge = std::make_unique<CompareExpr>(
            std::move(col),
            CompareOp::GE,
            std::move(low_lit));

        ExprPtr col2 = std::make_unique<ColumnRefExpr>(colname);
        ExprPtr le = std::make_unique<CompareExpr>(
            std::move(col2),
            CompareOp::LE,
            std::move(high_lit));

        return std::make_unique<BoolExpr>(std::move(ge), BoolOp::AND, std::move(le));
    }

    // Standard comparison: X op Y
    Token op_tok = current_;
    CompareOp op = ParseCompareOp(op_tok);
    Advance();

    Token right_tok = current_;
    if (right_tok.type != TokenType::IDENT && right_tok.type != TokenType::NUMBER) {
        throw SqlParseError("Expected column name or literal on right side of comparison");
    }
    Advance();

    // Build expression nodes
    ExprPtr left_expr;
    ExprPtr right_expr;

    if (left_tok.type == TokenType::IDENT) {
        left_expr = std::make_unique<ColumnRefExpr>(left_tok.lexeme);
    } else { // NUMBER
        left_expr = std::make_unique<IntLiteralExpr>(left_tok.number_value);
    }

    if (right_tok.type == TokenType::IDENT) {
        right_expr = std::make_unique<ColumnRefExpr>(right_tok.lexeme);
    } else { // NUMBER
        right_expr = std::make_unique<IntLiteralExpr>(right_tok.number_value);
    }

    return std::make_unique<CompareExpr>(std::move(left_expr), op, std::move(right_expr));
}

InsertStatement Parser::ParseInsert() {
    InsertStatement stmt;

    // We assume current_ is first token;
    // if caller didn't advance, we expect INSERT here.
    if (current_.type != TokenType::INSERT) {
        throw SqlParseError("Expected INSERT at beginning of INSERT statement");
    }
    Advance(); // consume INSERT

    Expect(TokenType::INTO, "Expected INTO after INSERT");

    if (current_.type != TokenType::IDENT) {
        throw SqlParseError("Expected table name after INSERT INTO");
    }
    stmt.table_name = current_.lexeme;
    Advance(); // table name

    Expect(TokenType::VALUES, "Expected VALUES after INSERT INTO <table>");

    Expect(TokenType::LPAREN, "Expected '(' after VALUES");

    // Parse comma-separated list of numeric literals
    if (current_.type != TokenType::NUMBER) {
        throw SqlParseError("Expected at least one numeric literal in VALUES");
    }
    while (true) {
        if (current_.type != TokenType::NUMBER) {
            throw SqlParseError("Expected numeric literal in VALUES");
        }
        stmt.values.push_back(current_.number_value);
        Advance();

        if (current_.type == TokenType::COMMA) {
            Advance();
            continue;
        }
        break;
    }

    Expect(TokenType::RPAREN, "Expected ')' after VALUES list");

    // Optional semicolon
    if (current_.type == TokenType::SEMICOLON) {
        Advance();
    }

    if (current_.type != TokenType::END_OF_FILE &&
        current_.type != TokenType::SEMICOLON) {
        throw SqlParseError("Unexpected trailing tokens after INSERT statement");
        }

    return stmt;
}
