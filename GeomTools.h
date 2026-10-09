#pragma once
#include "StdAfx.h"
#include <vector>

namespace GeomTools
{
    // Non-interactive cores for at.pointInPolygon / at.entitiesInside
    // (LuaTools.cpp): "which room is this door, window or fixture in".

    // A closed boundary flattened to a polygon in the WCS XY plane. Straight
    // segments keep their exact vertices; arcs and other curves are
    // tessellated (chord error well under 1% of the radius).
    using Outline = std::vector<AcGePoint2d>;

    // Reads a closed curve (polyline, circle, ellipse, spline, region-like
    // ACA space, ...) as a polygon. Fails for open curves and non-curves.
    bool GetBoundary(AcDbObjectId id, Outline& poly, CString& err);

    // Inside test (even-odd) plus the distance from p to the boundary edges.
    // With tol > 0 a point outside but within tol of an edge counts as
    // inside (a door sits in the wall, just outside the room's face).
    bool Contains(const Outline& poly, const AcGePoint2d& p, double tol, double* dist = nullptr);

    // Any object's footprint in the XY plane: curves (open or closed) as
    // their tessellated outline, ACA objects other than spaces (walls are
    // curves along their baseline) and everything else as the rectangle of
    // their geometric extents (exact for axis-aligned walls and fixtures).
    bool GetOutline(AcDbObjectId id, Outline& pts, bool& closed, CString& err);

    // Clear distance between two outlines (0 when they touch, cross or one
    // lies inside the other), with the closest points.
    double Distance(const Outline& a, bool aClosed, const Outline& b, bool bClosed,
                    AcGePoint2d* pa = nullptr, AcGePoint2d* pb = nullptr);

    // Largest circle inside a closed outline (its diameter is the room's clear
    // width: the short side of a rectangle at any angle, the main body's width
    // of an irregular room). Grid search refined by hill climbing to ~0.5 unit.
    bool InscribedCircle(const Outline& poly, AcGePoint2d& centre, double& radius);

    // Accessibility of a room for a person `passWidth` wide, sampled on a grid
    // (`grid` units; 0 = automatic, at most ~40000 samples). A point is usable
    // when a disc of that width covering it fits inside the room. Returns the
    // usable share of the floor, the area lost, and how many separate parts
    // the usable floor splits into (more than one: part of the room is reached
    // only through a gap narrower than passWidth).
    struct Usability
    {
        double fraction = 0.0;
        double lostArea = 0.0;   // drawing units squared
        int    parts    = 0;
        double grid     = 0.0;
    };
    bool Usable(const Outline& poly, double passWidth, double grid, Usability& out);

    // The candidates whose geometric-extents centre is inside the boundary
    // (within tol). The boundary itself is skipped.
    std::vector<AcDbObjectId> FilterInside(AcDbObjectId boundary, const Outline& poly,
                                           const std::vector<AcDbObjectId>& candidates,
                                           double tol);
}
