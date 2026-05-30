/*
 * Tokenizer.cpp — see Tokenizer.h. Faithful port of TSqlStandardTokenizer.cs.
 *
 * Differences from the C#: processes Unicode code points (char32_t) rather than
 * UTF-16 units (identical for the BMP, which covers all of SQL incl. the
 * currency-prefix symbols); and the cursor "marker" bookkeeping is omitted
 * (we always tokenize with a null marker).
 */
#include "Tokenizer.h"
#include "Utf8.h"
#include <stdexcept>

namespace pmsf {
namespace {

enum class TKZ {
    None,
    // variable-length types
    WhiteSpace, OtherNode, SingleLineComment, SingleLineCommentCStyle, BlockComment,
    String, NString, QuotedString, BracketQuotedName, OtherOperator, Number, BinaryValue,
    MonetaryValue, DecimalValue, FloatValue, PseudoName,
    // temporary types
    SingleAsterisk, SingleDollar, SingleHyphen, SingleSlash, SingleN, SingleLT, SingleGT,
    SingleExclamation, SinglePeriod, SingleZero, SinglePipe, SingleEquals,
    SingleOtherCompoundableOperator
};

bool isWhitespace(char32_t c) {
    return c == ' ' || c == '\t' || c == (char32_t)10 || c == (char32_t)13;
}

bool isCurrencyPrefix(char32_t c) {
    switch (c) {
        case 0x0024: case 0x00A2: case 0x00A3: case 0x00A4: case 0x00A5:
        case 0x09F2: case 0x09F3: case 0x0E3F: case 0x17DB:
        case 0x20A0: case 0x20A1: case 0x20A2: case 0x20A3: case 0x20A4:
        case 0x20A5: case 0x20A6: case 0x20A7: case 0x20A8: case 0x20A9:
        case 0x20AA: case 0x20AB: case 0x20AC: case 0x20AD: case 0x20AE:
        case 0x20AF: case 0x20B0: case 0x20B1:
        case 0xFDFC: case 0xFE69: case 0xFF04: case 0xFFE0: case 0xFFE1:
        case 0xFFE5: case 0xFFE6:
            return true;
        default:
            return false;
    }
}

bool isCompoundableOperatorCharacter(char32_t c) {
    return c == '/' || c == '-' || c == '+' || c == '*' || c == '%' || c == '&'
        || c == '^' || c == '<' || c == '>' || c == '|';
}

bool isOperatorCharacter(char32_t c) {
    return c == '/' || c == '-' || c == '+' || c == '%' || c == '*' || c == '&'
        || c == '|' || c == '^' || c == '=' || c == '<' || c == '>' || c == '~';
}

bool isNonWordCharacter(char32_t c) {
    return isWhitespace(c)
        || isOperatorCharacter(c)
        || (isCurrencyPrefix(c) && c != '$')
        || c == '\'' || c == '"' || c == ',' || c == '.' || c == '['
        || c == '(' || c == ')' || c == '!' || c == ';' || c == ':';
}

class TokenizerImpl {
public:
    explicit TokenizerImpl(const std::string& inputUtf8)
        : input_(utf8Decode(inputUtf8)) {}

    TokenList run() {
        readNextCharacter();
        while (hasUnprocessed_) {
            if (type_ == TKZ::None) {
                processOrOpenToken();
                readNextCharacter();
                continue;
            }
            dispatch();
            readNextCharacter();
        }
        if (type_ != TKZ::None) {
            if (type_ == TKZ::BlockComment || type_ == TKZ::String || type_ == TKZ::NString
                || type_ == TKZ::QuotedString || type_ == TKZ::BracketQuotedName)
                tokens_.hasUnfinishedToken = true;
            swallowOutstandingCharacterAndCompleteToken();
        }
        return std::move(tokens_);
    }

private:
    std::u32string input_;
    size_t next_ = 0;
    TokenList tokens_;
    TKZ type_ = TKZ::None;
    std::u32string tokenValue_;
    int commentNesting_ = 0;
    long currentCharInt_ = -1;
    bool hasUnprocessed_ = false;

    // SimplifiedStringReader
    long readChar() { long c = peekChar(); ++next_; return c; }
    long peekChar() const { return next_ < input_.size() ? (long)input_[next_] : -1; }

    char32_t cur() const {
        if (currentCharInt_ < 0 || !hasUnprocessed_)
            throw std::runtime_error("no/consumed current character");
        return (char32_t)currentCharInt_;
    }

