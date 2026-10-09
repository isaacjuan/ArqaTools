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
                     "Bathroom", "Laundry", "ServiceBedroom", "Corridor", "Hall", "Portal" }

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

at.defineCommand("ATPASSAGECHECK", function(p)
    local tol = p.tol or WALL_TOL
    local rooms = {}
    for _, r in ipairs(allRooms()) do
        if not p.dwelling or p.dwelling == "" or at.getData(r, "dwelling") == p.dwelling then
            rooms[#rooms + 1] = r
        end
    end
    if #rooms == 0 then
        print("ATPASSAGECHECK: no rooms found" .. (p.dwelling and (" for dwelling " .. p.dwelling) or "")
            .. " (tag them with ATROOMTYPE).")
        return
    end

    -- connection graph
    local adj, via = {}, {}
    local function link(a, b, door)
        adj[a] = adj[a] or {}; adj[b] = adj[b] or {}
        adj[a][b] = true; adj[b][a] = true
        via[a .. ">" .. b] = door; via[b .. ">" .. a] = door
    end
    local doors = at.entities("AEC_DOOR")
    for _, h in ipairs(at.entities("AecDbOpening")) do doors[#doors + 1] = h end
    local used, entrances, exterior = 0, {}, {}
    for _, h in ipairs(doors) do
        local d = at.getAecProps(h)
        if d and d.center then
            local touch = {}
            for _, r in ipairs(rooms) do
                if doorTouches(r, d, tol) then touch[#touch + 1] = r end
            end
            if #touch > 0 then used = used + 1 end
            local isEntrance = at.getData(h, "doorType") == "Entrance"
            if isEntrance and #touch > 0 then entrances[#entrances + 1] = { door = h, rooms = touch }
            elseif #touch == 1 then exterior[#exterior + 1] = { door = h, room = touch[1] } end
            if #touch == 1 or isEntrance then
                for _, r in ipairs(touch) do link(r, OUTSIDE, h) end
            end
            for i = 1, #touch do
                for j = i + 1, #touch do link(touch[i], touch[j], h) end
            end
        end
    end

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
    local noted = {}
    for _, r in ipairs(rooms) do
        if not CIRCULATION[roomType(r)] and not ensuite[r] then
            local ok, through = false, {}
            for m in pairs(adj[r] or {}) do
                if circ[m] then ok = true
                elseif ensuite[m] ~= r then through[#through + 1] = name(m) end   -- not its own en-suite
                -- a direct door between two useful spaces: fine only as an extra
                if m ~= OUTSIDE and not CIRCULATION[roomType(m)] and not ensuite[m] then
                    local key = r < m and (r .. "|" .. m) or (m .. "|" .. r)
                    if not noted[key] then
                        noted[key] = true
                        print(string.format("  note: direct door %s between useful spaces %s and %s.",
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

-- ── ATDWELLINGCHECK ─────────────────────────────────────────────────────────
-- Runs every check on the rooms tagged with one dwelling id and adds the
-- dwelling-level rules: composition (living, kitchen, bathroom, bedrooms as
-- tagged, laundry), the Art. 147 useful-area subtotal, then per-room size and
-- 300mm module, daylight, depth, the dwelling's doors, and access (Art. 147 +
-- circulation house rule). Problem lines of each check are counted.

-- Art. 147 subtotal of the minimum useful areas (living, kitchen, bedrooms,
-- bathroom) for 1, 2, 3+ bedrooms.
local QUITO_SUBTOTAL = { 28.50, 38.00, 49.00 }
local USEFUL_FOR_SUBTOTAL = { Living = true, Kitchen = true, MainBedroom = true,
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

    -- 5. access
    print("-- access (Art. 147, circulation house rule)")
    section("access", runCounted("ATPASSAGECHECK", { dwelling = p.dwelling }))

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
