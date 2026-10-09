-- ATSITE: interior-exterior relationship helpers (site boundary, windows,
-- view-distance and daylighting checks).
--
-- *** EVERY THRESHOLD BELOW IS AN UNVERIFIED PLACEHOLDER, NOT CONFIRMED LAW ***
--
-- View distance (ATVIEWCHECK default 1900mm): based on the OLD French-style
-- Civil Code arts. 678-680 (1.90m direct view / 0.60m oblique view from the
-- exterior wall face to the property line). Belgium reformed property-
-- neighbor-relations law via the new Book 3 of the Civil Code (law of
-- 4 Feb 2020, in force since 1 Sept 2021), which overhauled nuisance-window
-- rules. One 2026 source says 1.90m is still the reference figure but that
-- the old direct/oblique split "has been addressed" by the reform -- meaning
-- the CURRENT Belgian rule's structure may differ from what is encoded here.
-- This tool does NOT distinguish direct vs oblique; it checks one flat
-- distance only, which is almost certainly an oversimplification of
-- whatever the current rule actually is.
--
-- ATDAYLIGHTCHECK moved to ATROOMCHECKS.lua (2026-10-09): it now uses each
-- ACA window's width x height per room (the old version here summed plan-view
-- bounding boxes, i.e. width x wall thickness, over the whole drawing). Its
-- default 0.20 is also the Quito Art. 69 figure. Brussels background:
-- Daylighting ratio (0.20 = 1/5): based on Brussels
-- RRU Titre II Art. 10 ("eclairement naturel"), sourced only from permit
-- decisions citing a 1/5-of-habitable-floor-area figure in practice -- the
-- actual regulation text was not confirmed. Wallonia/Flanders PEB do not use
-- a daylighting ratio at all (glazing there is regulated for overheating /
-- solar gain, a different calculation entirely); do not reuse this ratio
-- outside Brussels.
--
-- CONFIRM BOTH FIGURES against the current legal text (ejustice.just.fgov.be
-- for the Civil Code, urbanisme.irisnet.be for the RRU) or with a project
-- architect/notary before relying on either check for anything other than
-- an early, rough design-stage flag. Edit this file and run ATLUARELOAD, no
-- rebuild needed.

local UNVERIFIED = "[UNVERIFIED FIGURE -- confirm current Belgian law before relying on this]"

local function ensureSiteLayers()
    at.ensureLayer("A-SITE", { color = "red" })
    at.ensureLayer("A-WINDOW", { color = "blue" })
end

