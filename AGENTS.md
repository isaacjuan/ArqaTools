# AGENTS.md — ArqaTools (AutoCAD ObjectARX Plugin)

## Build

```
Build.bat                          # Debug build + deploy to Documents
VerifyBuild.bat                    # Release verification build
dotnet build mcp\ArqaToolsMcp -c Release   # MCP server (C#)
```

Or manually:
```
msbuild ArqaTools.sln /t:Rebuild /p:Configuration="Debug 2025" /p:Platform=x64 /v:m
```

- Configurations are named `Debug 2025`/`Release 2025` but build against the **2026** SDK
  (see below); output is `x64\Debug 2025\ArqaTools.arx`.
- The ARX is file-locked while loaded in AutoCAD — unload it before rebuilding.
- `Build.bat` also copies the ARX to `Documents\` and `Documents\acadPlugins\` and
  `ReloadArqaTools.lsp` to both (and `pause`s at the end). To build without touching those
  deployments, run `msbuild` directly and load the output instead, e.g.
  `(arxload "D:/dev_jp/Sync/ArqaToolsMcp/x64/Debug 2025/ArqaTools.arx")`.
- `.mcp.json` launches `mcp/ArqaToolsMcp/bin/Release/net8.0/ArqaToolsMcp.exe` — after any C#
  change, rerun `dotnet build mcp\ArqaToolsMcp -c Release` or MCP clients keep running the old
  exe.
- `CopyToDocuments.bat` deploys from `c:\HSBCAD\ArqaToolsAcad2025\`, which no longer exists —
  it fails as-is; ignore it.

The build target is **ObjectARX for AutoCAD 2026**. Include/lib paths come from the
`OARX2026` env var (`$(OARX2026)\inc`, `$(OARX2026)\inc-x64`, `$(OARX2026)\lib-x64`).
Links against `rxapi.lib`, `acdb25.lib`, `acge25.lib`, `acgeoment.lib`, `ac1st25.lib`,
`accore.lib`, `acgiapi.lib`, `AcPal.lib` — note these lib names still carry the `25`
suffix even under the 2026 SDK (ObjectARX's internal API version is decoupled from the
product year). The project previously targeted IntelliCAD 14 via the IcArx SDK
(`ICAD14`/`ICAD14ODA`), then briefly OARX2025 — retired in favor of OARX2026 because an
ARX built against one major AutoCAD version's SDK is not ABI-compatible with a different
version's host process (this caused a heap-corruption crash when a 2025-built plugin was
loaded into AutoCAD 2026). **Always build against the SDK matching the AutoCAD version
you will actually load the plugin into** — both 2025 and 2026 SDKs/installs are present
on this machine (`OARX2025` env var still points at the 2025 SDK).

## Architecture

- **Entry**: `ArqaTools.cpp` — OMF app class, `IMPLEMENT_ARX_ENTRYPOINT(CArqaToolsApp)`,
  command table in `On_kInitAppMsg`. Module def exports `acrxEntryPoint` / `acrxGetApiVersion`.
- **Module pattern**: Module `FooTools.h/.cpp` with a `namespace FooTools { ... }` holding the
  non-interactive core. Commands are registered in `On_kInitAppMsg`'s `kCommands[]` as free
  functions defined in `ArqaTools.cpp` or direct namespace pointers (`LuaTools::luaRunCommand`,
  `AcmlTools::acmlRunCommand`, `McpBridge::startCommand`); `CArqaToolsApp` itself only keeps
  `arqaHelpCommand`/`versionCommand`/`reloadCommand`.
- **Infrastructure**:
  - `CommonTools` — model space access, group-aware transforms (`GroupUnits`: a selection
    resolved into objects/whole groups, each group once; `MoveObjects`), RAII guards
    (`AcDbObjectGuard<T>`, `SelectionSetGuard`, `ForEachSsEntity`), entity reference points
  - `CadInfra` — low-level DB plumbing: `InsertText`, `InsertMText`, curve-to-label xData
    (`StoreLinkXData`, `CollectXDataLinks`), `EnsureLayer`, `EnsureLinetype`, `GetPolylineCentroid`
  - `MeasureFormat` — area/unit formatting with locale support
  - `ReactorPersistence` — transient reactor lifecycle (rebuild on DWG open, cleanup on unload).
    A new single-curve label kind is one `CurveTextReactor` subclass, one xData app name and
    one row in `RebuildLabels`' table; its `Insert*` core ends with `LinkLabel` (AreaTools.cpp).
  - `LuaTools` (`ATLUA`, `ATAILUA`) — second, additive scripting engine alongside AutoLISP/ACML.
    Vendors plain Lua 5.4 (`ThirdParty\Lua\src\`, source-only, no prebuilt lib) directly into this
    project. `LuaTools::runLuaScript(code)` runs synchronously (`lua_pcall`) and returns a real
    `{ok, output, error}` — unlike `AITools::ExecuteLispCode`'s coarse `acedInvoke` return-code
    check, or `aiLispCommand`/`aiFixCommand`, which only copy generated LISP to the clipboard for
    manual paste. Restricted stdlib (`base`/`table`/`string`/`math` only — no `io`/`os`/`package`/
    `debug`). Exposes a global `at` table — input (`getPoint/getDistance/getReal/getInt/getString/
    getKeyword/getEntity/getSelection`; ESC aborts the script, Enter → default or nil, points
    converted UCS→WCS), query (`listEntities`, `entities([type])`, `getProps(handle)`, `getVar`),
    create (`drawLine/drawCircle/drawArc/drawRect/drawPolyline/drawText/drawMText/seqNumber/
    ensureLayer` → handle string), patterns (`goldenSpiral`, `pattern*` → `{handle,...}`),
    modify (`moveEntity/moveEntities/copyEntity/copyEntities/rotateEntity/erase/setLayer/setColor/alignTo/polyBoolean/
    regionToPolyline/distribute/distributeCopies/splitLine/splitPolyline`), text (`getText/
    setText/copyTextStyle/copyDimStyle/sumText/scaleText`), reactor-linked labels (`areaLabel/
    perimeterLabel/roomTag/lengthLabel/sumLengthLabel`), layers (`layers/getCurrentLayer/
    setCurrentLayer/setLayerState`), `countBlocks`, `exportSvg` (bare file name only, always
    written to Documents), helpers (`refPoint/formatArea/formatLength`), `print`, and command
    glue (`defineCommand`, `runCommand`, `command` — run any AutoCAD/ACA command by name with
    ordered prompt answers; use the English `_NAME` form).
    Bindings for existing tools call each module's non-interactive core (`ArabesqueTools::Draw*`,
    `GoldenRectTools::DrawGoldenSpiral`, `PolylineTools::BooleanPolylines/RegionToPolyline`,
    `AlignTools::AlignObjects`, `SeqNumTools::CreateSeqNumber`, `DistributeTools::
    DistributeObjects/DistributeCopies`, `TextTools::GetText/SetText/CopyTextStyle/...`,
    `AreaTools::Insert*Label/InsertRoomTag/CountBlocks/SplitLine/SplitPolyline`,
    `LayerTools::GetCurrentLayer/SetCurrentLayer/SetLayerState`, `SvgExportTools::ExportSvg`) —
    the interactive AT* commands are thin wrappers over the same cores (now mostly Lua files,
    see `LUA_COMMANDS.md`), so new tool logic belongs in a C++ core, not in a command. Void core
    functions are wrapped with `DrawAndCollect`, which uses
    `acdbEntLast`/`acdbEntNext` to return the handles they appended. The `kFns` table in `LuaTools.cpp`
    is the single source of truth: it registers the bindings *and* feeds `describeApi()`, which
    generates the API section of the `ATAILUA` prompt — add new functions only there, with a
    signature and doc string; a new *query* function must also go into the `kReadOnly` list
    inside `isReadOnlyFunction`, or MCP's read-only `run_lua` cannot call it.
    Lua is compiled as C, so `luaL_error`/`luaL_check*` longjmp past C++
    destructors: in bindings, check args before opening any `AcDbObjectGuard`/creating a
    `CString`, and raise errors only after those scopes close. A count hook polls `acedUsrBrk()`
    (ESC breaks runaway loops) and enforces `LuaRunOptions::maxInstructions` (`ATAILUA` caps AI
    code at 500M instructions). Aborts are sticky: global `pcall`/`xpcall` are C replacements that
    re-raise ESC, the instruction limit and memory errors; `setmetatable` refuses `__gc` (finalizers
    run with hooks off). Each state has a 256 MB cap via a custom
    allocator (`cappedAlloc`, `kMemoryLimit`). A run is a single UNDO step (it executes inside the calling
    command). `ATLUA` runs inline code or `@path\to\file.lua` (see `test.lua`,
    `test_foundations.lua`, `test_tier1.lua`, `test_tier2.lua`); `ATAILUA` asks the configured AI (reusing
    `AITools::SendToGitHubCopilotWithHistory`/`GetConversationHistory`) to write Lua against the
    `at` API and runs it directly. Fully decoupled from ACML — no cross-references either
    direction.
  - `LuaCommands` (`ATAICMD`, `ATLUACMDS`, `ATLUARELOAD`, `ATLUACMDDEL`, `ATLUAFOLDER`) — AutoCAD
    commands written in Lua and changeable at run time. **`LuaCommands\` in this repo is the
    versioned source of truth, but the plugin loads `Documents\ArqaTools\LuaCommands`** — copy
    edited files across (or use `ATLUAFOLDER`) and run `ATLUARELOAD` to pick up changes; no C++
    rebuild. Every `*.lua` there (files starting with `_` first, as shared helpers) is loaded
    at plugin start into one persistent `LuaTools::LuaEngine` (echoing `print` to the command
    line). A file calls `at.defineCommand("NAME", fn, "description")`; `LiveDefine` registers NAME
    in group `ARQATOOLS_LUA` through one of 128 template trampolines (`addCommand` callbacks take
    no context, so slot N forwards to `Dispatch(N)`). Names that `lookupCmd`/`acedGetCName`
    already know (core AutoCAD, other ARX, our C++ AT* commands) are refused, so Lua can never
    shadow a built-in. While a file's top level runs, the engine is in loading mode: only
    `at.defineCommand/print/format*` are usable there (enforced by the `at` proxy's `__index`).
    `ATAICMD` asks for a name and a request, sends the API (`describeApi()`), the current source and
    the command's last runtime error (kept by `Dispatch`, with traceback) to the AI, validates the
    reply in a throwaway engine (must define exactly that name; every `at.X` must exist per
    `LuaTools::hasApiFunction`) with up to two automatic correction rounds, shows the code for
    approval, copies the previous version to `history\NAME_<timestamp>.lua`, writes the file and
    reloads everything (`LoadAll` = fresh engine + `removeGroup` + re-register). A file that fails
    to load unregisters whatever it defined and is remembered in `g_failedFiles` with its error;
    `ATAICMD NAME` then repairs `NAME.lua` from its source and load error instead of starting over. The sandbox: no io/os/package/debug, no
    `dofile`/`loadfile`, `load` is text-only, and global `at` is a read-only userdata proxy.
  - `McpBridge` (`ATMCPSTART`, `ATMCPSTOP`, `ATMCPSTATUS`, internal `ATMCPRUN`) — named pipe
    `\\.\pipe\ArqaTools.<pid>` for the C# MCP server in `mcp\ArqaToolsMcp` (see `MCP_DESIGN.md`).
    The pipe thread never touches ObjectARX: it hands each request to a message-only window on
    the main thread; queries are answered there, Lua runs go through `sendStringToExecute
    ("_ATMCPRUN ")` only when the document `isQuiescent()` (typing into an active prompt would feed
    it the string). MCP Lua runs use `LuaRunOptions::readOnly` (`isReadOnlyFunction` whitelist).
    This is the only background thread in the project.
  - `AiHarness` — the loop around the model for AI-written Lua (`ATAICMD`, `ATAILUA`): model call,
    cleanup, per-task validation, test run (`CommandTester`, scratch drawing, auto-answered prompts),
    AI review with the plan-view image, correction rounds, `Present`/`Approve` (human gate).
    A new AI feature fills in an `AiHarness::Task` instead of writing its own loop. Settings: the
    `harness` table in `ai_config.lua` (`AiConfig::Harness`). See `HARNESS.md`.
  - `AiConfig` — providers/models/keys for every AI request, from `Documents\ArqaTools\ai_config.lua`
    (sandboxed, read on every request); keys per provider in the registry. See `AI_SETUP.md`.
  - `SvgExportTools` (`ATSVGEXPORT`) — selection → `.svg` file. A top-level block
    reference becomes a shared `<g>` in `<defs>` (built once per unique
    `AcDbBlockTableRecord`, geometry left in block-local space) plus one `<use
    transform="matrix(...)">` per occurrence — the matrix comes straight from
    `blockTransform()`'s action on the local origin/axes (see `ComputeUseTransform`),
    not a decomposed position/rotation/scale, so it's correct under mirroring/shear
    too. ByBlock color inside a definition is deferred to CSS `currentColor`, set
    per-instance via the `<use>`'s own `color` attribute. A block nested inside
    another block is flattened into its container (composing transforms) rather than
    becoming its own separate shared definition — only the outermost reference is
    deduped. Anything else unrecognized (custom/AEC objects like `AEC_WALL`, or a
    proxy standing in for a missing object enabler) falls back to `AcDbEntity::
    explode()` — this is the general pattern to reach for whenever a command needs
    "geometry as displayed on screen" for an object type this plugin doesn't have
    native support for.

## Important quirks

- **Debug config uses the release CRT (`/MD`, no `_DEBUG`)** — Autodesk's recommendation for ARX
  debug builds. AutoCAD runs on the release CRT, and ObjectARX calls like
  `AcDbRegion::createFromCurves` / `AcDbEntity::explode` grow `AcArray` buffers inside acad that
  our inline `AcArray` code later frees; with `/MDd` that is a cross-heap free
  (`_CrtIsValidHeapPointer` assert, then heap corruption). Debug still has optimization off and
  a full PDB. Do not switch back to `MultiThreadedDebugDLL`. `ATSUPPRESSASSERTS` is now compiled
  out (it is `#ifdef _DEBUG`).
