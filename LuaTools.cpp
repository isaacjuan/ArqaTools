#include "StdAfx.h"
#include "LuaTools.h"
#include "AITools.h"
#include "CommonTools.h"
#include "CategorizeTools.h"
#include "CadInfra.h"
#include "MeasureFormat.h"
#include "ArabesqueTools.h"
#include "GoldenRectTools.h"
#include "PolylineTools.h"
#include "AlignTools.h"
#include "SeqNumTools.h"
#include "DistributeTools.h"
#include "TextTools.h"
#include "AreaTools.h"
#include "LayerTools.h"
#include "SvgExportTools.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "dbmain.h"
#include "dbents.h"
#include "dbsymtb.h"
#include "dbxutil.h"
#include <sstream>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace LuaTools
{

// ─────────────────────────────────────────────────────────────────────────────
// Per-run context, stashed as a lightuserdata upvalue on every at.* C function
// and on the overridden global print(). Each runLuaScript() call gets its own
// lua_State and its own LuaCtx instance - there is no shared/static state, so
// this stays safe even though (unlike DevTools) nothing here enforces
// main-thread-only access; this project simply has no other thread that could
// call in concurrently (confirmed: no CreateThread/std::thread anywhere).
// ─────────────────────────────────────────────────────────────────────────────
struct LuaCtx
{
    std::string output;
    bool        cancelled       = false;
    long long   instructions    = 0;
    long long   maxInstructions = 0;   // 0 = unlimited
    bool        echo            = false;   // print() -> command line, not the buffer
    bool        loading         = false;   // a command file's top level is running
    DefineCommandFn defineFn    = nullptr;
    void*           defineUser  = nullptr;
};

namespace {

LuaCtx* ctx_from(lua_State* L)
{
    return static_cast<LuaCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
}

// ─────────────────────────────────────────────────────────────────────────────
// IMPORTANT for every binding below: the vendored Lua is compiled as plain C,
// so luaL_error/luaL_check* longjmp straight past C++ frames without running
// destructors. Do all argument checking BEFORE opening any AcDbObjectGuard /
// creating a CString, and raise errors only after such scopes have closed -
// otherwise an object is left open or memory leaks.
// ─────────────────────────────────────────────────────────────────────────────

// Raises the "cancelled" error (after the caller's RAII scopes have closed).
int RaiseCancelled(lua_State* L)
{
    ctx_from(L)->cancelled = true;
    return luaL_error(L, "cancelled by user");
}

// UTF-8 prompt from Lua -> AutoCAD prompt on a fresh command-line line. When a
// default is shown, "Prompt: " becomes "Prompt <default>: ".
CString MakePrompt(const char* utf8, const CString& defaultText = CString())
{
    CString p(CA2T(utf8, CP_UTF8));
    if (!defaultText.IsEmpty())
    {
        p.TrimRight();
        p.TrimRight(_T(':'));
        p.TrimRight();
        p += _T(" <") + defaultText + _T(">: ");
    }
    else if (!p.IsEmpty() && !_istspace(p[p.GetLength() - 1]))
        p += _T(" ");   // AI-written prompts often end in ':' with no space
    return _T("\n") + p;
}

std::string ToUtf8(const TCHAR* s)
{
    CT2A narrow(s, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

// t[key] = {x=, y=, z=} on the table at the top of the stack.
void SetPointField(lua_State* L, const char* key, const AcGePoint3d& p)
{
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, p.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, p.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, p.z); lua_setfield(L, -2, "z");
    lua_setfield(L, -2, key);
}

void SetNumberField(lua_State* L, const char* key, double v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

void SetStringField(lua_State* L, const char* key, const TCHAR* v)
{
    std::string s = ToUtf8(v ? v : _T(""));
    lua_pushlstring(L, s.data(), s.size());
    lua_setfield(L, -2, key);
}

// acedGetPoint & co. return UCS coordinates; everything in the at API
// (drawLine, moveEntity, getProps, …) works in WCS.
AcGePoint3d UcsToWcs(const ads_point p)
{
    ads_point u = { p[X], p[Y], p[Z] }, w;
    acdbUcs2Wcs(u, w, false);
    return AcGePoint3d(w[X], w[Y], w[Z]);
}

void WcsToUcs(const AcGePoint3d& p, ads_point out)
{
    ads_point w = { p.x, p.y, p.z };
    acdbWcs2Ucs(w, out, false);
}

double RadToDeg(double r) { return r * 180.0 / M_PI; }

// Case-insensitive match of an entity class against a filter like "LINE"
// (DXF name) or "AcDbLine" (ObjectARX class name).
bool ClassMatches(AcRxClass* pClass, const CString& filter)
{
    if (filter.IsEmpty()) return true;
    if (!pClass) return false;
    if (filter.CompareNoCase(pClass->name()) == 0) return true;
    const ACHAR* dxf = pClass->dxfName();
    return dxf && filter.CompareNoCase(dxf) == 0;
}

// Local copy of the AppendToModelSpace pattern used throughout this project
// (GoldenRectTools.cpp, CadInfra.cpp) - returns the new entity's ObjectId so
// callers can hand a handle string back to Lua.
AcDbObjectId AppendToModelSpace(AcDbEntity* pEnt)
{
    AcDbBlockTableRecord* pModelSpace = nullptr;
    if (CommonTools::GetModelSpace(pModelSpace) != Acad::eOk)
    {
        delete pEnt;
        return AcDbObjectId::kNull;
    }
    AcDbObjectId id;
    if (pModelSpace->appendAcDbEntity(id, pEnt) != Acad::eOk)
    {
        pModelSpace->close();
        delete pEnt;
        return AcDbObjectId::kNull;
    }
    pEnt->close();
    pModelSpace->close();
    return id;
}

// Resolves a hex handle string (as returned by at.drawLine/at.copyEntity, …)
// to an AcDbObjectId in the working database. AcDbHandle(const ACHAR*) parses
// the ascii hex digits directly (confirmed usage: ReactorPersistence.cpp:102,
// CadInfra.cpp:386); AcDbDatabase::getAcDbObjectId does the handle -> id
// lookup (confirmed usage: ReactorPersistence.cpp:103).
AcDbObjectId ResolveHandle(const std::string& hex)
{
    if (hex.empty())
        return AcDbObjectId::kNull;

    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    if (!pDb)
        return AcDbObjectId::kNull;

    CA2T wHex(hex.c_str(), CP_UTF8);
    AcDbHandle h(static_cast<LPCTSTR>(wHex));

    AcDbObjectId id;
    if (pDb->getAcDbObjectId(id, Adesk::kFalse, h) != Acad::eOk)
        return AcDbObjectId::kNull;
    return id;
}

// Reverse direction: ObjectId -> ascii hex handle string (confirmed usage:
// CadInfra.cpp:316/339 - objId.handle().getIntoAsciiBuffer(buf)).
std::string HandleToString(const AcDbObjectId& id)
{
    TCHAR buf[AcDbHandle::kStrSiz];
    id.handle().getIntoAsciiBuffer(buf, AcDbHandle::kStrSiz);
    CT2A narrow(buf, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

void PushHandle(lua_State* L, const AcDbObjectId& id)
{
    std::string h = HandleToString(id);
    lua_pushlstring(L, h.data(), h.size());
}

void PushIdList(lua_State* L, const std::vector<AcDbObjectId>& ids)
{
    lua_createtable(L, static_cast<int>(ids.size()), 0);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        PushHandle(L, ids[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
}

// Records the database's last entity on construction; collect() then walks
// everything appended after it. Lets void "draw a pattern" core functions
// (ArabesqueTools, GoldenRectTools) hand Lua the handles they created without
// changing their signatures.
class NewEntityTracker
{
public:
    NewEntityTracker() { m_hasLast = (acdbEntLast(m_last) == RTNORM); }

    std::vector<AcDbObjectId> collect() const
    {
        std::vector<AcDbObjectId> ids;
        ads_name cur, next;
        int rc = m_hasLast ? acdbEntNext(m_last, next) : acdbEntNext(nullptr, next);
        while (rc == RTNORM)
        {
            AcDbObjectId id;
            if (acdbGetObjectId(id, next) == Acad::eOk)
                ids.push_back(id);
            cur[0] = next[0];
            cur[1] = next[1];
            rc = acdbEntNext(cur, next);
        }
        return ids;
    }

private:
    ads_name m_last;
    bool     m_hasLast = false;
};

// Runs a void drawing function and pushes the table of handles it created.
template <typename Fn>
int DrawAndCollect(lua_State* L, Fn&& draw)
{
    std::vector<AcDbObjectId> ids;
    {
        NewEntityTracker tracker;
        draw();
        ids = tracker.collect();
    }
    PushIdList(L, ids);
    return 1;
}

// ── at.* C function bindings ────────────────────────────────────────────────

int at_listEntities(lua_State* L)
{
    CategorizeTools::DBObjectMap dbmap;   // default ctor -> workingDatabase()
    std::ostringstream ss;
    for (const auto& typeName : dbmap.getTypeNames())
    {
        CT2A narrow(typeName.c_str(), CP_UTF8);
        ss << static_cast<const char*>(narrow) << ": " << dbmap.getCount(typeName) << "\n";
    }
    std::string s = ss.str();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int at_drawLine(lua_State* L)
{
    double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), z1 = luaL_checknumber(L, 3);
    double x2 = luaL_checknumber(L, 4), y2 = luaL_checknumber(L, 5), z2 = luaL_checknumber(L, 6);

    AcDbLine* pLine = new AcDbLine(AcGePoint3d(x1, y1, z1), AcGePoint3d(x2, y2, z2));
    AcDbObjectId id = AppendToModelSpace(pLine);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }

    std::string h = HandleToString(id);
    lua_pushlstring(L, h.data(), h.size());
    return 1;
}

int at_drawCircle(lua_State* L)
{
    double cx = luaL_checknumber(L, 1), cy = luaL_checknumber(L, 2), cz = luaL_checknumber(L, 3);
    double r  = luaL_checknumber(L, 4);

    AcDbCircle* pCircle = new AcDbCircle(AcGePoint3d(cx, cy, cz), AcGeVector3d::kZAxis, r);
    AcDbObjectId id = AppendToModelSpace(pCircle);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }

    std::string h = HandleToString(id);
    lua_pushlstring(L, h.data(), h.size());
    return 1;
}

int at_drawArc(lua_State* L)
{
    double cx = luaL_checknumber(L, 1), cy = luaL_checknumber(L, 2), cz = luaL_checknumber(L, 3);
    double r  = luaL_checknumber(L, 4);
    double startDeg = luaL_checknumber(L, 5), endDeg = luaL_checknumber(L, 6);

    AcDbArc* pArc = new AcDbArc(AcGePoint3d(cx, cy, cz), AcGeVector3d::kZAxis, r,
                                 startDeg * M_PI / 180.0, endDeg * M_PI / 180.0);
    AcDbObjectId id = AppendToModelSpace(pArc);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }

    std::string h = HandleToString(id);
    lua_pushlstring(L, h.data(), h.size());
    return 1;
}

int at_drawRect(lua_State* L)
{
    double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), z1 = luaL_checknumber(L, 3);
    double x2 = luaL_checknumber(L, 4), y2 = luaL_checknumber(L, 5), z2 = luaL_checknumber(L, 6);

    AcDbPolyline* pPl = new AcDbPolyline();
    pPl->addVertexAt(0, AcGePoint2d(x1, y1));
    pPl->addVertexAt(1, AcGePoint2d(x2, y1));
    pPl->addVertexAt(2, AcGePoint2d(x2, y2));
    pPl->addVertexAt(3, AcGePoint2d(x1, y2));
    pPl->setClosed(true);
    pPl->setElevation((z1 + z2) * 0.5);

    AcDbObjectId id = AppendToModelSpace(pPl);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }

    std::string h = HandleToString(id);
    lua_pushlstring(L, h.data(), h.size());
    return 1;
}

int at_moveEntity(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    double dx = luaL_checknumber(L, 2), dy = luaL_checknumber(L, 3), dz = luaL_checknumber(L, 4);

    AcDbObjectId id = ResolveHandle(handle);
    if (id.isNull()) { lua_pushboolean(L, 0); lua_pushstring(L, "handle not found"); return 2; }

    AcDbObjectIdArray processedGroups;
    CommonTools::MoveEntityOrGroup(id, AcGeVector3d(dx, dy, dz), processedGroups);
    lua_pushboolean(L, 1);
    return 1;
}

int at_copyEntity(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    double dx = luaL_checknumber(L, 2), dy = luaL_checknumber(L, 3), dz = luaL_checknumber(L, 4);

    AcDbObjectId id = ResolveHandle(handle);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "handle not found"); return 2; }

    AcGePoint3d refPt;
    if (!CommonTools::GetEntityReferencePoint(id, refPt))
    { lua_pushnil(L); lua_pushstring(L, "could not determine reference point"); return 2; }

    AcGePoint3d target = refPt + AcGeVector3d(dx, dy, dz);
    AcDbObjectId newId = CommonTools::CopyEntityTo(id, target);
    if (newId.isNull()) { lua_pushnil(L); lua_pushstring(L, "copy failed"); return 2; }

    std::string h = HandleToString(newId);
    lua_pushlstring(L, h.data(), h.size());
    return 1;
}

int at_rotateEntity(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    double cx = luaL_checknumber(L, 2), cy = luaL_checknumber(L, 3), cz = luaL_checknumber(L, 4);
    double angleDeg = luaL_checknumber(L, 5);

    AcDbObjectId id = ResolveHandle(handle);
    if (id.isNull()) { lua_pushboolean(L, 0); lua_pushstring(L, "handle not found"); return 2; }

    CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
    if (!ent) { lua_pushboolean(L, 0); lua_pushstring(L, "could not open entity"); return 2; }

    AcGeMatrix3d xfm = AcGeMatrix3d::rotation(angleDeg * M_PI / 180.0,
                                               AcGeVector3d::kZAxis,
                                               AcGePoint3d(cx, cy, cz));
    ent->transformBy(xfm);
    lua_pushboolean(L, 1);
    return 1;
}

// ── User input ──────────────────────────────────────────────────────────────
// Every prompt follows the same convention: ESC raises "cancelled by user"
// (aborting the script, reported as cancelled rather than as an error), Enter
// with no input returns the default when one was given, else nil.

// at.getPoint([prompt [, bx, by, bz]]) -> x, y, z (WCS) | nil
int at_getPoint(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Specify point: ");
    bool hasBase = !lua_isnoneornil(L, 2);
    AcGePoint3d base;
    if (hasBase)
        base.set(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 0.0));

    int rc;
    ads_point pt;
    {
        CString wPrompt = MakePrompt(prompt);
        ads_point ucsBase;
        if (hasBase) WcsToUcs(base, ucsBase);
        rc = acedGetPoint(hasBase ? ucsBase : nullptr, wPrompt, pt);
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc != RTNORM) { lua_pushnil(L); return 1; }

    AcGePoint3d w = UcsToWcs(pt);
    lua_pushnumber(L, w.x); lua_pushnumber(L, w.y); lua_pushnumber(L, w.z);
    return 3;
}

