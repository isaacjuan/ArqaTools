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
- **ATROOMTAG / ATPERIMETER / ATLINEARLENGTH** labels stay linked to their curve through
  reactors and update when it changes.

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

Moving these commands to Lua removed about 830 lines of C++ and added about
150 (mostly the new `moveEntities`/`copyEntities` bindings and `CopyObjects`).
For example, `DistributeTools.cpp` went from 274 to 99 lines and
`SeqNumTools.cpp` from 302 to 162.

## Candidates

Other label commands could follow the same pattern: sum length and the area
label (`at.sumLengthLabel` and `at.areaLabel` already exist).
