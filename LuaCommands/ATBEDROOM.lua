-- ATBEDROOM: bedroom-specific circulation helpers (bed access clearance,
-- room-size regulatory check). Reuses the generic tools from ATCLEARANCE.lua
-- for furniture that only needs a single front clearance (wardrobe doors,
-- desk/chair pullout) -- see the header note below for which tool to use for
-- what. Edit this file and run ATLUARELOAD, no rebuild needed.
--
-- *** SOURCING, read before trusting a number ***
--
-- Bed/wardrobe walkway clearance (ATBEDCLEARANCE default 700mm, min 600mm):
-- no Belgian code governs private bedroom circulation (same finding as the
-- bathroom session -- NBN has nothing here, regional accessibility norms
-- target public buildings / "adaptable housing", not ordinary residential).
-- The 600mm-min/800-1000mm-comfortable walkway figure and the ~450-600mm
-- clearance in front of an open wardrobe door are SECONDARY-SOURCED
-- ergonomic convention (furniture-retailer/trade guides reporting Neufert-
-- style figures), not a verified Neufert page number and not law. Treat as
-- design-stage comfort guidance only, same status as the bathroom's
-- WTCB/Buildwise-style defaults.
--
-- Room-size minimums (ATROOMSIZECHECK): UNLIKE the bathroom session, real
-- regulatory figures DO exist here, but they come from RENTAL HOUSING
-- QUALITY law, not a general residential building code -- they apply when
-- a room is let out, not to every bedroom in every house. Confirmed via web
-- search 2026-10-07, not read from the primary legal text directly:
--   - Flanders (Vlaamse Codex Wonen / "woningkwaliteitsnormen"): a room with
--     its own kitchen, bath or toilet (but not all three) >= 12m2; a room
--     is for one person unless >= 18m2 AND the occupants have a "lasting
--     bond" (couple, parent+child). Self-contained dwelling net floor area
--     (living room + kitchen + bedrooms) >= 18m2 total. In force from
--     1 Jan 2021; pre-1 Oct 2016 licensed dwellings are grandfathered.
--   - Brussels-Capital Region: from 2026, minimum net surface 18m2 for one
--     occupant, +10m2 per additional occupant (28/38/48...). Student rooms:
--     12m2. A 2014 ministerial order (secondary-sourced, not confirmed
--     against the current text) also says bedrooms can't be a windowless
--     central room in a row of rooms.
--   - Wallonia: found only a "light dwellings" (habitations legeres)
--     overcrowding rule -- 15m2 for one occupant +5m2 per additional
--     occupant, at least one 10m2 room if 2+ occupants. May not apply to
--     an ordinary house.
--   - No general Flemish/Walloon rule was found for an ordinary bedroom in
--     an owner-occupied house (as opposed to a let room) -- don't quote
--     these figures as if they applied there.
-- CONFIRM against wonenvlaanderen.be/woningkwaliteit (Flanders),
-- be.brussels (Brussels) or the Walloon housing authority before relying on
-- this for anything beyond an early, rough design-stage flag.
--
-- Quito (Ecuador): unlike the Belgian figures above, this IS a general
-- building code that applies to every dwelling, owner-occupied included.
-- Read from the ordinance text itself (Ordenanza 3457, DMQ, 2003, Art. 147;
-- file "ORD-3746 - NORMAS DE ARQUITECTURA Y URBANISMO.pdf"). It sets a
-- minimum useful area AND a minimum side per room type, the area scaled by
-- the dwelling's bedroom count. See QUITO_SPACE_STANDARDS.md. Check whether
-- a later DMQ amendment changed the figures before relying on them.

local UNVERIFIED = "[SECONDARY-SOURCED FIGURE -- confirm current regional housing-quality rules before relying on this]"
local QUITO_SOURCE = "[Quito Ord. 3457 Art. 147 (2003) -- check for later DMQ amendments]"