    void readNextCharacter() {
        if (hasUnprocessed_) throw std::runtime_error("Unprocessed character detected!");
        currentCharInt_ = readChar();
        if (currentCharInt_ >= 0) hasUnprocessed_ = true;
    }
    void consume() {
        if (!hasUnprocessed_) throw std::runtime_error("No current character to consume!");
        tokenValue_.push_back(cur());
        hasUnprocessed_ = false;
    }
    void discardNextCharacter() { readNextCharacter(); hasUnprocessed_ = false; }

    bool valueEndsWithE() const {
        return !tokenValue_.empty() && (tokenValue_.back() == 'e' || tokenValue_.back() == 'E');
    }
    bool valueContainsPeriod() const {
        return tokenValue_.find('.') != std::u32string::npos;
    }

    // ── token finalization ───────────────────────────────────────────────────
    void saveToken(SqlTokenType t, const std::string& v) { tokens_.add(t, v); }
    std::string val() const { return utf8Encode(tokenValue_); }

    void completeToken() {
        switch (type_) {
            case TKZ::BlockComment:              saveToken(SqlTokenType::MultiLineComment, val()); break;
            case TKZ::OtherNode:                 saveToken(SqlTokenType::OtherNode, val()); break;
            case TKZ::PseudoName:                saveToken(SqlTokenType::PseudoName, val()); break;
            case TKZ::SingleLineComment:         saveToken(SqlTokenType::SingleLineComment, val()); break;
            case TKZ::SingleLineCommentCStyle:   saveToken(SqlTokenType::SingleLineCommentCStyle, val()); break;
            case TKZ::SingleHyphen:              saveToken(SqlTokenType::OtherOperator, "-"); break;
            case TKZ::SingleDollar:              saveToken(SqlTokenType::MonetaryValue, "$"); break;
            case TKZ::SingleSlash:               saveToken(SqlTokenType::OtherOperator, "/"); break;
            case TKZ::WhiteSpace:                saveToken(SqlTokenType::WhiteSpace, val()); break;
            case TKZ::SingleN:                   saveToken(SqlTokenType::OtherNode, "N"); break;
            case TKZ::SingleExclamation:         saveToken(SqlTokenType::OtherNode, "!"); break;
            case TKZ::SinglePipe:                saveToken(SqlTokenType::OtherNode, "|"); break;
            case TKZ::SingleGT:                  saveToken(SqlTokenType::OtherOperator, ">"); break;
            case TKZ::SingleLT:                  saveToken(SqlTokenType::OtherOperator, "<"); break;
            case TKZ::NString:                   saveToken(SqlTokenType::NationalString, val()); break;
            case TKZ::String:                    saveToken(SqlTokenType::String, val()); break;
            case TKZ::QuotedString:              saveToken(SqlTokenType::QuotedString, val()); break;
            case TKZ::BracketQuotedName:         saveToken(SqlTokenType::BracketQuotedName, val()); break;
            case TKZ::OtherOperator:
            case TKZ::SingleOtherCompoundableOperator: saveToken(SqlTokenType::OtherOperator, val()); break;
            case TKZ::SingleZero:                saveToken(SqlTokenType::Number, "0"); break;
            case TKZ::SinglePeriod:              saveToken(SqlTokenType::Period, "."); break;
            case TKZ::SingleAsterisk:            saveToken(SqlTokenType::Asterisk, val()); break;
            case TKZ::SingleEquals:              saveToken(SqlTokenType::EqualsSign, val()); break;
            case TKZ::Number:
            case TKZ::DecimalValue:
            case TKZ::FloatValue:                saveToken(SqlTokenType::Number, val()); break;
            case TKZ::BinaryValue:               saveToken(SqlTokenType::BinaryValue, val()); break;
            case TKZ::MonetaryValue:             saveToken(SqlTokenType::MonetaryValue, val()); break;
            default: throw std::runtime_error("Unrecognized SQL Node Type");
        }
        type_ = TKZ::None;
    }

    void completeTokenAndProcessNext() { completeToken(); processOrOpenToken(); }
    void appendCharAndCompleteToken() { consume(); completeToken(); }
    void swallowOutstandingCharacterAndCompleteToken() { hasUnprocessed_ = false; completeToken(); }

    void saveCurrentCharToNewToken(SqlTokenType t) {
        char32_t c = cur();
        hasUnprocessed_ = false;
        std::string s; utf8Append(s, c);
        saveToken(t, s);
    }

