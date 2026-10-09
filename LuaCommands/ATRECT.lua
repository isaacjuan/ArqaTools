-- ATRECT: draws a rectangle between two explicit corners on a given layer.
-- Generic utility -- no installed command wrapped at.drawRect with free-form
-- corners and a layer argument until now (ATGRID/ATFRONTCLEARANCE etc. all
-- derive corners from a fixture or a fixed cell size). Useful for precise
-- manual corrections, e.g. trimming a clearance zone to stop exactly at a
-- neighboring fixture instead of overlapping it. Edit this file and run
-- ATLUARELOAD, no rebuild needed.
at.defineCommand("ATRECT", function(p)
    local rect = at.drawRect(p.corner1.x, p.corner1.y, 0, p.corner2.x, p.corner2.y, 0)
    if p.layer and p.layer ~= "" then
        at.ensureLayer(p.layer)
        at.setLayer(rect, p.layer)
    end
    print(string.format("ATRECT: %s drawn%s.", rect, p.layer and (" on " .. p.layer) or ""))
end, "Draws a rectangle between two explicit corners, optionally on a given layer", {
    { name = "corner1", type = "point",  prompt = "First corner" },
    { name = "corner2", type = "point",  prompt = "Opposite corner", base = "corner1" },
    { name = "layer",   type = "string", prompt = "Layer (blank = current)", optional = true },
})
