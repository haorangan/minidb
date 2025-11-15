//
// Created by Haoran Gan on 11/15/25.
//

#pragma once

#include "sql/token.hpp"
#include <string>

class Lexer {
public:
    explicit Lexer(const std::string &input);

    Token NextToken();
    const Token &PeekToken();

private:
    std::string input_;
    size_t pos_;
    Token peek_;
    bool has_peek_;

    void SkipWhitespace();
    char CurrentChar() const;
    char Advance();
    Token LexNumber();
    Token LexIdentOrKeyword();
};
