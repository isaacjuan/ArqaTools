// AcmlLexer.cpp — ACML tokenizer implementation
#include "StdAfx.h"
#include "AcmlLexer.h"
#include <cctype>
#include <sstream>

namespace Acml
{

// ============================================================================
// Constructor
// ============================================================================
AcmlLexer::AcmlLexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename)
{}

// ============================================================================
// tokenize — main entry point
// ============================================================================
std::vector<Token> AcmlLexer::tokenize()
{
    std::vector<Token> tokens;
    tokens.reserve(source_.size() / 8);   // rough pre-allocation

    while (true)
    {
        skipSpaces();

        if (atEnd())
        {
            tokens.push_back(make(TokenType::Eof));
            break;
        }

        char c = peek();

        // ── Comments ─────────────────────────────────────────────────────
        if (c == '/' && peek(1) == '/')
        {
            skipLineComment();
            continue;
        }
        if (c == '/' && peek(1) == '*')
        {
            skipBlockComment();
            continue;
        }

        // ── Newline (significant separator) ──────────────────────────────
        if (c == '\n')
        {
            tokens.push_back(make(TokenType::Newline, "\\n"));
            advance();
            continue;
        }

        // ── Single-character delimiters ───────────────────────────────────
        switch (c)
        {
        case '{': tokens.push_back(make(TokenType::LBrace,    "{")); advance(); continue;
        case '}': tokens.push_back(make(TokenType::RBrace,    "}")); advance(); continue;
        case '(': tokens.push_back(make(TokenType::LParen,    "(")); advance(); continue;
        case ')': tokens.push_back(make(TokenType::RParen,    ")")); advance(); continue;
        case '[': tokens.push_back(make(TokenType::LBracket,  "[")); advance(); continue;
        case ']': tokens.push_back(make(TokenType::RBracket,  "]")); advance(); continue;
        case ':': tokens.push_back(make(TokenType::Colon,     ":")); advance(); continue;
        case ';': tokens.push_back(make(TokenType::Semicolon, ";")); advance(); continue;
        case ',': tokens.push_back(make(TokenType::Comma,     ",")); advance(); continue;
        case '.': tokens.push_back(make(TokenType::Dot,       ".")); advance(); continue;
        case '+': tokens.push_back(make(TokenType::Plus,      "+")); advance(); continue;
        case '-': tokens.push_back(make(TokenType::Minus,     "-")); advance(); continue;
        case '*': tokens.push_back(make(TokenType::Star,      "*")); advance(); continue;
        case '%': tokens.push_back(make(TokenType::Percent,   "%")); advance(); continue;
        }

        // ── Multi-character operators ─────────────────────────────────────
        if (c == '/')
        {
            tokens.push_back(make(TokenType::Slash, "/"));
            advance();
            continue;
        }
        if (c == '=' && peek(1) == '=')
        {
            tokens.push_back(make(TokenType::EqEq, "=="));
            advance(); advance();
            continue;
        }
        if (c == '!' && peek(1) == '=')
        {
            tokens.push_back(make(TokenType::NotEq, "!="));
            advance(); advance();
            continue;
        }
        if (c == '<' && peek(1) == '=')
        {
            tokens.push_back(make(TokenType::LtEq, "<="));
            advance(); advance();
            continue;
        }
        if (c == '>' && peek(1) == '=')
        {
            tokens.push_back(make(TokenType::GtEq, ">="));
            advance(); advance();
            continue;
        }
        if (c == '<') { tokens.push_back(make(TokenType::Lt, "<")); advance(); continue; }
        if (c == '>') { tokens.push_back(make(TokenType::Gt, ">")); advance(); continue; }
        if (c == '&' && peek(1) == '&')
        {
            tokens.push_back(make(TokenType::AmpAmp, "&&"));
            advance(); advance();
            continue;
        }
        if (c == '|' && peek(1) == '|')
        {
            tokens.push_back(make(TokenType::PipePipe, "||"));
            advance(); advance();
            continue;
        }

        // ── Value tokens ─────────────────────────────────────────────────
        if (c == '"')  { tokens.push_back(readString());      continue; }
        if (c == '#')  { tokens.push_back(readColorHex());    continue; }
        if (std::isdigit((unsigned char)c))
        {
            tokens.push_back(readNumber());
            continue;
        }
        if (std::isupper((unsigned char)c))
        {
            tokens.push_back(readElementType());
            continue;
        }
        if (std::islower((unsigned char)c) || c == '_')
        {
            tokens.push_back(readIdentifier());
            continue;
        }

        // ── Unrecognised character ────────────────────────────────────────
        std::string msg = "Unexpected character '";
        msg += c;
        msg += "'";
        tokens.push_back(error(msg));
        advance();
    }

    return tokens;
}

// ============================================================================
// Low-level helpers
// ============================================================================
char AcmlLexer::peek(size_t offset) const
{
    size_t idx = pos_ + offset;
    return (idx < source_.size()) ? source_[idx] : '\0';
}

