-- ATFREEZELAYER: freezes the layer of the picked object. The current layer
-- and layer 0 cannot be frozen (at.setLayerState refuses them).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATFREEZELAYER", function(p)
    local props, err = at.getProps(p.object)
    if not props then print("ATFREEZELAYER: cannot open object" .. (err and (" (" .. err .. ")") or "") .. "."); return end
    local layer = props.layer

    local ok, err2 = at.setLayerState(layer, { frozen = true })
    if not ok then print("ATFREEZELAYER: " .. err2 .. "."); return end
    print(string.format("Layer '%s' frozen.", layer))
end, "Freezes the layer of the picked object", {
    { name = "object", type = "entity", prompt = "Select object on layer to freeze",
      description = "Any object on the layer to freeze" },
})
