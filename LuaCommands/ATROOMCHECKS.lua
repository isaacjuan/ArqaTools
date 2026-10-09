-- ATROOMCHECKS: room checks that read real AutoCAD Architecture doors and
-- windows (at.getAecProps) and find them per room (at.entitiesInside).
-- Figures from the Quito building code, Ordenanza 3457 (2003), see
-- QUITO_SPACE_STANDARDS.md. Rooms are closed polylines or ACA spaces; tag
-- them with ATROOMTYPE first. Drawing units assumed mm.
--
--   ATDAYLIGHTCHECK   window area >= 20% of the room's floor area (Art. 69)
--   ATROOMDEPTHCHECK  room depth <= 5 x the window's smaller dimension (Art. 151)
--   ATDOORCHECK       door opening sizes (Art. 153)
--   ATDOORTYPE        tags a door as Entrance / Interior / Bathroom
--
-- Edit this file and run ATLUARELOAD, no rebuild needed.

local SOURCE = "[Quito Ord. 3457 (2003) -- check for later DMQ amendments]"

-- How far outside the room's face a door/window centre may sit and still
-- belong to the room: it sits in the wall, about half a wall thickness out.
local WALL_TOL = 300

local function fmt(v) return string.format("%.0f", v) end

-- Room floor area in m2: ACA's own calculation for spaces, else the curve's.
local function roomAreaM2(room)
    local aec = at.getAecProps(room)
    if aec and aec.kind == "space" and aec.area then return aec.area end
    local p = at.getProps(room)
    if p and p.area then return p.area / 1000000 end
    return nil
end

