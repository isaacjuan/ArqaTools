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

The build target is **ObjectARX for AutoCAD 2025**. Include/lib paths come from the
`OARX2025` env var (`$(OARX2025)\inc`, `$(OARX2025)\inc-x64`, `$(OARX2025)\lib-x64`).
Links against `rxapi.lib`, `acdb25.lib`, `acge25.lib`, `acgeoment.lib`, `ac1st25.lib`,
`accore.lib`, `acgiapi.lib`, `AcPal.lib`. The project previously targeted IntelliCAD 14
via the IcArx SDK (`ICAD14`/`ICAD14ODA`) — that path has been retired in favor of OARX2025.

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
- **OARX SDK headers are now used directly** (`$(OARX2025)\inc`) — this reverses the previous
  IcArx-only rule. `rxobject.h` and other ObjectARX headers are included from `StdAfx.h`.
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
- The solution and project were renamed from `HelloWorld` → `ArqaTools`. Some stale references
  to old names remain in the README and `.claude/settings.json`.
