-- ATINSERTAREA: area text at the centroid of a closed polyline. The text
-- follows the polyline (updates when it changes); that part is at.areaLabel
-- (AreaTools::InsertAreaLabel + PolylineAreaReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATINSERTAREA", function(p)
    local text, err = at.areaLabel(p.boundary)
    if not text then print("ATINSERTAREA: " .. err .. "."); return end
    print("Area text inserted: " .. (at.getText(text) or ""))
end, "Inserts an auto-updating area text in a closed polyline", {
    { name = "boundary", type = "entity", prompt = "Select a closed polyline", description = "Closed polyline to measure" },
})
