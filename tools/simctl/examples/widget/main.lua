-- Color widget. Keep this file at /WIDGETS/demo/main.lua.
-- --script is the name below, not the folder name.

local function create(zone, options)
  return {printed = false}
end

local function refresh(widget)
  if not widget.printed then
    widget.printed = true
    print("widget ready")
  end
end

return {name = "MyWidget", create = create, refresh = refresh}
