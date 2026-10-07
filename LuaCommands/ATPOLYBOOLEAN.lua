-- ATBOOLPOLY / ATSUBPOLY / ATINPOLY / ATUNIONPOLY: boolean of two closed
-- curves (polylines, circles, ellipses, closed splines). The result is a new
-- green region (color 3) on the current layer in model space; the originals
-- are left untouched. Subtract keeps the first minus the second. An empty
-- result (no overlap) is reported, not drawn. The geometry is at.polyBoolean
-- (PolylineTools::BooleanPolylines), which also checks the input types.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds four
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local OPS = {
    Union        = { op = "union",     label = "Union" },
    Intersection = { op = "intersect", label = "Intersection" },
    Subtract     = { op = "subtract",  label = "Subtract" },
}

local function runBoolean(name, opName, first, second)
    local o = OPS[opName]
    local region, err = at.polyBoolean(first, second, o.op)
    if not region then print(string.format("%s: %s (%s).", name, err, o.label)); return end
    print(o.label .. " operation completed successfully!")
end

local OPERANDS = {
    { name = "first",  type = "entity", prompt = "Select first closed curve",
      description = "Closed polyline, circle, ellipse or spline (for subtract: the one kept)" },
    { name = "second", type = "entity", prompt = "Select second closed curve",
      description = "Closed polyline, circle, ellipse or spline (for subtract: the one removed)" },
}

local function defineFixed(name, opName, description)
    at.defineCommand(name, function(p)
        runBoolean(name, opName, p.first, p.second)
    end, description, OPERANDS)
end

defineFixed("ATSUBPOLY",   "Subtract",     "Subtracts the second closed curve from the first (new region, originals kept)")
defineFixed("ATINPOLY",    "Intersection", "Intersection of two closed curves (new region, originals kept)")
defineFixed("ATUNIONPOLY", "Union",        "Union of two closed curves (new region, originals kept)")

at.defineCommand("ATBOOLPOLY", function(p)
    runBoolean("ATBOOLPOLY", p.operation, p.first, p.second)
end, "Boolean of two closed curves: Union, Intersection or Subtract (new region, originals kept)", {
    { name = "operation", type = "keyword", options = "Union Intersection Subtract",
      prompt = "Enter operation [Union/Intersection/Subtract]",
      description = "Union, Intersection, or Subtract (1st - 2nd)" },
    OPERANDS[1],
    OPERANDS[2],
})
