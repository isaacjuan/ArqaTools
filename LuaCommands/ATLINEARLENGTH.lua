-- ATLINEARLENGTH: length text at the midpoint of a line or polyline, rotated
-- perpendicular to it and offset half a text height. The text follows the
-- curve (updates when it changes); that part is at.lengthLabel
-- (AreaTools::InsertLengthLabel + LinearLengthReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATLINEARLENGTH", function(p)
    local text, err = at.lengthLabel(p.curve)
    if not text then print("ATLINEARLENGTH: " .. err .. "."); return end
    print("Length text inserted: " .. (at.getText(text) or ""))
end, "Inserts an auto-updating length text on a line or open polyline", {
    { name = "curve", type = "entity", prompt = "Select line or polyline", description = "Line or polyline to measure" },
})
