// AcmlGenerator.h — ACML geometry generator
//
// Walks a list of ResolvedElement structs and emits AutoCAD entities
// directly into the active document's model space via ObjectARX.
// Handles: Line, Circle, Arc, Polyline, Rectangle, Ellipse, Text,
//          Wall, Door, Window, Column.

#pragma once
#include "StdAfx.h"
#include "AcmlSemantic.h"
#include "CadInfra.h"
#include "dbents.h"
#include "dbpl.h"
#include "dbmtext.h"
#include <string>
#include <vector>

namespace Acml
{

class AcmlGenerator
{
public:
    // -------------------------------------------------------------------------
    // generate — draws all elements (and their children) into model space.
    // Must be called from within a command context.
    // Returns the number of entities created.
    // -------------------------------------------------------------------------
    int generate(const std::vector<ResolvedElement>& elements);

    bool                            hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors()    const { return errors_; }

private:
    std::vector<std::string> errors_;
    int                      count_ = 0;

    // ── Dispatch ──────────────────────────────────────────────────────────────
    void drawElement(const ResolvedElement& el, AcDbBlockTableRecord* ms);

    // ── Primitive element drawers ─────────────────────────────────────────────
    void drawLine      (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawCircle    (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawArc       (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawPolyline  (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawRectangle (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawEllipse   (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawText      (const ResolvedElement& el, AcDbBlockTableRecord* ms);

    // ── Architectural element drawers (plan-view symbols) ─────────────────────
    // Wall   : closed rectangle (length × thickness)
    // Door   : door-leaf line + quarter-circle swing arc
    // Window : wall opening with two glass lines
    // Column : square or circular cross-section
    void drawWall   (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawDoor   (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawWindow (const ResolvedElement& el, AcDbBlockTableRecord* ms);
    void drawColumn (const ResolvedElement& el, AcDbBlockTableRecord* ms);

    // ── Helpers ───────────────────────────────────────────────────────────────
    // Apply layer, post entity to model space, apply inherited transform, close it.
    bool postEntity(AcDbEntity* pEnt, const ResolvedElement& el,
                    AcDbBlockTableRecord* ms);

    // Get a 3D point from Vec-valued property; fallback to posX/posY/posZ.
    AcGePoint3d getPoint(const ResolvedElement& el,
                         const std::string& vecKey,
                         const std::string& xKey = "",
                         const std::string& yKey = "") const;

    void addError(const std::string& msg);
};

} // namespace Acml
