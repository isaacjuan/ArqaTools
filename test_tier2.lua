-- test_tier2.lua - exercises the Tier 2 at.* bindings
-- Run with: ATLUA  @<path>\test_tier2.lua

local x, y, z = at.getPoint("Base point for the test sheet: ")
if not x then return end
local s = at.getDistance("Module size: ", x, y, z) or 1000

local made = {}                                   -- everything drawn, for the SVG export
local function keep(h) if h then made[#made + 1] = h end return h end
local function keepAll(list) for _, h in ipairs(list or {}) do keep(h) end return list end

-- Layers ---------------------------------------------------------------------
local previous = at.getCurrentLayer()
assert(at.setCurrentLayer("LUA-T2"))
at.ensureLayer("LUA-T2-HIDDEN")
local hidden = at.drawCircle(x - s, y, z, s / 4)
at.setLayer(hidden, "LUA-T2-HIDDEN")
assert(at.setLayerState("LUA-T2-HIDDEN", { off = true }))
local ok, err = at.setLayerState("LUA-T2", { frozen = true })
print("freeze current layer refused: " .. tostring(err))
print(#at.layers() .. " layers, current = " .. at.getCurrentLayer())

-- Distribute -----------------------------------------------------------------
local dots = {}
for i, dx in ipairs({ 0.1, 2.7, 0.9, 1.6 }) do
    dots[i] = keep(at.drawCircle(x + dx * s, y + s * (0.2 + 0.1 * i), z, s / 10))
end
print("distributed: " .. at.distribute(dots, x, y, z, x + 3 * s, y, z, "linear"))
local box = keep(at.drawRect(x, y - s, z, x + s / 5, y - s * 0.8, z))
local copies = keepAll(at.distributeCopies(box, 4, x, y - s, z, x + 3 * s, y - s, z, "equal"))
print("copies: " .. #copies)

-- Text -----------------------------------------------------------------------
local tx = x + 4 * s
local t1 = keep(at.drawText(tx, y,         z, "10",    s / 8))
local t2 = keep(at.drawText(tx, y - s / 3, z, "20.5",  s / 12))
local t3 = keep(at.drawText(tx, y - s * 2 / 3, z, "1,000", s / 12))
local sum, valid, skipped = at.sumText({ t1, t2, t3, box })
print(string.format("sum %.2f (%d valid, %d skipped)", sum, valid, skipped))
at.setText(t1, "15")
print("t1 now: " .. at.getText(t1))
print("styled: " .. at.copyTextStyle(t1, { t2, t3 }, true))
print("scaled: " .. at.scaleText({ t2, t3 }, 0.5))

-- Linked labels --------------------------------------------------------------
local room = keep(at.drawRect(x, y + s, z, x + 2 * s, y + 2 * s, z))
print("area label:      " .. tostring(keep(at.areaLabel(room))))
print("perimeter label: " .. tostring(keep(at.perimeterLabel(room))))
local kitchen = keep(at.drawRect(x + 2.5 * s, y + s, z, x + 4 * s, y + 2 * s, z))
print("room tag:        " .. tostring(keep(at.roomTag(kitchen, "Kitchen"))))
local wall = keep(at.drawLine(x, y + 2.5 * s, z, x + 4 * s, y + 2.5 * s, z))
print("length label:    " .. tostring(keep(at.lengthLabel(wall))))
local lbl, total = at.sumLengthLabel({ wall, room, kitchen }, x + 2 * s, y + 3 * s, z)
keep(lbl)
print("sum length:      " .. tostring(lbl) .. " = " .. at.formatLength(total or 0))
print("label on open line (expected error): " .. select(2, at.areaLabel(wall)))

-- Split ----------------------------------------------------------------------
local base = at.drawLine(x, y + 4 * s, z, x + 4 * s, y + 4 * s, z)
local cuts = {}
for i = 1, 3 do
    cuts[i] = keep(at.drawLine(x + i * s, y + 3.5 * s, z, x + i * s, y + 4.5 * s, z))
end
print("line segments: " .. #keepAll(at.splitLine(base, cuts)))
local arcPoly = at.drawPolyline({ { x = x, y = y + 5 * s, bulge = 0.3 },
                                  { x = x + 4 * s, y = y + 5 * s } }, false)
-- bulge 0.3 sags the arc 0.6*s below its chord (to y + 4.4*s at mid-span), so cut from below that
local vcut = keep(at.drawLine(x + 2 * s, y + 4 * s, z, x + 2 * s, y + 6 * s, z))
print("polyline segments: " .. #keepAll(at.splitPolyline(arcPoly, { vcut }, false)))

-- Blocks ---------------------------------------------------------------------
local n = 0
for name, count in pairs(at.countBlocks()) do
    print("block " .. name .. ": " .. count)
    n = n + 1
end
if n == 0 then print("no named blocks in model space") end

-- SVG export -----------------------------------------------------------------
local path, exported, skippedSvg = at.exportSvg(made, "lua_tier2_test")
print("svg: " .. tostring(path) .. " (" .. tostring(exported) .. " exported, " .. tostring(skippedSvg) .. " skipped)")
print("bad file name (expected error): " .. select(2, at.exportSvg(made, "..\\evil")))

at.setCurrentLayer(previous)
