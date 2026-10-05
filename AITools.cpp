#include "StdAfx.h"
#include "AITools.h"
#include "AiConfig.h"
#include <winhttp.h>
#include <sstream>
#include <vector>
#include <ShlObj.h>
#include <atlfile.h>
#include <atlenc.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace AITools
{
    // Conversation history storage (limit to last 50 interactions to manage tokens)
    static std::vector<ChatMessage> conversationHistory;
    const int MAX_HISTORY_SIZE = 50;

    // Get conversation history
    std::vector<ChatMessage>& GetConversationHistory()
    {
        return conversationHistory;
    }

    // ATAILUA keeps its own thread: its first message carries the Lua
    // instructions, which must not be skipped because a LISP command already
    // started the shared history (and vice versa).
    static std::vector<ChatMessage> luaConversationHistory;

    std::vector<ChatMessage>& GetLuaConversationHistory()
    {
        return luaConversationHistory;
    }

    // Clear conversation history
    void ClearConversationHistory()
    {
        conversationHistory.clear();
        luaConversationHistory.clear();
    }

    // Provider, model and limits come from ai_config.lua (AiConfig); keys are
    // kept per provider. A configuration error is shown here, since every
    // AI command checks this first.
    bool IsTokenConfigured()
    {
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err))
        {
            acutPrintf(_T("\nAI configuration error: %s\n(ATAICONFIG opens the file.)\n"), (LPCTSTR)err);
            return false;
        }
        return p.auth == _T("none") || !AiConfig::Key(p).IsEmpty();
    }

    // Human-readable hint for the WinHTTP error codes seen in practice when
    // HttpPost's connect/send/receive calls fail - saves a trip to the docs.
    static CString WinHttpErrorHint(DWORD gle)
    {
        switch (gle)
        {
        case 12002: return _T(" - timeout: no reply in time; raise timeout for this provider in ai_config.lua (ATAICONFIG)");
        case 12007: return _T(" - name not resolved (DNS failure / no internet?)");
        case 12029: return _T(" - cannot connect (host unreachable / firewall?)");
        case 12030: return _T(" - connection reset");
        case 12157: return _T(" - secure channel error (TLS handshake failed - often a corporate proxy/firewall doing SSL inspection with an untrusted cert)");
        case 12175: return _T(" - secure failure (TLS negotiation failed - proxy/firewall SSL inspection, or an unsupported TLS version)");
        default:    return _T("");
        }
    }

    // -------------------------------------------------------------------------
    // HttpRequest: GET or POST via WinHTTP. Supports both HTTPS (cloud APIs)
    // and plain HTTP (local Ollama). An empty payload sends no body. Sets
    // statusCode to the HTTP status (0 on error).
    // -------------------------------------------------------------------------
    static CString HttpRequest(const wchar_t* method, const CString& host, INTERNET_PORT port, bool useHttps,
                               const CString& requestPath, const CString& jsonPayload,
                               bool addBearerAuth, const CString& token, DWORD& statusCode,
                               int timeoutSeconds = 180)
    {
        statusCode = 0;

        HINTERNET hSession = WinHttpOpen(L"AutoCAD-AI/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return _T("Error: Could not initialize HTTP session");
        // WinHTTP waits only 30 s for a reply by default - too short for a
        // model that reasons before answering a long prompt.
        int ms = timeoutSeconds * 1000;
        WinHttpSetTimeouts(hSession, 0, 60000, ms, ms);

        HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
        if (!hConnect)
        {
            DWORD gle = GetLastError();
            WinHttpCloseHandle(hSession);
            CString err; err.Format(_T("Error: Could not connect to %s (WinHTTP error %lu%s)"),
                                     (LPCTSTR)host, gle, (LPCTSTR)WinHttpErrorHint(gle));
            return err;
        }

        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, requestPath,
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                flags);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return _T("Error: Could not create request");
        }

        if (!jsonPayload.IsEmpty())
            WinHttpAddRequestHeaders(hRequest, _T("Content-Type: application/json"),
                                     -1, WINHTTP_ADDREQ_FLAG_ADD);
        if (addBearerAuth)
        {
            CString auth = _T("Authorization: Bearer ") + token;
            WinHttpAddRequestHeaders(hRequest, auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
        }

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, jsonPayload, -1, NULL, 0, NULL, NULL);
        std::vector<char> utf8Buf(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, jsonPayload, -1, utf8Buf.data(), utf8Len, NULL, NULL);
        DWORD bodyLen = static_cast<DWORD>(utf8Len - 1);

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                bodyLen ? utf8Buf.data() : WINHTTP_NO_REQUEST_DATA, bodyLen, bodyLen, 0))
        {
            DWORD gle = GetLastError();
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            CString err; err.Format(_T("Error: Could not send request (WinHTTP error %lu%s)"),
                                     gle, (LPCTSTR)WinHttpErrorHint(gle));
            return err;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            DWORD gle = GetLastError();
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            CString err; err.Format(_T("Error: Could not receive response (WinHTTP error %lu%s)"),
                                     gle, (LPCTSTR)WinHttpErrorHint(gle));
            return err;
        }

        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, &statusCode, &statusCodeSize, NULL);

        std::string body;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
        {
            std::vector<char> buf(avail + 1);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buf.data(), avail, &bytesRead))
            { buf[bytesRead] = '\0'; body += buf.data(); }
        }

        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);

        int wideLen = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, NULL, 0);
        std::vector<wchar_t> wideBuf(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, wideBuf.data(), wideLen);
        return CString(wideBuf.data());
    }

    // -------------------------------------------------------------------------
    // FormatHttpError: translate HTTP error status codes into user messages.
    // -------------------------------------------------------------------------
    static CString FormatHttpError(DWORD statusCode, const CString& response, const AiConfig::Provider& p)
    {
        if (statusCode == 429)
        {
            int waitSeconds = 0;
            int waitPos = response.Find(_T("wait "));
            if (waitPos >= 0)
            {
                CString waitStr = response.Mid(waitPos + 5);
                int sp = waitStr.Find(_T(' '));
                if (sp > 0) waitSeconds = _ttoi(waitStr.Left(sp));
            }
            CString limitMsg = _T("Provider: ") + p.name + (p.label.IsEmpty() ? CString() : _T(" (") + p.label + _T(")"))
                             + _T(".\n");
            CString error;
            if (waitSeconds > 0)
                error.Format(_T("Error: Rate limit reached (HTTP 429).\n%sPlease wait: %d hours and %d minutes before trying again."),
                             (LPCTSTR)limitMsg, waitSeconds / 3600, (waitSeconds % 3600) / 60);
            else
                error.Format(_T("Error: Rate limit reached (HTTP 429).\n%sPlease wait before trying again."),
                             (LPCTSTR)limitMsg);
            return error;
        }
        if (statusCode == 401)
            return _T("Error: Unauthorized (HTTP 401). Check the API key for '") + p.name + _T("' with ATAISETTOKEN.");
        if (statusCode == 403)
            return _T("Error: Forbidden (HTTP 403). The key for '") + p.name + _T("' may not have the needed permissions.");
        if (statusCode == 404)
            return _T("Error: Not found (HTTP 404) - check url and model of '") + p.name
                 + _T("' in ai_config.lua (ATAICONFIG). ") + response.Left(300);
        CString error;
        error.Format(_T("Error: HTTP %d - %s"), statusCode, (LPCTSTR)response);
        return error;
    }

    // Helper to escape JSON strings
    CString EscapeJsonString(const CString& str)
    {
        CString result;
        // Pre-allocate to avoid reallocations
        // Worst case: every char needs escaping (x2) plus some overhead
        result.Preallocate(str.GetLength() * 2);
        
        for (int i = 0; i < str.GetLength(); i++)
        {
            TCHAR ch = str[i];
            switch (ch)
            {
                case '\\': result += _T("\\\\"); break;
                case '\"': result += _T("\\\""); break;
                case '\n': result += _T("\\n"); break;
                case '\r': result += _T("\\r"); break;
                case '\t': result += _T("\\t"); break;
                default: result += ch; break;
            }
        }
        return result;
    }
    
    // Extract content from JSON response (simple parser for "content" field)
    CString ExtractContent(const CString& jsonResponse)
    {
        // For Gemini API: look for "parts":[{"text":"..."}]
        int partsPos = jsonResponse.Find(_T("\"parts\""));
        if (partsPos >= 0)
        {
            // Find "text" field after "parts"
            int textPos = jsonResponse.Find(_T("\"text\""), partsPos);
            if (textPos >= 0)
            {
                int colonPos = jsonResponse.Find(_T(':'), textPos);
                if (colonPos >= 0)
                {
                    int quoteStart = jsonResponse.Find(_T('\"'), colonPos);
                    if (quoteStart >= 0)
                    {
                        int quoteEnd = quoteStart + 1;
                        int escapeCount = 0;
                        
                        // Find closing quote, handling escaped quotes
                        while (quoteEnd < jsonResponse.GetLength())
                        {
                            if (jsonResponse[quoteEnd] == '\\')
                            {
                                escapeCount++;
                                quoteEnd++;
                                continue;
                            }
                            if (jsonResponse[quoteEnd] == '\"' && escapeCount % 2 == 0)
                                break;
                            escapeCount = 0;
                            quoteEnd++;
                        }
                        
                        CString content = jsonResponse.Mid(quoteStart + 1, quoteEnd - quoteStart - 1);
                        
                        // Unescape common sequences EXCEPT \n for now (will be removed later for clipboard)
                        content.Replace(_T("\\r"), _T("\r"));
                        content.Replace(_T("\\t"), _T("\t"));
                        content.Replace(_T("\\\""), _T("\""));
                        content.Replace(_T("\\\\"), _T("\\"));
                        
                        return content;
                    }
                }
            }
        }
        
        // For OpenAI/GitHub API: look for "content": "..." pattern
        int contentPos = jsonResponse.Find(_T("\"content\""));
        if (contentPos == -1)
        {
            // Try to find any text in choices
            int textPos = jsonResponse.Find(_T("\"text\""));
            if (textPos == -1)
                return _T("Error: Could not parse response");
            contentPos = textPos;
        }
        
        int colonPos = jsonResponse.Find(_T(':'), contentPos);
        if (colonPos == -1) return _T("Error: Invalid response format");
        
        int quoteStart = jsonResponse.Find(_T('\"'), colonPos);
        if (quoteStart == -1) return _T("Error: Invalid response format");
        
        int quoteEnd = quoteStart + 1;
        int escapeCount = 0;
        
        // Find closing quote, handling escaped quotes
        while (quoteEnd < jsonResponse.GetLength())
        {
            if (jsonResponse[quoteEnd] == '\\')
            {
                escapeCount++;
                quoteEnd++;
                continue;
            }
            if (jsonResponse[quoteEnd] == '\"' && escapeCount % 2 == 0)
                break;
            escapeCount = 0;
            quoteEnd++;
        }
        
        CString content = jsonResponse.Mid(quoteStart + 1, quoteEnd - quoteStart - 1);
        
        // Unescape common sequences EXCEPT \n for now (will be removed later for clipboard)
        content.Replace(_T("\\r"), _T("\r"));
        content.Replace(_T("\\t"), _T("\t"));
        content.Replace(_T("\\\""), _T("\""));
        content.Replace(_T("\\\\"), _T("\\"));
        
        return content;
    }
    
    // Numbers in request JSON always use '.', whatever the user's locale.
    static CString JsonNumber(double v)
    {
        static _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
        TCHAR buf[64];
        _stprintf_s_l(buf, _countof(buf), _T("%.6g"), cLocale, v);
        return CString(buf);
    }

    // One request to the active provider (ai_config.lua). An image rides on
    // the last user message - the only role that may carry one.
    static CString SendRequest(const std::vector<ChatMessage>& messages, const CString* imageBase64)
    {
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err)) return _T("Error: ") + err;

        CString key;
        if (p.auth != _T("none"))
        {
            key = AiConfig::Key(p);
            if (key.IsEmpty())
                return _T("Error: no API key for provider '") + p.name
                     + _T("'. Use ATAISETTOKEN (or set key_env in ai_config.lua).");
        }
        if (imageBase64 && !p.vision)
            return _T("Error: images not supported by ") + p.name + _T(" (") + p.model
                 + _T(") - set vision = true in ai_config.lua if the model reads images");

        int lastUser = -1;
        for (int i = 0; i < static_cast<int>(messages.size()); ++i)
            if (messages[i].role == _T("user")) lastUser = i;

        CString body;
        if (p.format == _T("gemini"))
        {
            // No native history in this request shape: flattened into one labelled prompt.
            CString combined;
            if (messages.size() == 1) combined = messages[0].content;
            else
                for (const auto& m : messages)
                    combined += (m.role == _T("assistant") ? _T("Assistant: ") : _T("User: ")) + m.content + _T("\n\n");
            body = _T("{\"contents\":[{\"parts\":[{\"text\":\"") + EscapeJsonString(combined) + _T("\"}");
            if (imageBase64)
                body += _T(",{\"inline_data\":{\"mime_type\":\"image/png\",\"data\":\"") + *imageBase64 + _T("\"}}");
            body += _T("]}],\"generationConfig\":{\"maxOutputTokens\":") + JsonNumber(p.maxTokens)
                  + _T(",\"temperature\":") + JsonNumber(p.temperature) + _T("}");
        }
        else
        {
            body = _T("{\"model\":\"") + EscapeJsonString(p.model) + _T("\",\"messages\":[");
            for (int i = 0; i < static_cast<int>(messages.size()); ++i)
            {
                if (i > 0) body += _T(",");
                CString text = _T("\"") + EscapeJsonString(messages[i].content) + _T("\"");
                body += _T("{\"role\":\"") + messages[i].role + _T("\",\"content\":");
                if (imageBase64 && i == lastUser)
                    body += _T("[{\"type\":\"text\",\"text\":") + text
                          + _T("},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,")
                          + *imageBase64 + _T("\"}}]");
                else
                    body += text;
                body += _T("}");
            }
            body += _T("],\"temperature\":") + JsonNumber(p.temperature)
                  + _T(",\"max_tokens\":") + JsonNumber(p.maxTokens) + _T(",\"stream\":false");
        }
        if (!p.extra.IsEmpty()) body += _T(",") + p.extra;
        body += _T("}");

        CString path = p.path;
        if (p.auth == _T("query")) path += (path.Find(_T('?')) >= 0 ? _T("&key=") : _T("?key=")) + key;

        DWORD statusCode = 0;
        CString response = HttpRequest(L"POST", p.host, p.port, p.https, path, body,
                                       p.auth == _T("bearer"), key, statusCode, p.timeoutSeconds);
        if (statusCode == 0)   return response;
        if (statusCode != 200) return FormatHttpError(statusCode, response, p);
        return ExtractContent(response);
    }

    // Send prompt to the active provider (single-turn).
    CString SendToGitHubCopilot(const CString& prompt)
    {
        ChatMessage m;
        m.role    = _T("user");
        m.content = prompt;
        return SendRequest({ m }, nullptr);
    }

    // Send conversation with history to the active provider (multi-turn).
    CString SendToGitHubCopilotWithHistory(const std::vector<ChatMessage>& messages)
    {
        return SendRequest(messages, nullptr);
    }

    CString SendWithImage(const CString& prompt, const CString& pngPath)
    {
        CAtlFile file;
        ULONGLONG size = 0;
        if (FAILED(file.Create(pngPath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING)) || FAILED(file.GetSize(size))
            || size == 0 || size > 8 * 1024 * 1024)
            return _T("Error: cannot read image ") + pngPath;
        std::vector<BYTE> bytes(static_cast<size_t>(size));
        if (FAILED(file.Read(bytes.data(), static_cast<DWORD>(size))))
            return _T("Error: cannot read image ") + pngPath;
        int b64Len = Base64EncodeGetRequiredLength(static_cast<int>(size), ATL_BASE64_FLAG_NOCRLF);
        std::string b64(static_cast<size_t>(b64Len), '\0');
        if (!Base64Encode(bytes.data(), static_cast<int>(size), &b64[0], &b64Len, ATL_BASE64_FLAG_NOCRLF))
            return _T("Error: cannot encode image");
        b64.resize(static_cast<size_t>(b64Len));
        CString image(b64.c_str());

        ChatMessage m;
        m.role    = _T("user");
        m.content = prompt;
        return SendRequest({ m }, &image);
    }

    // ATAISETTOKEN - API key for the active provider. Stored in the registry
    // per provider, never in ai_config.lua.
    void aiSetTokenCommand()
    {
        acutPrintf(_T("\n=== SET API KEY ===\n"));
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err)) { acutPrintf(_T("Error: %s\n"), (LPCTSTR)err); return; }
        if (p.auth == _T("none")) { acutPrintf(_T("Provider '%s' needs no key.\n"), (LPCTSTR)p.name); return; }

        acutPrintf(_T("Provider: %s  %s\n"), (LPCTSTR)p.name, (LPCTSTR)p.label);
        if (!p.keyUrl.IsEmpty()) acutPrintf(_T("Get a key from: %s\n"), (LPCTSTR)p.keyUrl);
        if (!p.keyEnv.IsEmpty())
            acutPrintf(_T("Note: key_env = %s in ai_config.lua; that variable wins when it is set.\n"), (LPCTSTR)p.keyEnv);
        acutPrintf(_T("The key is stored in the registry for this provider, not in ai_config.lua.\n"));

        AcString in;
        if (acedGetString(0, _T("\nAPI key (ESC to cancel): "), in) != RTNORM)
        { acutPrintf(_T("\nCommand cancelled.\n")); return; }
        CString key(in.kwszPtr());
        key.Trim();
        if (key.IsEmpty()) { acutPrintf(_T("\nError: the key cannot be empty.\n")); return; }

        if (AiConfig::SetKey(p.name, key))
            acutPrintf(_T("\nKey saved for '%s'. Test it with ATAITEST.\n"), (LPCTSTR)p.name);
        else
            acutPrintf(_T("\nError: could not save the key.\n"));
    }

    // ATAISETENDPOINT - choose the active provider from ai_config.lua
    void aiSetEndpointCommand()
    {
        acutPrintf(_T("\n=== CHOOSE AI PROVIDER ===\n"));
        std::vector<AiConfig::Provider> all;
        CString active, err;
        if (!AiConfig::LoadAll(all, active, err))
        { acutPrintf(_T("Error: %s\nFix the file (ATAICONFIG opens it).\n"), (LPCTSTR)err); return; }

        for (size_t i = 0; i < all.size(); ++i)
            acutPrintf(_T("%s%2d. %-10s %s  [model %s%s]\n"),
                       all[i].name.CompareNoCase(active) == 0 ? _T("*") : _T(" "), static_cast<int>(i + 1),
                       (LPCTSTR)all[i].name, (LPCTSTR)all[i].label, (LPCTSTR)all[i].model,
                       all[i].vision ? _T(", reads images") : _T(""));
        acutPrintf(_T("\nProviders come from %s\nAdd or change them there (ATAICONFIG).\n"), (LPCTSTR)AiConfig::ConfigPath());

        AcString in;
        if (acedGetString(0, _T("\nNumber or name <keep current>: "), in) != RTNORM)
        { acutPrintf(_T("\nCommand cancelled.\n")); return; }
        CString choice(in.kwszPtr());
        choice.Trim();
        if (choice.IsEmpty()) { acutPrintf(_T("\nStill using '%s'.\n"), (LPCTSTR)active); return; }

        const AiConfig::Provider* chosen = nullptr;
        int number = _ttoi(choice);
        if (number >= 1 && number <= static_cast<int>(all.size())) chosen = &all[number - 1];
        for (const auto& p : all)
            if (!chosen && p.name.CompareNoCase(choice) == 0) chosen = &p;
        if (!chosen) { acutPrintf(_T("\nNo provider '%s'.\n"), (LPCTSTR)choice); return; }

        if (!AiConfig::SetActive(chosen->name, err)) { acutPrintf(_T("\nError: %s\n"), (LPCTSTR)err); return; }
        acutPrintf(_T("\nActive provider: %s (model %s)\n"), (LPCTSTR)chosen->name, (LPCTSTR)chosen->model);
        if (chosen->auth != _T("none") && AiConfig::Key(*chosen).IsEmpty())
        {
            acutPrintf(_T("No key stored for '%s' yet: run ATAISETTOKEN.\n"), (LPCTSTR)chosen->name);
            if (!chosen->keyUrl.IsEmpty()) acutPrintf(_T("Get one from: %s\n"), (LPCTSTR)chosen->keyUrl);
        }
        else
            acutPrintf(_T("Test the connection with ATAITEST.\n"));
    }

    // ATAITEST command - Test API connection
    void aiTestCommand()
    {
        acutPrintf(_T("\n=== TEST AI CONNECTION ===\n"));
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err)) { acutPrintf(_T("Error: %s\n"), (LPCTSTR)err); return; }
        acutPrintf(_T("Provider: %s\nURL:      %s\nModel:    %s\nImages:   %s\n"), (LPCTSTR)p.name, (LPCTSTR)p.url,
                   (LPCTSTR)p.model, p.vision ? _T("yes") : _T("no"));
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: no API key for '%s'. Use ATAISETTOKEN.\n"), (LPCTSTR)p.name);
            return;
        }
        acutPrintf(_T("Testing connection with simple prompt...\n"));

        CString response = SendToGitHubCopilot(_T("Say 'Hello from AutoCAD!' in one short sentence."));

        acutPrintf(_T("\n--- API Response ---\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)response);
        acutPrintf(_T("--- End Response ---\n"));
    }

    // ATAILISTMODELS command - List the models the active provider offers (models_url)
    void aiListModelsCommand()
    {
        acutPrintf(_T("\n=== LIST AVAILABLE MODELS ===\n"));
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err)) { acutPrintf(_T("Error: %s\n"), (LPCTSTR)err); return; }
        if (p.modelsUrl.IsEmpty())
        {
            acutPrintf(_T("Provider '%s' has no models_url in ai_config.lua (ATAICONFIG).\n"), (LPCTSTR)p.name);
            return;
        }
        bool https = true;
        CString host, path;
        unsigned short port = 443;
        if (!AiConfig::SplitUrl(p.modelsUrl, https, host, port, path))
        { acutPrintf(_T("Error: invalid models_url %s\n"), (LPCTSTR)p.modelsUrl); return; }

        CString key = p.auth == _T("none") ? CString() : AiConfig::Key(p);
        if (p.auth == _T("query")) path += (path.Find(_T('?')) >= 0 ? _T("&key=") : _T("?key=")) + key;

        acutPrintf(_T("Provider: %s\nFetching %s ...\n\n"), (LPCTSTR)p.name, (LPCTSTR)p.modelsUrl);
        DWORD statusCode = 0;
        CString body = HttpRequest(L"GET", host, port, https, path, CString(), p.auth == _T("bearer"), key, statusCode);
        if (statusCode == 0)        acutPrintf(_T("%s\n"), (LPCTSTR)body);
        else if (statusCode != 200) acutPrintf(_T("%s\n"), (LPCTSTR)FormatHttpError(statusCode, body, p));
        else
        {
            acutPrintf(_T("--- Available Models ---\n"));
            acutPrintf(_T("%s\n"), (LPCTSTR)body);
            acutPrintf(_T("--- End List ---\nSet one with ATAISETMODEL.\n"));
        }
    }

    // ATAICONFIG - open ai_config.lua (written with defaults on first use)
    void aiConfigCommand()
    {
        CString path = AiConfig::ConfigPath();
        acutPrintf(_T("\nAI configuration: %s\n"), (LPCTSTR)path);
        std::vector<AiConfig::Provider> all;
        CString active, err;
        if (AiConfig::LoadAll(all, active, err))
            acutPrintf(_T("Active provider: %s, %d providers. Edits apply to the next AI request.\n"),
                       (LPCTSTR)active, static_cast<int>(all.size()));
        else
            acutPrintf(_T("The file has an error: %s\n"), (LPCTSTR)err);
        ShellExecute(NULL, _T("open"), _T("notepad.exe"), _T("\"") + path + _T("\""), NULL, SW_SHOWNORMAL);
    }

    // ATAIASK command - Ask Copilot a question
    void aiAskCommand()
    {
        acutPrintf(_T("\n=== ASK GITHUB COPILOT ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use ATAISETTOKEN command to set your API key first.\n"));
            return;
        }
        
        TCHAR promptBuffer[2048];
        int result = acedGetString(1, _T("Enter your question (or press ESC to cancel): "), promptBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString prompt(promptBuffer);
        prompt.Trim();
        
        if (prompt.IsEmpty())
        {
            acutPrintf(_T("\nError: Question cannot be empty.\n"));
            return;
        }
        
        acutPrintf(_T("\nSending to Copilot...\n"));
        CString response = SendToGitHubCopilot(prompt);
        
        acutPrintf(_T("\n========================================\n"));
        acutPrintf(_T("COPILOT RESPONSE:\n"));
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)response);
        acutPrintf(_T("========================================\n"));
    }
    
    // Extract AutoCAD commands from AI response
    CString ExtractCommands(const CString& aiResponse)
    {
        CString commands = aiResponse;
        
        // Remove common code block markers
        commands.Replace(_T("```lisp"), _T(""));
        commands.Replace(_T("```autolisp"), _T(""));
        commands.Replace(_T("```autocad"), _T(""));
        commands.Replace(_T("```"), _T(""));
        
        // Remove explanatory text before commands (look for command: pattern)
        int commandPos = commands.Find(_T("COMMAND:"));
        if (commandPos == -1)
            commandPos = commands.Find(_T("Command:"));
        if (commandPos == -1)
            commandPos = commands.Find(_T("command:"));
        
        if (commandPos != -1)
        {
            commands = commands.Mid(commandPos);
            // Remove the "COMMAND:" prefix
            commands.Replace(_T("COMMAND:"), _T(""));
            commands.Replace(_T("Command:"), _T(""));
            commands.Replace(_T("command:"), _T(""));
        }
        
        commands.Trim();
        return commands;
    }
    
    // Get knowledge base of custom commands
    CString GetCustomCommandsKnowledgeBase()
    {
        CString kb;
        kb = _T("Available CUSTOM ArqaTools Plugin commands:\n");
        kb += _T("- ATBOOLPOLY: Boolean operations on polylines (union/subtract/intersect)\n");
        kb += _T("- ATUNIONPOLY: Union of two polylines\n");
        kb += _T("- ATSUBPOLY: Subtract second polyline from first\n");
        kb += _T("- ATINPOLY: Intersection of two polylines\n");
        kb += _T("- ATALX/ATALY/ATALZ: Align objects by X/Y/Z coordinate\n");
        kb += _T("- ATMX/ATMY/ATMZ: Move objects in X/Y/Z direction only (restricted movement)\n");
        kb += _T("- ATCX/ATCY/ATCZ: Copy objects in X/Y/Z direction only (restricted copy)\n");
        kb += _T("- ATDISTLINE: Distribute objects evenly along a line between two points\n");
        kb += _T("- ATDISTBETWEEN: Distribute objects between two points (excludes endpoints)\n");
        kb += _T("- ATDISTEQUAL: Distribute with equal spacing (half-space at ends)\n");
        kb += _T("- ATSEQNUM: Add sequential numbers to selected objects\n");
        kb += _T("- ATINSERTAREA: Insert auto-updating area text in closed polyline\n");
        kb += _T("- ATSUMLENGTH: Insert auto-updating sum of lengths for multiple curves\n");
        kb += _T("- ATCOPYTEXT: Copy text content from one text to others\n");
        kb += _T("- ATCOPYSTYLE: Copy text style properties\n");
        kb += _T("- ATCOPYDIMSTYLE: Copy dimension style\n\n");
        kb += _T("IMPORTANT: When user asks for:\n");
        kb += _T("- 'number objects' or 'sequential numbers' → use ATSEQNUM\n");
        kb += _T("- 'distribute evenly' or 'space objects' → use ATDISTLINE or ATDISTEQUAL\n");
        kb += _T("- 'align objects' → use ATALX/ATALY/ATALZ\n");
        kb += _T("- 'move only in X/Y/Z' → use ATMX/ATMY/ATMZ\n");
        kb += _T("- 'copy only in X/Y/Z' → use ATCX/ATCY/ATCZ\n");
        kb += _T("- 'show area' or 'area label' → use ATINSERTAREA\n");
        kb += _T("- 'sum of lengths' → use ATSUMLENGTH\n");
        kb += _T("- 'combine polylines' or 'merge polylines' → use ATUNIONPOLY\n");
        return kb;
    }
    
    // ATAIDRAW command - Natural language to AutoCAD drawing
    void aiDrawCommand()
    {
        acutPrintf(_T("\n=== AI NATURAL LANGUAGE DRAWING ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use ATAISETTOKEN command to set your API key first.\n"));
            return;
        }
        
        TCHAR promptBuffer[2048];
        int result = acedGetString(1, _T("Describe what to draw (or press ESC to cancel): "), promptBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString userInput(promptBuffer);
        userInput.Trim();
        
        if (userInput.IsEmpty())
        {
            acutPrintf(_T("\nError: Description cannot be empty.\n"));
            return;
        }
        
        // Create a specialized prompt for command generation with custom commands
        CString customKB = GetCustomCommandsKnowledgeBase();
        CString aiPrompt;
        aiPrompt.Format(
            _T("You are an AutoCAD command generator with knowledge of custom ArqaTools plugin commands. ")
            _T("Convert the following natural language instruction into AutoCAD commands.\n\n")
            _T("User wants to: %s\n\n")
            _T("Available STANDARD AutoCAD commands:\n")
            _T("- CIRCLE centerX,centerY radius\n")
            _T("- LINE startX,startY endX,endY\n")
            _T("- RECTANG corner1X,corner1Y corner2X,corner2Y\n")
            _T("- PLINE (polyline - multiple points)\n")
            _T("- ARC (various methods)\n")
            _T("- TEXT position height rotation \"text\"\n")
            _T("- MOVE (select objects, base point, target point)\n")
            _T("- COPY (select objects, base point, target point)\n")
            _T("- ROTATE (select objects, base point, angle)\n\n")
            _T("%s\n")
            _T("Respond with ONLY the commands needed, one per line. No explanations or code blocks.\n\n")
            _T("Examples:\n")
            _T("- For 'draw a circle at 0,0 with radius 5': CIRCLE 0,0 5\n")
            _T("- For 'number the selected objects starting at 1': SEQNUM\n")
            _T("- For 'distribute 5 objects evenly from 0,0 to 100,0': DISTLINE\n")
            _T("- For 'align all objects to X coordinate 50': ALX\n")
            _T("- For 'move objects only in Y direction': MY\n\n")
            _T("COMMAND:"),
            (LPCTSTR)userInput,
            (LPCTSTR)customKB
        );
        
        acutPrintf(_T("\nAsking AI to generate commands...\n"));
        CString response = SendToGitHubCopilot(aiPrompt);
        
        if (response.Find(_T("Error:")) == 0)
        {
            acutPrintf(_T("\n%s\n"), (LPCTSTR)response);
            return;
        }
        
        // Extract and clean commands
        CString commands = ExtractCommands(response);
        
        acutPrintf(_T("\n========================================\n"));
        acutPrintf(_T("AI Generated Commands:\n"));
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)commands);
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("\nCopy and paste the commands above into AutoCAD command line.\n"));
        acutPrintf(_T("Or type them manually to execute.\n"));
    }
    
    // ATAIHELP command - Show AI knowledge base
    void aiHelpCommand()
    {
        acutPrintf(_T("\n=== AI KNOWLEDGE BASE ===\n"));
        acutPrintf(_T("The AI knows about these custom commands:\n\n"));
        
        CString kb = GetCustomCommandsKnowledgeBase();
        acutPrintf(_T("%s\n"), (LPCTSTR)kb);
        
        acutPrintf(_T("\n=== HOW TO USE ===\n"));
        acutPrintf(_T("Type ATAIDRAW and describe what you want in natural language.\n"));
        acutPrintf(_T("Examples:\n"));
        acutPrintf(_T("  'number the selected objects from 1 to 10'\n"));
        acutPrintf(_T("  'distribute 5 circles evenly between two points'\n"));
        acutPrintf(_T("  'align all rectangles to X coordinate 100'\n"));
        acutPrintf(_T("  'show the area of this closed polyline'\n"));
        acutPrintf(_T("  'move these objects only in the Y direction'\n\n"));
        acutPrintf(_T("The AI will suggest the appropriate command!\n"));
    }
    
    // Execute LISP code
    bool ExecuteLispCode(const CString& lispCode)
    {
        if (lispCode.IsEmpty())
            return false;
        
        acutPrintf(_T("\nSaving LISP code to file...\n"));
        
        // Write LISP code to a file in the user's Documents folder
        TCHAR docPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
        
        CString lspFile;
        lspFile.Format(_T("%s\\AI_Generated.lsp"), docPath);
        
        // Write the LISP code to file
        FILE* fp = NULL;
        errno_t err = _tfopen_s(&fp, lspFile, _T("w"));
        if (err != 0 || fp == NULL)
        {
            acutPrintf(_T("Error: Could not create LISP file.\n"));
            return false;
        }
        
        // Write as UTF-8
        fwprintf(fp, _T("%s"), (LPCTSTR)lispCode);
        fclose(fp);
        
        // Convert path to forward slashes (LISP-friendly, handles spaces better)
        CString lspPathLisp = lspFile;
        lspPathLisp.Replace(_T("\\"), _T("/"));
        
        acutPrintf(_T("\n========================================\n"));
        acutPrintf(_T("LISP code saved to: %s\n"), (LPCTSTR)lspFile);
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("\nExecuting LISP file...\n"));
        
        // Build the load command with forward slashes
        CString loadCmd;
        loadCmd.Format(_T("(load \"%s\")"), (LPCTSTR)lspPathLisp);
        
        // Execute using acedInvoke
        struct resbuf rbCode;
        rbCode.restype = RTSTR;
        rbCode.rbnext = NULL;
        
        // Allocate and copy the string
        int len = loadCmd.GetLength() + 1;
        TCHAR* pStr = new TCHAR[len];
        _tcscpy_s(pStr, len, loadCmd);
        rbCode.resval.rstring = pStr;
        
        struct resbuf* pResult = NULL;
        int rc = acedInvoke(&rbCode, &pResult);
        
        delete[] pStr;
        
        if (rc == RTNORM)
        {
            acutPrintf(_T("LISP executed successfully!\n"));
            if (pResult != NULL)
            {
                acutRelRb(pResult);
            }
        }
        else
        {
            // Copy load command to clipboard
            if (OpenClipboard(NULL))
            {
                EmptyClipboard();
                
                size_t cmdLen = (loadCmd.GetLength() + 1) * sizeof(TCHAR);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cmdLen);
                
                if (hMem != NULL)
                {
                    LPTSTR pMem = (LPTSTR)GlobalLock(hMem);
                    if (pMem != NULL)
                    {
                        _tcscpy_s(pMem, loadCmd.GetLength() + 1, loadCmd);
                        GlobalUnlock(hMem);
                        
#ifdef UNICODE
                        SetClipboardData(CF_UNICODETEXT, hMem);
#else
                        SetClipboardData(CF_TEXT, hMem);
#endif
                    }
                }
                
                CloseClipboard();
                
                acutPrintf(_T("\n✓ Load command copied to clipboard!\n"));
                acutPrintf(_T("Just paste (Ctrl+V) in AutoCAD command line:\n"));
                acutPrintf(_T("%s\n"), (LPCTSTR)loadCmd);
            }
            else
            {
                acutPrintf(_T("\nAutomatic execution failed. Use this command manually:\n"));
                acutPrintf(_T("%s\n"), (LPCTSTR)loadCmd);
            }
            
            if (pResult != NULL)
            {
                acutRelRb(pResult);
            }
        }
        
        return true;
    }
    
    // ATAILISP command - Natural language to LISP code
    void aiLispCommand()
    {
        acutPrintf(_T("\n=== AI LISP CODE GENERATOR ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use ATAISETTOKEN command to set your API key first.\n"));
            return;
        }
        
        TCHAR promptBuffer[2048];
        int result = acedGetString(1, _T("Describe what to do (or press ESC to cancel): "), promptBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString userInput(promptBuffer);
        userInput.Trim();
        
        if (userInput.IsEmpty())
        {
            acutPrintf(_T("\nError: Description cannot be empty.\n"));
            return;
        }
        
        // Create specialized prompt for LISP generation
        CString customKB = GetCustomCommandsKnowledgeBase();
        CString aiPrompt;
        aiPrompt.Format(
            _T("You are an AutoLISP code generator for AutoCAD. Generate AutoLISP code to accomplish the following task.\n\n")
            _T("User wants to: %s\n\n")
            _T("%s\n")
            _T("IMPORTANT LISP FUNCTIONS:\n")
            _T("- (command \"CIRCLE\" pt1 radius) - Draw circle\n")
            _T("- (command \"LINE\" pt1 pt2 \"\") - Draw line\n")
            _T("- (command \"RECTANG\" pt1 pt2) - Draw rectangle\n")
            _T("- (command \"TEXT\" insertPt height rotation textString) - Draw text\n")
            _T("- (command \"SEQNUM\") - Number objects (custom command)\n")
            _T("- (command \"DISTLINE\") - Distribute objects (custom command)\n")
            _T("- (command \"ALX\") - Align X (custom command)\n")
            _T("- (setq ss (ssget)) - Select objects\n")
            _T("- (setq pt (getpoint)) - Get point from user\n")
            _T("- (setq dist (getdist)) - Get distance from user\n")
            _T("- (setq num (getint)) - Get integer from user\n")
            _T("- (repeat n (expression)) - Loop n times\n")
            _T("- (while condition (expression)) - Conditional loop\n")
            _T("- (setq counter 1) - Initialize counter variable\n")
            _T("- (setq counter (1+ counter)) - Increment counter by 1\n")
            _T("- (itoa number) - Convert integer to string (for TEXT command)\n\n")
            _T("CRITICAL RULES FOR SEQUENTIAL NUMBERS:\n")
            _T("1. ALWAYS use a counter variable for sequential numbers:\n")
            _T("   CORRECT: (setq n 1) (repeat 10 (progn (command \"TEXT\" pt height 0 (itoa n)) (setq n (1+ n))))\n")
            _T("   WRONG: Using (rem (getvar \"CMDECHO\") ...) or random functions\n")
            _T("2. Counter MUST increment inside the loop: (setq n (1+ n))\n")
            _T("3. Use (itoa counter) to convert number to string for TEXT command\n\n")
            _T("Generate COMPLETE, WORKING AutoLISP code. CRITICAL REQUIREMENTS:\n")
            _T("1. ALL variables MUST be declared in the local variable list: ( / var1 var2 var3 ...)\n")
            _T("2. Include ALL variables used anywhere in the code (no undefined variables)\n")
            _T("3. Check every variable used - pt1, pt2, mid_pt, ang, end1, end2, etc.\n")
            _T("4. Code must be syntactically complete - no missing parentheses\n")
            _T("5. Do NOT include: explanations, comments outside code, or ```lisp markers\n")
            _T("6. Do NOT use (defun c:...) wrapper unless specifically needed\n")
            _T("7. The code should execute immediately when pasted into AutoCAD\n\n")
            _T("EXAMPLE of proper local variable declaration:\n")
            _T("  (defun c:TEST ( / pt1 pt2 dist mid ang )  ; ALL variables declared here\n")
            _T("    (setq pt1 (getpoint))\n")
            _T("    (setq pt2 (getpoint))\n")
            _T("    (setq dist (distance pt1 pt2))\n")
            _T("    (setq mid (list (/ (+ (car pt1) (car pt2)) 2) (/ (+ (cadr pt1) (cadr pt2)) 2)))\n")
            _T("    (setq ang (angle pt1 pt2))\n")
            _T("  )\n\n")
            _T("Examples:\n")
            _T("- For 'draw numbers 1 to 5 at position 0,0 moving Y by 5':\n")
            _T("  (progn (setq x 0 y 0 n 1) (repeat 5 (progn (command \"TEXT\" (list x y 0) 2.5 0 (itoa n)) (setq y (+ y 5) n (1+ n)))))\n")
            _T("- For 'draw 3 circles at 10,10 with radius 5,10,15':\n")
            _T("  (progn (setq r 5) (repeat 3 (progn (command \"CIRCLE\" '(10 10 0) r) (setq r (+ r 5)))))\n\n")
            _T("CODE:"),
            (LPCTSTR)userInput,
            (LPCTSTR)customKB
        );
        
        acutPrintf(_T("\nAsking AI to generate LISP code...\n"));
        
        // Build messages array with conversation history
        std::vector<ChatMessage>& history = GetConversationHistory();
        std::vector<ChatMessage> messages;
        
        // Check if this is first interaction
        bool isFirstInteraction = (history.size() == 0);
        
        CString userPrompt;
        if (isFirstInteraction)
        {
            // First time: Send full instructions + request
            userPrompt = aiPrompt;
            acutPrintf(_T("Starting new conversation with full instructions.\n"));
        }
        else
        {
            // Subsequent times: Only send the user's actual request
            userPrompt = userInput;
            acutPrintf(_T("Using conversation history (%d previous interactions)\n"), history.size() / 2);
        }
        
        // Add conversation history
        for (const auto& msg : history)
        {
            messages.push_back(msg);
        }
        
        // Add current user request
        ChatMessage userMsg;
        userMsg.role = _T("user");
        userMsg.content = userPrompt;
        messages.push_back(userMsg);
        
        // Send with history
        CString response = SendToGitHubCopilotWithHistory(messages);
        
        // Log the interaction to file
        TCHAR logDocPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, logDocPath);
        CString logFile;
        logFile.Format(_T("%s\\AI_Interactions_Log.txt"), logDocPath);
        
        FILE* logFp = NULL;
        errno_t logErr = _tfopen_s(&logFp, logFile, _T("a"));
        if (logErr == 0 && logFp != NULL)
        {
            // Get current timestamp
            SYSTEMTIME st;
            GetLocalTime(&st);
            
            fwprintf(logFp, _T("\n========================================\n"));
            fwprintf(logFp, _T("TIMESTAMP: %04d-%02d-%02d %02d:%02d:%02d\n"), 
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fwprintf(logFp, _T("========================================\n"));
            fwprintf(logFp, _T("USER INPUT:\n%s\n\n"), (LPCTSTR)userInput);
            
            // Log what was actually sent (not the full prompt if using history)
            if (isFirstInteraction)
            {
                fwprintf(logFp, _T("SENT TO AI (First interaction - full instructions):\n%s\n\n"), (LPCTSTR)userPrompt);
            }
            else
            {
                fwprintf(logFp, _T("SENT TO AI (with history):\n%s\n\n"), (LPCTSTR)userPrompt);
                fwprintf(logFp, _T("CONVERSATION HISTORY SIZE: %zu interactions\n\n"), history.size() / 2);
            }
            
            fwprintf(logFp, _T("AI RESPONSE:\n%s\n"), (LPCTSTR)response);
            fwprintf(logFp, _T("========================================\n\n"));
            fclose(logFp);
        }
        
        if (response.Find(_T("Error:")) == 0)
        {
            acutPrintf(_T("\n%s\n"), (LPCTSTR)response);
            return;
        }
        
        // Validate response is not empty
        if (response.IsEmpty() || response.GetLength() < 3)
        {
            acutPrintf(_T("\nError: Received empty or invalid response from AI.\n"));
            return;
        }
        
        // Clean up the response
        CString lispCode = response;
        lispCode.Replace(_T("```lisp"), _T(""));
        lispCode.Replace(_T("```autolisp"), _T(""));
        lispCode.Replace(_T("```"), _T(""));
        
        // Remove literal \n and \r from JSON
        lispCode.Replace(_T("\\n"), _T(" "));
        lispCode.Replace(_T("\\r"), _T(" "));
        lispCode.Replace(_T("\\t"), _T(" "));
        
        // Unescape Unicode sequences that AI might generate
        lispCode.Replace(_T("\\u003c"), _T("<"));
        lispCode.Replace(_T("\\u003e"), _T(">"));
        lispCode.Replace(_T("\\u003d"), _T("="));
        lispCode.Replace(_T("\\u0022"), _T("\""));
        lispCode.Replace(_T("\\u0027"), _T("'"));
        lispCode.Replace(_T("\\u002b"), _T("+"));
        lispCode.Replace(_T("\\u002d"), _T("-"));
        lispCode.Replace(_T("\\u002a"), _T("*"));
        lispCode.Replace(_T("\\u002f"), _T("/"));
        
        lispCode.Trim();
        
        // Remove CODE: prefix if present
        if (lispCode.Find(_T("CODE:")) == 0)
            lispCode = lispCode.Mid(5);
        lispCode.Trim();
        
        // Remove any explanatory text before the actual code
        int parenPos = lispCode.Find(_T('('));
        if (parenPos > 0)
        {
            lispCode = lispCode.Mid(parenPos);
            lispCode.Trim();
        }
        
        // Final validation - make sure we have valid LISP code
        if (lispCode.IsEmpty() || lispCode[0] != _T('('))
        {
            acutPrintf(_T("\nError: AI response does not contain valid LISP code.\n"));
            acutPrintf(_T("Response received: %s\n"), (LPCTSTR)response);
            return;
        }
        
        // Create clean single-line version for file (do this BEFORE displaying)
        CString cleanCode = lispCode;
        
        // CRITICAL: Remove semicolon comments (they break single-line code)
        // Find and remove everything from ; to the end of each line
        int commentPos = 0;
        while ((commentPos = cleanCode.Find(_T(';'), commentPos)) != -1)
        {
            // Find the end of this line (or end of string)
            int lineEnd = cleanCode.Find(_T('\n'), commentPos);
            if (lineEnd == -1)
                lineEnd = cleanCode.Find(_T('\r'), commentPos);
            
            if (lineEnd == -1)
            {
                // Comment goes to end of string, remove from ; onwards
                cleanCode = cleanCode.Left(commentPos);
                break;
            }
            else
            {
                // Remove from ; to end of line
                cleanCode.Delete(commentPos, lineEnd - commentPos);
            }
        }
        
        // Remove any remaining newlines
        cleanCode.Replace(_T("\r\n"), _T(" "));
        cleanCode.Replace(_T("\n"), _T(" "));
        cleanCode.Replace(_T("\r"), _T(" "));
        
        // Remove extra spaces
        while (cleanCode.Find(_T("  ")) >= 0)
            cleanCode.Replace(_T("  "), _T(" "));
        cleanCode.Trim();
        
        // Display the CLEANED code (what will actually be executed)
        acutPrintf(_T("\n========================================\n"));
        acutPrintf(_T("AI Generated LISP Code (cleaned):\n"));
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)cleanCode);
        acutPrintf(_T("========================================\n"));
        
        // Save LISP code to file in Documents folder
        TCHAR docPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
        
        CString lspFile;
        lspFile.Format(_T("%s\\AI_Generated.lsp"), docPath);
        
        FILE* fp = NULL;
        errno_t err = _tfopen_s(&fp, lspFile, _T("w"));
        if (err == 0 && fp != NULL)
        {
            fwprintf(fp, _T("%s"), (LPCTSTR)cleanCode);
            fclose(fp);
            
            // Convert path to forward slashes (LISP-friendly)
            CString lspPathLisp = lspFile;
            lspPathLisp.Replace(_T("\\"), _T("/"));
            
            // Create load command
            CString loadCmd;
            loadCmd.Format(_T("(load \"%s\")"), (LPCTSTR)lspPathLisp);
            
            // Copy load command to clipboard
            bool clipboardSuccess = false;
            if (OpenClipboard(NULL))
            {
                EmptyClipboard();
                
                size_t cmdLen = (loadCmd.GetLength() + 1) * sizeof(TCHAR);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cmdLen);
                
                if (hMem != NULL)
                {
                    LPTSTR pMem = (LPTSTR)GlobalLock(hMem);
                    if (pMem != NULL)
                    {
                        _tcscpy_s(pMem, loadCmd.GetLength() + 1, loadCmd);
                        GlobalUnlock(hMem);
                        
#ifdef UNICODE
                        HANDLE result = SetClipboardData(CF_UNICODETEXT, hMem);
#else
                        HANDLE result = SetClipboardData(CF_TEXT, hMem);
#endif
                        if (result != NULL)
                        {
                            clipboardSuccess = true;
                        }
                    }
                    else
                    {
                        GlobalFree(hMem);
                    }
                }
                
                CloseClipboard();
            }
            
            if (clipboardSuccess)
            {
                acutPrintf(_T("\n✓ LISP saved and load command copied to clipboard!\n"));
                acutPrintf(_T("Just paste (Ctrl+V) in AutoCAD command line:\n"));
                acutPrintf(_T("%s\n"), (LPCTSTR)loadCmd);
            }
            else
            {
                acutPrintf(_T("\n✓ LISP saved to: %s\n"), (LPCTSTR)lspFile);
                acutPrintf(_T("Execute this command:\n%s\n"), (LPCTSTR)loadCmd);
            }
            
            // Add this interaction to conversation history
            ChatMessage assistantMsg;
            assistantMsg.role = _T("assistant");
            assistantMsg.content = response;
            
            // Add user message and assistant response to history
            history.push_back(userMsg);
            history.push_back(assistantMsg);
            
            // Limit history size (keep last MAX_HISTORY_SIZE*2 messages)
            while (history.size() > MAX_HISTORY_SIZE * 2)
            {
                history.erase(history.begin());
            }
            
            acutPrintf(_T("(Conversation history: %d interactions. Use ATAICLEAR to reset)\n"), history.size() / 2);
        }
        else
        {
            acutPrintf(_T("\n⚠ Could not save LISP file. Copy this code manually:\n\n"));
            acutPrintf(_T("%s\n\n"), (LPCTSTR)cleanCode);
        }
    }
    
    // ATAIFIX command - Report error and ask AI to fix the last generated code
    void aiFixCommand()
    {
        acutPrintf(_T("\n=== AI ERROR REPORTER & FIXER ===\n"));
        
        std::vector<ChatMessage>& history = GetConversationHistory();
        
        if (history.size() == 0)
        {
            acutPrintf(_T("Error: No conversation history. Use ATAILISP first.\n"));
            return;
        }
        
        acutPrintf(_T("Describe the error (what went wrong):\n"));
        
        TCHAR errorBuffer[1024];
        int result = acedGetString(1, _T("Error description: "), errorBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString errorDescription(errorBuffer);
        errorDescription.Trim();
        
        if (errorDescription.IsEmpty())
        {
            acutPrintf(_T("\nError: Description cannot be empty.\n"));
            return;
        }
        
        // Build error feedback message
        CString feedbackMsg;
        feedbackMsg.Format(_T("ERROR REPORT: The previous code had this error: %s\n\n")
                          _T("Please analyze the error, explain what went wrong, and generate CORRECTED code that fixes this issue.\n")
                          _T("Make sure to:\n")
                          _T("1. Wrap ALL repeat/while body expressions with (progn ...) if multiple expressions\n")
                          _T("2. Declare ALL variables in the local variable list\n")
                          _T("3. Check for missing parentheses\n")
                          _T("4. Verify all LISP syntax is correct\n\n")
                          _T("Generate the CORRECTED code now:"),
                          (LPCTSTR)errorDescription);
        
        acutPrintf(_T("\nReporting error to AI and requesting fix...\n"));
        
        // Build messages with history
        std::vector<ChatMessage> messages;
        for (const auto& msg : history)
        {
            messages.push_back(msg);
        }
        
        // Add error feedback
        ChatMessage errorMsg;
        errorMsg.role = _T("user");
        errorMsg.content = feedbackMsg;
        messages.push_back(errorMsg);
        
        // Send with history
        CString response = SendToGitHubCopilotWithHistory(messages);
        
        if (response.Find(_T("Error:")) == 0)
        {
            acutPrintf(_T("\n%s\n"), (LPCTSTR)response);
            return;
        }
        
        // Clean up the response
        CString lispCode = response;
        lispCode.Replace(_T("```lisp"), _T(""));
        lispCode.Replace(_T("```autolisp"), _T(""));
        lispCode.Replace(_T("```"), _T(""));
        lispCode.Replace(_T("\\n"), _T(" "));
        lispCode.Replace(_T("\\r"), _T(" "));
        lispCode.Replace(_T("\\t"), _T(" "));
        lispCode.Replace(_T("\\u003c"), _T("<"));
        lispCode.Replace(_T("\\u003e"), _T(">"));
        lispCode.Replace(_T("\\u003d"), _T("="));
        lispCode.Replace(_T("\\u0022"), _T("\""));
        lispCode.Replace(_T("\\u0027"), _T("'"));
        lispCode.Replace(_T("\\u002b"), _T("+"));
        lispCode.Replace(_T("\\u002d"), _T("-"));
        lispCode.Replace(_T("\\u002a"), _T("*"));
        lispCode.Replace(_T("\\u002f"), _T("/"));
        lispCode.Trim();
        
        if (lispCode.Find(_T("CODE:")) == 0)
            lispCode = lispCode.Mid(5);
        lispCode.Trim();
        
        int parenPos = lispCode.Find(_T('('));
        if (parenPos > 0)
        {
            lispCode = lispCode.Mid(parenPos);
            lispCode.Trim();
        }
        
        if (lispCode.IsEmpty() || lispCode[0] != _T('('))
        {
            acutPrintf(_T("\nAI response (may include explanation):\n%s\n"), (LPCTSTR)response);
            return;
        }
        
        // Create clean code
        CString cleanCode = lispCode;
        
        // Remove semicolon comments
        int commentPos = 0;
        while ((commentPos = cleanCode.Find(_T(';'), commentPos)) != -1)
        {
            int lineEnd = cleanCode.Find(_T('\n'), commentPos);
            if (lineEnd == -1)
                lineEnd = cleanCode.Find(_T('\r'), commentPos);
            
            if (lineEnd == -1)
            {
                cleanCode = cleanCode.Left(commentPos);
                break;
            }
            else
            {
                cleanCode.Delete(commentPos, lineEnd - commentPos);
            }
        }
        
        cleanCode.Replace(_T("\r\n"), _T(" "));
        cleanCode.Replace(_T("\n"), _T(" "));
        cleanCode.Replace(_T("\r"), _T(" "));
        
        while (cleanCode.Find(_T("  ")) >= 0)
            cleanCode.Replace(_T("  "), _T(" "));
        cleanCode.Trim();
        
        acutPrintf(_T("\n========================================\n"));
        acutPrintf(_T("AI Generated CORRECTED LISP Code:\n"));
        acutPrintf(_T("========================================\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)cleanCode);
        acutPrintf(_T("========================================\n"));
        
        // Save to file
        TCHAR docPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
        CString lspFile;
        lspFile.Format(_T("%s\\AI_Generated.lsp"), docPath);
        
        FILE* fp = NULL;
        errno_t err = _tfopen_s(&fp, lspFile, _T("w"));
        if (err == 0 && fp != NULL)
        {
            fwprintf(fp, _T("%s"), (LPCTSTR)cleanCode);
            fclose(fp);
            
            CString lspPathLisp = lspFile;
            lspPathLisp.Replace(_T("\\"), _T("/"));
            
            CString loadCmd;
            loadCmd.Format(_T("(load \"%s\")"), (LPCTSTR)lspPathLisp);
            
            bool clipboardSuccess = false;
            if (OpenClipboard(NULL))
            {
                EmptyClipboard();
                
                size_t size = (loadCmd.GetLength() + 1) * sizeof(TCHAR);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                
                if (hMem != NULL)
                {
                    LPTSTR pMem = (LPTSTR)GlobalLock(hMem);
                    if (pMem != NULL)
                    {
                        _tcscpy_s(pMem, loadCmd.GetLength() + 1, loadCmd);
                        GlobalUnlock(hMem);
                        
#ifdef UNICODE
                        HANDLE clipResult = SetClipboardData(CF_UNICODETEXT, hMem);
#else
                        HANDLE clipResult = SetClipboardData(CF_TEXT, hMem);
#endif
                        if (clipResult != NULL)
                        {
                            clipboardSuccess = true;
                        }
                    }
                    else
                    {
                        GlobalFree(hMem);
                    }
                }
                
                CloseClipboard();
            }
            
            if (clipboardSuccess)
            {
                acutPrintf(_T("\n✓ CORRECTED LISP saved and load command copied!\n"));
                acutPrintf(_T("Paste (Ctrl+V) to test the fix:\n"));
                acutPrintf(_T("%s\n"), (LPCTSTR)loadCmd);
            }
            else
            {
                acutPrintf(_T("\n✓ CORRECTED LISP saved to: %s\n"), (LPCTSTR)lspFile);
                acutPrintf(_T("Execute: %s\n"), (LPCTSTR)loadCmd);
            }
            
            // Add error feedback and correction to history
            ChatMessage assistantMsg;
            assistantMsg.role = _T("assistant");
            assistantMsg.content = response;
            
            history.push_back(errorMsg);
            history.push_back(assistantMsg);
            
            while (history.size() > MAX_HISTORY_SIZE * 2)
            {
                history.erase(history.begin());
            }
            
            acutPrintf(_T("(Error reported to AI. Future code will avoid this mistake)\n"));
        }
        else
        {
            acutPrintf(_T("\n⚠ Could not save file. Copy manually:\n%s\n"), (LPCTSTR)cleanCode);
        }
    }
    
    // ATAICLEAR command - Clear conversation history
    void aiClearHistoryCommand()
    {
        ClearConversationHistory();
        acutPrintf(_T("\n=== CONVERSATION HISTORY CLEARED ===\n"));
        acutPrintf(_T("AI will start fresh with no memory of previous interactions.\n"));
    }

    // ATAISETMODEL - set the active provider's model in ai_config.lua
    void aiSetModelCommand()
    {
        acutPrintf(_T("\n=== SET AI MODEL ===\n"));
        AiConfig::Provider p;
        CString err;
        if (!AiConfig::Active(p, err)) { acutPrintf(_T("Error: %s\n"), (LPCTSTR)err); return; }
        acutPrintf(_T("Provider: %s\nCurrent model: %s\n"), (LPCTSTR)p.name, (LPCTSTR)p.model);
        if (!p.modelsUrl.IsEmpty()) acutPrintf(_T("ATAILISTMODELS lists the models this provider offers.\n"));

        AcString in;
        if (acedGetString(0, _T("\nModel name (ESC to cancel): "), in) != RTNORM)
        { acutPrintf(_T("\nCommand cancelled.\n")); return; }
        CString model(in.kwszPtr());
        model.Trim();
        if (model.IsEmpty()) { acutPrintf(_T("\nError: Model name cannot be empty.\n")); return; }

        if (!AiConfig::SetModel(p.name, model, err)) { acutPrintf(_T("\nError: %s\n"), (LPCTSTR)err); return; }
        acutPrintf(_T("\nModel for '%s' set to '%s' in ai_config.lua. Use ATAITEST to verify.\n"),
                   (LPCTSTR)p.name, (LPCTSTR)model);
        acutPrintf(_T("vision = %s for this provider: change it in ai_config.lua (ATAICONFIG) if this model %s images.\n"),
                   p.vision ? _T("true") : _T("false"), p.vision ? _T("cannot read") : _T("can read"));
    }
}