// at.getDistance([prompt [, bx, by, bz]]) -> number | nil
int at_getDistance(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Specify distance: ");
    bool hasBase = !lua_isnoneornil(L, 2);
    AcGePoint3d base;
    if (hasBase)
        base.set(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 0.0));

    int rc;
    double d = 0.0;
    {
        CString wPrompt = MakePrompt(prompt);
        ads_point ucsBase;
        if (hasBase) WcsToUcs(base, ucsBase);
        rc = acedGetDist(hasBase ? ucsBase : nullptr, wPrompt, &d);
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc != RTNORM) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, d);
    return 1;
}

// at.getReal([prompt [, default]]) -> number | nil
int at_getReal(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Enter a number: ");
    bool hasDef = !lua_isnoneornil(L, 2);
    double def = hasDef ? luaL_checknumber(L, 2) : 0.0;

    int rc;
    double v = 0.0;
    {
        CString defText;
        if (hasDef) defText.Format(_T("%g"), def);
        CString wPrompt = MakePrompt(prompt, defText);
        acedInitGet(hasDef ? 0 : RSG_NONULL, nullptr);
        rc = acedGetReal(wPrompt, &v);
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc == RTNONE && hasDef) v = def;
    else if (rc != RTNORM) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, v);
    return 1;
}

// at.getInt([prompt [, default]]) -> integer | nil
int at_getInt(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Enter an integer: ");
    bool hasDef = !lua_isnoneornil(L, 2);
    int def = hasDef ? static_cast<int>(luaL_checkinteger(L, 2)) : 0;

    int rc;
    int v = 0;
    {
        CString defText;
        if (hasDef) defText.Format(_T("%d"), def);
        CString wPrompt = MakePrompt(prompt, defText);
        acedInitGet(hasDef ? 0 : RSG_NONULL, nullptr);
        rc = acedGetInt(wPrompt, v);
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc == RTNONE && hasDef) v = def;
    else if (rc != RTNORM) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, v);
    return 1;
}

