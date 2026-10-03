// PolylineTools.h - Polyline and Boolean Operations

#pragma once

#include "StdAfx.h"

namespace PolylineTools
{
    // Boolean operations
    void subtractPolyCommand();
    void intersectPolyCommand();
    void unionPolyCommand();
    void booleanPolyCommand();
    
    // Region to polyline conversion
    void regionToPolyCommand();

    // Non-interactive cores (also used by the Lua bindings). On failure they
    // return kNull and, when err is given, a short reason.
    // Boolean of two closed polylines -> new region in model space (first
    // operand is the one kept for subtract).
    AcDbObjectId BooleanPolylines(AcDbObjectId first, AcDbObjectId second,
                                  AcDb::BoolOperType op, CString* err = nullptr);
    // Region -> new closed polyline in model space (region is left in place).
    AcDbObjectId RegionToPolyline(AcDbObjectId regionId, CString* err = nullptr);
}