char AcmlLexer::advance()
{
    char c = source_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

bool AcmlLexer::matchSeq(const std::string& s)
{
    if (pos_ + s.size() > source_.size()) return false;
    for (size_t i = 0; i < s.size(); ++i)
        if (source_[pos_ + i] != s[i]) return false;
    for (size_t i = 0; i < s.size(); ++i) advance();
    return true;
}

// ============================================================================
// Skip helpers
// ============================================================================
void AcmlLexer::skipSpaces()
{
    while (!atEnd())
    {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') advance();
        else break;
    }
}

void AcmlLexer::skipLineComment()
{
    // consume // ...  up to (but not including) the newline
    while (!atEnd() && peek() != '\n')
        advance();
}

void AcmlLexer::skipBlockComment()
{
    advance(); advance(); // consume /*
    while (!atEnd())
    {
        if (peek() == '*' && peek(1) == '/')
        {
            advance(); advance(); // consume */
            return;
        }
        advance();
    }
    errors_.push_back(filename_ + ":" + std::to_string(line_) +
                      ": unterminated block comment");
}

// ============================================================================
// Readers
// ============================================================================
Token AcmlLexer::readNumber()
{
    int startLine = line_, startCol = col_;
    std::string value;

    while (!atEnd() && std::isdigit((unsigned char)peek()))
        value += advance();

    // Decimal part
    if (peek() == '.' && std::isdigit((unsigned char)peek(1)))
    {
        value += advance(); // '.'
        while (!atEnd() && std::isdigit((unsigned char)peek()))
            value += advance();
    }

    Token t;
    t.type  = TokenType::Number;
    t.value = value;
    t.line  = startLine;
    t.col   = startCol;
    t.unit  = tryReadUnit();
    return t;
}

std::string AcmlLexer::tryReadUnit()
{
    // Units must start immediately after digits (no whitespace).
    // Each unit must be followed by a non-identifier character.
    auto isUnitEnd = [&]() -> bool {
        char c = peek();
        return atEnd() || (!std::isalnum((unsigned char)c) && c != '_');
    };

    // Check longest match first (deg before d, mm/cm before m)
    struct { const char* str; } units[] = {
        { "mm" }, { "cm" }, { "deg" }, { "rad" }, { "m" }, { nullptr }
    };

    for (int i = 0; units[i].str; ++i)
    {
        const std::string u = units[i].str;
        if (source_.compare(pos_, u.size(), u) == 0)
        {
            // Peek after the unit to ensure it ends
            size_t afterPos = pos_ + u.size();
            char   after    = (afterPos < source_.size()) ? source_[afterPos] : '\0';
            if (!std::isalnum((unsigned char)after) && after != '_')
            {
                for (size_t k = 0; k < u.size(); ++k) advance();
                return u;
            }
        }
    }

    // '%' as unit (after a number means percentage)
    if (peek() == '%')
    {
        advance();
        return "%";
    }

    return "";
}

Token AcmlLexer::readString()
{
    int startLine = line_, startCol = col_;
    advance(); // opening "
    std::string value;

    while (!atEnd() && peek() != '"')
    {
        if (peek() == '\n')
        {
            errors_.push_back(filename_ + ":" + std::to_string(startLine) +
                              ": unterminated string");
            break;
        }
        if (peek() == '\\' && peek(1) == '"')
        {
            advance();  // backslash
            value += advance(); // escaped quote
        }
        else
        {
            value += advance();
        }
    }

    if (!atEnd()) advance(); // closing "

    Token t;
    t.type  = TokenType::String;
    t.value = value;
    t.line  = startLine;
    t.col   = startCol;
    return t;
}

Token AcmlLexer::readColorHex()
{
    int startLine = line_, startCol = col_;
    advance(); // '#'
    std::string value = "#";

    for (int i = 0; i < 6; ++i)
    {
        if (atEnd() || !std::isxdigit((unsigned char)peek()))
        {
            errors_.push_back(filename_ + ":" + std::to_string(startLine) +
                              ": invalid color hex (expected 6 hex digits)");
            break;
        }
        value += advance();
    }

    Token t;
    t.type  = TokenType::ColorHex;
    t.value = value;
    t.line  = startLine;
    t.col   = startCol;
    return t;
}

Token AcmlLexer::readElementType()
{
    int startLine = line_, startCol = col_;
    std::string value;

    while (!atEnd() && std::isalnum((unsigned char)peek()))
        value += advance();

    Token t;
    t.type  = TokenType::ElementType;
    t.value = value;
    t.line  = startLine;
    t.col   = startCol;
    return t;
}

Token AcmlLexer::readIdentifier()
{
    int startLine = line_, startCol = col_;
    std::string value;

    while (!atEnd() && (std::isalnum((unsigned char)peek()) || peek() == '_'))
        value += advance();

    Token t;
    t.type  = TokenType::Identifier;
    t.value = value;
    t.line  = startLine;
    t.col   = startCol;
    return t;
}

// ============================================================================
// Token factories
// ============================================================================
Token AcmlLexer::make(TokenType type, const std::string& value)
{
    Token t;
    t.type  = type;
    t.value = value;
    t.line  = line_;
    t.col   = col_;
    return t;
}

Token AcmlLexer::error(const std::string& msg)
{
    std::string full = filename_ + ":" + std::to_string(line_) +
                       ":" + std::to_string(col_) + ": " + msg;
    errors_.push_back(full);

    Token t;
    t.type  = TokenType::Error;
    t.value = msg;
    t.line  = line_;
    t.col   = col_;
    return t;
}

} // namespace Acml