// at.getString([prompt [, default]]) -> string (spaces allowed)
int at_getString(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Enter text: ");
    const char* def    = luaL_optstring(L, 2, nullptr);

    int rc;
    std::string v;
    {
        CString defText = def ? CString(CA2T(def, CP_UTF8)) : CString();
        CString wPrompt = MakePrompt(prompt, defText);
        AcString s;
        rc = acedGetString(1, wPrompt, s);
        if (rc == RTNORM) v = ToUtf8(s.kwszPtr());
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc != RTNORM) { lua_pushnil(L); return 1; }
    if (v.empty() && def) v = def;
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

// at.getKeyword(prompt, "Yes No" [, default]) -> keyword string | nil
int at_getKeyword(lua_State* L)
{
    const char* prompt   = luaL_checkstring(L, 1);
    const char* keywords = luaL_checkstring(L, 2);
    const char* def      = luaL_optstring(L, 3, nullptr);

    int rc;
    std::string v;
    {
        CString defText = def ? CString(CA2T(def, CP_UTF8)) : CString();
        CString wPrompt = MakePrompt(prompt, defText);
        CString wKw(CA2T(keywords, CP_UTF8));
        acedInitGet(def ? 0 : RSG_NONULL, wKw);
        AcString s;
        rc = acedGetKword(wPrompt, s);
        if (rc == RTNORM) v = ToUtf8(s.kwszPtr());
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc == RTNONE && def) v = def;
    else if (rc != RTNORM) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

// at.getEntity([prompt]) -> handle, x, y, z (pick point, WCS) | nil
int at_getEntity(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Select object: ");

    int rc;
    ads_name ename;
    ads_point pt;
    AcDbObjectId id;
    {
        CString wPrompt = MakePrompt(prompt);
        rc = acedEntSel(wPrompt, ename, pt);
        if (rc == RTNORM && acdbGetObjectId(id, ename) != Acad::eOk)
            rc = RTERROR;
    }
    if (rc == RTCAN) return RaiseCancelled(L);
    if (rc != RTNORM) { lua_pushnil(L); return 1; }

    AcGePoint3d w = UcsToWcs(pt);
    PushHandle(L, id);
    lua_pushnumber(L, w.x); lua_pushnumber(L, w.y); lua_pushnumber(L, w.z);
    return 4;
}

// at.getSelection([prompt [, typeFilter]]) -> { handle, ... } (empty if none)
// typeFilter is an AutoCAD ssget DXF-0 filter, e.g. "LINE" or "LINE,ARC,CIRCLE".
int at_getSelection(lua_State* L)
{
    const char* prompt = luaL_optstring(L, 1, "Select objects: ");
    const char* filter = luaL_optstring(L, 2, nullptr);

    int rc;
    std::vector<AcDbObjectId> ids;
    {
        CString wPrompt = MakePrompt(prompt);
        CString wFilter = filter ? CString(CA2T(filter, CP_UTF8)) : CString();
        resbuf* pFilter = filter ? acutBuildList(RTDXF0, (LPCTSTR)wFilter, RTNONE) : nullptr;

        const ACHAR* prompts[2] = { wPrompt, _T("") };
        CommonTools::SelectionSetGuard ssGuard;
        rc = acedSSGet(_T(":$"), prompts, nullptr, pFilter, ssGuard.ss);
        ssGuard.acquired = (rc == RTNORM);
        if (pFilter) acutRelRb(pFilter);

        Adesk::Int32 len = 0;
        if (ssGuard.acquired && acedSSLength(ssGuard.ss, &len) == RTNORM)
        {
            for (Adesk::Int32 i = 0; i < len; ++i)
            {
                ads_name en;
                AcDbObjectId id;
                if (acedSSName(ssGuard.ss, i, en) == RTNORM && acdbGetObjectId(id, en) == Acad::eOk)
                    ids.push_back(id);
            }
        }
    }
    if (rc == RTCAN) return RaiseCancelled(L);

    PushIdList(L, ids);
    return 1;
}

// ── Entity query / edit ─────────────────────────────────────────────────────

// at.entities([typeFilter]) -> { handle, ... } of model-space entities in
// drawing order. typeFilter matches the DXF name ("LWPOLYLINE") or the class
// name ("AcDbPolyline"), case-insensitive.
int at_entities(lua_State* L)
{
    const char* filter = luaL_optstring(L, 1, nullptr);

    std::vector<AcDbObjectId> ids;
    {
        CString wFilter = filter ? CString(CA2T(filter, CP_UTF8)) : CString();
        AcDbBlockTableRecord* pMS = nullptr;
        if (CommonTools::GetModelSpace(pMS) == Acad::eOk)
        {
            AcDbBlockTableRecordIterator* pIter = nullptr;
            if (pMS->newIterator(pIter) == Acad::eOk)
            {
                for (; !pIter->done(); pIter->step())
                {
                    AcDbObjectId id;
                    if (pIter->getEntityId(id) == Acad::eOk && ClassMatches(id.objectClass(), wFilter))
                        ids.push_back(id);
                }
                delete pIter;
            }
            pMS->close();
        }
    }

    PushIdList(L, ids);
    return 1;
}

// Type-specific fields for getProps. Called with the props table on top of
// the stack and the entity open for read. Only lua_push*/setfield happen while
// the entity is open - those can raise only on out-of-memory.
void PushTypeSpecificProps(lua_State* L, AcDbEntity* pEnt)
{
    if (auto* p = AcDbLine::cast(pEnt))
    {
        SetPointField(L, "startPoint", p->startPoint());
        SetPointField(L, "endPoint",   p->endPoint());
    }
    else if (auto* p = AcDbCircle::cast(pEnt))
    {
        SetPointField(L, "center", p->center());
        SetNumberField(L, "radius", p->radius());
    }
    else if (auto* p = AcDbArc::cast(pEnt))
    {
        SetPointField(L, "center", p->center());
        SetNumberField(L, "radius", p->radius());
        SetNumberField(L, "startAngle", RadToDeg(p->startAngle()));
        SetNumberField(L, "endAngle",   RadToDeg(p->endAngle()));
    }
    else if (auto* p = AcDbPolyline::cast(pEnt))
    {
        lua_pushboolean(L, p->isClosed());
        lua_setfield(L, -2, "closed");
        SetNumberField(L, "elevation", p->elevation());

        unsigned int n = p->numVerts();
        lua_createtable(L, static_cast<int>(n), 0);
        for (unsigned int i = 0; i < n; ++i)
        {
            AcGePoint2d v;
            double bulge = 0.0;
            p->getPointAt(i, v);
            p->getBulgeAt(i, bulge);
            lua_createtable(L, 0, 3);
            SetNumberField(L, "x", v.x);
            SetNumberField(L, "y", v.y);
            SetNumberField(L, "bulge", bulge);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_setfield(L, -2, "vertices");
    }
    else if (auto* p = AcDbText::cast(pEnt))
    {
        SetStringField(L, "text", p->textStringConst());
        SetPointField(L, "position", p->position());
        SetNumberField(L, "height", p->height());
        SetNumberField(L, "rotation", RadToDeg(p->rotation()));
    }
    else if (auto* p = AcDbMText::cast(pEnt))
    {
        AcString contents;
        p->contents(contents);
        SetStringField(L, "text", contents.kwszPtr());
        SetPointField(L, "position", p->location());
        SetNumberField(L, "height", p->textHeight());
        SetNumberField(L, "rotation", RadToDeg(p->rotation()));
    }
    else if (auto* p = AcDbBlockReference::cast(pEnt))
    {
        AcString name;
        {
            CommonTools::AcDbObjectGuard<AcDbBlockTableRecord> btr(p->blockTableRecord());
            if (btr) btr->getName(name);
        }
        SetStringField(L, "name", name.kwszPtr());
        SetPointField(L, "position", p->position());
        SetNumberField(L, "rotation", RadToDeg(p->rotation()));
        AcGeScale3d s = p->scaleFactors();
        SetPointField(L, "scale", AcGePoint3d(s.sx, s.sy, s.sz));
    }

    // Any curve (line, arc, polyline, spline, ellipse, …): length, and area
    // when it encloses one.
    if (auto* c = AcDbCurve::cast(pEnt))
    {
        double endParam = 0.0, len = 0.0;
        if (c->getEndParam(endParam) == Acad::eOk && c->getDistAtParam(endParam, len) == Acad::eOk)
            SetNumberField(L, "length", len);
        double area = 0.0;
        if (c->isClosed() && c->getArea(area) == Acad::eOk)
            SetNumberField(L, "area", area);
    }
}

// at.getProps(handle) -> table | nil, err
int at_getProps(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    AcDbObjectId id = ResolveHandle(handle);

    bool opened = false;
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
        if (ent)
        {
            opened = true;
            AcRxClass* pClass = id.objectClass();
            lua_createtable(L, 0, 12);
            SetStringField(L, "handle", CA2T(handle, CP_UTF8));
            SetStringField(L, "type",  pClass && pClass->dxfName() ? pClass->dxfName() : _T(""));
            SetStringField(L, "class", pClass ? pClass->name() : _T(""));

            AcString layer, linetype;
            ent->layer(layer);
            ent->linetype(linetype);
            SetStringField(L, "layer", layer.kwszPtr());
            SetStringField(L, "linetype", linetype.kwszPtr());
            lua_pushinteger(L, ent->colorIndex());   // 256 = ByLayer, 0 = ByBlock
            lua_setfield(L, -2, "color");

            AcDbExtents ext;
            if (ent->getGeomExtents(ext) == Acad::eOk)
            {
                SetPointField(L, "min", ext.minPoint());
                SetPointField(L, "max", ext.maxPoint());
            }

            PushTypeSpecificProps(L, ent.get());
        }
    }
    if (!opened) { lua_pushnil(L); lua_pushstring(L, "handle not found"); return 2; }
    return 1;
}

// at.erase(handle) -> true | false, err
int at_erase(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    AcDbObjectId id = ResolveHandle(handle);

    Acad::ErrorStatus es = Acad::eNullObjectId;
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
        if (ent) es = ent->erase();
    }
    if (es != Acad::eOk) { lua_pushboolean(L, 0); lua_pushstring(L, "could not erase entity"); return 2; }
    lua_pushboolean(L, 1);
    return 1;
}

// at.setLayer(handle, layerName) -> true | false, err  (creates the layer if missing)
int at_setLayer(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    const char* layer  = luaL_checkstring(L, 2);
    AcDbObjectId id = ResolveHandle(handle);

    Acad::ErrorStatus es = Acad::eNullObjectId;
    {
        CString wLayer(CA2T(layer, CP_UTF8));
        if (!id.isNull())
            CadInfra::EnsureLayer(wLayer);
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
        if (ent) es = ent->setLayer(wLayer);
    }
    if (es != Acad::eOk) { lua_pushboolean(L, 0); lua_pushstring(L, "could not set layer"); return 2; }
    lua_pushboolean(L, 1);
    return 1;
}

// at.setColor(handle, aci) -> true | false, err  (0 = ByBlock, 256 = ByLayer)
int at_setColor(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    lua_Integer aci    = luaL_checkinteger(L, 2);
    luaL_argcheck(L, aci >= 0 && aci <= 256, 2, "ACI color must be 0..256");
    AcDbObjectId id = ResolveHandle(handle);

    Acad::ErrorStatus es = Acad::eNullObjectId;
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
        if (ent) es = ent->setColorIndex(static_cast<Adesk::UInt16>(aci));
    }
    if (es != Acad::eOk) { lua_pushboolean(L, 0); lua_pushstring(L, "could not set color"); return 2; }
    lua_pushboolean(L, 1);
    return 1;
}

// ── Tier 1: drawing helpers ─────────────────────────────────────────────────

// Reads point i (1-based) of the table at idx: {x=,y=[,bulge=][,z=]} or {x,y[,bulge]}.
// Uses only non-raising Lua API calls; returns false on a malformed entry.
bool ReadVertex(lua_State* L, int idx, lua_Integer i, double& x, double& y, double& bulge, double& z)
{
    lua_rawgeti(L, idx, i);
    bool ok = lua_istable(L, -1);
    if (ok)
    {
        int t = lua_gettop(L);
        lua_getfield(L, t, "x");      lua_getfield(L, t, "y");
        lua_getfield(L, t, "bulge");  lua_getfield(L, t, "z");
        if (lua_isnil(L, -4))         // positional form {x, y, bulge}
        {
            lua_pop(L, 4);
            lua_rawgeti(L, t, 1); lua_rawgeti(L, t, 2); lua_rawgeti(L, t, 3); lua_pushnil(L);
        }
        int okX = 0, okY = 0;
        x     = lua_tonumberx(L, -4, &okX);
        y     = lua_tonumberx(L, -3, &okY);
        bulge = lua_tonumber(L, -2);   // nil -> 0
        z     = lua_tonumber(L, -1);
        ok = okX && okY;
        lua_pop(L, 4);
    }
    lua_pop(L, 1);
    return ok;
}

// at.drawPolyline(points [, closed]) -> handle
int at_drawPolyline(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    bool closed = lua_toboolean(L, 2) != 0;
    lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, 1));
    luaL_argcheck(L, n >= 2, 1, "need at least 2 points");

    double x, y, b, z;
    for (lua_Integer i = 1; i <= n; ++i)        // validate before allocating
        if (!ReadVertex(L, 1, i, x, y, b, z))
            return luaL_error(L, "point %d must be {x=,y=} or {x,y}", static_cast<int>(i));

    AcDbPolyline* pPl = new AcDbPolyline(static_cast<unsigned int>(n));
    for (lua_Integer i = 1; i <= n; ++i)
    {
        ReadVertex(L, 1, i, x, y, b, z);
        if (i == 1) pPl->setElevation(z);
        pPl->addVertexAt(static_cast<unsigned int>(i - 1), AcGePoint2d(x, y), b);
    }
    pPl->setClosed(closed);

    AcDbObjectId id = AppendToModelSpace(pPl);
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }
    PushHandle(L, id);
    return 1;
}

// at.drawText(x,y,z, text [,height [,rotationDeg]]) -> handle
int at_drawText(lua_State* L)
{
    double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2), z = luaL_checknumber(L, 3);
    const char* text = luaL_checkstring(L, 4);
    double height = luaL_optnumber(L, 5, 0.0);
    double rotDeg = luaL_optnumber(L, 6, 0.0);

    AcDbObjectId id;
    {
        CString wText(CA2T(text, CP_UTF8));
        id = CadInfra::InsertText(AcGePoint3d(x, y, z), wText, rotDeg * M_PI / 180.0);
        if (!id.isNull() && height > 0.0)
        {
            CommonTools::AcDbObjectGuard<AcDbText> t(id, AcDb::kForWrite);
            if (t) t->setHeight(height);
        }
    }
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }
    PushHandle(L, id);
    return 1;
}

// at.drawMText(x,y,z, text [,height]) -> handle
int at_drawMText(lua_State* L)
{
    double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2), z = luaL_checknumber(L, 3);
    const char* text = luaL_checkstring(L, 4);
    double height = luaL_optnumber(L, 5, 0.0);

    AcDbObjectId id;
    {
        CString wText(CA2T(text, CP_UTF8));
        id = CadInfra::InsertMText(AcGePoint3d(x, y, z), wText);
        if (!id.isNull() && height > 0.0)
        {
            CommonTools::AcDbObjectGuard<AcDbMText> t(id, AcDb::kForWrite);
            if (t) t->setTextHeight(height);
        }
    }
    if (id.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }
    PushHandle(L, id);
    return 1;
}