-- Quito Art. 147: minimum side (m) and minimum useful area (m2) per room
-- type, indexed by the dwelling's bedroom count (1, 2, 3 = three or more).
-- nil = that room does not exist in a dwelling with that many bedrooms.
-- Bedroom areas include the wardrobe. Bedroom3 is also used for a 4th or
-- further bedroom (the table stops at "3 or more"; assumption, not stated).
local QUITO_ROOMS = {
    Living         = { side = 2.70, area = { 13.00, 13.00, 16.00 }, label = "living-dining room" },
    Kitchen        = { side = 1.50, area = {  4.00,  5.50,  6.50 }, label = "kitchen" },
    MainBedroom    = { side = 2.50, area = {  9.00,  9.00,  9.00 }, label = "main bedroom" },
    Bedroom2       = { side = 2.20, area = {   nil,  8.00,  8.00 }, label = "second bedroom" },
    Bedroom3       = { side = 2.20, area = {   nil,   nil,  7.00 }, label = "third (or further) bedroom" },
    Bathroom       = { side = 1.20, area = {  2.50,  2.50,  2.50 }, label = "bathroom" },
    Laundry        = { side = 1.30, area = {  3.00,  3.00,  3.00 }, label = "laundry/drying area" },
    ServiceBedroom = { side = 2.00, area = {  6.00,  6.00,  6.00 }, label = "staff bedroom" },
}

-- House design rule (project decision 2026-10-09, not a code requirement):
-- every space dimension is a multiple of a 300mm planning module, so sizes
-- are easy to choose and coordinate. Measured on the boundary as drawn.
local MODULE_DEFAULT = 300
local MODULE_TOL = 1.0   -- mm; drawing noise, not design freedom

-- Smallest multiple of `module` (mm) that is >= v (mm).
local function moduleCeil(v, module)
    return math.ceil(v / module - 1e-9) * module
end

