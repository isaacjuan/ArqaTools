#pragma once

// The interactive commands (ATGOLDENRECT, ATGOLDENRECTIN, ATGOLDENRECTINW) are
// Lua: LuaCommands\ATGOLDENRECT.lua, built on at.goldenSpiral, at.rectFrame and
// at.rectInFrame in LuaTools.cpp.
namespace GoldenRectTools
{
    // Non-interactive core of ATGOLDENRECT: golden-spiral rectangles whose first
    // side runs corner -> sidePt. Returns false if the points coincide.
    bool DrawGoldenSpiral(const AcGePoint3d& corner, const AcGePoint3d& sidePt);

    // A closed 4-vertex polyline read as a rectangle: vertex 0 is the origin,
    // edge 0->1 is "width", edge 0->3 is "height".
    struct RectFrame
    {
        AcGePoint3d  origin;      // vertex 0
        AcGeVector3d shortVec;    // the whole short edge, from vertex 0
        AcGeVector3d longDir;     // unit vector along the long edge
        double       shortLen = 0.0;
        double       longLen  = 0.0;
        double       width    = 0.0;   // |v1 - v0|
        double       height   = 0.0;   // |v3 - v0|
    };

    // Non-interactive core of ATGOLDENRECTIN/INW: reads a closed rectangular
    // polyline. On failure returns false and sets err (e.g. "Polyline must be
    // closed (rectangle expected).").
    bool ReadRectFrame(const AcDbObjectId& id, RectFrame& frame, CString& err);

    // Draws a closed polyline rectangle spanning the frame's whole short edge and
    // innerWidth along the long edge, centered on the pick point's projection on
    // the long edge and clamped to stay inside the frame. Elevation 0, as the
    // original command. Returns the new polyline id (null on failure).
    AcDbObjectId DrawRectInFrame(const RectFrame& frame, double innerWidth,
                                 const AcGePoint3d& pick);
}
