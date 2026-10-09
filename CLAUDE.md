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

**Drawing ACA objects with `run_acad_command`** (learned building TEST-06):

- Set `OSMODE` to 0 first: object snaps pull `_MOVE` base points and positions go wrong.
- Walls: `_WALLADD` `["WI", 150, "JU", "C", p1, p2, ..., ""]` (centre-justified).
- Doors / windows: pick the wall first, then the options, then the point:
  `_DOORADD [{"handle": wall}, "WI", 900, "HE", 2100, "JU", "C", point, ""]`. The point is
  not the centre (a door ends about 50 mm before it, a window at it, along the wall's
  direction): place it, read its extents with `at.getProps`, then `_MOVE` it into place.
- A point near a wall junction can anchor the door to the other wall: keep it away from
  corners and check the extents (the leaf sweeps a box about 1000 deep on the swing side).
- The swing side follows the side of the wall the point is on, not always reliably:
  `AECOPENINGFLIPSWING [{"handle": door}, ""]` flips it. Rotating or mirroring an anchored
  door detaches it from the wall.
- Properties without a command-line option (door height, window size of an existing
  object): AutoCAD COM from PowerShell, `ActiveDocument.HandleToObject("4730").Height = 2100`.
- `_SAVEAS ["", path]` / `_QSAVE` save without dialogs. The test drawing is
  `TEST_FIXTURES.dwg` (repo root, git-ignored); `TEST-06` in it is the reference house.

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

`DESIGN_PRINCIPLES.md` is the project's design method; follow its order when designing or
changing a dwelling:

1. **Zones on the site first:** from the road and access, public zone at the entrance side,
   private zone away from the road, service zone supplied from the road on its own access
   (mark road and access with `ATSITEMARK`, check with `ATSITECHECK`).
2. **Define each space** with the boundary it needs, from a line or floor change, a level
   change, a curtain or glass wall, to a heavy wall. A space does not need four walls.
3. **Connect through transition spaces** (`Hall` / `Corridor` / `Portal`): never reach a
   useful space through another one; the entrance opens into a hall or portal; an en-suite
   bathroom from its own bedroom is the exception. **A bedroom never opens off the central
   hall:** private rooms are entered from the private zone's own hall (tag `zone=Private`) or
   their en-suite. The shared bathroom is public, discreet, entered from a hall only. The
   entrance opens into the public zone. **Doors are never obstructed** (900 mm zone both sides)
and **open inward**, into the room they serve (outward only when there is no space for the
leaf, `ATDOORSWINGCHECK`).
   **Space economy:** as little circulation as possible; enter rectangular rooms through a
   long side, near its middle (`ATECONOMYCHECK`). **Furniture** follows the same logic: each
   piece has access sides (bed three, wardrobe its doors, desk its chair side) whose free floor
   must be reachable from the room's entrance (`ATFURNITURECHECK`, `DESIGN_PRINCIPLES.md` §6).
   Only a student bedroom (`student=Yes`) has a desk, with a single bed; other bedrooms no desk.
4. **Size on the 300 mm module**, code minimums rounded up (`QUITO_SPACE_STANDARDS.md`).

Tag rooms with `ATROOMTYPE` (and the entrance with `ATDOORTYPE`), then `ATDWELLINGCHECK <id>`
runs every check. Regional code notes: `QUITO_SPACE_STANDARDS.md` (binding),
`BEDROOM_DESIGN_CRITERIA.md`, `BATHROOM_DESIGN_CRITERIA.md` (Belgian, mostly secondary-sourced).

## Other notes

- ACML (`Acml*.cpp`, `*.acml`, `ATACML*`) is a separate DSL, decoupled from Lua.
- Workspace rule: run git commands from inside this project directory (the parent `D:\dev_jp`
  has no `.git`).
