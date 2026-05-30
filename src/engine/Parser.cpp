/*
 * Parser.cpp — see Parser.h. Faithful port of TSqlStandardParser.cs.
 *
 * This file currently contains the keyword table, the detection regexes and all
 * the support helpers. The main ParseSQL token loop is implemented in
 * ParserMain.cpp (split out for manageability).
 */
#include "Parser.h"
#include "ParseTree.h"
#include "StrUtil.h"
#include <regex>
#include <algorithm>
#include <stdexcept>

namespace pmsf {

using namespace SC;

// ── keyword classification table (552 entries, generated from the C# source) ─
const std::unordered_map<std::string, KeywordType>& keywordList() {
    static const std::unordered_map<std::string, KeywordType> kw = {
        #include "KeywordList.inc"
    };
    return kw;
}

// ── detection regexes (TSqlStandardParser statics) ──────────────────────────
// _CursorDetector's identifier char-class (a huge ES5 unicode table in the C#)
// is simplified to \S+ — any single non-whitespace identifier token — which is
// faithful for all real cursor names (ASCII + UTF-8 letters).
const std::regex& joinDetector() {
    static const std::regex r("^((RIGHT|INNER|LEFT|CROSS|FULL) )?(OUTER )?((HASH|LOOP|MERGE|REMOTE) )?(JOIN|APPLY) ");
    return r;
}
const std::regex& cursorDetector() {
    static const std::regex r("^DECLARE \\S+ ((INSENSITIVE|SCROLL) ){0,2}CURSOR ");
    return r;
}
const std::regex& triggerConditionDetector() {
    static const std::regex r("^(FOR|AFTER|INSTEAD OF)( (INSERT|UPDATE|DELETE) (, (INSERT|UPDATE|DELETE) )?(, (INSERT|UPDATE|DELETE) )?)");
    return r;
}

namespace detail {

// tokenList.GetRangeByIndex(from, to) — inclusive slice.
std::vector<Token> rangeByIndex(const TokenList& tl, int from, int to) {
    std::vector<Token> out;
    for (int i = from; i <= to && i < (int)tl.size(); ++i) out.push_back(tl[i]);
    return out;
}

std::string getEquivalentSqlNodeName(SqlTokenType tokenType) {
    switch (tokenType) {
        case SqlTokenType::WhiteSpace:               return ENAME_WHITESPACE;
        case SqlTokenType::SingleLineComment:        return ENAME_COMMENT_SINGLELINE;
        case SqlTokenType::SingleLineCommentCStyle:  return ENAME_COMMENT_SINGLELINE_CSTYLE;
        case SqlTokenType::MultiLineComment:         return ENAME_COMMENT_MULTILINE;
        case SqlTokenType::BracketQuotedName:        return ENAME_BRACKET_QUOTED_NAME;
        case SqlTokenType::Asterisk:                 return ENAME_ASTERISK;
        case SqlTokenType::EqualsSign:               return ENAME_EQUALSSIGN;
        case SqlTokenType::Comma:                    return ENAME_COMMA;
        case SqlTokenType::Period:                   return ENAME_PERIOD;
        case SqlTokenType::NationalString:           return ENAME_NSTRING;
        case SqlTokenType::String:                   return ENAME_STRING;
        case SqlTokenType::QuotedString:             return ENAME_QUOTED_STRING;
        case SqlTokenType::OtherOperator:            return ENAME_OTHEROPERATOR;
        case SqlTokenType::Number:                   return ENAME_NUMBER_VALUE;
        case SqlTokenType::MonetaryValue:            return ENAME_MONETARY_VALUE;
        case SqlTokenType::BinaryValue:              return ENAME_BINARY_VALUE;
        case SqlTokenType::PseudoName:               return ENAME_PSEUDONAME;
        default: throw std::runtime_error("Mapping not found for provided Token Type");
    }
}

bool contentStartsWithKeyword(Node* providedContainer, const char* contentToMatch) {
    auto kids = childrenExcludingNames(providedContainer, ENAMELIST_NONCONTENT);
    Node* first = kids.empty() ? nullptr : kids.front();
    bool haveKeyword = false;
    std::string keywordUpperValue;

    if (first && first->name == ENAME_OTHERKEYWORD) {
        keywordUpperValue = toUpperAscii(first->textValue);
        haveKeyword = true;
    }
    if (first && first->name == ENAME_COMPOUNDKEYWORD) {
        keywordUpperValue = first->getAttributeValue(ANAME_SIMPLETEXT);
        haveKeyword = true;
    }

    if (haveKeyword) {
        std::string match = contentToMatch ? contentToMatch : "";
        return keywordUpperValue == match || startsWith(keywordUpperValue, match + " ");
    }
    // contentToMatch == null means we were looking for a NON-keyword.
    return contentToMatch == nullptr;
}

void appendNodesWithMapping(ParseTree& sqlTree, const std::vector<Token>& tokens,
                            const std::string& otherTokenMappingName, Node* targetContainer) {
    for (const auto& token : tokens) {
        std::string elementName = (token.type == SqlTokenType::OtherNode)
            ? otherTokenMappingName : getEquivalentSqlNodeName(token.type);
        sqlTree.saveNewElement(elementName, token.value, targetContainer);
    }
}

std::string extractTokensString(const TokenList& tokenList, const std::vector<int>& positions) {
    std::string out;
    for (int pos : positions) {
        if (tokenList[pos].type == SqlTokenType::Comma) out += ",";
        else out += toUpperAscii(tokenList[pos].value);
        out += " ";
    }
    return out;
}

void processCompoundKeyword(const TokenList& tokenList, ParseTree& sqlTree, Node* targetContainer,
                            int& tokenID, const std::vector<int>& significantTokenPositions, int keywordCount) {
    Node* compoundKeyword = sqlTree.saveNewElement(ENAME_COMPOUNDKEYWORD, "", targetContainer);
    std::vector<int> firstN(significantTokenPositions.begin(), significantTokenPositions.begin() + keywordCount);
    std::string targetText = trimEnd(extractTokensString(tokenList, firstN));
    compoundKeyword->setAttribute(ANAME_SIMPLETEXT, targetText);
    appendNodesWithMapping(sqlTree,
                           rangeByIndex(tokenList, significantTokenPositions[0], significantTokenPositions[keywordCount - 1]),
                           ENAME_OTHERKEYWORD, compoundKeyword);
    tokenID = significantTokenPositions[keywordCount - 1];
}

void processCompoundKeywordWithError(const TokenList& tokenList, ParseTree& sqlTree, Node* currentContainerElement,
                                     int& tokenID, const std::vector<int>& significantTokenPositions, int keywordCount) {
    processCompoundKeyword(tokenList, sqlTree, currentContainerElement, tokenID, significantTokenPositions, keywordCount);
    sqlTree.setError();
}

// Build a normalized "KEYWORD PHRASE " starting at tokenID; fills the parallel
// rawKeywordParts / tokenCounts / overflowNodes lists.
std::string getKeywordMatchPhrase(const TokenList& tokenList, int tokenID,
                                  std::vector<std::string>& rawKeywordParts,
                                  std::vector<int>& tokenCounts,
                                  std::vector<std::vector<Token>>& overflowNodes) {
    std::string phrase;
    int phraseComponentsFound = 0;
    rawKeywordParts.clear();
    overflowNodes.clear();
    tokenCounts.clear();
    std::string precedingWhitespace;
    int originalTokenID = tokenID;

    while (tokenID < (int)tokenList.size() && phraseComponentsFound < 7) {
        SqlTokenType t = tokenList[tokenID].type;
        if (t == SqlTokenType::OtherNode || t == SqlTokenType::BracketQuotedName || t == SqlTokenType::Comma) {
            phrase += toUpperAscii(tokenList[tokenID].value) + " ";
            phraseComponentsFound++;
            rawKeywordParts.push_back(precedingWhitespace + tokenList[tokenID].value);

            tokenID++;
            tokenCounts.push_back(tokenID - originalTokenID);

            overflowNodes.push_back(std::vector<Token>());
            precedingWhitespace.clear();
            while (tokenID < (int)tokenList.size()
                   && (tokenList[tokenID].type == SqlTokenType::WhiteSpace
                       || tokenList[tokenID].type == SqlTokenType::SingleLineComment
                       || tokenList[tokenID].type == SqlTokenType::MultiLineComment)) {
                if (tokenList[tokenID].type == SqlTokenType::WhiteSpace)
                    precedingWhitespace += tokenList[tokenID].value;
                else
                    overflowNodes[phraseComponentsFound - 1].push_back(tokenList[tokenID]);
                tokenID++;
            }
        } else {
            break;
        }
    }
    return phrase;
}

std::vector<int> getSignificantTokenPositions(const TokenList& tokenList, int tokenID, int searchDistance) {
    std::vector<int> positions;
    while (tokenID < (int)tokenList.size() && (int)positions.size() < searchDistance) {
        SqlTokenType t = tokenList[tokenID].type;
        if (t == SqlTokenType::OtherNode || t == SqlTokenType::BracketQuotedName || t == SqlTokenType::Comma) {
            positions.push_back(tokenID);
            tokenID++;
            while (tokenID < (int)tokenList.size()
                   && (tokenList[tokenID].type == SqlTokenType::WhiteSpace
                       || tokenList[tokenID].type == SqlTokenType::SingleLineComment
                       || tokenList[tokenID].type == SqlTokenType::MultiLineComment)) {
                tokenID++;
            }
        } else {
            break;
        }
    }
    return positions;
}

std::string getCompoundKeyword(int& tokenID, int compoundKeywordCount,
                               const std::vector<int>& compoundKeywordTokenCounts,
                               const std::vector<std::string>& compoundKeywordRawStrings) {
    tokenID += compoundKeywordTokenCounts[compoundKeywordCount - 1] - 1;
    std::string out;
    for (int i = 0; i < compoundKeywordCount; i++) out += compoundKeywordRawStrings[i];
    return out;
}

Node* processCompoundKeyword(ParseTree& sqlTree, const std::string& newElementName, int& tokenID,
                             Node* /*currentContainerElement*/, int compoundKeywordCount,
                             const std::vector<int>& compoundKeywordTokenCounts,
                             const std::vector<std::string>& compoundKeywordRawStrings) {
    NodeRef newElement = createNode(newElementName,
        getCompoundKeyword(tokenID, compoundKeywordCount, compoundKeywordTokenCounts, compoundKeywordRawStrings));
    Node* raw = newElement.get();
    sqlTree.currentContainer()->addChild(newElement);
    return raw;
}

bool isStatementStarter(const Token& token) {
    if (token.type != SqlTokenType::OtherNode) return false;
    std::string u = toUpperAscii(token.value);
    static const std::vector<std::string> starters = {
        "ALTER","BACKUP","BREAK","CLOSE","CHECKPOINT","COMMIT","CONTINUE","CREATE","DBCC",
        "DEALLOCATE","DELETE","DECLARE","DENY","DROP","EXEC","EXECUTE","FETCH","GOTO","GRANT",
        "IF","INSERT","KILL","MERGE","OPEN","PRINT","RAISERROR","RECONFIGURE","RESTORE","RETURN",
        "REVERT","REVOKE","SELECT","SET","SETUSER","SHUTDOWN","TRUNCATE","UPDATE","USE","WAITFOR","WHILE"
    };
    for (const auto& s : starters) if (u == s) return true;
    return false;
}

bool isClauseStarter(const Token& token) {
    if (token.type != SqlTokenType::OtherNode) return false;
    std::string u = toUpperAscii(token.value);
    static const std::vector<std::string> starters = {
        "DELETE","EXCEPT","FOR","FROM","GROUP","HAVING","INNER","INTERSECT","INTO","INSERT",
        "MERGE","ORDER","OUTPUT","PIVOT","RETURNS","SELECT","UNION","UNPIVOT","UPDATE","USING",
        "VALUES","WHERE","WITH"
    };
    for (const auto& s : starters) if (u == s) return true;
    return false;
}

bool isLatestTokenADDLDetailValue(ParseTree& sqlTree) {
    auto content = childrenExcludingNames(sqlTree.currentContainer(), ENAMELIST_NONCONTENT);
    Node* latest = content.empty() ? nullptr : content.back();
    if (latest && (latest->name == ENAME_OTHERKEYWORD || latest->name == ENAME_DATATYPE_KEYWORD
                   || latest->name == ENAME_COMPOUNDKEYWORD)) {
        std::string u = (latest->name == ENAME_COMPOUNDKEYWORD)
            ? latest->getAttributeValue(ANAME_SIMPLETEXT) : toUpperAscii(latest->textValue);
        return u == "NVARCHAR" || u == "VARCHAR" || u == "DECIMAL" || u == "DEC" || u == "NUMERIC"
            || u == "VARBINARY" || u == "DEFAULT" || u == "IDENTITY" || u == "XML"
            || endsWith(u, "VARYING") || endsWith(u, "CHAR") || endsWith(u, "CHARACTER")
            || u == "FLOAT" || u == "DATETIMEOFFSET" || u == "DATETIME2" || u == "TIME";
    }
    return false;
}

bool isLatestTokenAComma(ParseTree& sqlTree) {
    auto content = childrenExcludingNames(sqlTree.currentContainer(), ENAMELIST_NONCONTENT);
    Node* latest = content.empty() ? nullptr : content.back();
    return latest && latest->name == ENAME_COMMA;
}

bool isLatestTokenAMiscName(ParseTree& sqlTree) {
    auto content = childrenExcludingNames(sqlTree.currentContainer(), ENAMELIST_NONCONTENT);
    Node* latest = content.empty() ? nullptr : content.back();
    if (!latest) return false;
    std::string testValue = toUpperAscii(latest->textValue);
    if (latest->name == ENAME_BRACKET_QUOTED_NAME) return true;
    if ((latest->name == ENAME_OTHERNODE || latest->name == ENAME_FUNCTION_KEYWORD)
        && !(testValue == "AND" || testValue == "OR" || testValue == "NOT" || testValue == "BETWEEN"
             || testValue == "LIKE" || testValue == "CONTAINS" || testValue == "EXISTS"
             || testValue == "FREETEXT" || testValue == "IN" || testValue == "ALL"
             || testValue == "SOME" || testValue == "ANY" || testValue == "FROM"
             || testValue == "JOIN" || endsWith(testValue, " JOIN") || testValue == "UNION"
             || testValue == "UNION ALL" || testValue == "USING" || testValue == "AS"
             || endsWith(testValue, " APPLY")))
        return true;
    return false;
}

bool hasLineBreakRe(const std::string& s) {
    return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

bool isLineBreakingWhiteSpaceOrComment(const Token& token) {
    return (token.type == SqlTokenType::WhiteSpace && hasLineBreakRe(token.value))
        || token.type == SqlTokenType::SingleLineComment;
}

bool isFollowedByLineBreakingWhiteSpaceOrSingleLineCommentOrEnd(const TokenList& tokenList, int tokenID) {
    int curr = tokenID + 1;
    while ((int)tokenList.size() >= curr + 1) {
        if (tokenList[curr].type == SqlTokenType::SingleLineComment) return true;
        else if (tokenList[curr].type == SqlTokenType::WhiteSpace) {
            if (hasLineBreakRe(tokenList[curr].value)) return true;
            else curr++;
        } else return false;
    }
    return true;
}

} // namespace detail

// ── ParseSQL — the main token loop (faithful port of TSqlStandardParser.ParseSQL) ─
namespace {
// C# String.Split(' ') keeps empty entries → pieces = spaces + 1.
int wordCountBySpace(const std::string& s) {
    return (int)std::count(s.begin(), s.end(), ' ') + 1;
}
// C# Split(' ', RemoveEmptyEntries).Length → count of non-empty space-delimited runs.
int wordCountNonEmpty(const std::string& s) {
    int n = 0; bool in = false;
    for (char c : s) { if (c == ' ') in = false; else if (!in) { in = true; ++n; } }
    return n;
}
}

NodeRef parseSQL(const TokenList& tokenList) {
    using namespace detail;
    ParseTree sqlTree(ENAME_SQL_ROOT);
    sqlTree.startNewStatement();

    int tokenCount = (int)tokenList.size();
    int tokenID = 0;
    while (tokenID < tokenCount) {
        const Token& token = tokenList[tokenID];

        switch (token.type) {
            case SqlTokenType::OpenParens: {
                Node* firstNonCommentParensSibling = sqlTree.getFirstNonWhitespaceNonCommentChildElement(sqlTree.currentContainer());
                Node* lastNonCommentParensSibling = sqlTree.getLastNonWhitespaceNonCommentChildElement(sqlTree.currentContainer());
                bool isInsertOrValuesClause =
                    firstNonCommentParensSibling != nullptr
                    && (
                        (firstNonCommentParensSibling->name == ENAME_OTHERKEYWORD
                         && startsWith(toUpperAscii(firstNonCommentParensSibling->textValue), "INSERT"))
                        || (firstNonCommentParensSibling->name == ENAME_COMPOUNDKEYWORD
                            && startsWith(firstNonCommentParensSibling->getAttributeValue(ANAME_SIMPLETEXT), "INSERT "))
                        || (firstNonCommentParensSibling->name == ENAME_OTHERKEYWORD
                            && startsWith(toUpperAscii(firstNonCommentParensSibling->textValue), "VALUES")));

                if (sqlTree.currentContainer()->name == ENAME_CTE_ALIAS
                    && sqlTree.currentContainer()->parent->name == ENAME_CTE_WITH_CLAUSE) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDL_PARENS, ""));
                } else if (sqlTree.currentContainer()->name == ENAME_CONTAINER_GENERALCONTENT
                           && sqlTree.currentContainer()->parent->name == ENAME_CTE_AS_BLOCK) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_SELECTIONTARGET_PARENS, ""));
                } else if (firstNonCommentParensSibling == nullptr
                           && sqlTree.currentContainer()->name == ENAME_SELECTIONTARGET) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_SELECTIONTARGET_PARENS, ""));
                } else if (firstNonCommentParensSibling != nullptr
                           && firstNonCommentParensSibling->name == ENAME_SET_OPERATOR_CLAUSE) {
                    sqlTree.considerStartingNewClause();
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_SELECTIONTARGET_PARENS, ""));
                } else if (isLatestTokenADDLDetailValue(sqlTree)) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDLDETAIL_PARENS, ""));
                } else if (sqlTree.currentContainer()->name == ENAME_DDL_PROCEDURAL_BLOCK
                           || sqlTree.currentContainer()->name == ENAME_DDL_OTHER_BLOCK
                           || sqlTree.currentContainer()->name == ENAME_DDL_DECLARE_BLOCK
                           || (sqlTree.currentContainer()->name == ENAME_SQL_CLAUSE
                               && firstNonCommentParensSibling != nullptr
                               && firstNonCommentParensSibling->name == ENAME_OTHERKEYWORD
                               && startsWith(toUpperAscii(firstNonCommentParensSibling->textValue), "OPTION"))
                           || isInsertOrValuesClause) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDL_PARENS, ""));
                } else if (lastNonCommentParensSibling != nullptr
                           && lastNonCommentParensSibling->name == ENAME_ALPHAOPERATOR
                           && toUpperAscii(lastNonCommentParensSibling->textValue) == "IN") {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_IN_PARENS, ""));
                } else if (isLatestTokenAMiscName(sqlTree)) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_FUNCTION_PARENS, ""));
                } else {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_EXPRESSION_PARENS, ""));
                }
                break;
            }

            case SqlTokenType::CloseParens: {
                sqlTree.escapeAnySingleOrPartialStatementContainers();
                const std::string& cn = sqlTree.currentContainer()->name;
                if (cn == ENAME_DDLDETAIL_PARENS || cn == ENAME_DDL_PARENS || cn == ENAME_FUNCTION_PARENS
                    || cn == ENAME_IN_PARENS || cn == ENAME_EXPRESSION_PARENS || cn == ENAME_SELECTIONTARGET_PARENS) {
                    sqlTree.moveToAncestorContainer(1);
                } else if (cn == ENAME_SQL_CLAUSE
                           && sqlTree.currentContainer()->parent->name == ENAME_SELECTIONTARGET_PARENS
                           && sqlTree.currentContainer()->parent->parent->name == ENAME_CONTAINER_GENERALCONTENT
                           && sqlTree.currentContainer()->parent->parent->parent->name == ENAME_CTE_AS_BLOCK) {
                    sqlTree.moveToAncestorContainer(4, ENAME_CTE_WITH_CLAUSE);
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CONTAINER_GENERALCONTENT, ""));
                } else if (cn == ENAME_SQL_CLAUSE
                           && (sqlTree.currentContainer()->parent->name == ENAME_EXPRESSION_PARENS
                               || sqlTree.currentContainer()->parent->name == ENAME_IN_PARENS
                               || sqlTree.currentContainer()->parent->name == ENAME_SELECTIONTARGET_PARENS)) {
                    sqlTree.moveToAncestorContainer(2);
                } else {
                    sqlTree.saveNewElementWithError(ENAME_OTHERNODE, ")");
                }
                break;
            }

            case SqlTokenType::OtherNode: {
                std::vector<int> significantTokenPositions = getSignificantTokenPositions(tokenList, tokenID, 7);
                std::string sts = extractTokensString(tokenList, significantTokenPositions);

                if (sqlTree.pathNameMatches(0, ENAME_PERMISSIONS_DETAIL)) {
                    if (startsWith(sts, "ON ")) {
                        sqlTree.moveToAncestorContainer(1, ENAME_PERMISSIONS_BLOCK);
                        sqlTree.startNewContainer(ENAME_PERMISSIONS_TARGET, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if (startsWith(sts, "TO ") || startsWith(sts, "FROM ")) {
                        sqlTree.moveToAncestorContainer(1, ENAME_PERMISSIONS_BLOCK);
                        sqlTree.startNewContainer(ENAME_PERMISSIONS_RECIPIENT, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "CREATE PROC") || startsWith(sts, "CREATE FUNC")
                           || startsWith(sts, "CREATE TRIGGER ") || startsWith(sts, "CREATE VIEW ")
                           || startsWith(sts, "ALTER PROC") || startsWith(sts, "ALTER FUNC")
                           || startsWith(sts, "ALTER TRIGGER ") || startsWith(sts, "ALTER VIEW ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDL_PROCEDURAL_BLOCK, ""));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (std::regex_search(sts, cursorDetector())) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CURSOR_DECLARATION, ""));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (sqlTree.pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK)
                           && std::regex_search(sts, triggerConditionDetector())) {
                    std::smatch tc;
                    std::regex_search(sts, tc, triggerConditionDetector());
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_TRIGGER_CONDITION, ""));
                    Node* triggerConditionType = sqlTree.saveNewElement(ENAME_COMPOUNDKEYWORD, "");
                    std::string typeSimpleText = tc[1].str();
                    triggerConditionType->setAttribute(ANAME_SIMPLETEXT, typeSimpleText);
                    int typeNodeCount = wordCountBySpace(typeSimpleText);
                    appendNodesWithMapping(sqlTree,
                        rangeByIndex(tokenList, significantTokenPositions[0], significantTokenPositions[typeNodeCount - 1]),
                        ENAME_OTHERKEYWORD, triggerConditionType);
                    int conditionNodeCount = wordCountBySpace(tc[2].str()) - 2;
                    appendNodesWithMapping(sqlTree,
                        rangeByIndex(tokenList, significantTokenPositions[typeNodeCount - 1] + 1,
                                     significantTokenPositions[typeNodeCount + conditionNodeCount - 1]),
                        ENAME_OTHERKEYWORD, sqlTree.currentContainer());
                    tokenID = significantTokenPositions[typeNodeCount + conditionNodeCount - 1];
                    sqlTree.moveToAncestorContainer(1, ENAME_DDL_PROCEDURAL_BLOCK);
                } else if (startsWith(sts, "FOR ")) {
                    sqlTree.escapeAnyBetweenConditions();
                    sqlTree.escapeAnySelectionTarget();
                    sqlTree.escapeJoinCondition();
                    if (sqlTree.pathNameMatches(0, ENAME_CURSOR_DECLARATION)) {
                        sqlTree.startNewContainer(ENAME_CURSOR_FOR_BLOCK, token.value, ENAME_CONTAINER_GENERALCONTENT);
                        sqlTree.startNewStatement();
                    } else if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                               && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                               && sqlTree.pathNameMatches(2, ENAME_CONTAINER_GENERALCONTENT)
                               && sqlTree.pathNameMatches(3, ENAME_CURSOR_FOR_BLOCK)) {
                        sqlTree.moveToAncestorContainer(4, ENAME_CURSOR_DECLARATION);
                        sqlTree.startNewContainer(ENAME_CURSOR_FOR_OPTIONS, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.considerStartingNewClause();
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "DECLARE ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDL_DECLARE_BLOCK, ""));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "CREATE ") || startsWith(sts, "ALTER ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_DDL_OTHER_BLOCK, ""));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "GRANT ") || startsWith(sts, "DENY ") || startsWith(sts, "REVOKE ")) {
                    if (startsWith(sts, "GRANT ")
                        && sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)
                        && sqlTree.pathNameMatches(2, ENAME_PERMISSIONS_BLOCK)
                        && sqlTree.getFirstNonWhitespaceNonCommentChildElement(sqlTree.currentContainer()) == nullptr) {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    } else {
                        sqlTree.considerStartingNewStatement();
                        sqlTree.startNewContainer(ENAME_PERMISSIONS_BLOCK, token.value, ENAME_PERMISSIONS_DETAIL);
                    }
                } else if (sqlTree.currentContainer()->name == ENAME_DDL_PROCEDURAL_BLOCK
                           && startsWith(sts, "RETURNS ")) {
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_DDL_RETURNS, ""));
                } else if (startsWith(sts, "AS ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK)) {
                        bool isDataTypeDefinition = false;
                        if (significantTokenPositions.size() > 1) {
                            auto it = keywordList().find(toUpperAscii(tokenList[significantTokenPositions[1]].value));
                            if (it != keywordList().end() && it->second == KeywordType::DataTypeKeyword)
                                isDataTypeDefinition = true;
                        }
                        if (isDataTypeDefinition) {
                            sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                        } else {
                            sqlTree.startNewContainer(ENAME_DDL_AS_BLOCK, token.value, ENAME_CONTAINER_GENERALCONTENT);
                            sqlTree.startNewStatement();
                        }
                    } else if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                               && sqlTree.pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)
                               && sqlTree.pathNameMatches(2, ENAME_DDL_PROCEDURAL_BLOCK)) {
                        sqlTree.moveToAncestorContainer(2, ENAME_DDL_PROCEDURAL_BLOCK);
                        sqlTree.startNewContainer(ENAME_DDL_AS_BLOCK, token.value, ENAME_CONTAINER_GENERALCONTENT);
                        sqlTree.startNewStatement();
                    } else if (sqlTree.pathNameMatches(0, ENAME_CTE_ALIAS)
                               && sqlTree.pathNameMatches(1, ENAME_CTE_WITH_CLAUSE)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_CTE_WITH_CLAUSE);
                        sqlTree.startNewContainer(ENAME_CTE_AS_BLOCK, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "BEGIN DISTRIBUTED TRANSACTION ") || startsWith(sts, "BEGIN DISTRIBUTED TRAN ")) {
                    sqlTree.considerStartingNewStatement();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_BEGIN_TRANSACTION, ""), tokenID, significantTokenPositions, 3);
                } else if (startsWith(sts, "BEGIN TRANSACTION ") || startsWith(sts, "BEGIN TRAN ")) {
                    sqlTree.considerStartingNewStatement();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_BEGIN_TRANSACTION, ""), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "SAVE TRANSACTION ") || startsWith(sts, "SAVE TRAN ")) {
                    sqlTree.considerStartingNewStatement();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_SAVE_TRANSACTION, ""), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "COMMIT TRANSACTION ") || startsWith(sts, "COMMIT TRAN ") || startsWith(sts, "COMMIT WORK ")) {
                    sqlTree.considerStartingNewStatement();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_COMMIT_TRANSACTION, ""), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "COMMIT ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_COMMIT_TRANSACTION, token.value));
                } else if (startsWith(sts, "ROLLBACK TRANSACTION ") || startsWith(sts, "ROLLBACK TRAN ") || startsWith(sts, "ROLLBACK WORK ")) {
                    sqlTree.considerStartingNewStatement();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_ROLLBACK_TRANSACTION, ""), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "ROLLBACK ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_ROLLBACK_TRANSACTION, token.value));
                } else if (startsWith(sts, "BEGIN TRY ")) {
                    sqlTree.considerStartingNewStatement();
                    Node* newTryBlock = sqlTree.saveNewElement(ENAME_TRY_BLOCK, "");
                    Node* tryContainerOpen = sqlTree.saveNewElement(ENAME_CONTAINER_OPEN, "", newTryBlock);
                    processCompoundKeyword(tokenList, sqlTree, tryContainerOpen, tokenID, significantTokenPositions, 2);
                    Node* tryMultiContainer = sqlTree.saveNewElement(ENAME_CONTAINER_MULTISTATEMENT, "", newTryBlock);
                    sqlTree.startNewStatement(tryMultiContainer);
                } else if (startsWith(sts, "BEGIN CATCH ")) {
                    sqlTree.considerStartingNewStatement();
                    Node* newCatchBlock = sqlTree.saveNewElement(ENAME_CATCH_BLOCK, "");
                    Node* catchContainerOpen = sqlTree.saveNewElement(ENAME_CONTAINER_OPEN, "", newCatchBlock);
                    processCompoundKeyword(tokenList, sqlTree, catchContainerOpen, tokenID, significantTokenPositions, 2);
                    Node* catchMultiContainer = sqlTree.saveNewElement(ENAME_CONTAINER_MULTISTATEMENT, "", newCatchBlock);
                    sqlTree.startNewStatement(catchMultiContainer);
                } else if (startsWith(sts, "BEGIN ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.startNewContainer(ENAME_BEGIN_END_BLOCK, token.value, ENAME_CONTAINER_MULTISTATEMENT);
                    sqlTree.startNewStatement();
                } else if (startsWith(sts, "MERGE ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    sqlTree.startNewContainer(ENAME_MERGE_CLAUSE, token.value, ENAME_MERGE_TARGET);
                } else if (startsWith(sts, "USING ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_MERGE_TARGET)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_MERGE_CLAUSE);
                        sqlTree.startNewContainer(ENAME_MERGE_USING, token.value, ENAME_SELECTIONTARGET);
                    } else {
                        sqlTree.saveNewElementWithError(ENAME_OTHERNODE, token.value);
                    }
                } else if (startsWith(sts, "ON ")) {
                    sqlTree.escapeAnySelectionTarget();
                    if (sqlTree.pathNameMatches(0, ENAME_MERGE_USING)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_MERGE_CLAUSE);
                        sqlTree.startNewContainer(ENAME_MERGE_CONDITION, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if (!sqlTree.pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK)
                               && !sqlTree.pathNameMatches(0, ENAME_DDL_OTHER_BLOCK)
                               && !sqlTree.pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)
                               && !sqlTree.pathNameMatches(0, ENAME_EXPRESSION_PARENS)
                               && !contentStartsWithKeyword(sqlTree.currentContainer(), "SET")) {
                        sqlTree.startNewContainer(ENAME_JOIN_ON_SECTION, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "CASE ")) {
                    sqlTree.startNewContainer(ENAME_CASE_STATEMENT, token.value, ENAME_CASE_INPUT);
                } else if (startsWith(sts, "WHEN ")) {
                    sqlTree.escapeMergeAction();
                    if (sqlTree.pathNameMatches(0, ENAME_CASE_INPUT)
                        || (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                            && sqlTree.pathNameMatches(1, ENAME_CASE_THEN))) {
                        if (sqlTree.pathNameMatches(0, ENAME_CASE_INPUT))
                            sqlTree.moveToAncestorContainer(1, ENAME_CASE_STATEMENT);
                        else
                            sqlTree.moveToAncestorContainer(3, ENAME_CASE_STATEMENT);
                        sqlTree.startNewContainer(ENAME_CASE_WHEN, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if ((sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                                && sqlTree.pathNameMatches(1, ENAME_MERGE_CONDITION))
                               || sqlTree.pathNameMatches(0, ENAME_MERGE_WHEN)) {
                        if (sqlTree.pathNameMatches(1, ENAME_MERGE_CONDITION))
                            sqlTree.moveToAncestorContainer(2, ENAME_MERGE_CLAUSE);
                        else
                            sqlTree.moveToAncestorContainer(1, ENAME_MERGE_CLAUSE);
                        sqlTree.startNewContainer(ENAME_MERGE_WHEN, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.saveNewElementWithError(ENAME_OTHERNODE, token.value);
                    }
                } else if (startsWith(sts, "THEN ")) {
                    sqlTree.escapeAnyBetweenConditions();
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_CASE_WHEN)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_CASE_WHEN);
                        sqlTree.startNewContainer(ENAME_CASE_THEN, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                               && sqlTree.pathNameMatches(1, ENAME_MERGE_WHEN)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_MERGE_WHEN);
                        sqlTree.startNewContainer(ENAME_MERGE_THEN, token.value, ENAME_MERGE_ACTION);
                        sqlTree.startNewStatement();
                    } else {
                        sqlTree.saveNewElementWithError(ENAME_OTHERNODE, token.value);
                    }
                } else if (startsWith(sts, "OUTPUT ")) {
                    bool isSprocArgument = false;
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                        && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                        && (contentStartsWithKeyword(sqlTree.currentContainer(), "EXEC")
                            || contentStartsWithKeyword(sqlTree.currentContainer(), "EXECUTE")
                            || contentStartsWithKeyword(sqlTree.currentContainer(), nullptr)))
                        isSprocArgument = true;
                    if (sqlTree.pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK))
                        isSprocArgument = true;
                    if (!isSprocArgument) {
                        sqlTree.escapeMergeAction();
                        sqlTree.considerStartingNewClause();
                    }
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "OPTION ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)) {
                        // "OPTION" here is NOT a new clause.
                    } else {
                        sqlTree.escapeMergeAction();
                        sqlTree.considerStartingNewClause();
                    }
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "END TRY ")) {
                    sqlTree.escapeAnySingleOrPartialStatementContainers();
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                        && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                        && sqlTree.pathNameMatches(2, ENAME_CONTAINER_MULTISTATEMENT)
                        && sqlTree.pathNameMatches(3, ENAME_TRY_BLOCK)) {
                        Node* tryBlock = sqlTree.currentContainer()->parent->parent->parent;
                        Node* tryContainerClose = sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, "", tryBlock);
                        processCompoundKeyword(tokenList, sqlTree, tryContainerClose, tokenID, significantTokenPositions, 2);
                        sqlTree.setCurrentContainer(tryBlock->parent);
                    } else {
                        processCompoundKeywordWithError(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                    }
                } else if (startsWith(sts, "END CATCH ")) {
                    sqlTree.escapeAnySingleOrPartialStatementContainers();
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                        && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                        && sqlTree.pathNameMatches(2, ENAME_CONTAINER_MULTISTATEMENT)
                        && sqlTree.pathNameMatches(3, ENAME_CATCH_BLOCK)) {
                        Node* catchBlock = sqlTree.currentContainer()->parent->parent->parent;
                        Node* catchContainerClose = sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, "", catchBlock);
                        processCompoundKeyword(tokenList, sqlTree, catchContainerClose, tokenID, significantTokenPositions, 2);
                        sqlTree.setCurrentContainer(catchBlock->parent);
                    } else {
                        processCompoundKeywordWithError(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                    }
                } else if (startsWith(sts, "END ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_CASE_THEN)) {
                        sqlTree.moveToAncestorContainer(3, ENAME_CASE_STATEMENT);
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, ""));
                        sqlTree.moveToAncestorContainer(1);
                    } else if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                               && sqlTree.pathNameMatches(1, ENAME_CASE_ELSE)) {
                        sqlTree.moveToAncestorContainer(2, ENAME_CASE_STATEMENT);
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, ""));
                        sqlTree.moveToAncestorContainer(1);
                    } else {
                        sqlTree.escapeAnySingleOrPartialStatementContainers();
                        if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                            && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                            && sqlTree.pathNameMatches(2, ENAME_CONTAINER_MULTISTATEMENT)
                            && sqlTree.pathNameMatches(3, ENAME_BEGIN_END_BLOCK)) {
                            Node* beginBlock = sqlTree.currentContainer()->parent->parent->parent;
                            Node* beginContainerClose = sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, "", beginBlock);
                            sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, beginContainerClose);
                            sqlTree.setCurrentContainer(beginBlock->parent);
                        } else {
                            sqlTree.saveNewElementWithError(ENAME_OTHERKEYWORD, token.value);
                        }
                    }
                } else if (startsWith(sts, "GO ")) {
                    sqlTree.escapeAnySingleOrPartialStatementContainers();
                    if ((tokenID == 0 || isLineBreakingWhiteSpaceOrComment(tokenList[tokenID - 1]))
                        && isFollowedByLineBreakingWhiteSpaceOrSingleLineCommentOrEnd(tokenList, tokenID)) {
                        if (sqlTree.findValidBatchEnd()) {
                            Node* sqlRoot = sqlTree.root();
                            Node* batchSeparator = sqlTree.saveNewElement(ENAME_BATCH_SEPARATOR, "", sqlRoot);
                            sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, batchSeparator);
                            sqlTree.startNewStatement(sqlRoot);
                        } else {
                            sqlTree.saveNewElementWithError(ENAME_OTHERKEYWORD, token.value);
                        }
                    } else {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "EXECUTE AS ")) {
                    bool executeAsInWithOptions = false;
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_DDL_WITH_CLAUSE)
                        && (isLatestTokenAComma(sqlTree) || !sqlTree.hasNonWhiteSpaceNonCommentContent(sqlTree.currentContainer())))
                        executeAsInWithOptions = true;
                    if (!executeAsInWithOptions) {
                        sqlTree.considerStartingNewStatement();
                        sqlTree.considerStartingNewClause();
                    }
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "EXEC ") || startsWith(sts, "EXECUTE ")) {
                    bool execShouldntTryToStartNewStatement = false;
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                        && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                        && (contentStartsWithKeyword(sqlTree.currentContainer(), "INSERT")
                            || contentStartsWithKeyword(sqlTree.currentContainer(), "INSERT INTO"))) {
                        int existingClauseCount = sqlTree.currentContainer()->parent != nullptr
                            ? (int)childrenByName(sqlTree.currentContainer()->parent, ENAME_SQL_CLAUSE).size() : 0;
                        if (existingClauseCount == 1) execShouldntTryToStartNewStatement = true;
                    }
                    if (!execShouldntTryToStartNewStatement) sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (std::regex_search(sts, joinDetector())) {
                    sqlTree.considerStartingNewClause();
                    std::smatch jm; std::regex_search(sts, jm, joinDetector());
                    std::string joinText = jm[0].str();
                    int targetKeywordCount = wordCountNonEmpty(joinText);
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, targetKeywordCount);
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_SELECTIONTARGET, ""));
                } else if (startsWith(sts, "UNION ALL ")) {
                    sqlTree.considerStartingNewClause();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.saveNewElement(ENAME_SET_OPERATOR_CLAUSE, ""), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "UNION ") || startsWith(sts, "INTERSECT ") || startsWith(sts, "EXCEPT ")) {
                    sqlTree.considerStartingNewClause();
                    Node* unionClause = sqlTree.saveNewElement(ENAME_SET_OPERATOR_CLAUSE, "");
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, unionClause);
                } else if (startsWith(sts, "WHILE ")) {
                    sqlTree.considerStartingNewStatement();
                    Node* newWhileLoop = sqlTree.saveNewElement(ENAME_WHILE_LOOP, "");
                    Node* whileContainerOpen = sqlTree.saveNewElement(ENAME_CONTAINER_OPEN, "", newWhileLoop);
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, whileContainerOpen);
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_BOOLEAN_EXPRESSION, "", newWhileLoop));
                } else if (startsWith(sts, "IF ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.startNewContainer(ENAME_IF_STATEMENT, token.value, ENAME_BOOLEAN_EXPRESSION);
                } else if (startsWith(sts, "ELSE ")) {
                    sqlTree.escapeAnyBetweenConditions();
                    sqlTree.escapeAnySelectionTarget();
                    sqlTree.escapeJoinCondition();
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_CASE_THEN)) {
                        sqlTree.moveToAncestorContainer(3, ENAME_CASE_STATEMENT);
                        sqlTree.startNewContainer(ENAME_CASE_ELSE, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.escapePartialStatementContainers();
                        if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                            && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                            && sqlTree.pathNameMatches(2, ENAME_CONTAINER_SINGLESTATEMENT)) {
                            Node* currentNode = sqlTree.currentContainer()->parent->parent;
                            bool stopSearching = false;
                            while (!stopSearching) {
                                if (sqlTree.pathNameMatches(currentNode, 1, ENAME_IF_STATEMENT)) {
                                    sqlTree.setCurrentContainer(currentNode->parent);
                                    sqlTree.startNewContainer(ENAME_ELSE_CLAUSE, token.value, ENAME_CONTAINER_SINGLESTATEMENT);
                                    sqlTree.startNewStatement();
                                    stopSearching = true;
                                } else if (sqlTree.pathNameMatches(currentNode, 1, ENAME_ELSE_CLAUSE)) {
                                    currentNode = currentNode->parent->parent->parent->parent->parent;
                                } else if (sqlTree.pathNameMatches(currentNode, 1, ENAME_WHILE_LOOP)) {
                                    currentNode = currentNode->parent->parent->parent->parent;
                                } else {
                                    sqlTree.saveNewElementWithError(ENAME_OTHERKEYWORD, token.value);
                                    stopSearching = true;
                                }
                            }
                        } else {
                            sqlTree.saveNewElementWithError(ENAME_OTHERKEYWORD, token.value);
                        }
                    }
                } else if (startsWith(sts, "INSERT INTO ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "NATIONAL CHARACTER VARYING ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 3);
                } else if (startsWith(sts, "NATIONAL CHAR VARYING ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 3);
                } else if (startsWith(sts, "BINARY VARYING ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "CHAR VARYING ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "CHARACTER VARYING ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "DOUBLE PRECISION ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "NATIONAL CHARACTER ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "NATIONAL CHAR ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "NATIONAL TEXT ")) {
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "INSERT ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "BULK INSERT ")) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    processCompoundKeyword(tokenList, sqlTree, sqlTree.currentContainer(), tokenID, significantTokenPositions, 2);
                } else if (startsWith(sts, "SELECT ")) {
                    if (sqlTree.newStatementDue) sqlTree.considerStartingNewStatement();
                    bool selectShouldntTryToStartNewStatement = false;
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)) {
                        bool isPrecededByInsertStatement = false;
                        for (Node* clause : childrenByName(sqlTree.currentContainer()->parent, ENAME_SQL_CLAUSE))
                            if (contentStartsWithKeyword(clause, "INSERT")) isPrecededByInsertStatement = true;
                        if (isPrecededByInsertStatement) {
                            bool existingSelectClauseFound = false, existingValuesClauseFound = false, existingExecClauseFound = false;
                            for (Node* clause : childrenByName(sqlTree.currentContainer()->parent, ENAME_SQL_CLAUSE))
                                if (contentStartsWithKeyword(clause, "SELECT")) existingSelectClauseFound = true;
                            for (Node* clause : childrenByName(sqlTree.currentContainer()->parent, ENAME_SQL_CLAUSE))
                                if (contentStartsWithKeyword(clause, "VALUES")) existingValuesClauseFound = true;
                            for (Node* clause : childrenByName(sqlTree.currentContainer()->parent, ENAME_SQL_CLAUSE))
                                if (contentStartsWithKeyword(clause, "EXEC") || contentStartsWithKeyword(clause, "EXECUTE")) existingExecClauseFound = true;
                            if (!existingSelectClauseFound && !existingValuesClauseFound && !existingExecClauseFound)
                                selectShouldntTryToStartNewStatement = true;
                        }
                        Node* firstEntryOfThisClause = sqlTree.getFirstNonWhitespaceNonCommentChildElement(sqlTree.currentContainer());
                        if (firstEntryOfThisClause != nullptr && firstEntryOfThisClause->name == ENAME_SET_OPERATOR_CLAUSE)
                            selectShouldntTryToStartNewStatement = true;
                    }
                    if (!selectShouldntTryToStartNewStatement) sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "UPDATE ")) {
                    if (sqlTree.newStatementDue) sqlTree.considerStartingNewStatement();
                    if (!(sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                          && sqlTree.pathNameMatches(1, ENAME_CURSOR_FOR_OPTIONS))) {
                        sqlTree.considerStartingNewStatement();
                        sqlTree.considerStartingNewClause();
                    }
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "TO ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_PERMISSIONS_TARGET)) {
                        sqlTree.moveToAncestorContainer(2, ENAME_PERMISSIONS_BLOCK);
                        sqlTree.startNewContainer(ENAME_PERMISSIONS_RECIPIENT, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if (startsWith(sts, "FROM ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                        && sqlTree.pathNameMatches(1, ENAME_PERMISSIONS_TARGET)) {
                        sqlTree.moveToAncestorContainer(2, ENAME_PERMISSIONS_BLOCK);
                        sqlTree.startNewContainer(ENAME_PERMISSIONS_RECIPIENT, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else {
                        sqlTree.considerStartingNewClause();
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                        sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_SELECTIONTARGET, ""));
                    }
                } else if (startsWith(sts, "CASCADE ")
                           && sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                           && sqlTree.pathNameMatches(1, ENAME_PERMISSIONS_RECIPIENT)) {
                    sqlTree.moveToAncestorContainer(2, ENAME_PERMISSIONS_BLOCK);
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CONTAINER_GENERALCONTENT, "", sqlTree.saveNewElement(ENAME_DDL_WITH_CLAUSE, "")));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "SET ")) {
                    Node* firstNonCommentSibling2 = sqlTree.getFirstNonWhitespaceNonCommentChildElement(sqlTree.currentContainer());
                    if (!(firstNonCommentSibling2 != nullptr
                          && firstNonCommentSibling2->name == ENAME_OTHERKEYWORD
                          && startsWith(toUpperAscii(firstNonCommentSibling2->textValue), "UPDATE")))
                        sqlTree.considerStartingNewStatement();
                    sqlTree.considerStartingNewClause();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                } else if (startsWith(sts, "BETWEEN ")) {
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_BETWEEN_CONDITION, ""));
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_CONTAINER_OPEN, ""));
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_BETWEEN_LOWERBOUND, ""));
                } else if (startsWith(sts, "AND ")) {
                    if (sqlTree.pathNameMatches(0, ENAME_BETWEEN_LOWERBOUND)) {
                        sqlTree.moveToAncestorContainer(1, ENAME_BETWEEN_CONDITION);
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_CONTAINER_CLOSE, ""));
                        sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_BETWEEN_UPPERBOUND, ""));
                    } else {
                        sqlTree.escapeAnyBetweenConditions();
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_AND_OPERATOR, ""));
                    }
                } else if (startsWith(sts, "OR ")) {
                    sqlTree.escapeAnyBetweenConditions();
                    sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_OR_OPERATOR, ""));
                } else if (startsWith(sts, "WITH ")) {
                    if (sqlTree.newStatementDue) sqlTree.considerStartingNewStatement();
                    if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                        && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                        && !sqlTree.hasNonWhiteSpaceNonCommentContent(sqlTree.currentContainer())) {
                        sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CTE_WITH_CLAUSE, ""));
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value, sqlTree.saveNewElement(ENAME_CONTAINER_OPEN, ""));
                        sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CTE_ALIAS, ""));
                    } else if (sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                               && sqlTree.pathNameMatches(1, ENAME_PERMISSIONS_RECIPIENT)) {
                        sqlTree.moveToAncestorContainer(2, ENAME_PERMISSIONS_BLOCK);
                        sqlTree.startNewContainer(ENAME_DDL_WITH_CLAUSE, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if (sqlTree.pathNameMatches(0, ENAME_DDL_PROCEDURAL_BLOCK)
                               || sqlTree.pathNameMatches(0, ENAME_DDL_OTHER_BLOCK)) {
                        sqlTree.startNewContainer(ENAME_DDL_WITH_CLAUSE, token.value, ENAME_CONTAINER_GENERALCONTENT);
                    } else if (sqlTree.pathNameMatches(0, ENAME_SELECTIONTARGET)) {
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    } else {
                        sqlTree.considerStartingNewClause();
                        sqlTree.saveNewElement(ENAME_OTHERKEYWORD, token.value);
                    }
                } else if ((int)tokenList.size() > tokenID + 1
                           && tokenList[tokenID + 1].type == SqlTokenType::Colon
                           && !((int)tokenList.size() > tokenID + 2 && tokenList[tokenID + 2].type == SqlTokenType::Colon)) {
                    sqlTree.considerStartingNewStatement();
                    sqlTree.saveNewElement(ENAME_LABEL, token.value + tokenList[tokenID + 1].value);
                    tokenID++;
                } else {
                    if (isStatementStarter(token) || sqlTree.newStatementDue) sqlTree.considerStartingNewStatement();
                    if (isClauseStarter(token)) sqlTree.considerStartingNewClause();
                    std::string newNodeName = ENAME_OTHERNODE;
                    auto it = keywordList().find(toUpperAscii(token.value));
                    if (it != keywordList().end()) {
                        switch (it->second) {
                            case KeywordType::OperatorKeyword: newNodeName = ENAME_ALPHAOPERATOR; break;
                            case KeywordType::FunctionKeyword: newNodeName = ENAME_FUNCTION_KEYWORD; break;
                            case KeywordType::DataTypeKeyword: newNodeName = ENAME_DATATYPE_KEYWORD; break;
                            case KeywordType::OtherKeyword:
                                sqlTree.escapeAnySelectionTarget();
                                newNodeName = ENAME_OTHERKEYWORD;
                                break;
                        }
                    }
                    sqlTree.saveNewElement(newNodeName, token.value);
                }
                break;
            }

            case SqlTokenType::Semicolon:
                sqlTree.saveNewElement(ENAME_SEMICOLON, token.value);
                sqlTree.newStatementDue = true;
                break;

            case SqlTokenType::Colon:
                if ((int)tokenList.size() > tokenID + 1 && tokenList[tokenID + 1].type == SqlTokenType::Colon) {
                    sqlTree.saveNewElement(ENAME_SCOPERESOLUTIONOPERATOR, token.value + tokenList[tokenID + 1].value);
                    tokenID++;
                } else if ((int)tokenList.size() > tokenID + 1 && tokenList[tokenID + 1].type == SqlTokenType::OtherNode) {
                    sqlTree.saveNewElement(ENAME_OTHERNODE, token.value + tokenList[tokenID + 1].value);
                    tokenID++;
                } else {
                    sqlTree.saveNewElementWithError(ENAME_OTHEROPERATOR, token.value);
                }
                break;

            case SqlTokenType::Comma: {
                bool isCTESplitter = sqlTree.pathNameMatches(0, ENAME_CONTAINER_GENERALCONTENT)
                                     && sqlTree.pathNameMatches(1, ENAME_CTE_WITH_CLAUSE);
                sqlTree.saveNewElement(getEquivalentSqlNodeName(token.type), token.value);
                if (isCTESplitter) {
                    sqlTree.moveToAncestorContainer(1, ENAME_CTE_WITH_CLAUSE);
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CTE_ALIAS, ""));
                }
                break;
            }

            case SqlTokenType::EqualsSign:
                sqlTree.saveNewElement(ENAME_EQUALSSIGN, token.value);
                if (sqlTree.pathNameMatches(0, ENAME_DDL_DECLARE_BLOCK))
                    sqlTree.setCurrentContainer(sqlTree.saveNewElement(ENAME_CONTAINER_GENERALCONTENT, ""));
                break;

            case SqlTokenType::MultiLineComment:
            case SqlTokenType::SingleLineComment:
            case SqlTokenType::SingleLineCommentCStyle:
            case SqlTokenType::WhiteSpace:
                if (sqlTree.pathNameMatches(0, ENAME_SQL_CLAUSE)
                    && sqlTree.pathNameMatches(1, ENAME_SQL_STATEMENT)
                    && sqlTree.currentContainer()->children.empty())
                    sqlTree.saveNewElementAsPriorSibling(getEquivalentSqlNodeName(token.type), token.value, sqlTree.currentContainer());
                else
                    sqlTree.saveNewElement(getEquivalentSqlNodeName(token.type), token.value);
                break;

            case SqlTokenType::BracketQuotedName:
            case SqlTokenType::Asterisk:
            case SqlTokenType::Period:
            case SqlTokenType::OtherOperator:
            case SqlTokenType::NationalString:
            case SqlTokenType::String:
            case SqlTokenType::QuotedString:
            case SqlTokenType::Number:
            case SqlTokenType::BinaryValue:
            case SqlTokenType::MonetaryValue:
            case SqlTokenType::PseudoName:
                sqlTree.saveNewElement(getEquivalentSqlNodeName(token.type), token.value);
                break;

            default:
                throw std::runtime_error("Unrecognized element encountered!");
        }

        tokenID++;
    }

    if (tokenList.hasUnfinishedToken)
        sqlTree.setError();
    if (!sqlTree.findValidBatchEnd())
        sqlTree.setError();

    return sqlTree.rootRef();
}

} // namespace pmsf
