/*
 * Token.h — faithful port of Token.cs / TokenList.cs / Interfaces.
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "SqlConstants.h"
#include <string>
#include <vector>

namespace pmsf {

struct Token {
    SqlTokenType type;
    std::string  value;
    Token() : type(SqlTokenType::OtherNode) {}
    Token(SqlTokenType t, std::string v) : type(t), value(std::move(v)) {}
};

// List<IToken> + HasUnfinishedToken (the marker fields are debug-only and unused
// by the parser, so they are omitted).
class TokenList {
public:
    std::vector<Token> tokens;
    bool hasUnfinishedToken = false;

    size_t size() const { return tokens.size(); }
    bool empty() const { return tokens.empty(); }
    const Token& operator[](size_t i) const { return tokens[i]; }
    Token& operator[](size_t i) { return tokens[i]; }
    void add(SqlTokenType t, const std::string& v) { tokens.push_back(Token(t, v)); }
    void add(const Token& t) { tokens.push_back(t); }
};

} // namespace pmsf
