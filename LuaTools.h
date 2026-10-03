// LuaTools.h - Second, additive scripting engine alongside AutoLISP/ACML
//
// Vendors plain Lua 5.4 (ThirdParty\Lua\src\) directly into this project and
// exposes a restricted "at" API table (user input, entity query/edit,
// draw/move/copy/rotate)
// so both a human (ATLUA) and the AI subsystem (ATAILUA) get a scripting
// surface with real function calls, real arguments, and real pcall-style
// error handling - unlike aiLispCommand/aiFixCommand, which only generate
// LISP text and copy it to the clipboard for manual paste.
//
// Does not replace or touch ACML or the existing LISP-generation commands.

#pragma once
#include "StdAfx.h"
#include <string>

struct lua_State;

namespace LuaTools
{
    // Result of one Lua script run - always a real success/failure signal,
    // unlike AITools::ExecuteLispCode's coarse acedInvoke return-code check.
    struct LuaRunResult
    {
        bool        ok = false;
        bool        cancelled = false;  // user pressed ESC (prompt or running loop)
        std::string output;   // everything written via print()/at.print()
        std::string error;    // Lua compile/runtime error message when ok == false
    };

    struct LuaRunOptions
    {
        // Hard cap on executed Lua VM instructions; 0 = unlimited. ESC always
        // aborts a running script regardless of this value.
        long long maxInstructions = 0;
    };

    // Called by at.defineCommand. `name` is already validated and upper-cased.
    // Return false and fill err to reject the definition.
    using DefineCommandFn = bool (*)(void* user, const char* name, const char* description,
                                     char* err, size_t errSize);

    struct LuaCtx;   // per-engine run context (LuaTools.cpp)

    // A sandboxed Lua state with the `at` API. A short-lived engine backs
    // ATLUA/ATAILUA; LuaCommands keeps one alive for the whole session so the
    // functions registered with at.defineCommand stay callable.
    class LuaEngine
    {
    public:
        // echoOutput: print() goes straight to the command line instead of
        // being buffered into LuaRunResult::output.
        explicit LuaEngine(bool echoOutput = false);
        ~LuaEngine();
        LuaEngine(const LuaEngine&)            = delete;
        LuaEngine& operator=(const LuaEngine&) = delete;

        bool valid() const { return m_L != nullptr; }

        // Compiles and runs a chunk. chunkName follows Lua conventions:
        // "@file.lua" or "=label" (shown in error messages).
        LuaRunResult runChunk(const std::string& code, const std::string& chunkName,
                              const LuaRunOptions& opts = {});

        // Calls the function registered via at.defineCommand(name, ...).
        // Errors carry a Lua traceback (file:line).
        LuaRunResult callCommand(const std::string& name, const LuaRunOptions& opts = {});

        // Without a handler, at.defineCommand raises an error.
        void setDefineCommandHandler(DefineCommandFn fn, void* user);

        // While loading, only at.defineCommand/print/format* may be used at
        // the file's top level - everything else belongs inside the command.
        void setLoading(bool loading);

    private:
        ::lua_State* m_L   = nullptr;
        LuaCtx*           m_ctx = nullptr;
        void beginRun(const LuaRunOptions& opts);
        void finishRun(int status, LuaRunResult& result);
    };

    // Strips markdown fences, a leading "CODE:" and the escaped control
    // characters the lightweight JSON parser can leave in an AI response.
    CString CleanAiLuaResponse(const CString& response);

    // Runs `code` synchronously against the working database. Restricted
    // stdlib (base/table/string/math only - no io/os/package/debug), fresh
    // lua_State per call. Safe to call directly from the command thread -
    // this project has no background threads, so there is no cross-thread
    // ObjectARX-access concern to guard against (unlike DevTools' IntelliCAD
    // build, which routes AI-tool execution through a main-thread queue).
    // Runs inside the calling command, so everything a script does is a
    // single UNDO step.
    LuaRunResult runLuaScript(const std::string& code, const LuaRunOptions& opts = {});

    // Human/AI-readable reference of the `at` API, generated from the same
    // table that registers the functions - so the ATAILUA prompt can never
    // drift from what is actually bound.
    std::string describeApi();

    // True if `name` is an at.* function (used to reject AI code that calls
    // functions which do not exist).
    bool hasApiFunction(const std::string& name);

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
