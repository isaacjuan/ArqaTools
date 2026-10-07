-- ATALX / ATALY / ATALZ: align objects on one axis. Each object (or the whole
-- group it belongs to) moves along that axis until its anchor gets the picked
-- point's coordinate. Anchor per type (AlignTools::AlignObjects): circle/arc
-- center, curve start point, text/block insertion point, otherwise the
-- bounding-box minimum. Points are WCS, so the axes are the world axes.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds three
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local function defineAlignAxis(name, axis)
    local A = axis:upper()
    at.defineCommand(name, function(p)
        local coord = p.reference[axis]
        local n = at.alignTo(p.objects, axis, coord)
        print(string.format("%s: aligned %d object(s) to %s = %.3f.", name, n, A, coord))
    end, "Aligns objects (or their groups) to the " .. A .. " coordinate of a picked point", {
        { name = "reference", type = "point",     prompt = "Select reference point for " .. A .. " coordinate",
          description = "Only its " .. A .. " coordinate is used" },
        { name = "objects",   type = "selection", prompt = "Select objects", description = "Objects to align" },
    })
end

defineAlignAxis("ATALX", "x")
defineAlignAxis("ATALY", "y")
defineAlignAxis("ATALZ", "z")
