#include "StdAfx.h"
#include "AITools.h"
#include <winhttp.h>
#include <sstream>
#include <vector>
#include <ShlObj.h>

#pragma comment(lib, "winhttp.lib")

namespace AITools
{
    // Store token in registry for persistence
    const wchar_t* REGISTRY_KEY = L"Software\\HelloWorldPlugin";
    const wchar_t* TOKEN_VALUE = L"GitHubToken";
    const wchar_t* ENDPOINT_VALUE = L"APIEndpoint";
    const wchar_t* MODEL_VALUE = L"AIModel";
    const wchar_t* DEFAULT_ENDPOINT = L"models.inference.ai.azure.com";
    const wchar_t* DEFAULT_OLLAMA_ENDPOINT = L"localhost:11434";

    // Cache for registry values
    static CString s_cachedToken;
    static bool s_tokenCached = false;
    static CString s_cachedEndpoint;
    static bool s_endpointCached = false;
    static CString s_cachedModel;
    static bool s_modelCached = false;
    
    // Conversation history storage (limit to last 50 interactions to manage tokens)
    static std::vector<ChatMessage> conversationHistory;
    const int MAX_HISTORY_SIZE = 50;
    
    // Get conversation history
    std::vector<ChatMessage>& GetConversationHistory()
    {
        return conversationHistory;
    }
    
    // Clear conversation history
    void ClearConversationHistory()
    {
        conversationHistory.clear();
    }
    
