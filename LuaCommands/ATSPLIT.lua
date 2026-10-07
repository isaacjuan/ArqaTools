-- Split commands, on top of at.splitLine / at.splitPolyline
-- (AreaTools::SplitLine / SplitPolyline):
--   ATSPLITLINE  split a line (any curve) at its intersections into line segments
--   ATSPLITPOLI  split a polyline at its intersections, keeping arc segments
-- The base is erased; the segments go on layer doc_areas, each with an
-- auto-updating length label (AreaTools::InsertLengthLabel).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds several
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local LAYER = "doc_areas"

-- isBase(props) -> true when the picked base is usable; badBase is the message otherwise.
local function defineSplit(name, split, what, isBase, badBase, basePrompt)
    at.defineCommand(name, function(p)
        local props = at.getProps(p.base)
        if not props or not isBase(props) then print(name .. ": " .. badBase .. "."); return end
        if not p.crossing or #p.crossing == 0 then print(name .. ": cancelled."); return end

        local segs, err = split(p.base, p.crossing, true, LAYER)
        if not segs then print(name .. ": " .. err .. "."); return end
        print(string.format("%d segment(s) created and tagged.", #segs))
    end, what, {
        { name = "base", type = "entity", prompt = basePrompt, description = "Curve to split; it is erased" },
        { name = "crossing", type = "selection", prompt = "Select crossing lines/polylines", optional = true,
          description = "Objects whose intersections with the base are the split points" },
    })
end

-- at.* is only usable while a command runs, not while this file loads,
-- hence the wrappers.
defineSplit("ATSPLITLINE", function(...) return at.splitLine(...) end,
    "Splits a line at its intersections into length-tagged line segments",
    function(props) return props.startPoint ~= nil end, "cannot open base line",
    "Select base line to split")

defineSplit("ATSPLITPOLI", function(...) return at.splitPolyline(...) end,
    "Splits a polyline at its intersections into length-tagged segments, keeping arcs",
    function(props) return props.type == "LWPOLYLINE" end, "selected object is not a polyline",
    "Select base polyline to split")
