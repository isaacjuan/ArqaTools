-- Copy-from-source commands for text and dimensions: pick one source, then
-- select the destinations (the source itself is always skipped).
--   ATCOPYTEXT      text content (at.getText / at.setText; MText keeps its codes)
--   ATCOPYSTYLE     text style, width factor, oblique, justification (no height)
--   ATCOPYTEXTFULL  same as ATCOPYSTYLE plus the text height
--   ATCOPYDIMSTYLE  dimension style
-- The style work is at.copyTextStyle / at.copyDimStyle (TextTools::CopyTextStyle /
-- CopyDimStyle). The source is checked before the destinations are asked for.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds several
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local SOURCE = { name = "source", type = "entity", prompt = "Select source", optional = true,
                 description = "Object to copy from" }

at.defineCommand("ATCOPYTEXT", function(p)
    if not p.source then print("ATCOPYTEXT: command cancelled."); return end
    local text = at.getText(p.source)
    if not text then print("ATCOPYTEXT: selected entity is not a text object."); return end

    print('Source text: "' .. text .. '"')
    print("Select destination text objects...")
    local dests = at.getSelection()
    if #dests == 0 then print("ATCOPYTEXT: no destination objects selected."); return end
    print(string.format("Selected %d destination objects.", #dests))

    local updated, skipped = 0, 0
    for _, h in ipairs(dests) do
        if h ~= p.source and at.setText(h, text) then updated = updated + 1
        else skipped = skipped + 1 end
    end
    print(string.format("Copy complete: %d text objects updated, %d skipped", updated, skipped))
end, "Copies the text content of one TEXT/MTEXT to other text objects", { SOURCE })

-- ATCOPYSTYLE / ATCOPYTEXTFULL: one body, includeHeight decides the height.
local function defineCopyStyle(name, includeHeight, description)
    at.defineCommand(name, function(p)
        if not p.source then print(name .. ": command cancelled."); return end
        -- An empty destination list only validates the source.
        if not at.copyTextStyle(p.source, {}) then
            print(name .. ": source entity is not a text object."); return
        end

        local dests = at.getSelection()
        if #dests == 0 then print(name .. ": no destination objects selected."); return end
        print(string.format("Processing %d destination object(s)...", #dests))

        local updated, err = at.copyTextStyle(p.source, dests, includeHeight)
        if not updated then print(name .. ": " .. err .. "."); return end
        -- Every destination is either updated or skipped.
        print(string.format("Updated: %d | Skipped: %d", updated, #dests - updated))
    end, description, { SOURCE })
end

defineCopyStyle("ATCOPYSTYLE", false, "Copies text style properties (not height) from one text to others")
defineCopyStyle("ATCOPYTEXTFULL", true, "Copies text style properties and height from one text to others")

at.defineCommand("ATCOPYDIMSTYLE", function(p)
    if not p.source then print("ATCOPYDIMSTYLE: command cancelled."); return end
    if not at.copyDimStyle(p.source, {}) then
        print("ATCOPYDIMSTYLE: source entity is not a dimension."); return
    end

    local dests = at.getSelection()
    if #dests == 0 then print("ATCOPYDIMSTYLE: no destination objects selected."); return end
    print(string.format("Processing %d destination object(s)...", #dests))

    local updated, err = at.copyDimStyle(p.source, dests)
    if not updated then print("ATCOPYDIMSTYLE: " .. err .. "."); return end
    print(string.format("Updated: %d | Skipped: %d", updated, #dests - updated))
end, "Copies the dimension style of one dimension to other dimensions", { SOURCE })