    // ── open a new token from the current character ───────────────────────────
    void processOrOpenToken() {
        if (type_ != TKZ::None) throw std::runtime_error("existing tokenization type not null");
        if (!hasUnprocessed_) throw std::runtime_error("no outstanding current character");
        tokenValue_.clear();
        char32_t c = cur();
        if (isWhitespace(c))      { type_ = TKZ::WhiteSpace; consume(); }
        else if (c == '-')        { type_ = TKZ::SingleHyphen; hasUnprocessed_ = false; }
        else if (c == '$')        { type_ = TKZ::SingleDollar; hasUnprocessed_ = false; }
        else if (c == '/')        { type_ = TKZ::SingleSlash; hasUnprocessed_ = false; }
        else if (c == 'N')        { type_ = TKZ::SingleN; hasUnprocessed_ = false; }
        else if (c == '\'')       { type_ = TKZ::String; hasUnprocessed_ = false; }
        else if (c == '"')        { type_ = TKZ::QuotedString; hasUnprocessed_ = false; }
        else if (c == '[')        { type_ = TKZ::BracketQuotedName; hasUnprocessed_ = false; }
        else if (c == '(')        { saveCurrentCharToNewToken(SqlTokenType::OpenParens); }
        else if (c == ')')        { saveCurrentCharToNewToken(SqlTokenType::CloseParens); }
        else if (c == ',')        { saveCurrentCharToNewToken(SqlTokenType::Comma); }
        else if (c == '.')        { type_ = TKZ::SinglePeriod; hasUnprocessed_ = false; }
        else if (c == '0')        { type_ = TKZ::SingleZero; hasUnprocessed_ = false; }
        else if (c >= '1' && c <= '9') { type_ = TKZ::Number; consume(); }
        else if (isCurrencyPrefix(c)) { type_ = TKZ::MonetaryValue; consume(); }
        else if (c == ';')        { saveCurrentCharToNewToken(SqlTokenType::Semicolon); }
        else if (c == ':')        { saveCurrentCharToNewToken(SqlTokenType::Colon); }
        else if (c == '*')        { type_ = TKZ::SingleAsterisk; hasUnprocessed_ = false; }
        else if (c == '=')        { type_ = TKZ::SingleEquals; hasUnprocessed_ = false; }
        else if (c == '<')        { type_ = TKZ::SingleLT; hasUnprocessed_ = false; }
        else if (c == '>')        { type_ = TKZ::SingleGT; hasUnprocessed_ = false; }
        else if (c == '!')        { type_ = TKZ::SingleExclamation; hasUnprocessed_ = false; }
        else if (c == '|')        { type_ = TKZ::SinglePipe; hasUnprocessed_ = false; }
        else if (isCompoundableOperatorCharacter(c)) { type_ = TKZ::SingleOtherCompoundableOperator; consume(); }
        else if (isOperatorCharacter(c)) { saveCurrentCharToNewToken(SqlTokenType::OtherOperator); }
        else                      { type_ = TKZ::OtherNode; consume(); }
    }

