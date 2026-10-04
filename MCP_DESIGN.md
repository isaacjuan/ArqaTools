# ArqaTools MCP server (design sketch)

Status: **phase 1 implemented** in this fork (`Sync\ArqaToolsMcp`, branch `feature/mcp`):
`McpBridge.h/.cpp` (plugin side), `mcp\ArqaToolsMcp` (C# MCP server), read-only mode in
`LuaTools`. Phases 2 to 4 below are still design.

### Using it

1. Build the plugin: `msbuild ArqaTools.sln /p:Configuration="Debug 2025" /p:Platform=x64`
   (not `Build.bat`: it deploys over `Documents\ArqaTools.arx`, the original plugin).
2. Build the server: `dotnet build mcp\ArqaToolsMcp -c Release`.
3. In AutoCAD 2026: unload the original ArqaTools if loaded, then
   `(arxload "D:/dev_jp/Sync/ArqaToolsMcp/x64/Debug 2025/ArqaTools.arx")` and `ATMCPSTART`.
4. Claude Code started in this folder picks up `.mcp.json` (server `arqatools`). Elsewhere:
   `claude mcp add arqatools -- D:\dev_jp\Sync\ArqaToolsMcp\mcp\ArqaToolsMcp\bin\Release\net8.0\ArqaToolsMcp.exe`.

Phase 1 tools: `list_instances`, `select_instance`, `ping`, `get_api`, `list_commands`,
`get_command_source`, `run_lua` (always read-only). From phase 3: `run_command` (see
"Interactive input"; answers are positional, not keyed by prompt text).

Goal: let any MCP client (Claude Code, Claude Desktop, Copilot, Autodesk Assistant if it
ever accepts external servers) use ArqaTools' Lua engine as a *safe execution layer*:
read the drawing, run sandboxed Lua, and propose new Lua commands that the user approves
inside AutoCAD. ArqaTools stops competing with chat assistants and becomes the thing they
call.

## Architecture

Two parts: an MCP server process and a bridge inside the host:

```
MCP client (Claude Code, ...)
   | stdio, MCP
ArqaToolsMcp.exe            C# .NET 8, ModelContextProtocol SDK
   | named pipe "ArqaTools.<pid>", newline-delimited JSON
ArqaTools.arx               new module McpBridge.h/.cpp
   | pipe thread -> queue -> main thread (command context)
LuaTools::LuaEngine / LuaCommands
```

Why a separate process instead of HTTP inside the ARX: keeps the ARX free of an HTTP/JSON-RPC
stack, the MCP SDK stays in .NET where it is maintained, and a crash in the server never takes
AutoCAD down. The C# server needs a pipe client with reconnect and
back-off, debug logging and `FakePipeServer` tests.

### Pipe protocol (as implemented)

Frames in both directions: 4-byte little-endian length + UTF-8 payload.

```
request:  "run_lua\nfor _,h in ipairs(at.entities('LINE')) do print(h) end"
response: {"ok":true,"output":"2A3\n2A4\n","cancelled":false}
          {"ok":false,"error":"at.drawLine is not available in read-only mode", ...}
```

Requests are plain text (method line + raw body) so the plugin needs no JSON parser; it only
writes JSON. Structured arguments in later phases (e.g. `run_command` args) can be converted
by the C# side into a Lua table literal and parsed by Lua itself.

Pipe: `\\.\pipe\ArqaTools.<pid>`, DACL current user + SYSTEM, `PIPE_REJECT_REMOTE_CLIENTS`,
one instance (one client at a time), one request in flight.

### Threading (the important part)

ObjectARX must not be touched from the pipe thread. Flow:

1. Pipe thread reads a request, pushes it to a locked queue, waits on an event.
2. It wakes the main thread: `PostMessage` to a hidden window created in `On_kInitAppMsg`.
3. The window proc calls `acDocManager->sendStringToExecute(curDoc, L"_ATMCPRUN ", ...)`.
4. `ATMCPRUN` (registered with `ACRX_CMD_TRANSPARENT` off, no prompts) pops the request and
   runs it. Being a real command means: document locked, one UNDO step per request (same as
   `ATLUA` today), ESC via `acedUsrBrk()` still works.
5. Result goes back through the queue, the pipe thread writes the response.

If no document is open, answer `{"ok": false, "error": "no active drawing"}` without
queuing. Timeout on the C# side (e.g. 60 s) so a stuck run never hangs the client.

## MCP tools

| Tool | Params | Does | Changes drawing |
|---|---|---|---|
| `ping` | | `"pong"` + AutoCAD version, drawing name | no |
| `get_api` | | `LuaTools::describeApi()` (also exposed as MCP resource `arqatools://api`) | no |
| `list_commands` | | name, description, file, last runtime error, load error (`g_failedFiles`) | no |
| `get_command_source` | `name` | current `.lua` source | no |
| `get_command_history` | `name` | list of `history\NAME_<ts>.lua` | no |
| `run_lua` | `code`, `readOnly` (default true) | runs a snippet, returns `{output, handles, error}` | only if `readOnly=false` |
| `propose_command` | `name`, `code`, `description` | validates, stores as pending, asks the user | not until approved |
| `command_status` | `name` | `pending` / `approved` / `rejected` (+ reason) / `load_error` | no |
| `run_command` | `name`, `args` (object) | runs an installed Lua command non-interactively | yes |
| `rollback_command` | `name`, `version` | proposes an older history file (goes through approval) | not until approved |

Deliberately missing: delete command, write files, run AutoLISP, change AI settings.
Those stay human-only (`ATLUACMDDEL`, etc.).

### Read-only mode

Implemented as `LuaRunOptions::readOnly`, checked in the `at` proxy's `__index` (same place as
loading mode). Allowed: `print`, `listEntities`, `entities`, `getProps`, `getText`, `sumText`,
`countBlocks`, `layers`, `getCurrentLayer`, `refPoint`, `formatArea`, `formatLength`
(`LuaTools::isReadOnlyFunction`). A new query function must be added to that list too.

