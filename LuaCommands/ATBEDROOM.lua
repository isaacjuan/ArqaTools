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
        local rule = QUITO_ROOMS[p.roomType]
        if not rule then
            print("ATROOMSIZECHECK: unknown room type, expected Living, Kitchen, MainBedroom, "
                .. "Bedroom2, Bedroom3, Bathroom, Laundry or ServiceBedroom.")
            return
        end
        local beds = math.floor(p.bedrooms)
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
    { name = "roomType",    type = "string", prompt = "Room type",
      options = "Living Kitchen MainBedroom Bedroom2 Bedroom3 Bathroom Laundry ServiceBedroom",
      default = "MainBedroom", conditional = true, description = "Quito only" },
    { name = "bedrooms",    type = "integer", prompt = "Bedrooms in the dwelling", default = 1, conditional = true,
      description = "Quito only: the dwelling's bedroom count, which sets the minimum area" },
})
