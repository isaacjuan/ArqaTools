-- ATPLACEMID: move an object (its whole group, if grouped) so that its
-- bounding-box center lands on the midpoint between two points.
-- Was a C++ command; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATPLACEMID", function(p)
    local cx, cy, cz = at.refPoint(p.target)   -- bounding-box center, WCS
    if not cx then
        print("ATPLACEMID: cannot get a reference point for that object (" .. tostring(cy) .. ").")
        return
    end

    -- Point params arrive in WCS, like at.refPoint.
    local mx = (p.first.x + p.second.x) / 2
    local my = (p.first.y + p.second.y) / 2
    local mz = (p.first.z + p.second.z) / 2

    if not at.moveEntity(p.target, mx - cx, my - cy, mz - cz) then
        print("ATPLACEMID: the object could not be moved.")
        return
    end
    print(string.format("Object placed at midpoint (%.2f, %.2f, %.2f)", mx, my, mz))
end, "Places an object at the midpoint between two points (bounding-box center; moves the whole group)", {
    { name = "target", type = "entity", prompt = "Select object to place",       description = "Object to move" },
    { name = "first",  type = "point",  prompt = "Pick first reference point",   description = "First point" },
    { name = "second", type = "point",  prompt = "Pick second reference point",  description = "Second point" },
})
