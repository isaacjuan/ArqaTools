# Quito space standards (residential)

Extracted 2026-10-09 from `D:\dev_jp\doc\ORD-3746 - NORMAS DE ARQUITECTURA Y URBANISMO.pdf`.
The text inside is **Ordenanza 3457** of the Concejo Metropolitano de Quito (2003), which
replaces Ordenanza 3445, *Normas de Arquitectura y Urbanismo*. The file name (3746) suggests
a later amendment: **check the current DMQ text before relying on these figures.**

Status compared with the Belgian notes (`BEDROOM_DESIGN_CRITERIA.md`,
`BATHROOM_DESIGN_CRITERIA.md`):

- These are **primary-sourced** (read from the ordinance text, not from a web summary).
- They are a **general building code** for all housing in the Distrito Metropolitano de
  Quito, owner-occupied included, not a rental-housing rule.
- Text was extracted with `pdftotext` (no page rendering available). The Art. 147 table was
  rebuilt from the extracted layout; its subtotals add up, which confirms the columns.

All dimensions are **clear (useful) dimensions** between finished surfaces, not centre lines
(Art. 66).

## 1. Minimum room sizes (Art. 147)

| Room | Min. side | 1-bed dwelling | 2-bed | 3+ bed |
|---|---|---|---|---|
| Living + dining | 2.70 m | 13.00 m² | 13.00 m² | 16.00 m² |
| Kitchen | 1.50 m | 4.00 m² | 5.50 m² | 6.50 m² |
| Main bedroom | 2.50 m | 9.00 m² | 9.00 m² | 9.00 m² |
| Bedroom 2 | 2.20 m | - | 8.00 m² | 8.00 m² |
| Bedroom 3 | 2.20 m | - | - | 7.00 m² |
| Bathroom | 1.20 m | 2.50 m² | 2.50 m² | 2.50 m² |
| **Subtotal useful area** | | **28.50 m²** | **38.00 m²** | **49.00 m²** |
| Laundry / drying | 1.30 m | 3.00 m² | 3.00 m² | 3.00 m² |
| Staff bedroom | 2.00 m | 6.00 m² | 6.00 m² | 6.00 m² |

Complementary rules (Art. 147):

- Bedroom areas **include the wardrobe**. A built-in wardrobe is at least 0.72 m² in the main
  bedroom and 0.54 m² in the others, always at least 0.60 m deep.
- **No bedroom or bathroom may be the only way into another room.**
- With more than one bedroom and only one bathroom, the bathroom must be reachable from a
  room that is not a bedroom.
- Only bathrooms may rely on duct or mechanical ventilation.
- The table stops at "3 or more bedrooms"; it does not say what a 4th bedroom needs.
  `ATROOMSIZECHECK` applies the bedroom 3 figure (assumption).

## 2. Bathrooms (Art. 150, Art. 68)

- At least one bathroom per dwelling with WC, washbasin and shower. The washbasin may be
  outside the WC/shower room, next to it.
- Shower: at least 0.56 m², shorter side at least 0.70 m, separate from the other fixtures.
  A shower may not drain over another fixture.
- Clearances measured from the fixture's projection:

| Between | Minimum |
|---|---|
| Two consecutive fixtures | 0.10 m |
| Fixture and side wall | 0.15 m |
| Fixture and the wall in front of it | 0.50 m |

These are legal minimums, much smaller than the comfort defaults in `ATFRONTCLEARANCE`
(WTCB/Buildwise-style). Special fixtures follow the manufacturer's specification.
Public buildings also need an accessible toilet (NTE INEN 2 293).

## 3. Kitchen (Art. 149)

- Worktop at least 0.60 m deep with a built-in sink; space for a cooker and a fridge.
- Circulation width: 0.90 m in general and for a single worktop; 1.10 m for a single worktop
  facing 30 cm shelving. **Facing worktops: the value is missing in the source document.**
- A kitchen or laundry may take light and air from a service patio of at least 9 m² when the
  window is 3.00 m from the facade line (Art. 155).

## 4. Laundry (Art. 152)

- At least 3 m², shorter side 1.30 m, covered, half-covered or open.
- May be part of the kitchen if the equipment and its working space are provided; the 3 m²
  drying area still applies.
- May be replaced by a shared laundry room: one washer-dryer per 4 dwellings.

## 5. Heights, doors, circulation

