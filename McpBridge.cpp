#include "StdAfx.h"
#include "McpBridge.h"
#include "ArqaTools.h"
#include "LuaTools.h"
#include "LuaCommands.h"
#include "CommandTester.h"
#include <sddl.h>
#include <ShlObj.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;   // this module's HINSTANCE

namespace McpBridge
{
namespace {

const UINT      kWmRequest       = WM_APP + 0x4D43;
const UINT_PTR  kRetryTimer      = 1;
const UINT      kRetryMs         = 250;
const DWORD     kBusyGiveUpMs    = 30000;   // a command stays active this long -> fail the request
const DWORD     kLostCommandMs   = 5000;    // ATMCPRUN was sent but never ran
const DWORD     kMaxFrame        = 16 * 1024 * 1024;
const long long kMaxInstructions = 50000000;
const TCHAR* const kWndClass     = _T("ArqaToolsMcpBridge");

struct Request
{
    std::string method;
    std::string body;
    std::string response;          // JSON, set once by Complete()
    DWORD       queuedAt = 0;
    DWORD       sentAt   = 0;      // when ATMCPRUN was sent; main thread only
    HANDLE      done     = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    ~Request() { if (done) CloseHandle(done); }
};

// The client sends one request at a time, so there is at most one pending.
std::mutex               g_mutex;
std::shared_ptr<Request> g_pending;
HWND                     g_wnd  = nullptr;
HANDLE                   g_stop = nullptr;
std::thread              g_thread;
CString                  g_pipeName;
std::atomic<bool>        g_connected{ false };
std::atomic<long>        g_served{ 0 };

HINSTANCE ModuleInstance()
{
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

// ── JSON (writing only; requests are plain text) ───────────────────────────
std::string ToUtf8(const CString& s)
{
    CT2A narrow(s, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

std::string Json(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string Json(const CString& s) { return Json(ToUtf8(s)); }

std::string Error(const std::string& msg)
{
    return "{\"ok\":false,\"error\":" + Json(msg) + "}";
}

// ── Request handlers (main thread) ─────────────────────────────────────────
std::string HandlePing()
{
    CString drawing, acadVer;
    if (AcApDocument* doc = acDocManager->curDocument())
        drawing = doc->fileName();
    resbuf rb;
    if (acedGetVar(_T("ACADVER"), &rb) == RTNORM && rb.restype == RTSTR)
    {
        acadVer = rb.resval.rstring;
        acutDelString(rb.resval.rstring);
    }
    return "{\"ok\":true,\"plugin\":" + Json(CString(GetVersionString()))
         + ",\"acadver\":" + Json(acadVer)
         + ",\"drawing\":" + Json(drawing)
         + ",\"pid\":" + std::to_string(GetCurrentProcessId()) + "}";
}

std::string HandleApi()
{
    std::string api = LuaTools::describeApi();

    // Names come from the "- at.NAME(" lines, so the list follows the API table.
    std::string readOnly;
    for (size_t pos = api.find("- at."); pos != std::string::npos; pos = api.find("- at.", pos + 5))
    {
        size_t start = pos + 5, end = start;
        while (end < api.size() && (isalnum(static_cast<unsigned char>(api[end])) || api[end] == '_'))
            ++end;
        std::string name = api.substr(start, end - start);
        if (!LuaTools::isReadOnlyFunction(name.c_str())) continue;
        readOnly += (readOnly.empty() ? "" : ",") + Json(name);
    }
    return "{\"ok\":true,\"api\":" + Json(api) + ",\"readOnly\":[" + readOnly + "]}";
}

std::string HandleListCommands()
{
    std::string cmds, failed;
    for (const auto& c : LuaCommands::Commands())
    {
        cmds += cmds.empty() ? "" : ",";
        cmds += "{\"name\":" + Json(c.name) + ",\"description\":" + Json(c.description)
              + ",\"file\":" + Json(c.file) + ",\"lastError\":" + Json(c.lastError)
              + ",\"params\":" + (c.paramsJson.empty() ? std::string("null") : c.paramsJson)
              + ",\"promptsInBody\":" + (c.promptsInBody ? "true" : "false") + "}";
    }
    for (const auto& f : LuaCommands::FailedFiles())
    {
        failed += failed.empty() ? "" : ",";
        failed += "{\"file\":" + Json(f.first) + ",\"error\":" + Json(f.second) + "}";
    }
    return "{\"ok\":true,\"folder\":" + Json(LuaCommands::CommandsFolder())
         + ",\"commands\":[" + cmds + "],\"failedFiles\":[" + failed + "]}";
}

std::string HandleCommandSource(const std::string& name)
{
    std::string source;
    CString err;
    if (!LuaCommands::CommandSource(CString(CA2T(name.c_str(), CP_UTF8)), source, err))
        return Error(ToUtf8(err));
    return "{\"ok\":true,\"source\":" + Json(source) + "}";
}

// Runs inside ATMCPRUN: drawing locked, one UNDO step.
std::string HandleRunLua(const std::string& code)
{
    LuaTools::LuaRunOptions opts;
    opts.maxInstructions = kMaxInstructions;
    opts.readOnly        = true;

    acutPrintf(_T("\n[MCP] running read-only Lua (%d bytes)\n"), static_cast<int>(code.size()));
    LuaTools::LuaRunResult r = LuaTools::runLuaScript(code, opts);

    std::string json = "{\"ok\":" + std::string(r.ok ? "true" : "false")
                     + ",\"output\":" + Json(r.output)
                     + ",\"cancelled\":" + (r.cancelled ? "true" : "false");
    if (!r.ok) json += ",\"error\":" + Json(r.error);
    return json + "}";
}

// Runs inside ATMCPRUN. body = "NAME\n<Lua table constructor>": positional
// answers for run_command, named parameters for call_command.
std::string HandleRunCommand(const std::string& body, bool named)
{
    size_t nl = body.find('\n');
    std::string name    = body.substr(0, nl);
    std::string literal = nl == std::string::npos ? std::string() : body.substr(nl + 1);

    acutPrintf(_T("\n[MCP] running %s\n"), static_cast<LPCTSTR>(CA2T(name.c_str(), CP_UTF8)));
    std::string output, error;
    bool cancelled = false;
    bool ok = LuaCommands::RunScripted(CString(CA2T(name.c_str(), CP_UTF8)),
                                       named ? (literal.empty() ? std::string("{}") : literal) : std::string(),
                                       named ? std::string() : literal,
                                       output, error, cancelled);

    std::string json = "{\"ok\":" + std::string(ok ? "true" : "false")
                     + ",\"output\":" + Json(output)
                     + ",\"cancelled\":" + (cancelled ? "true" : "false");
    if (!ok) json += ",\"error\":" + Json(error);
    return json + "}";
}

// Runs inside ATMCPRUN. Test-runs an installed command in a scratch drawing
// (CommandTester); the user's drawing is not touched.
std::string HandleTestCommand(const std::string& name)
{
    CString wName(CA2T(name.c_str(), CP_UTF8));
    wName.Trim();
    wName.MakeUpper();
    std::string source;
    CString err;
    if (!LuaCommands::CommandSource(wName, source, err)) return Error(ToUtf8(err));

    acutPrintf(_T("\n[MCP] test-running %s in a scratch drawing\n"), (LPCTSTR)wName);
    CString png = LuaCommands::CommandsFolder() + _T("\\test\\") + wName + _T(".png");
    SHCreateDirectoryEx(NULL, LuaCommands::CommandsFolder() + _T("\\test"), NULL);
    CommandTester::Result r = CommandTester::Run(wName, CString(CA2T(source.c_str(), CP_UTF8)), png);

    return "{\"ok\":true,\"ran\":" + std::string(r.ran ? "true" : "false")
         + ",\"runOk\":" + (r.ok ? "true" : "false")
         + ",\"skipped\":" + Json(r.skipped)
         + ",\"params\":" + Json(r.params)
         + ",\"output\":" + Json(r.output)
         + ",\"error\":" + Json(r.error)
         + ",\"report\":" + Json(r.report)
         + ",\"png\":" + Json(r.pngPath) + "}";
}

bool NeedsCommand(const std::string& method)
{
    return method == "run_lua" || method == "run_command" || method == "call_command"
        || method == "test_command";
}

std::string HandleQuery(const Request& req)
{
    if (req.method == "ping")               return HandlePing();
    if (req.method == "get_api")            return HandleApi();
    if (req.method == "list_commands")      return HandleListCommands();
    if (req.method == "get_command_source") return HandleCommandSource(req.body);
    return Error("unknown method: " + req.method);
}

// Hands the response to the waiting pipe thread. Ignored when the request was
// already given up (stop, or answered by the watchdog).
void Complete(const std::shared_ptr<Request>& req, const std::string& json)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending != req) return;
    req->response = json;
    SetEvent(req->done);
    g_pending.reset();
}

void Retry()
{
    SetTimer(g_wnd, kRetryTimer, kRetryMs, nullptr);
}

// Main thread: answer a query now, or get the request into ATMCPRUN once no
// command is active.
void Pump()
{
    std::shared_ptr<Request> req;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        req = g_pending;
    }
    if (!req) return;

    if (!NeedsCommand(req->method)) { Complete(req, HandleQuery(*req)); return; }

    AcApDocument* doc = acDocManager->curDocument();
    if (!doc) { Complete(req, Error("no drawing is open")); return; }

    DWORD now = GetTickCount();
    if (req->sentAt)
    {
        if (doc->isQuiescent() && now - req->sentAt > kLostCommandMs)
            Complete(req, Error("ATMCPRUN did not run; try again"));
        else
            Retry();
        return;
    }
    if (!doc->isQuiescent())
    {
        // Typing into an active prompt would feed it our command string.
        if (now - req->queuedAt > kBusyGiveUpMs)
            Complete(req, Error("AutoCAD is busy: a command is active. Finish or cancel it and retry."));
        else
            Retry();
        return;
    }
    req->sentAt = now;
    Retry();   // watchdog for a command string that never runs
    acDocManager->sendStringToExecute(doc, _T("_ATMCPRUN "), false, false, false);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == kWmRequest || (msg == WM_TIMER && wParam == kRetryTimer))
    {
        KillTimer(wnd, kRetryTimer);
        Pump();
        return 0;
    }
    return DefWindowProc(wnd, msg, wParam, lParam);
}

// ── Pipe thread ─────────────────────────────────────────────────────────────
// DACL: the current user and SYSTEM only (the default would let Everyone read).
CString CurrentUserSddl()
{
    CString sddl;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return sddl;
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    LPTSTR sid = nullptr;
    if (len && GetTokenInformation(token, TokenUser, buf.data(), len, &len)
        && ConvertSidToStringSid(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid))
    {
        sddl.Format(_T("D:P(A;;GA;;;%s)(A;;GA;;;SY)"), sid);
        LocalFree(sid);
    }
    CloseHandle(token);
    return sddl;
}

// Waits for an overlapped operation; false when it failed or a stop was requested.
bool Finish(HANDLE pipe, OVERLAPPED& ov, BOOL started, DWORD& bytes)
{
    bytes = 0;
    if (!started && GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE handles[2] = { ov.hEvent, g_stop };
    if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0)
    {
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, &bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, &bytes, FALSE) != FALSE;
}

bool Transfer(HANDLE pipe, bool write, char* data, DWORD len)
{
    HANDLE event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    bool ok = event != nullptr;
    for (DWORD done = 0; ok && done < len; )
    {
        OVERLAPPED ov = {};
        ov.hEvent = event;
        BOOL started = write ? WriteFile(pipe, data + done, len - done, nullptr, &ov)
                             : ReadFile(pipe, data + done, len - done, nullptr, &ov);
        DWORD n = 0;
        ok = Finish(pipe, ov, started, n) && n > 0;
        done += n;
    }
    if (event) CloseHandle(event);
    return ok;
}

bool ReadFrame(HANDLE pipe, std::string& out)
{
    unsigned char hdr[4];
    if (!Transfer(pipe, false, reinterpret_cast<char*>(hdr), 4)) return false;
    DWORD len = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (static_cast<DWORD>(hdr[3]) << 24);
    if (len > kMaxFrame) return false;
    out.assign(len, '\0');
    return len == 0 || Transfer(pipe, false, &out[0], len);
}

bool WriteFrame(HANDLE pipe, std::string data)
{
    DWORD len = static_cast<DWORD>(data.size());
    unsigned char hdr[4] = { static_cast<unsigned char>(len), static_cast<unsigned char>(len >> 8),
                             static_cast<unsigned char>(len >> 16), static_cast<unsigned char>(len >> 24) };
    return Transfer(pipe, true, reinterpret_cast<char*>(hdr), 4)
        && (len == 0 || Transfer(pipe, true, &data[0], len));
}

void ServeClient(HANDLE pipe)
{
    g_connected = true;
    std::string frame;
    while (ReadFrame(pipe, frame))
    {
        auto req = std::make_shared<Request>();
        size_t nl = frame.find('\n');
        req->method   = frame.substr(0, nl);
        req->body     = nl == std::string::npos ? std::string() : frame.substr(nl + 1);
        req->queuedAt = GetTickCount();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_pending = req;
        }
        PostMessage(g_wnd, kWmRequest, 0, 0);

        HANDLE handles[2] = { req->done, g_stop };
        bool answered = WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0;
        if (!answered)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_pending == req) g_pending.reset();
            break;
        }
        ++g_served;
        if (!WriteFrame(pipe, req->response)) break;
    }
    g_connected = false;
}

