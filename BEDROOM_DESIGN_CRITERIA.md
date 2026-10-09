# Bedroom design criteria

Working notes from the 2026-10-07 bedroom layout session (ArqaTools MCP + AutoCAD
Architecture), done as the bedroom counterpart to `BATHROOM_DESIGN_CRITERIA.md` and
following the same working method: abstract the circulation needs, ground every
figure in a real source (and label it clearly when the source is secondary or
unverified), build reusable Lua tooling, then prove it against an actual ACA layout.
Not a finished standard — see "Open items".

## 1. Regulatory basis — what's actually confirmed vs. secondary-sourced

Unlike the bathroom session (where no Belgian figure existed at all for
circulation), bedrooms have one genuine regulatory hook: **rental housing quality
law sets a minimum room floor area.** This is still not a general residential
*building code* requirement (an owner-occupied house's bedroom isn't checked
against it), but it is a real, enforced figure for a room that gets let out.
Found via web search 2026-10-07, not read from the primary legal text directly —
treat as **secondary-sourced**, not verified-against-source-text:

- **Flanders** (Vlaamse Codex Wonen / "woningkwaliteitsnormen"): a room with its
  own kitchen, bath or toilet (but not all three) must be **>= 12m²**. A room is
  for one person unless it's **>= 18m²** *and* the occupants have a "lasting
  bond" (couple, parent+child). A self-contained dwelling's total net floor area
  (living room + kitchen + bedrooms) must be >= 18m². In force since 1 Jan 2021;
  dwellings licensed before 1 Oct 2016 are grandfathered.
- **Brussels-Capital Region**: from 2026, minimum net surface **18m² for one
  occupant, +10m² per additional occupant** (28/38/48...). Student rooms: 12m².
  An older 2014 ministerial order (reported secondhand, not confirmed against
  current text) also says a bedroom can't be a windowless central room in a row.
- **Wallonia**: only found a "light dwellings" (habitations légères)
  overcrowding rule — **15m² for one occupant, +5m² per additional occupant**,
  with at least one 10m² room if 2+ occupants. May not apply to an ordinary house.
- No general Flemish/Walloon rule was found for an ordinary bedroom in an
  owner-occupied (non-let) house — don't quote these figures as if they applied
  there.
- **CONFIRM** against wonenvlaanderen.be/woningkwaliteit (Flanders), be.brussels
  (Brussels), or the Walloon housing authority before relying on
  `ATROOMSIZECHECK` for anything beyond an early, rough design-stage flag.

**Furniture clearance (bed walkway, wardrobe door swing, desk/chair pullout):**
same situation as the bathroom — no Belgian code governs this at all. The
figures used (`ATBEDCLEARANCE` 600mm min / 700mm comfortable; wardrobe front
clearance 600-1000mm; desk front clearance 600mm) are **secondary-sourced
ergonomic convention** (furniture-retailer/trade guides reporting Neufert-style
figures), not a verified Neufert page number and not law. Same status as the
bathroom's WTCB/Buildwise-style defaults — design-stage comfort guidance only.

## 2. Clearance zone philosophy (reused + one new case)

Everything from the bathroom session's §2 carries over unchanged (flush-
attachment rule, transient-vs-simultaneous-use overlap exception, zone-vs-
fixture is the only thing `ATCLEARANCECHECK` flags, minor-overlap tolerance).
One new case came up this session:

- **A chair belongs inside its own furniture's clearance zone.** A desk chair
  (or a dressing-table stool) tucked into the desk's front clearance is not a
  clash — it's what that clearance zone is *for*. `ATCLEARANCECHECK` doesn't
  know this (it will report "clearance zone overlaps fixture" for the chair
  just like it would for an unrelated object), so this is a **documented,
  expected exception**, not a tool bug: confirmed this session when the chair
  was moved from blocking the *wardrobe's* clearance (a real clash — a chair
  parked in a neighboring fixture's walkway) to sitting inside the *desk's own*
  clearance zone (an accepted non-clash). The distinction is whose zone it is,
  not whether the geometry overlaps.
- **A two-sided clearance is normal for a bed**, unlike the single-sided fixtures
  the bathroom tooling was built around (sink, WC, shower all have one "front").
  A bed usually needs a walkway on both long sides (getting in/out, making the
  bed) unless one side is flush against a wall — hence `ATBEDCLEARANCE` takes
  two independent sides instead of `ATFRONTCLEARANCE`'s one.

## 3. Real ACA furniture footprints encountered

