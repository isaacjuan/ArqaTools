-- Golden-ratio rectangles (GoldenRectTools):
--   ATGOLDENRECT     spiral of 6 golden rectangles, first side corner -> point
--                    (at.goldenSpiral = GoldenRectTools::DrawGoldenSpiral)
--   ATGOLDENRECTIN   golden rectangles placed inside a rectangular container
--   ATGOLDENRECTINW  the same with a custom proportion (inner / short side)
-- The container commands span the container's short side and slide along its
-- long side to each picked point (Enter finishes); that part is at.rectFrame /
-- at.rectInFrame (GoldenRectTools::ReadRectFrame / DrawRectInFrame).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds three
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local PHI = 1.618033988749895

-- ── ATGOLDENRECT ────────────────────────────────────────────────────────────
at.defineCommand("ATGOLDENRECT", function(p)
    local a, b = p.corner, p.sidePt
    local dx, dy = b.x - a.x, b.y - a.y
    if math.sqrt(dx * dx + dy * dy) < 0.001 then print("ATGOLDENRECT: Points are too close."); return end
    local ids = at.goldenSpiral(a.x, a.y, a.z, b.x, b.y, b.z)
    print(string.format("%d golden rectangles drawn, spiraling inward.", #ids))
end, "Draws a golden-ratio rectangle spiral", {
    { name = "corner", type = "point", prompt = "Golden rectangle start corner" },
    { name = "sidePt", type = "point", prompt = "Endpoint defining first side",
      description = "End of the first (long) side, from the start corner" },
})

-- C++ _tstof: leading number, else 0 ("0.5abc" -> 0.5, "" -> 0).
local function parseNumber(s)
    return tonumber(s:match("^%s*([+-]?%d*%.?%d*)")) or 0
end

local function placeRects(name, container, golden)
    local frame, err = at.rectFrame(container)
    if not frame then print(name .. ": " .. err); return end

    local innerWidth, proportion
    if golden then
        innerWidth = frame.shortLen / PHI
    else
        local s = at.getString("Proportion (0-1, e.g. 1=square, 0.5=half height)")
        if not s then print(name .. ": Cancelled."); return end
        proportion = parseNumber(s)
        if proportion <= 0 or proportion > 1 then
            print(name .. ": Proportion must be between 0 and 1."); return
        end
        innerWidth = frame.shortLen * proportion
    end

    local prompt = golden and "Pick location for golden rectangle" or "Pick location for inner rectangle"
    local count = 0
    while true do
        local x, y, z = at.getPoint(prompt)
        if not x then break end
        local h, drawErr = at.rectInFrame(container, innerWidth, x, y, z)
        if not h then print(name .. ": " .. drawErr); break end
        count = count + 1
    end

    if golden then
        print(string.format("%d golden rectangle(s) (%.2f x %.2f) drawn inside %.0f x %.0f container.",
                            count, frame.shortLen, innerWidth, frame.width, frame.height))
    else
        print(string.format("%d rectangle(s) (%.2f x %.2f, proportion %.3f) drawn inside %.0f x %.0f container.",
                            count, frame.shortLen, innerWidth, proportion, frame.width, frame.height))
    end
end

local function containerParams()
    return { { name = "container", type = "entity", prompt = "Select containing rectangle",
               description = "Closed 4-vertex rectangular polyline" } }
end

-- ── ATGOLDENRECTIN ──────────────────────────────────────────────────────────
at.defineCommand("ATGOLDENRECTIN", function(p)
    placeRects("ATGOLDENRECTIN", p.container, true)
end, "Places golden rectangles inside a container (picked points, Enter finishes)", containerParams())

-- ── ATGOLDENRECTINW ─────────────────────────────────────────────────────────
at.defineCommand("ATGOLDENRECTINW", function(p)
    placeRects("ATGOLDENRECTINW", p.container, false)
end, "Places custom-proportion rectangles inside a container (picked points, Enter finishes)", containerParams())
