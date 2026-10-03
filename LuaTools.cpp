#include "StdAfx.h"
#include "LuaTools.h"
#include "AITools.h"
#include "CommonTools.h"
#include "CategorizeTools.h"
#include "CadInfra.h"

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
namespace {

struct LuaCtx
{
    std::string output;
    bool        cancelled       = false;
    long long   instructions    = 0;
    long long   maxInstructions = 0;   // 0 = unlimited
};

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

    lua_createtable(L, static_cast<int>(ids.size()), 0);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        PushHandle(L, ids[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
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

    lua_createtable(L, static_cast<int>(ids.size()), 0);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        PushHandle(L, ids[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
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

// at.print(msg) - explicit alias, distinct from the overridden global print(),
// in case a script wants to be unambiguous about which one it means.
int at_print(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    const char* msg = luaL_checkstring(L, 1);
    c->output += msg;
    c->output += "\n";
    return 0;
}

// Overridden global print(...) - there is no console attached to the AutoCAD
// process for this plugin, so this captures into the same buffer as at.print
// instead of writing to stdout. Uses luaL_tolstring so it matches stock
// print()'s __tostring-aware formatting.
int lua_print_override(lua_State* L)
{
    LuaCtx* c = ctx_from(L);
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i)
    {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        c->output.append(s, len);
        lua_pop(L, 1);
        if (i < n) c->output += "\t";
    }
    c->output += "\n";
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
    // Modify
    { "moveEntity",   at_moveEntity,   "(handle,dx,dy,dz) -> true|false",      "moves the whole group if the entity is grouped" },
    { "copyEntity",   at_copyEntity,   "(handle,dx,dy,dz) -> handle",          "" },
    { "rotateEntity", at_rotateEntity, "(handle,cx,cy,cz,angleDeg) -> true|false", "rotate about Z through (cx,cy,cz)" },
    { "erase",        at_erase,        "(handle) -> true|false",               "" },
    { "setLayer",     at_setLayer,     "(handle,layerName) -> true|false",     "creates the layer if it does not exist" },
    { "setColor",     at_setColor,     "(handle,aci) -> true|false",           "ACI 1-255, 0 = ByBlock, 256 = ByLayer" },
};

void registerAtTable(lua_State* L, LuaCtx* ctx)
{
    lua_newtable(L);                       // at = {}
    for (const auto& e : kFns)
    {
        lua_pushlightuserdata(L, ctx);      // upvalue #1 = LuaCtx*
        lua_pushcclosure(L, e.fn, 1);
        lua_setfield(L, -2, e.name);        // at[name] = closure
    }
    lua_setglobal(L, "at");                 // _G.at = at
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

// ─────────────────────────────────────────────────────────────────────────────
// runLuaScript
// ─────────────────────────────────────────────────────────────────────────────
LuaRunResult runLuaScript(const std::string& code, const LuaRunOptions& opts)
{
    LuaRunResult result;

    lua_State* L = luaL_newstate();
    if (!L) { result.error = "luaL_newstate failed."; return result; }

    LuaCtx ctx;
    ctx.maxInstructions = opts.maxInstructions;
    *static_cast<LuaCtx**>(lua_getextraspace(L)) = &ctx;
    lua_sethook(L, instructionHook, LUA_MASKCOUNT, kHookInterval);

    // Restricted stdlib: base/table/string/math only. Deliberately NOT
    // io/os/package/debug, so an AI-authored script cannot touch the
    // filesystem or shell out.
    luaL_requiref(L, LUA_GNAME,       luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(L, 1);

    lua_pushlightuserdata(L, &ctx);
    lua_pushcclosure(L, lua_print_override, 1);
    lua_setglobal(L, "print");

    registerAtTable(L, &ctx);

    int loadStatus = luaL_loadstring(L, code.c_str());
    if (loadStatus != LUA_OK)
    {
        const char* msg = lua_tostring(L, -1);
        result.error = msg ? msg : "Lua load error.";
        lua_close(L);
        return result;
    }

    int callStatus = lua_pcall(L, 0, 0, 0);
    result.output = ctx.output;
    result.cancelled = ctx.cancelled;
    if (callStatus != LUA_OK)
    {
        const char* msg = lua_tostring(L, -1);
        result.error = msg ? msg : "Lua runtime error.";
        result.ok = false;
    }
    else
    {
        result.ok = true;
    }

    lua_close(L);
    return result;
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
        acutPrintf(_T("Use AISETTOKEN command to set your GitHub token first.\n"));
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

    std::vector<AITools::ChatMessage>& history = AITools::GetConversationHistory();
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

    acutPrintf(_T("(Conversation history: %d interactions. Use AICLEAR to reset)\n"),
               static_cast<int>(history.size() / 2));
}

} // namespace LuaTools
