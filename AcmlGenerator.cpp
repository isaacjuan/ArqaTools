// AcmlGenerator.cpp — ACML geometry generator implementation
#include "StdAfx.h"
#include "AcmlGenerator.h"
#include "CommonTools.h"
#include "CadInfra.h"
#include "dbents.h"
#include "dbpl.h"
#include "dbmtext.h"
#include "geassign.h"
#include <cmath>
#include <cctype>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Acml
{

// ============================================================================
// Draw defaults — used when required properties are absent
// ============================================================================
namespace {
    constexpr double kDefaultLineLength    = 1000.0;  // mm
    constexpr double kDefaultCircleRadius  =  100.0;  // mm
    constexpr double kDefaultRectWidth     = 1000.0;  // mm
    constexpr double kDefaultRectHeight    =  600.0;  // mm
    constexpr double kDefaultEllipseMajor  =  200.0;  // mm
    constexpr double kDefaultEllipseMinor  =  100.0;  // mm
    constexpr double kDefaultTextHeight    =  250.0;  // mm

    // Extract layer_* ACML properties into a CadInfra::LayerProps struct.
    CadInfra::LayerProps buildLayerProps(const ResolvedElement& el)
    {
        CadInfra::LayerProps lp;
        lp.color       = el.getStr("layer_color",       "");
        lp.linetype    = el.getStr("layer_linetype",    "");
        lp.lineweight  = el.props.count("layer_lineweight")
                             ? el.props.at("layer_lineweight").toMm() : -1.0;
        lp.description = el.getStr("layer_description", "");
        lp.plot        = el.getBool("layer_plot",   true);
        lp.locked      = el.getBool("layer_locked", false);
        return lp;
    }
} // anonymous namespace

// ============================================================================
// generate — entry point
// ============================================================================
int AcmlGenerator::generate(const std::vector<ResolvedElement>& elements)
{
    AcDbBlockTableRecord* ms = nullptr;
    if (CommonTools::GetModelSpace(ms) != Acad::eOk || !ms)
    {
        addError("Cannot open model space for writing.");
        return 0;
    }

    count_ = 0;
    for (const auto& el : elements)
        drawElement(el, ms);

    ms->close();
    return count_;
}

// ============================================================================
// drawElement — dispatch by element type name
// ============================================================================
void AcmlGenerator::drawElement(const ResolvedElement& el,
                                 AcDbBlockTableRecord*  ms)
{
    // Skip invisible elements
    if (!el.getBool("visible", true)) return;

    // Ensure the element's layer exists with its layer_* properties.
    // This must run even for conceptual elements (Scene, Group, …) that draw
    // no geometry themselves, so that layer_color / layer_linetype / etc. on
    // those elements are applied before any child entity is drawn on that layer.
    if (!el.layer.empty())
    {
        CA2T wLayer(el.layer.c_str(), CP_UTF8);
        CadInfra::EnsureLayer(CString(wLayer), buildLayerProps(el));
    }

    const std::string& t = el.typeName;

    // Primitives (§6.4)
    if      (t == "Line")      drawLine      (el, ms);
    else if (t == "Circle")    drawCircle    (el, ms);
    else if (t == "Arc")       drawArc       (el, ms);
    else if (t == "Polyline")  drawPolyline  (el, ms);
    else if (t == "Rectangle") drawRectangle (el, ms);
    else if (t == "Ellipse")   drawEllipse   (el, ms);
    else if (t == "Text")      drawText      (el, ms);
    // Architectural plan-view symbols
    else if (t == "Wall")      drawWall      (el, ms);
    else if (t == "Door")      drawDoor      (el, ms);
    else if (t == "Window")    drawWindow    (el, ms);
    else if (t == "Column")    drawColumn    (el, ms);
    // Grouping / structural / layout / data / scene — draw children recursively
    else if (t == "Group"    || t == "Room"      ||
             t == "Level"    || t == "Building"  ||
             t == "Config"   || t == "Row"       ||
             t == "Repeater" || t == "Model"     || t == "Item"      ||
             t == "Scene"    || t == "Container")
    { /* no own geometry — fall through to children (or none) */ }

    // Recurse into children regardless of parent type
    for (const auto& child : el.children)
        drawElement(child, ms);
}

// ============================================================================
// drawLine — Line { start: [x,y]; end: [x,y] }
//           or  { length: n; angle: deg; position_x: x; position_y: y }
// ============================================================================
void AcmlGenerator::drawLine(const ResolvedElement& el, AcDbBlockTableRecord* ms)
{
    AcGePoint3d p1, p2;
    double rot = el.getAngle("rotation", 0.0);

    auto itStart = el.props.find("start");
    auto itEnd   = el.props.find("end");

    if (itStart != el.props.end() && itStart->second.isVec() &&
        itEnd   != el.props.end() && itEnd->second.isVec())
    {
        p1 = AcGePoint3d(el.posX + itStart->second.vx(),
                         el.posY + itStart->second.vy(),
                         el.posZ + itStart->second.vz());
        p2 = AcGePoint3d(el.posX + itEnd->second.vx(),
                         el.posY + itEnd->second.vy(),
                         el.posZ + itEnd->second.vz());

        // Apply rotation around the element origin (el.posX, el.posY)
        if (rot != 0.0)
        {
            double c = std::cos(rot), s = std::sin(rot);
            auto rotPt = [&](AcGePoint3d& p) {
                double dx = p.x - el.posX, dy = p.y - el.posY;
                p.x = el.posX + dx * c - dy * s;
                p.y = el.posY + dx * s + dy * c;
            };
            rotPt(p1);
            rotPt(p2);
        }
    }
    else
    {
        // length + angle form — rotation adds to the direction angle
        double len   = el.getNum("length", kDefaultLineLength);
        double angle = el.getAngle("angle", 0.0) + rot;
        p1 = AcGePoint3d(el.posX, el.posY, el.posZ);
        p2 = AcGePoint3d(el.posX + len * std::cos(angle),
                         el.posY + len * std::sin(angle),
                         el.posZ);
    }

    AcDbLine* pLine = new AcDbLine(p1, p2);
    postEntity(pLine, el, ms);
}

// ============================================================================
// drawCircle — Circle { center: [x,y]; radius: r }
// ============================================================================
void AcmlGenerator::drawCircle(const ResolvedElement& el, AcDbBlockTableRecord* ms)
{
    AcGePoint3d ctr = getPoint(el, "center", "position_x", "position_y");
    double      rad = el.getNum("radius", kDefaultCircleRadius);

    AcDbCircle* pCircle = new AcDbCircle(ctr, AcGeVector3d::kZAxis, rad);
    postEntity(pCircle, el, ms);
}

// ============================================================================
// drawArc — Arc { center: [x,y]; radius: r; start_angle: deg; end_angle: deg }
// ============================================================================
void AcmlGenerator::drawArc(const ResolvedElement& el, AcDbBlockTableRecord* ms)
{
    AcGePoint3d ctr        = getPoint(el, "center", "position_x", "position_y");
    double      rad        = el.getNum("radius", kDefaultCircleRadius);
    double      startAngle = el.getAngle("start_angle", 0.0);
    double      endAngle   = el.getAngle("end_angle",   180.0);

    AcDbArc* pArc = new AcDbArc(ctr, AcGeVector3d::kZAxis,
                                 rad, startAngle, endAngle);
    postEntity(pArc, el, ms);
}

// ============================================================================
// drawPolyline — two coordinate modes:
//
//   points:   [[x,y], ...]          absolute coordinates
//   segments: [[length,angle_deg],...]  polar — each segment relative to
//                                        the previous point; start: [x,y]
//                                        sets the origin (default posX/posY)
// ============================================================================
void AcmlGenerator::drawPolyline(const ResolvedElement& el,
                                  AcDbBlockTableRecord*  ms)
{
    const double rot = el.getAngle("rotation", 0.0);
    double rc = 0.0, rs = 0.0;
    if (rot != 0.0) { rc = std::cos(rot); rs = std::sin(rot); }

    // Rotate a local-space offset and add the world origin
    auto toWorld = [&](double lx, double ly) -> AcGePoint2d
    {
        if (rot != 0.0)
        {
            double rx = lx * rc - ly * rs;
            double ry = lx * rs + ly * rc;
            lx = rx; ly = ry;
        }
        return AcGePoint2d(el.posX + lx, el.posY + ly);
    };

    // ── segments mode ─────────────────────────────────────────────────────────
    auto segIt = el.props.find("segments");
    if (segIt != el.props.end() && segIt->second.isVec())
    {
        const std::vector<double>& flat = segIt->second.vec;
        // Each segment is stored as a triplet [length, angle_deg, 0]
        int nSegs = (int)(flat.size() / 3);
        if (nSegs < 1)
        {
            addError("Polyline '" + el.id + "' segments list is empty.");
            return;
        }

        // Starting point — from 'start' Vec property, or posX/posY
        double cx = 0.0, cy = 0.0;
        auto startIt = el.props.find("start");
        if (startIt != el.props.end() && startIt->second.isVec())
        {
            cx = startIt->second.vx();
            cy = startIt->second.vy();
        }

        AcDbPolyline* pPoly = new AcDbPolyline(nSegs + 1);
        pPoly->addVertexAt(0, toWorld(cx, cy));

        for (int i = 0; i < nSegs; ++i)
        {
            double length    = flat[i * 3];                           // mm
            double angleDeg  = flat[i * 3 + 1];                      // degrees
            double angleRad  = angleDeg * M_PI / 180.0;
            cx += length * std::cos(angleRad);
            cy += length * std::sin(angleRad);
            pPoly->addVertexAt(i + 1, toWorld(cx, cy));
        }

        pPoly->setClosed(el.getBool("closed", false));
        postEntity(pPoly, el, ms);
        return;
    }

    // ── points mode (absolute coordinates) ───────────────────────────────────
    auto it = el.props.find("points");
    if (it == el.props.end() || !it->second.isVec())
    {
        addError("Polyline '" + el.id + "' needs 'points' or 'segments'.");
        return;
    }

    const std::vector<double>& flat = it->second.vec;
    int nPts = (int)(flat.size() / 3);
    if (nPts < 2)
    {
        addError("Polyline '" + el.id + "' needs at least 2 points.");
        return;
    }

    AcDbPolyline* pPoly = new AcDbPolyline(nPts);
    for (int i = 0; i < nPts; ++i)
        pPoly->addVertexAt(i, toWorld(flat[i * 3], flat[i * 3 + 1]));

    pPoly->setClosed(el.getBool("closed", false));

    double fillet = el.getNum("fillet_radius", 0.0);
    if (fillet > 0.0)
        for (int i = 0; i < nPts; ++i)
            pPoly->setBulgeAt(i, std::tan(std::atan(fillet) / 2.0));

    postEntity(pPoly, el, ms);
}

// ============================================================================
// drawRectangle — Rectangle { width: w; height: h; [corner_radius: r];
//                             [rotation: deg]; [anchor_point: center|...] }
// ============================================================================
void AcmlGenerator::drawRectangle(const ResolvedElement& el,
                                   AcDbBlockTableRecord*  ms)
{
    double w   = el.getNum("width",  kDefaultRectWidth);
    double h   = el.getNum("height", kDefaultRectHeight);
    double cr  = el.getNum("corner_radius", 0.0);
    double rot = el.getAngle("rotation", 0.0);

    // Rotation pivot: defaults to bottom_left (0,0); "center" → (w/2, h/2).
    double pivX = 0.0, pivY = 0.0;
    std::string ap = el.getStr("anchor_point", "bottom_left");
    if (ap == "center")              { pivX = w * 0.5; pivY = h * 0.5; }
    else if (ap == "top_left")       { pivX = 0.0;     pivY = h;       }
    else if (ap == "top_right")      { pivX = w;        pivY = h;       }
    else if (ap == "bottom_right")   { pivX = w;        pivY = 0.0;     }
    else if (ap == "left")           { pivX = 0.0;     pivY = h * 0.5; }
    else if (ap == "right")          { pivX = w;        pivY = h * 0.5; }
    else if (ap == "top")            { pivX = w * 0.5; pivY = h;       }
    else if (ap == "bottom")         { pivX = w * 0.5; pivY = 0.0;     }

    // Rotate a local-space point around the pivot, then offset by world origin.
    auto toWorld = [&](double lx, double ly) -> AcGePoint2d
    {
        lx -= pivX; ly -= pivY;
        if (rot != 0.0)
        {
            double c = std::cos(rot), s = std::sin(rot);
            double rx = lx * c - ly * s, ry = lx * s + ly * c;
            lx = rx; ly = ry;
        }
        return AcGePoint2d(el.posX + pivX + lx, el.posY + pivY + ly);
    };

    cr = (std::min)(cr, (std::min)(w, h) * 0.5);  // clamp to half the shorter side

    AcDbPolyline* pPoly = nullptr;

    if (cr <= 0.0)
    {
        // Plain rectangle — 4 vertices
        pPoly = new AcDbPolyline(4);
        pPoly->addVertexAt(0, toWorld(0,     0    ));
        pPoly->addVertexAt(1, toWorld(w,     0    ));
        pPoly->addVertexAt(2, toWorld(w,     h    ));
        pPoly->addVertexAt(3, toWorld(0,     h    ));
    }
    else
    {
        // Rounded rectangle — 8 vertices, arc bulge on every other segment.
        // Bulge = tan(θ/4) where θ = 90° = π/2 → tan(π/8) ≈ 0.41421356.
        const double bulge = std::tan(M_PI / 8.0);

        pPoly = new AcDbPolyline(8);
        //    bottom edge start  → bottom-right arc → right edge → top-right arc
        //  → top edge          → top-left arc     → left edge  → bottom-left arc
        pPoly->addVertexAt(0, toWorld(cr,     0    )); pPoly->setBulgeAt(0, 0.0);
        pPoly->addVertexAt(1, toWorld(w - cr, 0    )); pPoly->setBulgeAt(1, bulge);
        pPoly->addVertexAt(2, toWorld(w,      cr   )); pPoly->setBulgeAt(2, 0.0);
        pPoly->addVertexAt(3, toWorld(w,      h-cr )); pPoly->setBulgeAt(3, bulge);
        pPoly->addVertexAt(4, toWorld(w - cr, h    )); pPoly->setBulgeAt(4, 0.0);
        pPoly->addVertexAt(5, toWorld(cr,     h    )); pPoly->setBulgeAt(5, bulge);
        pPoly->addVertexAt(6, toWorld(0,      h-cr )); pPoly->setBulgeAt(6, 0.0);
        pPoly->addVertexAt(7, toWorld(0,      cr   )); pPoly->setBulgeAt(7, bulge);
    }

    pPoly->setClosed(true);
    postEntity(pPoly, el, ms);
}

// ============================================================================
// drawEllipse — Ellipse { center:[x,y]; semi_major:a; semi_minor:b; rotation:deg }
// ============================================================================
void AcmlGenerator::drawEllipse(const ResolvedElement& el,
                                 AcDbBlockTableRecord*  ms)
{
    AcGePoint3d ctr       = getPoint(el, "center", "position_x", "position_y");
    double      semiMajor = el.getNum("semi_major", kDefaultEllipseMajor);
    double      semiMinor = el.getNum("semi_minor", kDefaultEllipseMinor);
    double      rotation  = el.getAngle("rotation", 0.0);

    AcGeVector3d majorAxis(semiMajor * std::cos(rotation),
                           semiMajor * std::sin(rotation), 0.0);
    double ratio = (semiMajor > 0.0) ? semiMinor / semiMajor : 0.5;

    AcDbEllipse* pEll = new AcDbEllipse(ctr, AcGeVector3d::kZAxis,
                                         majorAxis, ratio);
    postEntity(pEll, el, ms);
}

// ============================================================================
// drawText — Text { content: "..."; position:[x,y]; height: h; rotation: deg }
// ============================================================================
void AcmlGenerator::drawText(const ResolvedElement& el, AcDbBlockTableRecord* ms)
{
    AcGePoint3d pos     = getPoint(el, "position", "position_x", "position_y");
    std::string content = el.getStr("content", "");
    double      height  = el.getNum("height",  kDefaultTextHeight);
    double      rot     = el.getAngle("rotation", 0.0);

    if (content.empty()) return;

    CA2T wContent(content.c_str(), CP_UTF8);
    AcDbText* pText = new AcDbText(pos, CString(wContent), AcDbObjectId::kNull,
                                   height, rot);
    pText->setHorizontalMode(AcDb::kTextCenter);
    pText->setVerticalMode(AcDb::kTextVertMid);
    pText->setAlignmentPoint(pos);
    postEntity(pText, el, ms);
}

// ============================================================================
// drawWall — plan-view filled outline (closed rectangle)
//
// Properties: width (length), height (thickness), rotation
// ============================================================================
void AcmlGenerator::drawWall(const ResolvedElement& el,
                              AcDbBlockTableRecord*  ms)
{
    // Wall re-uses Rectangle logic — just forward to it.
    drawRectangle(el, ms);
}

// ============================================================================
// drawDoor — plan-view door symbol: leaf line + quarter-circle swing arc
//
// Properties:
//   width       — door leaf width (default 900mm)
//   swing       — "right" (default) or "left"
//   rotation    — element rotation (orientates the whole symbol)
//
// Symbol origin = hinge point (posX, posY).
//
// swing "right":  hinge at left; leaf opens upward (+Y); arc 90°→0°
// swing "left":   hinge at right (posX+width); leaf opens upward; arc 90°→180°
// ============================================================================
void AcmlGenerator::drawDoor(const ResolvedElement& el,
                              AcDbBlockTableRecord*  ms)
{
    const double w    = el.getNum("width", 900.0);
    const double rot  = el.getAngle("rotation", 0.0);
    const bool   left = (el.getStr("swing", "right") == "left");

    // Hinge and tip of the open door leaf in local space (before rotation)
    const double hingeX = left ? el.posX + w : el.posX;
    const double hingeY = el.posY;
    const double tipX   = hingeX;
    const double tipY   = hingeY + w;   // door open at 90° from wall

    // Rotate a local point around the element origin (posX, posY)
    auto toWorld = [&](double lx, double ly) -> AcGePoint3d
    {
        double dx = lx - el.posX, dy = ly - el.posY;
        if (rot != 0.0)
        {
            double c = std::cos(rot), s = std::sin(rot);
            double rx = dx * c - dy * s, ry = dx * s + dy * c;
            dx = rx; dy = ry;
        }
        return AcGePoint3d(el.posX + dx, el.posY + dy, el.posZ);
    };

    // ── Door leaf (line from hinge to open tip) ───────────────────────────────
    AcDbLine* pLeaf = new AcDbLine(toWorld(hingeX, hingeY),
                                   toWorld(tipX,   tipY  ));
    postEntity(pLeaf, el, ms);

    // ── Swing arc (quarter circle, radius = w) ────────────────────────────────
    // Arc centre is the hinge; it sweeps from the open-leaf direction back to
    // the wall face.
    //   right swing: 90° → 0°  (note: AutoCAD arc goes CCW; so start=0, end=90)
    //   left  swing: 90° → 180° (start=90, end=180)
    const double startAng = left ? M_PI / 2.0 : 0.0;
    const double endAng   = left ? M_PI        : M_PI / 2.0;

    // The arc is in local space centred at the hinge; we need to account for
    // the element rotation by constructing it in world space.
    AcGePoint3d centre = toWorld(hingeX, hingeY);
    // Rotate the arc's own start/end angles by the element rotation
    const double arcStart = startAng + rot;
    const double arcEnd   = endAng   + rot;

    AcDbArc* pArc = new AcDbArc(centre, AcGeVector3d::kZAxis,
                                 w, arcStart, arcEnd);
    postEntity(pArc, el, ms);
}

// ============================================================================
// drawWindow — plan-view window symbol: opening + two glass lines
//
// Properties: width (opening), height (wall thickness), rotation
//
//  |──glass──|    ← two lines representing the glass pane, centred in the
//  |         |       wall thickness
// ============================================================================
void AcmlGenerator::drawWindow(const ResolvedElement& el,
                                AcDbBlockTableRecord*  ms)
{
    const double w   = el.getNum("width",  900.0);
    const double h   = el.getNum("height", 150.0);  // wall thickness
    const double rot = el.getAngle("rotation", 0.0);

    auto toWorld = [&](double lx, double ly) -> AcGePoint2d
    {
        double dx = lx, dy = ly;
        if (rot != 0.0)
        {
            double c = std::cos(rot), s = std::sin(rot);
            double rx = dx * c - dy * s, ry = dx * s + dy * c;
            dx = rx; dy = ry;
        }
        return AcGePoint2d(el.posX + dx, el.posY + dy);
    };

    // Outer rectangle (the opening in the wall)
    AcDbPolyline* pBox = new AcDbPolyline(4);
    pBox->addVertexAt(0, toWorld(0, 0));
    pBox->addVertexAt(1, toWorld(w, 0));
    pBox->addVertexAt(2, toWorld(w, h));
    pBox->addVertexAt(3, toWorld(0, h));
    pBox->setClosed(true);
    postEntity(pBox, el, ms);

    // Two glass lines at 1/3 and 2/3 of the wall thickness
    const double y1 = h / 3.0, y2 = h * 2.0 / 3.0;
    AcDbLine* pG1 = new AcDbLine(
        AcGePoint3d(toWorld(0, y1).x, toWorld(0, y1).y, el.posZ),
        AcGePoint3d(toWorld(w, y1).x, toWorld(w, y1).y, el.posZ));
    AcDbLine* pG2 = new AcDbLine(
        AcGePoint3d(toWorld(0, y2).x, toWorld(0, y2).y, el.posZ),
        AcGePoint3d(toWorld(w, y2).x, toWorld(w, y2).y, el.posZ));
    postEntity(pG1, el, ms);
    postEntity(pG2, el, ms);
}

// ============================================================================
// drawColumn — plan-view column cross-section
//
// Properties:
//   width / depth  — rectangular cross-section (default 300 × 300 mm)
//   radius         — if present, draws a circle instead
//   profile        — "circular" also forces circle
// ============================================================================
void AcmlGenerator::drawColumn(const ResolvedElement& el,
                                AcDbBlockTableRecord*  ms)
{
    const std::string profile = el.getStr("profile", "rectangular");
    const bool circular = (profile == "circular") ||
                          (el.props.find("radius") != el.props.end());

    if (circular)
    {
        AcGePoint3d ctr = getPoint(el, "center", "position_x", "position_y");
        double rad = el.getNum("radius", 150.0);
        AcDbCircle* pC = new AcDbCircle(ctr, AcGeVector3d::kZAxis, rad);
        postEntity(pC, el, ms);
    }
    else
    {
        // Square/rectangular — re-use drawRectangle logic
        drawRectangle(el, ms);
    }
}

// ============================================================================
// Helpers
// ============================================================================
bool AcmlGenerator::postEntity(AcDbEntity*           pEnt,
                                const ResolvedElement& el,
                                AcDbBlockTableRecord* ms)
{
    if (!pEnt) return false;

    // ── Layer ────────────────────────────────────────────────────────────────
    // el.layer may be "" when no ancestor or AIA default provided one → use "0".
    // drawElement() already called EnsureLayer with LayerProps before any entity
    // is drawn, so here we only need a plain ensure (no-op if already exists).
    const std::string& layerName = el.layer.empty() ? std::string("0") : el.layer;
    CA2T wLayer(layerName.c_str(), CP_UTF8);
    CadInfra::EnsureLayer(CString(wLayer));
    pEnt->setLayer(CString(wLayer));

    // ── Color ─────────────────────────────────────────────────────────────────
    if (!el.color.empty())
    {
        int r, g, b;
        if (CadInfra::ParseHexColor(el.color, r, g, b) || CadInfra::NamedColorToRGB(el.color, r, g, b))
        {
            AcCmColor acColor;
            acColor.setRGB(static_cast<Adesk::UInt8>(r),
                           static_cast<Adesk::UInt8>(g),
                           static_cast<Adesk::UInt8>(b));
            pEnt->setColor(acColor);
        }
        else
        {
            // Try AutoCAD color index (1–255)
            try {
                int idx = std::stoi(el.color);
                if (idx >= 1 && idx <= 255)
                {
                    AcCmColor acColor;
                    acColor.setColorIndex(static_cast<Adesk::UInt16>(idx));
                    pEnt->setColor(acColor);
                }
            } catch (...) {}
        }
    }

    // ── Linetype ──────────────────────────────────────────────────────────────
    if (!el.linetype.empty())
    {
        CA2T wLt(el.linetype.c_str(), CP_UTF8);
        CadInfra::EnsureLinetype(CString(wLt));
        pEnt->setLinetype(CString(wLt));
    }

    // ── Lineweight ────────────────────────────────────────────────────────────
    if (el.lineweight >= 0.0)
        pEnt->setLineWeight(CadInfra::MmToLineWeight(el.lineweight));

    // Apply inherited rotation from ancestor elements.
    // The draw functions already handle the element's own `rotation` property;
    // here we only apply the accumulated rotation from parent containers.
    // The pivot honours anchor_point (defaults to posX/posY = bottom_left).
    if (el.inheritedRotZ != 0.0)
    {
        double px, py;
        el.getPivot(px, py);
        AcGeMatrix3d xform = AcGeMatrix3d::rotation(el.inheritedRotZ,
                                                     AcGeVector3d::kZAxis,
                                                     AcGePoint3d(px, py, el.posZ));
        pEnt->transformBy(xform);
    }

    if (ms->appendAcDbEntity(pEnt) != Acad::eOk)
    {
        delete pEnt;
        addError("Failed to append entity to model space.");
        return false;
    }
    pEnt->close();
    ++count_;
    return true;
}

AcGePoint3d AcmlGenerator::getPoint(const ResolvedElement& el,
                                     const std::string&     vecKey,
                                     const std::string&     xKey,
                                     const std::string&     yKey) const
{
    // All vec coordinates are relative to the element's own origin (el.posX/posY),
    // which resolveElement has already set to the absolute world position.
    auto it = el.props.find(vecKey);
    if (it != el.props.end() && it->second.isVec())
        return AcGePoint3d(el.posX + it->second.vx(),
                           el.posY + it->second.vy(),
                           el.posZ + it->second.vz());

    // Fallback: el.posX/posY already incorporate position_x/position_y
    return AcGePoint3d(el.posX, el.posY, el.posZ);
}

void AcmlGenerator::addError(const std::string& msg)
{
    errors_.push_back("ACML Generator: " + msg);
}

} // namespace Acml
