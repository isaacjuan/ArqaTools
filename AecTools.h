#pragma once
#include "StdAfx.h"
#include <vector>

namespace AecTools
{
    // Reads AutoCAD Architecture object properties (doors, windows, walls,
    // spaces, ...) through the ACA COM interfaces with late binding, so the
    // plugin has no link-time dependency on ACA and still loads in plain
    // AutoCAD (where every call simply reports "not an AutoCAD Architecture
    // object"). Non-interactive core for at.getAecProps (LuaTools.cpp).

    struct Field
    {
        enum Kind { Number, String, Bool, Point };
        CString     name;      // Lua field name
        Kind        kind = Number;
        double      num  = 0.0;
        CString     str;
        bool        b    = false;
        AcGePoint3d pt;
    };

    struct Props
    {
        CString            kind;        // "door", "window", "opening", "wall", "space", "aec"
        CString            className;   // e.g. AecDbDoor
        bool               hasCenter = false;
        AcGePoint3d        center;      // WCS centre of the geometric extents
        std::vector<Field> fields;
    };

    // True when the class is an ACA (Aec*) object.
    bool IsAecClass(const AcRxClass* pClass);

    // Fills `out` with the standard fields for the object's kind, plus every
    // COM property named in `extra` (field name = the COM name as given).
    // Properties the object does not have, or whose value is not a number,
    // string, boolean or point, are skipped. Must run on the main thread.
    bool ReadProps(AcDbObjectId id, const std::vector<CString>& extra,
                   Props& out, CString& err);
}
