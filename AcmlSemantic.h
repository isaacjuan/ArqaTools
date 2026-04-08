// AcmlSemantic.h — ACML semantic analyzer
//
// Walks the AST, builds an ID registry, evaluates expressions, applies
// property inheritance and AIA layer defaults, and produces a flat list
// of ResolvedElement structs ready for the geometry generator.
//
// No ObjectARX dependency — pure C++20 standard library.

#pragma once
#include "AcmlAst.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cmath>

namespace Acml
{

// ============================================================================
// PickProvider — callbacks for interactive point / angle / distance input.
//
// Decouples the semantic analyzer from AutoCAD I/O so the analyzer remains
// a pure C++ component.  AcmlTools supplies a real implementation via
// acedGetPoint / acedGetAngle.  Tests or batch processing can pass an empty
// PickProvider (all callbacks null) — any pick expression then evaluates to
// undefined and the element is skipped silently.
// ============================================================================
struct PickProvider
{
    // Single click → world-space point.
    // Returns true and fills x/y/z on success; false on Escape.
    std::function<bool(const std::string& prompt,
                       double& x, double& y, double& z)> getPoint;

    // Two clicks → distance in mm between start and end points.
    // Returns true and fills dist on success; false on Escape.
    std::function<bool(const std::string& startPrompt,
                       const std::string& endPrompt,
                       double& dist)>                    getDistance;

    // Angle pick (rubber-band from a base point).
    // Returns true and fills angleRad on success; false on Escape.
    std::function<bool(const std::string& prompt,
                       double& angleRad)>                getAngle;
};

// ============================================================================
// Value — runtime value of any resolved ACML expression
// ============================================================================
struct Value
{
    enum class Kind { Undefined, Number, String, Bool, Color, Vec };
    // Unit stored as an enum to avoid per-value string heap allocation.
    enum class Unit { None, Mm, Cm, M, Deg, Rad, Pct };

    Kind        kind   = Kind::Undefined;
    double      number = 0.0;
    Unit        unit   = Unit::None;  // only meaningful when kind == Number
    std::string text;                 // String / Color / Enum
    std::vector<double> vec;          // Vec kind: [x] or [x,y] or [x,y,z] or polyline flat coords

    // ── Factories ────────────────────────────────────────────────────────────
    static Unit unitFromString(const std::string& s)
    {
        if (s == "m")   return Unit::M;
        if (s == "cm")  return Unit::Cm;
        if (s == "deg") return Unit::Deg;
        if (s == "rad") return Unit::Rad;
        if (s == "%")   return Unit::Pct;
        return Unit::None;  // "mm" or empty
    }

    static Value num(double v, Unit u = Unit::None)
    {
        Value r; r.kind = Kind::Number; r.number = v; r.unit = u;
        return r;
    }
    // Convenience overload for callers that already have a unit string (lexer output).
    static Value num(double v, const std::string& u)
    {
        return num(v, unitFromString(u));
    }
    static Value str(const std::string& s)
    {
        Value r; r.kind = Kind::String; r.text = s;
        return r;
    }
    static Value boolean(bool b)
    {
        Value r; r.kind = Kind::Bool; r.number = b ? 1.0 : 0.0;
        return r;
    }
    static Value color(const std::string& hex)
    {
        Value r; r.kind = Kind::Color; r.text = hex;
        return r;
    }
    static Value vec2(double x, double y)
    {
        Value r; r.kind = Kind::Vec; r.vec = {x, y, 0.0};
        return r;
    }
    static Value vec3(double x, double y, double z)
    {
        Value r; r.kind = Kind::Vec; r.vec = {x, y, z};
        return r;
    }
    static Value undefined() { return Value{}; }

    // ── Queries ───────────────────────────────────────────────────────────────
    bool isUndefined() const { return kind == Kind::Undefined; }
    bool isNumber()    const { return kind == Kind::Number;    }
    bool isString()    const { return kind == Kind::String || kind == Kind::Color; }
    bool isBool()      const { return kind == Kind::Bool;      }
    bool isVec()       const { return kind == Kind::Vec;       }

