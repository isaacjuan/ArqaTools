#pragma once
#include "StdAfx.h"
#include <string>
#include <vector>

namespace AITools
{
    // Message structure for conversation history
    struct ChatMessage
    {
        CString role;    // "user" or "assistant"
        CString content;
    };
    
    // Commands
    void aiAskCommand();
    void aiSetTokenCommand();
    void aiSetEndpointCommand();
    void aiSetModelCommand();
    void aiTestCommand();
    void aiListModelsCommand();
    void aiDrawCommand();
    void aiHelpCommand();
    void aiLispCommand();
    void aiClearHistoryCommand();
    void aiFixCommand();
    void aiConfigCommand();   // ATAICONFIG - open ai_config.lua

    // Helper functions. Provider, model and limits come from ai_config.lua
    // (AiConfig), the key from the registry per provider. The names still
    // say "GitHubCopilot" for history; they talk to whichever provider is active.
    CString SendToGitHubCopilot(const CString& prompt);
    CString SendToGitHubCopilotWithHistory(const std::vector<ChatMessage>& messages);
    // Single-turn request with a PNG attached to the user message (vision).
    // Returns "Error: ..." on failure; a provider without vision = true in
    // ai_config.lua fails with "Error: images not supported", and the caller
    // is expected to retry with SendToGitHubCopilot and text only.
    CString SendWithImage(const CString& prompt, const CString& pngPath);
    bool IsTokenConfigured();
    CString ExtractCommands(const CString& aiResponse);
    CString GetCustomCommandsKnowledgeBase();
    bool ExecuteLispCode(const CString& lispCode);
    void ClearConversationHistory();
    std::vector<ChatMessage>& GetConversationHistory();      // ATAILISP / ATAIFIX
    std::vector<ChatMessage>& GetLuaConversationHistory();   // ATAILUA
    // ClearConversationHistory (ATAICLEAR) clears both.
}
