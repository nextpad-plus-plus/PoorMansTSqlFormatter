/*
 * Formatter.h — faithful port of Formatters/TSqlStandardFormatter.cs
 * (plus the TSqlIdentityFormatter / TSqlObfuscatingFormatter used for the
 * [noformat] / [minify] special regions).
 *
 * Walks the parse-tree Node and renders formatted T-SQL text per the options.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "Node.h"
#include "Options.h"
#include <string>
#include <unordered_map>

namespace pmsf {

class TSqlStandardFormatter {
public:
    TSqlStandardFormatter() : TSqlStandardFormatter(TSqlStandardFormatterOptions()) {}
    explicit TSqlStandardFormatter(const TSqlStandardFormatterOptions& options);

    std::string formatSQLTree(Node* sqlTreeDoc);

    const TSqlStandardFormatterOptions& options() const { return options_; }

private:
    TSqlStandardFormatterOptions options_;
    std::unordered_map<std::string, std::string> keywordMapping_;  // empty unless standardizing
    std::string errorOutputPrefix_;
};

// Re-emits the parsed content verbatim (used for [noformat] regions).
class TSqlIdentityFormatter {
public:
    std::string formatSQLTree(Node* sqlTreeDoc);
};

// Minifies (collapses whitespace, drops comments) — used for [minify] regions.
class TSqlObfuscatingFormatter {
public:
    std::string formatSQLTree(Node* sqlTreeDoc);
};

} // namespace pmsf
