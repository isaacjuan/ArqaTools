// AcmlAst.h — ACML Abstract Syntax Tree node definitions
//
// All AST nodes inherit from AstNode and carry a NodeKind tag so callers
// can switch on kind without dynamic_cast. Ownership is expressed through
// NodePtr (unique_ptr<AstNode>); parent nodes own their children.
//
// Node hierarchy:
//   Top-level  : DocumentNode, ImportStmtNode, ComponentDefNode,
//                ElementTypeDefNode, ElementNode
//   Body items : AssignmentNode, ConstraintStmtNode
//   Expressions: LiteralNode, ReferenceNode, BinaryNode, FunctionCallNode,
//                ConditionalNode, ListNode, ObjectNode
//   Constraint : ConstraintExprNode, NamedArgNode

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace Acml
{

// ============================================================================
// Forward declarations + convenience alias
// ============================================================================
struct AstNode;
using NodePtr  = std::unique_ptr<AstNode>;
using NodeList = std::vector<NodePtr>;

// ============================================================================
// NodeKind — discriminator for all AST node types
// ============================================================================
enum class NodeKind
{
    // Top-level
    Document,
    ImportStmt,
    ComponentDef,
    ParamDecl,
    ElementTypeDef,
    Element,

    // Body items
    Assignment,
    ConstraintStmt,

    // Constraint expression
    ConstraintExpr,
    NamedArg,

    // Expressions
    Literal,
    Reference,
    Binary,
    FunctionCall,
    Conditional,
    List,
    Object,
};

// ============================================================================
// Base node
// ============================================================================
struct AstNode
{
    NodeKind kind;
    int      line = 0;
    int      col  = 0;

    explicit AstNode(NodeKind k) : kind(k) {}
    virtual ~AstNode() = default;

    // Non-copyable (unique_ptr semantics propagate to owners).
    AstNode(const AstNode&)            = delete;
    AstNode& operator=(const AstNode&) = delete;
};

// ============================================================================
// Helper: safe downcast (debug-asserts kind matches)
// ============================================================================
template<typename T>
T* as(AstNode* n) { return static_cast<T*>(n); }

template<typename T>
const T* as(const AstNode* n) { return static_cast<const T*>(n); }

// ============================================================================
// DocumentNode — root of the parsed tree
// ============================================================================
struct DocumentNode : AstNode
{
    // items is a mix of ImportStmtNode, ComponentDefNode,
    // ElementTypeDefNode, and ElementNode.
    NodeList items;

    DocumentNode() : AstNode(NodeKind::Document) {}
};

// ============================================================================
// ImportStmtNode — import "path/file.acml" [as ALIAS]
// ============================================================================
struct ImportStmtNode : AstNode
{
    std::string path;   // string literal value (without quotes)
    std::string alias;  // empty if no 'as' clause

    ImportStmtNode() : AstNode(NodeKind::ImportStmt) {}
};

// ============================================================================
// ParamDeclNode — param name: defaultValue
// ============================================================================
struct ParamDeclNode : AstNode
{
    std::string name;
    NodePtr     defaultValue;  // always a LiteralNode per spec

    ParamDeclNode() : AstNode(NodeKind::ParamDecl) {}
};

// ============================================================================
// ComponentDefNode — Component TypeName { param* body }
// ============================================================================
struct ComponentDefNode : AstNode
{
    std::string typeName;
    NodeList    params;   // ParamDeclNode list
    NodeList    body;     // Assignment | Element | ConstraintStmt

    ComponentDefNode() : AstNode(NodeKind::ComponentDef) {}
};

// ============================================================================
// ElementTypeDefNode — ElementType TypeName { [inherits: T] properties { } }
// ============================================================================
struct ElementTypeDefNode : AstNode
{
    std::string typeName;
    std::string inherits;    // empty if no 'inherits' clause
    NodeList    properties;  // AssignmentNode list (default property values)

    ElementTypeDefNode() : AstNode(NodeKind::ElementTypeDef) {}
};

// ============================================================================
// ElementNode — TypeName { body }
// (Covers both built-in types and Component instantiations — the semantic
//  analyzer distinguishes them via the ID registry / component registry.)
// ============================================================================
struct ElementNode : AstNode
{
    std::string typeName;
    NodeList    body;   // Assignment | Element | ConstraintStmt (interleaved)

    ElementNode() : AstNode(NodeKind::Element) {}
};

// ============================================================================
// AssignmentNode — name: expression
// ============================================================================
struct AssignmentNode : AstNode
{
    std::string name;
    NodePtr     value;

    AssignmentNode() : AstNode(NodeKind::Assignment) {}
};

// ============================================================================
// ConstraintStmtNode — constraint: constraintExpr
// ============================================================================
struct ConstraintStmtNode : AstNode
{
    NodePtr expr;   // ConstraintExprNode

    ConstraintStmtNode() : AstNode(NodeKind::ConstraintStmt) {}
};

// ============================================================================
// NamedArgNode — key: expr  (inside constraint arg list)
// ============================================================================
struct NamedArgNode : AstNode
{
    std::string name;
    NodePtr     value;

    NamedArgNode() : AstNode(NodeKind::NamedArg) {}
};

// ============================================================================
// ConstraintExprNode — funcName(args...) [op rhs]?
// e.g.  coincident(wall_a.end, wall_b.start)
//       distance(d1.right, v1.left) >= 400
// ============================================================================
struct ConstraintExprNode : AstNode
{
    std::string funcName;
    NodeList    args;      // mix of expression nodes and NamedArgNode
    std::string compOp;   // "" | ">=" | "<=" | ">" | "<" | "==" | "!="
    NodePtr     compRhs;  // nullptr when compOp is ""

    ConstraintExprNode() : AstNode(NodeKind::ConstraintExpr) {}
};

// ============================================================================
// LiteralNode — number, string, bool, color, enum
// ============================================================================
struct LiteralNode : AstNode
{
    enum class LitKind { Number, String, Bool, ColorHex, Enum };

    LitKind     litKind;
    std::string value;   // raw text: "1200", "brick", "true", "#FF0000"
    std::string unit;    // for Number: "mm", "cm", "m", "deg", "rad", "%", ""

    LiteralNode() : AstNode(NodeKind::Literal), litKind(LitKind::Number) {}
};

// ============================================================================
// ReferenceNode — a.b.c  (dotted identifier chain)
// Covers: self, parent, root, previous, next, and user IDs.
// ============================================================================
struct ReferenceNode : AstNode
{
    std::vector<std::string> parts;  // ["wall_a", "end"] for wall_a.end

    ReferenceNode() : AstNode(NodeKind::Reference) {}
};

// ============================================================================
// BinaryNode — left op right
// ============================================================================
struct BinaryNode : AstNode
{
    std::string op;    // "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||"
    NodePtr     left;
    NodePtr     right;

    BinaryNode() : AstNode(NodeKind::Binary) {}
};

// ============================================================================
// FunctionCallNode — name(arg1, arg2, ...)
// Covers built-in functions (sin, sqrt, distance, …) and position values
// (left(200), right(600), at(1500)).
// ============================================================================
struct FunctionCallNode : AstNode
{
    std::string name;
    NodeList    args;  // expression nodes

    FunctionCallNode() : AstNode(NodeKind::FunctionCall) {}
};

// ============================================================================
// ConditionalNode — if(condition, thenExpr, elseExpr)
// ============================================================================
struct ConditionalNode : AstNode
{
    NodePtr cond;
    NodePtr thenExpr;
    NodePtr elseExpr;

    ConditionalNode() : AstNode(NodeKind::Conditional) {}
};

// ============================================================================
// ListNode — [expr, expr, ...]
// ============================================================================
struct ListNode : AstNode
{
    NodeList items;

    ListNode() : AstNode(NodeKind::List) {}
};

// ============================================================================
// ObjectNode — { key: expr, key: expr, ... }
// Used for metadata and other map-valued properties.
// ============================================================================
struct ObjectNode : AstNode
{
    std::vector<std::pair<std::string, NodePtr>> entries;

    ObjectNode() : AstNode(NodeKind::Object) {}
};

} // namespace Acml
