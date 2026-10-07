-- ATCOUNTBLOCKS: table of block instance counts per block name, in a selection
-- or in the whole drawing (model space). Anonymous blocks (*U..., *D...) are
-- skipped. The counting is at.countBlocks (AreaTools::CountBlocks).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATCOUNTBLOCKS", function(p)
    local counts
    if p.scope == "Drawing" then
        counts = at.countBlocks()
    else
        -- p.objects is only asked (or needed from an agent) for Selection.
        if not p.objects then print("ATCOUNTBLOCKS: no objects selected."); return end
        counts = at.countBlocks(p.objects)
    end

    local names = {}
    for name in pairs(counts) do names[#names + 1] = name end
    if #names == 0 then print("ATCOUNTBLOCKS: no block references found."); return end
    table.sort(names)

    local rule = "----------------------------------------  -----"
    print(string.format("%-40s  COUNT", "BLOCK NAME"))
    print(rule)
    local total = 0
    for _, name in ipairs(names) do
        print(string.format("%-40s  %d", name, counts[name]))
        total = total + counts[name]
    end
    print(rule)
    print(string.format("%-40s  %d", "TOTAL", total))
end, "Counts block instances per block name in a selection or the whole drawing", {
    { name = "scope", type = "keyword", prompt = "Count in [Selection/Drawing]",
      options = "Selection Drawing", default = "Selection",
      description = "Selection counts the objects given; Drawing counts all of model space" },
    { name = "objects", type = "selection", optional = true, prompt = "Select objects",
      description = "Objects to count in (scope Selection only)" },
})