// at.ensureLayer(name [, {color=, linetype=, lineweight=, description=, plot=, locked=}]) -> true
// Properties only apply when the layer is created; an existing layer is left as is.
int at_ensureLayer(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    bool hasProps = lua_istable(L, 2);

    // Leave each field's value on the stack so the const char* stays valid.
    const char *color = nullptr, *linetype = nullptr, *desc = nullptr;
    double lw = -1.0;
    bool plot = true, locked = false;
    if (hasProps)
    {
        lua_getfield(L, 2, "color");       color    = lua_tostring(L, -1);
        lua_getfield(L, 2, "linetype");    linetype = lua_tostring(L, -1);
        lua_getfield(L, 2, "description"); desc     = lua_tostring(L, -1);
        lua_getfield(L, 2, "lineweight");  if (lua_isnumber(L, -1)) lw = lua_tonumber(L, -1);
        lua_getfield(L, 2, "plot");        if (!lua_isnil(L, -1)) plot = lua_toboolean(L, -1) != 0;
        lua_getfield(L, 2, "locked");      locked = lua_toboolean(L, -1) != 0;
    }

    {
        CadInfra::LayerProps props;
        if (color)    props.color       = color;
        if (linetype) props.linetype    = linetype;
        if (desc)     props.description = desc;
        props.lineweight = lw;
        props.plot       = plot;
        props.locked     = locked;
        CadInfra::EnsureLayer(CString(CA2T(name, CP_UTF8)), props);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// at.formatArea(area) / at.formatLength(length) -> string in drawing units (INSUNITS)
int at_formatArea(lua_State* L)
{
    double v = luaL_checknumber(L, 1);
    std::string s;
    {
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        s = ToUtf8(MeasureFormat::FormatArea(v, pDb->insunits()));
    }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int at_formatLength(lua_State* L)
{
    double v = luaL_checknumber(L, 1);
    std::string s;
    {
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        s = ToUtf8(MeasureFormat::FormatLength(v, pDb->insunits()));
    }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// at.refPoint(handle) -> x,y,z | nil,err  (the point the move/copy/align tools use)
int at_refPoint(lua_State* L)
{
    const char* handle = luaL_checkstring(L, 1);
    AcDbObjectId id = ResolveHandle(handle);
    AcGePoint3d p;
    if (id.isNull() || !CommonTools::GetEntityReferencePoint(id, p))
    { lua_pushnil(L); lua_pushstring(L, "no reference point"); return 2; }
    lua_pushnumber(L, p.x); lua_pushnumber(L, p.y); lua_pushnumber(L, p.z);
    return 3;
}

// Reads a {handle, ...} table at idx into ids (unknown handles are skipped).
std::vector<AcDbObjectId> ReadHandleList(lua_State* L, int idx)
{
    std::vector<AcDbObjectId> ids;
    lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, idx));
    for (lua_Integer i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, idx, i);
        const char* h = lua_tostring(L, -1);
        AcDbObjectId id = h ? ResolveHandle(h) : AcDbObjectId::kNull;
        lua_pop(L, 1);
        if (!id.isNull()) ids.push_back(id);
    }
    return ids;
}

// at.alignTo({handle,...}, "x"|"y"|"z", coord) -> number aligned
int at_alignTo(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    static const char* const kAxes[] = { "x", "y", "z", nullptr };
    int axis = luaL_checkoption(L, 2, nullptr, kAxes);
    double coord = luaL_checknumber(L, 3);

    int aligned;
    {
        std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
        aligned = AlignTools::AlignObjects(ids, axis, coord, false);
    }
    lua_pushinteger(L, aligned);
    return 1;
}

// ── Tier 1: polyline booleans / regions ─────────────────────────────────────

// at.polyBoolean(h1, h2, "union"|"intersect"|"subtract") -> region handle | nil,err
int at_polyBoolean(lua_State* L)
{
    const char* h1 = luaL_checkstring(L, 1);
    const char* h2 = luaL_checkstring(L, 2);
    static const char* const kOps[] = { "union", "intersect", "subtract", nullptr };
    static const AcDb::BoolOperType kOpVals[] = { AcDb::kBoolUnite, AcDb::kBoolIntersect, AcDb::kBoolSubtract };
    int op = luaL_checkoption(L, 3, nullptr, kOps);

    AcDbObjectId id;
    std::string err;
    {
        CString wErr;
        id = PolylineTools::BooleanPolylines(ResolveHandle(h1), ResolveHandle(h2), kOpVals[op], &wErr);
        if (id.isNull()) err = ToUtf8(wErr);
    }
    if (id.isNull()) { lua_pushnil(L); lua_pushlstring(L, err.data(), err.size()); return 2; }
    PushHandle(L, id);
    return 1;
}

// at.regionToPolyline(regionHandle) -> polyline handle | nil,err
int at_regionToPolyline(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);

    AcDbObjectId id;
    std::string err;
    {
        CString wErr;
        id = PolylineTools::RegionToPolyline(ResolveHandle(h), &wErr);
        if (id.isNull()) err = ToUtf8(wErr);
    }
    if (id.isNull()) { lua_pushnil(L); lua_pushlstring(L, err.data(), err.size()); return 2; }
    PushHandle(L, id);
    return 1;
}

// ── Tier 1: annotation ──────────────────────────────────────────────────────

// at.seqNumber(x,y,z, text, height [, withCircle]) -> textHandle [, circleHandle]
int at_seqNumber(lua_State* L)
{
    double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2), z = luaL_checknumber(L, 3);
    const char* text = luaL_checkstring(L, 4);
    double height = luaL_checknumber(L, 5);
    luaL_argcheck(L, height > 0.0, 5, "height must be > 0");
    bool withCircle = lua_toboolean(L, 6) != 0;

    AcDbObjectId textId, circleId;
    {
        CString wText(CA2T(text, CP_UTF8));
        textId = SeqNumTools::CreateSeqNumber(AcGePoint3d(x, y, z), wText, height, withCircle, &circleId, false);
    }
    if (textId.isNull()) { lua_pushnil(L); lua_pushstring(L, "draw failed"); return 2; }
    PushHandle(L, textId);
    if (circleId.isNull()) return 1;
    PushHandle(L, circleId);
    return 2;
}

// ── Tier 1: geometric patterns (each returns {handle,...} of what it drew) ──

// at.goldenSpiral(x1,y1,z1, x2,y2,z2)
int at_goldenSpiral(lua_State* L)
{
    AcGePoint3d a(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    AcGePoint3d b(luaL_checknumber(L, 4), luaL_checknumber(L, 5), luaL_checknumber(L, 6));
    luaL_argcheck(L, a.distanceTo(b) >= 0.001, 4, "points are too close");
    return DrawAndCollect(L, [&] { GoldenRectTools::DrawGoldenSpiral(a, b); });
}

int ClampInt(lua_Integer v, int lo, int hi)
{
    return static_cast<int>(v < lo ? lo : (v > hi ? hi : v));
}

// at.patternRosette(cx,cy,cz, R, n)
int at_patternRosette(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double R = luaL_checknumber(L, 4);
    int n = ClampInt(luaL_optinteger(L, 5, 8), 3, 64);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawRosette(c, R, n); });
}

// at.patternStar(cx,cy,cz, R, n [,innerFactor])
int at_patternStar(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double R = luaL_checknumber(L, 4);
    int n = ClampInt(luaL_optinteger(L, 5, 8), 3, 64);
    double f = luaL_optnumber(L, 6, 0.38);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawStar(c, R, n, f); });
}

// at.patternPetals(cx,cy,cz, R, n [,bulge])
int at_patternPetals(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double R = luaL_checknumber(L, 4);
    int n = ClampInt(luaL_optinteger(L, 5, 8), 3, 64);
    double b = luaL_optnumber(L, 6, 0.4142);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawPetals(c, R, n, b); });
}

// at.patternGeometric(cx,cy,cz, R, n [,innerFactor])
int at_patternGeometric(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double R = luaL_checknumber(L, 4);
    int n = ClampInt(luaL_optinteger(L, 5, 8), 3, 64);
    double f = luaL_optnumber(L, 6, 0.45);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawGeometric(c, R, n, f); });
}

// at.patternHojaNazari(cx,cy,cz, leafSize [,rings [,widthFactor]])
int at_patternHojaNazari(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double size = luaL_checknumber(L, 4);
    int rings = ClampInt(luaL_optinteger(L, 5, 3), 1, 12);
    double w = luaL_optnumber(L, 6, 0.35);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawHojaNazari(c, size, rings, w); });
}

// at.patternArabescoRl(x,y,z, A [,cols [,rows]])
int at_patternArabescoRl(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double A = luaL_checknumber(L, 4);
    int cols = static_cast<int>(luaL_optinteger(L, 5, 3));
    int rows = static_cast<int>(luaL_optinteger(L, 6, 3));
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawArabescoRl(c, A, cols, rows); });
}

// at.patternArabescoToro(cx,cy,cz, A [,nT [,mT [,subdiv [,widthF [,heightF]]]]])
int at_patternArabescoToro(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double A  = luaL_checknumber(L, 4);
    int nT    = static_cast<int>(luaL_optinteger(L, 5, 6));
    int mT    = static_cast<int>(luaL_optinteger(L, 6, 3));
    int D     = static_cast<int>(luaL_optinteger(L, 7, 6));
    double fw = luaL_optnumber(L, 8, 0.28);
    double fh = luaL_optnumber(L, 9, 0.08);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawArabescoToroSol(c, A, nT, mT, D, fw, fh); });
}

