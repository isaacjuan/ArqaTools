# Session handoff (2026-10-09)

Where the design-criteria work stands, and how to pick it up. Branch `feature/mcp`, pushed to
origin up to `2db029b`. Read `CLAUDE.md` first, then `DESIGN_PRINCIPLES.md` (the method) and
`QUITO_SPACE_STANDARDS.md` (code figures, test fixtures).

## 1. Resume in 5 steps

1. Start AutoCAD 2026 and open `D:\dev_jp\Sync\ArqaToolsMcp\TEST_FIXTURES.dwg` (git-ignored,
   local only; back it up, e.g. to SharePoint, if others need it).
2. `(arxload "D:/dev_jp/Sync/ArqaToolsMcp/x64/Debug 2025/ArqaTools.arx")`, then `ATMCPSTART`.
3. In Claude Code, `ping` (arqatools MCP) should show `TEST_FIXTURES.dwg`.
4. After editing a file in `LuaCommands\`, copy it to `Documents\ArqaTools\LuaCommands\`
   (OneDrive Documents on this machine) and run `_ATLUARELOAD`.
5. Regression: `ATDWELLINGCHECK TEST-06` must report `ALL CHECKS PASS`.

## 2. What exists

**Checks** (`LuaCommands\ATROOMCHECKS.lua`, `ATBEDROOM.lua`), all run by `ATDWELLINGCHECK <id>`:

| Area | Commands |
|---|---|
| Composition, sizes, 300 mm module | `ATROOMSIZECHECK`, `ATMODULECHECK` |
| Shape, furniture access | `ATROOMSHAPECHECK`, `ATFURNITURECHECK` |
| Daylight, depth | `ATDAYLIGHTCHECK`, `ATROOMDEPTHCHECK` |
| Doors: opening sizes (leaf + 30 mm frame), clear zones, inward swing, leaf clashes | `ATDOORCHECK`, `ATDOORCLEARCHECK`, `ATDOORSWINGCHECK` |
| Bathrooms, kitchens | `ATSANITARYCHECK`, `ATKITCHENCHECK` |
| Services: wet core, plumbing walls, drainage to the sewer | `ATSERVICESCHECK` |
| Circulation widths, access, zones (rules 1 to 8), siting, economy | `ATCIRCULATIONCHECK`, `ATPASSAGECHECK`, `ATZONECHECK`, `ATSITECHECK`, `ATECONOMYCHECK` |

**Tagging:** `ATROOMTYPE` (MCP answers `[room, roomType, dwelling, bedrooms, zone, student,
outdoor]`), `ATDOORTYPE` (Entrance), `ATFIXTURETYPE`, `ATBOUNDARYTYPE` (light boundaries and
wall markers), `ATSITEMARK` (Road, Access, ServiceAccess, Sewer; optional dwelling id),
`ATDOORMARK` (triangles as doors in sketches).

## 3. Test drawing contents (`TEST_FIXTURES.dwg`)

| Where | Dwelling | What |
|---|---|---|
| X 0 | TEST-01 | test bedroom 4740 (3600 x 3600), bathroom demo |
| X 20000 | TEST-02 | deliberate failures (passage, outward doors) |
| X 40000 | TEST-03 | no walls, light boundaries, kitchen, road |
| X 60000 | TEST-05 | student bedroom |
| X 80000 | **TEST-06** | **reference house**, compact layout, circulation 6.7 %, all checks pass |
| X 80000, y + 13742 | TEST-06A | earlier corridor version (14.7 %) |
| X 110000 | (untagged) | Eames House CSH 8 sketch, rectangles, layers `A-SKETCH*` |
| X 160000 | CSH-SKETCH | Case Study House plan from the Eames Foundation drawing, rectangles, wall markers, door triangles |

Handles of TEST-06 are listed in `QUITO_SPACE_STANDARDS.md` section 9.

## 4. Decisions taken today (all in `DESIGN_PRINCIPLES.md`)

- Doors open inward; outward only when there is no space for the leaf; leaves never meet.
- Zones do not mix: the private zone is entered, never crossed; it joins the rest at one point;
  the night route stays in the private zone.
- With a bathroom in the private zone, the shared one is a guest toilet (WC, basin).
- Circulation between rooms ranks above access inside a room (entry position is a note).
- A high-profile house may raise `maxCirc`; a compact one aims well below 10 %.
- An entrance porch is a `Portal` tagged `outdoor=Yes`: not built floor, the front door opens
  into the hall.
- Open (American) kitchen allowed; service WC off a workroom / laundry / garage allowed.
- Art. 153 door figures are the wall opening: leaf + 30 mm frame (700 / 800 / 900 leaves).
- Living and an open dining room are sized together (Art. 147 living-dining).

## 5. Open items (suggested order)

1. **TEST-06 kitchen sink:** it stands apart from the wet core (8.7 m) and drains 21.6 m;
   moving it to the kitchen's south wall shortens the run to about 18 m. Not done yet.
2. **Services, next part:** hot water (heater position, runs), electricity (meter near the
   service access), gas (cooker near an exterior wall). Only water and drainage exist.
3. **Candidate checks** from `ARCHITECTS_LESSONS.md` not adopted yet: C4 light on two sides,
   C5 garden side, C9 step-free route, C11 section-aware siting, C6 already covered by
   `ATSERVICESCHECK`; others lower priority.
4. **CSH-SKETCH:** confirm against the original whether kitchen and workroom are separated by
   a wall (it splits the service zone) and the door positions; windows and fixtures are not
   drawn, so daylight and bathroom results there are not meaningful. Sizes are scaled from a
   screenshot (about 30 mm/px).
5. **Eames sketch (X 110000):** not tagged; door triangles not drawn (positions unknown).
   Reference plans: MIT Dome https://dome.mit.edu/handle/1721.3/41250 , LOC HABS
   https://www.loc.gov/item/ca4169 .
6. **Optional laundry** in TEST-06 (the only remaining composition note).
7. **Team summary** of the checks and house rules (offered, not written; ask who it is for and
   the language).

## 6. Known limitations

- `hostWall` (door clearance) can pick the wrong wall for a door right at a corner; the swing
  check does not depend on it.
- Door marks (sketches): connections only; no size, clearance or swing checks.
- Drawing ACA objects over MCP has quirks (door point is not the centre, swing side by offset,
  `AECOPENINGFLIPSWING`, COM for widths): see `CLAUDE.md`, "Drawing ACA objects".
- The MCP connection drops when AutoCAD is busy in a command or dialog; after an AutoCAD
  restart, reload the ARX and `ATMCPSTART` (and check the open drawing is not an autosave).
- Untracked, deliberately not committed: `.claude/settings.local.json`, `.mcp_probe.jsonl`,
  `.mimocode/`, `AEC_AI_LANDSCAPE.md`, `AI_OPERATES_HUMANS_DECIDE.md`.