| Furniture | Footprint used | Notes |
|---|---|---|
| Bed | 2000mm (L) x 1500mm (W) | Double/queen-ish; headboard end is the short (1500mm) edge |
| Nightstand x2 | 500 x 450mm each | Flank the headboard end |
| Wardrobe | 4 modules, 600mm (D) x 500mm (W) each, stacked into a 2000mm run | User's layout (§4b); single-block 1500x800mm wardrobe used in the first-pass layout (§4a) was later deleted |
| Desk | 1000mm (W) x 600mm (D) | |
| Chair | 400 x 508mm | Belongs to the desk — see §2 |

Specific to this drawing's content library, not universal constants (same
caveat as the bathroom's fixture table).

## 4. The layouts built this session

Room (both layouts): **3600mm x 3400mm interior (12.24m²)**, 100mm walls (ACA
wall thickness extends *outward* from the baseline in this drawing's wall style
— confirmed by checking wall bounding boxes after drawing; this is the
**opposite** of what the bathroom session found, where thickness ate *inward*.
**Don't assume either direction — verify per-drawing/per-style before trusting
a baseline-to-interior offset.**) One 900mm door on the south wall, one 1200mm
window on the north wall.

### 4a. First pass (AI-placed, superseded)

As placed via Tool Palette, the bed was centered in the room, not against any
wall, which left only ~270mm between the bed and the wardrobe/desk run along
the east wall — not enough for any front clearance, a genuine zone-vs-fixture
problem. Fixed by moving the bed + both nightstands 450mm west (headboard end
~flush with the west wall). That freed a 700mm clearance in front of the
wardrobe and 600mm in front of the desk, but the bed's *north*-side clearance
(toward the window wall) only had 607mm available — a tight fit against the
600mm minimum, not the 700mm comfortable default. Workable, but tight.
Superseded by the user's own layout (§4b), which avoided the tight spot
entirely. Kept here as a record of the clash-and-fix process, not as the
recommended arrangement.

### 4b. User's layout (validated, recommended)

The user then rebuilt the furniture arrangement by hand, from their own
judgment rather than the AI-first placement:

- **Desk + chair** in the NW corner, desk back to the north wall, chair in
  front of it (south side) — a natural, no-clash pairing from the start.
- **Wardrobe**, rebuilt as 4 stacked 600x500mm modules along the **west**
  wall (2000mm total run), next to the door.
- **Bed**, pushed flush to the **east** wall instead of the west (opposite of
  the first pass), headboard at the east end, nightstands flanking it north
  and south.

This arrangement turned out better than the AI-first one: because the bed's
open long sides now face into the room's open floor (744mm to the north wall,
1156mm to the south) instead of toward a wall, **both** sides clear the full
700mm comfortable depth — no 600mm-minimum tight spot anywhere. The wardrobe's
700mm front clearance and the door's swing zone overlap a little near the
door, which is an acceptable transient-zone-on-transient-zone overlap (same
single-occupancy logic as the bathroom session), not a defect.

*Update 2026-10-09 (`ATFURNITURECHECK`):* the bed's north side does have 744 mm to the wall,
but **it cannot be reached**: the desk (east edge x = 1495) and the bed (west edge x = 1576)
leave an 81 mm gap, so the strip north of the bed, and the north nightstand, are cut off from
the door. The bed can be made from the south side only. Moving or narrowing the desk (or
turning it) opens the strip; with the desk removed the check passes.

Final clash scan (`ATCLEARANCECHECK`, bedroom-relevant results only — the
rest is leftover bathroom-session geometry still in this drawing, already
documented in `BATHROOM_DESIGN_CRITERIA.md`): **zero bedroom clashes** except
the desk-chair non-clash described in §2.

`ATROOMSIZECHECK` against this room: **12.24m² meets the Flemish single-room
12m² minimum**, with only 0.24m² to spare — would fail if the room shrank at
all, regardless of which furniture layout is used.

## 5. Known tooling caveats (new this session)

