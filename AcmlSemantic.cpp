// AcmlSemantic.cpp — ACML semantic analyzer implementation
#include "StdAfx.h"
#include "AcmlSemantic.h"
#include "AcmlLexer.h"
#include "AcmlParser.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Acml
{

// ============================================================================
// RAII scope guard for push/pop stacks (paramStack_, modelDataStack_).
// Guarantees pop even if an exception escapes the expansion functions.
// ============================================================================
namespace {
template<typename Stack>
struct StackGuard
{
    Stack& stack;
    explicit StackGuard(Stack& s, typename Stack::value_type item)
        : stack(s) { stack.push_back(std::move(item)); }
    ~StackGuard() { stack.pop_back(); }
    StackGuard(const StackGuard&)            = delete;
    StackGuard& operator=(const StackGuard&) = delete;
};
} // anonymous namespace

// ============================================================================
// Constructor
// ============================================================================
AcmlSemantic::AcmlSemantic(const std::string& filename, PickProvider pickProvider)
    : filename_(filename)
    , pickProvider_(std::move(pickProvider))
{}

// ============================================================================
// analyze — entry point
// ============================================================================
std::vector<ResolvedElement> AcmlSemantic::analyze(const DocumentNode* doc)
{
    if (!doc) return {};

    // Reset all per-analysis state so analyze() can be called multiple times
    // on different documents without previous runs leaking symbols or cache.
    idRegistry_.clear();
    cache_.clear();
    resolving_.clear();
    componentRegistry_.clear();
    typeRegistry_.clear();
    importedDocs_.clear();
    importedPaths_.clear();
    pickCache_.clear();
    pickCancelled_ = false;
    paramStack_.clear();
    modelDataStack_.clear();
    errors_.clear();

    // Seed import cycle detection with the main file's own path
    if (!filename_.empty())
        importedPaths_.insert(filename_);
    buildIdRegistry(doc);
    buildComponentRegistry(doc);
    buildTypeDefRegistry(doc);
    return resolveTopLevel(doc);
}

// ============================================================================
// Pass 1 — build ID registry
// Walks only ElementNodes and registers any id: assignment found.
// ============================================================================
void AcmlSemantic::buildIdRegistry(const DocumentNode* doc)
{
    std::function<void(const ElementNode*)> walk = [&](const ElementNode* elem)
    {
        // Find id property in this element's body
        for (const auto& item : elem->body)
        {
            if (item->kind != NodeKind::Assignment) continue;
            auto* a = as<AssignmentNode>(item.get());
            if (a->name != "id") continue;
            if (!a->value) continue;

            // id values are bare identifiers (ReferenceNode) or string literals
            std::string idVal;
            if (a->value->kind == NodeKind::Literal)
                idVal = as<LiteralNode>(a->value.get())->value;
            else if (a->value->kind == NodeKind::Reference)
            {
                auto* ref = as<ReferenceNode>(a->value.get());
                if (ref->parts.size() == 1)
                    idVal = ref->parts[0];
            }
            if (idVal.empty()) continue;
            if (idRegistry_.count(idVal))
                addError(a->line, a->col, "Duplicate id '" + idVal + "'");
            else
                idRegistry_[idVal] = elem;
        }

        // Recurse into children
        for (const auto& item : elem->body)
            if (item->kind == NodeKind::Element)
                walk(as<ElementNode>(item.get()));
    };

    for (const auto& item : doc->items)
        if (item->kind == NodeKind::Element)
            walk(as<ElementNode>(item.get()));
}

// ============================================================================
// buildComponentRegistry — collect Component definitions into registry
// ============================================================================
void AcmlSemantic::buildComponentRegistry(const DocumentNode* doc)
{
    for (const auto& item : doc->items)
    {
        if (item->kind != NodeKind::ComponentDef) continue;
        auto* comp = as<ComponentDefNode>(item.get());
        if (componentRegistry_.count(comp->typeName))
            addError(comp->line, comp->col,
                     "Duplicate component '" + comp->typeName + "'");
        else
            componentRegistry_[comp->typeName] = { comp };
    }
}

// ============================================================================
// buildTypeDefRegistry — collect ElementType definitions into registry
// ============================================================================
void AcmlSemantic::buildTypeDefRegistry(const DocumentNode* doc)
{
    for (const auto& item : doc->items)
    {
        if (item->kind != NodeKind::ElementTypeDef) continue;
        auto* td = as<ElementTypeDefNode>(item.get());
        if (typeRegistry_.count(td->typeName))
            addError(td->line, td->col,
                     "Duplicate ElementType '" + td->typeName + "'");
        else
            typeRegistry_[td->typeName] = { td, td->inherits };
    }
}

// ============================================================================
// Pass 2 — resolve top-level elements
// ============================================================================
std::vector<ResolvedElement> AcmlSemantic::resolveTopLevel(const DocumentNode* doc)
{
    std::vector<ResolvedElement> result;

    // Single pass: process imports first (they register types/components),
    // then collect element nodes for resolution below.
    std::vector<const ElementNode*> elems;
    for (const auto& item : doc->items)
    {
        if (item->kind == NodeKind::ImportStmt)
        {
            auto imported = processImport(as<ImportStmtNode>(item.get()));
            result.insert(result.end(),
                          std::make_move_iterator(imported.begin()),
                          std::make_move_iterator(imported.end()));
        }
        else if (item->kind == NodeKind::Element)
        {
            elems.push_back(as<ElementNode>(item.get()));
        }
    }

    for (size_t i = 0; i < elems.size(); ++i)
    {
        Ctx ctx;
        ctx.self     = elems[i];
        ctx.root     = doc;
        ctx.previous = (i > 0)                  ? elems[i - 1] : nullptr;
        ctx.next     = (i + 1 < elems.size())   ? elems[i + 1] : nullptr;

        if (elems[i]->typeName == "Repeater")
        {
            // Top-level Repeater: expand into N instances
            auto expanded = expandRepeater(elems[i], ctx, 0);
            for (auto& inst : expanded)
                result.push_back(std::move(inst));
        }
        else if (elems[i]->typeName != "Model")  // Model is data-only
        {
            result.push_back(resolveElement(elems[i], ctx));
        }
    }
    return result;
}

// ============================================================================
// resolveElement — resolve one element and all its children
// ============================================================================
ResolvedElement AcmlSemantic::resolveElement(const ElementNode* elem,
                                              const Ctx&         ctx,
                                              int                depth)
{
    if (depth > 64)
    {
        addError(elem->line, elem->col,
                 "Element nesting too deep (> 64) — possible infinite recursion.");
        return {};
    }

    // If this typeName is a user-defined Component, expand it
    auto compIt = componentRegistry_.find(elem->typeName);
    if (compIt != componentRegistry_.end())
        return expandComponent(elem, compIt->second, ctx, depth);

    ResolvedElement re;
    re.typeName = elem->typeName;
    re.posX     = ctx.posX;
    re.posY     = ctx.posY;
    re.posZ     = ctx.posZ;
    // Layer priority (§4.2): inherited from nearest ancestor  >  AIA type default  >  ""
    // "" is left for the generator to resolve as "0" so it never blocks the chain.
    re.layer = !ctx.inheritedLayer.empty()
               ? ctx.inheritedLayer
               : defaultLayer(elem->typeName);

    // Evaluate all assignment properties in order
    re.props = resolveProps(elem, depth);   // returns by value

    // ── % unit post-processing ────────────────────────────────────────────────
    // Properties with unit=="%" are resolved relative to the parent's size.
    // Must run before position computation so position_x/y can use %.
    if (ctx.parentProps)
    {
        auto resolvePct = [&](const char* key, const char* parentSizeKey)
        {
            auto it = re.props.find(key);
            if (it == re.props.end()) return;
            if (!it->second.isNumber() || it->second.unit != Value::Unit::Pct) return;
            double parentSize = 0.0;
            auto pit = ctx.parentProps->find(parentSizeKey);
            if (pit != ctx.parentProps->end())
                parentSize = pit->second.toMm();
            it->second = Value::num(it->second.number * parentSize / 100.0);
        };
        resolvePct("position_x", "width");
        resolvePct("position_y", "height");
        resolvePct("width",      "width");
        resolvePct("height",     "height");
        resolvePct("radius",     "width");   // e.g. Circle { radius: 50% }
    }

    // Extract id, layer, and visual overrides from resolved props.
    // Use find() for each key to avoid the double-lookup of count()+[].
    {
        auto it = re.props.find("id");
        if (it != re.props.end() && it->second.isString())
            re.id = it->second.text;
    }
    {
        auto it = re.props.find("layer");
        if (it != re.props.end() && it->second.isString())
            re.layer = it->second.text;
    }
    // ── Color / linetype / lineweight — own value only (v6: not inherited) ────
    {
        auto it = re.props.find("color");
        if (it != re.props.end() && it->second.isString())
            re.color = it->second.text;
    }
    {
        auto it = re.props.find("linetype");
        if (it != re.props.end() && it->second.isString())
            re.linetype = it->second.text;
    }
    {
        auto it = re.props.find("lineweight");
        if (it != re.props.end() && !it->second.isUndefined())
            re.lineweight = it->second.toMm();
    }

    // Propagate inherited visibility: if an ancestor is invisible, force this
    // element invisible regardless of its own visible property.
    if (!ctx.inheritedVisible)
        re.props["visible"] = Value::boolean(false);

    // ── Absolute position: apply parent accumulated rotation + scale to local offset
    {
        // When `pick` was used for a position property it returns a Vec3 with
        // absolute world-space coordinates (from acedGetPoint).  In that case
        // the value bypasses the parent transform — the user clicked a world
        // position directly.  Consecutive picks on the same element are grouped
        // (same cached Vec3), so position_x and position_y share one click.
        auto itPX = re.props.find("position_x");
        auto itPY = re.props.find("position_y");
        auto itPZ = re.props.find("position_z");
        bool pickX = (itPX != re.props.end()) && itPX->second.isVec();
        bool pickY = (itPY != re.props.end()) && itPY->second.isVec();
        bool pickZ = (itPZ != re.props.end()) && itPZ->second.isVec();

        if (pickX || pickY || pickZ)
        {
            // Reference Vec3 — prefer position_x's vec, fall back to position_y's.
            const Value* pv = pickX ? &itPX->second
                                    : (pickY ? &itPY->second : &itPZ->second);
            re.posX = pickX ? itPX->second.vx() : pv->vx();
            re.posY = pickY ? itPY->second.vy() : pv->vy();
            re.posZ = pickZ ? itPZ->second.vz() : pv->vz();
        }
        else
        {
            double lx = (itPX != re.props.end()) ? itPX->second.toMm() : 0.0;
            double ly = (itPY != re.props.end()) ? itPY->second.toMm() : 0.0;
            lx *= ctx.inheritedScaleX;
            ly *= ctx.inheritedScaleY;
            if (ctx.inheritedRotZ != 0.0)
            {
                double c = std::cos(ctx.inheritedRotZ), s = std::sin(ctx.inheritedRotZ);
                double rx = lx * c - ly * s, ry = lx * s + ly * c;
                lx = rx; ly = ry;
            }
            re.posX = ctx.posX + lx;
            re.posY = ctx.posY + ly;
            re.posZ = ctx.posZ + ((itPZ != re.props.end()) ? itPZ->second.toMm() : 0.0);
        }
    }

    // ── Own rotation (rotation is alias for rotation_z) and scale ────────────
    double ownRotZ = 0.0;
    {
        auto it = re.props.find("rotation");
        if (it == re.props.end()) it = re.props.find("rotation_z");
        if (it != re.props.end()) ownRotZ = it->second.toRad();
    }
    double ownScaleX = (re.props.find("scale_x") != re.props.end())
                           ? re.getNum("scale_x", 1.0) : re.getNum("scale", 1.0);
    double ownScaleY = (re.props.find("scale_y") != re.props.end())
                           ? re.getNum("scale_y", 1.0) : re.getNum("scale", 1.0);

    re.inheritedRotZ = ctx.inheritedRotZ;          // generator applies this
    re.worldScaleX   = ctx.inheritedScaleX * ownScaleX;
    re.worldScaleY   = ctx.inheritedScaleY * ownScaleY;

    // Accumulated transforms propagated to children of this element
    const double childRotZ   = ctx.inheritedRotZ + ownRotZ;
    const double childScaleX = re.worldScaleX;
    const double childScaleY = re.worldScaleY;

    // ── Computed spatial props (bounds.*, center.*, anchor.*) ────────────────
    // Computed after position is final.  For Row/Column, bounds.width/height are
    // overwritten after resolveLayoutChildren runs (see below), so we keep the
    // update in a lambda called at every return point.
    //
    // Keys are stored flat with dots ("bounds.x", "center.y", …) so that
    // three-part evalReference chains (id.bounds.x) can find them with a single
    // map lookup of "bounds.x".
    auto storeComputedProps = [&]()
    {
        double w = re.getNum("bounds.width",  re.getNum("width",  0.0));
        double h = re.getNum("bounds.height", re.getNum("height", 0.0));

        re.props["bounds.x"]      = Value::num(re.posX);
        re.props["bounds.y"]      = Value::num(re.posY);
        re.props["bounds.width"]  = Value::num(w);
        re.props["bounds.height"] = Value::num(h);
        re.props["center.x"]      = Value::num(re.posX + w * 0.5);
        re.props["center.y"]      = Value::num(re.posY + h * 0.5);
        re.props["anchor.left"]   = Value::num(re.posX);
        re.props["anchor.right"]  = Value::num(re.posX + w);
        re.props["anchor.bottom"] = Value::num(re.posY);
        re.props["anchor.top"]    = Value::num(re.posY + h);

        // Merge into the property cache so that subsequent cross-element
        // evalReference calls (e.g. "room1.bounds.width") can find these.
        if (paramStack_.empty())
        {
            static const char* kComputed[] = {
                "bounds.x", "bounds.y", "bounds.width", "bounds.height",
                "center.x", "center.y",
                "anchor.left", "anchor.right", "anchor.bottom", "anchor.top"
            };
            auto& cached = cache_[elem];
            for (auto* k : kComputed)
                cached[k] = re.props.at(k);
        }
    };

    // ── Layout containers: Row and Column ────────────────────────────────────
    // A "Column" with a 'profile' property is a structural element, not a
    // layout container. All other Column / all Row instances are layout containers.
    bool isLayout = (re.typeName == "Row") ||
                    (re.typeName == "Column" && !re.props.count("profile"));

    if (isLayout)
    {
        resolveLayoutChildren(elem, re, ctx, depth, childRotZ, childScaleX, childScaleY);
        storeComputedProps();   // bounds.width/height now set by resolveLayoutChildren
        return re;
    }

    // ── Normal child resolution (Repeaters expanded inline) ──────────────────
    std::vector<const ElementNode*> childElems;
    for (const auto& item : elem->body)
        if (item->kind == NodeKind::Element)
            childElems.push_back(as<ElementNode>(item.get()));

    for (size_t i = 0; i < childElems.size(); ++i)
    {
        Ctx childCtx;
        childCtx.self             = childElems[i];
        childCtx.parent           = elem;
        childCtx.root             = ctx.root;
        childCtx.previous         = (i > 0)                     ? childElems[i - 1] : nullptr;
        childCtx.next             = (i + 1 < childElems.size()) ? childElems[i + 1] : nullptr;
        childCtx.posX                = re.posX;
        childCtx.posY                = re.posY;
        childCtx.posZ                = re.posZ;
        childCtx.inheritedLayer      = re.layer;
        childCtx.inheritedVisible    = ctx.inheritedVisible && re.getBool("visible", true);
        childCtx.inheritedRotZ       = childRotZ;
        childCtx.inheritedScaleX     = childScaleX;
        childCtx.inheritedScaleY     = childScaleY;
        childCtx.parentProps         = &re.props;

        if (childElems[i]->typeName == "Repeater")
        {
            // Expand Repeater: each instance is added as a sibling child
            auto expanded = expandRepeater(childElems[i], childCtx, depth + 1);
            for (auto& inst : expanded)
                re.children.push_back(std::move(inst));
        }
        else if (childElems[i]->typeName != "Model")  // Model is data-only
        {
            re.children.push_back(resolveElement(childElems[i], childCtx, depth + 1));
        }
    }

    storeComputedProps();
    return re;
}

// ============================================================================
// resolveProps — resolve all assignment properties of an element.
// Returns by value. Uses the cache (keyed by element pointer) only when no
// component param context is active (paramStack_ is empty), because the same
// component body node can be instantiated with different param bindings.
// ============================================================================
std::map<std::string, Value>
AcmlSemantic::resolveProps(const ElementNode* elem, int depth)
{
    bool useCache = paramStack_.empty();

    if (useCache)
    {
        auto it = cache_.find(elem);
        if (it != cache_.end()) return it->second;
    }

    if (resolving_.count(elem))
    {
        addError(elem->line, elem->col,
                 "Circular reference in element '" + elem->typeName + "'");
        return {};
    }

    resolving_.insert(elem);

    std::map<std::string, Value> props;
    Ctx ctx;
    ctx.self       = elem;
    ctx.localProps = &props;   // live pointer — grows as each property is resolved

    for (const auto& item : elem->body)
    {
        if (item->kind != NodeKind::Assignment) continue;
        auto* a = as<AssignmentNode>(item.get());
        if (!a->value) continue;

        Value v = evalExpr(a->value.get(), ctx, depth + 1);

        // Special case: id value is a bare identifier (enum), treat as string
        if (a->name == "id" && !v.isUndefined())
        {
            if (v.kind == Value::Kind::String)
                props["id"] = v;
            else if (a->value->kind == NodeKind::Reference)
            {
                auto* ref = as<ReferenceNode>(a->value.get());
                if (ref->parts.size() == 1)
                    props["id"] = Value::str(ref->parts[0]);
                else
                    props["id"] = v;
            }
        }
        else
        {
            props[a->name] = v;
        }
    }

    // Apply ElementTypeDef defaults for any property not set by the instance.
    // Walk the inheritance chain (child → parent).
    // visited is a small vector — chains are typically 1-3 levels deep,
    // making std::vector with linear search faster than std::set.
    {
        std::string typeName = elem->typeName;
        std::vector<std::string> visited;
        while (!typeName.empty())
        {
            if (std::find(visited.begin(), visited.end(), typeName) != visited.end()) break;
            visited.push_back(typeName);
            auto typeIt = typeRegistry_.find(typeName);
            if (typeIt == typeRegistry_.end()) break;

            const auto* td = typeIt->second.node;
            for (const auto& propItem : td->properties)
            {
                if (propItem->kind != NodeKind::Assignment) continue;
                auto* a = as<AssignmentNode>(propItem.get());
                // Use find() to avoid the double lookup of count() + operator[]
                if (a->value && props.find(a->name) == props.end())
                    props[a->name] = evalExpr(a->value.get(), ctx, depth + 1);
            }
            typeName = typeIt->second.inherits;
        }
    }

    resolving_.erase(elem);

    if (useCache)
        cache_[elem] = props;

    return props;
}

// ============================================================================
// getCachedProps — return a stable pointer to the resolved props for elem.
// Resolves and caches if not already done. Returns nullptr when paramStack_
// is non-empty (component context) since those results are not cacheable.
// ============================================================================
const std::map<std::string, Value>*
AcmlSemantic::getCachedProps(const ElementNode* elem, int depth)
{
    if (!paramStack_.empty()) return nullptr;

    auto it = cache_.find(elem);
    if (it != cache_.end()) return &it->second;

    // Not cached yet — resolve (which will populate the cache).
    resolveProps(elem, depth);

    it = cache_.find(elem);
    return (it != cache_.end()) ? &it->second : nullptr;
}

// ============================================================================
// evalExpr — evaluate any expression node to a Value
// ============================================================================
Value AcmlSemantic::evalExpr(const AstNode* expr, const Ctx& ctx, int depth)
{
    if (!expr || depth > 32) return Value::undefined();

    switch (expr->kind)
    {
    // ── Literals ─────────────────────────────────────────────────────────────
    case NodeKind::Literal:
    {
        auto* n = as<LiteralNode>(expr);
        switch (n->litKind)
        {
        case LiteralNode::LitKind::Number:
            return Value::num(std::stod(n->value), n->unit);
        case LiteralNode::LitKind::String:
            return Value::str(n->value);
        case LiteralNode::LitKind::Bool:
            return Value::boolean(n->value == "true");
        case LiteralNode::LitKind::ColorHex:
            return Value::color(n->value);
        case LiteralNode::LitKind::Enum:
            return Value::str(n->value);
        }
        break;
    }

    // ── References ───────────────────────────────────────────────────────────
    case NodeKind::Reference:
        return evalReference(as<ReferenceNode>(expr), ctx, depth);

    // ── Binary operations ─────────────────────────────────────────────────────
    case NodeKind::Binary:
        return evalBinary(as<BinaryNode>(expr), ctx, depth);

    // ── Function calls ────────────────────────────────────────────────────────
    case NodeKind::FunctionCall:
        return evalFuncCall(as<FunctionCallNode>(expr), ctx, depth);

    // ── Lists [e, e, ...] → Vec ───────────────────────────────────────────────
    case NodeKind::List:
        return evalList(as<ListNode>(expr), ctx, depth);

    // ── Conditional if(cond, t, e) ────────────────────────────────────────────
    case NodeKind::Conditional:
    {
        auto* n = as<ConditionalNode>(expr);
        Value cond = evalExpr(n->cond.get(), ctx, depth + 1);
        if (cond.asBool())
            return evalExpr(n->thenExpr.get(), ctx, depth + 1);
        return evalExpr(n->elseExpr.get(), ctx, depth + 1);
    }

    default:
        break;
    }
    return Value::undefined();
}

// ============================================================================
// evalBinary
// ============================================================================
Value AcmlSemantic::evalBinary(const BinaryNode* n, const Ctx& ctx, int depth)
{
    Value L = evalExpr(n->left.get(),  ctx, depth + 1);
    Value R = evalExpr(n->right.get(), ctx, depth + 1);

    if (L.isUndefined() || R.isUndefined()) return Value::undefined();

    // Arithmetic — both numeric
    if (L.isNumber() && R.isNumber())
    {
        double lv = L.toMm(), rv = R.toMm();
        if (n->op == "+")  return Value::num(lv + rv);
        if (n->op == "-")  return Value::num(lv - rv);
        if (n->op == "*")  return Value::num(L.number * R.number);  // preserve raw for scale
        if (n->op == "/")  return (R.number != 0.0) ? Value::num(L.number / R.number) : Value::undefined();
        if (n->op == "%")  return Value::num(std::fmod(lv, rv));
        if (n->op == "==") return Value::boolean(std::abs(lv - rv) < 1e-9);
        if (n->op == "!=") return Value::boolean(std::abs(lv - rv) >= 1e-9);
        if (n->op == "<")  return Value::boolean(lv <  rv);
        if (n->op == ">")  return Value::boolean(lv >  rv);
        if (n->op == "<=") return Value::boolean(lv <= rv);
        if (n->op == ">=") return Value::boolean(lv >= rv);
    }

    // String concatenation — triggered when either operand is a string
    if (n->op == "+" && (L.isString() || R.isString()))
    {
        auto toStr = [](const Value& v) -> std::string {
            if (v.isString()) return v.text;
            if (v.isNumber())
            {
                // Format as integer when the value has no fractional part
                double d = v.number;
                if (d == std::floor(d) && std::abs(d) < 1e15)
                    return std::to_string((long long)d);
                return std::to_string(d);
            }
            if (v.isBool()) return v.asBool() ? "true" : "false";
            return "";
        };
        return Value::str(toStr(L) + toStr(R));
    }

    // Boolean ops
    if (n->op == "&&") return Value::boolean(L.asBool() && R.asBool());
    if (n->op == "||") return Value::boolean(L.asBool() || R.asBool());

    return Value::undefined();
}

// ============================================================================
// evalReference — resolve a.b.c dotted chain
// ============================================================================
Value AcmlSemantic::evalReference(const ReferenceNode* n,
                                   const Ctx& ctx, int depth)
{
    if (n->parts.empty()) return Value::undefined();

    const std::string& head = n->parts[0];

    // ── model_data.field  (inside Repeater template) ─────────────────────────
    if (head == "model_data" && !modelDataStack_.empty())
    {
        if (n->parts.size() == 1) return Value::undefined();  // bare model_data: unsupported
        auto it = modelDataStack_.back().find(n->parts[1]);
        return (it != modelDataStack_.back().end()) ? it->second : Value::undefined();
    }

    // ── Component param bindings / Repeater index+count (checked before id registry) ──
    // Search the full stack from innermost (back) to outermost (front) so that
    // inner scopes (Repeater index/count) shadow outer ones (Component params)
    // while still allowing outer params (door_width, spacing, …) to be found
    // when the inner scope does not define them.
    if (!paramStack_.empty())
    {
        for (auto sit = paramStack_.rbegin(); sit != paramStack_.rend(); ++sit)
        {
            auto pbIt = sit->find(head);
            if (pbIt == sit->end()) continue;

            // Found in this scope — return it
            if (n->parts.size() == 1) return pbIt->second;
            // head.prop on a Vec value
            if (n->parts.size() == 2 && pbIt->second.isVec())
            {
                const std::string& prop = n->parts[1];
                if (prop == "x") return Value::num(pbIt->second.vx());
                if (prop == "y") return Value::num(pbIt->second.vy());
                if (prop == "z") return Value::num(pbIt->second.vz());
            }
            return Value::undefined();
        }
    }

    // ── Implicit references ──────────────────────────────────────────────────
    // getCachedProps() returns a stable pointer to the cache entry, avoiding
    // a full map copy.  When inside a component (paramStack_ non-empty) it
    // returns nullptr and we fall back to the copy-based resolveProps().
    std::map<std::string, Value> ownedProps;   // fallback storage (component context)
    const std::map<std::string, Value>* targetProps = nullptr;

    auto resolveTarget = [&](const ElementNode* elem) {
        targetProps = getCachedProps(elem, depth + 1);
        if (!targetProps) {
            ownedProps  = resolveProps(elem, depth + 1);
            targetProps = &ownedProps;
        }
    };

    // ── Local variables — own properties already resolved in this element ───
    // Allows:  ancho_total: expr       (definition)
    //          width: ancho_total      (reference — must appear AFTER definition)
    if (ctx.localProps && n->parts.size() == 1)
    {
        auto it = ctx.localProps->find(head);
        if (it != ctx.localProps->end())
            return it->second;
    }

    if (head == "root" && ctx.root)
    {
        // `root` resolves to the Scene element (or the first element if no Scene).
        const ElementNode* rootElem = nullptr;
        for (const auto& item : ctx.root->items)
        {
            if (item->kind != NodeKind::Element) continue;
            auto* e = as<ElementNode>(item.get());
            if (e->typeName == "Scene") { rootElem = e; break; }
            if (!rootElem) rootElem = e;  // fallback: first element
        }
        if (rootElem) resolveTarget(rootElem);
        if (n->parts.size() == 1) return Value::str("root");
    }
    else if (head == "self" && ctx.self)
    {
        resolveTarget(ctx.self);
    }
    else if (head == "parent" && ctx.parent)
    {
        if (ctx.parentProps)
            targetProps = ctx.parentProps;
        else
            resolveTarget(ctx.parent);
    }
    else if (head == "previous" && ctx.previous)
    {
        resolveTarget(ctx.previous);
    }
    else if (head == "next" && ctx.next)
    {
        resolveTarget(ctx.next);
    }
    // ── Interactive pick (§3B) — bare `pick` keyword ─────────────────────────
    else if (head == "pick" && n->parts.size() == 1)
    {
        if (pickCancelled_) return Value::undefined();

        // Check per-element cache — consecutive `pick` properties on the same
        // element are grouped into a single user click (spec §3B.4).
        if (ctx.self)
        {
            auto cit = pickCache_.find(ctx.self);
            if (cit != pickCache_.end())
                return cit->second;
        }

        if (!pickProvider_.getPoint)
            return Value::undefined();  // no provider — batch/test mode

        std::string prompt = ctx.self
            ? ("Select " + ctx.self->typeName + " position")
            : "Select point";
        double px = 0.0, py = 0.0, pz = 0.0;
        if (!pickProvider_.getPoint(prompt, px, py, pz))
        {
            pickCancelled_ = true;
            return Value::undefined();
        }
        Value v = Value::vec3(px, py, pz);
        if (ctx.self) pickCache_[ctx.self] = v;
        return v;
    }
    else
    {
        // Try ID registry
        auto it = idRegistry_.find(head);
        if (it != idRegistry_.end())
        {
            // Single-part: return the name as a string (avoids self-cycle
            // when an element evaluates its own "id: name" assignment).
            // Two-part (id.prop) still needs props resolved.
            if (n->parts.size() == 1) return Value::str(head);

            resolveTarget(it->second);
        }
        else
        {
            // Single bare identifier — treat as string enum
            if (n->parts.size() == 1)
                return Value::str(head);
            return Value::undefined();
        }
    }

    // Single-part reference with no dot — return the element's id string
    if (n->parts.size() == 1) return Value::str(head);

    if (!targetProps) return Value::undefined();

    // Two-part: head.property
    if (n->parts.size() == 2)
    {
        auto it = targetProps->find(n->parts[1]);
        if (it != targetProps->end()) return it->second;
        return Value::undefined();
    }

    // Three-part: head.group.sub  (e.g. room.bounds.width, el.center.x, el.anchor.left)
    // The group and sub-key are stored as a flat "group.sub" key in the props map.
    if (n->parts.size() == 3)
    {
        std::string compound = n->parts[1] + "." + n->parts[2];
        auto it = targetProps->find(compound);
        if (it != targetProps->end()) return it->second;
        return Value::undefined();
    }

    return Value::undefined();
}

// ============================================================================
// evalFuncCall — built-in math + geometry functions
// ============================================================================
Value AcmlSemantic::evalFuncCall(const FunctionCallNode* n,
                                  const Ctx& ctx, int depth)
{
    // Evaluate all arguments
    std::vector<Value> args;
    args.reserve(n->args.size());
    for (const auto& a : n->args)
        args.push_back(evalExpr(a.get(), ctx, depth + 1));

    auto num0 = [&]() { return args.size() > 0 && args[0].isNumber() ? args[0].number : 0.0; };
    auto num1 = [&]() { return args.size() > 1 && args[1].isNumber() ? args[1].number : 0.0; };

    // ── Math functions ────────────────────────────────────────────────────────
    const std::string& fn = n->name;
    if (fn == "sin")   return Value::num(std::sin(args[0].toRad()));
    if (fn == "cos")   return Value::num(std::cos(args[0].toRad()));
    if (fn == "tan")   return Value::num(std::tan(args[0].toRad()));
    if (fn == "asin")  return Value::num(std::asin(num0()) * 180.0 / M_PI, Value::Unit::Deg);
    if (fn == "acos")  return Value::num(std::acos(num0()) * 180.0 / M_PI, Value::Unit::Deg);
    if (fn == "atan")  return Value::num(std::atan(num0()) * 180.0 / M_PI, Value::Unit::Deg);
    if (fn == "atan2") return Value::num(std::atan2(num0(), num1()) * 180.0 / M_PI, Value::Unit::Deg);
    if (fn == "sqrt")  return Value::num(std::sqrt(num0()));
    if (fn == "abs")   return Value::num(std::abs(num0()));
    if (fn == "pow")   return Value::num(std::pow(num0(), num1()));
    if (fn == "round") return Value::num(std::round(num0()));
    if (fn == "floor") return Value::num(std::floor(num0()));
    if (fn == "ceil")  return Value::num(std::ceil(num0()));
    if (fn == "min" && args.size() >= 2) return Value::num((std::min)(num0(), num1()));
    if (fn == "max" && args.size() >= 2) return Value::num((std::max)(num0(), num1()));
    if (fn == "clamp" && args.size() >= 3)
    {
        double lo = num1(), hi = args[2].isNumber() ? args[2].number : lo;
        double v  = num0();
        return Value::num(v < lo ? lo : (v > hi ? hi : v));
    }
    if (fn == "lerp" && args.size() >= 3)
        return Value::num(num0() + (num1() - num0()) * (args[2].isNumber() ? args[2].number : 0.0));

    // ── Position qualifiers (left/right/at — returned as Number in mm) ────────
    // These are resolved by the generator for wall-child positioning.
    if (fn == "left"  && !args.empty()) return Value::num(args[0].toMm());
    if (fn == "right" && !args.empty()) return Value::num(-args[0].toMm()); // negative = from right
    if (fn == "at"    && !args.empty()) return Value::num(args[0].toMm());

    // ── Geometry helpers ──────────────────────────────────────────────────────
    if (fn == "distance" && args.size() >= 2 && args[0].isVec() && args[1].isVec())
    {
        double dx = args[0].vx() - args[1].vx();
        double dy = args[0].vy() - args[1].vy();
        double dz = args[0].vz() - args[1].vz();
        return Value::num(std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    if (fn == "midpoint" && args.size() >= 2 && args[0].isVec() && args[1].isVec())
        return Value::vec3((args[0].vx() + args[1].vx()) / 2.0,
                           (args[0].vy() + args[1].vy()) / 2.0,
                           (args[0].vz() + args[1].vz()) / 2.0);

    // ── Interactive pick functions (§3B) ─────────────────────────────────────
    if (fn == "pick")
    {
        if (pickCancelled_) return Value::undefined();

        // Two-click distance mode: pick("start_prompt", "end_prompt") → scalar mm
        if (n->args.size() >= 2)
        {
            if (!pickProvider_.getDistance)
                return Value::undefined();
            std::string p1 = (!args.empty() && args[0].isString()) ? args[0].text : "Select start";
            std::string p2 = (args.size() > 1 && args[1].isString())  ? args[1].text : "Select end";
            double dist = 0.0;
            if (!pickProvider_.getDistance(p1, p2, dist))
            { pickCancelled_ = true; return Value::undefined(); }
            return Value::num(dist);
        }

        // Single-click point mode: pick() or pick("prompt") → Vec3 world coords
        if (!pickProvider_.getPoint)
            return Value::undefined();

        // Check per-element cache for grouped picks (§3B.4)
        if (ctx.self)
        {
            auto cit = pickCache_.find(ctx.self);
            if (cit != pickCache_.end())
                return cit->second;
        }
        std::string prompt = (!args.empty() && args[0].isString())
            ? args[0].text
            : (ctx.self ? ("Select " + ctx.self->typeName + " position") : "Select point");
        double px = 0.0, py = 0.0, pz = 0.0;
        if (!pickProvider_.getPoint(prompt, px, py, pz))
        { pickCancelled_ = true; return Value::undefined(); }
        Value v = Value::vec3(px, py, pz);
        if (ctx.self) pickCache_[ctx.self] = v;
        return v;
    }

    if (fn == "pick_angle")
    {
        if (pickCancelled_) return Value::undefined();
        if (!pickProvider_.getAngle)
            return Value::undefined();
        std::string prompt = (!args.empty() && args[0].isString())
            ? args[0].text : "Select direction";
        double angleRad = 0.0;
        if (!pickProvider_.getAngle(prompt, angleRad))
        { pickCancelled_ = true; return Value::undefined(); }
        return Value::num(angleRad * 180.0 / M_PI, Value::Unit::Deg);
    }

    if (fn == "pick_entity")
    {
        // Entity selection — not yet emitting a value; returns undefined silently.
        return Value::undefined();
    }

    return Value::undefined();
}

// ============================================================================
// evalList — [e1, e2, ...]  → Vec
// ============================================================================
Value AcmlSemantic::evalList(const ListNode* n, const Ctx& ctx, int depth)
{
    if (n->items.empty()) return Value::undefined();

    // Check if it's a nested list [[x,y],[x,y],...] — polyline points
    if (n->items[0]->kind == NodeKind::List)
    {
        // Return first point as Vec for now; generator will iterate
        // We encode as: kind=Vec, vec = all flat coordinates
        Value r;
        r.kind = Value::Kind::Vec;
        r.vec.reserve(n->items.size() * 3);  // avoid incremental reallocations
        for (const auto& item : n->items)
        {
            Value sub = evalExpr(item.get(), ctx, depth + 1);
            if (sub.isVec())
            {
                r.vec.push_back(sub.vx());
                r.vec.push_back(sub.vy());
                r.vec.push_back(sub.vz());
            }
        }
        return r;
    }

    // Flat list: [x, y] or [x, y, z]
    std::vector<double> vals;
    for (const auto& item : n->items)
    {
        Value v = evalExpr(item.get(), ctx, depth + 1);
        if (v.isNumber()) vals.push_back(v.toMm());
    }

    if (vals.size() == 1) return Value::vec2(vals[0], 0.0);
    if (vals.size() == 2) return Value::vec2(vals[0], vals[1]);
    if (vals.size() >= 3) return Value::vec3(vals[0], vals[1], vals[2]);
    return Value::undefined();
}

// ============================================================================
// resolveLayoutChildren — Row / Column sequential layout
//
// Assigns absolute posX (Row) or posY (Column) to each child based on the
// accumulated extent of preceding siblings plus the spacing gap.
// Children are resolved in order (or reversed if reverse: true).
// The child's own position_x/position_y adds on top as a local offset.
// ============================================================================
void AcmlSemantic::resolveLayoutChildren(const ElementNode* elem,
                                          ResolvedElement&   re,
                                          const Ctx&         ctx,
                                          int                depth,
                                          double             childRotZ,
                                          double             childScaleX,
                                          double             childScaleY)
{
    const bool   isRow   = (re.typeName == "Row");
    const double spacing = re.getNum("spacing", 0.0);
    const bool   reverse = re.getBool("reverse", false);

    // Per-side padding.  Specific side overrides uniform `padding`.
    auto pad = [&](const char* side) -> double {
        auto it = re.props.find(side);
        if (it != re.props.end() && !it->second.isUndefined())
            return it->second.toMm();
        return re.getNum("padding", 0.0);
    };

    const double padMain  = isRow ? pad("padding_left")  : pad("padding_top");
    const double padCross = isRow ? pad("padding_top")   : pad("padding_left");

    // Collect child element nodes from the element body
    std::vector<const ElementNode*> childElems;
    for (const auto& item : elem->body)
        if (item->kind == NodeKind::Element)
            childElems.push_back(as<ElementNode>(item.get()));

    if (reverse)
        std::reverse(childElems.begin(), childElems.end());

    double offset  = padMain;  // running position along the layout axis
    int    placed  = 0;        // count of children placed so far

    // Build a child Ctx with the CURRENT offset baked into posX/posY.
    // Layout offsets are in the Row/Column's local space; rotate them into
    // world space by applying the accumulated rotation (childRotZ).
    auto makeChildCtx = [&](const ElementNode* childElem, size_t i) -> Ctx
    {
        Ctx c;
        c.self             = childElem;
        c.parent           = elem;
        c.root             = ctx.root;
        c.previous         = (i > 0)                     ? childElems[i - 1] : nullptr;
        c.next             = (i + 1 < childElems.size()) ? childElems[i + 1] : nullptr;
        c.posZ                = re.posZ;
        c.inheritedLayer      = re.layer;
        c.inheritedVisible    = ctx.inheritedVisible && re.getBool("visible", true);
        c.inheritedRotZ       = childRotZ;
        c.inheritedScaleX     = childScaleX;
        c.inheritedScaleY     = childScaleY;
        c.parentProps         = &re.props;

        // Local layout offset (along main axis + cross padding), then scale + rotate
        double lx = isRow ? (offset * childScaleX) : (padCross * childScaleX);
        double ly = isRow ? (padCross * childScaleY) : (offset * childScaleY);
        if (childRotZ != 0.0)
        {
            double cs = std::cos(childRotZ), sn = std::sin(childRotZ);
            double rx = lx * cs - ly * sn, ry = lx * sn + ly * cs;
            lx = rx; ly = ry;
        }
        c.posX = re.posX + lx;
        c.posY = re.posY + ly;
        return c;
    };

    // Advance offset after placing one resolved child.
    // Spacing is applied BEFORE makeChildCtx, not here.
    auto advanceOffset = [&](const ResolvedElement& child)
    {
        double extent = isRow ? child.getNum("width",  0.0)
                              : child.getNum("height", 0.0);
        offset += extent;
        ++placed;
    };

    for (size_t i = 0; i < childElems.size(); ++i)
    {
        const ElementNode* childElem = childElems[i];
        if (childElem->typeName == "Model") continue;

        if (childElem->typeName == "Repeater")
        {
            // Expand inline so each instance gets the CURRENT offset in its ctx.
            // Resolve Repeater's own props (count / model reference).
            auto repProps = resolveProps(childElem, depth);

            // Find the single child template (skip Model nodes)
            const ElementNode* tmpl = nullptr;
            for (const auto& bitem : childElem->body)
                if (bitem->kind == NodeKind::Element)
                {
                    auto* e = as<ElementNode>(bitem.get());
                    if (e->typeName != "Model") { tmpl = e; break; }
                }
            if (!tmpl) continue;

            // Determine count and optional model items
            std::vector<std::map<std::string, Value>> modelItems;
            bool hasModel = false;
            {
                auto mit = repProps.find("model");
                if (mit != repProps.end())
                {
                    if (mit->second.isString())
                    {
                        auto idit = idRegistry_.find(mit->second.text);
                        if (idit != idRegistry_.end() &&
                            idit->second->typeName == "Model")
                        {
                            modelItems = resolveModel(idit->second, depth + 1);
                            hasModel   = true;
                        }
                    }
                    // model: <number> is a shorthand for count
                    // (falls through to count calculation below)
                }
            }
            int count = hasModel
                ? (int)modelItems.size()
                : [&]() -> int {
                    // count: takes priority (spec §6.6.1)
                    auto cit = repProps.find("count");
                    if (cit != repProps.end() && cit->second.isNumber())
                        return (int)cit->second.toMm();
                    // model: <number> as numeric shorthand
                    auto mit = repProps.find("model");
                    if (mit != repProps.end() && mit->second.isNumber())
                        return (int)mit->second.toMm();
                    return 0;
                }();
            if (count <= 0) continue;

            for (int idx = 0; idx < count; ++idx)
            {
                // Push iteration bindings
                std::map<std::string, Value> bindings;
                bindings["index"] = Value::num(static_cast<double>(idx));
                bindings["count"] = Value::num(static_cast<double>(count));
                StackGuard<decltype(paramStack_)> pg(paramStack_, bindings);
                // modelDataStack_ guard — only pushed when a Model is present
                struct ModelGuard {
                    decltype(modelDataStack_)& stack;
                    bool active;
                    ModelGuard(decltype(modelDataStack_)& s, bool a,
                               std::map<std::string,Value> item)
                        : stack(s), active(a)
                    { if (active) stack.push_back(std::move(item)); }
                    ~ModelGuard() { if (active) stack.pop_back(); }
                } mg(modelDataStack_, hasModel,
                     hasModel ? modelItems[static_cast<size_t>(idx)]
                              : std::map<std::string,Value>{});

                // Add spacing before each instance (except the very first)
                if (placed > 0) offset += spacing;

                // Resolve with the CURRENT offset
                Ctx instCtx = makeChildCtx(tmpl, i);
                ResolvedElement inst = resolveElement(tmpl, instCtx, depth + 1);
                advanceOffset(inst);
                re.children.push_back(std::move(inst));
                // pg and mg destructors pop their stacks here
            }
        }
        else
        {
            // Add spacing before each child (except the very first)
            if (placed > 0) offset += spacing;

            // Normal child: pass current offset in ctx, then advance
            Ctx childCtx = makeChildCtx(childElem, i);
            ResolvedElement child = resolveElement(childElem, childCtx, depth + 1);
            advanceOffset(child);
            re.children.push_back(std::move(child));
        }
    }

    // Expose computed size so parent expressions can reference it
    double padEnd = isRow ? pad("padding_right") : pad("padding_bottom");
    double total  = offset + padEnd;

    if (isRow) {
        re.props["content_width"]  = Value::num(total - padMain - padEnd);
        re.props["bounds.width"]   = Value::num(total);
    } else {
        re.props["content_height"] = Value::num(total - padMain - padEnd);
        re.props["bounds.height"]  = Value::num(total);
    }
    re.props["children_count"] = Value::num(static_cast<double>(childElems.size()));
}

// ============================================================================
// resolveModel — evaluate a Model element's Item children into property maps
// ============================================================================
std::vector<std::map<std::string, Value>>
AcmlSemantic::resolveModel(const ElementNode* modelElem, int depth)
{
    std::vector<std::map<std::string, Value>> result;

    for (const auto& bodyItem : modelElem->body)
    {
        if (bodyItem->kind != NodeKind::Element) continue;
        auto* item = as<ElementNode>(bodyItem.get());
        if (item->typeName != "Item") continue;

        std::map<std::string, Value> itemMap;
        Ctx itemCtx;
        itemCtx.self = item;

        for (const auto& propItem : item->body)
        {
            if (propItem->kind != NodeKind::Assignment) continue;
            auto* a = as<AssignmentNode>(propItem.get());
            if (a->value)
                itemMap[a->name] = evalExpr(a->value.get(), itemCtx, depth + 1);
        }
        result.push_back(std::move(itemMap));
    }
    return result;
}

// ============================================================================
// expandRepeater — expand a Repeater into N resolved element instances
//
// Binds `index` and `count` in paramStack_ for each iteration.
// If a Model is referenced, also pushes item fields onto modelDataStack_.
// ============================================================================
std::vector<ResolvedElement>
AcmlSemantic::expandRepeater(const ElementNode* repElem,
                              const Ctx&         ctx,
                              int                depth)
{
    if (depth > 64)
    {
        addError(repElem->line, repElem->col,
                 "Repeater nesting too deep.");
        return {};
    }

    // Resolve the Repeater's own properties (count, model)
    auto repProps = resolveProps(repElem, depth);

    // Find the single child template (first Element in body)
    const ElementNode* tmpl = nullptr;
    for (const auto& item : repElem->body)
        if (item->kind == NodeKind::Element)
        {
            auto* e = as<ElementNode>(item.get());
            if (e->typeName != "Model")  // skip inline Model nodes
            {
                tmpl = e;
                break;
            }
        }

    if (!tmpl)
    {
        addError(repElem->line, repElem->col,
                 "Repeater has no child template element.");
        return {};
    }

    // ── Determine count and model items ──────────────────────────────────────
    std::vector<std::map<std::string, Value>> modelItems;
    bool hasModel = false;

    auto modelIt = repProps.find("model");
    if (modelIt != repProps.end() && modelIt->second.isString())
    {
        const std::string& modelId = modelIt->second.text;
        auto idIt = idRegistry_.find(modelId);
        if (idIt != idRegistry_.end() && idIt->second->typeName == "Model")
        {
            modelItems = resolveModel(idIt->second, depth + 1);
            hasModel   = true;
        }
        else
            addError(repElem->line, repElem->col,
                     "Repeater: model '" + modelId + "' not found or not a Model.");
    }

    int count = hasModel
        ? (int)modelItems.size()
        : [&]() -> int {
            // count: takes priority (spec §6.6.1)
            auto cit = repProps.find("count");
            if (cit != repProps.end() && cit->second.isNumber())
                return (int)cit->second.toMm();
            // model: <number> as numeric shorthand
            if (modelIt != repProps.end() && modelIt->second.isNumber())
                return (int)modelIt->second.toMm();
            return 0;
        }();

    if (count <= 0)
    {
        if (!hasModel)
            addError(repElem->line, repElem->col,
                     "Repeater: 'count' must be > 0.");
        return {};
    }

    // ── Expand ───────────────────────────────────────────────────────────────
    std::vector<ResolvedElement> result;
    result.reserve(static_cast<size_t>(count));

    for (int idx = 0; idx < count; ++idx)
    {
        std::map<std::string, Value> iterBindings;
        iterBindings["index"] = Value::num(static_cast<double>(idx));
        iterBindings["count"] = Value::num(static_cast<double>(count));
        StackGuard<decltype(paramStack_)> pg(paramStack_, iterBindings);

        struct ModelGuard {
            decltype(modelDataStack_)& stack;
            bool active;
            ModelGuard(decltype(modelDataStack_)& s, bool a,
                       std::map<std::string,Value> item)
                : stack(s), active(a)
            { if (active) stack.push_back(std::move(item)); }
            ~ModelGuard() { if (active) stack.pop_back(); }
        } mg(modelDataStack_, hasModel,
             hasModel ? modelItems[static_cast<size_t>(idx)]
                      : std::map<std::string,Value>{});

        result.push_back(resolveElement(tmpl, ctx, depth + 1));
        // pg and mg destructors pop their stacks here
    }

    return result;
}

// ============================================================================
// expandComponent — instantiate a Component definition with bound arguments
// ============================================================================
ResolvedElement AcmlSemantic::expandComponent(const ElementNode*    inst,
                                               const ComponentEntry& entry,
                                               const Ctx&            callerCtx,
                                               int                   depth)
{
    if (depth > 64)
    {
        addError(inst->line, inst->col,
                 "Component expansion too deep — possible infinite recursion.");
        return {};
    }

    const ComponentDefNode* comp = entry.node;

    // ── Step 1: Build param bindings ─────────────────────────────────────────
    // Start with param defaults, then override with instance-provided arguments.
    std::map<std::string, Value> bindings;

    for (const auto& pItem : comp->params)
    {
        auto* pd = as<ParamDeclNode>(pItem.get());
        bindings[pd->name] = pd->defaultValue
                             ? evalExpr(pd->defaultValue.get(), callerCtx, depth + 1)
                             : Value::undefined();
    }

    for (const auto& item : inst->body)
    {
        if (item->kind != NodeKind::Assignment) continue;
        auto* a = as<AssignmentNode>(item.get());
        if (a->value)
            bindings[a->name] = evalExpr(a->value.get(), callerCtx, depth + 1);
    }

    // ── Step 2: Component context ────────────────────────────────────────────
    Ctx compCtx;
    compCtx.posX                = callerCtx.posX;
    compCtx.posY                = callerCtx.posY;
    compCtx.posZ                = callerCtx.posZ;
    compCtx.root                = callerCtx.root;
    compCtx.parent              = callerCtx.parent;
    compCtx.inheritedLayer      = callerCtx.inheritedLayer;
    compCtx.inheritedRotZ       = callerCtx.inheritedRotZ;
    compCtx.inheritedScaleX     = callerCtx.inheritedScaleX;
    compCtx.inheritedScaleY     = callerCtx.inheritedScaleY;

    // Apply position override from bindings
    if (bindings.count("position_x") && bindings["position_x"].isNumber())
        compCtx.posX = callerCtx.posX + bindings["position_x"].toMm();
    if (bindings.count("position_y") && bindings["position_y"].isNumber())
        compCtx.posY = callerCtx.posY + bindings["position_y"].toMm();
    if (bindings.count("position_z") && bindings["position_z"].isNumber())
        compCtx.posZ = callerCtx.posZ + bindings["position_z"].toMm();

    // ── Step 3: Build container ResolvedElement ───────────────────────────────
    ResolvedElement re;
    re.typeName = inst->typeName;
    re.posX     = compCtx.posX;
    re.posY     = compCtx.posY;
    re.posZ     = compCtx.posZ;
    // Same layer priority as resolveElement (§4.2): inherited > AIA type default > ""
    re.layer = !callerCtx.inheritedLayer.empty()
               ? callerCtx.inheritedLayer
               : defaultLayer(inst->typeName);

    // Start with instance bindings, then fill in component body assignments.
    re.props = bindings;

    // Push param bindings onto the stack so evalReference can resolve them.
    // StackGuard guarantees pop even if an exception escapes.
    StackGuard<decltype(paramStack_)> paramGuard(paramStack_, bindings);

    for (const auto& item : comp->body)
    {
        if (item->kind != NodeKind::Assignment) continue;
        auto* a = as<AssignmentNode>(item.get());
        if (!a->value) continue;
        // Only set if not already provided by the instance (instance wins)
        if (!bindings.count(a->name) || bindings[a->name].isUndefined())
            re.props[a->name] = evalExpr(a->value.get(), compCtx, depth + 1);
    }

    // Extract id and layer override
    if (re.props.count("id") && re.props["id"].isString())
        re.id = re.props["id"].text;
    if (re.props.count("layer") && re.props["layer"].isString())
        re.layer = re.props["layer"].text;

    // ── Step 4: Resolve child elements from the component body ───────────────
    std::vector<const ElementNode*> childElems;
    for (const auto& item : comp->body)
        if (item->kind == NodeKind::Element)
            childElems.push_back(as<ElementNode>(item.get()));

    for (size_t i = 0; i < childElems.size(); ++i)
    {
        Ctx childCtx             = compCtx;
        childCtx.self            = childElems[i];
        childCtx.parent          = nullptr;
        childCtx.previous        = (i > 0)                     ? childElems[i - 1] : nullptr;
        childCtx.next            = (i + 1 < childElems.size()) ? childElems[i + 1] : nullptr;
        childCtx.posX                = re.posX;
        childCtx.posY                = re.posY;
        childCtx.posZ                = re.posZ;
        childCtx.inheritedLayer      = re.layer;
        childCtx.inheritedRotZ       = re.inheritedRotZ + [&]() -> double {
            auto it = re.props.find("rotation");
            if (it == re.props.end()) it = re.props.find("rotation_z");
            return (it != re.props.end()) ? it->second.toRad() : 0.0;
        }();
        childCtx.inheritedScaleX = re.worldScaleX;
        childCtx.inheritedScaleY = re.worldScaleY;
        childCtx.parentProps     = &re.props;
        re.children.push_back(resolveElement(childElems[i], childCtx, depth + 1));
    }
    // paramGuard destructor pops paramStack_ here

    // ── Computed spatial props for components ─────────────────────────────────
    {
        double w = re.getNum("bounds.width",  re.getNum("width",  0.0));
        double h = re.getNum("bounds.height", re.getNum("height", 0.0));
        re.props["bounds.x"]      = Value::num(re.posX);
        re.props["bounds.y"]      = Value::num(re.posY);
        re.props["bounds.width"]  = Value::num(w);
        re.props["bounds.height"] = Value::num(h);
        re.props["center.x"]      = Value::num(re.posX + w * 0.5);
        re.props["center.y"]      = Value::num(re.posY + h * 0.5);
        re.props["anchor.left"]   = Value::num(re.posX);
        re.props["anchor.right"]  = Value::num(re.posX + w);
        re.props["anchor.bottom"] = Value::num(re.posY);
        re.props["anchor.top"]    = Value::num(re.posY + h);
    }

    return re;
}

// ============================================================================
// processImport — load an imported ACML file and return its top-level elements
// ============================================================================
std::vector<ResolvedElement> AcmlSemantic::processImport(const ImportStmtNode* imp)
{
    // Compute absolute path relative to the importing file's directory
    std::string importPath = imp->path;
    if (!filename_.empty() &&
        importPath.find(':') == std::string::npos &&
        importPath[0] != '/' && importPath[0] != '\\')
    {
        auto slash = filename_.find_last_of("/\\");
        if (slash != std::string::npos)
            importPath = filename_.substr(0, slash + 1) + importPath;
    }

    // Cycle detection
    if (importedPaths_.count(importPath))
    {
        addError(imp->line, imp->col,
                 "Circular import or self-import: '" + importPath + "'");
        return {};
    }
    importedPaths_.insert(importPath);

    // Read the file
    std::ifstream f(importPath, std::ios::in | std::ios::binary);
    if (!f.is_open())
    {
        addError(imp->line, imp->col,
                 "Cannot open import '" + importPath + "'");
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    // Lex + parse
    AcmlLexer lexer(ss.str(), importPath);
    auto tokens = lexer.tokenize();
    if (lexer.hasErrors())
    {
        for (const auto& e : lexer.errors()) errors_.push_back(e);
        return {};
    }

    AcmlParser parser(std::move(tokens), importPath);
    auto doc = parser.parse();
    if (parser.hasErrors())
    {
        for (const auto& e : parser.errors()) errors_.push_back(e);
        return {};
    }

    // Keep AST alive (registry holds raw pointers into it)
    importedDocs_.push_back(std::move(doc));
    const DocumentNode* importedDoc = importedDocs_.back().get();

    // Register the imported file's declarations into the shared registries
    buildIdRegistry(importedDoc);
    buildComponentRegistry(importedDoc);
    buildTypeDefRegistry(importedDoc);

    // Resolve and return the imported file's top-level elements
    return resolveTopLevel(importedDoc);
}

// ============================================================================
// defaultLayer — AIA layer convention (§8.1)
// ============================================================================
std::string AcmlSemantic::defaultLayer(const std::string& typeName)
{
    // AIA layer assignments for typed architectural elements (§8.1).
    // Returns "" for all other types so they can freely inherit from ancestors
    // without "0" blocking the inheritance chain (§4.2).
    // The generator falls back to "0" when el.layer is still empty after
    // all inheritance has been applied.
    static const std::map<std::string, std::string> kLayers = {
        { "Wall",        "A-WALL"      },
        { "Window",      "A-GLAZ"      },
        { "Door",        "A-DOOR"      },
        { "Column",      "S-COLS"      },
        { "Beam",        "S-BEAM"      },
        { "Slab",        "S-SLAB"      },
        { "Foundation",  "S-FNDN"      },
        { "Stair",       "A-FLOR-STRS" },
        { "Dimension",   "A-ANNO-DIMS" },
        { "Text",        "A-ANNO-TEXT" },
        { "Leader",      "A-ANNO-NOTE" },
        { "Hatch",       "A-PATT"      },
        { "BlockRef",    "A-FURN"      },
    };
    auto it = kLayers.find(typeName);
    return (it != kLayers.end()) ? it->second : "";
}

// ============================================================================
// Error helper
// ============================================================================
void AcmlSemantic::addError(int line, int col, const std::string& msg)
{
    errors_.push_back(filename_ + ":" + std::to_string(line) +
                      ":" + std::to_string(col) + ": " + msg);
}

} // namespace Acml