    // Convert number to mm (applies unit suffix)
    double toMm() const
    {
        if (kind != Kind::Number) return 0.0;
        if (unit == Unit::M)  return number * 1000.0;
        if (unit == Unit::Cm) return number * 10.0;
        return number;   // mm, None, or dimensionless
    }

    // Convert to radians (from deg or rad)
    double toRad() const
    {
        if (kind != Kind::Number) return 0.0;
        if (unit == Unit::Rad) return number;
        return number * M_PI / 180.0;   // deg (or bare number → treated as deg)
    }

    // Numeric truth: non-zero → true
    bool asBool() const { return std::abs(number) > 1e-12; }

    // Vec component with bounds check (returns 0 when out of range)
    double vx() const { return vec.size() > 0 ? vec[0] : 0.0; }
    double vy() const { return vec.size() > 1 ? vec[1] : 0.0; }
    double vz() const { return vec.size() > 2 ? vec[2] : 0.0; }
};

// ============================================================================
// ResolvedElement — one element with all properties evaluated
// ============================================================================
struct ResolvedElement
{
    std::string typeName;
    std::string id;
    std::string layer;
    double      posX = 0.0, posY = 0.0, posZ = 0.0; // absolute world origin

    // Transform state — set by the semantic pass, consumed by the generator.
    // inheritedRotZ   : parent's accumulated rotation (radians); own rotation is
    //                   already applied inside drawXxx().  Generator applies this
    //                   via AcDbEntity::transformBy() to orient local geometry.
    // worldScaleX/Y   : own scale × all ancestor scales (reserved for future use).
    double inheritedRotZ   = 0.0;
    // Accumulated scale from all ancestors × own scale.
    // Used by the semantic pass to propagate correct offsets to children of
    // layout containers (Row/Column/Repeater).  The generator does NOT apply
    // these as a geometric transform — entity dimensions (width, radius, …)
    // are taken literally from the ACML properties.
    double worldScaleX     = 1.0;
    double worldScaleY     = 1.0;

    // Visual appearance — "" / -1 means ByLayer (defer to the layer's setting).
    // Per v6 spec §4.2 these are NOT inherited; each element sets its own value
    // or defaults to ByLayer.
    std::string color;              // "" = ByLayer; "#RRGGBB" or named ("red" …)
    std::string linetype;           // "" = ByLayer; "continuous", "dashed", …
    double      lineweight = -1.0;  // -1 = ByLayer; ≥ 0 = explicit value in mm

    std::map<std::string, Value>  props;
    std::vector<ResolvedElement>  children;

    // ── Rotation pivot helpers ────────────────────────────────────────────────
    // Returns the world-space pivot point for inherited-rotation transforms.
    // Reads `anchor_point` property; defaults to posX/posY (bottom_left).
    void getPivot(double& px, double& py) const
    {
        double w = getNum("width",  0.0);
        double h = getNum("height", 0.0);
        std::string ap = getStr("anchor_point", "bottom_left");
        double ox = 0.0, oy = 0.0;
        if      (ap == "center")       { ox = w * 0.5; oy = h * 0.5; }
        else if (ap == "top_left")     { ox = 0.0;     oy = h;       }
        else if (ap == "top_right")    { ox = w;        oy = h;       }
        else if (ap == "bottom_right") { ox = w;        oy = 0.0;     }
        else if (ap == "left")         { ox = 0.0;     oy = h * 0.5; }
        else if (ap == "right")        { ox = w;        oy = h * 0.5; }
        else if (ap == "top")          { ox = w * 0.5; oy = h;       }
        else if (ap == "bottom")       { ox = w * 0.5; oy = 0.0;     }
        px = posX + ox;
        py = posY + oy;
    }

    // ── Property accessors ────────────────────────────────────────────────────
    // Returns numeric value in mm; falls back to `fallback` if not set.
    double getNum(const std::string& key, double fallback = 0.0) const
    {
        auto it = props.find(key);
        if (it == props.end() || it->second.isUndefined()) return fallback;
        if (it->second.isVec()) return it->second.vx();
        return it->second.toMm();
    }

    // Returns angle value in radians.
    double getAngle(const std::string& key, double fallbackDeg = 0.0) const
    {
        auto it = props.find(key);
        if (it == props.end() || it->second.isUndefined())
            return fallbackDeg * M_PI / 180.0;
        return it->second.toRad();
    }