void PipeThread(CString name, CString sddl)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(sddl, SDDL_REVISION_1, &sd, nullptr))
        return;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), sd, FALSE };

    while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT)
    {
        HANDLE pipe = CreateNamedPipe(name,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 65536, 65536, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (WaitForSingleObject(g_stop, 1000) == WAIT_OBJECT_0) break;
            continue;
        }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        DWORD n = 0;
        BOOL started = ConnectNamedPipe(pipe, &ov);
        bool connected = (!started && GetLastError() == ERROR_PIPE_CONNECTED)
                      || Finish(pipe, ov, started, n);
        CloseHandle(ov.hEvent);

        if (connected) ServeClient(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    LocalFree(sd);
}

// ── Lifecycle (main thread) ─────────────────────────────────────────────────
bool Start(CString& err)
{
    if (g_thread.joinable()) return true;

    CString sddl = CurrentUserSddl();
    if (sddl.IsEmpty()) { err = _T("cannot read the current user's SID"); return false; }

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = ModuleInstance();
    wc.lpszClassName = kWndClass;
    if (!RegisterClassEx(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    { err = _T("cannot register the bridge window class"); return false; }

    g_wnd = CreateWindowEx(0, kWndClass, _T(""), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, ModuleInstance(), nullptr);
    if (!g_wnd) { err = _T("cannot create the bridge window"); return false; }

    g_stop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_pipeName.Format(_T("\\\\.\\pipe\\ArqaTools.%lu"), GetCurrentProcessId());
    g_served = 0;
    g_thread = std::thread(PipeThread, g_pipeName, sddl);
    return true;
}

void Stop()
{
    if (!g_thread.joinable()) return;
    SetEvent(g_stop);
    g_thread.join();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.reset();
    }
    KillTimer(g_wnd, kRetryTimer);
    DestroyWindow(g_wnd);
    g_wnd = nullptr;
    UnregisterClass(kWndClass, ModuleInstance());
    CloseHandle(g_stop);
    g_stop = nullptr;
    g_connected = false;
}

} // namespace

void Uninit()
{
    Stop();
}

// ATMCPSTART
void startCommand()
{
    if (g_thread.joinable())
    {
        acutPrintf(_T("\nMCP bridge already running on %s\n"), (LPCTSTR)g_pipeName);
        return;
    }
    CString err;
    if (!Start(err)) { acutPrintf(_T("\nMCP bridge not started: %s\n"), (LPCTSTR)err); return; }
    acutPrintf(_T("\nMCP bridge listening on %s (current user only).\n"), (LPCTSTR)g_pipeName);
    acutPrintf(_T("Connect an MCP client through ArqaToolsMcp.exe. ATMCPSTOP stops it.\n"));
}

// ATMCPSTOP
void stopCommand()
{
    if (!g_thread.joinable()) { acutPrintf(_T("\nMCP bridge is not running.\n")); return; }
    Stop();
    acutPrintf(_T("\nMCP bridge stopped.\n"));
}

// ATMCPSTATUS
void statusCommand()
{
    if (!g_thread.joinable()) { acutPrintf(_T("\nMCP bridge is not running (ATMCPSTART starts it).\n")); return; }
    acutPrintf(_T("\nMCP bridge: %s\nClient connected: %s\nRequests served: %ld\n"),
               (LPCTSTR)g_pipeName, g_connected ? _T("yes") : _T("no"), g_served.load());
}

// ATMCPRUN (internal)
void runCommand()
{
    std::shared_ptr<Request> req;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        req = g_pending;
    }
    if (!req || !req->sentAt || !NeedsCommand(req->method)) return;
    if (req->method == "run_lua")           Complete(req, HandleRunLua(req->body));
    else if (req->method == "test_command") Complete(req, HandleTestCommand(req->body));
    else                                    Complete(req, HandleRunCommand(req->body, req->method == "call_command"));
}

} // namespace McpBridge
