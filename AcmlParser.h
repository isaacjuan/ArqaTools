// AcmlParser.h — ACML recursive descent parser
//
// Consumes a token stream produced by AcmlLexer and builds an AST
// rooted at DocumentNode. No ObjectARX dependency.
//
// Usage:
//   auto tokens = AcmlLexer(source, filename).tokenize();
//   AcmlParser parser(std::move(tokens), filename);
//   auto doc    = parser.parse();          // unique_ptr<DocumentNode>
//   if (parser.hasErrors()) { /* parser.errors() */ }

#pragma once
#include "AcmlAst.h"
#include "AcmlLexer.h"
#include <vector>
#include <string>
#include <memory>

namespace Acml
{

class AcmlParser
{
public:
    // -------------------------------------------------------------------------
    // Construct with the full token list. filename used in error messages only.
    // -------------------------------------------------------------------------
    explicit AcmlParser(std::vector<Token>  tokens,
                        const std::string&  filename = "");

    // -------------------------------------------------------------------------
    // parse() — entry point. Always returns a non-null DocumentNode; the tree
    // may be incomplete when errors occurred (best-effort recovery).
    // -------------------------------------------------------------------------
    std::unique_ptr<DocumentNode> parse();

    // -------------------------------------------------------------------------
    // Error reporting
    // -------------------------------------------------------------------------
    bool                            hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors()    const { return errors_; }

    // -------------------------------------------------------------------------
    // printAst — write indented tree representation to a string for diagnostics.
    // -------------------------------------------------------------------------
    static std::string printAst(const DocumentNode* doc);

private:
    // ── State ────────────────────────────────────────────────────────────────
    std::vector<Token>       tokens_;
    std::string              filename_;
    size_t                   pos_ = 0;
    std::vector<std::string> errors_;

    // A dummy EOF token returned when pos_ is past the end.
    static const Token kEof;

    // ── Token navigation ─────────────────────────────────────────────────────
    const Token& peek(size_t offset = 0) const;
    const Token& current()               const { return peek(0); }
    const Token& advance();
    bool         atEnd()                 const;

    bool check(TokenType t)                        const;
    bool checkIdent(const std::string& v)          const;
    bool checkElemType(const std::string& v)       const;

    bool match(TokenType t);
    bool matchIdent(const std::string& v);

    bool        expect(TokenType t,           const std::string& msg);
    bool        expectIdent(const std::string& v, const std::string& msg);

    void        skipNewlines();          // consume all leading Newline tokens
    void        skipSeparator();         // consume one ';' or '\n' if present
    void        syncToBodyItem();        // error recovery: skip to next '}' or IDENT ':'

    // ── Top-level parsers ─────────────────────────────────────────────────────
    NodePtr parseTopLevelItem();
    NodePtr parseImportStmt();
    NodePtr parseComponentDef();
    NodePtr parseElementTypeDef();
    NodePtr parseElement(const std::string& typeName, int line, int col);

    // ── Body ─────────────────────────────────────────────────────────────────
    // Parses items until '}' and fills body.
    void    parseBody(NodeList& body);
    NodePtr parseBodyItem();
    NodePtr parseParamDecl();

    // ── Assignments and constraints ───────────────────────────────────────────
    NodePtr parseAssignment(const std::string& name, int line, int col);
    NodePtr parseConstraintStmt(int line, int col);
    NodePtr parseConstraintExpr();

    // ── Expression parsing (precedence climbing) ──────────────────────────────
    NodePtr parseExpr();
    NodePtr parseOr();
    NodePtr parseAnd();
    NodePtr parseEquality();
    NodePtr parseRelational();
    NodePtr parseAddSub();
    NodePtr parseMulDiv();
    NodePtr parseUnary();
    NodePtr parsePrimary();

    NodePtr parseReference(const std::string& firstName, int line, int col);
    NodePtr parseFunctionCallOrRef(const std::string& name, int line, int col);
    NodePtr parseList(int line, int col);
    NodePtr parseObject(int line, int col);
    NodePtr parseConditional(int line, int col);

    // ── AST factory helpers ───────────────────────────────────────────────────
    template<typename T, typename... Args>
    std::unique_ptr<T> make(int line, int col, Args&&... args)
    {
        auto n = std::make_unique<T>(std::forward<Args>(args)...);
        n->line = line;
        n->col  = col;
        return n;
    }

    // ── Error helpers ─────────────────────────────────────────────────────────
    NodePtr errExpr(const std::string& msg);   // record error, return nullptr
    void    addError(const std::string& msg);
    void    addError(int line, int col, const std::string& msg);

    // ── Pretty-printer (recursive helper) ────────────────────────────────────
    static void printNode(const AstNode* n, int indent, std::string& out);
};

} // namespace Acml