-- The dimensions of a room boundary, in drawing units (mm):
-- closed polyline -> every straight edge; ACA space -> its length and width;
-- anything else -> its bounding-box width and depth.
local function roomDimensions(h)
    local props = at.getProps(h)
    if not props then return nil, "could not read the object" end
    if props.vertices and props.closed then
        local v, dims, arcs = props.vertices, {}, 0
        for i = 1, #v do
            local a, b = v[i], v[i % #v + 1]
            if math.abs(a.bulge or 0) > 1e-9 then
                arcs = arcs + 1
            else
                local len = math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2)
                if len > MODULE_TOL then dims[#dims + 1] = len end
            end
        end
        return dims, nil, arcs
    end
    local aec = at.getAecProps(h)
    if aec and aec.kind == "space" and aec.length and aec.width then
        return { aec.length, aec.width }
    end
    if props.min and props.max then
        return { props.max.x - props.min.x, props.max.y - props.min.y }
    end
    return nil, "no dimensions found"
end

-- Checks every dimension against the module. Returns ok, a text summary.
local function moduleCheck(h, module)
    local dims, err, arcs = roomDimensions(h)
    if not dims then return nil, err end
    local bad, parts, seen = 0, {}, {}
    for _, d in ipairs(dims) do
        local key = string.format("%.0f", d)
        if not seen[key] then
            seen[key] = true
            local off = math.abs(d - math.floor(d / module + 0.5) * module)
            if off <= MODULE_TOL then
                parts[#parts + 1] = key .. " ok"
            else
                bad = bad + 1
                parts[#parts + 1] = string.format("%s NOT a multiple of %d (nearest %.0f / %.0f)",
                    key, module, math.floor(d / module) * module, moduleCeil(d, module))
            end
        end
    end
    local text = table.concat(parts, ", ")
    if arcs and arcs > 0 then text = text .. string.format(" (%d arc edge(s) not checked)", arcs) end
    return bad == 0, text
end

-- Checks spaces / room boundaries against the 300mm planning module.
at.defineCommand("ATMODULECHECK", function(p)
    local module = p.module or MODULE_DEFAULT
    if module <= 0 then print("ATMODULECHECK: module must be > 0."); return end
    local pass, fail = 0, 0
    for _, h in ipairs(p.rooms) do
        local ok, text = moduleCheck(h, module)
        if ok == nil then
            print(string.format("ATMODULECHECK: %s skipped: %s", h, text))
        else
            if ok then pass = pass + 1 else fail = fail + 1 end
            local rt = at.getData(h, "roomType")
            print(string.format("ATMODULECHECK: %s%s: %s -> %s", h, rt and (" (" .. rt .. ")") or "",
                text, ok and "on module" or "OFF MODULE"))
        end
    end
    print(string.format("ATMODULECHECK: %d on module, %d off module (module %d mm, tolerance %.0f mm).",
        pass, fail, module, MODULE_TOL))
end, "Checks that every dimension of the selected spaces / closed room boundaries is a multiple of the planning module (default 300mm)", {
    { name = "rooms",  type = "selection", prompt = "Select spaces or closed room boundaries",
      filter = "AEC_SPACE,LWPOLYLINE" },
    { name = "module", type = "distance", prompt = "Planning module", default = MODULE_DEFAULT },
})

-- Draws one or two clearance rectangles flush against a bed's long side(s)
-- (the side(s) you walk around / make the bed from). Two sides is the
-- common case for a double bed against a wall; pass only side1 for a bed
-- with one side against a wall. Same flush-attachment rule as the bathroom
-- session: the zone always touches the fixture, zero gap.
at.defineCommand("ATBEDCLEARANCE", function(p)
    at.ensureLayer("A-CLR-BED", { color = "cyan" })
    local bp = at.getProps(p.bed)
    if not bp or not bp.min or not bp.max then
        print("ATBEDCLEARANCE: could not read the bed's bounding box.")
        return
    end

    local function drawSide(side, depth)
        local x0, y0, x1, y1 = bp.min.x, bp.min.y, bp.max.x, bp.max.y
        local rx0, ry0, rx1, ry1
        if side == "North" then
            rx0, ry0, rx1, ry1 = x0, y1, x1, y1 + depth
        elseif side == "South" then
            rx0, ry0, rx1, ry1 = x0, y0 - depth, x1, y0
        elseif side == "East" then
            rx0, ry0, rx1, ry1 = x1, y0, x1 + depth, y1
        elseif side == "West" then
            rx0, ry0, rx1, ry1 = x0 - depth, y0, x0, y1
        else
            return nil
        end
        local rect = at.drawRect(rx0, ry0, 0, rx1, ry1, 0)
        at.setLayer(rect, "A-CLR-BED")
        return rect
    end

    local r1 = drawSide(p.side1, p.depth1)
    print(string.format("ATBEDCLEARANCE: %s drawn on A-CLR-BED, %s side, %.0fmm deep. %s",
        r1 or "(failed)", p.side1, p.depth1, UNVERIFIED))

    if p.side2 and p.side2 ~= "" then
        local r2 = drawSide(p.side2, p.depth2 or p.depth1)
        print(string.format("ATBEDCLEARANCE: %s drawn on A-CLR-BED, %s side, %.0fmm deep. %s",
            r2 or "(failed)", p.side2, p.depth2 or p.depth1, UNVERIFIED))
    end
end, "Draws walkway clearance flush against one or two sides of a bed (secondary-sourced default 700mm)", {
    { name = "bed",    type = "entity", prompt = "Select the bed" },
    { name = "side1",  type = "string", prompt = "First side", options = "North South East West" },
    { name = "depth1", type = "distance", prompt = "Clearance depth", default = 700 },
    { name = "side2",  type = "string", prompt = "Second side (blank = none)", options = "North South East West", optional = true },
    { name = "depth2", type = "distance", prompt = "Second side depth (blank = same as first)", optional = true },
})

-- Checks a room's net floor area against regional rental-housing-quality
-- minimums. NOT a general residential building code check -- see the file
-- header. jurisdiction selects which figures to use; occupants drives the
-- Brussels/Wallonia per-person scaling and the Flemish "lasting bond" >=18m2
-- rule for shared rooms.
at.defineCommand("ATROOMSIZECHECK", function(p)
    local roomProps = at.getProps(p.room)
    if not roomProps or not roomProps.area then
        print("ATROOMSIZECHECK: could not read the room's area.")
        return
    end
    local areaM2 = roomProps.area / 1000000 -- drawing units assumed mm -> m2

    -- Quito: per room type, area AND minimum side.
    if p.jurisdiction == "Quito" then
        -- Room type and bedroom count: as given, else the room's ATROOMTYPE tags.
        local roomType = p.roomType
        if roomType == nil or roomType == "" then roomType = at.getData(p.room, "roomType") end
        local rule = QUITO_ROOMS[roomType or ""]
        if not rule then
            print("ATROOMSIZECHECK: " .. (roomType and ("unknown room type '" .. tostring(roomType) .. "'")
                    or "no room type given and the room is not tagged (run ATROOMTYPE)")
                .. "; expected Living, Kitchen, MainBedroom, Bedroom2, Bedroom3, Bathroom, Laundry or ServiceBedroom.")
            return
        end
        local bedrooms = p.bedrooms or at.getData(p.room, "bedrooms")
        if type(bedrooms) ~= "number" then
            print("ATROOMSIZECHECK: no bedroom count given and the room is not tagged with one (run ATROOMTYPE).")
            return
        end
        local beds = math.floor(bedrooms)
        if beds < 1 then
            print("ATROOMSIZECHECK: the dwelling needs at least 1 bedroom.")
            return
        end
        local reqArea = rule.area[math.min(beds, 3)]
        if not reqArea then
            print(string.format("ATROOMSIZECHECK: a %d-bedroom dwelling has no %s in Art. 147.",
                beds, rule.label))
            return
        end
        -- Shorter side from the bounding box: exact for a rectangle aligned
        -- with the axes, only an approximation for other shapes.
        local shortM
        if roomProps.min and roomProps.max then
            shortM = math.min(roomProps.max.x - roomProps.min.x,
                              roomProps.max.y - roomProps.min.y) / 1000
        end
        -- With the 300mm module, the legal minimum side rounds up to the next
        -- multiple (e.g. 2.50m -> 2.70m).
        local modSide = moduleCeil(rule.side * 1000, MODULE_DEFAULT) / 1000
        local modOk, modText = moduleCheck(p.room, MODULE_DEFAULT)
        local areaOk = areaM2 >= reqArea
        local sideOk = shortM and shortM >= rule.side
        print(string.format(
            "ATROOMSIZECHECK: room %s (%s, %d-bedroom dwelling): area %.2fm2 vs min %.2fm2 -> %s; "
                .. "shorter side %s vs min %.2fm -> %s -> %s. %s",
            p.room, rule.label, beds, areaM2, reqArea, areaOk and "ok" or "BELOW",
            shortM and string.format("%.2fm", shortM) or "unknown", rule.side,
            shortM and (sideOk and "ok" or "BELOW") or "not checked",
            (areaOk and sideOk) and "meets" or "DOES NOT MEET", QUITO_SOURCE))
        if shortM then
            print("  (shorter side read from the bounding box: exact only for an axis-aligned rectangle)")
        end
        print(string.format("  300mm module: %s -> %s; modular minimum side %.2fm%s.",
            modText or "not checked", modOk == nil and "not checked" or (modOk and "on module" or "OFF MODULE"),
            modSide, (shortM and shortM + 1e-6 < modSide) and " -> BELOW" or ""))
        return
    end

    local required, basis
    if p.jurisdiction == "Flanders" then
        if p.occupants <= 1 then
            required, basis = 12.0, "Vlaamse Codex Wonen: single room >= 12m2"
        else
            required, basis = 18.0, "Vlaamse Codex Wonen: shared room (lasting bond) >= 18m2"
        end
    elseif p.jurisdiction == "Brussels" then
        required, basis = 18.0 + 10.0 * math.max(0, p.occupants - 1),
            string.format("Brussels 2026 rule: 18m2 + 10m2 per additional occupant (%d occupant(s))", p.occupants)
    elseif p.jurisdiction == "Wallonia" then
        required, basis = 15.0 + 5.0 * math.max(0, p.occupants - 1),
            string.format("Wallonia light-dwelling overcrowding rule: 15m2 + 5m2 per additional occupant (%d occupant(s))", p.occupants)
    else
        print("ATROOMSIZECHECK: unknown jurisdiction, expected Flanders, Brussels, Wallonia or Quito.")
        return
    end

    local status = (areaM2 >= required) and "meets" or "BELOW"
    print(string.format(
        "ATROOMSIZECHECK: room %s = %.2fm2, %s requires %.2fm2 (%s) -> %s. %s",
        p.room, areaM2, p.jurisdiction, required, basis, status, UNVERIFIED))
end, "Checks a room against a region's minimum: floor area (Belgian rental-housing rules, secondary-sourced) or area + shorter side per room type (Quito building code)", {
    { name = "room",        type = "entity", prompt = "Select the room boundary" },
    { name = "jurisdiction", type = "string", prompt = "Jurisdiction", options = "Flanders Brussels Wallonia Quito", default = "Flanders" },
    { name = "occupants",   type = "number", prompt = "Number of occupants", default = 1, conditional = true,
      description = "Flanders/Brussels/Wallonia only" },
    { name = "roomType",    type = "string", prompt = "Room type <from the room's tag>",
      options = "Living Kitchen MainBedroom Bedroom2 Bedroom3 Bathroom Laundry ServiceBedroom",
      optional = true, conditional = true,
      description = "Quito only; omit to use the room's ATROOMTYPE tag" },
    { name = "bedrooms",    type = "integer", prompt = "Bedrooms in the dwelling <from the room's tag>",
      optional = true, conditional = true,
      description = "Quito only: the dwelling's bedroom count, which sets the minimum area; omit to use the room's tag" },
})

-- Tags a room boundary (closed polyline or ACA space) with what the room
-- checks need, so they don't have to be typed again for every check:
-- roomType, dwelling (any id grouping rooms of one dwelling) and the
-- dwelling's bedroom count. Stored with at.setData, travels with the DWG.
at.defineCommand("ATROOMTYPE", function(p)
    local ok, err = at.setData(p.room, "roomType", p.roomType)
    if not ok then print("ATROOMTYPE: " .. tostring(err)); return end
    if p.dwelling and p.dwelling ~= "" then at.setData(p.room, "dwelling", p.dwelling) end
    if p.bedrooms then at.setData(p.room, "bedrooms", p.bedrooms) end

    local tags = at.getData(p.room) or {}
    local parts = {}
    for _, k in ipairs({ "roomType", "dwelling", "bedrooms" }) do
        if tags[k] ~= nil then parts[#parts + 1] = k .. "=" .. tostring(tags[k]) end
    end
    print("ATROOMTYPE: room " .. p.room .. " tagged " .. table.concat(parts, ", ") .. ".")
end, "Tags a room boundary or ACA space with its room type, dwelling and the dwelling's bedroom count (used by ATROOMSIZECHECK)", {
    { name = "room",     type = "entity", prompt = "Select the room boundary or space" },
    { name = "roomType", type = "string", prompt = "Room type",
      options = "Living Kitchen MainBedroom Bedroom2 Bedroom3 Bathroom Laundry ServiceBedroom",
      default = "MainBedroom" },
    { name = "dwelling", type = "string", prompt = "Dwelling id (blank = keep)", optional = true,
      description = "Any label shared by the rooms of one dwelling, e.g. A-101" },
    { name = "bedrooms", type = "integer", prompt = "Bedrooms in the dwelling (blank = keep)", optional = true },
})
