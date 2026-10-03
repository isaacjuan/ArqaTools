-- test_foundations.lua - exercises the at.* input / query / edit API
-- Run with: ATLUA  @<path>\test_foundations.lua

-- 1. Interactive input: a circle grid at a picked point
local x, y, z = at.getPoint("Grid origin: ")
if not x then return end                       -- Enter = nothing to do
local rows    = at.getInt("Rows", 3)
local cols    = at.getInt("Columns", 4)
local spacing = at.getDistance("Spacing: ", x, y, z) or 20
local radius  = at.getReal("Radius", spacing / 4)

for r = 0, rows - 1 do
    for c = 0, cols - 1 do
        local h = at.drawCircle(x + c * spacing, y + r * spacing, z, radius)
        at.setLayer(h, "LUA-GRID")
        at.setColor(h, (r + c) % 2 == 0 and 1 or 5)
    end
end

-- 2. Query: report every polyline in the drawing
for _, h in ipairs(at.entities("LWPOLYLINE")) do
    local p = at.getProps(h)
    print(string.format("%s on %s: %d vertices, length %.2f%s",
        h, p.layer, #p.vertices, p.length or 0,
        p.area and string.format(", area %.2f", p.area) or ""))
end

-- 3. Selection + edit: optionally erase the short lines in a selection
local sel = at.getSelection("Select lines to clean up: ", "LINE")
if #sel > 0 then
    local minLen = at.getDistance("Erase lines shorter than: ")
    local answer = at.getKeyword("Really erase? [Yes/No]", "Yes No", "No")
    local erased = 0
    if minLen and answer == "Yes" then
        for _, h in ipairs(sel) do
            if at.getProps(h).length < minLen and at.erase(h) then
                erased = erased + 1
            end
        end
    end
    print(erased .. " of " .. #sel .. " lines erased")
end
