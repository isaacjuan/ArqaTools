#pragma once
#include "StdAfx.h"
#include "dbmain.h"   // AcDb::UnitsValue

// ============================================================================
// MeasureFormat — pure formatting of area and length values.
//
// No ARX runtime services are used here (no database queries, no entity
// access). Callers are responsible for obtaining the drawing units value
// (e.g. pDb->insunits()) and passing it in.
// ============================================================================
namespace MeasureFormat
{
    // Format an area value with thousands separator and squared unit suffix.
    // Millimeter drawings are automatically converted to m² for display.
    CString FormatArea(double area, AcDb::UnitsValue units);

    // Format a linear length value with thousands separator.
    //   rawUnits = true  → display in native drawing units (e.g. mm)
    //             false  → convert mm → m for display
    //   noSuffix = true  → omit the unit label (for segment text labels)
    CString FormatLength(double length, AcDb::UnitsValue units,
                         bool rawUnits = false, bool noSuffix = false);
}