| Item | Minimum | Article |
|---|---|---|
| Clear ceiling height | 2.30 m | 148 (and 67) |
| Under a sloping ceiling, lowest point | 2.05 m (attics may be lower) | 148 |
| Corridor inside a dwelling | 0.90 m | 160 |
| Shared corridor, multi-family | 1.20 m | 160 |
| Stair inside a house (winders and spiral allowed) | 0.90 m clear incl. handrail | 161 |
| Shared stair (apartments, lodging) | 1.20 m clear incl. handrail; landings as wide | 161 |
| Basement, attic, service stair | 0.80 m | 161 |
| Stair step | tread >= 0.26 m, 60 < 2×riser + tread < 64 cm | 161 |
| Stair headroom | 2.10 m (no beams below it) | 161 |
| Front door opening | 0.96 × 2.03 m | 153 |
| Interior door opening | 0.86 × 2.03 m | 153 |
| Bathroom door opening | 0.76 × 2.03 m | 153 |
| Guard at any drop | 0.90 m high | 154 |
| Lift | required from 5 storeys, basements included | 164 |

Public buildings (Art. 80, 87, 89): corridors 1.20 m (1.80 m for two wheelchairs, local
narrowing to 0.90 m), doors 0.90 × 2.05 m clear, exits 1.20 m (0.60 m per person).

## 6. Light and ventilation

- Every room except bathrooms, stairs, corridors, parking and storage needs a window to the
  outside (Art. 69).
- **Window area >= 20 % of the room's floor area**, and the opening part >= 30 % of the window
  (Art. 69).
- **Room depth <= 5 × the window's smaller dimension** (Art. 151). Integrated rooms count
  each from its own window; deeper rooms may add skylights or high windows.
- A window sill below 0.80 m needs a guard; floor-to-ceiling glazing uses safety glass
  (Art. 70).
- Light wells: at least 12 m², every side >= 3.00 m, up to three storeys. Taller
  multi-family buildings: shorter side >= 1/3 of the wall height, with 6.00 m as the minimum
  needed (Art. 159).
- Ventilation ducts (Art. 156): single-family, up to 6 m long, 0.10 m diameter with mechanical
  extraction; multi-family under 3 storeys, 0.04 m², side >= 0.20 m, max 6 m high; up to 5
  storeys, 0.20 m², max 12 m; taller, side >= 0.60 m and >= 0.18 m² free of services.

## 7. Party walls (Art. 157)

Hollow block or brick 0.15 m; solid or filled 0.12 m; reinforced concrete 0.10 m.

## 8. House rules and the 300 mm module

The project's own design rules (zones and their siting, defining spaces, transition and
circulation, en-suite bathrooms, unobstructed doors, the 300 mm module) are in
`DESIGN_PRINCIPLES.md`. Only the module's effect on the Quito minimums stays here.

### Modular minimum sides

Project decision (2026-10-09), on top of the code: **every space dimension is a multiple of
300 mm (0.30 m)**, so sizes are easy to choose and coordinate. Applied to the room boundary as
drawn (clear interior dimensions), tolerance 1 mm.

The legal minimum sides then round up to the next multiple:

| Room | Legal min. side | Modular min. side |
|---|---|---|
| Living + dining | 2.70 m | 2.70 m |
| Kitchen | 1.50 m | 1.50 m |
| Main bedroom | 2.50 m | **2.70 m** |
| Bedroom 2 / 3 | 2.20 m | **2.40 m** |
| Bathroom | 1.20 m | 1.20 m |
| Laundry / drying | 1.30 m | **1.50 m** |
| Staff bedroom | 2.00 m | **2.10 m** |

Corridors (0.90 m, 1.20 m) are already on the module. Door openings (0.96 / 0.86 / 0.76 m) are
element sizes, not space dimensions, and are not checked.

## 9. Tooling

- `ATMODULECHECK` checks every straight edge of closed polylines and the length/width of ACA
  spaces against the module (default 300 mm; arc edges are listed as not checked).
  `ATROOMSIZECHECK` (Quito) also reports it, with the modular minimum side.
- `ATROOMTYPE` tags a room boundary or ACA space with its `roomType` (Living, Kitchen,
  MainBedroom, Bedroom2, Bedroom3, Bathroom, Laundry, ServiceBedroom), a `dwelling` id and the
  dwelling's `bedrooms` count (`at.setData`, stored in the DWG).
- `ATROOMSIZECHECK` with `jurisdiction = Quito` checks both the area and the shorter side. It
  takes `roomType` and `bedrooms` as given, else from the room's tags. The shorter side is the
  room's clear width (`at.roomWidth`: the diameter of the largest circle inside it), exact for
  a rectangle at any angle and the main body's width for an irregular room; the bounding box
  is only a fallback. An `ATRECT` boundary or an
  `AEC_SPACE` both work (space areas now cross-check with `at.getAecProps`, see
  `BEDROOM_DESIGN_CRITERIA.md` §5).