// at.patternArabescoHip(cx,cy,cz, A [,nT [,mT [,ampF [,subdiv [,widthF [,heightF]]]]]])
int at_patternArabescoHip(lua_State* L)
{
    AcGePoint3d c(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    double A  = luaL_checknumber(L, 4);
    int nT    = static_cast<int>(luaL_optinteger(L, 5, 4));
    int mT    = static_cast<int>(luaL_optinteger(L, 6, 4));
    double fa = luaL_optnumber(L, 7, 3.0);
    int D     = static_cast<int>(luaL_optinteger(L, 8, 6));
    double fw = luaL_optnumber(L, 9, 0.28);
    double fh = luaL_optnumber(L, 10, 0.08);
    return DrawAndCollect(L, [&] { ArabesqueTools::DrawArabescoHipSol(c, A, nT, mT, fa, D, fw, fh); });
}

// ── Tier 2 helpers ──────────────────────────────────────────────────────────

// Pushes a handle, or nil + the error text, for core functions that return an
// id and fill a CString reason. Call after all RAII scopes have closed.
int PushIdOrError(lua_State* L, const AcDbObjectId& id, const std::string& err)
{
    if (id.isNull())
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    PushHandle(L, id);
    return 1;
}

int PushIdListOrError(lua_State* L, const std::vector<AcDbObjectId>& ids, const std::string& err)
{
    if (ids.empty())
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    PushIdList(L, ids);
    return 1;
}

int PushBoolOrError(lua_State* L, bool ok, const std::string& err)
{
    if (!ok)
    {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

const char* const kDistModes[] = { "linear", "between", "equal", nullptr };

// ── Tier 2: distribute ──────────────────────────────────────────────────────

// at.distribute({handle,...}, x1,y1,z1, x2,y2,z2 [, mode]) -> count | nil,err
int at_distribute(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    AcGePoint3d a(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    AcGePoint3d b(luaL_checknumber(L, 5), luaL_checknumber(L, 6), luaL_checknumber(L, 7));
    int mode = luaL_checkoption(L, 8, "linear", kDistModes);

    int placed;
    {
        std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
        placed = DistributeTools::DistributeObjects(ids, a, b, mode, false);
    }
    if (placed < 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, mode == 0 ? "need at least 2 objects and two distinct points"
                                    : "need at least 1 object and two distinct points");
        return 2;
    }
    lua_pushinteger(L, placed);
    return 1;
}

// at.distributeCopies(handle, count, x1,y1,z1, x2,y2,z2 [, mode]) -> {handle,...} | nil,err
int at_distributeCopies(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);
    lua_Integer count = luaL_checkinteger(L, 2);
    AcGePoint3d a(luaL_checknumber(L, 3), luaL_checknumber(L, 4), luaL_checknumber(L, 5));
    AcGePoint3d b(luaL_checknumber(L, 6), luaL_checknumber(L, 7), luaL_checknumber(L, 8));
    int mode = luaL_checkoption(L, 9, "linear", kDistModes);
    luaL_argcheck(L, count >= (mode == 0 ? 2 : 1) && count <= 10000, 2,
                  "count must be >= 2 for linear, >= 1 otherwise");

    std::vector<AcDbObjectId> ids;
    {
        AcDbObjectId src = ResolveHandle(h);
        if (!src.isNull())
            ids = DistributeTools::DistributeCopies(src, static_cast<int>(count), a, b, mode);
    }
    return PushIdListOrError(L, ids, "copy failed (unknown handle or coincident points)");
}

// ── Tier 2: text ────────────────────────────────────────────────────────────

// at.getText(handle) -> string | nil
int at_getText(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);
    bool ok;
    std::string s;
    {
        CString text;
        ok = TextTools::GetText(ResolveHandle(h), text);
        if (ok) s = ToUtf8(text);
    }
    if (!ok) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// at.setText(handle, text) -> true | false
int at_setText(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);
    const char* text = luaL_checkstring(L, 2);
    bool ok;
    {
        CString wText(CA2T(text, CP_UTF8));
        ok = TextTools::SetText(ResolveHandle(h), wText);
    }
    lua_pushboolean(L, ok);
    return 1;
}

// at.copyTextStyle(src, {dest,...} [, includeHeight]) -> updated | nil,err
int at_copyTextStyle(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    bool includeHeight = lua_toboolean(L, 3) != 0;

    int updated;
    {
        std::vector<AcDbObjectId> dest = ReadHandleList(L, 2);
        updated = TextTools::CopyTextStyle(ResolveHandle(h), dest, includeHeight);
    }
    if (updated < 0) { lua_pushnil(L); lua_pushstring(L, "source is not a text object"); return 2; }
    lua_pushinteger(L, updated);
    return 1;
}

// at.copyDimStyle(src, {dest,...}) -> updated | nil,err
int at_copyDimStyle(lua_State* L)
{
    const char* h = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    int updated;
    {
        std::vector<AcDbObjectId> dest = ReadHandleList(L, 2);
        updated = TextTools::CopyDimStyle(ResolveHandle(h), dest);
    }
    if (updated < 0) { lua_pushnil(L); lua_pushstring(L, "source is not a dimension"); return 2; }
    lua_pushinteger(L, updated);
    return 1;
}

// at.sumText({handle,...}) -> sum, validCount, skippedCount
int at_sumText(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    double sum;
    int valid = 0, invalid = 0;
    {
        std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
        sum = TextTools::SumTextValues(ids, &valid, &invalid, false);
    }
    lua_pushnumber(L, sum);
    lua_pushinteger(L, valid);
    lua_pushinteger(L, invalid);
    return 3;
}

// at.scaleText({handle,...}, factor) -> count
int at_scaleText(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    double factor = luaL_checknumber(L, 2);
    luaL_argcheck(L, factor > 0.0, 2, "factor must be > 0");
    int count;
    {
        std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
        count = TextTools::ScaleTextHeight(ids, factor);
    }
    lua_pushinteger(L, count);
    return 1;
}

// ── Tier 2: linked area / length labels ─────────────────────────────────────

// Shared body for the single-curve label functions.
template <typename Fn>
int LabelBinding(lua_State* L, Fn&& insert)
{
    const char* h = luaL_checkstring(L, 1);
    AcDbObjectId id;
    std::string err;
    {
        CString wErr;
        id = insert(ResolveHandle(h), wErr);
        if (id.isNull()) err = wErr.IsEmpty() ? "could not create label" : ToUtf8(wErr);
    }
    return PushIdOrError(L, id, err);
}

int at_areaLabel(lua_State* L)
{
    return LabelBinding(L, [](AcDbObjectId id, CString& e) { return AreaTools::InsertAreaLabel(id, &e); });
}

int at_perimeterLabel(lua_State* L)
{
    return LabelBinding(L, [](AcDbObjectId id, CString& e) { return AreaTools::InsertPerimeterLabel(id, &e); });
}

int at_lengthLabel(lua_State* L)
{
    const char* layer = luaL_optstring(L, 2, "");
    return LabelBinding(L, [layer](AcDbObjectId id, CString&)
        { return AreaTools::InsertLengthLabel(id, CString(CA2T(layer, CP_UTF8))); });
}

// at.roomTag(handle, name) -> mtextHandle | nil,err
int at_roomTag(lua_State* L)
{
    const char* name = luaL_checkstring(L, 2);
    return LabelBinding(L, [name](AcDbObjectId id, CString& e)
        { return AreaTools::InsertRoomTag(id, CString(CA2T(name, CP_UTF8)), &e); });
}

// at.sumLengthLabel({handle,...}, x,y,z) -> textHandle, total | nil,err
int at_sumLengthLabel(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    AcGePoint3d pos(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4));

    AcDbObjectId id;
    double total = 0.0;
    std::string err;
    {
        std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
        CString wErr;
        id = AreaTools::InsertSumLengthLabel(ids, pos, &total, nullptr, &wErr);
        if (id.isNull()) err = ToUtf8(wErr);
    }
    int n = PushIdOrError(L, id, err);
    if (n == 2) return 2;
    lua_pushnumber(L, total);
    return 2;
}

// at.countBlocks([{handle,...}]) -> { [blockName] = count, ... }
int at_countBlocks(lua_State* L)
{
    bool hasList = !lua_isnoneornil(L, 1);
    if (hasList) luaL_checktype(L, 1, LUA_TTABLE);

    std::vector<std::pair<std::string, int>> rows;
    {
        std::map<CString, int> counts;
        if (hasList)
        {
            std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
            counts = AreaTools::CountBlocks(&ids);
        }
        else
            counts = AreaTools::CountBlocks(nullptr);
        for (const auto& kv : counts)
            rows.emplace_back(ToUtf8(kv.first), kv.second);
    }

    lua_createtable(L, 0, static_cast<int>(rows.size()));
    for (const auto& r : rows)
    {
        lua_pushinteger(L, r.second);
        lua_setfield(L, -2, r.first.c_str());
    }
    return 1;
}

// Shared body for at.splitLine / at.splitPolyline.
template <typename Fn>
int SplitBinding(lua_State* L, Fn&& split)
{
    const char* h = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    bool tag = lua_isnoneornil(L, 3) ? true : (lua_toboolean(L, 3) != 0);
    const char* layer = luaL_optstring(L, 4, "doc_areas");

    std::vector<AcDbObjectId> segs;
    std::string err;
    {
        std::vector<AcDbObjectId> cross = ReadHandleList(L, 2);
        CString wErr;
        segs = split(ResolveHandle(h), cross, CString(CA2T(layer, CP_UTF8)), tag, wErr);
        if (segs.empty()) err = ToUtf8(wErr);
    }
    return PushIdListOrError(L, segs, err);
}

int at_splitLine(lua_State* L)
{
    return SplitBinding(L, [](AcDbObjectId base, const std::vector<AcDbObjectId>& cross,
                              const CString& layer, bool tag, CString& e)
        { return AreaTools::SplitLine(base, cross, layer, tag, &e); });
}

int at_splitPolyline(lua_State* L)
{
    return SplitBinding(L, [](AcDbObjectId base, const std::vector<AcDbObjectId>& cross,
                              const CString& layer, bool tag, CString& e)
        { return AreaTools::SplitPolyline(base, cross, layer, tag, &e); });
}

// ── Tier 2: layers ──────────────────────────────────────────────────────────

struct LayerRow
{
    std::string name;
    int  color = 7;
    bool frozen = false, off = false, locked = false, current = false;
};

