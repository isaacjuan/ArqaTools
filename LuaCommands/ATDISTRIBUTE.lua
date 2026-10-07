-- Distribute commands, on top of at.distribute / at.distributeCopies
-- (DistributeTools::DistributeObjects / DistributeCopies):
--   ATDISTLINE / ATDISTBETWEEN / ATDISTEQUAL              move selected objects
--   ATDISTCOPYLINE / ATDISTCOPYBETWEEN / ATDISTCOPYEQUAL  place N copies of one object
--   ATDISTTOLINE                                          N copies along a picked curve
-- Modes: linear = on both endpoints, between = endpoints excluded,
-- equal = half a gap at each end. Grouped objects move as a whole.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds several
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local MIN = { linear = 2, between = 1, equal = 1 }

-- Gap between neighbours for n items over distance d (DistributeTools::ModeSpacing).
local function spacing(mode, n, d)
    if mode == "linear" then return d / (n - 1) end
    if mode == "between" then return d / (n + 1) end
    return d / n
end

local function distance(a, b)
    return math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2 + (b.z - a.z) ^ 2)
end

local function spacingText(mode, s)
    if mode == "equal" then return string.format("equal spacing %.2f (%.2f from ends)", s, s / 2) end
    if mode == "between" then return string.format("spacing %.2f between points", s) end
    return string.format("spacing %.2f", s)
end

local LINE_PARAMS = {
    { name = "from",  type = "point", prompt = "Specify start point", description = "Start of the distribution line" },
    { name = "to",    type = "point", prompt = "Specify end point",   description = "End of the distribution line" },
}

local function withLine(params)
    for _, p in ipairs(LINE_PARAMS) do params[#params + 1] = p end
    return params
end

local function copyDistribute(name, mode, source, count, a, b)
    if not MIN[mode] then print(name .. ": unknown mode " .. tostring(mode)); return end
    if not count or count < MIN[mode] then
        print(string.format("%s: need at least %d copies.", name, MIN[mode]))
        return
    end
    local copies, err = at.distributeCopies(source, count, a.x, a.y, a.z, b.x, b.y, b.z, mode)
    if not copies then print(name .. ": " .. err); return end
    print(string.format("%s: %d copies placed, %s.", name, #copies, spacingText(mode, spacing(mode, count, distance(a, b)))))
end

-- ATDIST<LINE|BETWEEN|EQUAL>: move the selected objects.
local function defineDistribute(name, mode, what)
    at.defineCommand(name, function(p)
        local n, err = at.distribute(p.objects, p.from.x, p.from.y, p.from.z, p.to.x, p.to.y, p.to.z, mode)
        if not n then print(name .. ": " .. err); return end
        print(string.format("%s: distributed %d object(s), %s.", name, n, spacingText(mode, spacing(mode, n, distance(p.from, p.to)))))
    end, "Distributes objects " .. what .. " (grouped objects move as a whole)", withLine({
        { name = "objects", type = "selection", prompt = "Select objects", description = "Objects to distribute" },
    }))
end

-- ATDISTCOPY<LINE|BETWEEN|EQUAL>: place N copies of one object.
local function defineCopyDistribute(name, mode, what)
    at.defineCommand(name, function(p)
        copyDistribute(name, mode, p.source, p.count, p.from, p.to)
    end, "Copies one object N times " .. what, withLine({
        { name = "source", type = "entity",  prompt = "Select source object", description = "Object to copy" },
        { name = "count",  type = "integer",
          prompt = mode == "linear" and "Number of copies (total)" or "Number of copies",
          description = "At least " .. MIN[mode] },
    }))
end

defineDistribute("ATDISTLINE",    "linear",  "evenly along a line (on both endpoints)")
defineDistribute("ATDISTBETWEEN", "between", "between two points (endpoints excluded)")
defineDistribute("ATDISTEQUAL",   "equal",   "with equal spacing (half a gap at each end)")

defineCopyDistribute("ATDISTCOPYLINE",    "linear",  "along a line (endpoints included)")
defineCopyDistribute("ATDISTCOPYBETWEEN", "between", "between two points (endpoints excluded)")
defineCopyDistribute("ATDISTCOPYEQUAL",   "equal",   "with equal spacing (half a gap at each end)")

-- ATDISTTOLINE: N copies along a picked line or curve (its start to its end).
at.defineCommand("ATDISTTOLINE", function(p)
    local props = at.getProps(p.line)
    if not props or not props.startPoint then
        print("ATDISTTOLINE: that object is not a line or curve.")
        return
    end
    copyDistribute("ATDISTTOLINE", p.mode:lower(), p.source, p.count, props.startPoint, props.endPoint)
end, "Copies one object N times along a picked line or curve (start to end)", {
    { name = "source", type = "entity",  prompt = "Select source object to copy", description = "Object to copy" },
    { name = "line",   type = "entity",  prompt = "Select line to align along",   description = "Line or curve; its start and end points are used" },
    { name = "mode",   type = "keyword", prompt = "Mode", options = "Linear Between Equal", default = "Linear",
      description = "Linear (ends included), Between (ends excluded) or Equal (half gap at ends)" },
    { name = "count",  type = "integer", prompt = "Number of copies", description = "At least 2 for Linear, else 1" },
})
