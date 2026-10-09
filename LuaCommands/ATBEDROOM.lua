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

local UNVERIFIED = "[SECONDARY-SOURCED FIGURE -- confirm current regional housing-quality rules before relying on this]"

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
        print("ATROOMSIZECHECK: unknown jurisdiction, expected Flanders, Brussels or Wallonia.")
        return
    end

    local status = (areaM2 >= required) and "meets" or "BELOW"
    print(string.format(
        "ATROOMSIZECHECK: room %s = %.2fm2, %s requires %.2fm2 (%s) -> %s. %s",
        p.room, areaM2, p.jurisdiction, required, basis, status, UNVERIFIED))
end, "Checks a room's floor area against a region's rental-housing-quality minimum (secondary-sourced)", {
    { name = "room",        type = "entity", prompt = "Select the room boundary" },
    { name = "jurisdiction", type = "string", prompt = "Jurisdiction", options = "Flanders Brussels Wallonia", default = "Flanders" },
    { name = "occupants",   type = "number", prompt = "Number of occupants", default = 1 },
})
