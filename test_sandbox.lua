-- Sandbox limits: run each case on its own, e.g. ATLUA @test_sandbox.lua after
-- setting CASE below. Every case must END with an error, never hang AutoCAD.
--   1  pcall swallowing ESC        -> press ESC: "cancelled by user"
--   2  xpcall swallowing ESC       -> press ESC: "cancelled by user"
--   3  string.rep beyond the cap   -> "memory limit exceeded (256 MB): ..."
--   4  table growth beyond the cap -> "memory limit exceeded (256 MB): ..."
--   5  pcall still works normally  -> prints "ok false boom" and "ok true 42"
--   6  __gc finalizer              -> "__gc finalizers are not allowed"
local CASE = 1

if CASE == 1 then
  while true do pcall(function() while true do end end) end
elseif CASE == 2 then
  while true do xpcall(function() while true do end end, function() return "swallowed" end) end
elseif CASE == 3 then
  print(pcall(string.rep, "x", 512 * 1024 * 1024))
elseif CASE == 4 then
  local t = {}
  while true do
    pcall(function() t[#t + 1] = string.rep("x", 16 * 1024 * 1024) end)
  end
elseif CASE == 5 then
  print("ok", pcall(error, "boom", 0))
  print("ok", xpcall(function() return 42 end, print))
elseif CASE == 6 then
  setmetatable({}, { __gc = function() while true do end end })
  collectgarbage()
end
