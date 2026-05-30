/*
 * Options.h — faithful port of Formatters/TSqlStandardFormatterOptions.cs.
 *
 * The formatting options struct, including the "Key=Value,..." config-string
 * constructor used by the test corpus and persisted by the Settings dialog.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3). C++ port 2026.
 */
#pragma once

#include "StrUtil.h"
#include <string>
#include <vector>
#include <stdexcept>

namespace pmsf {

class TSqlStandardFormatterOptions {
public:
    TSqlStandardFormatterOptions() { setIndentString("\t"); }

    // Parse the "Key=Value,Key=Value,..." serialized form (defaults applied first).
    explicit TSqlStandardFormatterOptions(const std::string& serializedString) {
        setIndentString("\t");
        if (serializedString.empty())
            return;
        for (const std::string& kvp : splitOn(serializedString, ',')) {
            auto eq = kvp.find('=');
            std::string key = kvp.substr(0, eq);
            std::string value = (eq == std::string::npos) ? "" : kvp.substr(eq + 1);
            if (key == "IndentString") setIndentString(value);
            else if (key == "SpacesPerTab") spacesPerTab = std::stoi(value);
            else if (key == "MaxLineWidth") maxLineWidth = std::stoi(value);
            else if (key == "ExpandCommaLists") expandCommaLists = toBool(value);
            else if (key == "TrailingCommas") trailingCommas = toBool(value);
            else if (key == "SpaceAfterExpandedComma") spaceAfterExpandedComma = toBool(value);
            else if (key == "ExpandBooleanExpressions") expandBooleanExpressions = toBool(value);
            else if (key == "ExpandBetweenConditions") expandBetweenConditions = toBool(value);
            else if (key == "ExpandCaseStatements") expandCaseStatements = toBool(value);
            else if (key == "UppercaseKeywords") uppercaseKeywords = toBool(value);
            else if (key == "BreakJoinOnSections") breakJoinOnSections = toBool(value);
            else if (key == "HTMLColoring") htmlColoring = toBool(value);
            else if (key == "KeywordStandardization") keywordStandardization = toBool(value);
            else if (key == "ExpandInLists") expandInLists = toBool(value);
            else if (key == "NewClauseLineBreaks") newClauseLineBreaks = std::stoi(value);
            else if (key == "NewStatementLineBreaks") newStatementLineBreaks = std::stoi(value);
            else throw std::invalid_argument("Unknown option: " + key);
        }
    }

    // Serialize back to "Key=Value,..." form, emitting only non-default values.
    std::string toSerializedString() const {
        TSqlStandardFormatterOptions d;
        std::vector<std::pair<std::string, std::string>> ov;
        if (indentString != d.indentString) ov.push_back({"IndentString", indentString});
        if (spacesPerTab != d.spacesPerTab) ov.push_back({"SpacesPerTab", std::to_string(spacesPerTab)});
        if (maxLineWidth != d.maxLineWidth) ov.push_back({"MaxLineWidth", std::to_string(maxLineWidth)});
        if (expandCommaLists != d.expandCommaLists) ov.push_back({"ExpandCommaLists", boolStr(expandCommaLists)});
        if (trailingCommas != d.trailingCommas) ov.push_back({"TrailingCommas", boolStr(trailingCommas)});
        if (spaceAfterExpandedComma != d.spaceAfterExpandedComma) ov.push_back({"SpaceAfterExpandedComma", boolStr(spaceAfterExpandedComma)});
        if (expandBooleanExpressions != d.expandBooleanExpressions) ov.push_back({"ExpandBooleanExpressions", boolStr(expandBooleanExpressions)});
        if (expandBetweenConditions != d.expandBetweenConditions) ov.push_back({"ExpandBetweenConditions", boolStr(expandBetweenConditions)});
        if (expandCaseStatements != d.expandCaseStatements) ov.push_back({"ExpandCaseStatements", boolStr(expandCaseStatements)});
        if (uppercaseKeywords != d.uppercaseKeywords) ov.push_back({"UppercaseKeywords", boolStr(uppercaseKeywords)});
        if (breakJoinOnSections != d.breakJoinOnSections) ov.push_back({"BreakJoinOnSections", boolStr(breakJoinOnSections)});
        if (htmlColoring != d.htmlColoring) ov.push_back({"HTMLColoring", boolStr(htmlColoring)});
        if (keywordStandardization != d.keywordStandardization) ov.push_back({"KeywordStandardization", boolStr(keywordStandardization)});
        if (expandInLists != d.expandInLists) ov.push_back({"ExpandInLists", boolStr(expandInLists)});
        if (newClauseLineBreaks != d.newClauseLineBreaks) ov.push_back({"NewClauseLineBreaks", std::to_string(newClauseLineBreaks)});
        if (newStatementLineBreaks != d.newStatementLineBreaks) ov.push_back({"NewStatementLineBreaks", std::to_string(newStatementLineBreaks)});
        std::string out;
        for (size_t i = 0; i < ov.size(); ++i) {
            if (i) out += ",";
            out += ov[i].first + "=" + ov[i].second;
        }
        return out;
    }

    // IndentString setter mirrors C#: "\t" literal → tab, "\s" literal → space.
    void setIndentString(const std::string& value) {
        indentString = replaceAll(replaceAll(value, "\\t", "\t"), "\\s", " ");
    }

    std::string indentString;            // set via setIndentString in ctor
    int  spacesPerTab            = 4;
    int  maxLineWidth            = 999;
    bool expandCommaLists        = true;
    bool trailingCommas          = false;
    bool spaceAfterExpandedComma = false;
    bool expandBooleanExpressions= true;
    bool expandBetweenConditions = true;
    bool expandCaseStatements    = true;
    bool uppercaseKeywords       = true;
    bool breakJoinOnSections     = false;
    bool htmlColoring            = false;
    bool keywordStandardization  = false;
    bool expandInLists           = true;
    int  newClauseLineBreaks     = 1;
    int  newStatementLineBreaks  = 2;

private:
    // .NET Convert.ToBoolean: case-insensitive "true"/"false".
    static bool toBool(const std::string& v) {
        std::string u = toUpperAscii(v);
        if (u == "TRUE") return true;
        if (u == "FALSE") return false;
        throw std::invalid_argument("String was not recognized as a valid Boolean: " + v);
    }
    static std::string boolStr(bool b) { return b ? "True" : "False"; }
    static std::vector<std::string> splitOn(const std::string& s, char delim) {
        std::vector<std::string> out;
        size_t pos = 0, next;
        while ((next = s.find(delim, pos)) != std::string::npos) {
            out.push_back(s.substr(pos, next - pos));
            pos = next + 1;
        }
        out.push_back(s.substr(pos));
        return out;
    }
};

} // namespace pmsf