    // Returns string property; falls back to `fallback`.
    std::string getStr(const std::string& key,
                       const std::string& fallback = "") const
    {
        auto it = props.find(key);
        if (it == props.end()) return fallback;
        return it->second.isString() ? it->second.text : fallback;
    }

    // Returns bool property; falls back to `fallback`.
    bool getBool(const std::string& key, bool fallback = false) const
    {
        auto it = props.find(key);
        if (it == props.end()) return fallback;
        return it->second.asBool();
    }

    // Returns a [x, y, z] triple from a Vec or two scalar properties.
    // `vecKey` is tried first (e.g. "center", "start").
    // If not found, tries `xKey` and `yKey` individually.
    void getPoint(const std::string& vecKey,
                  const std::string& xKey, const std::string& yKey,
                  double& outX, double& outY, double& outZ) const
    {
        auto it = props.find(vecKey);
        if (it != props.end() && it->second.isVec())
        {
            outX = it->second.vx();
            outY = it->second.vy();
            outZ = it->second.vz();
            return;
        }
        outX = getNum(xKey, posX);
        outY = getNum(yKey, posY);
        outZ = posZ;
    }
};

// ============================================================================
// ComponentEntry — cached pointer to a parsed Component definition
// ============================================================================
struct ComponentEntry
{
    const ComponentDefNode* node;  // owned by importedDocs_ or the top-level DocumentNode
};

// ============================================================================
// TypeDefEntry — cached pointer to a parsed ElementType definition
// ============================================================================
struct TypeDefEntry
{
    const ElementTypeDefNode* node;
    std::string               inherits;  // empty or parent type name
};

// ============================================================================
// AcmlSemantic — semantic analyzer
// ============================================================================
class AcmlSemantic
{
public:
    explicit AcmlSemantic(const std::string& filename = "",
                          PickProvider       pickProvider = {});

    // -------------------------------------------------------------------------
    // analyze — entry point.
    // Returns top-level ResolvedElements (children nested inside each).
    // -------------------------------------------------------------------------
    std::vector<ResolvedElement> analyze(const DocumentNode* doc);

    bool                            hasErrors()        const { return !errors_.empty(); }
    const std::vector<std::string>& errors()           const { return errors_; }
    // True when the user pressed Escape during an interactive pick (§3B.7).
    bool                            wasPickCancelled() const { return pickCancelled_; }

private:
    // ── State ─────────────────────────────────────────────────────────────────
    std::string              filename_;
    std::vector<std::string> errors_;

    // First-pass ID registry: id value → ElementNode*
    std::map<std::string, const ElementNode*>       idRegistry_;
    // Cache of already-resolved property maps, keyed by element pointer
    std::map<const ElementNode*, std::map<std::string, Value>> cache_;
    // Cycle detection: element pointers currently being resolved
    std::set<const ElementNode*>                    resolving_;

    // Component and type registries (built during analyze())
    std::map<std::string, ComponentEntry>           componentRegistry_;
    std::map<std::string, TypeDefEntry>             typeRegistry_;

    // Imported document ASTs — kept alive so raw pointers in registries stay valid
    std::vector<std::unique_ptr<DocumentNode>>      importedDocs_;
    std::set<std::string>                           importedPaths_;

    // Pick support (§3B) — interactive coordinate / angle / distance input.
    // pickProvider_ supplies the actual I/O implementation (AutoCAD or test stub).
    // pickCache_ groups consecutive pick expressions on the same element into
    // one user click (spec §3B.4).  pickCancelled_ short-circuits analysis on
    // Escape so no partial geometry is created.
    PickProvider                        pickProvider_;
    std::map<const ElementNode*, Value> pickCache_;
    bool                                pickCancelled_ = false;

    // Param binding stack — pushed/popped during component expansion
    // evalReference checks the top of this stack before the id registry
    std::vector<std::map<std::string, Value>>       paramStack_;

    // Model data stack — pushed/popped during Repeater expansion.
    // evalReference uses it to resolve  model_data.field  expressions.
    std::vector<std::map<std::string, Value>>       modelDataStack_;