-- Property boundary + a simple north arrow. northDeg follows the drawing's
-- own math convention (0 = +X, 90 = +Y, counter-clockwise), NOT compass
-- bearing, so the default of 90 points "up" in a plan view laid out with
-- +Y as the top of the page.
at.defineCommand("ATSITEBOUNDARY", function(p)
    ensureSiteLayers()
    local pts = {}
    for _, pt in ipairs(p.corners) do
        table.insert(pts, { x = pt.x, y = pt.y })
    end
    local boundary = at.drawPolyline(pts, true)
    at.setLayer(boundary, "A-SITE")

    -- North arrow: a fixed-length indicator near the boundary's bounding box.
    local minX, minY, maxX, maxY = pts[1].x, pts[1].y, pts[1].x, pts[1].y
    for _, pt in ipairs(pts) do
        minX, maxX = math.min(minX, pt.x), math.max(maxX, pt.x)
        minY, maxY = math.min(minY, pt.y), math.max(maxY, pt.y)
    end
    local ax, ay = maxX + 500, maxY
    local len = 500
    local a = math.rad(p.northDeg)
    local tipX, tipY = ax + len * math.cos(a), ay + len * math.sin(a)
    local arrow = at.drawLine(ax, ay, 0, tipX, tipY, 0)
    at.setLayer(arrow, "A-SITE")
    local label = at.drawText(tipX, tipY, 0, "N", 150)
    at.setLayer(label, "A-SITE")

    print(string.format("ATSITEBOUNDARY: %s, %d-point property boundary drawn on A-SITE, north at %gdeg.",
        boundary, #pts, p.northDeg))
end, "Draws a property boundary polyline and a north arrow", {
    { name = "corners",  type = "points", prompt = "Property boundary corners (3+ points)" },
    { name = "northDeg", type = "number", prompt = "North direction (deg, 0=+X, 90=+Y, CCW)", default = 90 },
})

-- A window/opening, drawn as a plain rectangle on A-WINDOW. Deliberately
-- minimal (no sill height, no wall association) -- just enough geometry for
-- the two checks below.
at.defineCommand("ATWINDOWADD", function(p)
    ensureSiteLayers()
    local win = at.drawRect(p.corner1.x, p.corner1.y, 0, p.corner2.x, p.corner2.y, 0)
    at.setLayer(win, "A-WINDOW")
    print(string.format("ATWINDOWADD: %s drawn on A-WINDOW.", win))
end, "Draws a window/opening rectangle on layer A-WINDOW", {
    { name = "corner1", type = "point", prompt = "First corner" },
    { name = "corner2", type = "point", prompt = "Opposite corner", base = "corner1" },
})

local function distPointToSegment(px, py, x1, y1, x2, y2)
    local dx, dy = x2 - x1, y2 - y1
    local len2 = dx * dx + dy * dy
    if len2 == 0 then
        local ddx, ddy = px - x1, py - y1
        return math.sqrt(ddx * ddx + ddy * ddy)
    end
    local t = ((px - x1) * dx + (py - y1) * dy) / len2
    t = math.max(0, math.min(1, t))
    local cx, cy = x1 + t * dx, y1 + t * dy
    local ddx, ddy = px - cx, py - cy
    return math.sqrt(ddx * ddx + ddy * ddy)
end

-- Minimum distance from a point to a closed polyline's edges. Arcs (bulge)
-- are treated as straight chords -- a known simplification, fine for mostly-
-- straight property boundaries, not exact for curved ones.
local function minDistToBoundary(px, py, verts)
    local n = #verts
    local best = math.huge
    for i = 1, n do
        local a = verts[i]
        local b = verts[(i % n) + 1]
        local d = distPointToSegment(px, py, a.x, a.y, b.x, b.y)
        if d < best then best = d end
    end
    return best
end

-- Collects windows from both representations: the plain ATWINDOWADD
-- rectangle (layer A-WINDOW) and a real AutoCAD Architecture AEC_WINDOW
-- object (any layer -- ACA defaults new windows to A-Glaz, not A-WINDOW).
local function collectWindows()
    local windows = {}
    for _, h in ipairs(at.entities("LWPOLYLINE")) do
        local props = at.getProps(h)
        if props and props.layer == "A-WINDOW" then table.insert(windows, h) end
    end
    for _, h in ipairs(at.entities("AEC_WINDOW")) do
        table.insert(windows, h)
    end
    return windows
end

-- Checks every window's distance to the nearest A-SITE boundary edge, using
-- the window's center point as a stand-in for its exterior face (a
-- simplification -- the real Civil Code measurement point is the wall's
-- exterior parement, not the window center).
at.defineCommand("ATVIEWCHECK", function(p)
    local windows = collectWindows()
    local boundaries = {}
    for _, h in ipairs(at.entities("LWPOLYLINE")) do
        local props = at.getProps(h)
        if props and props.layer == "A-SITE" and props.closed then table.insert(boundaries, h) end
    end

    print(string.format("ATVIEWCHECK: %d window(s), %d site boundary(ies). %s",
        #windows, #boundaries, UNVERIFIED))

    if #boundaries == 0 then
        print("ATVIEWCHECK: no A-SITE boundary found, nothing to check against.")
        return
    end

    for _, w in ipairs(windows) do
        local wp = at.getProps(w)
        local cx, cy = (wp.min.x + wp.max.x) / 2, (wp.min.y + wp.max.y) / 2
        local best = math.huge
        for _, b in ipairs(boundaries) do
            local bp = at.getProps(b)
            local d = minDistToBoundary(cx, cy, bp.vertices)
            if d < best then best = d end
        end
        local status = (best < p.minDistance) and "BELOW threshold" or "ok"
        print(string.format("  %s: %.0fmm to nearest boundary (%s, threshold %.0fmm). %s",
            w, best, status, p.minDistance, UNVERIFIED))
    end
end, "Checks A-WINDOW openings against A-SITE boundary distance (placeholder threshold)", {
    { name = "minDistance", type = "distance", prompt = "Minimum distance to check against", default = 1900 },
})

