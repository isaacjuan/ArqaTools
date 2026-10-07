-- ATMATCHLAYER: moves the selected objects to the layer of a source object.
-- The source itself is skipped if it is also selected.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATMATCHLAYER", function(p)
    local props, err = at.getProps(p.source)
    if not props then print("ATMATCHLAYER: cannot open source object" .. (err and (" (" .. err .. ")") or "") .. "."); return end
    local layer = props.layer
    print("Source layer: " .. layer)

    local count = 0
    for _, h in ipairs(p.objects) do
        if h ~= p.source and at.setLayer(h, layer) then count = count + 1 end
    end

    print(string.format("Layer matched to '%s' on %d object(s).", layer, count))
end, "Changes the selected objects to the layer of a source object", {
    { name = "source",  type = "entity",    prompt = "Select SOURCE object (to match layer from)",
      description = "Object whose layer is copied" },
    { name = "objects", type = "selection", prompt = "Select objects to move to this layer",
      description = "Objects to put on the source object's layer" },
})
