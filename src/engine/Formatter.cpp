/*
 * Formatter.cpp — faithful port of Formatters/TSqlStandardFormatter.cs
 * (with TSqlIdentityFormatter / TSqlObfuscatingFormatter for special regions).
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 *
 * NOTE: HTML coloring is not supported (the macOS plugin exposes no HTML option,
 * matching the Windows "Format" command which always runs HTMLColoring=false).
 * All output is plain text; htmlClassName arguments are therefore dropped.
 */
#include "Formatter.h"
#include "KeywordRemapping.h"
#include "SqlConstants.h"
#include "StrUtil.h"
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace pmsf {

using namespace SC;

// Environment.NewLine on the Windows reference build (corpus was generated there).
static const std::string NL = "\r\n";

// ── shared base output buffer (BaseFormatterState, non-HTML) ────────────────
namespace {

bool startsWithBreakRe(const std::string& s) {
    // ^\s*(\r|\n) — the leading whitespace run contains a CR/LF.
    for (char c : s) {
        if (c == '\r' || c == '\n') return true;
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') continue;
        return false;
    }
    return false;
}

// ── TSqlStandardFormattingState ─────────────────────────────────────────────
enum class SpecialRegion { None, NoFormat, Minify };

class FmtState {
public:
    FmtState(const std::string& indentString, int spacesPerTab, int maxLineWidth, int initialIndentLevel)
        : indentLevel(initialIndentLevel), indentString_(indentString), maxLineWidth_(maxLineWidth) {
        int tabCount = 0;
        for (char c : indentString) if (c == '\t') ++tabCount;
        int tabExtra = tabCount * (spacesPerTab - 1);
        indentLength_ = (int)indentString.size() + tabExtra;
    }

    // "isolated state, inheriting existing conditions" constructor (for parens).
    FmtState makeInner() const {
        FmtState s(indentString_, /*spacesPerTab fold into len*/ 0, maxLineWidth_, indentLevel);
        s.indentLength_ = indentLength_;
        s.currentLineLength = indentLevel * indentLength_;
        s.currentLineHasContent = currentLineHasContent;
        return s;
    }

    // base append (htmlOutput == false): just appends, no wrap/track. Used by Indent().
    void baseAppend(const std::string& content) { out_ += content; }

    // C# two-arg AddOutputContent(content, class): wrap + append(if !special) + track.
    void addContent(const std::string& content) {
        if (currentLineHasContent && ((int)content.size() + currentLineLength > maxLineWidth_))
            whiteSpaceBreakToNextLine();
        if (specialRegion == SpecialRegion::None)
            out_ += content;
        currentLineHasContent = true;
        currentLineLength += (int)content.size();
    }

    // C# one-arg AddOutputContent(content): no-op inside special regions.
    void addContentGuarded(const std::string& content) {
        if (specialRegion == SpecialRegion::None)
            addContent(content);
    }

    void addContentRaw(const std::string& content) { out_ += content; }

    void addOutputLineBreak() {
        if (specialRegion == SpecialRegion::None)
            out_ += NL;
        currentLineLength = 0;
        currentLineHasContent = false;
    }

    void addOutputSpace() {
        if (specialRegion == SpecialRegion::None)
            out_ += " ";
    }

    void indent(int level) {
        for (int i = 0; i < level; ++i) {
            if (specialRegion == SpecialRegion::None)
                baseAppend(indentString_);
            currentLineLength += indentLength_;
        }
    }

    void whiteSpaceBreakToNextLine() {
        addOutputLineBreak();
        indent(indentLevel);
        breakExpected = false;
        sourceBreakPending = false;
        wordSeparatorExpected = false;
    }

    FmtState& incrementIndent() { ++indentLevel; return *this; }
    FmtState& decrementIndent() { --indentLevel; return *this; }

    bool startsWithBreak() const { return startsWithBreakRe(out_); }
    bool outputContainsLineBreak() const { return out_.find_first_of("\r\n") != std::string::npos; }

    void assimilate(FmtState& partial) {
        currentLineLength = currentLineLength + partial.currentLineLength;
        currentLineHasContent = currentLineHasContent || partial.currentLineHasContent;
        if (specialRegion == SpecialRegion::None)
            out_ += partial.dumpOutput();
    }

    void setRecentKeyword(const std::string& name) {
        if (recentKeywords_.find(indentLevel) == recentKeywords_.end())
            recentKeywords_[indentLevel] = toUpperAscii(name);
    }
    std::optional<std::string> getRecentKeyword() const {
        std::optional<std::string> found;
        std::optional<int> foundAt;
        for (const auto& kv : recentKeywords_) {
            int key = kv.first;
            if ((!foundAt.has_value() || foundAt.value() > key) && key >= indentLevel) {
                foundAt = key;
                found = kv.second;
            }
        }
        return found;
    }
    void resetKeywords() {
        for (auto it = recentKeywords_.begin(); it != recentKeywords_.end();) {
            if (it->first >= indentLevel) it = recentKeywords_.erase(it);
            else ++it;
        }
    }

    std::string dumpOutput() const { return out_; }

    // state flags (public, like C# properties)
    bool statementBreakExpected = false;
    bool breakExpected = false;
    bool wordSeparatorExpected = false;
    bool sourceBreakPending = false;
    int  additionalBreaksExpected = 0;
    bool unIndentInitialBreak = false;
    int  indentLevel = 0;
    int  currentLineLength = 0;
    bool currentLineHasContent = false;
    SpecialRegion specialRegion = SpecialRegion::None;
    Node* regionStartNode = nullptr;

private:
    std::string out_;
    std::string indentString_;
    int indentLength_ = 0;
    int maxLineWidth_ = 999;
    std::unordered_map<int, std::string> recentKeywords_;
};

} // anonymous namespace

// ── TSqlIdentityFormatter ───────────────────────────────────────────────────
namespace {

void identityProcessNode(std::string& out, Node* e);

void identityProcessList(std::string& out, const std::vector<Node*>& list) {
    for (Node* e : list) identityProcessNode(out, e);
}

void identityProcessNode(std::string& out, Node* e) {
    const std::string& n = e->name;
    if (n == ENAME_DDLDETAIL_PARENS || n == ENAME_DDL_PARENS || n == ENAME_FUNCTION_PARENS
        || n == ENAME_IN_PARENS || n == ENAME_EXPRESSION_PARENS || n == ENAME_SELECTIONTARGET_PARENS) {
        out += "(";
        std::vector<Node*> kids; for (auto& c : e->children) kids.push_back(c.get());
        identityProcessList(out, kids);
        out += ")";
    } else if (n == ENAME_COMMENT_MULTILINE) {
        out += "/*" + e->textValue + "*/";
    } else if (n == ENAME_COMMENT_SINGLELINE) {
        out += "--" + e->textValue;
    } else if (n == ENAME_COMMENT_SINGLELINE_CSTYLE) {
        out += "//" + e->textValue;
    } else if (n == ENAME_STRING) {
        out += "'" + replaceAll(e->textValue, "'", "''") + "'";
    } else if (n == ENAME_NSTRING) {
        out += "N'" + replaceAll(e->textValue, "'", "''") + "'";
    } else if (n == ENAME_QUOTED_STRING) {
        out += "\"" + replaceAll(e->textValue, "\"", "\"\"") + "\"";
    } else if (n == ENAME_BRACKET_QUOTED_NAME) {
        out += "[" + replaceAll(e->textValue, "]", "]]") + "]";
    } else if (n == ENAME_COMMA || n == ENAME_PERIOD || n == ENAME_SEMICOLON || n == ENAME_ASTERISK
               || n == ENAME_EQUALSSIGN || n == ENAME_SCOPERESOLUTIONOPERATOR || n == ENAME_ALPHAOPERATOR
               || n == ENAME_OTHEROPERATOR) {
        out += e->textValue;
    } else if (n == ENAME_AND_OPERATOR || n == ENAME_OR_OPERATOR) {
        Node* kw = childByName(e, ENAME_OTHERKEYWORD);
        if (kw) out += kw->textValue;
    } else if (n == ENAME_FUNCTION_KEYWORD || n == ENAME_OTHERKEYWORD || n == ENAME_DATATYPE_KEYWORD
               || n == ENAME_PSEUDONAME) {
        out += e->textValue;
    } else if (n == ENAME_OTHERNODE || n == ENAME_WHITESPACE || n == ENAME_NUMBER_VALUE
               || n == ENAME_MONETARY_VALUE || n == ENAME_BINARY_VALUE || n == ENAME_LABEL) {
        out += e->textValue;
    } else {
        // all container types: recurse children
        std::vector<Node*> kids; for (auto& c : e->children) kids.push_back(c.get());
        identityProcessList(out, kids);
    }
}

} // anonymous namespace

std::string TSqlIdentityFormatter::formatSQLTree(Node* sqlTreeDoc) {
    std::string out;
    if (sqlTreeDoc->name == ENAME_SQL_ROOT && sqlTreeDoc->getAttributeValue(ANAME_ERRORFOUND) == "1")
        out += ERROR_FOUND_WARNING;
    identityProcessNode(out, sqlTreeDoc);
    return out;
}

// ── TSqlObfuscatingFormatter (minify: no randomization, comments dropped) ────
namespace {

struct ObfState {
    // RandomizeLineLength is false in the minify-region usage, so the per-line
    // limit stays at MAX_LINE_LENGTH (80) for the whole run.
    static const int MAX_LINE_LENGTH = 80;

    std::string out;
    bool breakExpected = false;
    bool spaceExpectedForAnsiString = false;
    bool spaceExpectedForE = false;
    bool spaceExpectedForX = false;
    bool spaceExpectedForPlusMinus = false;
    bool spaceExpected = false;
    int  currentLineLength = 0;
    int  thisLineLimit = MAX_LINE_LENGTH;

    void clearSpace() {
        spaceExpected = spaceExpectedForAnsiString = spaceExpectedForE = false;
        spaceExpectedForX = spaceExpectedForPlusMinus = false;
    }
    void breakIfExpected() {
        if (breakExpected) {
            breakExpected = false;
            out += NL;
            clearSpace();
            currentLineLength = 0;
        }
    }
    void spaceIfExpectedForAnsiString() {
        if (spaceExpectedForAnsiString) { out += " "; clearSpace(); }
    }
    void spaceIfExpected() {
        if (spaceExpected) { out += " "; clearSpace(); }
    }
    void addContent(const std::string& content) {
        breakIfExpected();
        spaceIfExpected();
        if (currentLineLength > 0 && currentLineLength + (int)content.size() > thisLineLimit) {
            breakExpected = true;
            breakIfExpected();
        } else if (!content.empty() &&
            ((spaceExpectedForE && (content[0] == 'e' || content[0] == 'E'))
             || (spaceExpectedForX && (content[0] == 'x' || content[0] == 'X'))
             || (spaceExpectedForPlusMinus && content[0] == '+')
             || (spaceExpectedForPlusMinus && content[0] == '-'))) {
            spaceExpected = true;
            spaceIfExpected();
        }
        currentLineLength += (int)content.size();
        out += content;
        clearSpace();
    }
};

void obfProcessNode(ObfState& st, Node* e);

void obfProcessList(ObfState& st, const std::vector<Node*>& list) {
    for (Node* e : list) obfProcessNode(st, e);
}
std::vector<Node*> kidsOf(Node* e) {
    std::vector<Node*> k; for (auto& c : e->children) k.push_back(c.get()); return k;
}

void obfProcessNode(ObfState& st, Node* e) {
    const std::string& n = e->name;
    if (n == ENAME_DDLDETAIL_PARENS || n == ENAME_FUNCTION_PARENS || n == ENAME_IN_PARENS
        || n == ENAME_DDL_PARENS || n == ENAME_EXPRESSION_PARENS || n == ENAME_SELECTIONTARGET_PARENS) {
        st.spaceExpected = false;
        st.addContent("(");
        obfProcessList(st, kidsOf(e));
        st.spaceExpected = false;
        st.spaceExpectedForAnsiString = false;
        st.addContent(")");
    } else if (n == ENAME_WHITESPACE) {
        // do nothing
    } else if (n == ENAME_COMMENT_MULTILINE || n == ENAME_COMMENT_SINGLELINE || n == ENAME_COMMENT_SINGLELINE_CSTYLE) {
        // PreserveComments == false in the minify-region usage → drop
    } else if (n == ENAME_BATCH_SEPARATOR) {
        st.breakExpected = true;
        obfProcessList(st, kidsOf(e));
        st.breakExpected = true;
    } else if (n == ENAME_STRING) {
        st.spaceIfExpectedForAnsiString();
        st.spaceExpected = false;
        st.addContent("'" + replaceAll(e->textValue, "'", "''") + "'");
        st.spaceExpectedForAnsiString = true;
    } else if (n == ENAME_NSTRING) {
        st.addContent("N'" + replaceAll(e->textValue, "'", "''") + "'");
        st.spaceExpectedForAnsiString = true;
    } else if (n == ENAME_BRACKET_QUOTED_NAME) {
        st.spaceExpected = false;
        st.addContent("[" + replaceAll(e->textValue, "]", "]]") + "]");
    } else if (n == ENAME_QUOTED_STRING) {
        st.spaceExpected = false;
        st.addContent("\"" + replaceAll(e->textValue, "\"", "\"\"") + "\"");
    } else if (n == ENAME_COMMA || n == ENAME_PERIOD || n == ENAME_SEMICOLON || n == ENAME_SCOPERESOLUTIONOPERATOR
               || n == ENAME_ASTERISK || n == ENAME_EQUALSSIGN || n == ENAME_OTHEROPERATOR) {
        st.spaceExpected = false;
        st.addContent(e->textValue);
    } else if (n == ENAME_COMPOUNDKEYWORD) {
        st.addContent(e->getAttributeValue(ANAME_SIMPLETEXT));   // KeywordMapping empty
        st.spaceExpected = true;
    } else if (n == ENAME_LABEL) {
        st.addContent(e->textValue);
        st.breakExpected = true;
    } else if (n == ENAME_OTHERKEYWORD || n == ENAME_ALPHAOPERATOR || n == ENAME_DATATYPE_KEYWORD
               || n == ENAME_PSEUDONAME || n == ENAME_BINARY_VALUE) {
        st.addContent(e->textValue);
        st.spaceExpected = true;
    } else if (n == ENAME_NUMBER_VALUE) {
        st.addContent(e->textValue);
        std::string lower = toLowerAscii(e->textValue);
        if (lower.find('e') == std::string::npos) {
            st.spaceExpectedForE = true;
            if (e->textValue == "0") st.spaceExpectedForX = true;
        }
    } else if (n == ENAME_MONETARY_VALUE) {
        if (e->textValue.empty() || e->textValue[0] != '$')
            st.spaceExpected = false;
        st.addContent(e->textValue);
        if (e->textValue.size() == 1) st.spaceExpectedForPlusMinus = true;
    } else if (n == ENAME_OTHERNODE || n == ENAME_FUNCTION_KEYWORD) {
        st.addContent(e->textValue);
        st.spaceExpected = true;
    } else {
        // all container types: recurse children, no obfuscated-output impact
        obfProcessList(st, kidsOf(e));
    }
}

} // anonymous namespace

std::string TSqlObfuscatingFormatter::formatSQLTree(Node* sqlTreeDoc) {
    ObfState st;
    if (sqlTreeDoc->name == ENAME_SQL_ROOT && sqlTreeDoc->getAttributeValue(ANAME_ERRORFOUND) == "1")
        st.out += ERROR_FOUND_WARNING;
    obfProcessNode(st, sqlTreeDoc);
    st.breakIfExpected();
    return st.out;
}

// ── TSqlStandardFormatter ───────────────────────────────────────────────────
TSqlStandardFormatter::TSqlStandardFormatter(const TSqlStandardFormatterOptions& options)
    : options_(options) {
    if (options_.keywordStandardization)
        keywordMapping_ = standardKeywordRemapping();
    errorOutputPrefix_ = std::string("--WARNING! ERRORS ENCOUNTERED DURING SQL PARSING!") + NL;
}

namespace {

// vector<NodeRef> children → vector<Node*>
std::vector<Node*> childPtrs(Node* e) {
    std::vector<Node*> k; for (auto& c : e->children) k.push_back(c.get()); return k;
}

struct StdFormatter {
    const TSqlStandardFormatterOptions& opt;
    const std::unordered_map<std::string, std::string>& kwMap;

    std::string formatKeyword(const std::string& keyword) const {
        std::string outputKeyword = keyword;
        auto it = kwMap.find(toUpperAscii(keyword));
        if (it != kwMap.end()) outputKeyword = it->second;
        return opt.uppercaseKeywords ? toUpperAscii(outputKeyword) : toLowerAscii(outputKeyword);
    }
    std::string formatOperator(const std::string& op) const {
        return opt.uppercaseKeywords ? toUpperAscii(op) : toLowerAscii(op);
    }

    Node* firstSemanticElementChild(Node* contentElement) const {
        Node* target = nullptr;
        while (contentElement != nullptr) {
            target = childExcludingNames(contentElement, ENAMELIST_NONCONTENT);
            if (target != nullptr && nameIn(target->name, ENAMELIST_NONSEMANTICCONTENT))
                contentElement = target;
            else
                contentElement = nullptr;
        }
        return target;
    }

    void whiteSpaceBreakAsExpected(FmtState& s) {
        if (s.breakExpected) s.whiteSpaceBreakToNextLine();
        while (s.additionalBreaksExpected > 0) {
            s.whiteSpaceBreakToNextLine();
            s.additionalBreaksExpected--;
        }
    }
    void whiteSpaceSeparateWords(FmtState& s) {
        if (s.breakExpected || s.additionalBreaksExpected > 0) {
            bool wasUnIndent = s.unIndentInitialBreak;
            if (wasUnIndent) s.decrementIndent();
            whiteSpaceBreakAsExpected(s);
            if (wasUnIndent) s.incrementIndent();
        } else if (s.wordSeparatorExpected) {
            s.addOutputSpace();
        }
        s.unIndentInitialBreak = false;
        s.sourceBreakPending = false;
        s.wordSeparatorExpected = false;
    }
    void whiteSpaceSeparateComment(Node* contentElement, FmtState& s) {
        (void)contentElement;
        if (s.currentLineHasContent && s.sourceBreakPending) {
            s.breakExpected = true;
            whiteSpaceBreakAsExpected(s);
        } else if (s.wordSeparatorExpected) {
            s.addOutputSpace();
        }
        s.sourceBreakPending = false;
        s.wordSeparatorExpected = false;
    }
    void whiteSpaceSeparateStatements(Node* contentElement, FmtState& s) {
        if (!s.statementBreakExpected) return;
        Node* thisClauseStarter = firstSemanticElementChild(contentElement);
        auto recent = s.getRecentKeyword();
        bool exempt =
            thisClauseStarter != nullptr
            && thisClauseStarter->name == ENAME_OTHERKEYWORD
            && recent.has_value()
            && ((toUpperAscii(thisClauseStarter->textValue) == "SET" && recent.value() == "SET")
                || (toUpperAscii(thisClauseStarter->textValue) == "DECLARE" && recent.value() == "DECLARE")
                || (toUpperAscii(thisClauseStarter->textValue) == "PRINT" && recent.value() == "PRINT"));
        if (!exempt) {
            for (int i = opt.newStatementLineBreaks; i > 0; i--) s.addOutputLineBreak();
        } else {
            for (int i = opt.newClauseLineBreaks; i > 0; i--) s.addOutputLineBreak();
        }
        s.indent(s.indentLevel);
        s.breakExpected = false;
        s.additionalBreaksExpected = 0;
        s.sourceBreakPending = false;
        s.statementBreakExpected = false;
        s.wordSeparatorExpected = false;
    }

    void processList(const std::vector<Node*>& list, FmtState& s) {
        for (Node* e : list) processNode(e, s);
    }

    // Handle a [/NOFORMAT] or [/MINIFY] close inside a comment node.
    void handleSpecialRegionClose(Node* contentElement, FmtState& s) {
        std::string up = toUpperAscii(contentElement->textValue);
        if (s.specialRegion == SpecialRegion::NoFormat && contains(up, "[/NOFORMAT]")) {
            NodeRef skipped = extractStructureBetween(s.regionStartNode, contentElement);
            if (skipped) {
                TSqlIdentityFormatter idf;
                s.addContentRaw(idf.formatSQLTree(skipped.get()));
                s.wordSeparatorExpected = false;
                s.breakExpected = false;
            }
            s.specialRegion = SpecialRegion::None;
            s.regionStartNode = nullptr;
        } else if (s.specialRegion == SpecialRegion::Minify && contains(up, "[/MINIFY]")) {
            NodeRef skipped = extractStructureBetween(s.regionStartNode, contentElement);
            if (skipped) {
                TSqlObfuscatingFormatter of;
                s.addContentRaw(of.formatSQLTree(skipped.get()));
                s.wordSeparatorExpected = false;
                s.breakExpected = false;
            }
            s.specialRegion = SpecialRegion::None;
            s.regionStartNode = nullptr;
        }
    }

    void processNode(Node* contentElement, FmtState& s) {
        int initialIndent = s.indentLevel;
        const std::string& name = contentElement->name;

        if (name == ENAME_SQL_STATEMENT) {
            whiteSpaceSeparateStatements(contentElement, s);
            s.resetKeywords();
            processList(childPtrs(contentElement), s);
            s.statementBreakExpected = true;
        } else if (name == ENAME_SQL_CLAUSE) {
            s.unIndentInitialBreak = true;
            processList(childPtrs(contentElement), s.incrementIndent());
            s.decrementIndent();
            if (opt.newClauseLineBreaks > 0) s.breakExpected = true;
            if (opt.newClauseLineBreaks > 1) s.additionalBreaksExpected = opt.newClauseLineBreaks - 1;
        } else if (name == ENAME_SET_OPERATOR_CLAUSE) {
            s.decrementIndent();
            s.whiteSpaceBreakToNextLine();
            s.whiteSpaceBreakToNextLine();
            processList(childPtrs(contentElement), s.incrementIndent());
            s.breakExpected = true;
            s.additionalBreaksExpected = 1;
        } else if (name == ENAME_BATCH_SEPARATOR) {
            s.whiteSpaceBreakToNextLine();
            processList(childPtrs(contentElement), s);
            s.breakExpected = true;
        } else if (name == ENAME_DDL_PROCEDURAL_BLOCK || name == ENAME_DDL_OTHER_BLOCK
                   || name == ENAME_DDL_DECLARE_BLOCK || name == ENAME_CURSOR_DECLARATION
                   || name == ENAME_BEGIN_TRANSACTION || name == ENAME_SAVE_TRANSACTION
                   || name == ENAME_COMMIT_TRANSACTION || name == ENAME_ROLLBACK_TRANSACTION
                   || name == ENAME_CONTAINER_OPEN || name == ENAME_CONTAINER_CLOSE
                   || name == ENAME_WHILE_LOOP || name == ENAME_IF_STATEMENT
                   || name == ENAME_SELECTIONTARGET || name == ENAME_CONTAINER_GENERALCONTENT
                   || name == ENAME_CTE_WITH_CLAUSE || name == ENAME_PERMISSIONS_BLOCK
                   || name == ENAME_PERMISSIONS_DETAIL || name == ENAME_MERGE_CLAUSE
                   || name == ENAME_MERGE_TARGET) {
            processList(childPtrs(contentElement), s);
        } else if (name == ENAME_CASE_INPUT || name == ENAME_BOOLEAN_EXPRESSION
                   || name == ENAME_BETWEEN_LOWERBOUND || name == ENAME_BETWEEN_UPPERBOUND) {
            whiteSpaceSeparateWords(s);
            processList(childPtrs(contentElement), s);
        } else if (name == ENAME_CONTAINER_SINGLESTATEMENT || name == ENAME_CONTAINER_MULTISTATEMENT
                   || name == ENAME_MERGE_ACTION) {
            bool singleStatementIsIf = false;
            for (Node* statement : childrenByName(contentElement, ENAME_SQL_STATEMENT))
                for (Node* clause : childrenByName(statement, ENAME_SQL_CLAUSE))
                    for (Node* ifStatement : childrenByName(clause, ENAME_IF_STATEMENT)) {
                        (void)ifStatement; singleStatementIsIf = true;
                    }
            bool elseParent = contentElement->parent && contentElement->parent->name == ENAME_ELSE_CLAUSE;
            if (singleStatementIsIf && elseParent) s.decrementIndent();
            else s.breakExpected = true;
            processList(childPtrs(contentElement), s);
            if (singleStatementIsIf && elseParent) s.incrementIndent();
            s.statementBreakExpected = false;
            s.unIndentInitialBreak = false;
        } else if (name == ENAME_PERMISSIONS_TARGET || name == ENAME_PERMISSIONS_RECIPIENT
                   || name == ENAME_DDL_WITH_CLAUSE || name == ENAME_MERGE_CONDITION
                   || name == ENAME_MERGE_THEN) {
            s.breakExpected = true;
            s.unIndentInitialBreak = true;
            processList(childPtrs(contentElement), s.incrementIndent());
            s.decrementIndent();
        } else if (name == ENAME_JOIN_ON_SECTION) {
            if (opt.breakJoinOnSections) s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s);
            if (opt.breakJoinOnSections) s.incrementIndent();
            processList(childrenByName(contentElement, ENAME_CONTAINER_GENERALCONTENT), s);
            if (opt.breakJoinOnSections) s.decrementIndent();
        } else if (name == ENAME_CTE_ALIAS) {
            s.unIndentInitialBreak = true;
            processList(childPtrs(contentElement), s);
        } else if (name == ENAME_ELSE_CLAUSE) {
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s.decrementIndent());
            processList(childrenByName(contentElement, ENAME_CONTAINER_SINGLESTATEMENT), s.incrementIndent());
        } else if (name == ENAME_DDL_AS_BLOCK || name == ENAME_CURSOR_FOR_BLOCK) {
            s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s.decrementIndent());
            s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_GENERALCONTENT), s);
            s.incrementIndent();
        } else if (name == ENAME_TRIGGER_CONDITION) {
            s.decrementIndent();
            s.whiteSpaceBreakToNextLine();
            processList(childPtrs(contentElement), s.incrementIndent());
        } else if (name == ENAME_CURSOR_FOR_OPTIONS || name == ENAME_CTE_AS_BLOCK) {
            s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s.decrementIndent());
            processList(childrenByName(contentElement, ENAME_CONTAINER_GENERALCONTENT), s.incrementIndent());
        } else if (name == ENAME_DDL_RETURNS || name == ENAME_MERGE_USING || name == ENAME_MERGE_WHEN) {
            s.breakExpected = true;
            s.unIndentInitialBreak = true;
            processList(childPtrs(contentElement), s);
        } else if (name == ENAME_BETWEEN_CONDITION) {
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s);
            s.incrementIndent();
            processList(childrenByName(contentElement, ENAME_BETWEEN_LOWERBOUND), s.incrementIndent());
            if (opt.expandBetweenConditions) s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_CLOSE), s.decrementIndent());
            processList(childrenByName(contentElement, ENAME_BETWEEN_UPPERBOUND), s.incrementIndent());
            s.decrementIndent();
            s.decrementIndent();
        } else if (name == ENAME_DDLDETAIL_PARENS || name == ENAME_FUNCTION_PARENS) {
            s.wordSeparatorExpected = false;
            whiteSpaceBreakAsExpected(s);
            s.addContent(formatOperator("("));
            processList(childPtrs(contentElement), s.incrementIndent());
            s.decrementIndent();
            whiteSpaceBreakAsExpected(s);
            s.addContent(formatOperator(")"));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_DDL_PARENS || name == ENAME_EXPRESSION_PARENS
                   || name == ENAME_SELECTIONTARGET_PARENS || name == ENAME_IN_PARENS) {
            whiteSpaceSeparateWords(s);
            bool expr = (name == ENAME_EXPRESSION_PARENS || name == ENAME_IN_PARENS);
            if (expr) s.incrementIndent();
            s.addContent(formatOperator("("));
            FmtState innerState = s.makeInner();
            processList(childPtrs(contentElement), innerState);
            if (innerState.breakExpected || innerState.outputContainsLineBreak()) {
                if (!innerState.startsWithBreak()) s.whiteSpaceBreakToNextLine();
                s.assimilate(innerState);
                s.whiteSpaceBreakToNextLine();
            } else {
                s.assimilate(innerState);
            }
            s.addContent(formatOperator(")"));
            if (expr) s.decrementIndent();
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_BEGIN_END_BLOCK || name == ENAME_TRY_BLOCK || name == ENAME_CATCH_BLOCK) {
            bool nestedSingle = contentElement->parent && contentElement->parent->name == ENAME_SQL_CLAUSE
                && contentElement->parent->parent && contentElement->parent->parent->name == ENAME_SQL_STATEMENT
                && contentElement->parent->parent->parent && contentElement->parent->parent->parent->name == ENAME_CONTAINER_SINGLESTATEMENT;
            if (nestedSingle) s.decrementIndent();
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s);
            processList(childrenByName(contentElement, ENAME_CONTAINER_MULTISTATEMENT), s);
            s.decrementIndent();
            s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_CLOSE), s);
            s.incrementIndent();
            if (nestedSingle) s.incrementIndent();
        } else if (name == ENAME_CASE_STATEMENT) {
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s);
            s.incrementIndent();
            processList(childrenByName(contentElement, ENAME_CASE_INPUT), s);
            processList(childrenByName(contentElement, ENAME_CASE_WHEN), s);
            processList(childrenByName(contentElement, ENAME_CASE_ELSE), s);
            if (opt.expandCaseStatements) s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_CLOSE), s);
            s.decrementIndent();
        } else if (name == ENAME_CASE_WHEN || name == ENAME_CASE_THEN || name == ENAME_CASE_ELSE) {
            if (opt.expandCaseStatements) s.breakExpected = true;
            processList(childrenByName(contentElement, ENAME_CONTAINER_OPEN), s);
            processList(childrenByName(contentElement, ENAME_CONTAINER_GENERALCONTENT), s.incrementIndent());
            processList(childrenByName(contentElement, ENAME_CASE_THEN), s);
            s.decrementIndent();
        } else if (name == ENAME_AND_OPERATOR || name == ENAME_OR_OPERATOR) {
            if (opt.expandBooleanExpressions) s.breakExpected = true;
            Node* kw = childByName(contentElement, ENAME_OTHERKEYWORD);
            if (kw) processNode(kw, s);
        } else if (name == ENAME_COMMENT_MULTILINE) {
            handleSpecialRegionClose(contentElement, s);
            whiteSpaceSeparateComment(contentElement, s);
            s.addContent("/*" + contentElement->textValue + "*/");
            Node* ns = nextSibling(contentElement);
            bool atStatementEdge = (contentElement->parent && contentElement->parent->name == ENAME_SQL_STATEMENT)
                || (ns != nullptr && ns->name == ENAME_WHITESPACE && containsLineBreak(ns->textValue));
            if (atStatementEdge) s.breakExpected = true;
            else s.wordSeparatorExpected = true;
            std::string up = toUpperAscii(contentElement->textValue);
            if (s.specialRegion == SpecialRegion::None && contains(up, "[NOFORMAT]")) {
                s.specialRegion = SpecialRegion::NoFormat; s.regionStartNode = contentElement;
            } else if (s.specialRegion == SpecialRegion::None && contains(up, "[MINIFY]")) {
                s.specialRegion = SpecialRegion::Minify; s.regionStartNode = contentElement;
            }
        } else if (name == ENAME_COMMENT_SINGLELINE || name == ENAME_COMMENT_SINGLELINE_CSTYLE) {
            handleSpecialRegionClose(contentElement, s);
            whiteSpaceSeparateComment(contentElement, s);
            std::string body = replaceAll(replaceAll(contentElement->textValue, "\r", ""), "\n", "");
            s.addContent((name == ENAME_COMMENT_SINGLELINE ? "--" : "//") + body);
            s.breakExpected = true;
            s.sourceBreakPending = true;
            std::string up = toUpperAscii(contentElement->textValue);
            if (s.specialRegion == SpecialRegion::None && contains(up, "[NOFORMAT]")) {
                s.addOutputLineBreak();
                s.specialRegion = SpecialRegion::NoFormat; s.regionStartNode = contentElement;
            } else if (s.specialRegion == SpecialRegion::None && contains(up, "[MINIFY]")) {
                s.addOutputLineBreak();
                s.specialRegion = SpecialRegion::Minify; s.regionStartNode = contentElement;
            }
        } else if (name == ENAME_STRING || name == ENAME_NSTRING) {
            whiteSpaceSeparateWords(s);
            std::string outValue = (name == ENAME_NSTRING)
                ? "N'" + replaceAll(contentElement->textValue, "'", "''") + "'"
                : "'" + replaceAll(contentElement->textValue, "'", "''") + "'";
            s.addContent(outValue);
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_BRACKET_QUOTED_NAME) {
            whiteSpaceSeparateWords(s);
            s.addContentGuarded("[" + replaceAll(contentElement->textValue, "]", "]]") + "]");
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_QUOTED_STRING) {
            whiteSpaceSeparateWords(s);
            s.addContentGuarded("\"" + replaceAll(contentElement->textValue, "\"", "\"\"") + "\"");
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_COMMA) {
            bool isExpandedList =
                (opt.expandCommaLists
                 && !(contentElement->parent->name == ENAME_DDLDETAIL_PARENS
                      || contentElement->parent->name == ENAME_FUNCTION_PARENS
                      || contentElement->parent->name == ENAME_IN_PARENS))
                || (opt.expandInLists && contentElement->parent->name == ENAME_IN_PARENS);
            if (opt.trailingCommas) {
                whiteSpaceBreakAsExpected(s);
                s.addContent(formatOperator(","));
                if (isExpandedList) s.breakExpected = true;
                else s.wordSeparatorExpected = true;
            } else {
                if (isExpandedList) {
                    s.whiteSpaceBreakToNextLine();
                    s.addContent(formatOperator(","));
                    if (opt.spaceAfterExpandedComma) s.wordSeparatorExpected = true;
                } else {
                    whiteSpaceBreakAsExpected(s);
                    s.addContent(formatOperator(","));
                    s.wordSeparatorExpected = true;
                }
            }
        } else if (name == ENAME_PERIOD || name == ENAME_SEMICOLON || name == ENAME_SCOPERESOLUTIONOPERATOR) {
            s.wordSeparatorExpected = false;
            whiteSpaceBreakAsExpected(s);
            s.addContent(formatOperator(contentElement->textValue));
        } else if (name == ENAME_ASTERISK || name == ENAME_EQUALSSIGN
                   || name == ENAME_ALPHAOPERATOR || name == ENAME_OTHEROPERATOR) {
            whiteSpaceSeparateWords(s);
            s.addContent(formatOperator(contentElement->textValue));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_COMPOUNDKEYWORD) {
            whiteSpaceSeparateWords(s);
            s.setRecentKeyword(contentElement->getAttributeValue(ANAME_SIMPLETEXT));
            s.addContent(formatKeyword(contentElement->getAttributeValue(ANAME_SIMPLETEXT)));
            s.wordSeparatorExpected = true;
            processList(childrenByNames(contentElement, ENAMELIST_COMMENT), s.incrementIndent());
            s.decrementIndent();
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_OTHERKEYWORD || name == ENAME_DATATYPE_KEYWORD) {
            whiteSpaceSeparateWords(s);
            s.setRecentKeyword(contentElement->textValue);
            s.addContent(formatKeyword(contentElement->textValue));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_PSEUDONAME) {
            whiteSpaceSeparateWords(s);
            s.addContent(formatKeyword(contentElement->textValue));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_FUNCTION_KEYWORD) {
            whiteSpaceSeparateWords(s);
            s.setRecentKeyword(contentElement->textValue);
            s.addContent(contentElement->textValue);
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_OTHERNODE || name == ENAME_MONETARY_VALUE || name == ENAME_LABEL) {
            whiteSpaceSeparateWords(s);
            s.addContentGuarded(contentElement->textValue);
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_NUMBER_VALUE) {
            whiteSpaceSeparateWords(s);
            s.addContentGuarded(toLowerAscii(contentElement->textValue));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_BINARY_VALUE) {
            whiteSpaceSeparateWords(s);
            s.addContentGuarded("0x");
            s.addContentGuarded(toUpperAscii(contentElement->textValue.substr(2)));
            s.wordSeparatorExpected = true;
        } else if (name == ENAME_WHITESPACE) {
            if (containsLineBreak(contentElement->textValue)) s.sourceBreakPending = true;
        } else {
            throw std::runtime_error("Unrecognized element in SQL Xml!");
        }

        if (initialIndent != s.indentLevel)
            throw std::runtime_error("Messed up the indenting!! Check code/stack or panic!");
    }
};

} // anonymous namespace

std::string TSqlStandardFormatter::formatSQLTree(Node* sqlTreeDoc) {
    FmtState state(options_.indentString, options_.spacesPerTab, options_.maxLineWidth, 0);
    StdFormatter f{options_, keywordMapping_};

    if (sqlTreeDoc->name == ENAME_SQL_ROOT && sqlTreeDoc->getAttributeValue(ANAME_ERRORFOUND) == "1")
        state.addContentGuarded(errorOutputPrefix_);

    f.processList(childPtrs(sqlTreeDoc), state);

    f.whiteSpaceBreakAsExpected(state);

    if (state.specialRegion == SpecialRegion::NoFormat) {
        NodeRef skipped = extractStructureBetween(state.regionStartNode, sqlTreeDoc);
        TSqlIdentityFormatter idf;
        state.addContentRaw(idf.formatSQLTree(skipped.get()));
    } else if (state.specialRegion == SpecialRegion::Minify) {
        NodeRef skipped = extractStructureBetween(state.regionStartNode, sqlTreeDoc);
        TSqlObfuscatingFormatter of;
        state.addContentRaw(of.formatSQLTree(skipped.get()));
    }
    return state.dumpOutput();
}

} // namespace pmsf
