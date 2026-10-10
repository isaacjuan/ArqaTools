# Design principles (house method)

The project's own design method for dwellings, on top of the building code (code figures:
`QUITO_SPACE_STANDARDS.md`; Belgian notes: `BEDROOM_DESIGN_CRITERIA.md`,
`BATHROOM_DESIGN_CRITERIA.md`). These are design decisions, not law. Recorded 2026-10-09.

## 1. Design decision order

1. **Locate the zones on the site** (§2): where the road and the access are, where the
   private zone goes, how the service zone is supplied.
2. **Define the spaces** (§3): choose, for each boundary, how strongly it separates.
3. **Connect them** through transition spaces (§4), with as little circulation as
   possible and each room entered where it is used best (§5), furnished so every piece is
   usable (§6), keeping doors clear (§7).
4. **Size them**: code minimums (Quito) on the 300 mm module (§8).

Decide in this order: a later step never overrules an earlier one without going back to it.

## 2. Zones and their place on the site

A home has three zones:

| Zone | Rooms (default from the room type) |
|---|---|
| Public | entrance hall, portal, living room, dining room, the shared bathroom (with discretion) |
| Private | bedrooms and the spaces attached to them: en-suite bathroom, the private zone's own hall or corridor |
| Service | kitchen, laundry, garage, storage, staff bedroom |
| (circulation) | a corridor not tagged with a zone: it connects zones and belongs to none |

**Siting, the first design decision:**

- Start from **the road and the access**: they fix where the house is entered.
- The **public zone** faces the access: the entrance, its hall or portal, then living and
  dining.
- The **private zone** goes away from the road and the entrance: quiet, out of view.
- The **service zone** is supplied from the road (vehicles, deliveries, waste) through its own
  access, without crossing the public or private zone, and sits next to what it serves (the
  kitchen next to dining).

**In the drawing** (`ATSITECHECK`): mark the road edge, the pedestrian access and the service
access with `ATSITEMARK` (lines, polylines or points, layer `A-SITE`). Markers given a dwelling
id apply to that dwelling only and replace the untagged ones, so several layouts can share a
drawing. Each zone's distance from the road is the area-weighted mean of its rooms' centres.

- PROBLEM: the private zone is not farther from the road than the public zone.
- PROBLEM: the service zone has no exterior door of its own (other than the entrance).
- check: the service zone lies farther from the road than the private zone.
- check: the entrance (door tagged `Entrance`) is not the exterior door nearest the access
  (or the road, when no access is marked); a marked service access is not nearest a service
  door.

**Rules between zones** (project decisions, checked by `ATZONECHECK`):

1. **A bedroom never has its access from the central hall.** A private room is entered only
   from the private zone's own hall or corridor, when one is needed (tag it `zone=Private`),
   or from another private room (its en-suite). Never from the public hall, a neutral
   corridor, or a public or service room.
2. **The shared bathroom** belongs to the public zone, with discretion (out of direct view
   from living and entrance: design guidance, not checked), and is entered **from a hall or
   corridor only**, never from a useful space. An en-suite bathroom belongs to its bedroom.
   When the private zone has its own bathroom, the shared one is a guest toilet: WC and basin,
   no shower (the complete bathroom of Art. 150 is then the private one).
3. The entrance opens into the public zone, never into the private hall.
4. Public and service rooms may connect directly (kitchen to dining).
5. Each zone hangs together through its own rooms and corridors; a split zone is a "check"
   (a garage reached from outside can be separate on purpose).

A room's `zone` tag (`ATROOMTYPE ... zone`) overrides the default; `Default` removes it.

**Order and joints of the zones** (adopted 2026-10-09 from `ARCHITECTS_LESSONS.md` C1 to C3,
also checked by `ATZONECHECK`):

6. **Zones do not mix** (from Alexander's intimacy gradient, restated by zones): the private
   zone is entered, never crossed. A public or service room reached from the entrance only
   through the private zone (a living room behind a bedroom, the shared bathroom behind the
   private hall) is a PROBLEM. Together with rule 1 (private rooms only from the private hall),
   no route mixes the zones.
7. **One joint** (Kahn's Fisher House, Wright's Usonian wing): the private zone joins the rest
   of the house at one point, its own hall's door. Two joints are a check (make sure the second
   is intended), more are a PROBLEM. Exterior doors do not count.
8. **Night route** (Klein): from each bedroom to its en-suite, else to a shared bathroom, the
   best route passes no useful room (PROBLEM: day and night routes cross). Crossing the public
   hall, or going outdoors, is a check: a bathroom off the private hall keeps the night zone
   closed (a second bathroom, or a guest WC in the day zone, resolves it with rule 2).

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

**In the drawing:**

- Two rooms whose outlines share an edge (no gap, at least 600 mm long) with no ACA wall along
  it have a light boundary. Rooms divided by a real wall always have a gap (the wall's
  thickness) and are not light boundaries.
