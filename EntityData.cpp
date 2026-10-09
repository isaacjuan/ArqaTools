#include "StdAfx.h"
#include "EntityData.h"
#include "CommonTools.h"
#include "CadInfra.h"
#include <cmath>

namespace EntityData
{
const TCHAR* const APP_NAME = _T("ARQATOOLS_DATA");

namespace
{
    const int kMaxString = 255;

    // Parses the app's xdata chain (starting at its 1001 entry) into pairs.
    void Parse(const resbuf* rb, Map& out)
    {
        out.clear();
        const resbuf* p = rb ? rb->rbnext : nullptr;   // skip 1001 app name
        while (p && p->rbnext)
        {
            if (p->restype != AcDb::kDxfXdAsciiString) { p = p->rbnext; continue; }
            CString key = p->resval.rstring;
            const resbuf* v = p->rbnext;
            Value val;
            bool ok = true;
            switch (v->restype)
            {
            case AcDb::kDxfXdAsciiString: val.kind = Value::String; val.str = v->resval.rstring; break;
            case AcDb::kDxfXdReal:        val.kind = Value::Number; val.num = v->resval.rreal;   break;
            case AcDb::kDxfXdInteger32:   val.kind = Value::Bool;   val.b   = v->resval.rlong != 0; break;
            default: ok = false; break;
            }
            if (ok) out.emplace_back(key, val);
            p = v->rbnext;
        }
    }

    // Builds the full xdata chain for the map (just the 1001 entry when empty,
    // which removes the app's xdata from the entity).
    resbuf* Build(const Map& map)
    {
        resbuf* head = acutBuildList(AcDb::kDxfRegAppName, APP_NAME, RTNONE);
        resbuf* tail = head;
        for (const auto& kv : map)
        {
            resbuf* pair = nullptr;
            switch (kv.second.kind)
            {
            case Value::String:
                pair = acutBuildList(AcDb::kDxfXdAsciiString, (LPCTSTR)kv.first,
                                     AcDb::kDxfXdAsciiString, (LPCTSTR)kv.second.str, RTNONE);
                break;
            case Value::Number:
                pair = acutBuildList(AcDb::kDxfXdAsciiString, (LPCTSTR)kv.first,
                                     AcDb::kDxfXdReal, kv.second.num, RTNONE);
                break;
            case Value::Bool:
                pair = acutBuildList(AcDb::kDxfXdAsciiString, (LPCTSTR)kv.first,
                                     AcDb::kDxfXdInteger32, kv.second.b ? 1 : 0, RTNONE);
                break;
            }
            if (!pair) continue;
            tail->rbnext = pair;
            while (tail->rbnext) tail = tail->rbnext;
        }
        return head;
    }

    bool Equal(const Value& a, const Value& b)
    {
        if (a.kind != b.kind) return false;
        switch (a.kind)
        {
        case Value::String: return a.str.CompareNoCase(b.str) == 0;
        case Value::Number: return std::fabs(a.num - b.num) <= 1e-9 * (1.0 + std::fabs(a.num));
        case Value::Bool:   return a.b == b.b;
        }
        return false;
    }
}

bool Read(AcDbObjectId id, Map& out, CString& err)
{
    out.clear();
    CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
    if (!ent) { err = _T("handle not found"); return false; }
    resbuf* rb = ent->xData(APP_NAME);
    Parse(rb, out);
    if (rb) acutRelRb(rb);
    return true;
}

bool Set(AcDbObjectId id, const CString& key, const Value* value, CString& err)
{
    if (key.IsEmpty() || key.GetLength() > kMaxString)
    {
        err = _T("key must be 1 to 255 characters");
        return false;
    }
    if (value && value->kind == Value::String && value->str.GetLength() > kMaxString)
    {
        err = _T("string values are limited to 255 characters");
        return false;
    }

    Map map;
    if (!Read(id, map, err)) return false;

    auto it = map.begin();
    for (; it != map.end(); ++it)
        if (it->first == key) break;
    if (value)
    {
        if (it != map.end()) it->second = *value;
        else map.emplace_back(key, *value);
    }
    else if (it != map.end())
        map.erase(it);
    else
        return true;   // removing a key that is not there

    CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
    if (!ent) { err = _T("could not open the entity for write"); return false; }
    CadInfra::EnsureAppRegistered(ent->database(), APP_NAME);
    resbuf* rb = Build(map);
    Acad::ErrorStatus es = ent->setXData(rb);
    acutRelRb(rb);
    if (es != Acad::eOk) { err = _T("could not write xdata (over the 16 KB limit?)"); return false; }
    return true;
}

std::vector<AcDbObjectId> FindWith(const CString& key, const Value* value)
{
    std::vector<AcDbObjectId> out;
    for (AcDbObjectId id : CommonTools::ModelSpaceIds())
    {
        Map map;
        CString err;
        if (!Read(id, map, err)) continue;
        for (const auto& kv : map)
            if (kv.first == key && (!value || Equal(kv.second, *value)))
            {
                out.push_back(id);
                break;
            }
    }
    return out;
}
}
