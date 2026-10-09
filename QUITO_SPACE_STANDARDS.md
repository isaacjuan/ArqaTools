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

## 8. Tooling

- `ATROOMSIZECHECK` with `jurisdiction = Quito`, a `roomType` (Living, Kitchen, MainBedroom,
  Bedroom2, Bedroom3, Bathroom, Laundry, ServiceBedroom) and the dwelling's `bedrooms`
  count checks both the area and the shorter side. The shorter side comes from the bounding
  box, so it is exact only for an axis-aligned rectangle. Use an `ATRECT` boundary, not an
  `AEC_SPACE` (see `BEDROOM_DESIGN_CRITERIA.md` §5).
- Not automated yet: window area (20 %), room depth (1:5), door widths, corridor widths,
  the "no bedroom as a passage" rule.

## 9. Applied to the session layouts

- The 2026-10-07 test bedroom (3.60 × 3.40 m, 12.24 m²) meets the Quito main-bedroom minimum
  (9.00 m², side 2.50 m) with margin.
