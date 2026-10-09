-- ATBATHROOMDEMO: draws a simple schematic bathroom layout (1.5 x 2.5 m) with
-- a corner shower, a toilet and a wall-mounted washbasin, plus a door swing.
-- Dimensions in mm (matches this drawing's units). None of the generic
-- commands (ATGRID, ATSTAIR, ...) can produce room-specific furniture, so
-- this uses the plain at.* drawing API directly. Edit this file and run
-- ATLUARELOAD, no rebuild needed.
at.defineCommand("ATBATHROOMDEMO", function(p)
    local ox, oy = p.base.x, p.base.y
    local oz = p.base.z or 0

    local W, D = 1500, 2500  -- room width (x) and depth (y), mm

    at.ensureLayer("A-WALL", { color = "white" })
    at.ensureLayer("A-FIXT", { color = "cyan" })
    at.ensureLayer("A-TEXT", { color = "yellow" })

    -- Room outline
    local room = at.drawRect(ox, oy, oz, ox + W, oy + D, oz)
    at.setLayer(room, "A-WALL")

    -- Door: 700mm leaf hinged at (300,0), swinging into the room
    -- (kept clear of the washbasin at x >= 1050: max swing reach is
    -- hinge_x + 700; 400 would overlap the basin, 300 leaves 50mm margin)
    local hx, hy = ox + 300, oy
    local leaf = at.drawLine(hx, hy, oz, hx, hy + 700, oz)
    at.setLayer(leaf, "A-WALL")
    local swing = at.drawArc(hx, hy, oz, 700, 0, 90)
    at.setLayer(swing, "A-WALL")

    -- Corner shower, 900 x 900, back-left corner
    local shower = at.drawRect(ox, oy + D - 900, oz, ox + 900, oy + D, oz)
    at.setLayer(shower, "A-FIXT")
    local drain = at.drawCircle(ox + 450, oy + D - 450, oz, 50)
    at.setLayer(drain, "A-FIXT")
    local t1 = at.drawText(ox + 450, oy + D - 150, oz, "SHOWER", 90)
    at.setLayer(t1, "A-TEXT")

    -- Toilet, back-right corner, 400 wide x 700 deep from the back wall
    local tx1, ty1 = ox + 1050, oy + D - 700
    local tx2, ty2 = ox + 1450, oy + D
    local tank = at.drawRect(tx1, oy + D - 100, oz, tx2, ty2, oz)
    at.setLayer(tank, "A-FIXT")
    local bowl = at.drawCircle((tx1 + tx2) / 2, ty1 + 200, oz, 200)
    at.setLayer(bowl, "A-FIXT")
    local t2 = at.drawText((tx1 + tx2) / 2, oy + D - 550, oz, "WC", 90)
    at.setLayer(t2, "A-TEXT")

    -- Washbasin on the right wall, 550 wide (y) x 450 deep (x)
    local sx1, sy1 = ox + W - 450, oy + 150
    local sx2, sy2 = ox + W, oy + 700
    local basin = at.drawRect(sx1, sy1, oz, sx2, sy2, oz)
    at.setLayer(basin, "A-FIXT")
    local bowl2 = at.drawRect(sx1 + 50, sy1 + 50, oz, sx2 - 50, sy2 - 50, oz)
    at.setLayer(bowl2, "A-FIXT")
    local tap = at.drawCircle(ox + W - 20, (sy1 + sy2) / 2, oz, 15)
    at.setLayer(tap, "A-FIXT")
    local t3 = at.drawText((sx1 + sx2) / 2, sy1 - 80, oz, "SINK", 90)
    at.setLayer(t3, "A-TEXT")

    -- Room label with area
    local tag = at.roomTag(room, "BATHROOM")
    if tag then at.setLayer(tag, "A-TEXT") end

    print(string.format(
        "ATBATHROOMDEMO: drew %dx%d mm bathroom (shower, WC, sink) at (%.0f,%.0f).",
        W, D, ox, oy))
end, "Draws a simple 1.5 x 2.5 m bathroom layout with shower, WC and washbasin", {
    { name = "base", type = "point", prompt = "Bathroom base corner (bottom-left)" },
})