// at.layers() -> { {name=,color=,frozen=,off=,locked=,current=}, ... }
int at_layers(lua_State* L)
{
    std::vector<LayerRow> rows;
    {
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        AcDbLayerTable* pLT = nullptr;
        if (pDb && pDb->getLayerTable(pLT, AcDb::kForRead) == Acad::eOk)
        {
            AcDbObjectId current = pDb->clayer();
            AcDbLayerTableIterator* pIter = nullptr;
            if (pLT->newIterator(pIter) == Acad::eOk)
            {
                for (; !pIter->done(); pIter->step())
                {
                    AcDbObjectId id;
                    if (pIter->getRecordId(id) != Acad::eOk) continue;
                    CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> rec(id);
                    if (!rec) continue;
                    LayerRow r;
                    AcString name;
                    rec->getName(name);
                    r.name    = ToUtf8(name.kwszPtr());
                    r.color   = rec->color().colorIndex();
                    r.frozen  = rec->isFrozen();
                    r.off     = rec->isOff();
                    r.locked  = rec->isLocked();
                    r.current = (id == current);
                    rows.push_back(r);
                }
                delete pIter;
            }
            pLT->close();
        }
    }

    lua_createtable(L, static_cast<int>(rows.size()), 0);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const LayerRow& r = rows[i];
        lua_createtable(L, 0, 6);
        lua_pushlstring(L, r.name.data(), r.name.size()); lua_setfield(L, -2, "name");
        lua_pushinteger(L, r.color);   lua_setfield(L, -2, "color");
        lua_pushboolean(L, r.frozen);  lua_setfield(L, -2, "frozen");
        lua_pushboolean(L, r.off);     lua_setfield(L, -2, "off");
        lua_pushboolean(L, r.locked);  lua_setfield(L, -2, "locked");
        lua_pushboolean(L, r.current); lua_setfield(L, -2, "current");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// at.getCurrentLayer() -> name
int at_getCurrentLayer(lua_State* L)
{
    std::string s = ToUtf8(LayerTools::GetCurrentLayer());
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// at.setCurrentLayer(name [, create=true]) -> true | nil,err
int at_setCurrentLayer(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    bool create = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    bool ok;
    std::string err;
    {
        CString wErr;
        ok = LayerTools::SetCurrentLayer(CString(CA2T(name, CP_UTF8)), create, nullptr, &wErr);
        if (!ok) err = ToUtf8(wErr);
    }
    return PushBoolOrError(L, ok, err);
}

// Reads an optional boolean field as 1 / 0 / -1 (absent).
int TriStateField(lua_State* L, int idx, const char* key)
{
    lua_getfield(L, idx, key);
    int v = lua_isnil(L, -1) ? -1 : (lua_toboolean(L, -1) ? 1 : 0);
    lua_pop(L, 1);
    return v;
}

// at.setLayerState(name, {frozen=, off=, locked=}) -> true | nil,err
int at_setLayerState(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int frozen = TriStateField(L, 2, "frozen");
    int off    = TriStateField(L, 2, "off");
    int locked = TriStateField(L, 2, "locked");

    bool ok;
    std::string err;
    {
        CString wErr;
        ok = LayerTools::SetLayerState(CString(CA2T(name, CP_UTF8)), frozen, off, locked, &wErr);
        if (!ok) err = ToUtf8(wErr);
    }
    return PushBoolOrError(L, ok, err);
}

// ── Tier 2: SVG export ──────────────────────────────────────────────────────

// True for a bare file name: no folder parts, no drive, no "..".
bool IsPlainFileName(const CString& name)
{
    if (name.IsEmpty() || name.GetLength() > 200) return false;
    if (name.FindOneOf(_T("\\/:*?\"<>|")) >= 0) return false;
    if (name.Find(_T("..")) >= 0) return false;
    return true;
}

// at.exportSvg({handle,...} [, fileName]) -> path, exported, skipped | nil,err
// Always writes into the user's Documents folder; fileName must be a bare name.
int at_exportSvg(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* fileName = luaL_optstring(L, 2, "ArqaTools_Export.svg");

    bool ok = false;
    int exported = 0, skipped = 0;
    std::string err, path;
    {
        CString name(CA2T(fileName, CP_UTF8));
        name.Trim();
        if (!IsPlainFileName(name))
            err = "fileName must be a plain file name (no folders); files are written to Documents";
        else
        {
            if (name.Right(4).CompareNoCase(_T(".svg")) != 0) name += _T(".svg");
            CString full = SvgExportTools::DocumentsFolder() + _T("\\") + name;
            std::vector<AcDbObjectId> ids = ReadHandleList(L, 1);
            CString wErr;
            ok = SvgExportTools::ExportSvg(ids, full, &exported, &skipped, &wErr);
            if (ok) path = ToUtf8(full);
            else    err  = ToUtf8(wErr);
        }
    }
    if (!ok) { lua_pushnil(L); lua_pushlstring(L, err.data(), err.size()); return 2; }
    lua_pushlstring(L, path.data(), path.size());
    lua_pushinteger(L, exported);
    lua_pushinteger(L, skipped);
    return 3;
}

// at.print(msg) - explicit alias, distinct from the overridden global print(),
// in case a script wants to be unambiguous about which one it means.
int at_print(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    const char* msg = luaL_checkstring(L, 1);
    if (c->echo)
        acutPrintf(_T("\n%s"), static_cast<LPCTSTR>(CA2T(msg, CP_UTF8)));
    else
    {
        c->output += msg;
        c->output += "\n";
    }
    return 0;
}

// Overridden global print(...) - there is no console attached to the AutoCAD
// process for this plugin, so this captures into the same buffer as at.print
// (or echoes to the command line for a command engine) instead of writing to
// stdout. Uses luaL_tolstring so it matches stock print()'s
// __tostring-aware formatting.
int lua_print_override(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; ++i)
    {
        luaL_tolstring(L, i, nullptr);
        luaL_addvalue(&b);
        if (i < n) luaL_addchar(&b, '\t');
    }
    luaL_pushresult(&b);
    const char* line = lua_tostring(L, -1);
    if (c->echo)
        acutPrintf(_T("\n%s"), static_cast<LPCTSTR>(CA2T(line, CP_UTF8)));
    else
    {
        c->output += line;
        c->output += "\n";
    }
    return 0;
}

// at.defineCommand(name, fn [, description]) - only in command files.
int at_defineCommand(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const char* desc = luaL_optstring(L, 3, "");

    if (!c->defineFn)
        return luaL_error(L, "at.defineCommand only works in command files "
                             "(Documents\\ArqaTools\\LuaCommands); create one with ATAICMD");

    // Plain C buffers only: luaL_error below must not skip C++ destructors.
    char upper[32];
    size_t len = strlen(name);
    bool okName = len >= 1 && len <= 31 && isalpha(static_cast<unsigned char>(name[0]));
    for (size_t i = 0; okName && i < len; ++i)
    {
        unsigned char ch = static_cast<unsigned char>(name[i]);
        okName = isalnum(ch) || ch == '_';
        upper[i] = static_cast<char>(toupper(ch));
    }
    if (!okName)
        return luaL_error(L, "invalid command name '%s' (letters, digits, _; max 31; starts with a letter)", name);
    upper[len] = '\0';

    char err[256] = "";
    if (!c->defineFn(c->defineUser, upper, desc, err, sizeof(err)))
        return luaL_error(L, "cannot define %s: %s", upper, err);

    luaL_getsubtable(L, LUA_REGISTRYINDEX, "arqa.commands");
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, upper);
    lua_pop(L, 1);
    return 0;
}

// Single source of truth for the at API: registerAtTable binds from it and
// describeApi() renders it into the ATAILUA prompt. Add new functions here
// only - with an accurate signature and one-line doc.
struct AtFn
{
    const char*   name;
    lua_CFunction fn;
    const char*   signature;   // "(args) -> returns"
    const char*   doc;
};

