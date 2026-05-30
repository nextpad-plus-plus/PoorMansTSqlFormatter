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

// ── main loop lives in ParserMain.cpp ───────────────────────────────────────
// (declared there) — keep this translation unit focused on the support layer.

} // namespace pmsf
