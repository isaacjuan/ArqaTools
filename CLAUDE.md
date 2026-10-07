# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**`AGENTS.md` is the authoritative guide** (build, architecture, module pattern, ObjectARX/MFC
quirks, adding a command). Read it first. This file only adds what is specific to this fork and
the AI/MCP work. `README.md` and `ARCHITECTURE.md` are stale (pre-rename `HelloWorld` naming).

## This fork

`D:\dev_jp\Sync\ArqaToolsMcp`, branch `feature/mcp` (base: `feature/consolidation`). It adds the
MCP bridge and the AI harness on top of ArqaTools.

## Build

```
msbuild ArqaTools.sln /p:Configuration="Debug 2025" /p:Platform=x64 /v:m   # plugin
dotnet build mcp\ArqaToolsMcp -c Release                                  # MCP server
```

- **Do not run `Build.bat` in this fork**: it copies the ARX over `Documents\ArqaTools.arx`,
  which is the original (non-fork) plugin. Load the fork's build directly in AutoCAD 2026:
  `(arxload "D:/dev_jp/Sync/ArqaToolsMcp/x64/Debug 2025/ArqaTools.arx")`, then `ATMCPSTART`.
- Config names still say `2025`, but the target is the `OARX2026` SDK / AutoCAD 2026.
- The ARX is locked while loaded in AutoCAD; unload it before rebuilding.
- New `.cpp`/`.h` files must be added to `ArqaTools.vcxproj` (both `ClCompile` and `ClInclude`).

## Testing

No unit tests, no CI. Verification happens inside a running AutoCAD:

- Lua scripts: `ATLUA @path\to\file.lua` (`test.lua`, `test_foundations.lua`, `test_tier1.lua`,
  `test_tier2.lua`).
- Lua commands: `CommandTester` (`Run` for a command, `RunScript` for a script) executes in a
  scratch drawing with auto-answered prompts and produces a geometry report and plan-view PNG.
- From Claude Code, via the `arqatools` MCP server (`.mcp.json`, requires `ATMCPSTART` in
  AutoCAD): `ping`, `get_api`, `list_commands`, `get_command_source`, `run_lua` (always
  read-only), `run_command` (positional `answers`), `test_command`.
- Lua command files live in `Documents\ArqaTools\LuaCommands`; after editing one, the user must
  run `ATLUARELOAD` in AutoCAD (it cannot be triggered over MCP).

## AI / MCP architecture (cross-file)

```
Claude Code --stdio/MCP--> mcp\ArqaToolsMcp (C# .NET 8)
   --named pipe \\.\pipe\ArqaTools.<pid>, length-prefixed frames-->
McpBridge (pipe thread -> message-only window on main thread)
   --> LuaTools::LuaEngine / LuaCommands / CommandTester
```

- Requests are plain text (`method\nbody`); the plugin only writes JSON, never parses it.
  Structured args are converted to Lua table literals on the C# side (`LuaLiteral.cs`).
- The pipe thread never calls ObjectARX. Lua runs are dispatched via
  `sendStringToExecute("_ATMCPRUN ")` only when the document is quiescent.
- `LuaTools.cpp`'s `kFns` table is the single source of truth for the `at` API: it registers
  bindings, generates `describeApi()` (the AI prompt and MCP `get_api`), and flags read-only
  functions (`isReadOnlyFunction`).
- `AiHarness` is the shared loop for AI-written Lua (`ATAICMD`, `ATAILUA`): model call, cleanup,
  validation, `CommandTester` run, AI review with image, correction rounds, human approval. New
  AI features supply an `AiHarness::Task` rather than their own loop. Providers/models/harness
  settings come from `Documents\ArqaTools\ai_config.lua` (`AiConfig`).

Design and rationale: `MCP_DESIGN.md` (phases, pipe protocol, read-only mode),
`HARNESS.md` and `HARNESS_RESPONSIBILITIES.md` (harness roles and guarantees), `AI_SETUP.md`,
`LUA_COMMANDS.md` (commands moved from C++ to Lua, and how to move more).

## Other notes

- ACML (`Acml*.cpp`, `*.acml`, commands `ATACML*`) is a separate DSL interpreter, fully
  decoupled from Lua.
- Workspace rule (from `D:\dev_jp\CLAUDE.md`): run git commands from inside this project
  directory.
