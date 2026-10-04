// LuaCommands.h - AutoCAD commands implemented in Lua, editable at run time
//
// Every *.lua file in Documents\ArqaTools\LuaCommands is loaded at plugin
// start into one persistent LuaEngine. A file registers commands with
//
//     at.defineCommand("ATSTAIRS", function() ... end, "Draw a stair in plan")
//
// and each becomes a real AutoCAD command (group ARQATOOLS_LUA). Files whose
// name starts with "_" load first and can hold shared helper functions.
//
// ATAICMD lets the AI create or change a command while AutoCAD runs: it
// writes the file, which is validated in a throwaway engine, shown to the
// user for approval, backed up (history\) if it replaces a version, and
// hot-reloaded. Lua commands cannot shadow existing AutoCAD/ArqaTools commands.

#pragma once
#include "StdAfx.h"
#include <string>
#include <utility>
#include <vector>

namespace LuaCommands
{
    // Read-only views for McpBridge.
    struct CommandSummary
    {
        CString name;
        CString description;
        CString file;
        CString lastError;   // last failed run; empty when fine
    };
    std::vector<CommandSummary> Commands();
    std::vector<std::pair<CString, CString>> FailedFiles();   // file, load error

    // UTF-8 source of a Lua command, or of NAME.lua when that file failed to load.
    bool CommandSource(const CString& name, std::string& source, CString& err);

    // Runs a Lua command without prompting: `answers` is a Lua table
    // constructor fed to its at.get* calls (LuaRunOptions::answers). Output is
    // echoed and also returned. Records lastError like a normal run.
    bool RunScripted(const CString& name, const std::string& answers,
                     std::string& output, std::string& error, bool& cancelled);

    // Plugin lifecycle (ArqaTools.cpp On_kInitAppMsg / On_kUnloadAppMsg).
    void Init();
    void Uninit();

    // Documents\ArqaTools\LuaCommands (created on demand).
    CString CommandsFolder();

    // ATLUACMDS   - list Lua commands (name, description, file, last error)
    void listCommand();
    // ATLUARELOAD - reload every command file (after editing them by hand)
    void reloadCommand();
    // ATAICMD     - AI creates a new Lua command or changes an existing one
    void aiCommand();
    // ATLUACMDDEL - remove a Lua command (its file moves to history\)
    void deleteCommand();
    // ATLUAFOLDER - open the commands folder in Explorer
    void folderCommand();
}