- Where a wall stops (a hall open to the living room), the two outlines still lie a wall's
  thickness apart: for rooms modelled with ACA walls, a stretch of at least 600 mm across a gap
  up to 400 mm with no wall in it is an opening, an "open" boundary. Plans drawn with plain
  lines instead of ACA walls keep the strict rule (outlines must touch).
- Its type: draw a line or polyline along the edge and tag it with `ATBOUNDARYTYPE` (Line,
  Floor, Level, Curtain, Glass, Wall; layer `A-BOUNDARY`). Untagged it counts as "open".
  Line, Floor, Level, Curtain and open are walkable and connect the rooms; Glass and Wall
  separate them (glass only for movement, not for view). An ACA curtain wall counts as glass.
- Rooms **of the same zone** joined by a walkable light boundary are one space lightly
  articulated (a living room with a dining area one step up): the circulation rule treats them
  as one. Across zones the boundary is a direct connection, so a bedroom behind a curtain off
  the living room breaks both the zone and the circulation rules.
- `ATBOUNDARYLIST` lists what was detected; `ATPASSAGECHECK`, `ATZONECHECK` and
  `ATDWELLINGCHECK` use it.

## 4. Transition and circulation

**Principle:** people do not adapt instantly to a different space; it takes time to perceive
the new environment. Important changes of space go through transition spaces: halls,
corridors, portals.

- **Circulation:** useful spaces (living, kitchen, bedrooms, bathrooms, laundry, ...) connect
  only through circulation spaces (`Hall`, `Corridor`, `Portal`, whose only purpose is to
  connect) or from outside, never through another useful space. A direct door between two
  useful spaces is fine only as an extra connection.
- **Entrance:** the dwelling's entrance opens into a hall or portal, never straight into a
  useful space. A **portal** can be an outdoor transition, a recessed porch in front of the
  door (Alexander's entrance transition): model it as a room of type `Portal` tagged
  `outdoor=Yes` (`ATROOMTYPE`). The front door between portal and hall opens into the hall,
  as if the portal were outside, and the outdoor portal is left out of the built floor area.
- **En-suite bathroom** (interpretation of Quito Art. 147): a bathroom whose only door leads
  into one bedroom is that bedroom's private bathroom and is allowed. It does not count as the
  shared bathroom, and a bathroom between two bedrooms is not private.
- Circulation spaces need no daylight of their own (Quito Art. 69).

## 5. Space economy

A better design has **as little circulation and as much useful space as possible**. The
transition principle (§4) asks for halls and corridors; economy asks for them to be as small
as they can be while doing that job. Let halls distribute directly to several rooms instead of
adding corridor length.

**Priority: circulation between rooms comes before access inside a room.** Less hall and
corridor area is worth more than a perfectly placed door: when a compact lobby forces a door to
the end of a wall, keep the lobby. The entry position below is guidance ("note"), never a
reason to add circulation.

**Where a space is entered changes how well it can be used.** A rectangular space is best
entered through its long side: every part of it is then reached walking less, and the
furniture can use both ends. Entering through a short side turns the room into a corridor to
its far end; entering off-centre wastes the corner behind the door.

In the drawing (`ATECONOMYCHECK`, guidance: it reports "check" lines, not problems):

- circulation share: Hall, Corridor and Portal area over the dwelling's built floor (rooms
  tagged `outdoor=Yes`, such as an open porch, are listed but not counted). The ideal is
  as low as possible; the right figure depends on the design conditions, so `maxCirc` is a
  parameter (default 15 %) and above it is a check;
- for each useful room and each entry (door or walkable light boundary): the side it is on
  and the mean walking distance to every point of the room, compared with entering at the
  middle of the longest side. A room at least 1.25 times as long as wide entered from a short
  side, or more than 15 % extra walking, is a note (lower priority than circulation between
  rooms, above).

**All of a space should be usable.** Slivers, narrow niches and parts reached only through a
gap narrower than a person are floor paid for and not used. In the drawing
(`ATROOMSHAPECHECK`): the room's clear width (the largest circle inside it), and the floor
where a person 600 mm wide (`passWidth`) fits. A room that splits into parts joined by gaps
narrower than that is a PROBLEM (part of it is not accessible); more than 5 % of the floor too
narrow to use (`maxLost`) is a check. Even a perfect rectangle loses its four corners to a
600 mm disc (about 0.08 m²), which is within the margin. Furniture is not subtracted yet.

## 6. Furniture

**Furniture is what defines and serves the use of a space.** Each piece follows the same space
logic as a room, with its own nature: it has the sides it is used from, each needs free floor
in front of it, and that floor must be reachable from the room's entrance. A bed is used from
three sides, a wardrobe from its doors, a desk from where the chair goes.

| Piece | Access sides | Depth |
|---|---|---|
| Double bed | both long sides (required), foot (recommended) | 600 mm |
| Single bed | one long side (required), foot (recommended); standing with its long side against the wall: only the open long side | 600 mm |
| Wardrobe, chest of drawers, shelving | front | 600 mm |
| Desk | front (its chair belongs there) | 700 mm |
| Dining table | every free side (chairs belong there) | 700 mm |
| Sofa | front | 450 mm |
| Nightstand, chair | used from the bed / moved with use: no zone of their own | |