- **`AEC_SPACE`'s `getProps` area reading is unreliable**, similar in spirit to
  the bathroom session's `AEC_MVBLOCK_REF` bounding-box finding. A space object
  generated with `-SPACEADD` over this exact 3600x3400 room reported **8.605m²**
  (perimeter/`length` property did read a correct 14000mm, i.e. 2*(3600+3400),
  but `area` did not match a plain rectangle of that perimeter). Root cause not
  diagnosed this session — possibly a style-level default area, a non-rectangular
  generated boundary not reflected in bounding box, or another view/display-rep
  dependency like the one already reported for `AEC_MVBLOCK_REF`. **Mitigation
  used**: for `ATROOMSIZECHECK`, draw a plain `ATRECT` boundary matching the
  known wall-interior coordinates instead of trusting the `AEC_SPACE` object's
  own area. Don't trust `AEC_SPACE.area` for a regulatory check until this is
  root-caused.
  *Update 2026-10-09:* `at.getAecProps(h).area` reads ACA's own calculated
  area through COM. On the spaces now in the drawing it agrees with
  `getProps` (bedroom space 4740: 12.24m² both ways); the 8.605m² reading
  could not be reproduced, so the original space was probably edited or
  deleted since. Cross-check the two when in doubt.
- **Wall thickness direction is not consistent drawing-to-drawing** — see §4.
  Always draw one test wall and check its bounding box against the baseline
  before committing to a full room loop's coordinates.
- All the previously-documented caveats (OSMODE reset on restart, wall-erase
  cascading to hosted doors/windows, `run_acad_command` must be sequential,
  watchdog auto-cancel on a trailing prompt) still apply and were hit/handled
  the same way this session.
- The ArqaTools MCP pipe dropped once mid-session (tools briefly listed as
  "disconnected") and reconnected on its own within the same turn, with no data
  loss — noting as an observed transient, not yet root-caused.

## 6. Space planning principles (same as bathroom, reconfirmed)

- Don't centre-place a fixture that has a "back" (bed headboard, wardrobe,
  desk) in the middle of a room by default — it burns clearance on both sides
  when only one side needs it. Push it to a wall first, then check what's left.
- A chair that will live tucked under/into a desk or vanity should be placed
  there from the start, not left wherever it was dropped — an untucked chair
  reads as a real clash against *whichever* fixture's clearance it happens to
  be sitting in, which this session hit directly (chair blocking the wardrobe,
  not the desk it belonged to).
- **Which wall a bed backs onto matters more than just "is it against a
  wall."** Pushing the bed's *short* (headboard) end to a wall leaves both
  *long* sides open to room floor, which is where the walkway/making-the-bed
  clearance is actually needed — this gave both sides a full comfortable
  700mm in the user's layout (§4b). The first AI-placed layout (§4a) also put
  the bed against a wall, but left one long side facing *another* wall (the
  window wall) instead of open floor, which squeezed that side down to a
  tight 600mm minimum. Same "against a wall" instinct, different result —
  check which sides of the fixture actually need the clearance before
  deciding which wall to use.

## 7. Tool reference (this session's additions)

In `Documents\ArqaTools\LuaCommands\ATBEDROOM.lua`, reload with `ATLUARELOAD`:

- **`ATBEDCLEARANCE`**: walkway clearance flush against one or two sides of a
  bed (secondary-sourced default 700mm/600mm minimum).
- **`ATROOMSIZECHECK`**: checks a room's floor area against a region's rental-
  housing-quality minimum (Flanders/Brussels/Wallonia, secondary-sourced — §1).
  Also has a `Quito` mode (area + shorter side per room type, from the Quito
  building code) — see `QUITO_SPACE_STANDARDS.md`.

Reused as-is from the bathroom session's `ATCLEARANCE.lua`: `ATFRONTCLEARANCE`
(wardrobe and desk front clearance), `ATDOORSWINGZONE` (room door),
`ATCLEARANCECHECK` (zone-vs-fixture clash scan, drawing-wide).

## 8. Open items / not yet resolved

- Wardrobe/desk clearance figures are secondary-sourced ergonomic convention,
  same unverified status as the bathroom's non-WC fixtures — no measured
  reference block exists for bedroom furniture the way the WC envelope was
  measured from the Tutt & Adler blocks.
- Sliding-door wardrobes (which the research says need no swing clearance, just
  a ~500mm walkway) were not modeled — this session's wardrobe is treated as
  hinged-door by default. Worth a dedicated check once a specific wardrobe
  style/door type is chosen.
- `AEC_SPACE` area unreliability (§5) is a new, undiagnosed finding — not yet
  reported to the ArqaTools dev session the way the `AEC_MVBLOCK_REF` bbox issue
  was.
- Brussels 2014 ministerial order text (bedroom can't be a windowless central
  room) and the exact current Wallonia rule for ordinary (non-light-dwelling)
  housing are still unconfirmed against primary sources.
