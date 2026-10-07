-- ATSVGEXPORT: exports the selected objects to ArqaTools_Export.svg in the
-- user's Documents folder (overwritten if it exists). Block references become
-- shared <defs> + <use>; unsupported objects fall back to explode(); skipped
-- ones are listed on the command line. The export is at.exportSvg
-- (SvgExportTools::ExportSvg).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATSVGEXPORT", function(p)
    if not p.objects or #p.objects == 0 then print("ATSVGEXPORT: no objects selected."); return end
    local path, exported, skipped = at.exportSvg(p.objects)
    if not path then print("ATSVGEXPORT: " .. exported); return end   -- exported holds the error
    print(string.format("Exported %d entities (%d skipped) to:\n%s", exported, skipped, path))
end, "Exports the selected objects to ArqaTools_Export.svg in Documents", {
    { name = "objects", type = "selection", prompt = "Select objects", optional = true,
      description = "Objects to export" },
})
