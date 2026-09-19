-- grid.lua
local spacing, radius = 20, 3

for row = 0, 2 do
    for col = 0, 2 do
        at.drawCircle(col * spacing, row * spacing, 0, radius)
    end
end

local base = at.drawLine(0, 0, 0, 40, 0, 0)
if base then
    at.moveEntity(base, 0, -10, 0)
    local copy = at.copyEntity(base, 0, 50, 0)
    at.rotateEntity(copy, 20, 40, 0, 90)
    print("base=" .. base .. " copy=" .. copy)
else
    print("draw failed")
end

print(at.listEntities())
