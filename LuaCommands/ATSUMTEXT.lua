-- ATSUMTEXT: sums the numeric values of the selected TEXT/MTEXT ("1,234.50",
-- "$12", "€ 3" parse; others are skipped), prints each value and the total,
-- then places the total (2 decimals) as a text at a picked point, with the
-- current text style and the drawing's default label height. Parsing is
-- at.sumText (TextTools::SumTextValues), the text is at.drawText.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATSUMTEXT", function(p)
    if not p.objects then print("ATSUMTEXT: no objects selected."); return end
    print(string.format("Processing %d text object(s)...", #p.objects))

    -- One at.sumText call per text so each value can be listed, as the C++ did.
    local total, valid, invalid = 0, 0, 0
    for _, h in ipairs(p.objects) do
        local content = at.getText(h)
        if content then
            local v, ok = at.sumText({ h })
            if ok == 1 then
                total = total + v
                valid = valid + 1
                print(string.format("  %s = %.2f", content, v))
            else
                invalid = invalid + 1
                print(string.format("  Skipped '%s' (not numeric)", content))
            end
        end
    end
    if valid == 0 then print("ATSUMTEXT: no valid numeric values found."); return end

    print("========================================")
    print(string.format("TOTAL SUM: %.2f", total))
    print(string.format("Valid values: %d", valid))
    print(string.format("Skipped: %d", invalid))
    print("========================================")

    print("Specify point for sum text:")
    local pos = p.position
    if not pos then print("ATSUMTEXT: command cancelled. Sum calculated but not inserted."); return end

    local sumText = string.format("%.2f", total)
    local text = at.drawText(pos.x, pos.y, pos.z, sumText)
    if not text then print("ATSUMTEXT: could not add text to drawing."); return end
    print("Sum text created: " .. sumText)
end, "Sums numeric values from selected text objects and places the total as text", {
    { name = "objects", type = "selection", filter = "TEXT,MTEXT", optional = true,
      prompt = "Select text objects containing numeric values",
      description = "TEXT/MTEXT with numbers; non-numeric texts are skipped" },
    { name = "position", type = "point", optional = true, prompt = "Insertion point",
      description = "Where the total is placed (asked after the total is printed)" },
})
