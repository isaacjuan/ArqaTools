-- ATCHGTOLAYER: puts the selected objects on the current layer and reports
-- how many changed and how many failed (e.g. objects on locked layers).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATCHGTOLAYER", function(p)
    local layer = at.getCurrentLayer()
    if not layer or layer == "" then print("ATCHGTOLAYER: cannot access current layer."); return end
    print("Current layer: " .. layer)
    print(string.format("Processing %d objects...", #p.objects))

    local changed, failed = 0, 0
    for _, h in ipairs(p.objects) do
        if at.setLayer(h, layer) then changed = changed + 1 else failed = failed + 1 end
    end

    print(string.format("Changed %d objects to layer '%s'", changed, layer))
    if failed > 0 then print(string.format("Failed to change %d objects", failed)) end
end, "Changes the selected objects to the current layer", {
    { name = "objects", type = "selection", prompt = "Select objects to change to the current layer",
      description = "Objects to move to the current layer" },
})
