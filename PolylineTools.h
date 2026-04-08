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
}