- `ATROOMCHECKS.lua`, reading real ACA doors and windows per room:
  - `ATDAYLIGHTCHECK`: window width × height ≥ 20 % of the floor area (Art. 69). Bathrooms
    exempt. The 30 % opening part is not checked (ACA does not say how much of a window opens).
  - `ATROOMDEPTHCHECK`: depth from the window's wall ≤ 5 × the window's smaller dimension
    (Art. 151); with several windows, one passing window is enough (assumption).
  - `ATDOORCHECK`: door width and height against Art. 153. Type from the `ATDOORTYPE` tag,
    else a door opening to a bathroom is a bathroom door, else interior. Sizes are ACA's
    width/height as the door style measures them; the code means the rough opening.
  - A room's type comes from its `ATROOMTYPE` tag, else an ACA space named like a room type
    ("Bathroom"). A door or window belongs to a room when its centre is within 300 mm of it
    (the wall); a door also when a corner of its extents is inside (outward-swinging doors).
  - `ATPASSAGECHECK`: builds the dwelling's room/door graph and checks that no bedroom or
    bathroom is the only way into another room, and that a single bathroom for several
    bedrooms opens onto a non-bedroom (Art. 147). A door touching one room leads "outside";
    model every room, or an unmodelled space counts as outside. Open connections without a
    door or ACA opening are not seen.
    Also the circulation house rule (`DESIGN_PRINCIPLES.md` §4).
  - `ATDWELLINGCHECK <id>`: everything for one dwelling: composition (living, kitchen,
    bathroom, bedrooms as tagged, laundry), the Art. 147 useful-area subtotal
    (28.5 / 38 / 49 m²), sizes + module, daylight, depth, the dwelling's doors, access. Ends
    with a per-section problem count.