const AtFn kFns[] = {
    // Output
    { "print",        at_print,        "(msg)",                                "show a line to the user (the global print(...) works too)" },
    { "defineCommand", at_defineCommand, "(NAME, function() ... end [,description])",
      "command files only: registers NAME as an AutoCAD command that runs the function" },
    // User input - ESC aborts the script; Enter returns the default, else nil
    { "getPoint",     at_getPoint,     "([prompt [,bx,by,bz]]) -> x,y,z | nil", "pick a point (rubber-band from the optional base point)" },
    { "getDistance",  at_getDistance,  "([prompt [,bx,by,bz]]) -> number | nil", "type or pick a distance" },
    { "getReal",      at_getReal,      "([prompt [,default]]) -> number | nil", "ask for a number" },
    { "getInt",       at_getInt,       "([prompt [,default]]) -> integer | nil", "ask for an integer" },
    { "getString",    at_getString,    "([prompt [,default]]) -> string | nil", "ask for text (spaces allowed)" },
    { "getKeyword",   at_getKeyword,   "(prompt, \"Yes No\" [,default]) -> keyword | nil", "choose one of the space-separated keywords" },
    { "getEntity",    at_getEntity,    "([prompt]) -> handle,x,y,z | nil",       "pick one object" },
    { "getSelection", at_getSelection, "([prompt [,typeFilter]]) -> {handle,...}", "select objects; typeFilter like \"LINE,CIRCLE\"; empty table if none" },
    // Query
    { "listEntities", at_listEntities, "() -> string",                          "entity type counts in model space" },
    { "entities",     at_entities,     "([typeFilter]) -> {handle,...}",        "model-space entities, optionally of one type (\"LINE\", \"LWPOLYLINE\", \"INSERT\", ...)" },
    { "getProps",     at_getProps,     "(handle) -> table | nil,err",
      "handle,type,class,layer,linetype,color(ACI),min,max; plus startPoint/endPoint, center/radius/startAngle/endAngle, "
      "closed/elevation/vertices{x,y,bulge}, text/position/height/rotation, name/position/rotation/scale, length, area "
      "as applicable; points are {x=,y=,z=}, angles in degrees" },
    // Create
    { "drawLine",     at_drawLine,     "(x1,y1,z1,x2,y2,z2) -> handle",        "" },
    { "drawCircle",   at_drawCircle,   "(cx,cy,cz,r) -> handle",               "" },
    { "drawArc",      at_drawArc,      "(cx,cy,cz,r,startDeg,endDeg) -> handle", "counter-clockwise from start to end" },
    { "drawRect",     at_drawRect,     "(x1,y1,z1,x2,y2,z2) -> handle",        "closed polyline from two corners" },
    { "drawPolyline", at_drawPolyline, "({{x=,y=[,bulge=]},...} [,closed]) -> handle", "points may also be {x,y[,bulge]}; elevation from the first point's z" },
    { "drawText",     at_drawText,     "(x,y,z,text [,height [,rotationDeg]]) -> handle", "single-line text centered on the point; default height from the drawing" },
    { "drawMText",    at_drawMText,    "(x,y,z,text [,height]) -> handle",     "multiline text centered on the point" },
    { "seqNumber",    at_seqNumber,    "(x,y,z,text,height [,withCircle]) -> textHandle [,circleHandle]", "ATSEQNUM-style number; with a circle the two are grouped" },
    { "ensureLayer",  at_ensureLayer,  "(name [,{color=,linetype=,lineweight=,description=,plot=,locked=}]) -> true",
      "create the layer if missing; color \"#RRGGBB\" or a name, lineweight in mm" },
    // Patterns - each returns {handle,...} of everything drawn
    { "goldenSpiral",       at_goldenSpiral,       "(x1,y1,z1,x2,y2,z2) -> {handle,...}", "golden-ratio spiral of rectangles, first side p1->p2" },
    { "patternRosette",     at_patternRosette,     "(cx,cy,cz,R [,n=8]) -> {handle,...}", "n interlocking circles" },
    { "patternStar",        at_patternStar,        "(cx,cy,cz,R [,n=8 [,innerFactor=0.38]]) -> {handle,...}", "n-pointed star polyline" },
    { "patternPetals",      at_patternPetals,      "(cx,cy,cz,R [,n=8 [,bulge=0.4142]]) -> {handle,...}", "lens-shaped petal flower" },
    { "patternGeometric",   at_patternGeometric,   "(cx,cy,cz,R [,n=8 [,innerFactor=0.45]]) -> {handle,...}", "star plus inner rosette" },
    { "patternHojaNazari",  at_patternHojaNazari,  "(cx,cy,cz,leafSize [,rings=3 [,widthFactor=0.35]]) -> {handle,...}", "Alhambra leaf tessellation" },
    { "patternArabescoRl",  at_patternArabescoRl,  "(x,y,z,A [,cols=3 [,rows=3]]) -> {handle,...}", "Andalusian 30/45 lattice from lower-left corner; tile S = 6.464*A" },
    { "patternArabescoToro", at_patternArabescoToro, "(cx,cy,cz,A [,nT=6 [,mT=3 [,subdiv=6 [,widthF=0.28 [,heightF=0.08]]]]]) -> {handle,...}", "lattice straps on a 3D torus" },
    { "patternArabescoHip", at_patternArabescoHip, "(cx,cy,cz,A [,nT=4 [,mT=4 [,ampF=3 [,subdiv=6 [,widthF=0.28 [,heightF=0.08]]]]]]) -> {handle,...}", "lattice straps on a hyperbolic paraboloid" },
    // Modify
    { "moveEntity",   at_moveEntity,   "(handle,dx,dy,dz) -> true|false",      "moves the whole group if the entity is grouped" },
    { "copyEntity",   at_copyEntity,   "(handle,dx,dy,dz) -> handle",          "" },
    { "rotateEntity", at_rotateEntity, "(handle,cx,cy,cz,angleDeg) -> true|false", "rotate about Z through (cx,cy,cz)" },
    { "erase",        at_erase,        "(handle) -> true|false",               "" },
    { "setLayer",     at_setLayer,     "(handle,layerName) -> true|false",     "creates the layer if it does not exist" },
    { "setColor",     at_setColor,     "(handle,aci) -> true|false",           "ACI 1-255, 0 = ByBlock, 256 = ByLayer" },
    { "alignTo",      at_alignTo,      "({handle,...}, \"x\"|\"y\"|\"z\", coord) -> count", "ATALX-style: move each object (or its group) so its reference point sits at coord" },
    { "polyBoolean",  at_polyBoolean,  "(h1,h2,\"union\"|\"intersect\"|\"subtract\") -> regionHandle | nil,err", "closed polylines; subtract keeps h1 minus h2; originals untouched" },
    { "distribute",   at_distribute,   "({handle,...}, x1,y1,z1, x2,y2,z2 [,mode]) -> count | nil,err",
      "ATDIST*: spread objects (groups move whole) between two points; mode \"linear\" (on endpoints, default), \"between\" (inside), \"equal\" (half gap at ends)" },
    { "distributeCopies", at_distributeCopies, "(handle, count, x1,y1,z1, x2,y2,z2 [,mode]) -> {handle,...} | nil,err",
      "ATDISTCOPY*: place count copies between two points; same modes as distribute" },
    // Text
    { "getText",      at_getText,      "(handle) -> string | nil",             "TEXT or MTEXT content (MText keeps its format codes)" },
    { "setText",      at_setText,      "(handle, text) -> true|false",         "" },
    { "copyTextStyle", at_copyTextStyle, "(src, {dest,...} [,includeHeight]) -> count | nil,err", "ATCOPYSTYLE / ATCOPYTEXTFULL" },
    { "copyDimStyle", at_copyDimStyle, "(src, {dest,...}) -> count | nil,err", "ATCOPYDIMSTYLE" },
    { "sumText",      at_sumText,      "({handle,...}) -> sum, valid, skipped", "sum numeric texts (\"1,234.50\", \"$12\" ok); non-text objects ignored" },
    { "scaleText",    at_scaleText,    "({handle,...}, factor) -> count",      "multiply TEXT/MTEXT heights" },
    // Linked labels - update when the curve changes, erased with it, survive save/reopen
    { "areaLabel",    at_areaLabel,    "(polyline) -> textHandle | nil,err",   "ATINSERTAREA; closed polylines" },
    { "perimeterLabel", at_perimeterLabel, "(polyline) -> textHandle | nil,err", "ATPERIMETER; closed polylines" },
    { "roomTag",      at_roomTag,      "(polyline, name) -> mtextHandle | nil,err", "ATROOMTAG: name + area" },
    { "lengthLabel",  at_lengthLabel,  "(curve [,layer]) -> textHandle | nil,err", "ATLINEARLENGTH: perpendicular label at the midpoint" },
    { "sumLengthLabel", at_sumLengthLabel, "({curve,...}, x,y,z) -> textHandle, total | nil,err", "ATSUMLENGTH" },
    { "countBlocks",  at_countBlocks,  "([{handle,...}]) -> {[blockName]=count,...}", "whole model space when no list is given; anonymous blocks skipped" },
    { "splitLine",    at_splitLine,    "(line, {crossing,...} [,tag=true [,layer=\"doc_areas\"]]) -> {handle,...} | nil,err",
      "ATSPLITLINE: replaces the line (erased) by segments at its intersections, optionally length-tagged" },
    { "splitPolyline", at_splitPolyline, "(polyline, {crossing,...} [,tag=true [,layer=\"doc_areas\"]]) -> {handle,...} | nil,err",
      "ATSPLITPOLI: same, keeping arc segments" },
    // Layers
    { "layers",       at_layers,       "() -> {{name=,color=,frozen=,off=,locked=,current=},...}", "" },
    { "getCurrentLayer", at_getCurrentLayer, "() -> name", "" },
    { "setCurrentLayer", at_setCurrentLayer, "(name [,create=true]) -> true | nil,err", "ATNL: creates the layer (color 7) if missing" },
    { "setLayerState", at_setLayerState, "(name, {frozen=,off=,locked=}) -> true | nil,err", "only the given fields change; cannot freeze the current layer or layer 0" },
    // Export
    { "exportSvg",    at_exportSvg,    "({handle,...} [,fileName]) -> path, exported, skipped | nil,err",
      "ATSVGEXPORT; always written to Documents, fileName must be a bare name (default ArqaTools_Export.svg), existing file overwritten" },
    { "regionToPolyline", at_regionToPolyline, "(regionHandle) -> handle | nil,err", "closed polyline following one boundary loop; a region with holes gives a single loop (a warning is printed)" },
    // Measurement helpers
    { "refPoint",     at_refPoint,     "(handle) -> x,y,z | nil,err",          "the reference point used by move/copy/align" },
    { "formatArea",   at_formatArea,   "(area) -> string",                     "formatted for the drawing's units, e.g. \"12.50 m2\"" },
    { "formatLength", at_formatLength, "(length) -> string",                   "formatted for the drawing's units" },
};

// What a command file's top level may touch while it loads.
bool AllowedWhileLoading(const char* key)
{
    return strcmp(key, "defineCommand") == 0 || strcmp(key, "print") == 0
        || strcmp(key, "formatArea") == 0    || strcmp(key, "formatLength") == 0;
}

// __index of the `at` proxy: upvalue 1 = LuaCtx*, upvalue 2 = the real table.
int at_index(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    const char* key = lua_tostring(L, 2);
    if (c->loading && key && !AllowedWhileLoading(key))
        return luaL_error(L, "at.%s cannot be used while a command file loads; "
                             "call it inside the command function", key);
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(2));
    return 1;
}

int at_newindex(lua_State* L)
{
    return luaL_error(L, "the at table is read-only");
}

// Global `at` is a userdata proxy over the real function table: scripts
// sharing a persistent engine cannot overwrite or remove at.* for each other
// (a userdata also defeats rawset), and the loading restriction is enforced
// in one place.
void registerAtTable(lua_State* L, LuaCtx* ctx)
{
    lua_newtable(L);                       // real = {}
    for (const auto& e : kFns)
    {
        lua_pushlightuserdata(L, ctx);      // upvalue #1 = LuaCtx*
        lua_pushcclosure(L, e.fn, 1);
        lua_setfield(L, -2, e.name);        // real[name] = closure
    }
    int real = lua_gettop(L);

    lua_newuserdatauv(L, 0, 0);            // proxy
    lua_createtable(L, 0, 3);              // its metatable
    lua_pushlightuserdata(L, ctx);
    lua_pushvalue(L, real);
    lua_pushcclosure(L, at_index, 2);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, at_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");    // getmetatable(at) -> false, not replaceable
    lua_setmetatable(L, -2);
    lua_setglobal(L, "at");                // _G.at = proxy
    lua_pop(L, 1);                         // real (kept alive by the __index upvalue)
}

