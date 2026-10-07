-- ATCX / ATCY / ATCZ: copy objects along one axis only. The distance is the
-- base-to-target difference on that axis; the other axes are ignored.
-- A grouped object copies its whole group (once, even if several members are
-- selected); the copies are not grouped. Points are WCS, so the axes are the
-- world axes.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds three
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local function defineCopyAxis(name, axis)
    local A = axis:upper()
    at.defineCommand(name, function(p)
        local d = p.target[axis] - p.base[axis]
        local copies = at.copyEntities(p.objects,
                                       axis == "x" and d or 0,
                                       axis == "y" and d or 0,
                                       axis == "z" and d or 0)
        print(string.format("%s: created %d copies %.2f units away in %s.", name, #copies, d, A))
    end, "Copies objects in the " .. A .. " direction only (grouped objects copy their whole group)", {
        { name = "objects", type = "selection", prompt = "Select objects",     description = "Objects to copy" },
        { name = "base",    type = "point",     prompt = "Specify base point", description = "Start of the displacement" },
        { name = "target",  type = "point",     prompt = "Specify target point (only " .. A .. " distance will be used)",
          description = "End of the displacement; only its " .. A .. " coordinate counts" },
    })
end

defineCopyAxis("ATCX", "x")
defineCopyAxis("ATCY", "y")
defineCopyAxis("ATCZ", "z")