- **Language standard is `stdcpp17`**, not C++20 — the SDK uses `requires` as a method name
  (a keyword in C++20).
- **MFC + ATL mixed DLL** — `ArqaTools.cpp` declares `class CArqaToolsAtlModule : public CAtlMfcModule {};`
  with a static `_AtlModule` instance. `CAtlMfcModule` does **not** override `DllMain`; the MFC
  dynamic lib (`mfc140u(d).lib`, `UseOfMfc=Dynamic`) provides it. Do not add a custom `DllMain`.
- **`StdAfx.h` include order matters**: ATL config macros (`_ATL_APARTMENT_THREADED`, etc.) must
  be defined before any ATL/MFC headers; MFC core (`afxwin.h`/`afxext.h`/`afxcmn.h`/`afxrich.h`)
  must be included before `atlbase.h`/`atlcom.h`/`atlstr.h` so MFC module state initializes first.
- **Do NOT add the `_DEBUG` undef/restore workaround** in `StdAfx.h` — it breaks MFC CRT
  linking (links release `mfcs140u.lib` in debug builds, causing `afxCurrentResourceHandle` assertions).
- **OARX SDK headers are now used directly** (`$(OARX2026)\inc`) — this reverses the previous
  IcArx-only rule. `rxobject.h` and other ObjectARX headers are included from `StdAfx.h`.
