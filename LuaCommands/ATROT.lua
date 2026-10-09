at.defineCommand("ATROT", function(p)
    local handle = p.object
    local cx, cy, cz

    if p.center then
        cx, cy, cz = p.center.x, p.center.y, p.center.z
    else
        cx, cy, cz = at.refPoint(handle)
        if not cx then
            local pr = at.getProps(handle)
            if pr and pr.min and pr.max then
                cx = (pr.min.x + pr.max.x) / 2
                cy = (pr.min.y + pr.max.y) / 2
                cz = (pr.min.z + pr.max.z) / 2
            else
                cx, cy, cz = 0, 0, 0
            end
        end
    end

    local ok = at.rotateEntity(handle, cx, cy, cz, -90)
    if ok then
        print("ATROT: rotated 90 degrees clockwise about ("
            .. string.format("%.3f, %.3f, %.3f", cx, cy, cz) .. ")")
    else
        print("ATROT: could not rotate the selected entity")
    end
end, "Rotates a selected entity 90 degrees clockwise (about Z, through a base point)", {
    { name = "object", type = "entity", prompt = "Entity to rotate" },
    { name = "center", type = "point", prompt = "Rotation base point <reference point of the entity>", optional = true },
})