    // Set the API token in registry
    bool SetAPIToken(const CString& token)
    {
        HKEY hKey;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, 
                                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        
        if (result != ERROR_SUCCESS)
        {
            acutPrintf(_T("\nError: Could not create registry key.\n"));
            return false;
        }
        
        result = RegSetValueExW(hKey, TOKEN_VALUE, 0, REG_SZ, 
                               (BYTE*)(LPCTSTR)token, (token.GetLength() + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
        
        if (result == ERROR_SUCCESS)
        {
            // Update cache
            s_cachedToken = token;
            s_tokenCached = true;
            
            acutPrintf(_T("\nAPI token saved successfully!\n"));
            return true;
        }
        else
        {
            acutPrintf(_T("\nError: Could not save token.\n"));
            return false;
        }
    }
    
    // Get the API token from registry
    CString GetAPIToken()
    {
        // Return cached value if available
        if (s_tokenCached)
            return s_cachedToken;

        HKEY hKey;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey);
        
        if (result != ERROR_SUCCESS)
            return _T("");
        
        WCHAR buffer[512];
        DWORD bufferSize = sizeof(buffer);
        result = RegQueryValueExW(hKey, TOKEN_VALUE, NULL, NULL, (LPBYTE)buffer, &bufferSize);
        RegCloseKey(hKey);
        
        if (result == ERROR_SUCCESS)
        {
            s_cachedToken = CString(buffer);
            s_tokenCached = true;
            return s_cachedToken;
        }
        
        return _T("");
    }
    
    // Returns true if the endpoint resolves to a local Ollama instance.
    static bool IsOllama(const CString& endpoint)
    {
        return (endpoint.Find(_T("localhost")) >= 0 ||
                endpoint.Find(_T("127.0.0.1")) >= 0);
    }

    // Splits "host:port" → host, port, useHttps.
    // Plain HTTP is used for localhost; everything else defaults to HTTPS/443.
    static void ParseEndpoint(const CString& rawEndpoint,
                              CString& host, INTERNET_PORT& port, bool& useHttps)
    {
        host = rawEndpoint;
        int colonPos = host.ReverseFind(_T(':'));
        if (colonPos > 0)
        {
            port = (INTERNET_PORT)_ttoi(host.Mid(colonPos + 1));
            host = host.Left(colonPos);
        }
        else
        {
            port = INTERNET_DEFAULT_HTTPS_PORT;
        }
        useHttps = !IsOllama(rawEndpoint);
        if (!useHttps && port == INTERNET_DEFAULT_HTTPS_PORT)
            port = 11434;
    }

    // Get / Set the active model name in registry
    bool SetAPIModel(const CString& model)
    {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        { acutPrintf(_T("\nError: Could not create registry key.\n")); return false; }

        LONG r = RegSetValueExW(hKey, MODEL_VALUE, 0, REG_SZ,
                                (BYTE*)(LPCTSTR)model, (model.GetLength() + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
        if (r == ERROR_SUCCESS)
        { s_cachedModel = model; s_modelCached = true;
          acutPrintf(_T("\nModel saved: %s\n"), (LPCTSTR)model); return true; }
        acutPrintf(_T("\nError: Could not save model.\n")); return false;
    }

    CString GetAPIModel()
    {
        if (s_modelCached) return s_cachedModel;

        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            WCHAR buf[256]; DWORD sz = sizeof(buf);
            if (RegQueryValueExW(hKey, MODEL_VALUE, NULL, NULL, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            { RegCloseKey(hKey); s_cachedModel = CString(buf); s_modelCached = true; return s_cachedModel; }
            RegCloseKey(hKey);
        }
        // Default: gpt-4o for cloud, empty means Ollama will be prompted
        s_cachedModel = _T("gpt-4o");
        s_modelCached = true;
        return s_cachedModel;
    }

    // Check if token is configured (Ollama needs no token)
    bool IsTokenConfigured()
    {
        if (IsOllama(GetAPIEndpoint())) return true;
        return !GetAPIToken().IsEmpty();
    }
    
    // Set the API endpoint in registry
    bool SetAPIEndpoint(const CString& endpoint)
    {
        HKEY hKey;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, 
                                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        
        if (result != ERROR_SUCCESS)
        {
            acutPrintf(_T("\nError: Could not create registry key.\n"));
            return false;
        }
        
        result = RegSetValueExW(hKey, ENDPOINT_VALUE, 0, REG_SZ, 
                               (BYTE*)(LPCTSTR)endpoint, (endpoint.GetLength() + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
        
        if (result == ERROR_SUCCESS)
        {
            // Update cache
            s_cachedEndpoint = endpoint;
            s_endpointCached = true;
            
            acutPrintf(_T("\nAPI endpoint saved successfully!\n"));
            return true;
        }
        else
        {
            acutPrintf(_T("\nError: Could not save endpoint.\n"));
            return false;
        }
    }
    
    // Get the API endpoint from registry
    CString GetAPIEndpoint()
    {
        // Return cached value if available
        if (s_endpointCached)
            return s_cachedEndpoint;

        HKEY hKey;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey);
        
        if (result != ERROR_SUCCESS)
        {
            s_cachedEndpoint = CString(DEFAULT_ENDPOINT);
            s_endpointCached = true;
            return s_cachedEndpoint;
        }
        
        WCHAR buffer[512];
        DWORD bufferSize = sizeof(buffer);
        result = RegQueryValueExW(hKey, ENDPOINT_VALUE, NULL, NULL, (LPBYTE)buffer, &bufferSize);
        RegCloseKey(hKey);
        
        if (result == ERROR_SUCCESS)
        {
            s_cachedEndpoint = CString(buffer);
            s_endpointCached = true;
            return s_cachedEndpoint;
        }
        
        s_cachedEndpoint = CString(DEFAULT_ENDPOINT);
        s_endpointCached = true;
        return s_cachedEndpoint;
    }
    
    // -------------------------------------------------------------------------
    // HttpPost: POST via WinHTTP. Supports both HTTPS (cloud APIs) and plain
    // HTTP (local Ollama). Sets statusCode to the HTTP status (0 on error).
    // CC=5  CogC=5  Nesting=2
    // -------------------------------------------------------------------------
    static CString HttpPost(const CString& host, INTERNET_PORT port, bool useHttps,
                            const CString& requestPath, const CString& jsonPayload,
                            bool addBearerAuth, const CString& token, DWORD& statusCode)
    {
        statusCode = 0;

        HINTERNET hSession = WinHttpOpen(L"AutoCAD-AI/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return _T("Error: Could not initialize HTTP session");

        HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            CString err; err.Format(_T("Error: Could not connect to %s"), (LPCTSTR)host);
            return err;
        }

        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", requestPath,
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                flags);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return _T("Error: Could not create request");
        }

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

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                utf8Buf.data(), utf8Len - 1, utf8Len - 1, 0))
        {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return _T("Error: Could not send request");
        }

        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            return _T("Error: Could not receive response");
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
    // CC=4  CogC=5
    // -------------------------------------------------------------------------
    static CString FormatHttpError(DWORD statusCode, const CString& response, bool isGemini)
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
            CString limitMsg = isGemini
                ? _T("Daily limit: 1500 requests per day (Gemini free tier).\n")
                : _T("Daily limit: 50 requests per day.\n");
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
            return _T("Error: Unauthorized (HTTP 401). Check your API token with AISETTOKEN command.");
        if (statusCode == 403)
            return _T("Error: Forbidden (HTTP 403). Your token may not have proper permissions.");
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
    
    // Send prompt to API (single-turn).
    // Dispatches to Ollama / Gemini / DeepSeek / OpenAI format based on endpoint.
    // CC=6  CogC=7  Nesting=2
    CString SendToGitHubCopilot(const CString& prompt)
    {
        CString rawEndpoint = GetAPIEndpoint();
        bool isOllama   = IsOllama(rawEndpoint);
        bool isGemini   = (rawEndpoint.Find(_T("generativelanguage.googleapis.com")) >= 0);
        bool isDeepSeek = (rawEndpoint.Find(_T("api.deepseek.com")) >= 0);

        if (!isOllama)
        {
            CString token = GetAPIToken();
            if (token.IsEmpty())
                return _T("Error: API token not configured. Use AISETTOKEN command first.");
        }

        CString host; INTERNET_PORT port; bool useHttps;
        ParseEndpoint(rawEndpoint, host, port, useHttps);

        CString token     = isOllama ? CString(_T("")) : GetAPIToken();
        CString model     = GetAPIModel();
        CString escaped   = EscapeJsonString(prompt);
        CString requestPath, jsonPayload;

        if (isOllama)
        {
            requestPath = _T("/v1/chat/completions");
            jsonPayload.Format(_T("{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false,\"temperature\":0.7}"),
                               (LPCTSTR)model, (LPCTSTR)escaped);
        }
        else if (isGemini)
        {
            requestPath.Format(_T("/v1/models/gemini-2.5-flash:generateContent?key=%s"), (LPCTSTR)token);
            jsonPayload.Format(_T("{\"contents\":[{\"parts\":[{\"text\":\"%s\"}]}],\"generationConfig\":{\"maxOutputTokens\":8192,\"temperature\":0.7}}"),
                               (LPCTSTR)escaped);
        }
        else if (isDeepSeek)
        {
            requestPath = _T("/v1/chat/completions");
            jsonPayload.Format(_T("{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"model\":\"deepseek-chat\",\"temperature\":0.7,\"max_tokens\":8192}"),
                               (LPCTSTR)escaped);
        }
        else
        {
            requestPath = _T("/chat/completions");
            jsonPayload.Format(_T("{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"model\":\"%s\",\"temperature\":0.7,\"max_tokens\":1000}"),
                               (LPCTSTR)escaped, (LPCTSTR)model);
        }

        DWORD statusCode = 0;
        CString response = HttpPost(host, port, useHttps, requestPath, jsonPayload,
                                    !isGemini && !isOllama, token, statusCode);
        if (statusCode == 0)   return response;
        if (statusCode != 200) return FormatHttpError(statusCode, response, isGemini);
        return ExtractContent(response);
    }
    
    // Send conversation with history to API (multi-turn).
    // Gemini doesn't support native multi-turn: history is flattened to a single prompt.
    // Ollama and DeepSeek use OpenAI-compatible messages array natively.
    // CC=6  CogC=7  Nesting=2
    CString SendToGitHubCopilotWithHistory(const std::vector<ChatMessage>& messages)
    {
        CString rawEndpoint = GetAPIEndpoint();
        bool isOllama   = IsOllama(rawEndpoint);
        bool isGemini   = (rawEndpoint.Find(_T("generativelanguage.googleapis.com")) >= 0);
        bool isDeepSeek = (rawEndpoint.Find(_T("api.deepseek.com")) >= 0);

        if (!isOllama)
        {
            CString token = GetAPIToken();
            if (token.IsEmpty())
                return _T("Error: API token not configured. Use AISETTOKEN command first.");
        }

        // Gemini: flatten history into a labelled single prompt.
        if (isGemini)
        {
            CString combined;
            for (const auto& msg : messages)
            {
                if      (msg.role == _T("user"))      combined += _T("User: ")      + msg.content + _T("\n\n");
                else if (msg.role == _T("assistant")) combined += _T("Assistant: ") + msg.content + _T("\n\n");
            }
            return SendToGitHubCopilot(combined);
        }

        // Build messages JSON array (shared by OpenAI / DeepSeek / Ollama).
        CString messagesJson = _T("[");
        for (size_t i = 0; i < messages.size(); i++)
        {
            if (i > 0) messagesJson += _T(",");
            messagesJson += _T("{\"role\":\"") + messages[i].role
                          + _T("\",\"content\":\"") + EscapeJsonString(messages[i].content)
                          + _T("\"}");
        }
        messagesJson += _T("]");

        CString host; INTERNET_PORT port; bool useHttps;
        ParseEndpoint(rawEndpoint, host, port, useHttps);

        CString token       = isOllama ? CString(_T("")) : GetAPIToken();
        CString model       = isOllama   ? GetAPIModel()
                            : isDeepSeek ? CString(_T("deepseek-chat"))
                                         : GetAPIModel();
        CString requestPath = (isOllama || isDeepSeek) ? _T("/v1/chat/completions")
                                                        : _T("/chat/completions");
        CString streamFlag  = isOllama ? _T(",\"stream\":false") : _T("");
        CString jsonPayload;
        jsonPayload.Format(_T("{\"messages\":%s,\"model\":\"%s\",\"temperature\":0.7,\"max_tokens\":8192%s}"),
                           (LPCTSTR)messagesJson, (LPCTSTR)model, (LPCTSTR)streamFlag);

        DWORD statusCode = 0;
        CString response = HttpPost(host, port, useHttps, requestPath, jsonPayload,
                                    !isOllama, token, statusCode);
        if (statusCode == 0)   return response;
        if (statusCode != 200) return FormatHttpError(statusCode, response, false);
        return ExtractContent(response);
    }
    
    // AISETTOKEN command - Set GitHub API token
    void aiSetTokenCommand()
    {
        acutPrintf(_T("\n=== SET GITHUB COPILOT API TOKEN ===\n"));
        acutPrintf(_T("This token will be stored securely in Windows registry.\n"));
        acutPrintf(_T("Get your token from: https://github.com/settings/tokens\n\n"));
        
        TCHAR tokenBuffer[512];
        int result = acedGetString(0, _T("Enter your GitHub token (or press ESC to cancel): "), tokenBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString token(tokenBuffer);
        token.Trim();
        
        if (token.IsEmpty())
        {
            acutPrintf(_T("\nError: Token cannot be empty.\n"));
            return;
        }
        
        if (SetAPIToken(token))
        {
            acutPrintf(_T("\nToken configured successfully! You can now use AI commands.\n"));
        }
    }
    
    // AISETENDPOINT command - Set API endpoint
    void aiSetEndpointCommand()
    {
        acutPrintf(_T("\n=== SET API ENDPOINT ===\n"));
        acutPrintf(_T("Current endpoint: %s\n\n"), (LPCTSTR)GetAPIEndpoint());
        acutPrintf(_T("Available endpoints:\n"));
        acutPrintf(_T("1. models.inference.ai.azure.com (GitHub Models, 50 requests/day)\n"));
        acutPrintf(_T("2. api.githubcopilot.com (GitHub Copilot Subscription)\n"));
        acutPrintf(_T("3. generativelanguage.googleapis.com (Google Gemini, 1500 requests/day FREE)\n"));
        acutPrintf(_T("4. api.openai.com (OpenAI API)\n"));
        acutPrintf(_T("5. api.deepseek.com (DeepSeek API - Very affordable)\n"));
        acutPrintf(_T("6. localhost:11434 (Ollama - local, no token needed)\n"));
        acutPrintf(_T("7. Custom endpoint\n\n"));

        TCHAR choiceBuffer[10];
        int result = acedGetString(1, _T("Enter choice (1-7) or press ESC to cancel: "), choiceBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString choice(choiceBuffer);
        choice.Trim();
        CString newEndpoint;
        
        if (choice == _T("1"))
        {
            newEndpoint = _T("models.inference.ai.azure.com");
        }
        else if (choice == _T("2"))
        {
            newEndpoint = _T("api.githubcopilot.com");
        }
        else if (choice == _T("3"))
        {
            newEndpoint = _T("generativelanguage.googleapis.com");
            acutPrintf(_T("\nGemini selected. Get your FREE API key from:\n"));
            acutPrintf(_T("https://aistudio.google.com/app/apikey\n"));
        }
        else if (choice == _T("4"))
        {
            newEndpoint = _T("api.openai.com");
        }
        else if (choice == _T("5"))
        {
            newEndpoint = _T("api.deepseek.com");
            acutPrintf(_T("\nDeepSeek selected. Get your API key from:\n"));
            acutPrintf(_T("https://platform.deepseek.com/api_keys\n"));
        }
        else if (choice == _T("6"))
        {
            newEndpoint = _T("localhost:11434");
            acutPrintf(_T("\nOllama selected. No API token required.\n"));
            acutPrintf(_T("Set your model with AISETMODEL (e.g. llama3.2, mistral, codellama).\n"));
            acutPrintf(_T("Make sure Ollama is running: ollama serve\n"));
        }
        else if (choice == _T("7"))
        {
            TCHAR endpointBuffer[256];
            result = acedGetString(1, _T("Enter custom endpoint (without https://): "), endpointBuffer);
            if (result != RTNORM)
            {
                acutPrintf(_T("\nCommand cancelled.\n"));
                return;
            }
            newEndpoint = CString(endpointBuffer);
            newEndpoint.Trim();
        }
        else
        {
            acutPrintf(_T("\nInvalid choice.\n"));
            return;
        }
        
        if (newEndpoint.IsEmpty())
        {
            acutPrintf(_T("\nError: Endpoint cannot be empty.\n"));
            return;
        }
        
        if (SetAPIEndpoint(newEndpoint))
        {
            acutPrintf(_T("Endpoint: %s\n"), (LPCTSTR)newEndpoint);
            acutPrintf(_T("\nNow test the connection with AITEST command.\n"));
        }
    }
    
    // AITEST command - Test API connection
    void aiTestCommand()
    {
        acutPrintf(_T("\n=== TEST API CONNECTION ===\n"));

        CString endpoint = GetAPIEndpoint();
        bool ollama  = IsOllama(endpoint);
        bool isGemini = (endpoint.Find(_T("generativelanguage.googleapis.com")) >= 0);

        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured. Use AISETTOKEN command first.\n"));
            return;
        }

        acutPrintf(_T("Endpoint: %s\n"), (LPCTSTR)endpoint);
        if (ollama) acutPrintf(_T("Model: %s\n"), (LPCTSTR)GetAPIModel());
        acutPrintf(_T("Testing connection with simple prompt...\n"));

        CString response = SendToGitHubCopilot(_T("Say 'Hello from AutoCAD!' in one short sentence."));

        acutPrintf(_T("\n--- API Response ---\n"));
        acutPrintf(_T("%s\n"), (LPCTSTR)response);
        acutPrintf(_T("--- End Response ---\n"));

        if (isGemini)
            acutPrintf(_T("\nTip: Use AILISTMODELS to see all available Gemini/Ollama models.\n"));
        else if (ollama)
            acutPrintf(_T("\nTip: Use AILISTMODELS to see locally installed Ollama models.\n"));
    }
    
    // AILISTMODELS command - List available models (Gemini or Ollama)
    void aiListModelsCommand()
    {
        acutPrintf(_T("\n=== LIST AVAILABLE MODELS ===\n"));

        CString endpoint = GetAPIEndpoint();
        bool isOllama = IsOllama(endpoint);
        bool isGemini = (endpoint.Find(_T("generativelanguage.googleapis.com")) >= 0);

        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured. Use AISETTOKEN command first.\n"));
            return;
        }

        if (!isGemini && !isOllama)
        {
            acutPrintf(_T("This command lists models for Gemini (option 3) or Ollama (option 6).\n"));
            acutPrintf(_T("Current endpoint: %s\n"), (LPCTSTR)endpoint);
            return;
        }

        CString host; INTERNET_PORT port; bool useHttps;
        ParseEndpoint(endpoint, host, port, useHttps);
        CString token = isOllama ? CString(_T("")) : GetAPIToken();

        acutPrintf(_T("Endpoint: %s\n"), (LPCTSTR)endpoint);
        acutPrintf(_T("Fetching available models...\n\n"));

        // Ollama: GET /api/tags  (plain HTTP, no auth)
        if (isOllama)
        {
            HINTERNET hSession = WinHttpOpen(L"AutoCAD-AI/1.0",
                                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME,
                                             WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) { acutPrintf(_T("Error: Could not initialize HTTP session\n")); return; }

            HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
            if (!hConnect)
            { WinHttpCloseHandle(hSession); acutPrintf(_T("Error: Could not connect to Ollama\n")); return; }

            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/tags",
                                                    NULL, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (!hRequest)
            { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
              acutPrintf(_T("Error: Could not create request\n")); return; }

            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(hRequest, NULL))
            { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
              acutPrintf(_T("Error: Could not get models from Ollama. Is 'ollama serve' running?\n")); return; }

            std::string body;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
            {
                std::vector<char> buf(avail + 1); DWORD read = 0;
                if (WinHttpReadData(hRequest, buf.data(), avail, &read))
                { buf[read] = '\0'; body += buf.data(); }
            }
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);

            int wl = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, NULL, 0);
            std::vector<wchar_t> wb(wl);
            MultiByteToWideChar(CP_UTF8, 0, body.c_str(), -1, wb.data(), wl);
            acutPrintf(_T("--- Installed Ollama Models ---\n%s\n--- End List ---\n"), wb.data());
            acutPrintf(_T("Use AISETMODEL to select a model (e.g. llama3.2, mistral).\n"));
            return;
        }

