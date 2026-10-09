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

    // The candidates whose geometric-extents centre is inside the boundary
    // (within tol). The boundary itself is skipped.
    std::vector<AcDbObjectId> FilterInside(AcDbObjectId boundary, const Outline& poly,
                                           const std::vector<AcDbObjectId>& candidates,
                                           double tol);
}
