#include "StdAfx.h"
#include "LuaTools.h"
#include "AITools.h"
#include "CommonTools.h"
#include "CategorizeTools.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "dbmain.h"
#include "dbents.h"
#include "dbsymtb.h"
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
};

LuaCtx* ctx_from(lua_State* L)
{
    return static_cast<LuaCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
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

void registerAtTable(lua_State* L, LuaCtx* ctx)
{
    static const struct { const char* name; lua_CFunction fn; } kFns[] = {
        { "listEntities", at_listEntities },
        { "drawLine",     at_drawLine },
        { "drawCircle",   at_drawCircle },
        { "drawArc",      at_drawArc },
        { "drawRect",     at_drawRect },
        { "moveEntity",   at_moveEntity },
        { "copyEntity",   at_copyEntity },
        { "rotateEntity", at_rotateEntity },
        { "print",        at_print },
    };

    lua_newtable(L);                       // at = {}
    for (const auto& e : kFns)
    {
        lua_pushlightuserdata(L, ctx);      // upvalue #1 = LuaCtx*
        lua_pushcclosure(L, e.fn, 1);
        lua_setfield(L, -2, e.name);        // at[name] = closure
    }
    lua_setglobal(L, "at");                 // _G.at = at
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// runLuaScript
// ─────────────────────────────────────────────────────────────────────────────
LuaRunResult runLuaScript(const std::string& code)
{
    LuaRunResult result;

    lua_State* L = luaL_newstate();
    if (!L) { result.error = "luaL_newstate failed."; return result; }

    LuaCtx ctx;

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

    LuaRunResult r = runLuaScript(code);

    if (!r.output.empty())
    {
        CA2T wOut(r.output.c_str(), CP_UTF8);
        acutPrintf(_T("\n--- output ---\n%s"), static_cast<LPCTSTR>(wOut));
    }

    if (r.ok)
        acutPrintf(_T("\n[ATLUA] ok\n"));
    else
    {
        CA2T wErr(r.error.c_str(), CP_UTF8);
        acutPrintf(_T("\n[ATLUA] error: %s\n"), static_cast<LPCTSTR>(wErr));
    }
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

    CString aiPrompt;
    aiPrompt.Format(
        _T("You are a Lua 5.4 code generator for an AutoCAD plugin (ArqaTools). Generate a Lua ")
        _T("script to accomplish the following task, using ONLY the \"at\" API described below.\n\n")
        _T("User wants to: %s\n\n")
        _T("AVAILABLE API (global table `at`):\n")
        _T("- at.listEntities() -> string                          - entity type counts in model space\n")
        _T("- at.drawLine(x1,y1,z1,x2,y2,z2) -> handle string\n")
        _T("- at.drawCircle(cx,cy,cz,r) -> handle string\n")
        _T("- at.drawRect(x1,y1,z1,x2,y2,z2) -> handle string\n")
        _T("- at.drawArc(cx,cy,cz,r,startDeg,endDeg) -> handle string\n")
        _T("- at.moveEntity(handle,dx,dy,dz) -> true/false\n")
        _T("- at.copyEntity(handle,dx,dy,dz) -> new handle string\n")
        _T("- at.rotateEntity(handle,cx,cy,cz,angleDeg) -> true/false\n")
        _T("- at.print(msg) or the standard print(...) - both are captured and shown to the user\n\n")
        _T("RULES:\n")
        _T("1. Respond with ONLY raw Lua code - no explanation, no markdown, no ```lua fences.\n")
        _T("2. Only base/table/string/math stdlibs are available - no io/os/require/loadstring.\n")
        _T("3. Use plain Lua control flow (for/while/if) - there is no LISP or ACML syntax here.\n\n")
        _T("CODE:"),
        (LPCTSTR)userInput
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

    CT2A narrowCode(static_cast<LPCTSTR>(luaCode), CP_UTF8);
    LuaRunResult r = runLuaScript(static_cast<const char*>(narrowCode));

    if (!r.output.empty())
    {
        CA2T wOut(r.output.c_str(), CP_UTF8);
        acutPrintf(_T("\n--- output ---\n%s"), static_cast<LPCTSTR>(wOut));
    }

    if (r.ok)
        acutPrintf(_T("\n[ATAILUA] ok\n"));
    else
    {
        CA2T wErr(r.error.c_str(), CP_UTF8);
        acutPrintf(_T("\n[ATAILUA] error: %s\n"), static_cast<LPCTSTR>(wErr));
    }

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