- **Test against a Release build, not Debug**, when a command touches `explode()` or manually
  deletes non-`AcRxObject` SDK types (e.g. `AcDbBlockTableRecordIterator`) — the Debug CRT's
  heap validator can report a false-positive `_CrtIsValidHeapPointer` crash across the plugin/
  AutoCAD module boundary that Release (what real users run, and what AutoCAD's own binaries
  use) does not hit. Confirmed via `SvgExportTools`' `explode()` fallback on an `AEC_WALL`.
- Every new `.cpp` must include `StdAfx.h` as its first include and be listed in
  `ArqaTools.vcxproj` under both `<ClCompile>` (source) and `<ClInclude>` (header).
- **Command names are prefixed `AT`** (e.g. `ATSEQNUM`, `ATGOLDENRECT`) — for Lua commands and
  C++ `kCommands[]` entries alike, and update the `ATHELP` listing.

## Adding a new command

User-facing drawing/editing/label commands are **Lua commands**, not C++ — follow
`LUA_COMMANDS.md` ("Adding or extracting a command"). In short:

1. Put the logic in a non-interactive C++ core and expose it in the `kFns` table
   (`LuaTools.cpp`) with a signature and doc string.
2. Write `LuaCommands\ATMYCMD.lua` with `at.defineCommand("ATMYCMD", fn, "description", params)`.
   Declare **every** input as a parameter and never call `at.get*` in the body — only then can
   AI/MCP callers pass values by name (a file whose body prompts is flagged "prompts inside").
