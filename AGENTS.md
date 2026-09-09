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
- **Command names are prefixed `AT`** (e.g. `ATHELLO`, `ATGOLDENRECT`) — follow this for any
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
