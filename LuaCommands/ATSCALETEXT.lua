-- ATSCALETEXT: multiplies the height of the selected TEXT/MTEXT by a factor
-- (other objects, dimensions included, are left alone). The work is
-- at.scaleText (TextTools::ScaleTextHeight). The factor is checked before the
-- objects are asked for (p.objects is only asked when first read).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATSCALETEXT", function(p)
    if not p.factor then print("ATSCALETEXT: command cancelled."); return end
    if p.factor <= 0 then print("ATSCALETEXT: scale factor must be > 0."); return end

    if not p.objects then print("ATSCALETEXT: no objects selected."); return end

    local count = at.scaleText(p.objects, p.factor)
    print(string.format("Scaled text height x%.2f on %d object(s).", p.factor, count))
end, "Scales the text height of selected objects by a factor", {
    { name = "factor", type = "number", optional = true,
      prompt = "Scale factor (e.g. 2.0 to double, 0.5 to halve)",
      description = "Height multiplier, > 0" },
    { name = "objects", type = "selection", optional = true, prompt = "Select objects",
      description = "Objects whose TEXT/MTEXT heights are scaled; others are left alone" },
})
