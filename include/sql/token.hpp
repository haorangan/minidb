//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include <string>

enum class TokenType {
    // single-char
    STAR,
    COMMA,
    SEMICOLON,
    LPAREN,
    RPAREN,

    // operators
    EQ,     // =
    NEQ,    // != or <>
    LT,     // <
    LE,     // <=
    GT,     // >
    GE,     // >=

    // literals / identifiers
    IDENT,   // table/column names
    NUMBER,  // integer literals

    // keywords
    SELECT,
    FROM,
    WHERE,
    AND,
    OR,
    NOT,
    BETWEEN,
    AS,
    INSERT,
    INTO,
    VALUES,
    CREATE,
    TABLE,
    INT_TYPE,

    ORDER,
    BY,
    LIMIT,
    ASC,
    DESC,

    JOIN,
    INNER,
    ON,
    GROUP,
    EXPLAIN,
    BEGIN,
    COMMIT,
    ROLLBACK,

    END_OF_FILE,
    UNKNOWN
};

struct Token {
    TokenType type;
    std::string lexeme;
    int64_t number_value = 0; // used if type == NUMBER

    Token() : type(TokenType::UNKNOWN) {}
    Token(TokenType t, std::string l) : type(t), lexeme(std::move(l)) {}
    Token(TokenType t, std::string l, int64_t v) : type(t), lexeme(std::move(l)), number_value(v) {}
};
