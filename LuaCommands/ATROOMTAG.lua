-- ATROOMTAG: room name + area label at the centroid of a closed polyline. The
-- label follows the polyline (area updates when it changes); that part is
-- at.roomTag (AreaTools::InsertRoomTag + RoomTagReactor).
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed.
at.defineCommand("ATROOMTAG", function(p)
    local tag, err = at.roomTag(p.boundary, p.name)
    if not tag then print("ATROOMTAG: " .. err .. "."); return end
    print("Room tag inserted: " .. p.name)
end, "Inserts a room name + area label in a closed polyline (updates with the polyline)", {
    { name = "boundary", type = "entity", prompt = "Select closed polyline (room boundary)",
      description = "Closed polyline of the room" },
    { name = "name",     type = "string", prompt = "Room name", description = "Text above the area" },
})