### Interactive input

Agents cannot click. In MCP runs, `at.getPoint/getEntity/getSelection/...` must not block:

- `run_lua`: input functions raise `"interactive input not available over MCP"`.
- `run_command` (implemented): the agent passes `answers`, a JSON array in the order the command
  asks. The C# server turns it into a Lua table constructor (`{n=4,[1]={0,0,0},[2]=3,...}`),
  `LuaRunOptions::answers` evaluates it with an empty environment, and each `at.get*` binding
  takes the next entry instead of prompting (`NextAnswer` in `LuaTools.cpp`). `null` = Enter
  (the prompt's default). Asking for more inputs than given raises an error listing every
  prompt asked so far, so the agent can retry with the right answers. Keyed-by-prompt args
  were dropped: prompt texts are AI-written and unstable, the order is not.

## Approval flow (keeps the human in charge)

`propose_command` must never install code by itself.

1. Validate exactly like `ATAICMD` does today (throwaway engine, must define exactly `name`,
   every `at.X` checked with `hasApiFunction`). On failure return the error so the agent can fix
   and resubmit (the agent does the correction rounds, not ArqaTools).
2. Write to `LuaCommands\pending\NAME.lua` + `NAME.json` (description, client name, time).
3. Notify the user: command-line message + balloon/tray notification "Claude proposes command
   NAME, type ATLUAREVIEW".
4. `ATLUAREVIEW` shows the code (and a diff against the current version, if any) with
   Approve / Reject / Reject with reason. Approve reuses the existing install path (copy old
   version to `history\`, write file, `LoadAll`).
5. The agent polls `command_status`; a reject reason is returned to it.

Optional per-user setting `ATMCPTRUST` (off by default): auto-approve proposals that only use
read-only functions. Never auto-approve anything that modifies the drawing.

## Safety summary

- Sandbox unchanged: no io/os/package/debug, text-only `load`, read-only `at` proxy.
- Instruction cap for MCP runs (`maxInstructions`, e.g. 50M like `Dispatch`).
- Built-in command names still refused (`lookupCmd`/`acedGetCName` check).
- Every drawing change is a single UNDO step.
- Pipe restricted to the current user. No network listener.
- Optional log of every MCP request to `Documents\ArqaTools\mcp.log` (who, what, result).

## Multiple AutoCAD instances

Pipe name includes the process id (`ArqaTools.<pid>`). The C# server enumerates
`\\.\pipe\ArqaTools.*`; if more than one exists, tools take an optional `instance` param and a
`list_instances` tool returns pid + drawing names.

## Phases

1. **Read-only bridge**: `McpBridge` (pipe, queue, `ATMCPRUN`), C# MCP server,
   tools `ping`, `get_api`, `list_commands`, `get_command_source`,
   `run_lua(readOnly)`. Proves threading + transport. Low risk.
2. **Proposals**: pending folder, `propose_command`, `command_status`, `ATLUAREVIEW`.
   This is the feature nobody else has.
3. **Execution**: `run_lua(readOnly=false)`, `run_command` with args, `rollback_command`.
4. **Polish**: multi-instance, request log, `ATMCPTRUST`, IntelliCAD check
   (`sendStringToExecute` and hidden-window approach should port; verify on IcArx).

## Open questions

- Ship the C# server inside the ArqaTools installer, or as a separate download?
- Should approved commands record which agent/model wrote them (in the file header)? Useful
  for support and for measuring "commands still used after a week".
- Bridge on by default, or opt-in via `ATMCPSTART`? Recommend opt-in for the first release.