        // Gemini path below — unchanged
        acutPrintf(_T("Endpoint: %s\n"), (LPCTSTR)endpoint);
            
            // Initialize WinHTTP
            HINTERNET hSession = WinHttpOpen(L"AutoCAD-Copilot/1.0",
                                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME,
                                            WINHTTP_NO_PROXY_BYPASS, 0);
            
            if (!hSession)
            {
                acutPrintf(_T("Error: Could not initialize HTTP session\n"));
                return;
            }
            
            HINTERNET hConnect = WinHttpConnect(hSession, endpoint, INTERNET_DEFAULT_HTTPS_PORT, 0);
            
            if (!hConnect)
            {
                WinHttpCloseHandle(hSession);
                acutPrintf(_T("Error: Could not connect to endpoint\n"));
                return;
            }
            
            // List models endpoint
            CString listPath;
            listPath.Format(_T("/v1/models?key=%s"), (LPCTSTR)token);
            
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", listPath,
                                                   NULL, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   WINHTTP_FLAG_SECURE);
            
            if (!hRequest)
            {
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                acutPrintf(_T("Error: Could not create request\n"));
                return;
            }
            
            // Send request
            BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            
            if (!result)
            {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                acutPrintf(_T("Error: Could not send request\n"));
                return;
            }
            
            result = WinHttpReceiveResponse(hRequest, NULL);
            
            if (!result)
            {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                acutPrintf(_T("Error: Could not receive response\n"));
                return;
            }
            
            // Read response
            std::string responseBody;
            DWORD bytesAvailable = 0;
            DWORD bytesRead = 0;
            
            do
            {
                bytesAvailable = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable))
                    break;
                
                if (bytesAvailable == 0)
                    break;
                
                std::vector<char> buffer(bytesAvailable + 1);
                if (!WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
                    break;
                
                buffer[bytesRead] = 0;
                responseBody += buffer.data();
                
            } while (bytesAvailable > 0);
            
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            
            // Convert response to CString
            int wideLength = MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, NULL, 0);
            std::vector<wchar_t> wideBuffer(wideLength);
            MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, wideBuffer.data(), wideLength);
            CString modelsResponse(wideBuffer.data());
            
            acutPrintf(_T("--- Available Models ---\n"));
            acutPrintf(_T("%s\n"), (LPCTSTR)modelsResponse);
            acutPrintf(_T("--- End List ---\n"));
    }
    
    // AIASK command - Ask Copilot a question
    void aiAskCommand()
    {
        acutPrintf(_T("\n=== ASK GITHUB COPILOT ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use AISETTOKEN command to set your GitHub token first.\n"));
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
        kb = _T("Available CUSTOM HelloWorld Plugin commands:\n");
        kb += _T("- HELLO: Creates a red circle with cross at specified point\n");
        kb += _T("- DRAWBOX: Creates a 3D wireframe box\n");
        kb += _T("- BOOLPOLY: Boolean operations on polylines (union/subtract/intersect)\n");
        kb += _T("- UNIONPOLY: Union of two polylines\n");
        kb += _T("- SUBPOLY: Subtract second polyline from first\n");
        kb += _T("- INPOLY: Intersection of two polylines\n");
        kb += _T("- ALX/ALY/ALZ: Align objects by X/Y/Z coordinate\n");
        kb += _T("- MX/MY/MZ: Move objects in X/Y/Z direction only (restricted movement)\n");
        kb += _T("- CX/CY/CZ: Copy objects in X/Y/Z direction only (restricted copy)\n");
        kb += _T("- DISTLINE: Distribute objects evenly along a line between two points\n");
        kb += _T("- DISTBETWEEN: Distribute objects between two points (excludes endpoints)\n");
        kb += _T("- DISTEQUAL: Distribute with equal spacing (half-space at ends)\n");
        kb += _T("- SEQNUM: Add sequential numbers to selected objects\n");
        kb += _T("- INSERTAREA: Insert auto-updating area text in closed polyline\n");
        kb += _T("- SUMLENGTH: Insert auto-updating sum of lengths for multiple curves\n");
        kb += _T("- COPYTEXT: Copy text content from one text to others\n");
        kb += _T("- COPYSTYLE: Copy text style properties\n");
        kb += _T("- COPYDIMSTYLE: Copy dimension style\n\n");
        kb += _T("IMPORTANT: When user asks for:\n");
        kb += _T("- 'number objects' or 'sequential numbers' → use SEQNUM\n");
        kb += _T("- 'distribute evenly' or 'space objects' → use DISTLINE or DISTEQUAL\n");
        kb += _T("- 'align objects' → use ALX/ALY/ALZ\n");
        kb += _T("- 'move only in X/Y/Z' → use MX/MY/MZ\n");
        kb += _T("- 'copy only in X/Y/Z' → use CX/CY/CZ\n");
        kb += _T("- 'show area' or 'area label' → use INSERTAREA\n");
        kb += _T("- 'sum of lengths' → use SUMLENGTH\n");
        kb += _T("- 'combine polylines' or 'merge polylines' → use UNIONPOLY\n");
        return kb;
    }
    
    // AIDRAW command - Natural language to AutoCAD drawing
    void aiDrawCommand()
    {
        acutPrintf(_T("\n=== AI NATURAL LANGUAGE DRAWING ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use AISETTOKEN command to set your GitHub token first.\n"));
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
            _T("You are an AutoCAD command generator with knowledge of custom HelloWorld plugin commands. ")
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
    
    // AIHELP command - Show AI knowledge base
    void aiHelpCommand()
    {
        acutPrintf(_T("\n=== AI KNOWLEDGE BASE ===\n"));
        acutPrintf(_T("The AI knows about these custom commands:\n\n"));
        
        CString kb = GetCustomCommandsKnowledgeBase();
        acutPrintf(_T("%s\n"), (LPCTSTR)kb);
        
        acutPrintf(_T("\n=== HOW TO USE ===\n"));
        acutPrintf(_T("Type AIDRAW and describe what you want in natural language.\n"));
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
    
    // AILISP command - Natural language to LISP code
    void aiLispCommand()
    {
        acutPrintf(_T("\n=== AI LISP CODE GENERATOR ===\n"));
        
        if (!IsTokenConfigured())
        {
            acutPrintf(_T("Error: API token not configured.\n"));
            acutPrintf(_T("Use AISETTOKEN command to set your GitHub token first.\n"));
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
            
            acutPrintf(_T("(Conversation history: %d interactions. Use AICLEAR to reset)\n"), history.size() / 2);
        }
        else
        {
            acutPrintf(_T("\n⚠ Could not save LISP file. Copy this code manually:\n\n"));
            acutPrintf(_T("%s\n\n"), (LPCTSTR)cleanCode);
        }
    }
    
    // AIFIX command - Report error and ask AI to fix the last generated code
    void aiFixCommand()
    {
        acutPrintf(_T("\n=== AI ERROR REPORTER & FIXER ===\n"));
        
        std::vector<ChatMessage>& history = GetConversationHistory();
        
        if (history.size() == 0)
        {
            acutPrintf(_T("Error: No conversation history. Use AILISP first.\n"));
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
    
    // AICLEAR command - Clear conversation history
    void aiClearHistoryCommand()
    {
        ClearConversationHistory();
        acutPrintf(_T("\n=== CONVERSATION HISTORY CLEARED ===\n"));
        acutPrintf(_T("AI will start fresh with no memory of previous interactions.\n"));
    }

    // AISETMODEL command - Set the active AI model name
    void aiSetModelCommand()
    {
        acutPrintf(_T("\n=== SET AI MODEL ===\n"));
        acutPrintf(_T("Current model: %s\n\n"), (LPCTSTR)GetAPIModel());

        CString endpoint = GetAPIEndpoint();
        if (IsOllama(endpoint))
        {
            acutPrintf(_T("Ollama endpoint active (%s).\n"), (LPCTSTR)endpoint);
            acutPrintf(_T("Common models: llama3.2, mistral, codellama, phi3, gemma3\n"));
            acutPrintf(_T("Run 'ollama list' in a terminal to see installed models.\n\n"));
        }
        else
        {
            acutPrintf(_T("Cloud endpoint active (%s).\n"), (LPCTSTR)endpoint);
            acutPrintf(_T("Default model for OpenAI-compatible APIs: gpt-4o\n\n"));
        }

        TCHAR modelBuffer[128];
        int result = acedGetString(1, _T("Enter model name (or press ESC to cancel): "), modelBuffer);
        if (result != RTNORM)
        { acutPrintf(_T("\nCommand cancelled.\n")); return; }

        CString model(modelBuffer);
        model.Trim();
        if (model.IsEmpty())
        { acutPrintf(_T("\nError: Model name cannot be empty.\n")); return; }

        SetAPIModel(model);
        acutPrintf(_T("Model set to '%s'. Use AITEST to verify.\n"), (LPCTSTR)model);
    }
}
