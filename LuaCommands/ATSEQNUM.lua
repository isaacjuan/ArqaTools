-- ATSEQNUM: place sequential numbers at picked points (Enter finishes).
-- With circles, each number gets a golden-ratio circle (radius = 1.618 x text
-- height), the text is compressed to fit, and the two are grouped (SEQNUM_n);
-- that part is at.seqNumber (SeqNumTools::CreateSeqNumber).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.

-- A fractional step always shows 2 decimals ("1.50", "2.00"); an integer step
-- shows whole numbers as integers.
local function formatNumber(n, step)
    if step ~= math.floor(step) or n ~= math.floor(n) then
        return string.format("%.2f", n)
    end
    return string.format("%.0f", n)
end

at.defineCommand("ATSEQNUM", function(p)
    if p.height <= 0 then print("ATSEQNUM: text height must be greater than zero."); return end
    local circles = p.circles == "Yes"

    local n, count = p.start, 0
    while true do
        local text = formatNumber(n, p.step)
        local x, y, z = at.getPoint("Specify point for number " .. text .. " (or press ENTER to finish)")
        if not x then break end
        at.seqNumber(x, y, z, text, p.height, circles)
        n = n + p.step
        count = count + 1
    end
    print(string.format("ATSEQNUM: created %d sequential number(s)%s.", count, circles and " in circles" or ""))
end, "Places sequential numbers at picked points, optionally in grouped circles", {
    { name = "start",   type = "number",   prompt = "Enter starting number", default = 1 },
    { name = "step",    type = "number",   prompt = "Enter step value",      default = 1 },
    { name = "height",  type = "distance", prompt = "Specify text height",   default = 2.5,
      description = "Text height; circle radius is 1.618 x this" },
    { name = "circles", type = "keyword",  prompt = "Add circles around numbers", options = "Yes No", default = "No" },
})