    // ── continue the in-progress token ────────────────────────────────────────
    void dispatch() {
        char32_t c = cur();
        switch (type_) {
            case TKZ::WhiteSpace:
                if (isWhitespace(c)) consume(); else completeTokenAndProcessNext();
                break;
            case TKZ::SinglePeriod:
                if (c >= '0' && c <= '9') { type_ = TKZ::DecimalValue; tokenValue_.push_back('.'); consume(); }
                else { tokenValue_.push_back('.'); completeTokenAndProcessNext(); }
                break;
            case TKZ::SingleZero:
                if (c == 'x' || c == 'X') { type_ = TKZ::BinaryValue; tokenValue_.push_back('0'); consume(); }
                else if (c >= '0' && c <= '9') { type_ = TKZ::Number; tokenValue_.push_back('0'); consume(); }
                else if (c == '.') { type_ = TKZ::DecimalValue; tokenValue_.push_back('0'); consume(); }
                else { tokenValue_.push_back('0'); completeTokenAndProcessNext(); }
                break;
            case TKZ::Number:
                if (c == 'e' || c == 'E') { type_ = TKZ::FloatValue; consume(); }
                else if (c == '.') { type_ = TKZ::DecimalValue; consume(); }
                else if (c >= '0' && c <= '9') consume();
                else completeTokenAndProcessNext();
                break;
            case TKZ::DecimalValue:
                if (c == 'e' || c == 'E') { type_ = TKZ::FloatValue; consume(); }
                else if (c >= '0' && c <= '9') consume();
                else completeTokenAndProcessNext();
                break;
            case TKZ::FloatValue:
                if (c >= '0' && c <= '9') consume();
                else if ((c == '-' || c == '+') && valueEndsWithE()) consume();
                else completeTokenAndProcessNext();
                break;
            case TKZ::BinaryValue:
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) consume();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleDollar:
                tokenValue_.push_back('$');
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) type_ = TKZ::PseudoName;
                else type_ = TKZ::MonetaryValue;
                consume();
                break;
            case TKZ::MonetaryValue:
                if (c >= '0' && c <= '9') consume();
                else if (c == '-' && tokenValue_.size() == 1) consume();
                else if (c == '.' && !valueContainsPeriod()) consume();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleHyphen:
                if (c == '-') { type_ = TKZ::SingleLineComment; hasUnprocessed_ = false; }
                else if (c == '=') { type_ = TKZ::OtherOperator; tokenValue_.push_back('-'); appendCharAndCompleteToken(); }
                else { type_ = TKZ::OtherOperator; tokenValue_.push_back('-'); completeTokenAndProcessNext(); }
                break;
            case TKZ::SingleSlash:
                if (c == '*') { type_ = TKZ::BlockComment; hasUnprocessed_ = false; commentNesting_++; }
                else if (c == '/') { type_ = TKZ::SingleLineCommentCStyle; hasUnprocessed_ = false; }
                else if (c == '=') { type_ = TKZ::OtherOperator; tokenValue_.push_back('/'); appendCharAndCompleteToken(); }
                else { type_ = TKZ::OtherOperator; tokenValue_.push_back('/'); completeTokenAndProcessNext(); }
                break;
            case TKZ::SingleLineComment:
            case TKZ::SingleLineCommentCStyle:
                if (c == (char32_t)13 || c == (char32_t)10) {
                    long nextCharInt = peekChar();
                    if (c == (char32_t)13 && nextCharInt == 10) { consume(); readNextCharacter(); }
                    appendCharAndCompleteToken();
                } else consume();
                break;
            case TKZ::BlockComment:
                if (c == '*') {
                    if (peekChar() == (long)'/') {
                        commentNesting_--;
                        if (commentNesting_ > 0) { consume(); readNextCharacter(); consume(); }
                        else { hasUnprocessed_ = false; readNextCharacter(); swallowOutstandingCharacterAndCompleteToken(); }
                    } else consume();
                } else {
                    if (c == '/' && peekChar() == (long)'*') { consume(); readNextCharacter(); consume(); commentNesting_++; }
                    else consume();
                }
                break;
            case TKZ::OtherNode:
            case TKZ::PseudoName:
                if (isNonWordCharacter(c)) completeTokenAndProcessNext();
                else consume();
                break;
            case TKZ::SingleN:
                if (c == '\'') { type_ = TKZ::NString; hasUnprocessed_ = false; }
                else if (isNonWordCharacter(c)) completeTokenAndProcessNext();
                else { type_ = TKZ::OtherNode; tokenValue_.push_back('N'); consume(); }
                break;
            case TKZ::NString:
            case TKZ::String:
                if (c == '\'') {
                    if (peekChar() == (long)'\'') { consume(); discardNextCharacter(); }
                    else swallowOutstandingCharacterAndCompleteToken();
                } else consume();
                break;
            case TKZ::QuotedString:
                if (c == '"') {
                    if (peekChar() == (long)'"') { consume(); discardNextCharacter(); }
                    else swallowOutstandingCharacterAndCompleteToken();
                } else consume();
                break;
            case TKZ::BracketQuotedName:
                if (c == ']') {
                    if (peekChar() == (long)']') { consume(); discardNextCharacter(); }
                    else swallowOutstandingCharacterAndCompleteToken();
                } else consume();
                break;
            case TKZ::SingleLT:
                tokenValue_.push_back('<'); type_ = TKZ::OtherOperator;
                if (c == '=' || c == '>' || c == '<') appendCharAndCompleteToken();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleGT:
                tokenValue_.push_back('>'); type_ = TKZ::OtherOperator;
                if (c == '=' || c == '>') appendCharAndCompleteToken();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleAsterisk:
                tokenValue_.push_back('*');
                if (c == '=') { type_ = TKZ::OtherOperator; appendCharAndCompleteToken(); }
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleOtherCompoundableOperator:
                type_ = TKZ::OtherOperator;
                if (c == '=') appendCharAndCompleteToken();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SinglePipe:
                type_ = TKZ::OtherOperator; tokenValue_.push_back('|');
                if (c == '=' || c == '|') appendCharAndCompleteToken();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleEquals:
                tokenValue_.push_back('=');
                if (c == '=') appendCharAndCompleteToken();
                else completeTokenAndProcessNext();
                break;
            case TKZ::SingleExclamation:
                tokenValue_.push_back('!');
                if (c == '=' || c == '<' || c == '>') { type_ = TKZ::OtherOperator; appendCharAndCompleteToken(); }
                else { type_ = TKZ::OtherNode; completeTokenAndProcessNext(); }
                break;
            default:
                throw std::runtime_error("In-progress node unrecognized!");
        }
    }
};

} // namespace

TokenList tokenizeSQL(const std::string& inputUtf8) {
    TokenizerImpl impl(inputUtf8);
    return impl.run();
}

} // namespace pmsf
