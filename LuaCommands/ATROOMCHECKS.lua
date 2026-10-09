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
                     "Bathroom", "Laundry", "ServiceBedroom" }

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
    for _, c in ipairs({ { p.min.x, p.min.y }, { p.max.x, p.min.y }, { p.max.x, p.max.y }, { p.min.x, p.max.y } }) do
        if at.pointInPolygon(room, c[1], c[2]) then return true end
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
