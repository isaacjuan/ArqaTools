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

    // Helper functions
    bool SetAPIToken(const CString& token);
    CString GetAPIToken();
    bool SetAPIEndpoint(const CString& endpoint);
    CString GetAPIEndpoint();
    bool SetAPIModel(const CString& model);
    CString GetAPIModel();
    CString SendToGitHubCopilot(const CString& prompt);
    CString SendToGitHubCopilotWithHistory(const std::vector<ChatMessage>& messages);
    bool IsTokenConfigured();
    CString ExtractCommands(const CString& aiResponse);
    CString GetCustomCommandsKnowledgeBase();
    bool ExecuteLispCode(const CString& lispCode);
    void ClearConversationHistory();
    std::vector<ChatMessage>& GetConversationHistory();
}
