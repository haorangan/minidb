//
// Created by Haoran Gan on 11/15/25.
//

#include "sql/lexer.hpp"

#include <cctype>

Lexer::Lexer(const std::string &input)
    : input_(input), pos_(0), has_peek_(false) {}

char Lexer::CurrentChar() const {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_];
}

char Lexer::Advance() {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_++];
}

void Lexer::SkipWhitespace() {
    while (std::isspace(static_cast<unsigned char>(CurrentChar()))) {
        Advance();
    }
}

Token Lexer::LexNumber() {
    size_t start = pos_;
    while (std::isdigit(static_cast<unsigned char>(CurrentChar()))) {
        Advance();
    }
    std::string text = input_.substr(start, pos_ - start);
    int64_t value = std::stoll(text);
    return {TokenType::NUMBER, text, value};
}

static TokenType KeywordType(const std::string &upper) {
    if (upper == "SELECT")  return TokenType::SELECT;
    if (upper == "FROM")    return TokenType::FROM;
    if (upper == "WHERE")   return TokenType::WHERE;
    if (upper == "AND")     return TokenType::AND;
    if (upper == "OR")      return TokenType::OR;
    if (upper == "NOT")     return TokenType::NOT;
    if (upper == "BETWEEN") return TokenType::BETWEEN;
    if (upper == "AS")      return TokenType::AS;
    return TokenType::IDENT;
}

Token Lexer::LexIdentOrKeyword() {
    size_t start = pos_;
    while (std::isalnum(static_cast<unsigned char>(CurrentChar())) || CurrentChar() == '_') {
        Advance();
    }
    std::string text = input_.substr(start, pos_ - start);
    std::string upper = text;
    for (char &c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    TokenType ttype = KeywordType(upper);
    return {ttype, text};
}

Token Lexer::NextToken() {
    if (has_peek_) {
        has_peek_ = false;
        return peek_;
    }

    SkipWhitespace();
    char c = CurrentChar();
    if (c == '\0') {
        return {TokenType::END_OF_FILE, ""};
    }

    // single-char tokens
    if (c == '*') { Advance(); return {TokenType::STAR, "*"}; }
    if (c == ',') { Advance(); return {TokenType::COMMA, ","}; }
    if (c == ';') { Advance(); return {TokenType::SEMICOLON, ";"}; }
    if (c == '(') { Advance(); return {TokenType::LPAREN, "("}; }
    if (c == ')') { Advance(); return {TokenType::RPAREN, ")"}; }

    // operators and multi-char combos
    if (c == '=') {
        Advance();
        return {TokenType::EQ, "="};
    }
    if (c == '<') {
        Advance();
        if (CurrentChar() == '=') {
            Advance();
            return {TokenType::LE, "<="};
        } else if (CurrentChar() == '>') {
            Advance();
            return {TokenType::NEQ, "<>"};
        }
        return {TokenType::LT, "<"};
    }
    if (c == '>') {
        Advance();
        if (CurrentChar() == '=') {
            Advance();
            return {TokenType::GE, ">="};
        }
        return {TokenType::GT, ">"};
    }
    if (c == '!') {
        Advance();
        if (CurrentChar() == '=') {
            Advance();
            return {TokenType::NEQ, "!="};
        }
        return {TokenType::UNKNOWN, "!"};
    }

    // numbers
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return LexNumber();
    }

    // identifier or keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return LexIdentOrKeyword();
    }

    // unknown char
    Advance();
    return {TokenType::UNKNOWN, std::string(1, c)};
}

const Token &Lexer::PeekToken() {
    if (!has_peek_) {
        peek_ = NextToken();
        has_peek_ = true;
    }
    return peek_;
}