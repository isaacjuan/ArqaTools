-- ATPERIMETER: "P: <length>" text 1.5 text heights below the centroid of a
-- closed polyline. The text follows the polyline (updates when it changes);
-- that part is at.perimeterLabel (AreaTools::InsertPerimeterLabel +
-- PerimeterReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATPERIMETER", function(p)
    local text, err = at.perimeterLabel(p.boundary)
    if not text then print("ATPERIMETER: " .. err .. "."); return end
    print("Perimeter text inserted: " .. (at.getText(text) or ""))
end, "Inserts an auto-updating perimeter text in a closed polyline", {
    { name = "boundary", type = "entity", prompt = "Select closed polyline", description = "Closed polyline to measure" },
})
