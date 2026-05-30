/*
 * Tokenizer.h — faithful port of Tokenizers/TSqlStandardTokenizer.cs (+ the
 * trivial SimplifiedStringReader). Produces a TokenList from a UTF-8 SQL string.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "Token.h"
#include <string>

namespace pmsf {

TokenList tokenizeSQL(const std::string& inputUtf8);

} // namespace pmsf
