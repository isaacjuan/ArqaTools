#include "StdAfx.h"
#include "AiConfig.h"
#include "SvgExportTools.h"
#include <ShlObj.h>
#include <algorithm>
#include <cstdio>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace AiConfig
{
namespace {

const wchar_t* const kRegistryKey = L"Software\\ArqaToolsPlugin";
const wchar_t* const kKeysKey     = L"Software\\ArqaToolsPlugin\\Keys";

std::string ToUtf8(const CString& s)
{
    CT2A narrow(s, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

CString FromUtf8(const char* s)
{
    return CString(CA2T(s ? s : "", CP_UTF8));
}

bool ReadFile(const CString& path, std::string& out)
{
    FILE* fp = nullptr;
    if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return false;
    out.clear();
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) out.append(chunk, n);
    fclose(fp);
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

bool WriteFile(const CString& path, const std::string& text)
{
    FILE* fp = nullptr;
    if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return false;
    bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
    fclose(fp);
    return ok;
}

CString RegString(const wchar_t* key, const wchar_t* value)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return CString();
    WCHAR buf[1024];
    DWORD size = sizeof(buf);
    LONG r = RegQueryValueExW(hKey, value, NULL, NULL, (LPBYTE)buf, &size);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS ? CString(buf) : CString();
}

std::string LuaQuote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}

// ── Defaults ────────────────────────────────────────────────────────────────
const char* const kHeader =
"-- ArqaTools AI configuration.\n"
"-- Read on every AI request: edit, save, and the next ATAI* / ATAICMD call uses it.\n"
"--\n"
"-- active       the provider the AI commands use (ATAISETENDPOINT changes it)\n"
"-- providers    one entry per service:\n"
"--   label        shown by ATAISETENDPOINT\n"
"--   url          request URL; {model} is replaced by the model\n"
"--   format       \"openai\" (chat/completions JSON) or \"gemini\" (generateContent)\n"
"--   auth         \"bearer\" (Authorization header), \"query\" (?key=, Gemini) or \"none\"\n"
"--   model        model sent with each request (ATAISETMODEL changes it)\n"
"--   vision       true if the model reads images: ATAICMD's test-run review then\n"
"--                attaches a plan view of what the new command drew\n"
"--   max_tokens, temperature\n"
"--   timeout      seconds to wait for the reply (default 180; models that think first are slow)\n"
"--   key_env      optional: environment variable holding the API key\n"
"--   key_url      where to get a key\n"
"--   models_url   optional: URL listing the available models (ATAILISTMODELS)\n"
"--   extra        optional: raw JSON members added to every request body,\n"
"--                e.g. extra = '\"thinking\":{\"type\":\"disabled\"}'\n"
"--\n"
"-- API keys are NOT stored here: ATAISETTOKEN saves one per provider in the\n"
"-- registry (HKCU\\Software\\ArqaToolsPlugin\\Keys), or set key_env.\n"
"-- Add any OpenAI-compatible service as a new entry; no rebuild needed.\n"
"\n";

const char* const kHarness =
"-- How ATAICMD / ATAILUA check AI-written code before you accept it (HARNESS.md).\n"
"harness = {\n"
"  max_attempts  = 3,      -- model calls per request, correction rounds included\n"
"  test_run      = true,   -- run the result once in a scratch drawing and report what it drew\n"
"  review        = true,   -- a second AI call judges the result (and its plan view) against the request\n"
"  block_on_fail = false,  -- true: never install or run code the review failed\n"
"}\n"
"\n";

const char* const kProviders =
"providers = {\n"
"  github = {\n"
"    label = \"GitHub Models (about 50 requests/day)\",\n"
"    url = \"https://models.inference.ai.azure.com/chat/completions\",\n"
"    format = \"openai\", auth = \"bearer\", model = \"gpt-4o\", vision = true,\n"
"    key_url = \"https://github.com/settings/tokens\",\n"
"  },\n"
"  copilot = {\n"
"    label = \"GitHub Copilot subscription\",\n"
"    url = \"https://api.githubcopilot.com/chat/completions\",\n"
"    format = \"openai\", auth = \"bearer\", model = \"gpt-4o\", vision = true,\n"
"    key_url = \"https://github.com/settings/tokens\",\n"
"  },\n"
"  gemini = {\n"
"    label = \"Google Gemini (free tier)\",\n"
"    url = \"https://generativelanguage.googleapis.com/v1/models/{model}:generateContent\",\n"
"    format = \"gemini\", auth = \"query\", model = \"gemini-2.5-flash\", vision = true,\n"
"    key_url = \"https://aistudio.google.com/app/apikey\",\n"
"    models_url = \"https://generativelanguage.googleapis.com/v1/models\",\n"
"  },\n"
"  openai = {\n"
"    label = \"OpenAI\",\n"
"    url = \"https://api.openai.com/v1/chat/completions\",\n"
"    format = \"openai\", auth = \"bearer\", model = \"gpt-4o\", vision = true,\n"
"    key_url = \"https://platform.openai.com/api-keys\",\n"
"    models_url = \"https://api.openai.com/v1/models\",\n"
"  },\n"
"  deepseek = {\n"
"    label = \"DeepSeek (deepseek-flash reads images, deepseek-v4-pro is text-only)\",\n"
"    url = \"https://api.deepseek.com/chat/completions\",\n"
"    format = \"openai\", auth = \"bearer\", model = \"deepseek-flash\", vision = true,\n"
"    key_url = \"https://platform.deepseek.com/api_keys\",\n"
"    models_url = \"https://api.deepseek.com/models\",\n"
"  },\n"
"  ollama = {\n"
"    label = \"Ollama (local, no key; run 'ollama serve')\",\n"
"    url = \"http://localhost:11434/v1/chat/completions\",\n"
"    format = \"openai\", auth = \"none\", model = \"llama3.2\", vision = false,\n"
"    models_url = \"http://localhost:11434/api/tags\",\n"
"  },\n";

// Where the old registry endpoint pointed, as a provider name.
CString ProviderForLegacyEndpoint(const CString& endpoint)
{
    CString e = endpoint;
    e.MakeLower();
    if (e.IsEmpty() || e.Find(_T("models.inference.ai.azure.com")) >= 0) return _T("github");
    if (e.Find(_T("githubcopilot")) >= 0)                                return _T("copilot");
    if (e.Find(_T("generativelanguage")) >= 0)                           return _T("gemini");
    if (e.Find(_T("api.openai.com")) >= 0)                               return _T("openai");
    if (e.Find(_T("deepseek")) >= 0)                                     return _T("deepseek");
    if (e.Find(_T("localhost")) >= 0 || e.Find(_T("127.0.0.1")) >= 0)    return _T("ollama");
    return _T("custom");
}

// First use: the defaults, set to what the registry used to say.
void WriteDefaults(const CString& path)
{
    CString endpoint = RegString(kRegistryKey, L"APIEndpoint");
    CString model    = RegString(kRegistryKey, L"AIModel");
    CString key      = RegString(kRegistryKey, L"GitHubToken");
    CString active   = ProviderForLegacyEndpoint(endpoint);

    std::string text = kHeader;
    text += "active = " + LuaQuote(ToUtf8(active)) + "\n\n";
    text += kHarness;
    text += kProviders;
    if (active == _T("custom"))
        text += "  custom = {\n"
                "    label = \"Custom OpenAI-compatible endpoint\",\n"
                "    url = " + LuaQuote("https://" + ToUtf8(endpoint) + "/chat/completions") + ",\n"
                "    format = \"openai\", auth = \"bearer\", model = \"gpt-4o\", vision = false,\n"
                "  },\n";
    text += "}\n";
    WriteFile(path, text);

    // The old single model only meant something for that endpoint.
    CString err;
    CString lower = model;
    lower.MakeLower();
    bool keepModel = !model.IsEmpty() && active != _T("gemini")
                  && (active != _T("deepseek") || lower.Find(_T("deepseek")) == 0);
    if (keepModel && !(active != _T("ollama") && model == _T("gpt-4o")))
        SetModel(active, model, err);

    if (!key.IsEmpty() && active != _T("ollama") && RegString(kKeysKey, active).IsEmpty())
        SetKey(active, key);
}

// ── Reading the file ────────────────────────────────────────────────────────
void InstructionCap(lua_State* L, lua_Debug*)
{
    luaL_error(L, "ai_config.lua runs too long (endless loop?)");
}

CString StringField(lua_State* L, int idx, const char* key, const CString& def = CString())
{
    lua_getfield(L, idx, key);
    CString v = lua_type(L, -1) == LUA_TSTRING ? FromUtf8(lua_tostring(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}

double NumberField(lua_State* L, int idx, const char* key, double def)
{
    lua_getfield(L, idx, key);
    double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

bool BoolField(lua_State* L, int idx, const char* key, bool def)
{
    lua_getfield(L, idx, key);
    bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : def;
    lua_pop(L, 1);
    return v;
}

bool ReadProvider(lua_State* L, int idx, const CString& name, Provider& p, CString& err)
{
    p.name        = name;
    p.label       = StringField(L, idx, "label");
    p.url         = StringField(L, idx, "url");
    p.format      = StringField(L, idx, "format", _T("openai"));
    p.auth        = StringField(L, idx, "auth", _T("bearer"));
    p.model       = StringField(L, idx, "model");
    p.vision      = BoolField(L, idx, "vision", false);
    p.maxTokens   = static_cast<int>(NumberField(L, idx, "max_tokens", 8192));
    p.temperature = NumberField(L, idx, "temperature", 0.7);
    p.timeoutSeconds = static_cast<int>(NumberField(L, idx, "timeout", 180));
    if (p.timeoutSeconds < 5) p.timeoutSeconds = 5;
    p.keyEnv      = StringField(L, idx, "key_env");
    p.keyUrl      = StringField(L, idx, "key_url");
    p.modelsUrl   = StringField(L, idx, "models_url");
    p.extra       = StringField(L, idx, "extra");
    p.format.MakeLower();
    p.auth.MakeLower();

    if (p.url.IsEmpty()) { err = _T("provider '") + name + _T("' has no url"); return false; }
    if (p.format != _T("openai") && p.format != _T("gemini"))
    { err = _T("provider '") + name + _T("': format must be \"openai\" or \"gemini\""); return false; }
    if (p.auth != _T("bearer") && p.auth != _T("query") && p.auth != _T("none"))
    { err = _T("provider '") + name + _T("': auth must be \"bearer\", \"query\" or \"none\""); return false; }

    CString url = p.url;
    url.Replace(_T("{model}"), p.model);
    if (!SplitUrl(url, p.https, p.host, p.port, p.path))
    { err = _T("provider '") + name + _T("': invalid url ") + p.url; return false; }
    return true;
}

} // namespace

CString ConfigPath()
{
    CString folder = SvgExportTools::DocumentsFolder() + _T("\\ArqaTools");
    SHCreateDirectoryEx(NULL, folder, NULL);
    CString path = folder + _T("\\ai_config.lua");
    if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) WriteDefaults(path);
    return path;
}

bool SplitUrl(const CString& url, bool& https, CString& host, unsigned short& port, CString& path)
{
    CString rest;
    if (url.Left(8).CompareNoCase(_T("https://")) == 0)     { https = true;  rest = url.Mid(8); port = 443; }
    else if (url.Left(7).CompareNoCase(_T("http://")) == 0) { https = false; rest = url.Mid(7); port = 80;  }
    else return false;

    int slash = rest.Find(_T('/'));
    CString hostPort = slash >= 0 ? rest.Left(slash) : rest;
    path = slash >= 0 ? rest.Mid(slash) : CString(_T("/"));
    int colon = hostPort.ReverseFind(_T(':'));
    if (colon > 0)
    {
        port = static_cast<unsigned short>(_ttoi(hostPort.Mid(colon + 1)));
        hostPort = hostPort.Left(colon);
    }
    host = hostPort;
    return !host.IsEmpty() && port != 0;
}

namespace {

// Runs ai_config.lua in a fresh sandboxed state; the caller reads globals and
// closes it. nullptr + err on failure.
lua_State* RunConfig(CString& err)
{
    CString path = ConfigPath();
    std::string code;
    if (!ReadFile(path, code)) { err = _T("cannot read ") + path; return nullptr; }

    lua_State* L = luaL_newstate();
    if (!L) { err = _T("cannot create a Lua state"); return nullptr; }
    luaL_requiref(L, LUA_GNAME,       luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(L, 1);
    for (const char* name : { "dofile", "loadfile", "load", "require" })
    {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_sethook(L, InstructionCap, LUA_MASKCOUNT, 1000000);
    if (luaL_loadbufferx(L, code.data(), code.size(), "@ai_config.lua", "t") != LUA_OK
        || lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        err = path + _T(": ") + FromUtf8(lua_tostring(L, -1));
        lua_close(L);
        return nullptr;
    }
    lua_sethook(L, nullptr, 0, 0);
    return L;
}

} // namespace

bool Harness(HarnessSettings& s, CString& err)
{
    s = HarnessSettings();
    lua_State* L = RunConfig(err);
    if (!L) return false;
    lua_getglobal(L, "harness");
    if (lua_istable(L, -1))
    {
        int t = lua_gettop(L);
        s.maxAttempts = (std::max)(1, static_cast<int>(NumberField(L, t, "max_attempts", s.maxAttempts)));
        s.testRun     = BoolField(L, t, "test_run", s.testRun);
        s.review      = BoolField(L, t, "review", s.review);
        s.blockOnFail = BoolField(L, t, "block_on_fail", s.blockOnFail);
    }
    lua_close(L);
    return true;
}

bool LoadAll(std::vector<Provider>& providers, CString& active, CString& err)
{
    providers.clear();
    active.Empty();
    CString path = ConfigPath();
    lua_State* L = RunConfig(err);
    if (!L) return false;

    bool ok = false;
    {
        lua_getglobal(L, "active");
        active = lua_type(L, -1) == LUA_TSTRING ? FromUtf8(lua_tostring(L, -1)) : CString();
        lua_pop(L, 1);

        lua_getglobal(L, "providers");
        if (!lua_istable(L, -1))
            err = _T("ai_config.lua must define providers = { ... }");
        else
        {
            ok = true;
            int t = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, t) != 0)
            {
                if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1))
                {
                    Provider p;
                    CString name = FromUtf8(lua_tostring(L, -2));
                    if (!ReadProvider(L, lua_gettop(L), name, p, err)) ok = false;
                    else providers.push_back(p);
                }
                lua_pop(L, 1);
                if (!ok) { lua_pop(L, 1); break; }
            }
        }
    }
    lua_close(L);
    std::sort(providers.begin(), providers.end(),
              [](const Provider& a, const Provider& b) { return a.name.CompareNoCase(b.name) < 0; });
    if (ok && providers.empty()) { err = _T("ai_config.lua defines no providers"); ok = false; }
    if (!ok && !err.IsEmpty()) err = path + _T(": ") + err;
    return ok;
}

bool Active(Provider& p, CString& err)
{
    std::vector<Provider> all;
    CString active;
    if (!LoadAll(all, active, err)) return false;
    for (const Provider& candidate : all)
        if (candidate.name.CompareNoCase(active) == 0) { p = candidate; return true; }
    err = _T("active = \"") + active + _T("\" is not one of the providers in ") + ConfigPath();
    return false;
}

// ── Editing the file ────────────────────────────────────────────────────────
namespace {

bool IsIdent(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Index just past a quoted string or comment starting at i, else i.
size_t SkipStringOrComment(const std::string& s, size_t i)
{
    if (s[i] == '"' || s[i] == '\'')
    {
        char q = s[i];
        for (size_t j = i + 1; j < s.size(); ++j)
        {
            if (s[j] == '\\') { ++j; continue; }
            if (s[j] == q) return j + 1;
        }
        return s.size();
    }
    if (s.compare(i, 2, "--") == 0)
    {
        size_t eol = s.find('\n', i);
        return eol == std::string::npos ? s.size() : eol;
    }
    return i;
}

// Position of `key` followed by `=` (not `==`) at top nesting level within
// [from, to), skipping strings and comments; npos if absent.
size_t FindAssignment(const std::string& s, const std::string& key, size_t from, size_t to, size_t& valueStart)
{
    int depth = 0;
    for (size_t i = from; i < to; )
    {
        size_t skipped = SkipStringOrComment(s, i);
        if (skipped != i) { i = skipped; continue; }
        char c = s[i];
        if (c == '{') { ++depth; ++i; continue; }
        if (c == '}') { --depth; ++i; continue; }
        if (depth == 0 && s.compare(i, key.size(), key) == 0
            && (i == 0 || !IsIdent(s[i - 1])) && (i + key.size() >= s.size() || !IsIdent(s[i + key.size()])))
        {
            size_t j = i + key.size();
            while (j < to && (s[j] == ' ' || s[j] == '\t')) ++j;
            if (j < to && s[j] == '=' && (j + 1 >= to || s[j + 1] != '='))
            {
                ++j;
                while (j < to && (s[j] == ' ' || s[j] == '\t')) ++j;
                valueStart = j;
                return i;
            }
        }
        ++i;
    }
    return std::string::npos;
}

// End of the value starting at `start`: a string literal, a table, or a bare word.
size_t ValueEnd(const std::string& s, size_t start)
{
    if (start >= s.size()) return start;
    if (s[start] == '"' || s[start] == '\'') return SkipStringOrComment(s, start);
    if (s[start] == '{')
    {
        int depth = 0;
        for (size_t i = start; i < s.size(); )
        {
            size_t skipped = SkipStringOrComment(s, i);
            if (skipped != i) { i = skipped; continue; }
            if (s[i] == '{') ++depth;
            if (s[i] == '}' && --depth == 0) return i + 1;
            ++i;
        }
        return s.size();
    }
    size_t i = start;
    while (i < s.size() && s[i] != ',' && s[i] != '\n' && s[i] != '}') ++i;
    return i;
}

bool Rewrite(const CString& path, std::string& text, CString& err)
{
    if (WriteFile(path, text)) return true;
    err = _T("cannot write ") + path;
    return false;
}

} // namespace

bool SetActive(const CString& name, CString& err)
{
    CString path = ConfigPath();
    std::string text;
    if (!ReadFile(path, text)) { err = _T("cannot read ") + path; return false; }
    std::string value = LuaQuote(ToUtf8(name));
    size_t start = 0;
    size_t at = FindAssignment(text, "active", 0, text.size(), start);
    if (at == std::string::npos)
    {
        size_t prov = FindAssignment(text, "providers", 0, text.size(), start);
        text.insert(prov == std::string::npos ? 0 : prov, "active = " + value + "\n\n");
    }
    else
        text.replace(start, ValueEnd(text, start) - start, value);
    return Rewrite(path, text, err);
}

bool SetModel(const CString& provider, const CString& model, CString& err)
{
    CString path = ConfigPath();
    std::string text;
    if (!ReadFile(path, text)) { err = _T("cannot read ") + path; return false; }

    size_t tableStart = 0;
    if (FindAssignment(text, "providers", 0, text.size(), tableStart) == std::string::npos || text[tableStart] != '{')
    { err = _T("no providers = { ... } table in ") + path; return false; }
    size_t tableEnd = ValueEnd(text, tableStart);

    size_t blockStart = 0;
    std::string name = ToUtf8(provider);
    if (FindAssignment(text, name, tableStart + 1, tableEnd - 1, blockStart) == std::string::npos || text[blockStart] != '{')
    { err = _T("provider '") + provider + _T("' not found in ") + path; return false; }
    size_t blockEnd = ValueEnd(text, blockStart);

    std::string value = LuaQuote(ToUtf8(model));
    size_t valueStart = 0;
    if (FindAssignment(text, "model", blockStart + 1, blockEnd - 1, valueStart) == std::string::npos)
        text.insert(blockStart + 1, " model = " + value + ",");
    else
        text.replace(valueStart, ValueEnd(text, valueStart) - valueStart, value);
    return Rewrite(path, text, err);
}

CString Key(const Provider& p)
{
    if (!p.keyEnv.IsEmpty())
    {
        TCHAR buf[1024];
        DWORD n = GetEnvironmentVariable(p.keyEnv, buf, _countof(buf));
        if (n > 0 && n < _countof(buf)) return CString(buf);
    }
    return RegString(kKeysKey, p.name);
}

bool SetKey(const CString& provider, const CString& key)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKeysKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(hKey, provider, 0, REG_SZ, (const BYTE*)(LPCTSTR)key,
                            (key.GetLength() + 1) * sizeof(TCHAR));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

} // namespace AiConfig