    // ── Context for resolving one element ────────────────────────────────────
    struct Ctx
    {
        const ElementNode*  self     = nullptr;
        const ElementNode*  parent   = nullptr;  // nearest ElementNode ancestor
        const DocumentNode* root     = nullptr;
        const ElementNode*  previous = nullptr;
        const ElementNode*  next     = nullptr;
        double              posX = 0.0, posY = 0.0, posZ = 0.0;
        std::string         inheritedLayer;
        bool                inheritedVisible = true;  // false → all children forced invisible
        // Accumulated transforms propagated from ancestors
        double              inheritedRotZ   = 0.0;   // radians
        double              inheritedScaleX = 1.0;
        double              inheritedScaleY = 1.0;
        // Resolved props of parent (for parent.xxx lookups)
        const std::map<std::string, Value>* parentProps = nullptr;
        // In-progress props of the element currently being resolved.
        // Allows bare-identifier references to own earlier properties
        // (local variables): width: ancho_total  where ancho_total was
        // defined earlier in the same element body.
        const std::map<std::string, Value>* localProps  = nullptr;
    };

    // ── Passes ────────────────────────────────────────────────────────────────
    void buildIdRegistry       (const DocumentNode* doc);
    void buildComponentRegistry(const DocumentNode* doc);
    void buildTypeDefRegistry  (const DocumentNode* doc);

    std::vector<ResolvedElement> resolveTopLevel(const DocumentNode* doc);

    ResolvedElement resolveElement(const ElementNode* elem,
                                   const Ctx&         ctx,
                                   int                depth = 0);

    // Expand a component instantiation with argument bindings.
    ResolvedElement expandComponent(const ElementNode*    inst,
                                    const ComponentEntry& entry,
                                    const Ctx&            callerCtx,
                                    int                   depth);

    // Resolve children of a Row or Column layout container,
    // computing position offsets along the layout axis.
    // Expands any Repeater children inline.
    // childRotZ/ScaleX/ScaleY are the accumulated transforms to pass down
    // (parent inherited + own rotation/scale of the Row/Column itself).
    void resolveLayoutChildren(const ElementNode* elem,
                               ResolvedElement&   re,
                               const Ctx&         ctx,
                               int                depth,
                               double             childRotZ,
                               double             childScaleX,
                               double             childScaleY);

    // Expand a Repeater element into a list of resolved instances.
    // Each instance binds `index`, `count`, and optionally `model_data.*`.
    std::vector<ResolvedElement> expandRepeater(const ElementNode* repElem,
                                                const Ctx&         ctx,
                                                int                depth);

    // Evaluate a Model element's Item children into a list of property maps.
    std::vector<std::map<std::string, Value>>
    resolveModel(const ElementNode* modelElem, int depth);

    // Process an import statement: lex/parse the file, register its
    // components and types, and return its top-level resolved elements.
    std::vector<ResolvedElement> processImport(const ImportStmtNode* imp);

    // Evaluate an expression node to a Value.
    // depth guards against runaway recursion in circular references.
    Value evalExpr(const AstNode*  expr,
                   const Ctx&      ctx,
                   int             depth = 0);

    Value evalBinary     (const BinaryNode*       n, const Ctx& ctx, int d);
    Value evalReference  (const ReferenceNode*    n, const Ctx& ctx, int d);
    Value evalFuncCall   (const FunctionCallNode* n, const Ctx& ctx, int d);
    Value evalList       (const ListNode*         n, const Ctx& ctx, int d);

    // Resolve props of any ElementNode (with caching + cycle detection).
    // Returns by value; uses cache when paramStack_ is empty.
    std::map<std::string, Value> resolveProps(const ElementNode* elem,
                                              int depth = 0);

    // Return a stable pointer to elem's resolved props without copying the map.
    // Resolves and caches if needed.  Only valid when paramStack_ is empty;
    // returns nullptr in component-expansion context (caller must fall back to
    // the copy-returning resolveProps).
    const std::map<std::string, Value>* getCachedProps(const ElementNode* elem,
                                                       int depth = 0);

    // AIA default layer per element type
    static std::string defaultLayer(const std::string& typeName);

    // Error helpers
    void addError(int line, int col, const std::string& msg);
};

} // namespace Acml
