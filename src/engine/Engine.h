/*
 * Engine.h — the top-level pipeline (SqlFormattingManager.cs:66):
 *   tokenize → parse (build tree, flag errors) → format.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "Tokenizer.h"
#include "Parser.h"
#include "Formatter.h"
#include <string>

namespace pmsf {

inline std::string formatSql(const std::string& input, const TSqlStandardFormatterOptions& options) {
    TokenList tokens = tokenizeSQL(input);
    NodeRef tree = parseSQL(tokens);
    TSqlStandardFormatter formatter(options);
    return formatter.formatSQLTree(tree.get());
}

inline std::string formatSql(const std::string& input) {
    return formatSql(input, TSqlStandardFormatterOptions());
}

} // namespace pmsf
