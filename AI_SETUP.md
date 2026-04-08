# GitHub Copilot AI Integration Setup

## Quick Start

### 1. Get Your GitHub Token
1. Go to: https://github.com/settings/tokens
2. Click **"Generate new token"** → **"Generate new token (classic)"**
3. Give it a name: `AutoCAD Copilot Integration`
4. Select these scopes:
   - ✅ `copilot` (if available)
   - ✅ `read:user`
   - ✅ `user:email`
5. Click **"Generate token"**
6. **COPY THE TOKEN** immediately (you won't see it again!)

### 2. Configure in AutoCAD
1. Load the plugin in AutoCAD (use `RELOADHW` command)
2. Type: **`AISETTOKEN`**
3. Paste your GitHub token when prompted
4. Token is securely stored in Windows Registry

### 3. Test the Connection
Type: **`AITEST`**

This will send a test message to GitHub Copilot and display the response.

## Available Commands

### `AISETTOKEN`
Set or update your GitHub API token. The token is stored securely in Windows registry at:
```
HKEY_CURRENT_USER\Software\HelloWorldPlugin\GitHubToken
```

### `AIASK`
Ask GitHub Copilot any question!

**Examples:**
- "How do I create a polyline in AutoCAD?"
- "Explain the difference between DISTLINE and DISTBETWEEN"
- "What's the formula for calculating circle area?"
- "Write a LISP function to select all red circles"

**Usage:**
1. Type: `AIASK`
2. Enter your question
3. Get response in command line

### `AITEST`
Quick test to verify your API connection is working.

## Features

✅ **Secure Token Storage** - Token stored in Windows Registry (user-specific)
✅ **No External Dependencies** - Uses built-in WinHTTP library
✅ **Simple JSON Parsing** - Lightweight implementation
✅ **Command Line Interface** - Responses displayed directly in AutoCAD
✅ **Error Handling** - Clear error messages for troubleshooting

## API Details

- **Service**: GitHub Copilot API
- **Endpoint**: `https://api.githubcopilot.com/chat/completions`
- **Model**: GPT-4o
- **Authentication**: Bearer Token

## Troubleshooting

### "Error: API token not configured"
Run `AISETTOKEN` command first to set your token.

### "Error: HTTP 401"
Your token is invalid or expired. Generate a new token and run `AISETTOKEN` again.

### "Error: HTTP 403"
You don't have access to GitHub Copilot. Verify your subscription at https://github.com/settings/copilot

### "Error: Could not connect to GitHub API"
Check your internet connection and firewall settings.

### "Error: Could not parse response"
The API response format may have changed. Contact support.

## Security Notes

- Token is stored in **HKEY_CURRENT_USER** (user-specific, not system-wide)
- Token is stored as plain text in registry (Windows security applies)
- Never share your token with others
- You can revoke tokens at any time: https://github.com/settings/tokens

## Example Workflow

```
Command: AIASK
Enter your question: How do I distribute objects evenly in AutoCAD?

Sending to Copilot...

========================================
COPILOT RESPONSE:
========================================
To distribute objects evenly in AutoCAD, you can use the 
DISTLINE command to place objects at equal distances along 
a line between two points. Select the objects you want to 
distribute, specify the start and end points, and the objects 
will be positioned with equal spacing between them.
========================================
```

## Future Enhancements (Possible)

- 🔮 Context-aware queries (analyze selected objects)
- 🔮 Generate AutoCAD scripts from natural language
- 🔮 Multi-turn conversations (chat history)
- 🔮 Save/load conversation history
- 🔮 Create text entities with AI responses
- 🔮 Voice input integration

## Support

If you encounter issues:
1. Verify your GitHub Copilot subscription is active
2. Check token permissions at https://github.com/settings/tokens
3. Test with `AITEST` command
4. Review AutoCAD command line for detailed error messages
