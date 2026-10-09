# Bathroom design criteria

Working notes from the 2026-10-07 bathroom layout session (ArqaTools MCP + AutoCAD
Architecture). Captures the design principles, reference data, and tooling caveats
established while building and checking several bathroom layouts, so later work can
reuse them instead of re-deriving them. Not a finished standard — see "Open items"
for what's still unverified.

## 1. Regulatory basis — what's actually confirmed vs. assumed

Two topics were researched for Belgium specifically. Neither has a single clean,
confirmed national figure; both default to **comfort guidance, not code compliance**:

- **Circulation / fixture clearances**: there is no Belgian NBN standard for this.
  NBN S01-400-1 is an *acoustic* standard, not circulation. Belgian accessibility
  rules are regional (Wallonia/Flanders/Brussels) and target public buildings or the
  "accessible/adaptable housing" category, not ordinary private residential
  construction. Default basis used: **WTCB/Buildwise-style comfort guidance**
  (doors ~80-90cm, ~55-60cm fixture clearances), explicitly labeled as convention,
  not law.
- **Interior-exterior (view distance / daylighting)**: the old French-style Civil
  Code figures (1.90m direct view / 0.60m oblique, Code Civil arts. 678-680) may not
  reflect current Belgian law — Belgium reformed property-neighbor-relations law via
  the new **Book 3 of the Civil Code** (law of 4 Feb 2020, in force since
  1 Sept 2021), which overhauled nuisance-window rules, and the old direct/oblique
  split may no longer apply as structured. The Brussels RRU daylighting figure
  (~1/5 of habitable floor area, Titre II Art. 10) is sourced only from permit
  decisions citing that ratio in practice, not the regulation text itself.
  **Every threshold in `ATSITE.lua` is tagged `[UNVERIFIED FIGURE]`** in its own
  output for this reason — confirm against ejustice.just.fgov.be (Civil Code) or
  urbanisme.irisnet.be (RRU) before relying on either check for anything beyond an
  early, rough design-stage flag.

## 2. Clearance zone philosophy

- **A clearance zone always goes flush (in contact) with its own fixture** — zero
  gap, zero overlap. Any overlap between a zone and the fixture it belongs to is a
  drawing error, not a design choice, even if small (confirmed with the user:
  a 7mm case was "accidental manual drawing error," not intentional).
- **Transient-use zones may overlap simultaneous-use zones** in a single-occupancy
  space. A door swing is only occupied while opening/closing the door; a fixture's
  use-clearance is only needed while that fixture is in use. Since a small bathroom
  is used by one person at a time, these two needs never coincide, so the overlap is
  acceptable and should **not** be flagged as a clash. This is why
  `ATCLEARANCECHECK` deliberately checks *zone-vs-fixture* only, never
  *zone-vs-zone* — confirmed as a deliberate design decision, documented inline in
  `ATCLEARANCE.lua`.
- A zone overlapping the **physical fixture** of a *different* fixture (not a
  transient zone) is still a real problem and should be flagged — e.g. a shower's
  recommended step-out zone overlapping a WC's solid body means you can't actually
  step out of the shower; that's a genuine functional conflict, not an acceptable
  single-occupancy transient overlap.
- Minor (sub-100mm) overlaps against a *recommended* (not minimum, not code) figure
  are generally acceptable in practice — "architecture always has errors like this,
  there is a lot to define, and it's very probable everything is OK at the end."
  Don't over-fit designs to chase a last few millimeters against a comfort-guidance
  number.
- **Placeholder fixtures change behavior.** A generic square shower symbol doesn't
  represent a corner-entry unit's real diagonal step-out path. Don't over-fix a
  clearance clash against a placeholder's assumed access face — re-check once the
  real fixture model is in place.

## 3. Reference clearance data (measured from user-supplied blocks)

Source: Tutt & Adler, *Proyectos* (English: the Metric Handbook tradition). The
user inserted two reference blocks (`Inodoro`, `B2P`) from this source into the
drawing; dimensions below were **measured directly** from the block geometry
(exploding a disposable copy, not guessed from the book):

### WC (confirmed, from the `Inodoro` block)

- **Outer / recommended envelope: 1000mm × 1400mm**, flush with the wall (depth
  measured from the back/wall edge).
- **Inner / minimum envelope: 850mm × 1100mm** — inset 75mm on each side from the
  outer envelope, 300mm shorter in depth.
- Both envelopes **share the fixture's back edge** (flush with the wall) and are
  centered on the fixture's width — not two fully concentric rectangles, more a
  "U-shaped" inset open at the back.
- Implemented as `ATENVELOPECLEARANCE` (draws both nested envelopes from one
  command, given a fixture + which side is the wall).

### Sink / basin, shower

- The `B2P` reference block had **no clearance envelope drawn** (just the fixture
  geometry) — no reference figures available from it. `ATENVELOPECLEARANCE`'s
  sink/shower-adjacent defaults are **not verified against a reference block**;
  treat them as provisional until a real reference is found.
- Simpler one-sided clearance (`ATFRONTCLEARANCE`, default 600mm depth) was used
  for sink and shower throughout this session — WTCB/Buildwise-style comfort
  guidance, not from a measured source.

## 4. Real ACA fixture footprints encountered

