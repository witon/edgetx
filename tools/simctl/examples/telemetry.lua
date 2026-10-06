-- Black-and-white telemetry script.
-- Copy to /SCRIPTS/TELEMETRY/telem.lua on the simulator SD card.
-- The stored name is at most 6 characters.

local function init()
  print("telem ready")
end

local function background()
end

local function run(event)
  if event ~= 0 then
    print("telem event")
  end
  return 0
end

return {init = init, background = background, run = run}
