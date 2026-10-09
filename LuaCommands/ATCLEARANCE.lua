-- ATCLEARANCE: circulation/clearance helpers for residential layouts.
--
-- IMPORTANT: there is no single Belgian NBN standard for residential
-- circulation/fixture clearances. NBN S01-400-1 is an ACOUSTIC standard, not
-- circulation. Belgian accessibility rules are regional (Wallonia/Flanders/
-- Brussels) and target public buildings or the "accessible/adaptable
-- housing" category, not ordinary private residential construction.
-- The defaults below are WTCB/Buildwise-style COMFORT GUIDANCE (design
-- convention), not a legal/regulatory requirement. Treat ATCLEARANCECHECK's
-- output as a design-comfort check, never as proof of code compliance.
--
-- Workflow: generate zones with ATDOORSWINGZONE / ATFRONTCLEARANCE (drawn on
-- layer A-CLEAR), then run ATCLEARANCECHECK to flag overlaps against
-- fixtures (layer A-FIXT). Edit this file and run ATLUARELOAD, no rebuild
-- needed.
--
-- DESIGN DECISION (confirmed with the user 2026-10-07): ATCLEARANCECHECK
-- deliberately checks zone-vs-FIXTURE only, never zone-vs-zone. In a
-- single-occupancy space (a small residential bathroom, used by one person
-- at a time), a door-swing zone (transient -- only occupied while opening or
-- closing the door) may legitimately overlap a fixture's use-clearance zone
-- (simultaneous-use -- needed only while that fixture is in use), since the
-- two are never needed at the same instant. Flagging that as a clash would
-- be a false positive. A zone overlapping the physical FIXTURE itself is
-- still a real problem (the door would hit the sink) and IS checked. If
-- zone-vs-zone checking is ever added, it must keep this distinction:
-- transient zones (door swings) are allowed to overlap simultaneous-use
-- zones (fixture clearances); two simultaneous-use zones overlapping each
-- other (e.g. two fixtures' clearances facing each other) is still worth
-- flagging, since both could genuinely be needed at once.

local function ensureClearLayer()
    at.ensureLayer("A-CLEAR", { color = "magenta" })
end

-- Pie-slice zone swept by a door leaf, as a closed polyline (hinge -> arc end
-- -> arc start, with a bulge on the arc segment), so it can be boolean-tested
-- against fixtures like any other closed region.
at.defineCommand("ATDOORSWINGZONE", function(p)
    local hx, hy = p.hinge.x, p.hinge.y
    local hz = p.hinge.z or 0
    local r = p.width
    local a0 = math.rad(p.startDeg)
    local a1 = math.rad(p.startDeg + p.swingDeg)
    local x0, y0 = hx + r * math.cos(a0), hy + r * math.sin(a0)
    local x1, y1 = hx + r * math.cos(a1), hy + r * math.sin(a1)
    local bulge = math.tan(math.rad(p.swingDeg) / 4)

    ensureClearLayer()
    local zone = at.drawPolyline({
        { x = hx, y = hy },
        { x = x0, y = y0, bulge = bulge },
        { x = x1, y = y1 },
    }, true)
    at.setLayer(zone, "A-CLEAR")

    print(string.format(
        "ATDOORSWINGZONE: %s, swept zone %s drawn on A-CLEAR (comfort guidance, not a legal clearance).",
        zone, "r=" .. r))
end, "Draws the floor area swept by a door leaf, as a closed clearance zone", {
    { name = "hinge",    type = "point",    prompt = "Door hinge point" },
    { name = "width",    type = "distance", prompt = "Door leaf width (clear passage)", default = 800, base = "hinge" },
    { name = "startDeg", type = "number",   prompt = "Swing start angle (deg, 0 = along +X from hinge)", default = 0 },
    { name = "swingDeg", type = "number",   prompt = "Swing angle (deg, 90 = quarter turn)", default = 90 },
})

-- Generic "clear floor space" in front of any existing fixture, derived from
-- its bounding box plus which side faces the room. Works for a WC, a sink, a
-- shower, or anything else drawn as a closed rectangle.
at.defineCommand("ATFRONTCLEARANCE", function(p)
    local props = at.getProps(p.fixture)
    if not props or not props.min or not props.max then
        print("ATFRONTCLEARANCE: could not read the fixture's extents.")
        return
    end
    local x1, y1, x2, y2 = props.min.x, props.min.y, props.max.x, props.max.y
    local d = p.depth
    local rx1, ry1, rx2, ry2

    if p.side == "North" then
        rx1, ry1, rx2, ry2 = x1, y2, x2, y2 + d
    elseif p.side == "South" then
        rx1, ry1, rx2, ry2 = x1, y1 - d, x2, y1
    elseif p.side == "East" then
        rx1, ry1, rx2, ry2 = x2, y1, x2 + d, y2
    else -- West
        rx1, ry1, rx2, ry2 = x1 - d, y1, x1, y2
    end

    ensureClearLayer()
    local zone = at.drawRect(rx1, ry1, 0, rx2, ry2, 0)
    at.setLayer(zone, "A-CLEAR")

    print(string.format(
        "ATFRONTCLEARANCE: %s, %gmm clear zone drawn on the %s side of %s (comfort guidance, not a legal clearance).",
        zone, d, p.side, p.fixture))
end, "Draws a clear-floor-space rectangle on one side of an existing fixture", {
    { name = "fixture", type = "entity",   prompt = "Select the fixture",
      description = "Closed rectangle (WC, sink, shower, ...)" },
    { name = "side",    type = "keyword",  prompt = "Which side is the front",
      options = "North South East West", default = "North" },
    { name = "depth",   type = "distance", prompt = "Clearance depth", default = 600 },
})

-- Wraps a fixture with TWO nested clearance envelopes (minimum + recommended)
-- that share the fixture's back edge (the wall side) rather than projecting
-- from one face only, matching the WC clearance symbol from Tutt & Adler's
-- reference block (measured directly from the user's inserted "Inodoro"
-- block 2026-10-07 by exploding a disposable copy): outer/recommended
-- 1000x1400mm, inner/minimum 850x1100mm, both centered on the fixture's
-- width and flush with its back (wall) edge. Only the WC figure is verified
-- this way; sink/shower defaults below are NOT from a measured block (the
-- "B2P" block had no clearance envelope drawn) -- treat them as provisional
-- until a reference block confirms them.
at.defineCommand("ATENVELOPECLEARANCE", function(p)
    local fp = at.getProps(p.fixture)
    if not fp or not fp.min or not fp.max then
        print("ATENVELOPECLEARANCE: could not read the fixture's extents.")
        return
    end
    local x1, y1, x2, y2 = fp.min.x, fp.min.y, fp.max.x, fp.max.y
    local cx, cy = (x1 + x2) / 2, (y1 + y2) / 2

    local function envelope(width, depth)
        local hw = width / 2
        if p.side == "North" then      -- wall at max.y, envelope extends south (-Y)
            return cx - hw, y2 - depth, cx + hw, y2
        elseif p.side == "South" then  -- wall at min.y, envelope extends north (+Y)
            return cx - hw, y1, cx + hw, y1 + depth
        elseif p.side == "East" then   -- wall at max.x, envelope extends west (-X)
            return x2 - depth, cy - hw, x2, cy + hw
        else                           -- West: wall at min.x, envelope extends east (+X)
            return x1, cy - hw, x1 + depth, cy + hw
        end
    end

    ensureClearLayer()
    local ox1, oy1, ox2, oy2 = envelope(p.outerWidth, p.outerDepth)
    local outer = at.drawRect(ox1, oy1, 0, ox2, oy2, 0)
    at.setLayer(outer, "A-CLEAR")

    local inner = nil
    if p.innerWidth and p.innerDepth and p.innerWidth > 0 and p.innerDepth > 0 then
        local ix1, iy1, ix2, iy2 = envelope(p.innerWidth, p.innerDepth)
        inner = at.drawRect(ix1, iy1, 0, ix2, iy2, 0)
        at.setLayer(inner, "A-CLEAR")
    end

    print(string.format(
        "ATENVELOPECLEARANCE: outer(recommended) %s %gx%gmm%s, back=%s side of %s.",
        outer, p.outerWidth, p.outerDepth,
        inner and string.format(", inner(minimum) %s %gx%gmm", inner, p.innerWidth, p.innerDepth) or "",
        p.side, p.fixture))
end, "Wraps a fixture with nested minimum/recommended clearance envelopes sharing its back (wall) edge", {
    { name = "fixture",    type = "entity",   prompt = "Select the fixture" },
    { name = "side",       type = "keyword",  prompt = "Which side is against the wall (back of fixture)",
      options = "North South East West", default = "North" },
    { name = "outerWidth", type = "distance", prompt = "Outer (recommended) envelope width", default = 1000 },
    { name = "outerDepth", type = "distance", prompt = "Outer (recommended) envelope depth from wall", default = 1400 },
    { name = "innerWidth", type = "distance", prompt = "Inner (minimum) envelope width (0 to skip)",
      default = 850, optional = true },
    { name = "innerDepth", type = "distance", prompt = "Inner (minimum) envelope depth from wall (0 to skip)",
      default = 1100, optional = true },
})

-- Non-destructive clash check: every A-CLEAR zone against every fixture
-- (schematic LWPOLYLINE/CIRCLE on A-FIXT, or a real ACA plumbing fixture --
-- AEC_MVBLOCK_REF, any layer, typically P-Fixt). Uses polyBoolean
-- "intersect" purely as an overlap test, erasing the probe region/polygon it
-- creates immediately after reading the result, so the drawing is left
-- exactly as it was before the check.
--
-- Relies on the 2026-10-07 polyBoolean fix: nil + "empty result: the shapes
-- do not overlap" for disjoint input, and direct acceptance of any closed
-- curve (polyline, circle, ellipse, closed spline), not just polylines.
--
-- AEC_MVBLOCK_REF is a block reference, not a closed curve, so polyBoolean
-- can't take it directly -- it's approximated here as its bounding-box
-- rectangle. That's a real fixture, like an L-shaped vanity, could under-use
-- this (flagging a clash with empty corner space that isn't really there);
-- it will never MISS a genuine clash, since the bbox always contains the
-- true footprint.
--
-- Scope note: walls are a zero-thickness outline in this model, so zones are
-- not checked against A-WALL (a zone fully inside the room would always
-- register as "overlapping" the room outline, which is meaningless noise).
local function bboxToRect(handle)
    local p = at.getProps(handle)
    if not p or not p.min or not p.max then return nil end
    return at.drawRect(p.min.x, p.min.y, 0, p.max.x, p.max.y, 0)
end

at.defineCommand("ATCLEARANCECHECK", function()
    local zones, fixtures = {}, {}
    for _, h in ipairs(at.entities("LWPOLYLINE")) do
        local p = at.getProps(h)
        if p then
            if p.layer == "A-CLEAR" then table.insert(zones, h)
            elseif p.layer == "A-FIXT" then table.insert(fixtures, h) end
        end
    end
    -- Fixtures made of circles (e.g. a toilet bowl, a basin tap) also count.
    for _, h in ipairs(at.entities("CIRCLE")) do
        local p = at.getProps(h)
        if p and p.layer == "A-FIXT" then table.insert(fixtures, h) end
    end
    -- Real ACA plumbing fixtures (approximated by bounding box -- see above).
    local mvblockFixtures = {}
    for _, h in ipairs(at.entities("AEC_MVBLOCK_REF")) do
        table.insert(fixtures, h)
        table.insert(mvblockFixtures, h)
    end

    print(string.format("ATCLEARANCECHECK: %d clearance zone(s), %d fixture(s) (%d real ACA fixture(s)).",
        #zones, #fixtures, #mvblockFixtures))

    local clashes = 0
    for _, z in ipairs(zones) do
        for _, f in ipairs(fixtures) do
            local testF, tempF = f, false
            local fp = at.getProps(f)
            if fp and fp.type == "AEC_MVBLOCK_REF" then
                testF = bboxToRect(f)
                tempF = true
                if not testF then goto continue end
            end

            local region = at.polyBoolean(z, testF, "intersect")
            if region then
                clashes = clashes + 1
                print(string.format("CLASH: clearance zone %s overlaps fixture %s", z, f))
                at.erase(region)
            end
            if tempF then at.erase(testF) end
            ::continue::
        end
    end

    if clashes == 0 then
        print("No clashes found (comfort-guidance check only, not a code-compliance check).")
    else
        print(string.format("%d clash(es) found.", clashes))
    end
end, "Checks A-CLEAR clearance zones against fixtures (schematic or real ACA) for overlaps")
