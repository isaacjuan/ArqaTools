#include "StdAfx.h"
#include "LuaCommands.h"
#include "LuaTools.h"
#include "AITools.h"
#include "SvgExportTools.h"
#include <ShlObj.h>
#include <shellapi.h>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

namespace LuaCommands
{
namespace {

const TCHAR* const kGroup    = _T("ARQATOOLS_LUA");
constexpr int      kMaxSlots = 128;

struct CmdInfo
{
    CString name;          // upper-case
    CString description;
    CString file;          // full path of the defining file
    int     slot = -1;
    CString lastError;     // last failed run (with traceback); empty when fine
};

std::unique_ptr<LuaTools::LuaEngine> g_engine;
std::map<CString, CmdInfo>           g_cmds;              // by name
std::map<CString, CString>           g_failedFiles;       // full path -> load error
CString                              g_slotName[kMaxSlots];
CString                              g_loadingFile;       // file whose top level is running

// ── AutoCAD callback trampolines ────────────────────────────────────────────
// addCommand takes a plain void(*)() with no context, so each registered Lua
// command gets one of kMaxSlots generated functions that forward their slot.
void Dispatch(int slot);

template <int N> void Trampoline() { Dispatch(N); }

template <int... Is>
constexpr std::array<AcRxFunctionPtr, sizeof...(Is)> MakeTrampolines(std::integer_sequence<int, Is...>)
{
    return { { &Trampoline<Is>... } };
}

const std::array<AcRxFunctionPtr, kMaxSlots> kTrampolines =
    MakeTrampolines(std::make_integer_sequence<int, kMaxSlots>{});

// ── Small helpers ───────────────────────────────────────────────────────────
std::string ToUtf8(const CString& s)
{
    CT2A narrow(s, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

CString FromUtf8(const std::string& s)
{
    return CString(CA2T(s.c_str(), CP_UTF8));
}

CString FileNameOf(const CString& path)
{
    int pos = path.ReverseFind(_T('\\'));
    return pos >= 0 ? path.Mid(pos + 1) : path;
}

bool ReadFileUtf8(const CString& path, std::string& out)
{
    FILE* fp = nullptr;
    if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return false;
    out.clear();
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
        out.append(chunk, n);
    fclose(fp);
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0)   // UTF-8 BOM
        out.erase(0, 3);
    return true;
}

bool WriteFileUtf8(const CString& path, const std::string& text)
{
    FILE* fp = nullptr;
    if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return false;
    bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
    fclose(fp);
    return ok;
}

// Same rules as at.defineCommand.
bool IsValidCommandName(const CString& name)
{
    if (name.IsEmpty() || name.GetLength() > 31 || !_istalpha(name[0])) return false;
    for (int i = 0; i < name.GetLength(); ++i)
        if (!_istalnum(name[i]) && name[i] != _T('_')) return false;
    return true;
}

// True when AutoCAD (core, another ARX, or our own C++ commands) already
// knows the name. Lua commands must never shadow those.
bool CommandNameTaken(const CString& name)
{
    if (acedRegCmds->lookupCmd(name, true, true)) return true;
    AcString canonical;
    return acedGetCName(name, canonical) == RTNORM;
}

int CommandsInFile(const CString& file)
{
    int n = 0;
    for (const auto& kv : g_cmds)
        if (kv.second.file.CompareNoCase(file) == 0) ++n;
    return n;
}

CString HistoryFolder()
{
    return CommandsFolder() + _T("\\history");
}

// Copies (or moves) a command file into history\ as NAME_yyyymmdd_hhmmss.lua.
bool Backup(const CString& file, bool move)
{
    CString history = HistoryFolder();
    SHCreateDirectoryEx(NULL, history, NULL);
    CString base = FileNameOf(file);
    if (base.Right(4).CompareNoCase(_T(".lua")) == 0) base = base.Left(base.GetLength() - 4);
    CString dest = history + _T("\\") + base + _T("_")
                 + CTime::GetCurrentTime().Format(_T("%Y%m%d_%H%M%S")) + _T(".lua");
    return move ? MoveFileEx(file, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE
                : CopyFile(file, dest, FALSE) != FALSE;
}

// *.lua directly in the folder: "_" helpers first, then alphabetical.
std::vector<CString> ListLuaFiles(const CString& folder)
{
    std::vector<CString> files;
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(folder + _T("\\*.lua"), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        CString name(fd.cFileName);
        if (name.Right(4).CompareNoCase(_T(".lua")) != 0) continue;
        files.push_back(name);
    } while (FindNextFile(h, &fd));
    FindClose(h);

    std::sort(files.begin(), files.end(), [](const CString& a, const CString& b)
    {
        bool ha = !a.IsEmpty() && a[0] == _T('_'), hb = !b.IsEmpty() && b[0] == _T('_');
        if (ha != hb) return ha;
        return a.CompareNoCase(b) < 0;
    });
    for (auto& f : files) f = folder + _T("\\") + f;
    return files;
}

void CopyError(char* err, size_t errSize, const std::string& msg)
{
    strncpy_s(err, errSize, msg.c_str(), _TRUNCATE);
}

// ── Registration (live engine) ──────────────────────────────────────────────
void Unregister(CmdInfo& info)
{
    acedRegCmds->removeCmd(kGroup, info.name);
    if (info.slot >= 0) g_slotName[info.slot].Empty();
}

bool LiveDefine(void*, const char* name, const char* desc, char* err, size_t errSize)
{
    CString wName = FromUtf8(name);

    auto it = g_cmds.find(wName);
    if (it != g_cmds.end())
    {
        CopyError(err, errSize, "already defined in " + ToUtf8(FileNameOf(it->second.file)));
        return false;
    }
    if (CommandNameTaken(wName))
    {
        CopyError(err, errSize, "an AutoCAD or ArqaTools command with that name already exists");
        return false;
    }

    int slot = -1;
    for (int i = 0; i < kMaxSlots && slot < 0; ++i)
        if (g_slotName[i].IsEmpty()) slot = i;
    if (slot < 0)
    {
        CopyError(err, errSize, "too many Lua commands (max 128)");
        return false;
    }

    if (acedRegCmds->addCommand(kGroup, wName, wName, ACRX_CMD_MODAL, kTrampolines[slot]) != Acad::eOk)
    {
        CopyError(err, errSize, "AutoCAD refused to register the command");
        return false;
    }

    CmdInfo info;
    info.name        = wName;
    info.description = FromUtf8(desc);
    info.file        = g_loadingFile;
    info.slot        = slot;
    g_cmds[wName]    = info;
    g_slotName[slot] = wName;
    return true;
}

// Runs one file's top level in the live engine; on failure, unregisters
// whatever it had already defined so a broken file leaves nothing behind.
bool LoadFile(const CString& path, CString& err)
{
    std::string code;
    if (!ReadFileUtf8(path, code)) { err = _T("cannot read file"); return false; }

    // A file's top level only defines things; the cap keeps a stray endless
    // loop from hanging AutoCAD while it starts up.
    LuaTools::LuaRunOptions opts;
    opts.maxInstructions = 50000000;

    g_loadingFile = path;
    g_engine->setLoading(true);
    LuaTools::LuaRunResult r = g_engine->runChunk(code, "@" + ToUtf8(FileNameOf(path)), opts);
    g_engine->setLoading(false);
    g_loadingFile.Empty();
    if (r.ok) return true;

    err = FromUtf8(r.error);
    for (auto it = g_cmds.begin(); it != g_cmds.end(); )
    {
        if (it->second.file.CompareNoCase(path) == 0) { Unregister(it->second); it = g_cmds.erase(it); }
        else ++it;
    }
    return false;
}

// Fresh engine, every file reloaded, every command re-registered.
void LoadAll(bool report)
{
    acedRegCmds->removeGroup(kGroup);
    g_cmds.clear();
    g_failedFiles.clear();
    for (auto& s : g_slotName) s.Empty();

    g_engine.reset(new LuaTools::LuaEngine(/*echoOutput=*/true));
    g_engine->setDefineCommandHandler(LiveDefine, nullptr);

    CString folder = CommandsFolder();
    int failed = 0;
    for (const CString& file : ListLuaFiles(folder))
    {
        CString err;
        if (!LoadFile(file, err))
        {
            ++failed;
            g_failedFiles[file] = err;
            acutPrintf(_T("\n[Lua] %s not loaded: %s"), (LPCTSTR)FileNameOf(file), (LPCTSTR)err);
        }
    }
    if (report || failed)
        acutPrintf(_T("\n[Lua] %d command(s) loaded from %s%s\n"),
                   static_cast<int>(g_cmds.size()), (LPCTSTR)folder,
                   failed ? _T(" (some files failed, see above)") : _T(""));
}

void Dispatch(int slot)
{
    if (slot < 0 || slot >= kMaxSlots || !g_engine) return;
    CString name = g_slotName[slot];
    if (name.IsEmpty()) return;

    LuaTools::LuaRunResult r = g_engine->callCommand(ToUtf8(name));

    auto it = g_cmds.find(name);
    if (r.ok)
    {
        if (it != g_cmds.end()) it->second.lastError.Empty();
        return;
    }
    if (r.cancelled)
    {
        acutPrintf(_T("\n*Cancel*\n"));
        return;
    }
    CString err = FromUtf8(r.error);
    if (it != g_cmds.end()) it->second.lastError = err;
    acutPrintf(_T("\n[%s] error: %s\n(Run ATAICMD %s to let the AI fix it.)\n"),
               (LPCTSTR)name, (LPCTSTR)err, (LPCTSTR)name);
}

// ── Validation of AI-written files (throwaway engine) ───────────────────────
struct Validation
{
    CString              target;
    std::vector<CString> defined;
};

bool ValidateDefine(void* user, const char* name, const char*, char* err, size_t errSize)
{
    auto* v = static_cast<Validation*>(user);
    CString wName = FromUtf8(name);
    if (wName != v->target)
    {
        CopyError(err, errSize, "this file may only define " + ToUtf8(v->target));
        return false;
    }
    if (!v->defined.empty())
    {
        CopyError(err, errSize, "at.defineCommand called more than once");
        return false;
    }
    if (g_cmds.find(wName) == g_cmds.end() && CommandNameTaken(wName))
    {
        CopyError(err, errSize, "an AutoCAD or ArqaTools command with that name already exists");
        return false;
    }
    v->defined.push_back(wName);
    return true;
}

// Every "at.<name>" in the code must be a real at.* function.
bool CheckApiNames(const std::string& code, CString& err)
{
    for (size_t pos = code.find("at."); pos != std::string::npos; pos = code.find("at.", pos + 3))
    {
        if (pos > 0)
        {
            char prev = code[pos - 1];
            if (isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == '.') continue;
        }
        size_t start = pos + 3, end = start;
        while (end < code.size() && (isalnum(static_cast<unsigned char>(code[end])) || code[end] == '_'))
            ++end;
        if (end == start) continue;
        std::string fn = code.substr(start, end - start);
        if (!LuaTools::hasApiFunction(fn))
        {
            err = _T("at.") + FromUtf8(fn) + _T(" does not exist - use only the at.* functions listed in the API");
            return false;
        }
    }
    return true;
}

bool ValidateFile(const CString& code, const CString& name, CString& err)
{
    if (code.IsEmpty()) { err = _T("the response contained no code"); return false; }

    std::string utf8 = ToUtf8(code);
    if (!CheckApiNames(utf8, err)) return false;

    LuaTools::LuaEngine sandbox(/*echoOutput=*/false);
    Validation v;
    v.target = name;
    sandbox.setDefineCommandHandler(ValidateDefine, &v);
    sandbox.setLoading(true);

    LuaTools::LuaRunOptions opts;
    opts.maxInstructions = 10000000;   // a file's top level only defines things
    LuaTools::LuaRunResult r = sandbox.runChunk(utf8, "@" + ToUtf8(name) + ".lua", opts);
    if (!r.ok) { err = FromUtf8(r.error); return false; }
    if (v.defined.empty())
    {
        err = _T("the file must call at.defineCommand(\"") + name
            + _T("\", function() ... end, \"description\")");
        return false;
    }
    return true;
}

CString BuildPrompt(const CString& name, const CString& request, bool existing,
                    const std::string& currentSource, const CString& lastError)
{
    // Built by concatenation, never Format(): the source and the request may
    // contain '%'.
    CString p;
    p += _T("You are writing an AutoCAD command in Lua 5.4 for the ArqaTools plugin.\n\n");
    p += _T("Command name: ") + name + _T("\n");
    if (existing)
    {
        p += _T("Requested change: ") + request + _T("\n\n");
        p += _T("CURRENT FILE:\n") + FromUtf8(currentSource) + _T("\n\n");
        if (!lastError.IsEmpty())
            p += _T("ERROR TO FIX (from loading or running this file):\n") + lastError + _T("\n\n");
    }
    else
        p += _T("What it should do: ") + request + _T("\n\n");

    p += _T("FILE FORMAT - return one complete Lua file shaped like this:\n");
    p += _T("at.defineCommand(\"") + name + _T("\", function()\n");
    p += _T("    -- ask for input: at.getPoint / at.getSelection / at.getReal / ...\n");
    p += _T("    -- do the work with the at API\n");
    p += _T("    -- report results with print(...)\n");
    p += _T("end, \"<one-line description of the command>\")\n\n");

    p += _T("RULES:\n");
    p += _T("1. Respond with ONLY the raw Lua file - no explanation, no markdown fences.\n");
    p += _T("2. Call at.defineCommand exactly once, for \"") + name + _T("\".\n");
    p += _T("3. Outside that function only plain Lua is allowed (local helper functions, constants); ")
         _T("every at.* call must run inside a function.\n");
    p += _T("4. Only the base/table/string/math libraries exist - no io/os/require/dofile.\n");
    p += _T("5. ESC at any prompt cancels the command automatically; handle nil when the user just presses Enter.\n");
    p += _T("6. Coordinates are WCS, handles are strings, angles are degrees.\n");
    p += _T("7. Use only the at.* functions listed below - nothing else exists.\n\n");

    p += _T("AVAILABLE API (global table `at`):\n");
    p += FromUtf8(LuaTools::describeApi());

    CString others;
    for (const auto& kv : g_cmds)
        if (kv.first != name) others += _T(" ") + kv.first;
    if (!others.IsEmpty())
        p += _T("\nOther Lua commands already installed (do not redefine them):") + others + _T("\n");
    return p;
}

// Prompts for a command name; returns false on ESC or an invalid name.
bool PromptCommandName(const TCHAR* prompt, CString& name)
{
    AcString in;
    if (acedGetString(0, prompt, in) != RTNORM) return false;
    name = in.kwszPtr();
    name.Trim();
    name.MakeUpper();
    if (!IsValidCommandName(name))
    {
        acutPrintf(_T("\nInvalid name (letters, digits and _, max 31, starting with a letter).\n"));
        return false;
    }
    return true;
}

bool AskYesNo(const TCHAR* prompt, bool defaultYes)
{
    acedInitGet(0, _T("Yes No"));
    AcString kw;
    int rc = acedGetKword(prompt, kw);
    if (rc == RTNONE) return defaultYes;
    return rc == RTNORM && CString(kw.kwszPtr()).CompareNoCase(_T("Yes")) == 0;
}

const TCHAR* const kExampleFile =
    _T("-- Example Lua command. Edit it and run ATLUARELOAD, or change it with ATAICMD.\n")
    _T("at.defineCommand(\"ATLUAHELLO\", function()\n")
    _T("    local x, y, z = at.getPoint(\"Circle center: \")\n")
    _T("    if not x then return end\n")
    _T("    local r = at.getDistance(\"Radius: \", x, y, z) or 10\n")
    _T("    local h = at.drawCircle(x, y, z, r)\n")
    _T("    print(\"Circle \" .. h .. \", area \" .. at.formatArea(math.pi * r * r))\n")
    _T("end, \"Example: draw a circle and report its area\")\n");

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
CString CommandsFolder()
{
    CString folder = SvgExportTools::DocumentsFolder() + _T("\\ArqaTools\\LuaCommands");
    SHCreateDirectoryEx(NULL, folder, NULL);
    return folder;
}

void Init()
{
    CString folder = CommandsFolder();
    if (ListLuaFiles(folder).empty() && GetFileAttributes(HistoryFolder()) == INVALID_FILE_ATTRIBUTES)
        WriteFileUtf8(folder + _T("\\ATLUAHELLO.lua"), ToUtf8(kExampleFile));   // first run only
    LoadAll(true);
}

void Uninit()
{
    acedRegCmds->removeGroup(kGroup);
    g_cmds.clear();
    for (auto& s : g_slotName) s.Empty();
    g_engine.reset();
}

// ATLUACMDS
void listCommand()
{
    acutPrintf(_T("\n=== LUA COMMANDS ===\n"));
    if (g_cmds.empty() && g_failedFiles.empty())
    {
        acutPrintf(_T("None. Create one with ATAICMD, or put .lua files in\n%s\nand run ATLUARELOAD.\n"),
                   (LPCTSTR)CommandsFolder());
        return;
    }
    for (const auto& kv : g_cmds)
    {
        const CmdInfo& c = kv.second;
        acutPrintf(_T("%-20s %s  [%s]%s\n"), (LPCTSTR)c.name, (LPCTSTR)c.description,
                   (LPCTSTR)FileNameOf(c.file), c.lastError.IsEmpty() ? _T("") : _T("  (last run failed)"));
    }
    for (const auto& kv : g_failedFiles)
        acutPrintf(_T("%-20s NOT LOADED: %s\n"), (LPCTSTR)FileNameOf(kv.first), (LPCTSTR)kv.second);
    acutPrintf(_T("Folder: %s\n"), (LPCTSTR)CommandsFolder());
}

// ATLUARELOAD
void reloadCommand()
{
    LoadAll(true);
}

// ATLUAFOLDER
void folderCommand()
{
    CString folder = CommandsFolder();
    ShellExecute(NULL, _T("open"), folder, NULL, NULL, SW_SHOWNORMAL);
    acutPrintf(_T("\n%s\n"), (LPCTSTR)folder);
}

// ATLUACMDDEL
void deleteCommand()
{
    CString name;
    if (!PromptCommandName(_T("\nLua command to remove: "), name)) return;

    auto it = g_cmds.find(name);
    if (it == g_cmds.end()) { acutPrintf(_T("\n%s is not a Lua command.\n"), (LPCTSTR)name); return; }

    CString file = it->second.file;
    if (CommandsInFile(file) > 1)
    {
        acutPrintf(_T("\n%s is defined together with other commands in %s - edit that file by hand.\n"),
                   (LPCTSTR)name, (LPCTSTR)FileNameOf(file));
        return;
    }
    CString prompt;
    prompt.Format(_T("\nRemove %s (file moves to history\\)? [Yes/No] <No>: "), (LPCTSTR)name);
    if (!AskYesNo(prompt, false)) { acutPrintf(_T("\nNothing removed.\n")); return; }

    if (!Backup(file, /*move=*/true))
    { acutPrintf(_T("\nCould not move %s to history.\n"), (LPCTSTR)file); return; }
    LoadAll(false);
    acutPrintf(_T("\n%s removed.\n"), (LPCTSTR)name);
}

// ATAICMD
void aiCommand()
{
    acutPrintf(_T("\n=== AI LUA COMMAND ===\n"));
    if (!AITools::IsTokenConfigured())
    {
        acutPrintf(_T("Error: API token not configured. Use ATAISETTOKEN first.\n"));
        return;
    }

    CString name;
    if (!PromptCommandName(_T("\nCommand name (new, or an existing Lua command to change): "), name)) return;

    auto it = g_cmds.find(name);
    bool existing = it != g_cmds.end();
    CString targetFile = CommandsFolder() + _T("\\") + name + _T(".lua");

    // NAME.lua that failed to load: its command is not registered, but the
    // AI should repair that file (seeing its code and load error), not start over.
    auto failed = g_failedFiles.end();
    if (!existing)
        for (auto f = g_failedFiles.begin(); f != g_failedFiles.end(); ++f)
            if (f->first.CompareNoCase(targetFile) == 0) failed = f;
    bool broken = failed != g_failedFiles.end();

    if (!existing && !broken && CommandNameTaken(name))
    {
        acutPrintf(_T("\n%s is already an AutoCAD or ArqaTools command - choose another name.\n"), (LPCTSTR)name);
        return;
    }

    std::string currentSource;
    CString lastError;
    if (broken)
    {
        lastError = _T("The file does not load: ") + failed->second;
        ReadFileUtf8(targetFile, currentSource);
        existing = true;   // same flow as changing a command: send source + error
        acutPrintf(_T("Repairing %s (%s does not load) - its code and load error will be sent to the AI\n"),
                   (LPCTSTR)name, (LPCTSTR)FileNameOf(targetFile));
    }
    else if (existing)
    {
        targetFile = it->second.file;
        lastError  = it->second.lastError;
        if (CommandsInFile(targetFile) > 1)
        {
            acutPrintf(_T("\n%s is defined together with other commands in %s - edit that file by hand.\n"),
                       (LPCTSTR)name, (LPCTSTR)FileNameOf(targetFile));
            return;
        }
        ReadFileUtf8(targetFile, currentSource);
        acutPrintf(_T("Changing %s (%s)%s\n"), (LPCTSTR)name, (LPCTSTR)FileNameOf(targetFile),
                   lastError.IsEmpty() ? _T("") : _T(" - its last error will be sent to the AI"));
    }

    CString prompt;
    prompt.Format(existing ? _T("\nDescribe the change to %s: ") : _T("\nDescribe what %s should do: "),
                  (LPCTSTR)name);
    AcString requestIn;
    if (acedGetString(1, prompt, requestIn) != RTNORM) { acutPrintf(_T("\nCancelled.\n")); return; }
    CString request(requestIn.kwszPtr());
    request.Trim();
    if (request.IsEmpty() && lastError.IsEmpty())
    { acutPrintf(_T("\nA description is required.\n")); return; }
    if (request.IsEmpty()) request = _T("Fix the error shown below.");

    // Ask, validate, and let the AI correct itself up to twice.
    std::vector<AITools::ChatMessage> messages;
    AITools::ChatMessage first;
    first.role    = _T("user");
    first.content = BuildPrompt(name, request, existing, currentSource, lastError);
    messages.push_back(first);

    CString code, validationError;
    const int kAttempts = 3;
    for (int attempt = 1; attempt <= kAttempts; ++attempt)
    {
        acutPrintf(attempt == 1 ? _T("\nAsking AI...\n") : _T("Asking AI to correct it (attempt %d)...\n"), attempt);
        CString response = AITools::SendToGitHubCopilotWithHistory(messages);
        if (response.Find(_T("Error:")) == 0 || response.GetLength() < 3)
        {
            acutPrintf(_T("\n%s\n"), response.IsEmpty() ? _T("Error: empty AI response.") : (LPCTSTR)response);
            return;
        }
        code = LuaTools::CleanAiLuaResponse(response);
        if (ValidateFile(code, name, validationError)) { validationError.Empty(); break; }

        acutPrintf(_T("Validation failed: %s\n"), (LPCTSTR)validationError);
        AITools::ChatMessage reply, fix;
        reply.role    = _T("assistant");
        reply.content = response;
        fix.role      = _T("user");
        fix.content   = _T("That file failed validation:\n") + validationError
                      + _T("\nReturn the corrected complete file only.");
        messages.push_back(reply);
        messages.push_back(fix);
    }
    if (!validationError.IsEmpty())
    {
        acutPrintf(_T("\nThe AI did not produce a valid file. Last attempt:\n%s\n"), (LPCTSTR)code);
        return;
    }

    acutPrintf(_T("\n========================================\n%s\n========================================\n"),
               (LPCTSTR)code);
    prompt.Format(_T("\nInstall %s? [Yes/No] <Yes>: "), (LPCTSTR)name);
    if (!AskYesNo(prompt, true)) { acutPrintf(_T("\nNot installed.\n")); return; }

    if (GetFileAttributes(targetFile) != INVALID_FILE_ATTRIBUTES)
        Backup(targetFile, /*move=*/false);
    if (!WriteFileUtf8(targetFile, ToUtf8(code)))
    { acutPrintf(_T("\nCould not write %s\n"), (LPCTSTR)targetFile); return; }

    LoadAll(false);
    auto installed = g_cmds.find(name);
    if (installed == g_cmds.end())
    {
        acutPrintf(_T("\n%s was saved but did not load (see the message above).\n"), (LPCTSTR)name);
        return;
    }
    acutPrintf(_T("\n%s %s (%s). Type %s to run it.\n"), (LPCTSTR)name,
               existing ? _T("updated") : _T("installed"), (LPCTSTR)FileNameOf(targetFile), (LPCTSTR)name);

    prompt.Format(_T("\nRun %s now? [Yes/No] <No>: "), (LPCTSTR)name);
    if (AskYesNo(prompt, false))
        Dispatch(installed->second.slot);
}

} // namespace LuaCommands
