/*
 * Parser.h — faithful port of Parsers/TSqlStandardParser.cs.
 *
 * Builds the parse-tree Node from a TokenList (the centerpiece). Public entry
 * is parseSQL(); the keyword classification table is shared.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "Token.h"
#include "Node.h"
#include <string>
#include <unordered_map>

namespace pmsf {

enum class KeywordType {
    OperatorKeyword,
    FunctionKeyword,
    DataTypeKeyword,
    OtherKeyword
};

// The static keyword classification table (552 entries).
const std::unordered_map<std::string, KeywordType>& keywordList();

// Tokenize → parse. Returns the root node of the parse tree (sets the
// ANAME_ERRORFOUND attribute on the root if a parse error was encountered).
NodeRef parseSQL(const TokenList& tokenList);

} // namespace pmsf
