-- ATSUMLENGTH: one text with the summed length of the selected curves (lines,
-- arcs, circles, polylines...; other objects are skipped). The text follows
-- the curves (updates when any changes); that part is at.sumLengthLabel
-- (AreaTools::InsertSumLengthLabel + PolylineSumLengthReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATSUMLENGTH", function(p)
    -- Show the total before asking where to put it, as the C++ command did.
    local curves, total = {}, 0
    for _, h in ipairs(p.objects) do
        local props = at.getProps(h)
        if props and props.length then
            curves[#curves + 1] = h
            total = total + props.length
        end
    end
    if #curves == 0 then print("ATSUMLENGTH: no valid curves selected."); return end
    print("Total length: " .. at.formatLength(total))

    local pos = p.position   -- asked here, after the total is shown
    if not pos then print("ATSUMLENGTH: cancelled."); return end

    local text, err = at.sumLengthLabel(curves, pos.x, pos.y, pos.z)
    if not text then print("ATSUMLENGTH: " .. err .. "."); return end
    print(string.format("Sum length text inserted. Monitoring %d curve(s).", #curves))
end, "Inserts an auto-updating sum of lengths for the selected curves", {
    { name = "objects", type = "selection", prompt = "Select curves",
      description = "Lines, arcs, circles, polylines; other objects are skipped" },
    { name = "position", type = "point", optional = true, prompt = "Specify position for text",
      description = "Where the sum text goes" },
})