3. Copy the file to `Documents\ArqaTools\LuaCommands\` and run `ATLUARELOAD` (names already taken
   by core AutoCAD/other ARX/our C++ commands are refused — remove the C++ command first).
4. Add the `ATHELP` line marked "(Lua command)".

Only framework commands (AI, ACML, Lua/MCP infrastructure) are still C++: define the function
(usually in the owning module's namespace, or file-local in `ArqaTools.cpp`), add
`{ _T("ATMYCMD"), myCommand }` to `kCommands[]` in `On_kInitAppMsg()`, add a declaration where
the other commands have theirs, and list it in `ATHELP`.

## Testing / verification

No unit-test framework, CI, or linters — verification happens in a running AutoCAD:

- **Lua scripts**: `ATLUA @path\to\file.lua` — repo-root `test.lua`, `test_foundations.lua`,
  `test_tier1.lua`, `test_tier2.lua`, `test_sandbox.lua`.
- **Lua commands**: the MCP `test_command` tool runs one in a scratch drawing with
  auto-answered parameters (internally `CommandTester::Run`/`RunScript`) and returns printed
  output, a geometry report and a plan-view PNG; `AiHarness` uses the same for AI-written code.
  Blocked in such test runs (`BlockedInTestRun` in `LuaTools.cpp`): the reactor label
  functions, `at.exportSvg` and `at.command`.
- **MCP server** (`.mcp.json` → `arqatools`; needs the ARX loaded + `ATMCPSTART` in AutoCAD):
  `list_instances`/`select_instance`, `ping`, `get_api`, `list_commands`,
  `get_command_source`, `run_lua` (read-only — only `isReadOnlyFunction` names),
  `run_command` (positional `answers`), `run_acad_command`/`list_acad_commands` (drives any
  AutoCAD/ACA command), `test_command`. Lua commands with declared parameters are additionally
  published as their own tools.
- After editing a `LuaCommands\` file: copy it to `Documents\ArqaTools\LuaCommands\` on the host
  (no MCP tool can do that for you), then run `ATLUARELOAD` in AutoCAD — or via MCP
  `run_acad_command "_ATLUARELOAD"`.

## Editing `.lsp` reload commands

The reload commands live in `ReloadArqaTools.lsp`. Key vars: `*hw-project-path*` must point
to the repo root — the checked-in value (`c:\HSBCAD\ArqaToolsAcad2025`) is stale and no longer
exists, so `RELOADHWBUILD` fails until it is updated. Commands: `RELOADHW`, `UNLOADHW`,
`RELOADHWBUILD`, `RELOADHWPATH`.

## Misc

- No `npm`, `cargo`, `pytest`, etc. — `Build.bat` guards against re-initializing
  `VsDevCmd.bat` if `VSCMD_VER` is already set.
- Run git commands from inside this project directory (the workspace root `D:\dev_jp` has no
  `.git`).
- The solution and project were renamed from `HelloWorld` → `ArqaTools`, and commands were later
  reprefixed `AT*`. `README.md`, `ARCHITECTURE.md`, and most of `USER_GUIDE.md` still predate
  both changes (old `HelloWorld`/`CHelloWorldApp` naming, pre-`AT` command names) — treat them
  as historical/stale except where a section has been updated since (e.g. `USER_GUIDE.md`'s
  `ATSVGEXPORT` entry). Stale references also remain in `.claude/settings.json`.
