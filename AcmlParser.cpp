// AcmlParser.cpp — ACML recursive descent parser implementation
#include "StdAfx.h"
#include "AcmlParser.h"
#include <sstream>
#include <cassert>

namespace Acml
{

// ============================================================================
// Static sentinel
// ============================================================================
const Token AcmlParser::kEof = []() {
    Token t;
    t.type  = TokenType::Eof;
    t.value = "";
    t.line  = 0;
    t.col   = 0;
    return t;
}();

// ============================================================================
// Constructor
// ============================================================================
AcmlParser::AcmlParser(std::vector<Token> tokens, const std::string& filename)
    : tokens_(std::move(tokens)), filename_(filename)
{}

// ============================================================================
// parse — entry point
// ============================================================================
std::unique_ptr<DocumentNode> AcmlParser::parse()
{
    auto doc = std::make_unique<DocumentNode>();
    doc->line = 1; doc->col = 1;

    skipNewlines();
    while (!atEnd())
    {
        auto item = parseTopLevelItem();
        if (item)
            doc->items.push_back(std::move(item));
        skipNewlines();
    }
    return doc;
}

// ============================================================================
// Token navigation
// ============================================================================
const Token& AcmlParser::peek(size_t offset) const
{
    size_t idx = pos_ + offset;
    return (idx < tokens_.size()) ? tokens_[idx] : kEof;
}

const Token& AcmlParser::advance()
{
    const Token& t = peek();
    if (!atEnd()) ++pos_;
    return t;
}

bool AcmlParser::atEnd() const
{
    // Skip past Newlines when checking end — but keep them consumable.
    return pos_ >= tokens_.size() ||
           tokens_[pos_].type == TokenType::Eof;
}

bool AcmlParser::check(TokenType t) const
{
    return current().type == t;
}

bool AcmlParser::checkIdent(const std::string& v) const
{
    return current().type == TokenType::Identifier && current().value == v;
}

bool AcmlParser::checkElemType(const std::string& v) const
{
    return current().type == TokenType::ElementType && current().value == v;
}

bool AcmlParser::match(TokenType t)
{
    if (check(t)) { advance(); return true; }
    return false;
}

bool AcmlParser::matchIdent(const std::string& v)
{
    if (checkIdent(v)) { advance(); return true; }
    return false;
}

bool AcmlParser::expect(TokenType t, const std::string& msg)
{
    if (check(t)) { advance(); return true; }
    addError(current().line, current().col, msg +
             " (got '" + current().value + "')");
    return false;
}

bool AcmlParser::expectIdent(const std::string& v, const std::string& msg)
{
    if (checkIdent(v)) { advance(); return true; }
    addError(current().line, current().col, msg +
             " (got '" + current().value + "')");
    return false;
}

void AcmlParser::skipNewlines()
{
    while (check(TokenType::Newline)) advance();
}

void AcmlParser::skipSeparator()
{
    if (check(TokenType::Semicolon) || check(TokenType::Newline))
        advance();
}

void AcmlParser::syncToBodyItem()
{
    // Skip tokens until we see '}' (end of block) or a potential assignment start.
    while (!atEnd())
    {
        TokenType t = current().type;
        if (t == TokenType::RBrace)  return;
        if (t == TokenType::Newline) { advance(); return; }
        if (t == TokenType::Semicolon) { advance(); return; }
        advance();
    }
}

// ============================================================================
// Top-level items
// ============================================================================
NodePtr AcmlParser::parseTopLevelItem()
{
    skipNewlines();
    const Token& tok = current();

    // import "path" [as alias]
    if (tok.type == TokenType::Identifier && tok.is("import"))
        return parseImportStmt();

    // Component TypeName { ... }
    if (tok.type == TokenType::ElementType && tok.is("Component"))
        return parseComponentDef();

    // ElementType TypeName { ... }
    if (tok.type == TokenType::ElementType && tok.is("ElementType"))
        return parseElementTypeDef();

    // TypeName { ... }  — top-level element (Config, Building, etc.)
    if (tok.type == TokenType::ElementType)
    {
        std::string typeName = tok.value;
        int ln = tok.line, cl = tok.col;
        advance();
        return parseElement(typeName, ln, cl);
    }

    addError(tok.line, tok.col,
             "Expected element type, 'import', 'Component', or 'ElementType'");
    advance();
    return nullptr;
}

// ── import ───────────────────────────────────────────────────────────────────
NodePtr AcmlParser::parseImportStmt()
{
    int ln = current().line, cl = current().col;
    advance(); // 'import'

    if (!check(TokenType::String))
    {
        addError(ln, cl, "Expected file path string after 'import'");
        return nullptr;
    }

    auto node    = make<ImportStmtNode>(ln, cl);
    node->path   = current().value;
    advance(); // string

    if (matchIdent("as"))
    {
        if (!check(TokenType::Identifier))
            addError(current().line, current().col, "Expected alias name after 'as'");
        else
        {
            node->alias = current().value;
            advance();
        }
    }

    skipSeparator();
    return node;
}

// ── Component ─────────────────────────────────────────────────────────────────
NodePtr AcmlParser::parseComponentDef()
{
    int ln = current().line, cl = current().col;
    advance(); // 'Component'

    if (!check(TokenType::ElementType))
    {
        addError(ln, cl, "Expected component name (PascalCase) after 'Component'");
        return nullptr;
    }

    auto node      = make<ComponentDefNode>(ln, cl);
    node->typeName = current().value;
    advance();

    if (!expect(TokenType::LBrace, "Expected '{' to open Component body"))
        return node;

    skipNewlines();
    while (!check(TokenType::RBrace) && !atEnd())
    {
        if (checkIdent("param"))
        {
            auto p = parseParamDecl();
            if (p) node->params.push_back(std::move(p));
        }
        else
        {
            auto item = parseBodyItem();
            if (item) node->body.push_back(std::move(item));
        }
        skipNewlines();
    }

    expect(TokenType::RBrace, "Expected '}' to close Component body");
    return node;
}

// ── ElementType definition ────────────────────────────────────────────────────
NodePtr AcmlParser::parseElementTypeDef()
{
    int ln = current().line, cl = current().col;
    advance(); // 'ElementType'

    if (!check(TokenType::ElementType))
    {
        addError(ln, cl, "Expected type name (PascalCase) after 'ElementType'");
        return nullptr;
    }

    auto node      = make<ElementTypeDefNode>(ln, cl);
    node->typeName = current().value;
    advance();

    if (!expect(TokenType::LBrace, "Expected '{' to open ElementType body"))
        return node;

    skipNewlines();

    // Optional: inherits: ParentType
    if (checkIdent("inherits"))
    {
        advance();
        expect(TokenType::Colon, "Expected ':' after 'inherits'");
        if (check(TokenType::ElementType))
        {
            node->inherits = current().value;
            advance();
        }
        else
            addError(current().line, current().col,
                     "Expected parent type name after 'inherits:'");
        skipSeparator();
        skipNewlines();
    }

    // Optional: properties { assignment* }
    if (checkIdent("properties"))
    {
        advance();
        if (expect(TokenType::LBrace, "Expected '{' after 'properties'"))
        {
            skipNewlines();
            while (!check(TokenType::RBrace) && !atEnd())
            {
                if (check(TokenType::Identifier))
                {
                    std::string name = current().value;
                    int aln = current().line, acl = current().col;
                    advance();
                    auto a = parseAssignment(name, aln, acl);
                    if (a) node->properties.push_back(std::move(a));
                }
                else { syncToBodyItem(); }
                skipNewlines();
            }
            expect(TokenType::RBrace, "Expected '}' to close 'properties' block");
        }
        skipNewlines();
    }

    expect(TokenType::RBrace, "Expected '}' to close ElementType body");
    return node;
}

// ── Element ───────────────────────────────────────────────────────────────────
NodePtr AcmlParser::parseElement(const std::string& typeName, int line, int col)
{
    auto node      = make<ElementNode>(line, col);
    node->typeName = typeName;

    if (!expect(TokenType::LBrace, "Expected '{' after element type '" + typeName + "'"))
        return node;

    parseBody(node->body);

    expect(TokenType::RBrace, "Expected '}' to close element '" + typeName + "'");
    return node;
}

// ── Body ──────────────────────────────────────────────────────────────────────
void AcmlParser::parseBody(NodeList& body)
{
    skipNewlines();
    while (!check(TokenType::RBrace) && !atEnd())
    {
        auto item = parseBodyItem();
        if (item)
            body.push_back(std::move(item));
        skipNewlines();
    }
}

NodePtr AcmlParser::parseBodyItem()
{
    const Token& tok = current();

    // Child element: TypeName { ... }
    if (tok.type == TokenType::ElementType)
    {
        std::string typeName = tok.value;
        int ln = tok.line, cl = tok.col;
        advance();
        return parseElement(typeName, ln, cl);
    }

    if (tok.type == TokenType::Identifier)
    {
        // constraint: expr
        if (tok.is("constraint"))
        {
            int ln = tok.line, cl = tok.col;
            advance();
            return parseConstraintStmt(ln, cl);
        }

        // property assignment: name: expr
        std::string name = tok.value;
        int ln = tok.line, cl = tok.col;
        advance();
        return parseAssignment(name, ln, cl);
    }

    // Unexpected token — recover
    addError(tok.line, tok.col,
             "Expected property name or element type, got '" + tok.value + "'");
    syncToBodyItem();
    return nullptr;
}

NodePtr AcmlParser::parseParamDecl()
{
    int ln = current().line, cl = current().col;
    advance(); // 'param'

    if (!check(TokenType::Identifier))
    {
        addError(ln, cl, "Expected parameter name after 'param'");
        return nullptr;
    }

    auto node  = make<ParamDeclNode>(ln, cl);
    node->name = current().value;
    advance();

    if (!expect(TokenType::Colon, "Expected ':' after param name"))
        return node;

    node->defaultValue = parseExpr();
    skipSeparator();
    return node;
}

// ============================================================================
// Assignment and constraint
// ============================================================================
NodePtr AcmlParser::parseAssignment(const std::string& name, int line, int col)
{
    if (!expect(TokenType::Colon, "Expected ':' after property name '" + name + "'"))
        return nullptr;

    skipNewlines(); // allow value on next line

    auto node  = make<AssignmentNode>(line, col);
    node->name = name;
    node->value = parseExpr();

    skipSeparator();
    return node;
}

NodePtr AcmlParser::parseConstraintStmt(int line, int col)
{
    if (!expect(TokenType::Colon, "Expected ':' after 'constraint'"))
        return nullptr;

    skipNewlines();

    auto node  = make<ConstraintStmtNode>(line, col);
    node->expr = parseConstraintExpr();

    skipSeparator();
    return node;
}

NodePtr AcmlParser::parseConstraintExpr()
{
    int ln = current().line, cl = current().col;

    if (!check(TokenType::Identifier))
    {
        addError(ln, cl, "Expected constraint function name");
        return nullptr;
    }

    auto node      = make<ConstraintExprNode>(ln, cl);
    node->funcName = current().value;
    advance();

    if (!expect(TokenType::LParen,
                "Expected '(' after constraint function '" + node->funcName + "'"))
        return node;

    // Parse argument list — mix of expressions and named args (key: expr)
    skipNewlines();
    while (!check(TokenType::RParen) && !atEnd())
    {
        // Named arg? IDENT ':' expr  (but not if it looks like a reference a.b)
        if (check(TokenType::Identifier) &&
            peek(1).type == TokenType::Colon &&
            peek(2).type != TokenType::Colon) // not ::
        {
            int aln = current().line, acl = current().col;
            auto named    = make<NamedArgNode>(aln, acl);
            named->name   = current().value;
            advance(); // IDENT
            advance(); // :
            named->value  = parseExpr();
            node->args.push_back(std::move(named));
        }
        else
        {
            node->args.push_back(parseExpr());
        }

        skipNewlines();
        if (!match(TokenType::Comma)) break;
        skipNewlines();
    }

    expect(TokenType::RParen, "Expected ')' to close constraint arguments");

    // Optional comparison operator: >= 400
    auto& tok = current();
    if (tok.type == TokenType::GtEq  || tok.type == TokenType::LtEq  ||
        tok.type == TokenType::Gt    || tok.type == TokenType::Lt     ||
        tok.type == TokenType::EqEq  || tok.type == TokenType::NotEq)
    {
        node->compOp  = tok.value;
        advance();
        node->compRhs = parseExpr();
    }

    return node;
}

// ============================================================================
// Expression parsing — precedence climbing
//   ||  (lowest)
//   &&
//   == !=
//   < > <= >=
//   + -
//   * / %
//   unary -
//   primary  (highest)
// ============================================================================
NodePtr AcmlParser::parseExpr()    { return parseOr(); }

NodePtr AcmlParser::parseOr()
{
    auto left = parseAnd();
    while (check(TokenType::PipePipe))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node  = make<BinaryNode>(ln, cl);
        node->op   = op;
        node->left = std::move(left);
        node->right = parseAnd();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseAnd()
{
    auto left = parseEquality();
    while (check(TokenType::AmpAmp))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node   = make<BinaryNode>(ln, cl);
        node->op    = op;
        node->left  = std::move(left);
        node->right = parseEquality();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseEquality()
{
    auto left = parseRelational();
    while (check(TokenType::EqEq) || check(TokenType::NotEq))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node   = make<BinaryNode>(ln, cl);
        node->op    = op;
        node->left  = std::move(left);
        node->right = parseRelational();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseRelational()
{
    auto left = parseAddSub();
    while (check(TokenType::Lt)   || check(TokenType::Gt)  ||
           check(TokenType::LtEq) || check(TokenType::GtEq))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node   = make<BinaryNode>(ln, cl);
        node->op    = op;
        node->left  = std::move(left);
        node->right = parseAddSub();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseAddSub()
{
    auto left = parseMulDiv();
    while (check(TokenType::Plus) || check(TokenType::Minus))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node   = make<BinaryNode>(ln, cl);
        node->op    = op;
        node->left  = std::move(left);
        node->right = parseMulDiv();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseMulDiv()
{
    auto left = parseUnary();
    while (check(TokenType::Star) || check(TokenType::Slash) ||
           check(TokenType::Percent))
    {
        int ln = current().line, cl = current().col;
        std::string op = current().value; advance();
        auto node   = make<BinaryNode>(ln, cl);
        node->op    = op;
        node->left  = std::move(left);
        node->right = parseUnary();
        left = std::move(node);
    }
    return left;
}

NodePtr AcmlParser::parseUnary()
{
    if (check(TokenType::Minus))
    {
        int ln = current().line, cl = current().col;
        advance();
        // Represent unary minus as Binary(0 - operand)
        auto node    = make<BinaryNode>(ln, cl);
        node->op     = "-";
        auto zero    = make<LiteralNode>(ln, cl);
        zero->litKind = LiteralNode::LitKind::Number;
        zero->value   = "0";
        node->left   = std::move(zero);
        node->right  = parseUnary();
        return node;
    }
    return parsePrimary();
}

NodePtr AcmlParser::parsePrimary()
{
    const Token& tok = current();
    int ln = tok.line, cl = tok.col;

    // '(' expr ')'
    if (check(TokenType::LParen))
    {
        advance();
        auto inner = parseExpr();
        expect(TokenType::RParen, "Expected ')' to close expression");
        return inner;
    }

    // '[' list ']'
    if (check(TokenType::LBracket))
        return parseList(ln, cl);

    // '{' object '}'
    if (check(TokenType::LBrace))
        return parseObject(ln, cl);

    // Number literal
    if (check(TokenType::Number))
    {
        auto node      = make<LiteralNode>(ln, cl);
        node->litKind  = LiteralNode::LitKind::Number;
        node->value    = tok.value;
        node->unit     = tok.unit;
        advance();
        return node;
    }

    // String literal
    if (check(TokenType::String))
    {
        auto node     = make<LiteralNode>(ln, cl);
        node->litKind = LiteralNode::LitKind::String;
        node->value   = tok.value;
        advance();
        return node;
    }

    // Color hex #RRGGBB
    if (check(TokenType::ColorHex))
    {
        auto node     = make<LiteralNode>(ln, cl);
        node->litKind = LiteralNode::LitKind::ColorHex;
        node->value   = tok.value;
        advance();
        return node;
    }

    // Identifier or keyword:
    //   true / false  → LiteralNode Bool
    //   if(...)       → ConditionalNode
    //   name(...)     → FunctionCallNode
    //   name.prop...  → ReferenceNode
    //   bare name     → ReferenceNode (single part)
    if (check(TokenType::Identifier))
    {
        std::string name = tok.value;
        advance();

        if (name == "true" || name == "false")
        {
            auto node     = make<LiteralNode>(ln, cl);
            node->litKind = LiteralNode::LitKind::Bool;
            node->value   = name;
            return node;
        }

        if (name == "if")
            return parseConditional(ln, cl);

        // function call or reference
        return parseFunctionCallOrRef(name, ln, cl);
    }

    // Bare enum-like identifier starting with lowercase (context-resolved
    // by semantic analyzer: "center", "sliding", "exterior", etc.)
    // Already covered by Identifier branch above.

    addError(ln, cl, "Unexpected token '" + tok.value + "' in expression");
    advance();
    return nullptr;
}

// ── Reference or function call ────────────────────────────────────────────────
NodePtr AcmlParser::parseFunctionCallOrRef(const std::string& name,
                                            int line, int col)
{
    // Function call: name(...)
    if (check(TokenType::LParen))
    {
        advance(); // '('
        auto node  = make<FunctionCallNode>(line, col);
        node->name = name;

        skipNewlines();
        while (!check(TokenType::RParen) && !atEnd())
        {
            node->args.push_back(parseExpr());
            skipNewlines();
            if (!match(TokenType::Comma)) break;
            skipNewlines();
        }
        expect(TokenType::RParen, "Expected ')' to close function call '" + name + "'");
        return node;
    }

    // Reference chain: name [.part]*
    return parseReference(name, line, col);
}

NodePtr AcmlParser::parseReference(const std::string& firstName,
                                    int line, int col)
{
    auto node = make<ReferenceNode>(line, col);
    node->parts.push_back(firstName);

    while (check(TokenType::Dot))
    {
        advance(); // '.'
        if (check(TokenType::Identifier))
        {
            node->parts.push_back(current().value);
            advance();
        }
        else
        {
            addError(current().line, current().col,
                     "Expected identifier after '.'");
            break;
        }
    }
    return node;
}

// ── List [e, e, ...] ──────────────────────────────────────────────────────────
NodePtr AcmlParser::parseList(int line, int col)
{
    advance(); // '['
    auto node = make<ListNode>(line, col);

    skipNewlines();
    while (!check(TokenType::RBracket) && !atEnd())
    {
        node->items.push_back(parseExpr());
        skipNewlines();
        if (!match(TokenType::Comma)) break;
        skipNewlines();
    }
    expect(TokenType::RBracket, "Expected ']' to close list");
    return node;
}

// ── Object { k: v, ... } ─────────────────────────────────────────────────────
NodePtr AcmlParser::parseObject(int line, int col)
{
    advance(); // '{'
    auto node = make<ObjectNode>(line, col);

    skipNewlines();
    while (!check(TokenType::RBrace) && !atEnd())
    {
        if (!check(TokenType::Identifier))
        {
            addError(current().line, current().col,
                     "Expected key name in object literal");
            syncToBodyItem();
            break;
        }
        std::string key = current().value;
        advance();
        expect(TokenType::Colon, "Expected ':' after key '" + key + "'");
        auto val = parseExpr();
        node->entries.emplace_back(key, std::move(val));
        skipSeparator();
        skipNewlines();
    }
    expect(TokenType::RBrace, "Expected '}' to close object literal");
    return node;
}

// ── if(cond, then, else) ──────────────────────────────────────────────────────
NodePtr AcmlParser::parseConditional(int line, int col)
{
    expect(TokenType::LParen, "Expected '(' after 'if'");
    auto node      = make<ConditionalNode>(line, col);
    node->cond     = parseExpr();
    expect(TokenType::Comma, "Expected ',' after condition in 'if'");
    node->thenExpr = parseExpr();
    expect(TokenType::Comma, "Expected ',' after then-expr in 'if'");
    node->elseExpr = parseExpr();
    expect(TokenType::RParen, "Expected ')' to close 'if'");
    return node;
}

// ============================================================================
// Error helpers
// ============================================================================
NodePtr AcmlParser::errExpr(const std::string& msg)
{
    addError(current().line, current().col, msg);
    return nullptr;
}

void AcmlParser::addError(const std::string& msg)
{
    addError(current().line, current().col, msg);
}

void AcmlParser::addError(int line, int col, const std::string& msg)
{
    std::string full = filename_ + ":" + std::to_string(line) +
                       ":" + std::to_string(col) + ": " + msg;
    errors_.push_back(full);
}

// ============================================================================
// Pretty-printer
// ============================================================================
std::string AcmlParser::printAst(const DocumentNode* doc)
{
    std::string out;
    printNode(doc, 0, out);
    return out;
}

void AcmlParser::printNode(const AstNode* n, int indent, std::string& out)
{
    if (!n) { out += std::string(indent * 2, ' ') + "<null>\n"; return; }

    std::string pad(indent * 2, ' ');

    switch (n->kind)
    {
    case NodeKind::Document:
    {
        out += pad + "Document\n";
        for (const auto& item : as<DocumentNode>(n)->items)
            printNode(item.get(), indent + 1, out);
        break;
    }
    case NodeKind::ImportStmt:
    {
        auto* v = as<ImportStmtNode>(n);
        out += pad + "Import \"" + v->path + "\"";
        if (!v->alias.empty()) out += " as " + v->alias;
        out += "\n";
        break;
    }
    case NodeKind::ComponentDef:
    {
        auto* v = as<ComponentDefNode>(n);
        out += pad + "Component " + v->typeName + "\n";
        for (const auto& p : v->params)   printNode(p.get(), indent + 1, out);
        for (const auto& b : v->body)     printNode(b.get(), indent + 1, out);
        break;
    }
    case NodeKind::ParamDecl:
    {
        auto* v = as<ParamDeclNode>(n);
        out += pad + "param " + v->name + ": ";
        if (v->defaultValue && v->defaultValue->kind == NodeKind::Literal)
        {
            auto* lit = as<LiteralNode>(v->defaultValue.get());
            out += lit->value + lit->unit;
        }
        out += "\n";
        break;
    }
    case NodeKind::ElementTypeDef:
    {
        auto* v = as<ElementTypeDefNode>(n);
        out += pad + "ElementType " + v->typeName;
        if (!v->inherits.empty()) out += " inherits " + v->inherits;
        out += "\n";
        for (const auto& p : v->properties) printNode(p.get(), indent + 1, out);
        break;
    }
    case NodeKind::Element:
    {
        auto* v = as<ElementNode>(n);
        out += pad + v->typeName + "\n";
        for (const auto& item : v->body) printNode(item.get(), indent + 1, out);
        break;
    }
    case NodeKind::Assignment:
    {
        auto* v = as<AssignmentNode>(n);
        out += pad + v->name + ": ";
        // Inline simple literals for readability
        if (v->value && v->value->kind == NodeKind::Literal)
        {
            auto* lit = as<LiteralNode>(v->value.get());
            out += lit->value + lit->unit + "\n";
        }
        else if (v->value && v->value->kind == NodeKind::Reference)
        {
            auto* ref = as<ReferenceNode>(v->value.get());
            for (size_t i = 0; i < ref->parts.size(); ++i)
            {
                if (i) out += ".";
                out += ref->parts[i];
            }
            out += "\n";
        }
        else
        {
            out += "<expr>\n";
            if (v->value) printNode(v->value.get(), indent + 1, out);
        }
        break;
    }
    case NodeKind::ConstraintStmt:
    {
        out += pad + "constraint:\n";
        printNode(as<ConstraintStmtNode>(n)->expr.get(), indent + 1, out);
        break;
    }
    case NodeKind::ConstraintExpr:
    {
        auto* v = as<ConstraintExprNode>(n);
        out += pad + v->funcName + "(";
        out += std::to_string(v->args.size()) + " args)";
        if (!v->compOp.empty()) out += " " + v->compOp + " rhs";
        out += "\n";
        break;
    }
    case NodeKind::Binary:
    {
        auto* v = as<BinaryNode>(n);
        out += pad + "(" + v->op + ")\n";
        printNode(v->left.get(),  indent + 1, out);
        printNode(v->right.get(), indent + 1, out);
        break;
    }
    case NodeKind::FunctionCall:
    {
        auto* v = as<FunctionCallNode>(n);
        out += pad + v->name + "(" + std::to_string(v->args.size()) + " args)\n";
        break;
    }
    case NodeKind::Reference:
    {
        auto* v = as<ReferenceNode>(n);
        out += pad;
        for (size_t i = 0; i < v->parts.size(); ++i)
        {
            if (i) out += ".";
            out += v->parts[i];
        }
        out += "\n";
        break;
    }
    case NodeKind::Literal:
    {
        auto* v = as<LiteralNode>(n);
        out += pad + v->value + v->unit + "\n";
        break;
    }
    default:
        out += pad + "<node:" + std::to_string((int)n->kind) + ">\n";
        break;
    }
}

} // namespace Acml
