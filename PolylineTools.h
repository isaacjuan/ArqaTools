// PolylineTools.h - Polyline and Boolean Operations

#pragma once

#include "StdAfx.h"

namespace PolylineTools
{
    // Non-interactive cores (also used by the Lua bindings). On failure they
    // return kNull and, when err is given, a short reason.
    // Boolean of two closed curves (polylines, circles, ellipses, closed
    // splines) -> new region in model space (first operand is the one kept for
    // subtract). An empty result (no overlap for intersect) fails.
    AcDbObjectId BooleanPolylines(AcDbObjectId first, AcDbObjectId second,
                                  AcDb::BoolOperType op, CString* err = nullptr);
    // Region -> new closed polyline in model space (region is left in place).
    AcDbObjectId RegionToPolyline(AcDbObjectId regionId, CString* err = nullptr);
}
