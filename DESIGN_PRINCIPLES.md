# Design principles (house method)

The project's own design method for dwellings, on top of the building code (code figures:
`QUITO_SPACE_STANDARDS.md`; Belgian notes: `BEDROOM_DESIGN_CRITERIA.md`,
`BATHROOM_DESIGN_CRITERIA.md`). These are design decisions, not law. Recorded 2026-10-09.

## 1. Design decision order

1. **Locate the zones on the site** (§2): where the road and the access are, where the
   private zone goes, how the service zone is supplied.
2. **Define the spaces** (§3): choose, for each boundary, how strongly it separates.
3. **Connect them** through transition spaces (§4), keeping doors clear (§5).
4. **Size them**: code minimums (Quito) on the 300 mm module (§6).

Decide in this order: a later step never overrules an earlier one without going back to it.

## 2. Zones and their place on the site

A home has three zones:

| Zone | Rooms (default from the room type) |
|---|---|
| Public | entrance hall, portal, living room, dining room |
| Private | bedrooms and the spaces attached to them (bathrooms, en-suites) |
| Service | kitchen, laundry, garage, storage, staff bedroom |
| (circulation) | corridors: they connect zones and belong to none |

**Siting, the first design decision:**

- Start from **the road and the access**: they fix where the house is entered.
- The **public zone** faces the access: the entrance, its hall or portal, then living and
  dining.
- The **private zone** goes away from the road and the entrance: quiet, out of view.
- The **service zone** is supplied from the road (vehicles, deliveries, waste) through its own
  access, without crossing the public or private zone, and sits next to what it serves (the
  kitchen next to dining).

**Rules between zones** (project interpretation):

1. A private room is entered only from circulation or another private room, never directly
   from a public or service room.
2. Public and service rooms may connect directly (kitchen to dining).
3. Each zone hangs together through its own rooms and corridors; a split zone is a "check"
   (a garage reached from outside can be separate on purpose).

A room's `zone` tag (`ATROOMTYPE ... zone`) overrides the default, e.g. a guest toilet off the
entrance hall = Public.

## 3. Defining a space

There are many ways to define a space, from the lightest to the heaviest:

| Boundary | Separates | Example |
|---|---|---|
| A line, a mark | only the idea of a place | a rug, a beam line, a lamp |
| A change of floor | use, by touch and sight | tiles to wood |
| A change of level | use and movement, step by step | a raised dining area, a sunken living |
| A subtle curtain | view, when wanted | fabric, a screen, a sliding panel |
| A curtain wall (glass) | climate and sound, not view | glazed front to the garden |
| A heavy wall | everything: sound, view, light | stone, masonry |

Choose the boundary by what has to be separated (privacy, sound, light, view, climate), not
by habit. A space does not need four walls to be a space.

Consequence for the checks: two spaces divided only by a light boundary (line, floor, level,
curtain) are **connected** for access and zoning even though no door joins them.

## 4. Transition and circulation

**Principle:** people do not adapt instantly to a different space; it takes time to perceive
the new environment. Important changes of space go through transition spaces: halls,
corridors, portals.

- **Circulation:** useful spaces (living, kitchen, bedrooms, bathrooms, laundry, ...) connect
  only through circulation spaces (`Hall`, `Corridor`, `Portal`, whose only purpose is to
  connect) or from outside, never through another useful space. A direct door between two
  useful spaces is fine only as an extra connection.
- **Entrance:** the dwelling's entrance opens into a hall or portal, never straight into a
  useful space.
- **En-suite bathroom** (interpretation of Quito Art. 147): a bathroom whose only door leads
  into one bedroom is that bedroom's private bathroom and is allowed. It does not count as the
  shared bathroom, and a bathroom between two bedrooms is not private.
- Circulation spaces need no daylight of their own (Quito Art. 69).

## 5. Doors are never obstructed

Nothing stands in front of a door: on both sides of the wall, a zone as wide as the opening
and 900 mm deep (approach space, and the swing of a 900 leaf) stays free of furniture and
fixtures.

## 6. 300 mm planning module

Every space dimension is a multiple of 300 mm, so sizes are easy to choose and coordinate.
Applied to the room boundary as drawn (clear interior dimensions), tolerance 1 mm. Code
minimum sides round up to the next multiple (table in `QUITO_SPACE_STANDARDS.md` §8). Door
openings are element sizes, not space dimensions.

## 7. What the tools check

| Principle | Tool | Status |
|---|---|---|
| §2 rules between zones | `ATZONECHECK` | done |
| §2 siting against road and access | (none yet) | needs the road / access marked in the drawing |
| §3 light boundaries as connections | (none yet) | the room graph sees only doors and ACA openings |
| §4 transition, entrance, en-suite | `ATPASSAGECHECK` | done |
| §5 door clearance | `ATDOORCLEARCHECK` | done |
| §6 module | `ATMODULECHECK`, `ATROOMSIZECHECK` | done |
| everything for one dwelling | `ATDWELLINGCHECK <id>` | done |

Rooms are tagged with `ATROOMTYPE` (room type, dwelling, bedrooms, zone) first; doors with
`ATDOORTYPE` (the entrance).