| Fixture | ACA style | Footprint used |
|---|---|---|
| Shower | `M_BATH_SHWR_3D Shower` | 900×900mm |
| WC | `M_BATH_TOIL_3D Toilet - Flush Valve` | 675×762mm (see §5 — this one's bounding box is unreliable) |
| Sink/basin | `M_BATH_BASIN_Basin - Rect` | 450×550mm |

These are specific product/style footprints from this drawing's content library,
not universal constants — a different WC style will have a different footprint.

## 5. Known tooling caveats (read before trusting a number)

- **`AEC_MVBLOCK_REF` bounding box can be view/display-representation-dependent,
  not a fixed value.** ACA objects draw through multiple display representations
  (Plan, Model, Elevation, Plan Low Detail...) selected by view direction and
  active display configuration; `at.getProps`'s extents come from whichever
  representation is currently active, not a single canonical footprint. Observed
  symptoms this session: the same WC instance returned 675×370, 675×762, 762×675
  (axes swapped), and 370×675-at-a-different-position across different queries,
  with no deliberate rotate/move in between in at least one case. Root cause per
  the ArqaTools dev session: likely true (not corrupted geometry), tied to
  `test_command` scratch rendering, screenshots/zooms, view changes, or other ACA
  commands flipping the active display rep. **Mitigation:** don't trust a single
  `getProps` reading for precision placement math against a *real* ACA fixture —
  re-verify after any view change, and prefer the fixture's own insertion
  point/rotation over its bounding box once that capability exists (requested from
  the dev session, not yet built as of this writing).
- **`OSMODE` (running object snap) resets to nonzero after AutoCAD restarts.** It
  must be disabled (`OSMODE` → `0` via `run_acad_command`) at the start of each
  fresh AutoCAD session before doing precision point-based wall/fixture placement
  via `-WALLADD` etc. — otherwise typed coordinates silently snap to nearby
  existing geometry (confirmed cause of several wrong-wall-dimension incidents this
  session).
- **Erasing a wall cascades to delete its hosted door/window.** Don't assume a door
  or window survives a wall rebuild — always re-insert after redrawing walls, and
  verify counts afterward.
- **`run_acad_command` calls must be sequential, never parallel** — AutoCAD can only
  run one command at a time; sending two at once causes connection/timeout errors.
- ACA multi-placement commands (`-SPACEADD`, `-WINDOWADD`, `-DOORADD`) loop back to
  a secondary "Insert point" prompt after a successful placement. A watchdog fix
  (2026-10-07) now auto-cancels this cleanly (like pressing Esc) when the bridge's
  scripted inputs run out, keeping whatever was already created — confirmed
  working. Before that fix, this required a manual Esc from a person at the
  keyboard every time.

## 6. Space planning principles

- **Don't leave unprogrammed leftover space** in a standard-size residential
  bathroom — it tends to become a dumping ground for clutter rather than useful
  floor area. Size the room to what the fixtures + clearances actually need.
- **If the brief is a larger/luxury bathroom**, extra space should be deliberately
  programmed with something (a jacuzzi/bathtub, double vanity, bench, storage),
  not left as unassigned open floor.
- A compact, well-packed arrangement (shower + WC sharing one wall side-by-side,
  sink on an adjacent wall) can fit real ACA fixtures in a footprint close to the
  original schematic target (~1500×2500mm) rather than the much larger footprint a
  naive corner-by-corner layout produces — demonstrated at ~1650×2088mm
  (3.445m²) and ~1742×2088mm (3.64m²) alternatives in this session, versus a
  first-pass 2400×2500mm (6.0m²) design.

## 7. Tool reference (this session's additions)

All in `Documents\ArqaTools\LuaCommands\`, reload with `ATLUARELOAD`:

- **`ATSITE.lua`**: `ATSITEBOUNDARY` (property line + north arrow), `ATWINDOWADD`
  (schematic window rect), `ATVIEWCHECK` (window-to-boundary distance, placeholder
  threshold), `ATDAYLIGHTCHECK` (window-area/floor-area ratio, placeholder figure).
- **`ATCLEARANCE.lua`**: `ATDOORSWINGZONE` (door leaf pie-slice), `ATFRONTCLEARANCE`
  (one-sided clearance rect from any fixture's bbox), `ATENVELOPECLEARANCE`
  (nested minimum/recommended envelopes sharing the fixture's back edge — WC
  figures verified, others provisional), `ATCLEARANCECHECK` (non-destructive
  zone-vs-fixture clash scan, recognizes both schematic geometry and real
  `AEC_MVBLOCK_REF`/`AEC_WINDOW` objects).
- **Generic utilities added because no installed command covered them**:
  `ATERASE` (erase by handle list), `ATROTATE` (rotate about a point), `ATRECT`
  (rectangle between two explicit corners, optional layer).

## 8. Open items / not yet resolved

- Sink and shower clearance envelopes have no verified reference figures (only WC
  does). Find or measure a reference block for each before trusting
  `ATENVELOPECLEARANCE`'s non-WC defaults.
- The shower fixture used throughout is a generic square placeholder; the intended
  final unit is a **corner-entry model with a diagonal step-out path**. Re-run
  clearance checks once that model is selected/inserted — the current "shower
  clearance overlaps WC" finding in the compact layout may not apply to the real
  unit.
- Current Belgian Book 3 Civil Code article numbers/figures for view distance, and
  the actual RRU Art. 10 text for daylighting, are still unconfirmed (see §1).
- `AEC_MVBLOCK_REF` view-dependent extents (§5) is reported to the ArqaTools dev
  session; a view-independent location/rotation field on `getProps` has been
  requested but not yet built as of this writing.