Depths are starting figures from the ergonomic notes (`BEDROOM_DESIGN_CRITERIA.md`); adjust the
`FURNITURE` table in `ATROOMCHECKS.lua`.

**Furnishing programme of bedrooms:** only a **student bedroom** has a desk, and its bed is a
single (narrow) one. **Other bedrooms have no desk.** Tag a student bedroom with
`ATROOMTYPE ... student=Yes`; a desk in any other bedroom, or a double bed in a student
bedroom, is a PROBLEM (a student bedroom without a desk is a check). A student's single bed can
stand with its long side against the wall, so it needs free floor on one long side only: the
room stays compact. A double bed always needs both long sides free; standing with a long side
against the wall is a PROBLEM.

In the drawing (`ATFURNITURECHECK`, also inside `ATDWELLINGCHECK`):

- a piece's back is the wall it stands against (within 100 mm), a bed's back the wall at its
  headboard (its short side); adjacent modules of a wardrobe, shelving or desk form one run;
- each access zone must not be cut by a wall or by another piece. A chair in its desk or table
  zone and a nightstand (or a small chest) by the head of a bed belong there;
- each zone must be reachable from the room's entrance on the free floor (the room minus the
  furniture) by a person 600 mm wide, entering anywhere through the door's opening
  (`at.roomReach`). This finds pieces that cut a room in two;
- a required side failing is a PROBLEM, a recommended one a check. Pieces are typed from their
  block / style name (English or Spanish) or tagged with `ATFIXTURETYPE`.

## 7. Doors

**Never obstructed.** Nothing stands in front of a door: on both sides of the wall, a zone as
wide as the opening and 900 mm deep (approach space, and the swing of a 900 leaf) stays free of
furniture and fixtures (blocks, ACA multi-view blocks and rectangles tagged with
`ATFIXTURETYPE`).

**Doors open inward.** A door opens into the space it serves: it is more secure, and the leaf
does not cut into the circulation.

- An exterior door, the entrance included, opens into the dwelling.
- An interior door opens away from the hall or corridor, into the room.
- Between two rooms, the door opens into the room further from the entrance (an en-suite
  door into the bathroom).
- Opening outward is the exception, only when the room has no space for the leaf (very
  unusual).
- **Leaves never meet:** two doors whose swings overlap (typically two doors near the same
  corner of a room) hit each other; move one along its wall or change its hand. PROBLEM when
  their plan extents (leaf and arc) overlap by more than 100 mm both ways.

In the drawing (`ATDOORSWINGCHECK`, also inside `ATDWELLINGCHECK`):

- The side a door opens into is the room, or the outside, that holds most of its plan extents
  (the leaf and its arc). "Further from the entrance" counts rooms from the door tagged
  `Entrance`, inside the dwelling (a kitchen's service door does not make it "near").
- A door opening outward is a PROBLEM, unless a square of the door's width on the inner side
  leaves the room or is cut by a fixture. Then it is a "check": the exception, to be confirmed.
- Doors with no swing drawn (sliding doors, plain openings) are skipped.

## 8. 300 mm planning module

Every space dimension is a multiple of 300 mm, so sizes are easy to choose and coordinate.
Applied to the room boundary as drawn (clear interior dimensions), tolerance 1 mm. Code
minimum sides round up to the next multiple (table in `QUITO_SPACE_STANDARDS.md` §8). Door
openings are element sizes, not space dimensions.

## 9. What the tools check

| Principle | Tool | Status |
|---|---|---|
| §2 rules between zones | `ATZONECHECK` | done |
| §2 zones do not mix, one joint, night route (rules 6 to 8) | `ATZONECHECK` | done |
| §2 siting against road and access | `ATSITEMARK`, `ATSITECHECK` | done |
| §3 light boundaries as connections | `ATBOUNDARYTYPE`, `ATBOUNDARYLIST`, room graph | done |
| §4 transition, entrance, en-suite | `ATPASSAGECHECK` | done |
| §5 space economy | `ATECONOMYCHECK` | done (guidance) |
| §5 all space usable | `ATROOMSHAPECHECK`, `at.roomWidth`, `at.roomUsable` | done |
| §6 furniture access | `ATFURNITURECHECK`, `at.roomReach` | done |
| §7 door clearance | `ATDOORCLEARCHECK` | done |
| §7 doors open inward | `ATDOORSWINGCHECK` | done |
| §8 module | `ATMODULECHECK`, `ATROOMSIZECHECK` | done |
| everything for one dwelling | `ATDWELLINGCHECK <id>` | done |

Rooms are tagged with `ATROOMTYPE` (room type, dwelling, bedrooms, zone) first; doors with
`ATDOORTYPE` (the entrance).
