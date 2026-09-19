// LuaTools.h - Second, additive scripting engine alongside AutoLISP/ACML
//
// Vendors plain Lua 5.4 (ThirdParty\Lua\src\) directly into this project and
// exposes a restricted "at" API table (list/draw/move/copy/rotate entities)
// so both a human (ATLUA) and the AI subsystem (ATAILUA) get a scripting
// surface with real function calls, real arguments, and real pcall-style
// error handling - unlike aiLispCommand/aiFixCommand, which only generate
// LISP text and copy it to the clipboard for manual paste.
//
// Does not replace or touch ACML or the existing LISP-generation commands.

#pragma once
#include "StdAfx.h"
#include <string>

namespace LuaTools
{
    // Result of one Lua script run - always a real success/failure signal,
    // unlike AITools::ExecuteLispCode's coarse acedInvoke return-code check.
    struct LuaRunResult
    {
        bool        ok = false;
        std::string output;   // everything written via print()/at.print()
        std::string error;    // Lua compile/runtime error message when ok == false
    };

    // Runs `code` synchronously against the working database. Restricted
    // stdlib (base/table/string/math only - no io/os/package/debug), fresh
    // lua_State per call. Safe to call directly from the command thread -
    // this project has no background threads, so there is no cross-thread
    // ObjectARX-access concern to guard against (unlike DevTools' IntelliCAD
    // build, which routes AI-tool execution through a main-thread queue).
    LuaRunResult runLuaScript(const std::string& code);

    // ATLUA - prompts for Lua code (or "@<path>" to load a .lua file) and
    // runs it via runLuaScript(), printing the result to the command line.
    void luaRunCommand();

    // ATAILUA - asks the AI to write a Lua script for a natural-language
    // request (reuses AITools::SendToGitHubCopilotWithHistory/
    // GetConversationHistory for the HTTP/config/history plumbing), then
    // runs the result directly via runLuaScript() and prints {ok,output,error}
    // - no clipboard-copy fallback needed, unlike aiLispCommand/aiFixCommand.
    void aiLuaCommand();
}
