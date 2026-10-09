#pragma once
#include "StdAfx.h"
#include <utility>
#include <vector>

namespace EntityData
{
    // Key/value tags on any entity (room boundaries, ACA spaces, fixtures),
    // stored as xdata under the ARQATOOLS_DATA app so they travel with the
    // DWG and with copies. Non-interactive core for at.getData / at.setData /
    // at.findByData (LuaTools.cpp).
    //
    // Layout: pairs of (1000 key)(value), value 1000 = string, 1040 = number,
    // 1071 = boolean (0/1). Keys and string values are at most 255 characters
    // (an xdata string limit).

    extern const TCHAR* const APP_NAME;

    struct Value
    {
        enum Kind { String, Number, Bool };
        Kind    kind = String;
        CString str;
        double  num = 0.0;
        bool    b   = false;
    };

    using Map = std::vector<std::pair<CString, Value>>;   // insertion order kept

    // Every tag on the entity (empty map when it has none).
    bool Read(AcDbObjectId id, Map& out, CString& err);

    // Sets key to *value, or removes it when value is null. Other keys are kept.
    bool Set(AcDbObjectId id, const CString& key, const Value* value, CString& err);

    // Model-space entities that carry key (and, with value, that exact value;
    // strings compare case-insensitively).
    std::vector<AcDbObjectId> FindWith(const CString& key, const Value* value);
}
