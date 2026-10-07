// AiConfig.h - AI provider configuration in an external Lua file
//
// Documents\ArqaTools\ai_config.lua names the providers the AI commands can
// talk to (URL, request format, auth style, model, vision, limits) and which
// one is active. It is read on every AI request, so an edit applies to the
// next ATAI* / ATAICMD call with no reload and no rebuild. On first use the
// file is written with defaults, carrying over the old registry settings
// (endpoint, model, and the single API key, which moves to that provider).
//
// API keys never go in the file: ATAISETTOKEN stores one per provider under
// HKCU\Software\ArqaToolsPlugin\Keys, or a provider names an environment
// variable (key_env).
//
// The file runs in its own sandboxed Lua state (base/string/table/math, no
// file or OS access, instruction cap) and only data is read back from it.

#pragma once
#include "StdAfx.h"
#include <vector>

namespace AiConfig
{
    struct Provider
    {
        CString name;          // key in `providers`
        CString label;         // shown by ATAISETENDPOINT
        CString url;           // request URL; {model} is replaced by `model`
        CString format;        // "openai" (chat/completions), "gemini" (generateContent) or "anthropic" (messages)
        CString auth;          // "bearer", "x-api-key" (Anthropic), "query" (?key=) or "none"
        CString model;
        bool    vision = false;    // may receive images (ATAICMD's test-run review)
        int     maxTokens = 8192;
        double  temperature = 0.7;
        bool    temperatureSet = false;   // set in the file; "anthropic" sends temperature only then
        int     timeoutSeconds = 180;   // waiting for the reply (models that think first are slow)
        CString keyEnv;       // environment variable holding the key (optional)
        CString keyUrl;        // where to get a key (shown to the user)
        CString modelsUrl;     // GET URL listing models (ATAILISTMODELS), optional
        CString extra;         // raw JSON members appended to the request body, optional

        // `url` split for WinHTTP (after {model} substitution)
        bool           https = true;
        CString        host;
        unsigned short port = 443;
        CString        path;   // path + query
    };

    // How ATAICMD / ATAILUA check AI-written code (AiHarness): the optional
    // `harness = { ... }` table in ai_config.lua. Missing fields keep these defaults.
    struct HarnessSettings
    {
        int  maxAttempts = 3;      // model calls per request, correction rounds included
        bool testRun     = true;   // run the result in a scratch drawing
        bool review      = true;   // second AI call judges it against the request
        bool blockOnFail = false;  // never install/run what the review failed
    };
    bool Harness(HarnessSettings& s, CString& err);

    // The file's path; writes the defaults first if it does not exist.
    CString ConfigPath();

    // Every provider (sorted by name) and the active provider's name.
    bool LoadAll(std::vector<Provider>& providers, CString& active, CString& err);

    // The active provider, ready to use.
    bool Active(Provider& p, CString& err);

    // Rewrite `active = "..."` / that provider's `model = "..."` in the file.
    bool SetActive(const CString& name, CString& err);
    bool SetModel(const CString& provider, const CString& model, CString& err);

    // API key: the key_env variable if set, else the registry entry for the provider.
    CString Key(const Provider& p);
    bool SetKey(const CString& provider, const CString& key);

    // Splits a URL into https/host/port/path.
    bool SplitUrl(const CString& url, bool& https, CString& host, unsigned short& port, CString& path);
}
