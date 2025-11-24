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
    if (upper == "INSERT")  return TokenType::INSERT;
    if (upper == "INTO")    return TokenType::INTO;
    if (upper == "VALUES")  return TokenType::VALUES;
    if (upper == "CREATE")  return TokenType::CREATE;
    if (upper == "TABLE")   return TokenType::TABLE;
    if (upper == "INT")     return TokenType::INT_TYPE;
    if (upper == "ORDER")   return TokenType::ORDER;
    if (upper == "BY")      return TokenType::BY;
    if (upper == "LIMIT")   return TokenType::LIMIT;
    if (upper == "ASC")     return TokenType::ASC;
    if (upper == "DESC")    return TokenType::DESC;
    if (upper == "JOIN")    return TokenType::JOIN;
    if (upper == "INNER")   return TokenType::INNER;
    if (upper == "ON")      return TokenType::ON;
    if (upper == "GROUP")   return TokenType::GROUP;
    if (upper == "EXPLAIN") return TokenType::EXPLAIN;
    if (upper == "BEGIN")    return TokenType::BEGIN;
    if (upper == "COMMIT")   return TokenType::COMMIT;
    if (upper == "ROLLBACK") return TokenType::ROLLBACK;
    return TokenType::IDENT;
}

Token Lexer::LexIdentOrKeyword() {
    size_t start = pos_;
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    while (is_ident_char(CurrentChar())) {
        Advance();
    }
    // Qualified name: "table.column" lexes as one identifier
    if (CurrentChar() == '.' && pos_ + 1 < input_.size() &&
        (std::isalpha(static_cast<unsigned char>(input_[pos_ + 1])) || input_[pos_ + 1] == '_')) {
        Advance();
        while (is_ident_char(CurrentChar())) {
            Advance();
        }
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