// Message handler for command calls: appends a traceback (file:line chain)
// so a failing command - or the AI asked to fix it - can see where it broke.
int tracebackHandler(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

// Count hook: lets ESC break out of a runaway loop (acedUsrBrk polls the
// keyboard) and enforces LuaRunOptions::maxInstructions. Runs between VM
// instructions, never inside an at.* binding, so raising here is safe.
const int kHookInterval = 50000;

void instructionHook(lua_State* L, lua_Debug*)
{
    LuaCtx* c = *static_cast<LuaCtx**>(lua_getextraspace(L));
    c->instructions += kHookInterval;
    if (acedUsrBrk())
    {
        c->cancelled = true;
        luaL_error(L, "cancelled by user");
    }
    if (c->maxInstructions > 0 && c->instructions > c->maxInstructions)
        luaL_error(L, "instruction limit exceeded (%I) - infinite loop?",
                   static_cast<lua_Integer>(c->maxInstructions));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// describeApi
// ─────────────────────────────────────────────────────────────────────────────
std::string describeApi()
{
    std::string s;
    for (const auto& e : kFns)
    {
        s += "- at.";
        s += e.name;
        s += e.signature;
        if (*e.doc) { s += "  -- "; s += e.doc; }
        s += "\n";
    }
    return s;
}

bool hasApiFunction(const std::string& name)
{
    for (const auto& e : kFns)
        if (name == e.name) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LuaEngine
// ─────────────────────────────────────────────────────────────────────────────
LuaEngine::LuaEngine(bool echoOutput)
{
    m_L = luaL_newstate();
    if (!m_L) return;

    m_ctx = new LuaCtx;
    m_ctx->echo = echoOutput;
    *static_cast<LuaCtx**>(lua_getextraspace(m_L)) = m_ctx;
    lua_sethook(m_L, instructionHook, LUA_MASKCOUNT, kHookInterval);

    // Restricted stdlib: base/table/string/math only. Deliberately NOT
    // io/os/package/debug, and base's dofile/loadfile are removed too, so an
    // AI-authored script cannot touch the filesystem or shell out.
    luaL_requiref(m_L, LUA_GNAME,       luaopen_base,   1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(m_L, 1);
    luaL_requiref(m_L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(m_L, 1);
    lua_pushnil(m_L); lua_setglobal(m_L, "dofile");
    lua_pushnil(m_L); lua_setglobal(m_L, "loadfile");
    // load() stays (handy for generated code) but text-only: precompiled
    // bytecode is not verified by Lua and can crash the host process.
    luaL_dostring(m_L, "local raw = load; "
                       "load = function(chunk, name, _, env) return raw(chunk, name, 't', env) end");

    lua_pushlightuserdata(m_L, m_ctx);
    lua_pushcclosure(m_L, lua_print_override, 1);
    lua_setglobal(m_L, "print");

    registerAtTable(m_L, m_ctx);
}

LuaEngine::~LuaEngine()
{
    if (m_L) lua_close(m_L);
    delete m_ctx;
}

void LuaEngine::setDefineCommandHandler(DefineCommandFn fn, void* user)
{
    if (!m_ctx) return;
    m_ctx->defineFn   = fn;
    m_ctx->defineUser = user;
}

void LuaEngine::setLoading(bool loading)
{
    if (m_ctx) m_ctx->loading = loading;
}

void LuaEngine::beginRun(const LuaRunOptions& opts)
{
    m_ctx->output.clear();
    m_ctx->cancelled       = false;
    m_ctx->instructions    = 0;
    m_ctx->maxInstructions = opts.maxInstructions;
}

void LuaEngine::finishRun(int status, LuaRunResult& result)
{
    result.output    = m_ctx->output;
    result.cancelled = m_ctx->cancelled;
    result.ok        = (status == LUA_OK);
    if (!result.ok)
    {
        const char* msg = lua_tostring(m_L, -1);
        result.error = msg ? msg : "Lua runtime error.";
    }
}

LuaRunResult LuaEngine::runChunk(const std::string& code, const std::string& chunkName,
                                 const LuaRunOptions& opts)
{
    LuaRunResult result;
    if (!m_L) { result.error = "luaL_newstate failed."; return result; }

    int top = lua_gettop(m_L);
    beginRun(opts);
    int status = luaL_loadbuffer(m_L, code.data(), code.size(), chunkName.c_str());
    if (status == LUA_OK)
        status = lua_pcall(m_L, 0, 0, 0);
    finishRun(status, result);
    lua_settop(m_L, top);
    return result;
}

LuaRunResult LuaEngine::callCommand(const std::string& name, const LuaRunOptions& opts)
{
    LuaRunResult result;
    if (!m_L) { result.error = "luaL_newstate failed."; return result; }

    int top = lua_gettop(m_L);
    beginRun(opts);
    lua_pushcfunction(m_L, tracebackHandler);
    int handler = lua_gettop(m_L);
    luaL_getsubtable(m_L, LUA_REGISTRYINDEX, "arqa.commands");
    lua_getfield(m_L, -1, name.c_str());
    lua_remove(m_L, -2);
    if (!lua_isfunction(m_L, -1))
    {
        lua_settop(m_L, top);
        result.error = "command " + name + " is not defined";
        return result;
    }
    int status = lua_pcall(m_L, 0, 0, handler);
    finishRun(status, result);
    lua_settop(m_L, top);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// runLuaScript - one-shot engine for ATLUA / ATAILUA
// ─────────────────────────────────────────────────────────────────────────────
LuaRunResult runLuaScript(const std::string& code, const LuaRunOptions& opts)
{
    LuaEngine engine;
    return engine.runChunk(code, "=ATLUA", opts);
}

CString CleanAiLuaResponse(const CString& response)
{
    CString luaCode = response;
    luaCode.Replace(_T("```lua"), _T(""));
    luaCode.Replace(_T("```"), _T(""));

    // This project's lightweight JSON parser doesn't always unescape control
    // characters inside the response's "content" string - the same quirk
    // aiLispCommand works around (AITools.cpp's cleanup block). Unlike LISP
    // (where the code gets squashed to one line anyway), Lua statements are
    // newline/whitespace-friendly, so restore real newlines/tabs rather than
    // collapsing to spaces.
    luaCode.Replace(_T("\\r\\n"), _T("\n"));
    luaCode.Replace(_T("\\n"), _T("\n"));
    luaCode.Replace(_T("\\r"), _T("\n"));
    luaCode.Replace(_T("\\t"), _T("\t"));
    luaCode.Replace(_T("\\\""), _T("\""));

    luaCode.Trim();
    if (luaCode.Find(_T("CODE:")) == 0)
        luaCode = luaCode.Mid(5);
    luaCode.Trim();
    return luaCode;
}

// Prints a run's captured output and its ok / cancelled / error status.
static void ReportResult(const TCHAR* tag, const LuaRunResult& r)
{
    if (!r.output.empty())
    {
        CA2T wOut(r.output.c_str(), CP_UTF8);
        acutPrintf(_T("\n--- output ---\n%s"), static_cast<LPCTSTR>(wOut));
    }

    if (r.ok)
        acutPrintf(_T("\n[%s] ok\n"), tag);
    else if (r.cancelled)
        acutPrintf(_T("\n[%s] cancelled\n"), tag);
    else
    {
        CA2T wErr(r.error.c_str(), CP_UTF8);
        acutPrintf(_T("\n[%s] error: %s\n"), tag, static_cast<LPCTSTR>(wErr));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ATLUA - luaRunCommand
// ─────────────────────────────────────────────────────────────────────────────
void luaRunCommand()
{
    acutPrintf(_T("\n=== ATLUA ===\n"));

    TCHAR promptBuffer[2048];
    int result = acedGetString(1, _T("Lua code (or @path\\to\\file.lua): "), promptBuffer);
    if (result != RTNORM) { acutPrintf(_T("\nCommand cancelled.\n")); return; }

    CString input(promptBuffer);
    input.Trim();
    if (input.IsEmpty()) return;

    std::string code;
    if (input[0] == _T('@'))
    {
        CString path = input.Mid(1);
        FILE* fp = nullptr;
        if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp)
        {
            acutPrintf(_T("\n[ATLUA] Cannot open file: %s\n"), (LPCTSTR)path);
            return;
        }
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
            code.append(chunk, n);
        fclose(fp);
    }
    else
    {
        CT2A narrow(static_cast<LPCTSTR>(input), CP_UTF8);
        code = static_cast<const char*>(narrow);
    }

    ReportResult(_T("ATLUA"), runLuaScript(code));
}

// ─────────────────────────────────────────────────────────────────────────────
// ATAILUA - aiLuaCommand
// ─────────────────────────────────────────────────────────────────────────────
void aiLuaCommand()
{
    acutPrintf(_T("\n=== AI LUA CODE GENERATOR ===\n"));

    if (!AITools::IsTokenConfigured())
    {
        acutPrintf(_T("Error: API token not configured.\n"));
        acutPrintf(_T("Use ATAISETTOKEN command to set your API key first.\n"));
        return;
    }

    TCHAR promptBuffer[2048];
    int result = acedGetString(1, _T("Describe what to do (or press ESC to cancel): "), promptBuffer);
    if (result != RTNORM) { acutPrintf(_T("\nCommand cancelled.\n")); return; }

    CString userInput(promptBuffer);
    userInput.Trim();
    if (userInput.IsEmpty())
    {
        acutPrintf(_T("\nError: Description cannot be empty.\n"));
        return;
    }

    CA2T wApi(describeApi().c_str(), CP_UTF8);
    CString aiPrompt;
    aiPrompt.Format(
        _T("You are a Lua 5.4 code generator for an AutoCAD plugin (ArqaTools). Generate a Lua ")
        _T("script to accomplish the following task, using ONLY the \"at\" API described below.\n\n")
        _T("User wants to: %s\n\n")
        _T("AVAILABLE API (global table `at`; handles are strings; coordinates are WCS):\n")
        _T("%s\n")
        _T("RULES:\n")
        _T("1. Respond with ONLY raw Lua code - no explanation, no markdown, no ```lua fences.\n")
        _T("2. Only base/table/string/math stdlibs are available - no io/os/require/loadstring.\n")
        _T("3. Use plain Lua control flow (for/while/if) - there is no LISP or ACML syntax here.\n")
        _T("4. If the task needs a location, object or value the user did not give, ask for it ")
        _T("with at.getPoint/at.getSelection/at.getReal etc. instead of inventing it.\n\n")
        _T("CODE:"),
        (LPCTSTR)userInput,
        static_cast<LPCTSTR>(wApi)
    );

    acutPrintf(_T("\nAsking AI to generate Lua code...\n"));

    std::vector<AITools::ChatMessage>& history = AITools::GetLuaConversationHistory();
    std::vector<AITools::ChatMessage> messages;

    bool isFirstInteraction = (history.size() == 0);
    CString userPrompt = isFirstInteraction ? aiPrompt : userInput;

    for (const auto& msg : history)
        messages.push_back(msg);

    AITools::ChatMessage userMsg;
    userMsg.role = _T("user");
    userMsg.content = userPrompt;
    messages.push_back(userMsg);

    CString response = AITools::SendToGitHubCopilotWithHistory(messages);

    if (response.Find(_T("Error:")) == 0)
    {
        acutPrintf(_T("\n%s\n"), (LPCTSTR)response);
        return;
    }
    if (response.IsEmpty() || response.GetLength() < 3)
    {
        acutPrintf(_T("\nError: Received empty or invalid response from AI.\n"));
        return;
    }

    CString luaCode = CleanAiLuaResponse(response);

    if (luaCode.IsEmpty())
    {
        acutPrintf(_T("\nError: AI response did not contain any Lua code.\n"));
        acutPrintf(_T("Response received: %s\n"), (LPCTSTR)response);
        return;
    }

    acutPrintf(_T("\n========================================\n"));
    acutPrintf(_T("AI Generated Lua Code:\n"));
    acutPrintf(_T("========================================\n"));
    acutPrintf(_T("%s\n"), (LPCTSTR)luaCode);
    acutPrintf(_T("========================================\n"));

    // AI-written code gets a hard instruction cap on top of ESC, so a runaway
    // loop ends on its own (~ a few seconds of pure Lua) even if nobody is
    // watching the command line.
    LuaRunOptions opts;
    opts.maxInstructions = 500000000;
    CT2A narrowCode(static_cast<LPCTSTR>(luaCode), CP_UTF8);
    ReportResult(_T("ATAILUA"), runLuaScript(static_cast<const char*>(narrowCode), opts));

    AITools::ChatMessage assistantMsg;
    assistantMsg.role = _T("assistant");
    assistantMsg.content = response;
    history.push_back(userMsg);
    history.push_back(assistantMsg);

    const size_t kMaxLuaHistorySize = 50;
    while (history.size() > kMaxLuaHistorySize * 2)
        history.erase(history.begin());

    acutPrintf(_T("(Conversation history: %d interactions. Use ATAICLEAR to reset)\n"),
               static_cast<int>(history.size() / 2));
}

} // namespace LuaTools
