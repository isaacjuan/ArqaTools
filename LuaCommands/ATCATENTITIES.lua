-- ATCATENTITIES: prints a table of model-space entity counts per class name
-- (AcDbLine, AcDbPolyline, ...), largest count first, with a total. The
-- counts come from at.listEntities (CategorizeTools::DBObjectMap).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATCATENTITIES", function()
    local rows = {}
    for name, count in at.listEntities():gmatch("([^\n]+): (%d+)\n") do
        rows[#rows + 1] = { name = name, count = tonumber(count) }
    end
    if #rows == 0 then print("ATCATENTITIES: no entities found in model space."); return end

    table.sort(rows, function(a, b)
        if a.count ~= b.count then return a.count > b.count end
        return a.name < b.name
    end)

    local rule = string.rep("-", 40) .. " -------"
    print("=== Entity Categories ===")
    print(string.format("%-40s %s", "Type", "Count"))
    print(rule)
    local total = 0
    for _, r in ipairs(rows) do
        total = total + r.count
        print(string.format("%-40s %d", r.name, r.count))
    end
    print(rule)
    print(string.format("%-40s %d", "TOTAL", total))
end, "Lists model-space entity counts by type")
