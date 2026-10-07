-- ATREG2POLY: converts a region to a new closed green polyline (color 3) on
-- the current layer, following its boundary loop; the region is left in
-- place. A region with holes gives a single loop (a warning is printed). The
-- conversion is at.regionToPolyline (PolylineTools::RegionToPolyline).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATREG2POLY", function(p)
    if not p.region then print("ATREG2POLY: selection cancelled."); return end
    local poly, err = at.regionToPolyline(p.region)
    if not poly then print("ATREG2POLY: " .. err .. "."); return end
    local props = at.getProps(poly)
    local n = (props and props.vertices) and #props.vertices or 0
    print(string.format("Polyline created with %d vertices!", n))
end, "Converts a region to a closed polyline (region kept)", {
    { name = "region", type = "entity", prompt = "Select region", optional = true,
      description = "Region to convert" },
})
