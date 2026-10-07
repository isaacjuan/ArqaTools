-- ATNL: quick new layer. Creates the layer (color 7) if it does not exist and
-- makes it current; says whether it was created or already existed. The
-- creation and switch are at.setCurrentLayer (LayerTools::SetCurrentLayer).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATNL", function(p)
    local name = p.name:match("^%s*(.-)%s*$")
    if name == "" then print("ATNL: layer name cannot be empty."); return end

    -- Layer names are case-insensitive in AutoCAD.
    local existed = false
    for _, l in ipairs(at.layers()) do
        if l.name:lower() == name:lower() then existed = true; break end
    end

    local ok, err = at.setCurrentLayer(name, true)
    if not ok then print("ATNL: " .. err .. "."); return end
    if existed then
        print(string.format("Layer '%s' already exists, set as current.", name))
    else
        print(string.format("Layer '%s' created and set as current.", name))
    end
end, "Creates a layer (if needed) and sets it as current", {
    { name = "name", type = "string", prompt = "Layer name",
      description = "Layer to create (color 7) or reuse, then make current" },
})