-- Room outline as a list of {x,y}: polyline vertices (arcs as chords), else
-- the bounding-box corners (exact for axis-aligned rectangular spaces).
local function roomOutline(room)
    local p = at.getProps(room)
    if not p then return nil end
    if p.vertices and p.closed and #p.vertices >= 3 then
        local pts = {}
        for _, v in ipairs(p.vertices) do pts[#pts + 1] = { x = v.x, y = v.y } end
        return pts
    end
    if p.min and p.max then
        return { { x = p.min.x, y = p.min.y }, { x = p.max.x, y = p.min.y },
                 { x = p.max.x, y = p.max.y }, { x = p.min.x, y = p.max.y } }
    end
    return nil
end

local function distToSegment(px, py, a, b)
    local dx, dy = b.x - a.x, b.y - a.y
    local len2 = dx * dx + dy * dy
    local t = len2 > 0 and ((px - a.x) * dx + (py - a.y) * dy) / len2 or 0
    t = math.max(0, math.min(1, t))
    local cx, cy = a.x + t * dx, a.y + t * dy
    return math.sqrt((px - cx) ^ 2 + (py - cy) ^ 2)
end

-- Windows of a room: ACA windows whose centre is inside it or in its walls.
local function roomWindows(room, tol)
    local list = {}
    for _, h in ipairs(at.entitiesInside(room, "AEC_WINDOW", tol) or {}) do
        local w = at.getAecProps(h)
        if w and w.width and w.height then list[#list + 1] = w end
    end
    return list
end

local ROOM_TYPES = { "Living", "Kitchen", "MainBedroom", "Bedroom2", "Bedroom3",
                     "Bathroom", "Laundry", "ServiceBedroom", "Corridor", "Hall", "Portal",
                     "Dining", "Garage", "Storage" }

-- Circulation and transition spaces: their only purpose is to connect the
-- useful spaces and ease the change between them (hall, corridor, portal =
-- porch / entrance vestibule)
-- (house rule). They need no daylight of their own (Art. 69: corridors may
-- be lit indirectly).
local CIRCULATION = { Corridor = true, Hall = true, Portal = true }

-- Room type: the ATROOMTYPE tag, else an ACA space whose name is a room type
-- ("Bathroom", "kitchen", ...; case-insensitive).
local function roomType(room)
    local rt = at.getData(room, "roomType")
    if rt then return rt end
    local aec = at.getAecProps(room)
    if aec and aec.kind == "space" and aec.name then
        local n = aec.name:lower()
        for _, t in ipairs(ROOM_TYPES) do
            if t:lower() == n then return t end
        end
    end
    return nil
end

local function roomLabel(room)
    local rt = roomType(room)
    return room .. (rt and (" (" .. rt .. ")") or "")
end

-- Every room the checks know about: tagged boundaries plus all ACA spaces.
local function allRooms()
    local seen, list = {}, {}
    for _, src in ipairs({ at.findByData("roomType"), at.entities("AEC_SPACE") }) do
        for _, h in ipairs(src) do
            if not seen[h] then seen[h] = true; list[#list + 1] = h end
        end
    end
    return list
end

-- Does the door belong to the room? Its extents centre within tol of the
-- room, or any extents corner inside it (a door swinging out of a room has
-- its centre outside, but its frame corners reach into the room).
local function doorTouches(room, door, tol)
    if at.pointInPolygon(room, door.center.x, door.center.y, tol) then return true end
    local p = at.getProps(door.handle)
    if not (p and p.min and p.max) then return false end
    -- corners within 25mm: a frame as deep as its wall has its corners on
    -- the room faces on both sides of the wall
    for _, c in ipairs({ { p.min.x, p.min.y }, { p.max.x, p.min.y }, { p.max.x, p.max.y }, { p.min.x, p.max.y } }) do
        if at.pointInPolygon(room, c[1], c[2], 25) then return true end
    end
    return false
end

-- ── ATDAYLIGHTCHECK ─────────────────────────────────────────────────────────
-- Art. 69: window area >= 20% of the room's useful floor area; the opening
-- part >= 30% of the window (not checked: ACA does not say how much of a
-- window opens). Window area = ACA width x height (the opening size as the
-- window style measures it). Bathrooms are exempt: they may use ducts or
-- mechanical ventilation instead (Art. 69, 147).
at.defineCommand("ATDAYLIGHTCHECK", function(p)
    local ratio = p.ratio or 0.20
    local pass, fail = 0, 0
    for _, room in ipairs(p.rooms) do
        local area = roomAreaM2(room)
        local rt = roomType(room)
        if not area then
            print("ATDAYLIGHTCHECK: " .. room .. " skipped: no floor area.")
        elseif rt == "Bathroom" then
            print("ATDAYLIGHTCHECK: " .. roomLabel(room) .. ": exempt (bathrooms may use a duct or mechanical ventilation).")
        elseif CIRCULATION[rt] then
            print("ATDAYLIGHTCHECK: " .. roomLabel(room) .. ": exempt (circulation may be lit indirectly, Art. 69).")
        else
            local wins, glazed, parts = roomWindows(room, p.tol or WALL_TOL), 0, {}
            for _, w in ipairs(wins) do
                local a = w.width * w.height / 1000000
                glazed = glazed + a
                parts[#parts + 1] = string.format("%s %sx%s=%.2fm2", w.handle, fmt(w.width), fmt(w.height), a)
            end
            local required = area * ratio
            local ok = glazed + 1e-9 >= required
            if ok then pass = pass + 1 else fail = fail + 1 end
            print(string.format("ATDAYLIGHTCHECK: %s: floor %.2fm2 -> needs %.2fm2 of window (%.0f%%); has %.2fm2 [%s] -> %s",
                roomLabel(room), area, required, ratio * 100, glazed,
                #parts > 0 and table.concat(parts, ", ") or "no windows found", ok and "ok" or "BELOW"))
        end
    end
    print(string.format("ATDAYLIGHTCHECK: %d ok, %d below. Opening part (30%% of window) not checked. %s",
        pass, fail, SOURCE))
end, "Checks each room's ACA window area against its floor area (Quito Art. 69: 20%; bathrooms exempt)", {
    { name = "rooms", type = "selection", prompt = "Select rooms (closed polylines or spaces)",
      filter = "AEC_SPACE,LWPOLYLINE" },
    { name = "ratio", type = "number", prompt = "Window area / floor area", default = 0.20 },
    { name = "tol",   type = "distance", prompt = "Max. distance of a window centre outside the room (wall)",
      default = WALL_TOL },
})

-- ── ATROOMDEPTHCHECK ────────────────────────────────────────────────────────
-- Art. 151: a room's depth may be at most 5 x the window's smaller dimension.
-- Depth is measured from the room edge the window sits on (the nearest edge)
-- to the farthest point of the room, perpendicular to that edge. With several
-- windows the room passes when one of them satisfies the rule (assumption:
-- the article counts integrated rooms "from each of their windows"). Deeper
-- rooms may compensate with skylights or high windows; that is not checked.
at.defineCommand("ATROOMDEPTHCHECK", function(p)
    local pass, fail = 0, 0
    for _, room in ipairs(p.rooms) do
        local outline = roomOutline(room)
        local wins = roomWindows(room, p.tol or WALL_TOL)
        if not outline then
            print("ATROOMDEPTHCHECK: " .. room .. " skipped: no outline.")
        elseif #wins == 0 and roomType(room) == "Bathroom" then
            print("ATROOMDEPTHCHECK: " .. roomLabel(room) .. ": no windows, exempt (bathrooms may use a duct).")
        elseif #wins == 0 and CIRCULATION[roomType(room)] then
            print("ATROOMDEPTHCHECK: " .. roomLabel(room) .. ": no windows, exempt (circulation, Art. 69).")
        elseif #wins == 0 then
            fail = fail + 1
            print("ATROOMDEPTHCHECK: " .. roomLabel(room) .. ": no windows found -> FAILS (needs a window, Art. 69)")
        else
            local best, parts = false, {}
            for _, w in ipairs(wins) do
                local cx, cy = w.center.x, w.center.y
                -- the window's edge: nearest outline segment to its centre
                local ei, ed = 1, math.huge
                for i = 1, #outline do
                    local d = distToSegment(cx, cy, outline[i], outline[i % #outline + 1])
                    if d < ed then ei, ed = i, d end
                end
                local a, b = outline[ei], outline[ei % #outline + 1]
                local ux, uy = b.x - a.x, b.y - a.y
                local len = math.sqrt(ux * ux + uy * uy)
                local depth = 0
                for _, v in ipairs(outline) do
                    local d = math.abs((v.x - a.x) * uy - (v.y - a.y) * ux) / len
                    if d > depth then depth = d end
                end
                local small = math.min(w.width, w.height)
                local allowed = 5 * small
                local ok = depth <= allowed + 1e-6
                if ok then best = true end
                parts[#parts + 1] = string.format("%s: depth %s vs 5 x %s = %s -> %s",
                    w.handle, fmt(depth), fmt(small), fmt(allowed), ok and "ok" or "TOO DEEP")
            end
            if best then pass = pass + 1 else fail = fail + 1 end
            print(string.format("ATROOMDEPTHCHECK: %s: %s => room %s", roomLabel(room),
                table.concat(parts, "; "), best and "ok" or "TOO DEEP"))
        end
    end
    print(string.format("ATROOMDEPTHCHECK: %d ok, %d fail (skylights / high windows not considered). %s",
        pass, fail, SOURCE))
end, "Checks room depth against 5 x the smaller window dimension (Quito Art. 151)", {
    { name = "rooms", type = "selection", prompt = "Select rooms (closed polylines or spaces)",
      filter = "AEC_SPACE,LWPOLYLINE" },
    { name = "tol",   type = "distance", prompt = "Max. distance of a window centre outside the room (wall)",
      default = WALL_TOL },
})

-- ── ATDOORCHECK ─────────────────────────────────────────────────────────────
-- Art. 153 minimum door openings (vano): entrance 0.96 x 2.03 m, interior
-- 0.86 x 2.03 m, bathroom 0.76 x 2.03 m. The door's type comes from its
-- ATDOORTYPE tag, else: a door touching a Bathroom-tagged room is a bathroom
-- door, any other door is interior. Sizes are ACA's width/height, measured as
-- the door style says (measureTo); the code means the rough opening.
local DOOR_MIN = {
    Entrance = { w = 960, h = 2030 },
    Interior = { w = 860, h = 2030 },
    Bathroom = { w = 760, h = 2030 },
}

at.defineCommand("ATDOORCHECK", function(p)
    local doors = p.doors
    if not doors or #doors == 0 then doors = at.entities("AEC_DOOR") end
    local rooms = allRooms()
    local tol = p.tol or WALL_TOL
    local pass, fail = 0, 0
    for _, h in ipairs(doors) do
        local d = at.getAecProps(h)
        if not d or d.kind ~= "door" or not d.width then
            print("ATDOORCHECK: " .. h .. " skipped: not an ACA door.")
        else
            local kind, why = at.getData(h, "doorType"), "tag"
            local touching = {}
            for _, r in ipairs(rooms) do
                if doorTouches(r, d, tol) then
                    touching[#touching + 1] = roomLabel(r)
                    if not kind and roomType(r) == "Bathroom" then kind, why = "Bathroom", "opens to a bathroom" end
                end
            end
            if not kind then kind, why = "Interior", "default" end
            local min = DOOR_MIN[kind]
            if not min then
                print(string.format("ATDOORCHECK: %s: unknown doorType '%s' (Entrance, Interior or Bathroom).", h, kind))
            else
                local wOk, hOk = d.width >= min.w - 0.5, d.height >= min.h - 0.5
                if wOk and hOk then pass = pass + 1 else fail = fail + 1 end
                print(string.format("ATDOORCHECK: %s %s door (%s)%s: %sx%s vs min %sx%s -> width %s, height %s",
                    h, kind, why, #touching > 0 and (", rooms " .. table.concat(touching, ", ")) or "",
                    fmt(d.width), fmt(d.height), fmt(min.w), fmt(min.h),
                    wOk and "ok" or "BELOW", hOk and "ok" or "BELOW"))
            end
        end
    end
    print(string.format("ATDOORCHECK: %d ok, %d below. Sizes as ACA measures them (style measureTo). %s",
        pass, fail, SOURCE))
end, "Checks ACA door sizes against Quito minimums (Art. 153: entrance 960, interior 860, bathroom 760, height 2030)", {
    { name = "doors", type = "selection", prompt = "Select doors <all>", filter = "AEC_DOOR", optional = true },
    { name = "tol",   type = "distance", prompt = "Max. distance of a door centre outside a room (wall)",
      default = WALL_TOL },
})

-- ── ATPASSAGECHECK ──────────────────────────────────────────────────────────
-- Art. 147: no bedroom or bathroom may be the obligatory passage to another
-- room; with more than one bedroom and a single bathroom, the bathroom must
-- open onto a room that is not a bedroom.
--
-- Rooms are linked by the ACA doors and openings that touch them. A door that
-- touches only one room links it to "outside" (the exterior, or a space that
-- is not modelled as a room), and so does a door tagged Entrance. Model every
-- room of the dwelling, or an unmodelled space counts as outside and can hide
-- a problem. Open connections without a door or opening object are not seen.
--
-- En-suite bathroom (project interpretation of Art. 147): a bathroom whose
-- only door leads into one bedroom is that bedroom's private bathroom and is
-- allowed. It does not count as the dwelling's shared bathroom (rule 2), and
-- a bathroom between two bedrooms is not private.
--
-- Transition principle (house rule): people need time to perceive a new
-- environment, so important changes of space go through transition spaces
-- (hall, portal, corridor): useful spaces connect only through circulation,
-- and the dwelling's entrance (doors tagged Entrance) opens into a hall or
-- portal, never straight into a useful space.
local BEDROOM_TYPES = { MainBedroom = true, Bedroom2 = true, Bedroom3 = true, ServiceBedroom = true }
local OUTSIDE = "outside"

-- The rooms of a dwelling (all rooms when id is nil or "").
local function dwellingRooms(id)
    local rooms = {}
    for _, r in ipairs(allRooms()) do
        if not id or id == "" or at.getData(r, "dwelling") == id then rooms[#rooms + 1] = r end
    end
    return rooms
end

-- Zones (DESIGN_PRINCIPLES.md §2), used by ATZONECHECK and the access rules.
local ZONE_OF = {
    Hall = "public", Portal = "public", Living = "public", Dining = "public",
    MainBedroom = "private", Bedroom2 = "private", Bedroom3 = "private", Bathroom = "private",
    Kitchen = "service", Laundry = "service", Garage = "service", Storage = "service",
    ServiceBedroom = "service",
    Corridor = "circulation",
}

local function zoneOf(room)
    local z = at.getData(room, "zone")
    if type(z) == "string" and z ~= "" then return z:lower() end
    return ZONE_OF[roomType(room) or ""]
end

-- ── Light boundaries (DESIGN_PRINCIPLES.md §3) ──────────────────────────────
-- A space can be defined by a line, a change of floor or level, a curtain, a
-- glass wall or a heavy wall. Two rooms whose outlines share an edge (gap
-- <= BOUNDARY_GAP, shared length >= BOUNDARY_MIN) with no ACA wall along it
-- are divided by a light boundary. Its type comes from a line / polyline drawn
-- along it and tagged by ATBOUNDARYTYPE; untagged it is "open".
-- Walkable types connect the rooms for access and zoning; glass and wall do
-- not (glass still joins them visually).
local BOUNDARY_GAP, BOUNDARY_MIN = 25, 600
local WALKABLE = { open = true, line = true, floor = true, level = true, curtain = true }

local function pointInPts(px, py, pts)
    local inside, j = false, #pts
    for i = 1, #pts do
        local a, b = pts[i], pts[j]
        if (a.y > py) ~= (b.y > py) and px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x then
            inside = not inside
        end
        j = i
    end
    return inside
end

-- Distance from a point to an open or closed chain of points.
local function chainDist(px, py, pts, closed)
    local best = math.huge
    local n = closed and #pts or #pts - 1
    for i = 1, n do
        local d = distToSegment(px, py, pts[i], pts[i % #pts + 1])
        if d < best then best = d end
    end
    return best
end

-- The longest stretch where two closed outlines run along each other within
-- BOUNDARY_GAP: { a = {x,y}, b = {x,y}, length } or nil.
local function sharedEdge(pa, pb)
    local best
    for i = 1, #pa do
        local a1, a2 = pa[i], pa[i % #pa + 1]
        local ux, uy = a2.x - a1.x, a2.y - a1.y
        local la = math.sqrt(ux * ux + uy * uy)
        if la > 1 then
            ux, uy = ux / la, uy / la
            for j = 1, #pb do
                local b1, b2 = pb[j], pb[j % #pb + 1]
                local vx, vy = b2.x - b1.x, b2.y - b1.y
                local lb = math.sqrt(vx * vx + vy * vy)
                if lb > 1 and math.abs(ux * vy - uy * vx) / lb < 1e-3 then
                    local gap = math.abs((b1.x - a1.x) * uy - (b1.y - a1.y) * ux)
                    if gap <= BOUNDARY_GAP then
                        local t1 = (b1.x - a1.x) * ux + (b1.y - a1.y) * uy
                        local t2 = (b2.x - a1.x) * ux + (b2.y - a1.y) * uy
                        if t1 > t2 then t1, t2 = t2, t1 end
                        local lo, hi = math.max(0, t1), math.min(la, t2)
                        if hi - lo >= BOUNDARY_MIN and (not best or hi - lo > best.length) then
                            best = { a = { x = a1.x + ux * lo, y = a1.y + uy * lo },
                                     b = { x = a1.x + ux * hi, y = a1.y + uy * hi }, length = hi - lo }
                        end
                    end
                end
            end
        end
    end
    return best
end

-- Every light boundary between the given rooms:
-- { r1, r2, a, b, length, type, marker, walkable }.
local function lightBoundaries(rooms)
    local outlines = {}
    for _, r in ipairs(rooms) do
        local pts, closed = at.outline(r)
        if pts and closed and #pts >= 3 then outlines[r] = pts end
    end
    -- walls (and curtain walls) that could stand on a shared edge
    local walls = {}
    for _, t in ipairs({ "AEC_WALL", "AecDbCurtainWall" }) do
        for _, h in ipairs(at.entities(t)) do
            local pts = at.outline(h)
            if pts and #pts >= 3 then walls[#walls + 1] = { h = h, pts = pts, glass = t ~= "AEC_WALL" } end
        end
    end
    -- boundary markers tagged by ATBOUNDARYTYPE
    local markers = {}
    for _, h in ipairs(at.findByData("boundary")) do
        local pts, closed = at.outline(h)
        if pts and #pts >= 2 then
            markers[#markers + 1] = { h = h, pts = pts, closed = closed, type = tostring(at.getData(h, "boundary")):lower() }
        end
    end

    local out = {}
    for i = 1, #rooms do
        for j = i + 1, #rooms do
            local r1, r2 = rooms[i], rooms[j]
            if outlines[r1] and outlines[r2] then
                local e = sharedEdge(outlines[r1], outlines[r2])
                if e then
                    local mx, my = (e.a.x + e.b.x) / 2, (e.a.y + e.b.y) / 2
                    local kind, marker = "open", nil
                    for _, w in ipairs(walls) do
                        if pointInPts(mx, my, w.pts) or chainDist(mx, my, w.pts, true) <= BOUNDARY_GAP then
                            kind, marker = w.glass and "glass" or "wall", w.h
                            break
                        end
                    end
                    if kind == "open" then
                        for _, m in ipairs(markers) do
                            if chainDist(mx, my, m.pts, m.closed) <= BOUNDARY_GAP then
                                kind, marker = m.type, m.h
                                break
                            end
                        end
                    end
                    -- a real ACA wall between rooms drawn to its centre line is not a light boundary
                    if not (kind == "wall" and marker and not at.getData(marker, "boundary")) then
                        out[#out + 1] = { r1 = r1, r2 = r2, a = e.a, b = e.b, length = e.length,
                                          type = kind, marker = marker, walkable = WALKABLE[kind] == true }
                    end
                end
            end
        end
    end
    return out
end

-- Connection graph of rooms through ACA doors and openings: adj[a][b] = true,
-- via["a>b"] = door. A door touching one room, or tagged Entrance, links to
-- OUTSIDE. Also returns the entrance doors and the one-room (exterior) doors.
-- Walkable light boundaries link rooms too (via = "open <type> boundary").
local function buildGraph(rooms, tol)
    local g = { adj = {}, via = {}, entrances = {}, exterior = {}, used = 0, boundaries = {} }
    local function link(a, b, door)
        g.adj[a] = g.adj[a] or {}; g.adj[b] = g.adj[b] or {}
        g.adj[a][b] = true; g.adj[b][a] = true
        g.via[a .. ">" .. b] = door; g.via[b .. ">" .. a] = door
    end
    for _, lb in ipairs(lightBoundaries(rooms)) do
        g.boundaries[#g.boundaries + 1] = lb
        if lb.walkable then link(lb.r1, lb.r2, lb.type .. " boundary") end
    end
    local doors = at.entities("AEC_DOOR")
    for _, h in ipairs(at.entities("AecDbOpening")) do doors[#doors + 1] = h end
    for _, h in ipairs(doors) do
        local d = at.getAecProps(h)
        if d and d.center then
            local touch = {}
            for _, r in ipairs(rooms) do
                if doorTouches(r, d, tol) then touch[#touch + 1] = r end
            end
            if #touch > 0 then g.used = g.used + 1 end
            local isEntrance = at.getData(h, "doorType") == "Entrance"
            if isEntrance and #touch > 0 then g.entrances[#g.entrances + 1] = { door = h, rooms = touch }
            elseif #touch == 1 then g.exterior[#g.exterior + 1] = { door = h, room = touch[1] } end
            if #touch == 1 or isEntrance then
                for _, r in ipairs(touch) do link(r, OUTSIDE, h) end
            end
            for i = 1, #touch do
                for j = i + 1, #touch do link(touch[i], touch[j], h) end
            end
        end
    end
    return g
end

at.defineCommand("ATPASSAGECHECK", function(p)
    local tol = p.tol or WALL_TOL
    local rooms = dwellingRooms(p.dwelling)
    if #rooms == 0 then
        print("ATPASSAGECHECK: no rooms found" .. (p.dwelling and (" for dwelling " .. p.dwelling) or "")
            .. " (tag them with ATROOMTYPE).")
        return
    end

    -- connection graph
    local g = buildGraph(rooms, tol)
    local adj, via, entrances, exterior, used = g.adj, g.via, g.entrances, g.exterior, g.used

    local function reach(skip)
        local seen, queue = { [OUTSIDE] = true }, { OUTSIDE }
        while #queue > 0 do
            local n = table.remove(queue)
            for m in pairs(adj[n] or {}) do
                if m ~= skip and not seen[m] then seen[m] = true; queue[#queue + 1] = m end
            end
        end
        return seen
    end
    local function name(n) return n == OUTSIDE and OUTSIDE or roomLabel(n) end

    -- connections, for the record
    for _, r in ipairs(rooms) do
        local parts = {}
        for m in pairs(adj[r] or {}) do parts[#parts + 1] = name(m) .. " via " .. via[r .. ">" .. m] end
        table.sort(parts)
        print(string.format("ATPASSAGECHECK: %s -> %s", roomLabel(r),
            #parts > 0 and table.concat(parts, ", ") or "no doors"))
    end

    local problems = 0
    local base = reach(nil)
    for _, r in ipairs(rooms) do
        if not base[r] then
            problems = problems + 1
            print("  PROBLEM: " .. roomLabel(r) .. " cannot be reached from outside (no door found).")
        end
    end

    -- en-suite bathrooms: the only connection is one bedroom
    local ensuite = {}
    for _, b in ipairs(rooms) do
        if roomType(b) == "Bathroom" then
            local only, n = nil, 0
            for m in pairs(adj[b] or {}) do only = m; n = n + 1 end
            if n == 1 and only ~= OUTSIDE and BEDROOM_TYPES[roomType(only)] then
                ensuite[b] = only
                print(string.format("  ok: %s is the en-suite bathroom of %s (exclusive use).",
                    roomLabel(b), roomLabel(only)))
            end
        end
    end

    -- rule 1: bedrooms and bathrooms are no obligatory passage
    local beds, baths = 0, {}
    for _, x in ipairs(rooms) do
        local t = roomType(x)
        if BEDROOM_TYPES[t] then beds = beds + 1 end
        if t == "Bathroom" and not ensuite[x] then baths[#baths + 1] = x end
        if BEDROOM_TYPES[t] or t == "Bathroom" then
            local without = reach(x)
            for _, y in ipairs(rooms) do
                if y ~= x and base[y] and not without[y] and ensuite[y] ~= x then
                    problems = problems + 1
                    print(string.format("  PROBLEM: %s is the only way into %s (Art. 147).", roomLabel(x), roomLabel(y)))
                end
            end
        end
    end

    -- rule 2: one shared bathroom for several bedrooms opens onto a
    -- non-bedroom; there must be a shared bathroom at all
    if beds > 1 and #baths == 0 and next(ensuite) then
        problems = problems + 1
        print(string.format("  PROBLEM: %d bedrooms but only en-suite bathroom(s); the others have no bathroom (Art. 147).", beds))
    elseif beds > 1 and #baths == 1 then
        local b, ok = baths[1], false
        for m in pairs(adj[b] or {}) do
            if m == OUTSIDE or not BEDROOM_TYPES[roomType(m)] then ok = true end
        end
        if not ok then
            problems = problems + 1
            print(string.format("  PROBLEM: %s is the only bathroom for %d bedrooms and opens only onto bedrooms (Art. 147).",
                roomLabel(b), beds))
        else
            print(string.format("  ok: the single bathroom %s opens onto a room that is not a bedroom.", roomLabel(b)))
        end
    end

    -- house rule: every useful space is reached from a circulation space
    -- (hall, corridor) or from outside, never only through another useful
    -- space. Circulation reachable from outside through circulation only:
    local circ, queue = { [OUTSIDE] = true }, { OUTSIDE }
    while #queue > 0 do
        local n = table.remove(queue)
        for m in pairs(adj[n] or {}) do
            if not circ[m] and CIRCULATION[roomType(m)] then circ[m] = true; queue[#queue + 1] = m end
        end
    end
    -- Rooms of one zone joined by a walkable light boundary (a line, a floor or
    -- level change, a curtain) are one space articulated lightly, not two
    -- spaces reached through each other (DESIGN_PRINCIPLES.md §3).
    local parent = {}
    local function find(x)
        while parent[x] do x = parent[x] end
        return x
    end
    local lightPair = {}
    for _, lb in ipairs(g.boundaries) do
        local z1, z2 = zoneOf(lb.r1), zoneOf(lb.r2)
        if lb.walkable and z1 and z1 == z2 then
            local a, b = find(lb.r1), find(lb.r2)
            if a ~= b then parent[a] = b end
            lightPair[lb.r1 .. "|" .. lb.r2] = true; lightPair[lb.r2 .. "|" .. lb.r1] = true
        end
    end
    local spaceOk = {}
    for _, r in ipairs(rooms) do
        for m in pairs(adj[r] or {}) do
            if circ[m] then spaceOk[find(r)] = true end
        end
    end

    local noted = {}
    for _, r in ipairs(rooms) do
        if not CIRCULATION[roomType(r)] and not ensuite[r] then
            local ok, through = spaceOk[find(r)] == true, {}
            for m in pairs(adj[r] or {}) do
                if circ[m] then ok = true
                elseif ensuite[m] ~= r then through[#through + 1] = name(m) end   -- not its own en-suite
                -- a direct door between two useful spaces: fine only as an extra
                if m ~= OUTSIDE and not CIRCULATION[roomType(m)] and not ensuite[m]
                        and not lightPair[r .. "|" .. m] then
                    local key = r < m and (r .. "|" .. m) or (m .. "|" .. r)
                    if not noted[key] then
                        noted[key] = true
                        print(string.format("  note: direct connection (%s) between useful spaces %s and %s.",
                            via[r .. ">" .. m], roomLabel(r), roomLabel(m)))
                    end
                end
            end
            if not ok and base[r] then
                problems = problems + 1
                table.sort(through)
                print(string.format("  PROBLEM: %s is reached only through %s; connect it to a hall or corridor (house rule).",
                    roomLabel(r), table.concat(through, ", ")))
            end
        end
    end

    -- transition: the entrance opens into a hall or portal
    if #entrances == 0 then
        print("  check: no door tagged Entrance (ATDOORTYPE); the entrance transition is not verified.")
    end
    for _, e in ipairs(entrances) do
        local into = {}
        for _, r in ipairs(e.rooms) do
            if not CIRCULATION[roomType(r)] then into[#into + 1] = roomLabel(r) end
        end
        if #into > 0 then
            problems = problems + 1
            print(string.format("  PROBLEM: entrance %s opens straight into %s; enter through a hall or portal (house rule).",
                e.door, table.concat(into, ", ")))
        else
            print(string.format("  ok: entrance %s opens into a transition space.", e.door))
        end
    end
    for _, e in ipairs(exterior) do
        if not CIRCULATION[roomType(e.room)] then
            print(string.format("  note: door %s leads from %s to outside or an unmodelled space (balcony, patio?).",
                e.door, roomLabel(e.room)))
        end
    end

    print(string.format("ATPASSAGECHECK: %d room(s), %d door(s)/opening(s) used, %d problem(s). %s",
        #rooms, used, problems, SOURCE))
end, "Checks room access: no bedroom or bathroom is the only way into another room, a single bathroom for several bedrooms opens onto a non-bedroom (Quito Art. 147), and every useful space is reached from a hall, corridor or outside, never only through another useful space (house rule)", {
    { name = "dwelling", type = "string", prompt = "Dwelling id <all rooms>", optional = true,
      description = "Only rooms tagged with this dwelling id (ATROOMTYPE); omit for every room in the drawing" },
    { name = "tol",      type = "distance", prompt = "Max. distance of a door centre outside a room (wall)",
      default = WALL_TOL },
})

-- ── ATCIRCULATIONCHECK ──────────────────────────────────────────────────────
-- Art. 160: corridors inside a dwelling at least 0.90m clear; shared
-- (communal) corridors in multi-family buildings 1.20m (pass minWidth=1200).
-- Applies to every circulation room (Corridor, Hall, Portal).
--
-- Clear width = the narrowest of: the room's own width (the closest pair of
-- parallel facing edges of its outline, so an L-shaped corridor gives each
-- arm), and the passage beside each obstacle inside it (blocks / ACA
-- multi-view blocks by default): the wider of its two gaps to the facing
-- edges, since people pass on the free side. Door leaves swinging into the
-- corridor are transient and not counted.

local function segPointDist(px, py, a, b)
    return distToSegment(px, py, a, b)
end

local function cross(o, a, b) return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x) end

-- Distance between segments a1-a2 and b1-b2 (0 when they cross).
local function segSeg(a1, a2, b1, b2)
    local d1, d2 = cross(a1, a2, b1), cross(a1, a2, b2)
    local d3, d4 = cross(b1, b2, a1), cross(b1, b2, a2)
    if ((d1 > 0 and d2 < 0) or (d1 < 0 and d2 > 0)) and ((d3 > 0 and d4 < 0) or (d3 < 0 and d4 > 0)) then
        return 0
    end
    return math.min(segPointDist(a1.x, a1.y, b1, b2), segPointDist(a2.x, a2.y, b1, b2),
                    segPointDist(b1.x, b1.y, a1, a2), segPointDist(b2.x, b2.y, a1, a2))
end

-- Distance from segment a-b to a closed outline's edges.
local function segOutline(a, b, pts)
    local best = math.huge
    for i = 1, #pts do
        local d = segSeg(a, b, pts[i], pts[i % #pts + 1])
        if d < best then best = d end
    end
    return best
end

-- Pairs of parallel edges of a closed outline that face each other (their
-- projections overlap): {i, j, gap, ux, uy (unit direction of edge i)}.
local function facingPairs(pts)
    local n, pairs_ = #pts, {}
    for i = 1, n do
        local a, b = pts[i], pts[i % n + 1]
        local ux, uy = b.x - a.x, b.y - a.y
        local li = math.sqrt(ux * ux + uy * uy)
        if li > 1 then
            ux, uy = ux / li, uy / li
            for j = i + 1, n do
                local c, d = pts[j], pts[j % n + 1]
                local vx, vy = d.x - c.x, d.y - c.y
                local lj = math.sqrt(vx * vx + vy * vy)
                local adjacent = (j == i + 1) or (i == 1 and j == n)
                if lj > 1 and not adjacent and math.abs(ux * vy - uy * vx) / lj < 1e-3 then
                    -- overlap of the two edges along edge i's direction
                    local s0, s1 = 0, li
                    local t0 = (c.x - a.x) * ux + (c.y - a.y) * uy
                    local t1 = (d.x - a.x) * ux + (d.y - a.y) * uy
                    if t0 > t1 then t0, t1 = t1, t0 end
                    local lo, hi = math.max(s0, t0), math.min(s1, t1)
                    if hi - lo > 1 then
                        local gap = math.abs((c.x - a.x) * uy - (c.y - a.y) * ux)
                        pairs_[#pairs_ + 1] = { i = i, j = j, gap = gap, ux = ux, uy = uy,
                                                lo = lo, hi = hi, ax = a.x, ay = a.y }
                    end
                end
            end
        end
    end
    return pairs_
end

local function splitTypes(s)
    local out = {}
    for t in tostring(s or ""):gmatch("[^,%s]+") do out[#out + 1] = t end
    return out
end

at.defineCommand("ATCIRCULATIONCHECK", function(p)
    local minW = p.minWidth or 900
    local rooms = p.rooms
    if not rooms or #rooms == 0 then
        rooms = {}
        for _, r in ipairs(allRooms()) do
            if CIRCULATION[roomType(r)] and (not p.dwelling or p.dwelling == ""
                    or at.getData(r, "dwelling") == p.dwelling) then
                rooms[#rooms + 1] = r
            end
        end
    end
    if #rooms == 0 then
        print("ATCIRCULATIONCHECK: no circulation rooms found (tag them Corridor, Hall or Portal with ATROOMTYPE).")
        return
    end
    local types = splitTypes(p.obstacles or "INSERT,AEC_MVBLOCK_REF")

    local pass, fail = 0, 0
    for _, r in ipairs(rooms) do
        local pts, closed = at.outline(r)
        if not pts or not closed or #pts < 3 then
            print("ATCIRCULATIONCHECK: " .. r .. " skipped: not a closed room outline.")
        else
            local fp = facingPairs(pts)
            local width = math.huge
            for _, f in ipairs(fp) do if f.gap < width then width = f.gap end end
            local clear, limiting = width, "room width"
            local notes = {}
            for _, t in ipairs(types) do
                for _, o in ipairs(at.entitiesInside(r, t) or {}) do
                    local op = at.outline(o)
                    if op and #op >= 2 then
                        -- the facing pair the obstacle stands between
                        local cx, cy = 0, 0
                        for _, v in ipairs(op) do cx = cx + v.x; cy = cy + v.y end
                        cx, cy = cx / #op, cy / #op
                        local best
                        for _, f in ipairs(fp) do
                            local s = (cx - f.ax) * f.ux + (cy - f.ay) * f.uy
                            if s >= f.lo and s <= f.hi and (not best or f.gap < best.gap) then best = f end
                        end
                        if best then
                            local g1 = segOutline(pts[best.i], pts[best.i % #pts + 1], op)
                            local g2 = segOutline(pts[best.j], pts[best.j % #pts + 1], op)
                            local passage = math.max(g1, g2)
                            notes[#notes + 1] = string.format("%s leaves %s", o, fmt(passage))
                            if passage < clear then clear, limiting = passage, "beside " .. o end
                        end
                    end
                end
            end
            local ok = clear + 0.5 >= minW
            if ok then pass = pass + 1 else fail = fail + 1 end
            print(string.format("ATCIRCULATIONCHECK: %s: room width %s%s; clear %s (%s) vs min %s -> %s",
                roomLabel(r), width == math.huge and "?" or fmt(width),
                #notes > 0 and (", obstacles: " .. table.concat(notes, ", ")) or "",
                clear == math.huge and "?" or fmt(clear), limiting, fmt(minW), ok and "ok" or "BELOW"))
        end
    end
    print(string.format("ATCIRCULATIONCHECK: %d ok, %d below (min %s mm; shared corridors 1200, Art. 160). %s",
        pass, fail, fmt(minW), SOURCE))
end, "Checks the clear width of circulation rooms (Corridor, Hall, Portal) including obstacles inside them (Quito Art. 160: 900 mm, shared 1200 mm)", {
    { name = "rooms",     type = "selection", prompt = "Select circulation rooms <all tagged>",
      filter = "AEC_SPACE,LWPOLYLINE", optional = true },
    { name = "dwelling",  type = "string", prompt = "Dwelling id <all>", optional = true,
      description = "When no rooms are selected: only this dwelling's circulation rooms" },
    { name = "minWidth",  type = "distance", prompt = "Minimum clear width", default = 900,
      description = "900 inside a dwelling, 1200 for shared corridors in multi-family buildings" },
    { name = "obstacles", type = "string", prompt = "Obstacle types", default = "INSERT,AEC_MVBLOCK_REF",
      description = "Comma-separated entity types counted as obstacles inside the room" },
})

-- ── ATDOORCLEARCHECK ────────────────────────────────────────────────────────
-- House rule: a door is never obstructed. In front of every door, on both
-- sides of its wall, a clear zone as wide as the opening and `depth` deep
-- (default 900mm: the approach space, and the swing of a 900 leaf) must be
-- free of obstacles (blocks / ACA multi-view blocks by default).
--
-- The opening is centred on the door's extents centre projected onto its
-- host wall (the wall whose baseline is nearest); the zone runs along that
-- wall's direction, so it is right for walls in any direction.

local function inPoly(px, py, pts)
    local inside, n = false, #pts
    local j = n
    for i = 1, n do
        local a, b = pts[i], pts[j]
        if (a.y > py) ~= (b.y > py) and px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x then
            inside = not inside
        end
        j = i
    end
    return inside
end

local function properCross(a1, a2, b1, b2)
    local d1, d2 = cross(a1, a2, b1), cross(a1, a2, b2)
    local d3, d4 = cross(b1, b2, a1), cross(b1, b2, a2)
    return ((d1 > 0 and d2 < 0) or (d1 < 0 and d2 > 0)) and ((d3 > 0 and d4 < 0) or (d3 < 0 and d4 > 0))
end

-- Do two closed outlines share interior? (Touching edges do not count.)
local function overlaps(pa, pb)
    for i = 1, #pa do
        for j = 1, #pb do
            if properCross(pa[i], pa[i % #pa + 1], pb[j], pb[j % #pb + 1]) then return true end
        end
    end
    local function anyInside(p, q)
        for _, v in ipairs(p) do
            if inPoly(v.x, v.y, q) and segOutline(v, v, q) > 1 then return true end
        end
        -- also the centre (one fully inside the other with no vertex strictly inside is rare)
        local cx, cy = 0, 0
        for _, v in ipairs(p) do cx = cx + v.x; cy = cy + v.y end
        cx, cy = cx / #p, cy / #p
        return inPoly(cx, cy, q) and segOutline({ x = cx, y = cy }, { x = cx, y = cy }, q) > 1
    end
    return anyInside(pa, pb) or anyInside(pb, pa)
end

-- The door's host wall: the ACA wall whose baseline is nearest its centre.
local function hostWall(door)
    local best, bestD
    for _, h in ipairs(at.entities("AEC_WALL")) do
        local w = at.getAecProps(h)
        if w and w.startPoint and w.endPoint then
            local d = distToSegment(door.center.x, door.center.y, w.startPoint, w.endPoint)
            if not bestD or d < bestD then best, bestD = w, d end
        end
    end
    if best and bestD <= (best.width or 0) + door.width then return best end
    return nil
end

-- Clear zone in front of a door, both sides of the wall, as a closed outline.
local function doorZone(door, depth)
    local w = hostWall(door)
    if not w then return nil, "no host wall found" end
    local a, b = w.startPoint, w.endPoint
    local ux, uy = b.x - a.x, b.y - a.y
    local len = math.sqrt(ux * ux + uy * uy)
    if len < 1 then return nil, "host wall has no length" end
    ux, uy = ux / len, uy / len
    local nx, ny = -uy, ux
    local t = (door.center.x - a.x) * ux + (door.center.y - a.y) * uy
    local ox, oy = a.x + ux * t, a.y + uy * t
    local half = door.width / 2 - 2                -- 2mm in: an obstacle beside the opening only touches
    local reach = (w.justify == "Center" and (w.width or 0) / 2 or (w.width or 0)) + depth
    return {
        { x = ox - ux * half - nx * reach, y = oy - uy * half - ny * reach },
        { x = ox + ux * half - nx * reach, y = oy + uy * half - ny * reach },
        { x = ox + ux * half + nx * reach, y = oy + uy * half + ny * reach },
        { x = ox - ux * half + nx * reach, y = oy - uy * half + ny * reach },
    }, w
end

at.defineCommand("ATDOORCLEARCHECK", function(p)
    local depth = p.depth or 900
    local doors = p.doors
    if not doors or #doors == 0 then
        doors = {}
        local rooms
        if p.dwelling and p.dwelling ~= "" then rooms = at.findByData("dwelling", p.dwelling) end
        for _, h in ipairs(at.entities("AEC_DOOR")) do
            local keep = not rooms
            if rooms then
                local d = at.getAecProps(h)
                for _, r in ipairs(rooms) do
                    if d and d.center and doorTouches(r, d, WALL_TOL) then keep = true; break end
                end
            end
            if keep then doors[#doors + 1] = h end
        end
    end
    local obstacles = {}
    for _, t in ipairs(splitTypes(p.obstacles or "INSERT,AEC_MVBLOCK_REF")) do
        for _, h in ipairs(at.entities(t)) do
            local pts, closed = at.outline(h)
            if pts and #pts >= 3 then obstacles[#obstacles + 1] = { h = h, pts = pts } end
        end
    end

    local pass, fail = 0, 0
    for _, h in ipairs(doors) do
        local d = at.getAecProps(h)
        if not d or d.kind ~= "door" or not d.width then
            print("ATDOORCLEARCHECK: " .. h .. " skipped: not an ACA door.")
        else
            local zone, w = doorZone(d, depth)
            if not zone then
                print("ATDOORCLEARCHECK: " .. h .. " skipped: " .. tostring(w) .. ".")
            else
                local blocked = {}
                for _, o in ipairs(obstacles) do
                    if overlaps(zone, o.pts) then blocked[#blocked + 1] = o.h end
                end
                if #blocked == 0 then
                    pass = pass + 1
                    print(string.format("ATDOORCLEARCHECK: %s (%s wide): clear %s mm on both sides -> ok", h, fmt(d.width), fmt(depth)))
                else
                    fail = fail + 1
                    print(string.format("ATDOORCLEARCHECK: %s (%s wide): BLOCKED by %s within %s mm of the opening; move it clear of the door",
                        h, fmt(d.width), table.concat(blocked, ", "), fmt(depth)))
                end
            end
        end
    end
    print(string.format("ATDOORCLEARCHECK: %d clear, %d blocked (zone %s mm deep both sides, house rule).", pass, fail, fmt(depth)))
end, "Checks that no obstacle stands in front of a door: a zone as wide as the opening and 900 mm deep on both sides of the wall must be free (house rule)", {
    { name = "doors",     type = "selection", prompt = "Select doors <all>", filter = "AEC_DOOR", optional = true },
    { name = "dwelling",  type = "string", prompt = "Dwelling id <all>", optional = true,
      description = "When no doors are selected: only the doors of this dwelling's rooms" },
    { name = "depth",     type = "distance", prompt = "Clear depth on each side of the door", default = 900 },
    { name = "obstacles", type = "string", prompt = "Obstacle types", default = "INSERT,AEC_MVBLOCK_REF",
      description = "Comma-separated entity types counted as obstacles" },
})

-- ── ATZONECHECK ─────────────────────────────────────────────────────────────
-- House principle: a home has three zones.
--   public  : entrance hall, portal, living, dining
--   private : bedrooms and the spaces attached to them (bathrooms, en-suites)
--   service : kitchen, laundry, garage, storage, staff bedroom
-- Corridors are circulation: they connect zones and belong to none. A room's
-- `zone` tag (ATROOMTYPE) overrides the default, e.g. a guest toilet = public.
-- Rules (project interpretation):
--   1. a private room is entered only from circulation or another private
--      room, never directly from a public or service room;
--   2. public and service rooms may connect directly (kitchen - dining);
--   3. each zone should hang together through its own rooms and corridors
--      (a split zone is reported as "check", not counted as a problem).
at.defineCommand("ATZONECHECK", function(p)
    local rooms = dwellingRooms(p.dwelling)
    if #rooms == 0 then
        print("ATZONECHECK: no rooms found (tag them with ATROOMTYPE).")
        return
    end
    local g = buildGraph(rooms, p.tol or WALL_TOL)
    local problems = 0

    -- classification
    local byZone, area = {}, {}
    for _, r in ipairs(rooms) do
        local z = zoneOf(r) or "?"
        byZone[z] = byZone[z] or {}
        table.insert(byZone[z], r)
        area[z] = (area[z] or 0) + (roomAreaM2(r) or 0)
    end
    for _, z in ipairs({ "public", "private", "service", "circulation", "?" }) do
        if byZone[z] then
            local names = {}
            for _, r in ipairs(byZone[z]) do names[#names + 1] = roomLabel(r) end
            print(string.format("ATZONECHECK: %s (%.2fm2): %s", z == "?" and "no zone" or z, area[z],
                table.concat(names, ", ")))
        end
    end
    if byZone["?"] then
        print("  check: rooms without a zone; tag their roomType or zone with ATROOMTYPE.")
    end

    -- rule 1: private rooms are entered from circulation or private rooms only
    for _, r in ipairs(byZone.private or {}) do
        for m in pairs(g.adj[r] or {}) do
            local zm = m ~= OUTSIDE and zoneOf(m) or nil
            if zm == "public" or zm == "service" then
                problems = problems + 1
                print(string.format("  PROBLEM: private %s opens directly into %s room %s via %s; enter private rooms from a corridor (zones).",
                    roomLabel(r), zm, roomLabel(m), g.via[r .. ">" .. m]))
            end
        end
    end

    -- rule 3: each zone hangs together through its own rooms and corridors
    for _, z in ipairs({ "public", "private", "service" }) do
        local list = byZone[z]
        if list and #list > 1 then
            local member = {}
            for _, r in ipairs(list) do member[r] = true end
            local group, groups = {}, 0
            for _, start in ipairs(list) do
                if not group[start] then
                    groups = groups + 1
                    local queue = { start }
                    group[start] = groups
                    while #queue > 0 do
                        local n = table.remove(queue)
                        for m in pairs(g.adj[n] or {}) do
                            if not group[m] and m ~= OUTSIDE and (member[m] or zoneOf(m) == "circulation") then
                                group[m] = groups; queue[#queue + 1] = m
                            end
                        end
                    end
                end
            end
            if groups > 1 then
                print(string.format("  check: the %s zone is split into %d separate groups; keep its rooms together.", z, groups))
            else
                print(string.format("  ok: the %s zone hangs together.", z))
            end
        end
    end

    print(string.format("ATZONECHECK: %d room(s), %d problem(s) (public / private / service, house principle).",
        #rooms, problems))
end, "Checks the public / private / service zoning of a dwelling: private rooms entered only from circulation or other private rooms, zones kept together (house principle)", {
    { name = "dwelling", type = "string", prompt = "Dwelling id <all rooms>", optional = true },
    { name = "tol",      type = "distance", prompt = "Max. distance of a door centre outside a room (wall)",
      default = WALL_TOL },
})

-- ── ATSITEMARK / ATSITECHECK ────────────────────────────────────────────────
-- Siting, the first design decision (DESIGN_PRINCIPLES.md §2): the road and
-- the access fix where the house is entered; the public zone faces the
-- access, the private zone goes away from the road, the service zone is
-- supplied from the road through its own access.
--
-- Mark the site with ATSITEMARK: lines / polylines / points tagged
-- site = road | access | serviceaccess (layer A-SITE).

local function siteMarkers(kind)
    local out = {}
    for _, h in ipairs(at.findByData("site", kind)) do
        local pts, closed = at.outline(h)
        if pts and #pts >= 1 then out[#out + 1] = { h = h, pts = pts, closed = closed } end
    end
    return out
end

-- Distance from a point to the nearest marker of a list (math.huge if none).
local function markerDist(px, py, markers)
    local best = math.huge
    for _, m in ipairs(markers) do
        local d
        if #m.pts == 1 then
            d = math.sqrt((px - m.pts[1].x) ^ 2 + (py - m.pts[1].y) ^ 2)
        else
            d = chainDist(px, py, m.pts, m.closed)
        end
        if d < best then best = d end
    end
    return best
end

local function outlineCentre(pts)
    local minx, miny, maxx, maxy = math.huge, math.huge, -math.huge, -math.huge
    for _, v in ipairs(pts) do
        minx, maxx = math.min(minx, v.x), math.max(maxx, v.x)
        miny, maxy = math.min(miny, v.y), math.max(maxy, v.y)
    end
    return (minx + maxx) / 2, (miny + maxy) / 2
end

at.defineCommand("ATSITEMARK", function(p)
    at.ensureLayer("A-SITE", { color = "red" })
    local kind = p.siteType:lower()
    local n = 0
    for _, h in ipairs(p.objects) do
        if at.setData(h, "site", kind) then
            at.setLayer(h, "A-SITE")
            n = n + 1
        end
    end
    print(string.format("ATSITEMARK: %d object(s) marked %s on A-SITE.", n, p.siteType))
end, "Marks the road edge, the pedestrian access or the service access (lines, polylines or points) for ATSITECHECK", {
    { name = "objects",  type = "selection", prompt = "Select lines, polylines or points",
      filter = "LINE,LWPOLYLINE,POINT,CIRCLE" },
    { name = "siteType", type = "keyword", prompt = "Marks", options = "Road Access ServiceAccess",
      default = "Road" },
})

at.defineCommand("ATSITECHECK", function(p)
    local roads = siteMarkers("road")
    if #roads == 0 then
        print("ATSITECHECK: check: no road marked (ATSITEMARK Road); siting not verified.")
        return
    end
    local access = siteMarkers("access")
    local serviceAccess = siteMarkers("serviceaccess")
    local rooms = dwellingRooms(p.dwelling)
    if #rooms == 0 then
        print("ATSITECHECK: no rooms found (tag them with ATROOMTYPE).")
        return
    end
    local g = buildGraph(rooms, WALL_TOL)
    local problems = 0

    -- 1. zone distance from the road (area-weighted room centres)
    local sum, wsum = {}, {}
    for _, r in ipairs(rooms) do
        local z = zoneOf(r)
        local pts = at.outline(r)
        if z and z ~= "circulation" and pts and #pts >= 3 then
            local cx, cy = outlineCentre(pts)
            local a = roomAreaM2(r) or 1
            sum[z] = (sum[z] or 0) + markerDist(cx, cy, roads) * a
            wsum[z] = (wsum[z] or 0) + a
        end
    end
    local dist = {}
    local parts = {}
    for _, z in ipairs({ "public", "private", "service" }) do
        if wsum[z] and wsum[z] > 0 then
            dist[z] = sum[z] / wsum[z]
            parts[#parts + 1] = string.format("%s %s", z, fmt(dist[z]))
        end
    end
    print("ATSITECHECK: mean distance from the road: " .. table.concat(parts, ", "))
    if dist.public and dist.private then
        if dist.private <= dist.public then
            problems = problems + 1
            print("  PROBLEM: the private zone is not farther from the road than the public zone; put the private zone away from the road.")
        else
            print("  ok: the public zone faces the road, the private zone lies away from it.")
        end
    end
    if dist.service and dist.private and dist.service > dist.private then
        print("  check: the service zone lies farther from the road than the private zone; it is supplied from the road.")
    end

    -- exterior doors: entrances and one-room doors
    local exterior = {}
    for _, e in ipairs(g.entrances) do
        local d = at.getAecProps(e.door)
        exterior[#exterior + 1] = { door = e.door, rooms = e.rooms, entrance = true, d = d }
    end
    for _, e in ipairs(g.exterior) do
        local d = at.getAecProps(e.door)
        exterior[#exterior + 1] = { door = e.door, rooms = { e.room }, entrance = false, d = d }
    end
    local function nearestDoor(markers)
        local best, bd
        for _, e in ipairs(exterior) do
            if e.d and e.d.center then
                local dd = markerDist(e.d.center.x, e.d.center.y, markers)
                if not bd or dd < bd then best, bd = e, dd end
            end
        end
        return best, bd
    end

    -- 2. the entrance faces the access
    local entrance = nil
    for _, e in ipairs(exterior) do if e.entrance then entrance = e end end
    if not entrance then
        print("  check: no door tagged Entrance (ATDOORTYPE); the entrance position is not verified.")
    else
        local ref = #access > 0 and access or roads
        local near = nearestDoor(ref)
        if near and near.door == entrance.door then
            print(string.format("  ok: entrance %s is the exterior door nearest the %s.", entrance.door,
                #access > 0 and "access" or "road"))
        else
            print(string.format("  check: entrance %s is not the exterior door nearest the %s (%s is).", entrance.door,
                #access > 0 and "access" or "road", near and near.door or "?"))
        end
    end

    -- 3. the service zone has its own access from outside
    local hasService = dist.service ~= nil
    if hasService then
        local own = {}
        for _, e in ipairs(exterior) do
            if not e.entrance then
                for _, r in ipairs(e.rooms) do
                    if zoneOf(r) == "service" then own[#own + 1] = e end
                end
            end
        end
        if #own == 0 then
            problems = problems + 1
            print("  PROBLEM: the service zone has no exterior door of its own; supply it from the road without crossing the other zones.")
        else
            local names = {}
            for _, e in ipairs(own) do names[#names + 1] = e.door end
            print("  ok: the service zone has its own exterior door(s): " .. table.concat(names, ", ") .. ".")
            if #serviceAccess > 0 then
                local near = nearestDoor(serviceAccess)
                local isOwn = false
                for _, e in ipairs(own) do if near and near.door == e.door then isOwn = true end end
                if not isOwn then
                    print(string.format("  check: the exterior door nearest the service access is %s, not a service door.",
                        near and near.door or "?"))
                end
            end
        end
    end

    print(string.format("ATSITECHECK: %d problem(s) (siting against road and access, DESIGN_PRINCIPLES.md §2).", problems))
end, "Checks the siting of a dwelling's zones against the marked road and access: private zone away from the road, entrance at the access, service zone with its own access (house principle)", {
    { name = "dwelling", type = "string", prompt = "Dwelling id <all rooms>", optional = true },
})

-- ── ATSANITARYCHECK ─────────────────────────────────────────────────────────
-- Quito Art. 68 and 150, per bathroom:
--   shower >= 0.56m2 with a shorter side >= 0.70m, separate from the rest;
--   >= 0.10m between consecutive fixtures (any two fixtures);
--   WC, basin, bidet: >= 0.15m to a side wall, >= 0.50m to the wall in front.
-- Every dwelling needs at least one bathroom with WC, shower (or bath) and
-- basin; the basin may stand just outside the WC/shower room (Art. 150).
--
-- Fixtures are blocks / ACA multi-view blocks inside the bathroom, typed from
-- their style or block name (toilet / basin / shower / bath / bidet, English
-- or Spanish) or a fixtureType tag. A fixture's back is the room edge nearest
-- to it; front and sides follow from that, so block drawing conventions do
-- not matter. Gaps are measured along the fixture's centre lines. ACA
-- multi-view block extents can depend on the current view (see
-- BATHROOM_DESIGN_CRITERIA.md section 5): check the result in plan view.
local FIXTURE_KEYS = {
    { "WC",      { "toil", "inodoro", "retrete", "wc" } },
    { "Bidet",   { "bidet" } },
    { "Basin",   { "basin", "lavabo", "lavamanos", "lavatory", "sink", "vanity" } },
    { "Shower",  { "shwr", "shower", "ducha" } },
    { "Bathtub", { "tub", "bañera", "banera", "tina" } },
}
local WALL_FIXTURES = { WC = true, Basin = true, Bidet = true }

local function fixtureType(h)
    local t = at.getData(h, "fixtureType")
    if t then return t end
    local name
    local a = at.getAecProps(h, { "StyleName" })
    if a and a.StyleName then name = a.StyleName end
    if not name then
        local pr = at.getProps(h)
        name = pr and pr.name
    end
    if not name then return nil end
    name = name:lower()
    for _, k in ipairs(FIXTURE_KEYS) do
        for _, w in ipairs(k[2]) do
            if name:find(w, 1, true) then return k[1] end
        end
    end
    return nil
end

-- First hit of a ray from (ox,oy) along (dx,dy) with an outline's edges.
local function rayHit(ox, oy, dx, dy, pts)
    local best
    for i = 1, #pts do
        local a, b = pts[i], pts[i % #pts + 1]
        local ex, ey = b.x - a.x, b.y - a.y
        local det = ex * dy - dx * ey
        if math.abs(det) > 1e-12 then
            local wx, wy = a.x - ox, a.y - oy
            local t = (ex * wy - wx * ey) / det
            local s = (dx * wy - dy * wx) / det
            if t > 1e-6 and s >= -1e-9 and s <= 1 + 1e-9 and (not best or t < best) then best = t end
        end
    end
    return best
end

local function centroid(pts)
    local cx, cy = 0, 0
    for _, v in ipairs(pts) do cx = cx + v.x; cy = cy + v.y end
    return cx / #pts, cy / #pts
end

-- Half extent of an outline from (cx,cy) along unit direction (dx,dy).
local function reachAlong(pts, cx, cy, dx, dy)
    local m = 0
    for _, v in ipairs(pts) do
        local s = (v.x - cx) * dx + (v.y - cy) * dy
        if s > m then m = s end
    end
    return m
end

local function checkBathroom(room, report)
    local roomPts = at.outline(room)
    if not roomPts or #roomPts < 3 then
        print("ATSANITARYCHECK: " .. room .. " skipped: no outline.")
        return
    end
    local fixtures, other = {}, {}
    for _, t in ipairs({ "AEC_MVBLOCK_REF", "INSERT" }) do
        for _, h in ipairs(at.entitiesInside(room, t) or {}) do
            local ft = fixtureType(h)
            local pts = at.outline(h)
            if ft and pts and #pts >= 3 then
                fixtures[#fixtures + 1] = { h = h, type = ft, pts = pts }
            elseif not ft then
                other[#other + 1] = h
            end
        end
    end
    local have, parts = {}, {}
    for _, f in ipairs(fixtures) do
        have[f.type] = true
        parts[#parts + 1] = f.type .. " " .. f.h
    end
    print(string.format("ATSANITARYCHECK: %s: %s", roomLabel(room),
        #parts > 0 and table.concat(parts, ", ") or "no fixtures found"))
    report.bathrooms = report.bathrooms + 1
    if have.WC and (have.Shower or have.Bathtub) then
        report.complete = true
        if have.Basin then report.full = true end
    end

    local function bad(msg) report.fails = report.fails + 1; print("  BELOW: " .. msg) end

    -- shower size
    for _, f in ipairs(fixtures) do
        if f.type == "Shower" then
            local minx, miny, maxx, maxy = math.huge, math.huge, -math.huge, -math.huge
            for _, v in ipairs(f.pts) do
                minx, maxx = math.min(minx, v.x), math.max(maxx, v.x)
                miny, maxy = math.min(miny, v.y), math.max(maxy, v.y)
            end
            local w, d = maxx - minx, maxy - miny
            local a, s = w * d / 1000000, math.min(w, d)
            if a + 1e-9 >= 0.56 and s + 0.5 >= 700 then
                print(string.format("  ok: shower %s %sx%s = %.2fm2", f.h, fmt(w), fmt(d), a))
            else
                bad(string.format("shower %s %sx%s = %.2fm2, needs 0.56m2 with a side of at least 700 (Art. 150)",
                    f.h, fmt(w), fmt(d), a))
            end
        end
    end

    -- gaps between fixtures
    for i = 1, #fixtures do
        for j = i + 1, #fixtures do
            local a, b = fixtures[i], fixtures[j]
            local d = at.distance(a.h, b.h)
            if d and d + 0.5 < 100 then
                bad(string.format("%s %s and %s %s are %s apart, need 100 (Art. 68)", a.type, a.h, b.type, b.h, fmt(d)))
            end
        end
    end

    -- side and front clearances of WC / basin / bidet
    for _, f in ipairs(fixtures) do
        if WALL_FIXTURES[f.type] then
            -- back: the room edge nearest the fixture
            local bi, bd
            for i = 1, #roomPts do
                local d = segOutline(roomPts[i], roomPts[i % #roomPts + 1], f.pts)
                if not bd or d < bd then bi, bd = i, d end
            end
            local a, b = roomPts[bi], roomPts[bi % #roomPts + 1]
            local ux, uy = b.x - a.x, b.y - a.y
            local l = math.sqrt(ux * ux + uy * uy)
            ux, uy = ux / l, uy / l
            local cx, cy = centroid(f.pts)
            local nx, ny = -uy, ux
            if (cx - a.x) * nx + (cy - a.y) * ny < 0 then nx, ny = -nx, -ny end

            local hit = rayHit(cx, cy, nx, ny, roomPts)
            local front = hit and (hit - reachAlong(f.pts, cx, cy, nx, ny))
            local sides = {}
            for _, sgn in ipairs({ 1, -1 }) do
                local dx, dy = ux * sgn, uy * sgn
                local half = reachAlong(f.pts, cx, cy, dx, dy)
                local wall = rayHit(cx, cy, dx, dy, roomPts)
                local fix
                for _, o in ipairs(fixtures) do
                    if o ~= f then
                        local t = rayHit(cx, cy, dx, dy, o.pts)
                        if t and (not fix or t < fix) then fix = t end
                    end
                end
                if wall and not (fix and fix < wall) then sides[#sides + 1] = wall - half end
            end
            local msgs, ok = {}, true
            if front then
                msgs[#msgs + 1] = "front " .. fmt(front)
                if front + 0.5 < 500 then ok = false end
            end
            for _, s in ipairs(sides) do
                msgs[#msgs + 1] = "side wall " .. fmt(s)
                if s + 0.5 < 150 then ok = false end
            end
            if ok then
                print(string.format("  ok: %s %s: %s", f.type, f.h, table.concat(msgs, ", ")))
            else
                bad(string.format("%s %s: %s; needs front 500, side wall 150 (Art. 68)", f.type, f.h, table.concat(msgs, ", ")))
            end
        end
    end
    if #other > 0 then
        print("  note: not recognised as fixtures: " .. table.concat(other, ", ") .. " (tag fixtureType if needed)")
    end
end

at.defineCommand("ATSANITARYCHECK", function(p)
    local rooms = p.rooms
    if not rooms or #rooms == 0 then
        rooms = {}
        for _, r in ipairs(dwellingRooms(p.dwelling)) do
            if roomType(r) == "Bathroom" then rooms[#rooms + 1] = r end
        end
    end
    if #rooms == 0 then
        print("ATSANITARYCHECK: no bathrooms found (tag them Bathroom with ATROOMTYPE).")
        return
    end
    local report = { fails = 0, bathrooms = 0, complete = false, full = false }
    for _, r in ipairs(rooms) do checkBathroom(r, report) end
    if p.dwelling and p.dwelling ~= "" then
        if not report.complete then
            report.fails = report.fails + 1
            print("  PROBLEM: no bathroom with WC and shower (or bath) in this dwelling (Art. 150).")
        elseif not report.full then
            print("  check: the complete bathroom has no basin inside; Art. 150 allows it next to the WC/shower room.")
        else
            print("  ok: the dwelling has a complete bathroom (WC, shower or bath, basin).")
        end
    end
    print(string.format("ATSANITARYCHECK: %d bathroom(s), %d problem(s). Extents of ACA fixtures depend on the view: check in plan. %s",
        report.bathrooms, report.fails, SOURCE))
end, "Checks bathroom fixtures (Quito Art. 68, 150): shower size, 100 mm between fixtures, 150 mm to side walls, 500 mm in front, and a complete bathroom per dwelling", {
    { name = "rooms",    type = "selection", prompt = "Select bathrooms <all tagged>",
      filter = "AEC_SPACE,LWPOLYLINE", optional = true },
    { name = "dwelling", type = "string", prompt = "Dwelling id <all>", optional = true,
      description = "When no rooms are selected: this dwelling's bathrooms, plus the complete-bathroom rule" },
})

-- ── ATECONOMYCHECK ──────────────────────────────────────────────────────────
-- Space economy (DESIGN_PRINCIPLES.md §5), guidance rather than rules, so it
-- reports "check" lines, not problems:
--   1. as little circulation and as much useful space as possible: the
--      circulation share (Hall, Corridor, Portal area / total) above `maxCirc`
--      (default 15%, a starting figure) is a check;
--   2. where a room is entered decides how far you walk in it: a rectangular
--      room is best entered through a long side. For each entry (door or
--      walkable light boundary) of a useful room: the side it is on, and the
--      mean walking distance from it to every point of the room compared with
--      entering at the middle of the longest side. A room at least 1.25 times
--      as long as wide entered from a short side, or more than 15% extra
--      walking, is a check.
local ECON_GRID = 150          -- mm between sample points
local ECON_ASPECT = 1.25       -- below this a room is "square enough"
local ECON_EXTRA = 0.15        -- extra mean walk that is worth a check

-- Sample points inside a closed outline on a ECON_GRID grid.
local function samplePoints(pts)
    local minx, miny, maxx, maxy = math.huge, math.huge, -math.huge, -math.huge
    for _, v in ipairs(pts) do
        minx, maxx = math.min(minx, v.x), math.max(maxx, v.x)
        miny, maxy = math.min(miny, v.y), math.max(maxy, v.y)
    end
    local out = {}
    local x = minx + ECON_GRID / 2
    while x < maxx do
        local y = miny + ECON_GRID / 2
        while y < maxy do
            if pointInPts(x, y, pts) then out[#out + 1] = { x = x, y = y } end
            y = y + ECON_GRID
        end
        x = x + ECON_GRID
    end
    return out
end

local function meanDist(px, py, samples)
    local s = 0
    for _, q in ipairs(samples) do s = s + math.sqrt((q.x - px) ^ 2 + (q.y - py) ^ 2) end
    return #samples > 0 and s / #samples or 0
end

-- Nearest outline edge to a point: index, length, projected point.
local function nearestEdge(pts, px, py)
    local bi, bd
    for i = 1, #pts do
        local d = distToSegment(px, py, pts[i], pts[i % #pts + 1])
        if not bd or d < bd then bi, bd = i, d end
    end
    local a, b = pts[bi], pts[bi % #pts + 1]
    local ex, ey = b.x - a.x, b.y - a.y
    local len = math.sqrt(ex * ex + ey * ey)
    local t = len > 0 and math.max(0, math.min(1, ((px - a.x) * ex + (py - a.y) * ey) / (len * len))) or 0
    return bi, len, { x = a.x + ex * t, y = a.y + ey * t }
end

-- Where a door opens into a room: its opening centre on the host wall (or its
-- extents centre for a freestanding door), projected onto the room's edge.
local function doorEntry(d, roomPts)
    local px, py = d.center.x, d.center.y
    local w = hostWall(d)
    if w then
        local a, b = w.startPoint, w.endPoint
        local ux, uy = b.x - a.x, b.y - a.y
        local l = math.sqrt(ux * ux + uy * uy)
        if l > 0 then
            ux, uy = ux / l, uy / l
            local t = (px - a.x) * ux + (py - a.y) * uy
            px, py = a.x + ux * t, a.y + uy * t
        end
    end
    local _, _, q = nearestEdge(roomPts, px, py)
    return q
end

at.defineCommand("ATECONOMYCHECK", function(p)
    local maxCirc = p.maxCirc or 0.15
    local rooms = dwellingRooms(p.dwelling)
    if #rooms == 0 then
        print("ATECONOMYCHECK: no rooms found (tag them with ATROOMTYPE).")
        return
    end
    local g = buildGraph(rooms, WALL_TOL)

    -- 1. circulation share
    local total, circArea = 0, 0
    for _, r in ipairs(rooms) do
        local a = roomAreaM2(r) or 0
        total = total + a
        if CIRCULATION[roomType(r)] then circArea = circArea + a end
    end
    local share = total > 0 and circArea / total or 0
    print(string.format("ATECONOMYCHECK: circulation %.2fm2 of %.2fm2 = %.1f%% (useful %.1f%%)",
        circArea, total, share * 100, (1 - share) * 100))
    if share > maxCirc then
        print(string.format("  check: circulation above %.0f%%; shorten corridors or let halls do more work.", maxCirc * 100))
    else
        print(string.format("  ok: circulation at or below %.0f%%.", maxCirc * 100))
    end

    -- 2. where each useful room is entered
    local doors = at.entities("AEC_DOOR")
    for _, r in ipairs(rooms) do
        if not CIRCULATION[roomType(r)] then
            local pts, closed = at.outline(r)
            if pts and closed and #pts >= 3 then
                local samples = samplePoints(pts)
                -- the longest edge and the ideal entry at its middle
                local li, ll = 1, 0
                local shortest = math.huge
                for i = 1, #pts do
                    local a, b = pts[i], pts[i % #pts + 1]
                    local l = math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2)
                    if l > ll then li, ll = i, l end
                    if l > 1 and l < shortest then shortest = l end
                end
                local la, lb = pts[li], pts[li % #pts + 1]
                local ideal = meanDist((la.x + lb.x) / 2, (la.y + lb.y) / 2, samples)
                local elongated = shortest < math.huge and ll / shortest >= ECON_ASPECT

                -- entries: doors and walkable light boundaries of this room
                local entries = {}
                for _, h in ipairs(doors) do
                    local d = at.getAecProps(h)
                    if d and d.center and doorTouches(r, d, WALL_TOL) then
                        entries[#entries + 1] = { what = "door " .. h, pt = doorEntry(d, pts) }
                    end
                end
                for _, lb in ipairs(g.boundaries) do
                    if lb.walkable and (lb.r1 == r or lb.r2 == r) then
                        entries[#entries + 1] = { what = lb.type .. " boundary",
                            pt = { x = (lb.a.x + lb.b.x) / 2, y = (lb.a.y + lb.b.y) / 2 } }
                    end
                end

                if #entries == 0 then
                    print(string.format("ATECONOMYCHECK: %s: no entry found.", roomLabel(r)))
                else
                    local parts, worst = {}, 0
                    local shortSide = false
                    for _, e in ipairs(entries) do
                        local _, el = nearestEdge(pts, e.pt.x, e.pt.y)
                        local isLong = el >= ll - 1
                        local extra = ideal > 0 and meanDist(e.pt.x, e.pt.y, samples) / ideal - 1 or 0
                        if extra > worst then worst = extra end
                        if elongated and not isLong then shortSide = true end
                        parts[#parts + 1] = string.format("%s on a %s side (%s), %+.0f%% walk",
                            e.what, isLong and "long" or "short", fmt(el), extra * 100)
                    end
                    print(string.format("ATECONOMYCHECK: %s (%s x %s): %s", roomLabel(r), fmt(ll), fmt(shortest),
                        table.concat(parts, "; ")))
                    if shortSide then
                        print("  check: entered from a short side; entering through a long side means walking less.")
                    elseif worst > ECON_EXTRA then
                        print(string.format("  check: the entry is off-centre (%.0f%% more walking than the middle of the long side).", worst * 100))
                    end
                end
            end
        end
    end
    print("ATECONOMYCHECK: done (space economy guidance, DESIGN_PRINCIPLES.md §5).")
end, "Space economy guidance: circulation share of the dwelling, and whether each room is entered through a long side with little walking (house principle)", {
    { name = "dwelling", type = "string", prompt = "Dwelling id <all rooms>", optional = true },
    { name = "maxCirc",  type = "number", prompt = "Maximum circulation share", default = 0.15,
      description = "Circulation area / total area above which a check is reported (0.15 = 15%)" },
})

-- ── ATBOUNDARYTYPE / ATBOUNDARYLIST ─────────────────────────────────────────
-- Marks lines / polylines drawn along the edge between two rooms as a light
-- boundary of a given type (DESIGN_PRINCIPLES.md §3), on layer A-BOUNDARY.
at.defineCommand("ATBOUNDARYTYPE", function(p)
    at.ensureLayer("A-BOUNDARY", { color = "magenta", linetype = "Continuous" })
    local n = 0
    for _, h in ipairs(p.markers) do
        if at.setData(h, "boundary", p.boundaryType:lower()) then
            at.setLayer(h, "A-BOUNDARY")
            n = n + 1
        end
    end
    print(string.format("ATBOUNDARYTYPE: %d marker(s) tagged %s (%s).", n, p.boundaryType,
        WALKABLE[p.boundaryType:lower()] and "walkable: connects the rooms" or "not walkable: separates the rooms"))
end, "Marks lines drawn along the edge between two rooms as a light boundary: Line, Floor, Level, Curtain (walkable) or Glass, Wall (not walkable)", {
    { name = "markers",      type = "selection", prompt = "Select boundary lines / polylines",
      filter = "LINE,LWPOLYLINE" },
    { name = "boundaryType", type = "keyword", prompt = "Boundary type",
      options = "Line Floor Level Curtain Glass Wall", default = "Floor" },
})

-- Lists the light boundaries between a dwelling's rooms (or all rooms).
at.defineCommand("ATBOUNDARYLIST", function(p)
    local rooms = dwellingRooms(p.dwelling)
    local list = lightBoundaries(rooms)
    for _, lb in ipairs(list) do
        print(string.format("ATBOUNDARYLIST: %s | %s: %s boundary, %s long%s -> %s",
            roomLabel(lb.r1), roomLabel(lb.r2), lb.type, fmt(lb.length),
            lb.marker and (" (" .. lb.marker .. ")") or " (not marked)",
            lb.walkable and "connected" or "separated"))
    end
    print(string.format("ATBOUNDARYLIST: %d light boundary(ies) between %d room(s); rooms divided by ACA walls are not listed.",
        #list, #rooms))
end, "Lists the light boundaries (no wall between two rooms that touch) and whether they connect the rooms", {
    { name = "dwelling", type = "string", prompt = "Dwelling id <all rooms>", optional = true },
})

-- ── ATDWELLINGCHECK ─────────────────────────────────────────────────────────
-- Runs every check on the rooms tagged with one dwelling id and adds the
-- dwelling-level rules: composition (living, kitchen, bathroom, bedrooms as
-- tagged, laundry), the Art. 147 useful-area subtotal, then per-room size and
-- 300mm module, daylight, depth, the dwelling's doors, and access (Art. 147 +
-- circulation house rule). Problem lines of each check are counted.

-- Art. 147 subtotal of the minimum useful areas (living, kitchen, bedrooms,
-- bathroom) for 1, 2, 3+ bedrooms.
local QUITO_SUBTOTAL = { 28.50, 38.00, 49.00 }
local USEFUL_FOR_SUBTOTAL = { Living = true, Dining = true, Kitchen = true, MainBedroom = true,
                              Bedroom2 = true, Bedroom3 = true, Bathroom = true }
-- Upper-case words the checks print on a failing line (summaries are lower-case).
local FAIL_MARKERS = { "BELOW", "OFF MODULE", "DOES NOT MEET", "PROBLEM", "TOO DEEP", "FAILS" }

-- Runs a command, echoing its output, and counts its failing lines.
local function runCounted(nameCmd, args)
    local real, fails = print, 0
    print = function(...)
        local n = select("#", ...)
        local parts = {}
        for i = 1, n do parts[i] = tostring((select(i, ...))) end
        local line = table.concat(parts, "\t")
        for _, m in ipairs(FAIL_MARKERS) do
            if line:find(m, 1, true) then fails = fails + 1; break end
        end
        real("  " .. line)
    end
    local ok, err = pcall(at.runCommand, nameCmd, args)
    print = real
    if not ok then
        fails = fails + 1
        print("  ERROR in " .. nameCmd .. ": " .. tostring(err))
    end
    return fails
end

at.defineCommand("ATDWELLINGCHECK", function(p)
    local rooms = at.findByData("dwelling", p.dwelling)
    if #rooms == 0 then
        print("ATDWELLINGCHECK: no rooms tagged with dwelling " .. p.dwelling .. " (use ATROOMTYPE).")
        return
    end
    local sections = {}
    local function section(title, fails)
        sections[#sections + 1] = { title = title, fails = fails }
    end

    -- 1. composition and useful-area subtotal
    print(string.format("== ATDWELLINGCHECK %s: %d room(s) ==", p.dwelling, #rooms))
    print("-- composition (Art. 147, 150, 152)")
    local count, beds, tagBeds, useful = {}, 0, nil, 0
    for _, r in ipairs(rooms) do
        local t = roomType(r) or "?"
        count[t] = (count[t] or 0) + 1
        if t == "MainBedroom" or t == "Bedroom2" or t == "Bedroom3" then beds = beds + 1 end
        local b = at.getData(r, "bedrooms")
        if type(b) == "number" and (not tagBeds or b > tagBeds) then tagBeds = b end
        if USEFUL_FOR_SUBTOTAL[t] then useful = useful + (roomAreaM2(r) or 0) end
    end
    local parts = {}
    for t, n in pairs(count) do parts[#parts + 1] = t .. " x" .. n end
    table.sort(parts)
    print("  rooms: " .. table.concat(parts, ", "))
    local fails = 0
    local function need(cond, okText, failText)
        if cond then print("  ok: " .. okText) else fails = fails + 1; print("  PROBLEM: " .. failText) end
    end
    need(count.Living, "living room present", "no Living room")
    need(count.Kitchen, "kitchen present", "no Kitchen")
    need(count.Bathroom, "bathroom present", "no Bathroom (Art. 150: at least one)")
    need(beds >= 1, beds .. " bedroom(s)", "no bedroom (MainBedroom)")
    if tagBeds then
        need(tagBeds == beds, "bedroom count matches the tag (" .. beds .. ")",
            string.format("rooms tagged bedrooms=%d but %d bedroom(s) found", tagBeds, beds))
    end
    if count.Laundry then
        print("  ok: laundry present")
    else
        print("  check: no Laundry room; Art. 152 needs 3m2 (may be in the kitchen, open-air or communal)")
    end
    if beds >= 1 then
        local sub = QUITO_SUBTOTAL[math.min(beds, 3)]
        need(useful + 1e-6 >= sub,
            string.format("useful area %.2fm2 >= subtotal %.2fm2 for %d bedroom(s)", useful, sub, beds),
            string.format("useful area %.2fm2 below the Art. 147 subtotal %.2fm2 for %d bedroom(s)", useful, sub, beds))
    end
    section("composition", fails)

    -- 2. per-room size + module
    print("-- room sizes and 300mm module (Art. 147, house rule)")
    fails = 0
    for _, r in ipairs(rooms) do
        fails = fails + runCounted("ATROOMSIZECHECK", { room = r, jurisdiction = "Quito" })
    end
    section("room sizes / module", fails)

    -- 3. daylight and depth
    print("-- daylight (Art. 69)")
    section("daylight", runCounted("ATDAYLIGHTCHECK", { rooms = rooms }))
    print("-- room depth (Art. 151)")
    section("room depth", runCounted("ATROOMDEPTHCHECK", { rooms = rooms }))

    -- 4. doors touching the dwelling's rooms
    print("-- doors (Art. 153)")
    local doors, seen = {}, {}
    for _, h in ipairs(at.entities("AEC_DOOR")) do
        local d = at.getAecProps(h)
        if d and d.center then
            for _, r in ipairs(rooms) do
                if not seen[h] and doorTouches(r, d, WALL_TOL) then seen[h] = true; doors[#doors + 1] = h end
            end
        end
    end
    if #doors == 0 then
        print("  PROBLEM: no doors found")
        section("doors", 1)
    else
        section("doors", runCounted("ATDOORCHECK", { doors = doors }))
    end

    -- 4b. nothing in front of the doors
    print("-- door clearance (house rule)")
    section("door clearance", runCounted("ATDOORCLEARCHECK", { dwelling = p.dwelling }))

    -- 4c. bathrooms
    print("-- bathrooms (Art. 68, 150)")
    section("bathrooms", runCounted("ATSANITARYCHECK", { dwelling = p.dwelling }))

    -- 5. circulation widths
    print("-- circulation widths (Art. 160)")
    section("circulation widths", runCounted("ATCIRCULATIONCHECK", { dwelling = p.dwelling }))

    -- 6. access
    print("-- access (Art. 147, circulation house rule)")
    section("access", runCounted("ATPASSAGECHECK", { dwelling = p.dwelling }))

    -- 7. zones
    print("-- zones (public / private / service, house principle)")
    section("zones", runCounted("ATZONECHECK", { dwelling = p.dwelling }))

    -- 7b. siting against road and access
    print("-- siting (house principle)")
    section("siting", runCounted("ATSITECHECK", { dwelling = p.dwelling }))

    -- 8. space economy (guidance: "check" lines, not counted)
    print("-- space economy (house principle)")
    section("space economy", runCounted("ATECONOMYCHECK", { dwelling = p.dwelling }))

    -- summary
    local total, line = 0, {}
    for _, s in ipairs(sections) do
        total = total + s.fails
        line[#line + 1] = string.format("%s %s", s.title, s.fails == 0 and "ok" or (s.fails .. " problem(s)"))
    end
    print(string.format("== ATDWELLINGCHECK %s: %s ==", p.dwelling,
        total == 0 and "ALL CHECKS PASS" or (total .. " PROBLEM LINE(S)")))
    print("   " .. table.concat(line, "; "))
end, "Runs every room check on one dwelling (rooms tagged with its id by ATROOMTYPE): composition, useful-area subtotal, sizes, 300mm module, daylight, depth, doors, access (Quito + house rules)", {
    { name = "dwelling", type = "string", prompt = "Dwelling id (as tagged by ATROOMTYPE)" },
})

-- Tags doors with their type for ATDOORCHECK (overrides the inference).
at.defineCommand("ATDOORTYPE", function(p)
    local n = 0
    for _, h in ipairs(p.doors) do
        if at.setData(h, "doorType", p.doorType) then n = n + 1 end
    end
    print(string.format("ATDOORTYPE: %d door(s) tagged %s.", n, p.doorType))
end, "Tags ACA doors as Entrance, Interior or Bathroom (used by ATDOORCHECK)", {
    { name = "doors",    type = "selection", prompt = "Select doors", filter = "AEC_DOOR" },
    { name = "doorType", type = "keyword", prompt = "Door type", options = "Entrance Interior Bathroom",
      default = "Interior" },
})
