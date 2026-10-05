# AGENTS.md — ArqaTools (AutoCAD ObjectARX Plugin)

## Build

```
Build.bat                          # Debug build + deploy to Documents
VerifyBuild.bat                    # Release verification build
CopyToDocuments.bat                # Deploy only (from c:\HSBCAD\ArqaToolsAcad2025\)
```

Or manually:
```
msbuild ArqaTools.sln /t:Rebuild /p:Configuration="Debug 2025" /p:Platform=x64 /v:m
```

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
- **Module pattern**: Module `FooTools.h/.cpp` with a `namespace FooTools { void func(); }`.
  `CArqaToolsApp` has a static `func()` that calls `FooTools::func()`. Commands are
  registered as `{ _T("CMD"), CArqaToolsApp::func }`.
- **Infrastructure**:
  - `CommonTools` — model space access, group-aware transforms, RAII guards
    (`AcDbObjectGuard<T>`, `SelectionSetGuard`, `ForEachSsEntity`), entity reference points
  - `CadInfra` — low-level DB plumbing: `InsertText`, `InsertMText`, xData read/write,
    `EnsureLayer`, `EnsureLinetype`, `GetPolylineCentroid`, `UpdateAreaText`
  - `MeasureFormat` — area/unit formatting with locale support
  - `ReactorPersistence` — transient reactor lifecycle (rebuild on DWG open, cleanup on unload)
  - `LuaTools` (`ATLUA`, `ATAILUA`) — second, additive scripting engine alongside AutoLISP/ACML.
    Vendors plain Lua 5.4 (`ThirdParty\Lua\src\`, source-only, no prebuilt lib) directly into this
    project. `LuaTools::runLuaScript(code)` runs synchronously (`lua_pcall`) and returns a real
    `{ok, output, error}` — unlike `AITools::ExecuteLispCode`'s coarse `acedInvoke` return-code
    check, or `aiLispCommand`/`aiFixCommand`, which only copy generated LISP to the clipboard for
    manual paste. Restricted stdlib (`base`/`table`/`string`/`math` only — no `io`/`os`/`package`/
    `debug`). Exposes a global `at` table — user input (`getPoint/getDistance/getReal/getInt/
    getString/getKeyword/getEntity/getSelection`; ESC aborts the script, Enter → default or nil,
    points converted UCS→WCS), query (`listEntities`, `entities([type])`, `getProps(handle)`),
    create (`drawLine/drawCircle/drawArc/drawRect/drawPolyline/drawText/drawMText/seqNumber/
    ensureLayer` → handle string), patterns (`goldenSpiral`, `pattern*` → `{handle,...}`),
    modify (`moveEntity/copyEntity/rotateEntity/erase/setLayer/setColor/alignTo/polyBoolean/
    regionToPolyline/distribute/distributeCopies/splitLine/splitPolyline`), text (`getText/
    setText/copyTextStyle/copyDimStyle/sumText/scaleText`), reactor-linked labels (`areaLabel/
    perimeterLabel/roomTag/lengthLabel/sumLengthLabel`), layers (`layers/getCurrentLayer/
    setCurrentLayer/setLayerState`), `countBlocks`, `exportSvg` (bare file name only, always
    written to Documents), helpers (`refPoint/formatArea/formatLength`), `print`. Bindings for
    existing tools call each module's non-interactive core (`ArabesqueTools::Draw*`,
    `GoldenRectTools::DrawGoldenSpiral`, `PolylineTools::BooleanPolylines/RegionToPolyline`,
    `AlignTools::AlignObjects`, `SeqNumTools::CreateSeqNumber`, `DistributeTools::
    DistributeObjects/DistributeCopies`, `TextTools::GetText/SetText/CopyTextStyle/...`,
    `AreaTools::Insert*Label/InsertRoomTag/CountBlocks/SplitLine/SplitPolyline`,
    `LayerTools::GetCurrentLayer/SetCurrentLayer/SetLayerState`, `SvgExportTools::ExportSvg`) —
    the AT* commands are thin prompt wrappers over the same functions, so new tool logic belongs
    in such a core, not in the command. Void core functions are wrapped with `DrawAndCollect`, which uses
    `acdbEntLast`/`acdbEntNext` to return the handles they appended. The `kFns` table in `LuaTools.cpp`
    is the single source of truth: it registers the bindings *and* feeds `describeApi()`, which
    generates the API section of the `ATAILUA` prompt — add new functions only there, with a
    signature and doc string. Lua is compiled as C, so `luaL_error`/`luaL_check*` longjmp past C++
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
    commands written in Lua and changeable at run time. Every `*.lua` in
    `Documents\ArqaTools\LuaCommands` (files starting with `_` first, as shared helpers) is loaded
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
- **Command names are prefixed `AT`** (e.g. `ATSEQNUM`, `ATGOLDENRECT`) — follow this for any
  new command added to `kCommands[]` in `On_kInitAppMsg()`, and update the `ATHELP` listing.

## Adding a new command

1. Add `static void myCommand();` to `CArqaToolsApp` in `ArqaTools.h`.
2. Add `{ _T("ATMYCMD"), myCommand }` to the `kCommands[]` array in `On_kInitAppMsg()`.
3. Implement a thin wrapper in `ArqaTools.cpp` that delegates to a module function.
4. Add the corresponding module function to the relevant `FooTools` namespace.

## Editing `.lsp` reload commands

The reload commands live in `ReloadArqaTools.lsp`. Key vars: `*hw-project-path*` must point
to the repo root. Commands: `RELOADHW`, `UNLOADHW`, `RELOADHWBUILD`, `RELOADHWPATH`.

## Misc

- No tests, no CI, no linters. There is no `npm`, `cargo`, `pytest`, etc.
- `Build.bat` guards against re-initializing `VsDevCmd.bat` if `VSCMD_VER` is already set.
- The solution and project were renamed from `HelloWorld` → `ArqaTools`, and commands were later
  reprefixed `AT*`. `README.md`, `ARCHITECTURE.md`, and most of `USER_GUIDE.md` still predate
  both changes (old `HelloWorld`/`CHelloWorldApp` naming, pre-`AT` command names) — treat them
  as historical/stale except where a section has been updated since (e.g. `USER_GUIDE.md`'s
  `ATSVGEXPORT` entry). Stale references also remain in `.claude/settings.json`.
