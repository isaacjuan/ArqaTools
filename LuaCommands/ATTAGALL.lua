-- ATTAGALL: a length label on every selected curve (lines, arcs, polylines...;
-- other objects are counted as skipped). Each label sits at the curve's
-- midpoint, perpendicular to it, and follows the curve (updates when it
-- changes); that part is at.lengthLabel (AreaTools::InsertLengthLabel +
-- LinearLengthReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATTAGALL", function(p)
    if not p.objects or #p.objects == 0 then print("ATTAGALL: no objects selected."); return end

    -- at.lengthLabel fails on non-curves, so a failure counts as skipped.
    local tagged, skipped = 0, 0
    for _, h in ipairs(p.objects) do
        if at.lengthLabel(h) then tagged = tagged + 1 else skipped = skipped + 1 end
    end
    print(string.format("ATTAGALL complete: %d tagged, %d skipped.", tagged, skipped))
end, "Inserts an auto-updating length text on every selected line/polyline", {
    { name = "objects", type = "selection", prompt = "Select objects", optional = true,
      description = "Lines, arcs, polylines; other objects are skipped" },
})
