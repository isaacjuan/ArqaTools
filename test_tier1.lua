-- test_tier1.lua - exercises the Tier 1 at.* bindings
-- Run with: ATLUA  @<path>\test_tier1.lua

local x, y, z = at.getPoint("Base point for the test sheet: ")
if not x then return end
local s = at.getDistance("Module size: ", x, y, z) or 1000

at.ensureLayer("LUA-T1", { color = "#2E86C1", lineweight = 0.35, description = "Tier 1 test" })

-- Polyline with an arc segment, plus labels
local pl = at.drawPolyline({ {x = x, y = y}, {x = x + 2*s, y = y, bulge = 0.5},
                             {x = x + 2*s, y = y + s}, {x = x, y = y + s} }, true)
at.setLayer(pl, "LUA-T1")
local p = at.getProps(pl)
at.drawText(x + s, y + s / 2, z, at.formatArea(p.area), s / 10)
at.drawMText(x + s, y - s / 4, z, "Perimeter " .. at.formatLength(p.length), s / 12)

-- Boolean: cut a notch out of the left edge, then turn the region back into a polyline.
-- (The square overlaps the edge on purpose: a fully enclosed square would leave a
-- region with a hole, and regionToPolyline only follows one loop.)
local hole = at.drawRect(x - s / 4, y + s / 4, z, x + s / 2, y + s * 3 / 4, z)
local region, err = at.polyBoolean(pl, hole, "subtract")
if region then
    local outline = at.regionToPolyline(region)
    at.moveEntity(outline, 0, -2 * s, 0)
    print("region " .. region .. " -> polyline " .. tostring(outline))
else
    print("boolean failed: " .. err)
end

-- Patterns along a row
local cx = x + 4 * s
local r = s / 2
print("rosette:  " .. #at.patternRosette(cx, y, z, r / 2, 8) .. " entities")
print("star:     " .. #at.patternStar(cx + 1.5 * s, y, z, r, 12, 0.5) .. " entities")
print("petals:   " .. #at.patternPetals(cx + 3 * s, y, z, r, 6) .. " entities")
print("geometric:" .. #at.patternGeometric(cx + 4.5 * s, y, z, r, 8) .. " entities")
print("hoja:     " .. #at.patternHojaNazari(cx, y + 3 * s, z, s / 3, 2) .. " entities")
print("arabesco: " .. #at.patternArabescoRl(cx + 2 * s, y + 2 * s, z, s / 10, 2, 2) .. " entities")
print("spiral:   " .. #at.goldenSpiral(x, y + 2 * s, z, x + 2 * s, y + 2 * s, z) .. " entities")

-- Sequence numbers, then align them on one Y
local nums = {}
for i = 1, 5 do
    local t = at.seqNumber(x + i * s / 2, y - s - i * s / 10, z, tostring(i), s / 15, true)
    nums[#nums + 1] = t
end
print(at.alignTo(nums, "y", y - s) .. " numbers aligned")
