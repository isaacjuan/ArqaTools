-- ATMX / ATMY / ATMZ: move objects along one axis only. The distance is the
-- base-to-target difference on that axis; the other axes are ignored.
-- Grouped objects move with their group (once, even if several members are
-- selected). Points are WCS, so the axes are the world axes.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds three
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local function defineMoveAxis(name, axis)
    local A = axis:upper()
    at.defineCommand(name, function(p)
        local d = p.target[axis] - p.base[axis]
        local n = at.moveEntities(p.objects,
                                  axis == "x" and d or 0,
                                  axis == "y" and d or 0,
                                  axis == "z" and d or 0)
        print(string.format("%s: moved %d object(s) %.2f units in %s.", name, n, d, A))
    end, "Moves objects in the " .. A .. " direction only (grouped objects move with their group)", {
        { name = "objects", type = "selection", prompt = "Select objects",     description = "Objects to move" },
        { name = "base",    type = "point",     prompt = "Specify base point", description = "Start of the move" },
        { name = "target",  type = "point",     prompt = "Specify target point (only " .. A .. " distance will be used)",
          description = "End of the move; only its " .. A .. " coordinate counts" },
    })
end

defineMoveAxis("ATMX", "x")
defineMoveAxis("ATMY", "y")
defineMoveAxis("ATMZ", "z")
