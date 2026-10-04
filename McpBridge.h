// McpBridge.h - named-pipe bridge for the ArqaToolsMcp server (MCP)
//
// An MCP client (Claude Code, ...) talks to ArqaToolsMcp.exe over stdio; that
// process talks to this plugin over the pipe \\.\pipe\ArqaTools.<pid>
// (current user only, no remote clients). See MCP_DESIGN.md.
//
// Wire format, both directions: 4-byte little-endian length + UTF-8 payload.
//   request:  "<method>\n<body>"         (body is raw text: a name, Lua code, ...)
//   response: one JSON object, always with "ok" (true/false) and "error" on failure
//
// ObjectARX is never touched from the pipe thread: a request is handed to a
// message-only window on the main thread. Queries are answered there; Lua runs
// go through the internal ATMCPRUN command, so the drawing is locked and a run
// is one UNDO step, the same as ATLUA.

#pragma once
#include "StdAfx.h"

namespace McpBridge
{
    // On_kUnloadAppMsg: stops the pipe thread and fails any pending request.
    void Uninit();

    // ATMCPSTART  - start listening on the pipe (opt-in)
    void startCommand();
    // ATMCPSTOP   - stop listening
    void stopCommand();
    // ATMCPSTATUS - pipe name, client connected, requests served
    void statusCommand();
    // ATMCPRUN    - internal: runs the pending Lua request (sent via sendStringToExecute)
    void runCommand();
}
