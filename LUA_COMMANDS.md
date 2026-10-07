# Lua Commands

Several AutoCAD commands that used to be C++ are now Lua files. Changing them
needs no rebuild: edit the `.lua` file and run `ATLUARELOAD`.

The C++ side keeps only the **non-interactive cores** (geometry, groups,
reactors). Each Lua file is a thin wrapper: it asks for input, calls the core
through an `at.*` function, and prints the result. See `AGENTS.md` for the
LuaCommands framework itself.

## Where the files live

| Location | Role |
|---|---|
| `LuaCommands\` (this repo) | Source of truth, versioned |
| `Documents\ArqaTools\LuaCommands\` | What the plugin loads at start and on `ATLUARELOAD` |

After editing a file in the repo, copy it to the Documents folder (or edit it
there and copy it back before committing). `ATLUAFOLDER` opens that folder.

## Extracted commands

| Command(s) | Lua file | `at.*` functions used | C++ core kept |
|---|---|---|---|
| `ATPLACEMID` | `ATPLACEMID.lua` | `refPoint`, `moveEntity` | group-aware move in `LuaTools.cpp` |
| `ATMX` `ATMY` `ATMZ` | `ATMOVEAXIS.lua` | `moveEntities` | group-aware move in `LuaTools.cpp` |
| `ATCX` `ATCY` `ATCZ` | `ATCOPYAXIS.lua` | `copyEntities` | `AlignTools::CopyObjects` |
| `ATALX` `ATALY` `ATALZ` | `ATALIGNAXIS.lua` | `alignTo` | `AlignTools::AlignObjects` |
| `ATDISTLINE` `ATDISTBETWEEN` `ATDISTEQUAL` `ATDISTCOPYLINE` `ATDISTCOPYBETWEEN` `ATDISTCOPYEQUAL` `ATDISTTOLINE` | `ATDISTRIBUTE.lua` | `distribute`, `distributeCopies`, `getProps` | `DistributeTools::DistributeObjects`, `DistributeCopies` |
| `ATSEQNUM` | `ATSEQNUM.lua` | `getPoint`, `seqNumber` | `SeqNumTools::CreateSeqNumber` |
| `ATROOMTAG` | `ATROOMTAG.lua` | `roomTag` | `AreaTools::InsertRoomTag` + `RoomTagReactor` |
| `ATPERIMETER` | `ATPERIMETER.lua` | `perimeterLabel`, `getText` | `AreaTools::InsertPerimeterLabel` + `PerimeterReactor` |
| `ATLINEARLENGTH` | `ATLINEARLENGTH.lua` | `lengthLabel`, `getText` | `AreaTools::InsertLengthLabel` + `LinearLengthReactor` |
| `ATSUMLENGTH` | `ATSUMLENGTH.lua` | `getProps`, `formatLength`, `getPoint`, `sumLengthLabel` | `AreaTools::InsertSumLengthLabel` + `PolylineSumLengthReactor` |
| `ATINSERTAREA` | `ATINSERTAREA.lua` | `areaLabel`, `getText` | `AreaTools::InsertAreaLabel` + `PolylineAreaReactor` |
| `ATTAGALL` | `ATTAGALL.lua` | `lengthLabel` per curve | `AreaTools::InsertLengthLabel` |
| `ATCOUNTBLOCKS` | `ATCOUNTBLOCKS.lua` | `countBlocks`, `getSelection` | `AreaTools::CountBlocks` |
| `ATSPLITLINE` `ATSPLITPOLI` | `ATSPLIT.lua` | `splitLine`, `splitPolyline`, `getProps` | `AreaTools::SplitLine`, `SplitPolyline` |
| `ATCOPYTEXT` `ATCOPYSTYLE` `ATCOPYTEXTFULL` `ATCOPYDIMSTYLE` | `ATTEXTCOPY.lua` | `getText`, `setText`, `copyTextStyle`, `copyDimStyle`, `getSelection` | `TextTools::GetText`, `SetText`, `CopyTextStyle`, `CopyDimStyle` |
| `ATSUMTEXT` | `ATSUMTEXT.lua` | `sumText`, `getPoint`, `drawText` | `TextTools::SumTextValues` |
| `ATSCALETEXT` | `ATSCALETEXT.lua` | `scaleText`, `getSelection` | `TextTools::ScaleTextHeight` |
| `ATCHGTOLAYER` | `ATCHGTOLAYER.lua` | `getCurrentLayer`, `setLayer` | (none) |
| `ATNL` | `ATNL.lua` | `layers`, `setCurrentLayer` | `LayerTools::SetCurrentLayer` |
| `ATMATCHLAYER` | `ATMATCHLAYER.lua` | `getProps`, `setLayer` | (none) |
| `ATFREEZELAYER` | `ATFREEZELAYER.lua` | `getProps`, `setLayerState` | `LayerTools::SetLayerState` |
| `ATBOOLPOLY` `ATSUBPOLY` `ATINPOLY` `ATUNIONPOLY` | `ATPOLYBOOLEAN.lua` | `polyBoolean`, `getProps` | `PolylineTools::BooleanPolylines` |
| `ATREG2POLY` | `ATREG2POLY.lua` | `regionToPolyline` | `PolylineTools::RegionToPolyline` |
| `ATSVGEXPORT` | `ATSVGEXPORT.lua` | `exportSvg`, `getSelection` | `SvgExportTools::ExportSvg` |
| `ATCATENTITIES` | `ATCATENTITIES.lua` | `listEntities` | `CategorizeTools::DBObjectMap` |
| `ATARABESQUE` `ATHOJANAZARI` `ATARABESCORL` `ATARABESCOTOROSOL` `ATARABESCOHIPSOL` | `ATARABESQUE.lua` | `pattern*` | `ArabesqueTools::Draw*` |
| `ATGOLDENRECT` `ATGOLDENRECTIN` `ATGOLDENRECTINW` | `ATGOLDENRECT.lua` | `goldenSpiral`, `rectFrame`, `rectInFrame` | `GoldenRectTools::DrawGoldenSpiral`, `ReadRectFrame`, `DrawRectInFrame` |

`ATHELP` lists these with the suffix "(Lua command)".

### Behavior notes

- **Groups:** objects in an `AcDbGroup` move with their whole group, and a
  group moves once even if several members are selected. Copies
  (`ATCX/Y/Z`) copy the whole group, but the copies are not grouped.
- **Coordinates:** point parameters arrive in WCS, so the X/Y/Z commands work
  on world axes.
- **Align anchor** (`ATALX/Y/Z`): circle/arc center, curve start point,
  text/block insertion point, otherwise the bounding-box minimum.
- **ATPLACEMID** uses the bounding-box center.
- **Distribute modes:** `linear` puts items on both endpoints (min 2),
  `between` excludes the endpoints (min 1), and `equal` leaves half a gap at
  each end (min 1). `ATDISTTOLINE` uses the picked curve's start and end points
  (`getProps` returns `startPoint`/`endPoint` for any curve).
- **Label commands** (ATINSERTAREA, ATROOMTAG, ATPERIMETER, ATLINEARLENGTH,
  ATTAGALL, ATSUMLENGTH): labels stay linked to their curve(s) through
  reactors and update when they change.
- **Prompts inside the command:** commands that validate a first pick before
  asking for more (the text copy family, ATSCALETEXT, ATSUMTEXT, ATCOUNTBLOCKS,
  ATSUMLENGTH) ask the later inputs with `at.getSelection`/`at.getPoint` inside
  the function. MCP agents cannot pass those as named values; CommandTester
  scripted answers still work.
- **No rubber-band preview** on declared distance/point parameters (ARABESQUE
  radius, ATGOLDENRECT second point): the parameter prompt has no base point.
- **ATSUMLENGTH** sums `getProps(h).length` in Lua to print the total before
  asking for the text position (`at.getPoint`), as the C++ command did.

## Adding or extracting a command

1. Make sure a non-interactive C++ core exists and is exposed in the `kFns`
   table in `LuaTools.cpp`. `kFns` is the single source of truth for the API
   and for the AI prompt, so give it a correct signature and doc string.
2. Write the Lua file:

   ```lua
   at.defineCommand("ATNAME", function(p)
       local result, err = at.someCore(p.boundary)
       if not result then print("ATNAME: " .. err .. "."); return end
       print("Done.")
   end, "One-line description", {
       { name = "boundary", type = "entity", prompt = "Select closed polyline",
         description = "What it is for" },
   })
   ```

   Parameter types include `entity`, `selection`, `point`, `distance`,
   `number`, `integer`, `string` and `keyword` (with `options` and `default`).
3. Remove the C++ command: its `kCommands` entry and wrapper in
   `ArqaTools.cpp`, its declaration in `ArqaTools.h`, and the interactive code
   in the tool file. Keep the core.
4. Add "(Lua command)" to its `ATHELP` line.
5. Build, copy the Lua file to Documents, load the ARX, run `ATLUARELOAD` and
   test by hand. You can also test through the `arqatools` MCP server
   (`test_command`), which needs `ATMCPSTART` running in AutoCAD.

## Gotchas

- **Name clash:** a Lua command cannot use a name that a C++ command (or core
  AutoCAD, or another ARX) already registers. Remove the C++ command first.
- **Several commands in one file** (`ATMOVEAXIS`, `ATCOPYAXIS`, `ATALIGNAXIS`,
  `ATDISTRIBUTE`): `ATAICMD` and `ATLUACMDDEL` will not touch these files.
  Edit them by hand.
- **Lua keywords as parameter names:** `end`, `function`, `local` and similar
  cannot be used with `p.<name>` syntax. `ATDISTRIBUTE` uses `from`/`to` for
  this reason.
- **Entities open for read:** a C++ core must close its read handle before it
  moves or modifies the entity, or the write open fails with
  `eWasOpenForRead` (this was the original `ATPLACEMID` bug).

## Effect on the C++ code

All interactive drawing and editing commands are now Lua. Only framework
commands stay in C++: ATHELP, ATVERSION, ATRELOAD, the ATAI* commands, ACML,
the ATLUA*/ATAICMD commands and the ATMCP* bridge.

The moves removed roughly 2,400 lines of interactive C++. For example,
`DistributeTools.cpp` went from 274 to 99 lines and `SeqNumTools.cpp` from 302
to 162.

## Known gaps

- `at.drawText` centers its text; the old ATSUMTEXT placed it left/baseline.
- `getProps` has no dimension style name, so ATCOPYDIMSTYLE no longer prints it.
- `at.setCurrentLayer` does not say whether it created the layer; ATNL checks
  `at.layers()` first.
- Declared distance/point parameters cannot take a base point for the
  rubber-band preview.
