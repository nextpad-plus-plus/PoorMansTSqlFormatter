/*
 * KeywordRemapping.h — faithful port of StandardKeywordRemapping.cs.
 *
 * The keyword-standardization map (PROC→PROCEDURE, JOIN→INNER JOIN, …), applied
 * by the formatter only when Options.keywordStandardization is enabled.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include <string>
#include <unordered_map>

namespace pmsf {

inline const std::unordered_map<std::string, std::string>& standardKeywordRemapping() {
    static const std::unordered_map<std::string, std::string> m = {
        {"PROC", "PROCEDURE"},
        {"LEFT OUTER JOIN", "LEFT JOIN"},
        {"RIGHT OUTER JOIN", "RIGHT JOIN"},
        {"FULL OUTER JOIN", "FULL JOIN"},
        {"JOIN", "INNER JOIN"},
        {"TRAN", "TRANSACTION"},
        {"BEGIN TRAN", "BEGIN TRANSACTION"},
        {"COMMIT TRAN", "COMMIT TRANSACTION"},
        {"ROLLBACK TRAN", "ROLLBACK TRANSACTION"},
        {"BINARY VARYING", "VARBINARY"},
        {"CHAR VARYING", "VARCHAR"},
        {"CHARACTER", "CHAR"},
        {"CHARACTER VARYING", "VARCHAR"},
        {"DEC", "DECIMAL"},
        {"DOUBLE PRECISION", "FLOAT"},
        {"INTEGER", "INT"},
        {"NATIONAL CHARACTER", "NCHAR"},
        {"NATIONAL CHAR", "NCHAR"},
        {"NATIONAL CHARACTER VARYING", "NVARCHAR"},
        {"NATIONAL CHAR VARYING", "NVARCHAR"},
        {"NATIONAL TEXT", "NTEXT"},
        {"OUT", "OUTPUT"},
    };
    return m;
}

} // namespace pmsf
