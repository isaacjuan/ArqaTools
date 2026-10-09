# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**`AGENTS.md` is the authoritative guide** for architecture, module pattern, ObjectARX/MFC quirks,
and adding commands. Read it first. This file adds what is specific to this fork (branch
`feature/mcp`, base `feature/consolidation`) and the AI/MCP work. `README.md` and `ARCHITECTURE.md`
are stale (pre-rename `HelloWorld` naming).

## Build

```powershell
# Plugin (ObjectARX 2026 SDK, config names still say "2025")
msbuild ArqaTools.sln /p:Configuration="Debug 2025" /p:Platform=x64 /v:m

# MCP server (C# .NET 8)
dotnet build mcp\ArqaToolsMcp -c Release
```

**Do not run `Build.bat`** in this fork: it deploys over the original plugin. Load the fork
directly in AutoCAD 2026:

```lisp
(arxload "D:/dev_jp/Sync/ArqaToolsMcp/x64/Debug 2025/ArqaTools.arx")
```

Then `ATMCPSTART` to open the MCP pipe.

- The ARX is file-locked while loaded; unload before rebuilding.
- New `.cpp`/`.h` files need entries in `ArqaTools.vcxproj` (`ClCompile` + `ClInclude`).

## LuaCommands: two folders

| Folder | Role |
|--------|------|
| `LuaCommands\` (this repo) | **Source of truth**, versioned |
| `Documents\ArqaTools\LuaCommands\` | What the plugin actually loads |

After editing a file in the repo, copy it to Documents. Then run `ATLUARELOAD` in AutoCAD (or
`run_acad_command "_ATLUARELOAD"` via MCP) to pick up changes. No C++ rebuild needed for Lua
command changes.

## MCP workflow (Claude Code)

`.mcp.json` registers the `arqatools` server. Before using it:

1. Build the MCP server: `dotnet build mcp\ArqaToolsMcp -c Release`
2. In AutoCAD: load the ARX and run `ATMCPSTART`
3. Reconnect the MCP client if needed (`/mcp` in Claude Code)

Key tools:
- `ping` - verify connection, get AutoCAD version and drawing name
- `get_api` - the `at.*` function catalog (also a resource)
- `list_commands` / `get_command_source` - Lua command inventory
- `run_lua` - execute read-only Lua (query functions only)
- `run_command` - execute a Lua command with positional `answers`
- `run_acad_command` / `list_acad_commands` - drive any AutoCAD/ACA command
- `test_command` - run a command in a scratch drawing, get geometry report and PNG

Commands with declared parameters are published as their own MCP tools with typed schemas.

## AI / MCP architecture

```
Claude Code --stdio/MCP--> mcp\ArqaToolsMcp (C# .NET 8)
   --named pipe \\.\pipe\ArqaTools.<pid>, length-prefixed frames-->
McpBridge (pipe thread -> message-only window on main thread)
   --> LuaTools::LuaEngine / LuaCommands / CommandTester
```

- The plugin only writes JSON, never parses it. Structured args become Lua table literals on
  the C# side (`LuaLiteral.cs`).
- The pipe thread never calls ObjectARX; Lua runs go through `sendStringToExecute("_ATMCPRUN ")`
  only when the document is quiescent.
- `LuaTools.cpp`'s `kFns` table is the single source of truth for the `at` API: it registers
  bindings, generates `describeApi()`, and flags read-only functions (`isReadOnlyFunction`).
- `AiHarness` wraps AI-written Lua (`ATAICMD`, `ATAILUA`): model call, cleanup, validation,
  test run, AI review with image, correction rounds, human approval. New AI features supply an
  `AiHarness::Task` rather than their own loop.

Design docs: `MCP_DESIGN.md`, `HARNESS.md`, `HARNESS_RESPONSIBILITIES.md`, `AI_SETUP.md`,
`LUA_COMMANDS.md`.

## Testing

No unit tests or CI. Verification happens in a running AutoCAD:

- **Lua scripts**: `ATLUA @path\to\file.lua` (repo-root `test*.lua` files)
- **Lua commands**: `test_command` (MCP) or `CommandTester::Run` (C++) runs in a scratch drawing
  with auto-answered prompts, returns geometry report and plan-view PNG
- **MCP tools**: use `ping`, `run_lua`, `run_command`, `test_command` from Claude Code

After editing a `LuaCommands\` file: copy to Documents and `ATLUARELOAD`.

## Design criteria (space planning work)

- **Every space dimension is a multiple of 300 mm** (house rule, checked by `ATMODULECHECK`).
  Legal minimum sides round up to the next multiple.
- **Important changes of space go through transition spaces** (people need time to perceive a
  new environment): useful spaces connect only through circulation (`Hall` / `Corridor` /
  `Portal`) or from outside, never through another useful space; the entrance opens into a
  hall or portal. Exception: an en-suite bathroom reached only from its own bedroom.
  Checked by `ATPASSAGECHECK`; see `QUITO_SPACE_STANDARDS.md` §8.1.
- **Doors are never obstructed:** a zone as wide as the opening and 900 mm deep stays free on
  both sides (`ATDOORCLEARCHECK`). When placing furniture, keep it out of door zones.
- `ATDWELLINGCHECK <id>` runs every check on one dwelling.
- Regional rules: `QUITO_SPACE_STANDARDS.md` (binding code), `BEDROOM_DESIGN_CRITERIA.md`,
  `BATHROOM_DESIGN_CRITERIA.md` (Belgian, mostly secondary-sourced). Tag rooms with
  `ATROOMTYPE` before running checks.

## Other notes

- ACML (`Acml*.cpp`, `*.acml`, `ATACML*`) is a separate DSL, decoupled from Lua.
- Workspace rule: run git commands from inside this project directory (the parent `D:\dev_jp`
  has no `.git`).
