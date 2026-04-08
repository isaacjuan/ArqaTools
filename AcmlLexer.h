// AcmlLexer.h — ACML tokenizer
//
// Converts a UTF-8 .acml source string into a flat token stream.
// No ObjectARX dependency — pure C++20 standard library.
//
// Usage:
//   std::string src = ...; // file contents
//   AcmlLexer lexer(src, "myfile.acml");
//   auto tokens = lexer.tokenize();
//   if (lexer.hasErrors()) { /* lexer.errors() */ }

#pragma once
#include <string>
#include <vector>
#include <optional>

namespace Acml
{

// ============================================================================
// Token types
// ============================================================================
enum class TokenType
{
    // Delimiters
    LBrace, RBrace,           // { }
    LParen, RParen,           // ( )
    LBracket, RBracket,       // [ ]
    Colon,                    // :
    Semicolon,                // ;
    Comma,                    // ,
    Dot,                      // .
    Newline,                  // \n  (significant as property separator)

    // Values
    ElementType,              // [A-Z][a-zA-Z0-9]*       e.g. Wall, Window
    Identifier,               // [a-z_][a-z0-9_]*        e.g. wall_a, self, true
    Number,                   // [0-9]+(\.[0-9]+)?       e.g. 1200, 3.14
    String,                   // "..."
    ColorHex,                 // #RRGGBB

    // Operators
    Plus,                     // +
    Minus,                    // -
    Star,                     // *
    Slash,                    // /
    Percent,                  // %  (modulo operator; unit '%' lives in Token::unit)
    EqEq,                     // ==
    NotEq,                    // !=
    Lt,                       // <
    Gt,                       // >
    LtEq,                     // <=
    GtEq,                     // >=
    AmpAmp,                   // &&
    PipePipe,                 // ||

    // Sentinel
    Eof,
    Error
};

// ============================================================================
// Token
// ============================================================================
struct Token
{
    TokenType   type  = TokenType::Error;
    std::string value;          // raw text (number digits, identifier name, string body…)
    std::string unit;           // for Number tokens: "mm" | "cm" | "m" | "deg" | "rad" | "%"
    int         line  = 1;
    int         col   = 1;

    // Convenience: check for specific identifier/keyword text
    bool is(const std::string& v) const { return value == v; }
};

// ============================================================================
// Lexer
// ============================================================================
class AcmlLexer
{
public:
    // -------------------------------------------------------------------------
    // Construct with source text. filename is used only in error messages.
    // -------------------------------------------------------------------------
    explicit AcmlLexer(const std::string& source,
                       const std::string& filename = "");

    // -------------------------------------------------------------------------
    // tokenize()
    // Runs the full source through the lexer and returns all tokens including
    // a final Eof token. Newlines are emitted as tokens (property separator).
    // Comment lines are discarded. Whitespace (spaces/tabs/CR) is skipped.
    // -------------------------------------------------------------------------
    std::vector<Token> tokenize();

    // -------------------------------------------------------------------------
    // Error reporting
    // -------------------------------------------------------------------------
    bool                            hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors()    const { return errors_; }

private:
    // ── State ────────────────────────────────────────────────────────────────
    std::string              source_;
    std::string              filename_;
    size_t                   pos_  = 0;
    int                      line_ = 1;
    int                      col_  = 1;
    std::vector<std::string> errors_;

    // ── Low-level helpers ────────────────────────────────────────────────────
    bool   atEnd()                  const { return pos_ >= source_.size(); }
    char   peek(size_t offset = 0)  const;
    char   advance();
    bool   matchSeq(const std::string& s); // consume s if it matches at pos_

    // ── Skip ─────────────────────────────────────────────────────────────────
    void   skipSpaces();            // skip \t \r spaces (NOT \n)
    void   skipLineComment();       // consume from // to end of line
    void   skipBlockComment();      // consume /* ... */

    // ── Readers ──────────────────────────────────────────────────────────────
    Token  readNumber();            // [0-9]+ ('.' [0-9]+)?  + optional unit
    Token  readString();            // "..."
    Token  readColorHex();          // #RRGGBB
    Token  readElementType();       // [A-Z][a-zA-Z0-9]*
    Token  readIdentifier();        // [a-z_][a-z0-9_]*

    // Try to read a unit suffix immediately after a number (no whitespace).
    // Returns the unit string ("mm","cm","m","deg","rad","%") or "" if none.
    std::string tryReadUnit();

    // ── Token factories ──────────────────────────────────────────────────────
    Token  make(TokenType type, const std::string& value = "");
    Token  error(const std::string& msg);
};

} // namespace Acml
