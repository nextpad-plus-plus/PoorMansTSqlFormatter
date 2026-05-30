/*
 * SqlConstants.h — faithful C++ port of PoorMansTSqlFormatterLib's
 * Interfaces/SqlTokenType.cs and Interfaces/SqlStructureConstants.cs.
 *
 * Poor Man's T-SQL Formatter — (C) 2011-2017 Tao Klerks (AGPL v3).
 * macOS C++ port: 2026.
 */
#pragma once

#include <string>
#include <vector>

namespace pmsf {

// ── SqlTokenType (Interfaces/SqlTokenType.cs) ───────────────────────────────
enum class SqlTokenType {
    OpenParens,
    CloseParens,
    WhiteSpace,
    OtherNode,
    SingleLineComment,
    SingleLineCommentCStyle,
    MultiLineComment,
    String,
    NationalString,
    BracketQuotedName,
    QuotedString,
    Comma,
    Period,
    Semicolon,
    Colon,
    Asterisk,
    EqualsSign,
    MonetaryValue,
    Number,
    BinaryValue,
    OtherOperator,
    PseudoName
};

// ── SqlStructureConstants (Interfaces/SqlStructureConstants.cs) ──────────────
// Element (node) names.
namespace SC {
inline constexpr const char* ENAME_SQL_ROOT                = "SqlRoot";
inline constexpr const char* ENAME_SQL_STATEMENT           = "SqlStatement";
inline constexpr const char* ENAME_SQL_CLAUSE              = "Clause";
inline constexpr const char* ENAME_SET_OPERATOR_CLAUSE     = "SetOperatorClause";
inline constexpr const char* ENAME_INSERT_CLAUSE           = "InsertClause";
inline constexpr const char* ENAME_BEGIN_END_BLOCK         = "BeginEndBlock";
inline constexpr const char* ENAME_TRY_BLOCK               = "TryBlock";
inline constexpr const char* ENAME_CATCH_BLOCK             = "CatchBlock";
inline constexpr const char* ENAME_BATCH_SEPARATOR         = "BatchSeparator";
inline constexpr const char* ENAME_CASE_STATEMENT          = "CaseStatement";
inline constexpr const char* ENAME_CASE_INPUT              = "Input";
inline constexpr const char* ENAME_CASE_WHEN               = "When";
inline constexpr const char* ENAME_CASE_THEN              = "Then";
inline constexpr const char* ENAME_CASE_ELSE               = "CaseElse";
inline constexpr const char* ENAME_IF_STATEMENT            = "IfStatement";
inline constexpr const char* ENAME_ELSE_CLAUSE             = "ElseClause";
inline constexpr const char* ENAME_BOOLEAN_EXPRESSION      = "BooleanExpression";
inline constexpr const char* ENAME_WHILE_LOOP              = "WhileLoop";
inline constexpr const char* ENAME_CURSOR_DECLARATION      = "CursorDeclaration";
inline constexpr const char* ENAME_CURSOR_FOR_BLOCK        = "CursorForBlock";
inline constexpr const char* ENAME_CURSOR_FOR_OPTIONS      = "CursorForOptions";
inline constexpr const char* ENAME_CTE_WITH_CLAUSE         = "CTEWithClause";
inline constexpr const char* ENAME_CTE_ALIAS               = "CTEAlias";
inline constexpr const char* ENAME_CTE_AS_BLOCK            = "CTEAsBlock";
inline constexpr const char* ENAME_BEGIN_TRANSACTION       = "BeginTransaction";
inline constexpr const char* ENAME_COMMIT_TRANSACTION      = "CommitTransaction";
inline constexpr const char* ENAME_ROLLBACK_TRANSACTION    = "RollbackTransaction";
inline constexpr const char* ENAME_SAVE_TRANSACTION        = "SaveTransaction";
inline constexpr const char* ENAME_DDL_DECLARE_BLOCK       = "DDLDeclareBlock";
inline constexpr const char* ENAME_DDL_PROCEDURAL_BLOCK    = "DDLProceduralBlock";
inline constexpr const char* ENAME_DDL_OTHER_BLOCK         = "DDLOtherBlock";
inline constexpr const char* ENAME_DDL_AS_BLOCK            = "DDLAsBlock";
inline constexpr const char* ENAME_DDL_PARENS              = "DDLParens";
inline constexpr const char* ENAME_DDL_SUBCLAUSE           = "DDLSubClause";
inline constexpr const char* ENAME_DDL_RETURNS             = "DDLReturns";
inline constexpr const char* ENAME_DDLDETAIL_PARENS        = "DDLDetailParens";
inline constexpr const char* ENAME_DDL_WITH_CLAUSE         = "DDLWith";
inline constexpr const char* ENAME_PERMISSIONS_BLOCK       = "PermissionsBlock";
inline constexpr const char* ENAME_PERMISSIONS_DETAIL      = "PermissionsDetail";
inline constexpr const char* ENAME_PERMISSIONS_TARGET      = "PermissionsTarget";
inline constexpr const char* ENAME_PERMISSIONS_RECIPIENT   = "PermissionsRecipient";
inline constexpr const char* ENAME_TRIGGER_CONDITION       = "TriggerCondition";
inline constexpr const char* ENAME_SELECTIONTARGET_PARENS  = "SelectionTargetParens";
inline constexpr const char* ENAME_EXPRESSION_PARENS       = "ExpressionParens";
inline constexpr const char* ENAME_FUNCTION_PARENS         = "FunctionParens";
inline constexpr const char* ENAME_IN_PARENS               = "InParens";
inline constexpr const char* ENAME_FUNCTION_KEYWORD        = "FunctionKeyword";
inline constexpr const char* ENAME_DATATYPE_KEYWORD        = "DataTypeKeyword";
inline constexpr const char* ENAME_COMPOUNDKEYWORD         = "CompoundKeyword";
inline constexpr const char* ENAME_OTHERKEYWORD            = "OtherKeyword";
inline constexpr const char* ENAME_LABEL                   = "Label";
inline constexpr const char* ENAME_CONTAINER_OPEN          = "ContainerOpen";
inline constexpr const char* ENAME_CONTAINER_MULTISTATEMENT = "ContainerMultiStatementBody";
inline constexpr const char* ENAME_CONTAINER_SINGLESTATEMENT = "ContainerSingleStatementBody";
inline constexpr const char* ENAME_CONTAINER_GENERALCONTENT = "ContainerContentBody";
inline constexpr const char* ENAME_CONTAINER_CLOSE         = "ContainerClose";
inline constexpr const char* ENAME_SELECTIONTARGET         = "SelectionTarget";
inline constexpr const char* ENAME_MERGE_CLAUSE            = "MergeClause";
inline constexpr const char* ENAME_MERGE_TARGET            = "MergeTarget";
inline constexpr const char* ENAME_MERGE_USING             = "MergeUsing";
inline constexpr const char* ENAME_MERGE_CONDITION         = "MergeCondition";
inline constexpr const char* ENAME_MERGE_WHEN              = "MergeWhen";
inline constexpr const char* ENAME_MERGE_THEN              = "MergeThen";
inline constexpr const char* ENAME_MERGE_ACTION            = "MergeAction";
inline constexpr const char* ENAME_JOIN_ON_SECTION        = "JoinOn";

inline constexpr const char* ENAME_PSEUDONAME              = "PseudoName";
inline constexpr const char* ENAME_WHITESPACE              = "WhiteSpace";
inline constexpr const char* ENAME_OTHERNODE               = "Other";
inline constexpr const char* ENAME_COMMENT_SINGLELINE      = "SingleLineComment";
inline constexpr const char* ENAME_COMMENT_SINGLELINE_CSTYLE = "SingleLineCommentCStyle";
inline constexpr const char* ENAME_COMMENT_MULTILINE       = "MultiLineComment";
inline constexpr const char* ENAME_STRING                  = "String";
inline constexpr const char* ENAME_NSTRING                 = "NationalString";
inline constexpr const char* ENAME_QUOTED_STRING           = "QuotedString";
inline constexpr const char* ENAME_BRACKET_QUOTED_NAME     = "BracketQuotedName";
inline constexpr const char* ENAME_COMMA                   = "Comma";
inline constexpr const char* ENAME_PERIOD                  = "Period";
inline constexpr const char* ENAME_SEMICOLON               = "Semicolon";
inline constexpr const char* ENAME_SCOPERESOLUTIONOPERATOR = "ScopeResolutionOperator";
inline constexpr const char* ENAME_ASTERISK                = "Asterisk";
inline constexpr const char* ENAME_EQUALSSIGN              = "EqualsSign";
inline constexpr const char* ENAME_ALPHAOPERATOR           = "AlphaOperator";
inline constexpr const char* ENAME_OTHEROPERATOR           = "OtherOperator";

inline constexpr const char* ENAME_AND_OPERATOR            = "And";
inline constexpr const char* ENAME_OR_OPERATOR             = "Or";
inline constexpr const char* ENAME_BETWEEN_CONDITION       = "Between";
inline constexpr const char* ENAME_BETWEEN_LOWERBOUND      = "LowerBound";
inline constexpr const char* ENAME_BETWEEN_UPPERBOUND      = "UpperBound";

inline constexpr const char* ENAME_NUMBER_VALUE            = "NumberValue";
inline constexpr const char* ENAME_MONETARY_VALUE          = "MonetaryValue";
inline constexpr const char* ENAME_BINARY_VALUE            = "BinaryValue";

// attribute names
inline constexpr const char* ANAME_ERRORFOUND   = "errorFound";
inline constexpr const char* ANAME_HASERROR     = "hasError";
inline constexpr const char* ANAME_DATALOSS     = "dataLossLimitation";
inline constexpr const char* ANAME_SIMPLETEXT   = "simpleText";

// element-name groups
inline const std::vector<std::string> ENAMELIST_COMMENT = {
    ENAME_COMMENT_MULTILINE, ENAME_COMMENT_SINGLELINE, ENAME_COMMENT_SINGLELINE_CSTYLE
};
inline const std::vector<std::string> ENAMELIST_NONCONTENT = {
    ENAME_WHITESPACE, ENAME_COMMENT_MULTILINE, ENAME_COMMENT_SINGLELINE, ENAME_COMMENT_SINGLELINE_CSTYLE
};
inline const std::vector<std::string> ENAMELIST_NONSEMANTICCONTENT = {
    ENAME_SQL_CLAUSE, ENAME_DDL_PROCEDURAL_BLOCK, ENAME_DDL_OTHER_BLOCK, ENAME_DDL_DECLARE_BLOCK
};

} // namespace SC

// MessagingConstants.cs — the error-warning prefix prepended to output.
inline constexpr const char* ERROR_FOUND_WARNING = "--WARNING! ERRORS ENCOUNTERED DURING SQL PARSING!\r\n";

} // namespace pmsf