- Room types `Corridor`, `Hall` and `Portal` (circulation / transition): size check uses the
  Art. 160 width (0.90 m), no area; exempt from daylight and depth.
  - `ATCIRCULATIONCHECK`: clear width of every circulation room (Art. 160: 900 mm inside a
    dwelling, `minWidth = 1200` for shared corridors). Room width = the closest pair of
    parallel facing edges of its outline (each arm of an L); an obstacle inside it (blocks, ACA
    multi-view blocks) leaves the wider of its two gaps to the facing edges. Door leaves are
    transient and not counted. Also part of `ATDWELLINGCHECK`.
  - `ATDOORCLEARCHECK`: the door zone of `DESIGN_PRINCIPLES.md` §7 (opening width × `depth`, default 900 mm, both
    sides of the host wall, following the wall's direction) must not overlap any obstacle
    (blocks, ACA multi-view blocks). Touching the zone's edge is fine.
- Geometry functions: `at.outline(h)` (footprint in plan; ACA objects other than spaces use
  their extents rectangle, exact for axis-aligned walls and fixtures) and `at.distance(h1, h2)`
  (clear distance with closest points).
  - `ATSANITARYCHECK`: per bathroom, fixtures (blocks / ACA multi-view blocks typed from their
    style or block name, English or Spanish, or a `fixtureType` tag): shower ≥ 0.56 m² with a
    side ≥ 0.70 m; ≥ 0.10 m between fixtures; WC / basin / bidet ≥ 0.15 m to a side wall and
    ≥ 0.50 m to the wall in front (Art. 68). The back of a fixture is the room edge nearest to
    it. With a dwelling: at least one bathroom with WC and shower or bath (Art. 150). ACA
    fixture extents depend on the view: check in plan.
  - `ATZONECHECK`: the zone rules of `DESIGN_PRINCIPLES.md` §2.
  - `ATKITCHENCHECK` (Art. 149): worktop ≥ 0.60 m deep; aisle in front of each worktop
    (three rays across its width, up to the nearest element or the room edge) ≥ 0.90 m, or
    ≥ 1.10 m when it faces shelving; facing worktops get the general 0.90 m, since the
    ordinance's figure is missing. Missing sink, cooker or fridge are reported as checks.
  - `ATFIXTURETYPE` tags blocks, ACA multi-view blocks or plain rectangles (schematic
    layouts) as WC, Basin, Shower, Bathtub, Bidet, Worktop, Sink, Cooker, Fridge or Shelving;
    rectangles count as fixtures only when tagged. A fixture's back is the wall it runs along
    the longest among those it touches; a fixture in a corner touching two walls equally can
    be read either way.
  - All of them run inside `ATDWELLINGCHECK`.
- Room types `Dining` (counts with living, Art. 147), `Garage` (Art. 162 parking rules) and
  `Storage`: no room minimum.
- **Guest toilet (interpretation, to confirm):** Art. 147 gives one bathroom figure (2.50 m²,
  side 1.20 m) and no separate one for a WC-and-basin room. `ATROOMSIZECHECK` applies it fully
  to bathrooms with a shower or bath; a bathroom without one that falls short is reported as a
  "check" (confirm the figure does not bind a guest toilet), not a failure. The dwelling still
  needs a complete bathroom (Art. 150), which must meet Art. 147.
- Not automated: the 30 % openable part of windows (Art. 69), ventilation ducts (Art. 156),
  heights and guards (Art. 148, 154), stairs (Art. 161).
- Test fixtures in `TEST_FIXTURES.dwg` (repo root, not versioned; formerly the unsaved
  `Drawing1`):
  - X = 20000, dwelling `TEST-02`: hall, bathroom, two bedrooms with ACA walls and doors;
    bedroom 2 reachable only through bedroom 1, a deliberate Art. 147 violation. Entrance door
    5AD5 is tagged `Entrance`; fixture 5AFA (900 × 900) stands in the hall's south-east
    corner, clear of every door zone, and leaves exactly the 900 mm passage minimum. Doors
    5AD5 (entrance) and 5AED open outward on purpose: failing cases for `ATDOORSWINGCHECK`
    (as is 5B03 in TEST-03).
  - X = 40000, dwelling `TEST-03`: hall, living, dining and a bedroom drawn without walls,
    freestanding entrance door 5B03; light boundaries: hall/living open, living/dining floor
    (marker 5B00), living/bedroom wall (marker 5B02, so the bedroom is unreachable). Kitchen
    5B07 above the dining room with its own exterior door 5B0A and tagged rectangles: worktop
    5B0B (north wall) with sink 5B0D and cooker 5B0E, fridge 5B0F, shelving 5B10 (1500 aisle);
    road line 5B08 along the south
    (y = -2000) and access point 5B09, marked with `ATSITEMARK`.
  - X = 60000, dwelling `TEST-05`: a 2700 × 3000 student bedroom (`student=Yes`) with tagged
    rectangles: single bed 5B16 long side against the north wall, desk 5B17 with chair 5B18
    against the south wall, freestanding door 5B19; passes `ATFURNITURECHECK`.
  - X = 80000, dwelling `TEST-06`: **the reference house, passes every check of
    `ATDWELLINGCHECK`** (remaining notes: optional laundry, guest-toilet size to confirm, and
    economy guidance on three entries). The compact layout (2026-10-09, designed by Juan,
    made exact by Claude): two bedrooms, all clear dimensions on the 300 mm module, ACA walls
    150 (centre-justified), 8 ACA doors all opening inward, 5 ACA windows. Road line 5BE7 at
    y = -6000, access point 5BE8, service access point 5BE9, all marked for `TEST-06` only.
    South band: dining 5BD1 (2700 × 4200) and living 5BD2 (4200 × 4200), one space across an
    open boundary; the living room open to the entrance hall 5BD3 (1500 × 4200) where wall
    5B36 stops at y = 2100 (an opening detected by the light-boundary check); entrance 5B40 on
    the hall's east side; guest toilet 5BD4 (1200 × 1800, WC and basin, door 5B60). North band:
    kitchen 5BD0 (3000 × 3600) over the dining room, with door 5D0B to dining (east, clear of the service door leaf), service door
    5B58 and window 5D13 on its west side; bedroom 2 5BD6 (3000 × 3600); a private lobby 5BD5
    (1200 × 1800, `zone=Private`, door 5D03 from the hall) serving bedroom 2 (5B70), the family
    bathroom 5C0A (2100 × 1800, door 5BA0; WC, basin, shower) and the main bedroom 5BD7
    (3600 × 3000, door 5C02) to the north. Circulation 11.2 % (was 14.7 %). Tagged rectangles:
    fixtures, worktop with sink and cooker, fridge, dining table, sofa, per bedroom a double
    bed and a wardrobe. Use it as the regression baseline: after a change to the checks it
    must still report "ALL CHECKS PASS".
  - X = 80000, y + 13742, dwelling `TEST-06A`: copy of the earlier TEST-06 layout (corridor
    version, circulation 14.7 %), kept for comparison. `TEST_FIXTURES_user_1934.dwg` (repo
    root, not versioned) holds the hand-drawn compact sketch as it was before it was made exact.

## 10. Applied to the session layouts

- The 2026-10-07 test bedroom (3.60 × 3.40 m, 12.24 m²) meets the Quito main-bedroom minimum
  (9.00 m², side 2.50 m) with margin, but was **off the 300 mm module** (3400). On 2026-10-09
  it was enlarged to 3600 × 3600 (12.96 m²): north wall 472E moved to y = 3600, side walls
  extended, the associative space updated with `AECSPACEUPDATESELECTEDSPACES`. Its door 4730 was raised
  to 2100 (Art. 153 needs 2030) and its window 4738 widened to 1800 × 1500 (2.70 m² for the
  2.45 m² Art. 69 needs); the desk is gone and the furniture passes. The bathroom demo's door
  4297 was flipped to open inward.
