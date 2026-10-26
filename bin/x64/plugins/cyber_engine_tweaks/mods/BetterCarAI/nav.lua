-- Better Car AI - the route to the map destination (BCAI navigation, native
-- plugin 0.28+). The plugin's navigator plans it (for time, and ending on the
-- pin's level rather than on a road above or below it, where the game's GPS
-- can go wrong); this draws it on the road ahead with the next turn, and
-- plans again when the destination moves or the player leaves the route.
-- Autodrive+ drives the same planner's route.

local Nav = {}

-- What init.lua hands over: settings, log, scriptSettings, autodriveSystem,
-- mountedVehicle, nativeVersion, visible (whether to draw over the game now).
local deps = nil

local state = {
  active = false,
  summary = nil,  -- the route ("route: ...") or why there is none
  goal = nil,     -- where it was planned to
  inCar = false,  -- planned from a car (from its lane) or on foot
  points = {},    -- the line ahead
  status = {},    -- BCAI_NavStatus, parsed
  timer = 0,
  offFor = 0,
  plannedAt = -100,
}

-- ImGui colours are 0xAABBGGRR, as signed 32-bit ints.
local function rgba(r, g, b, a)
  local c = a * 16777216 + b * 65536 + g * 256 + r
  if c >= 2147483648 then c = c - 4294967296 end
  return c
end
local LINE = rgba(255, 205, 60, 215)
local LINE_SHADOW = rgba(40, 30, 0, 120)

local function parse(text)
  local t = {}
  for k, v in tostring(text or ""):gmatch("([%w_]+)=([^;]*)") do
    t[k] = tonumber(v) or v
  end
  return t
end

local function distance(a, b)
  local dx, dy, dz = a.x - b.x, a.y - b.y, a.z - b.z
  return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function km(m)
  if m >= 1000 then return string.format("%.1f km", m / 1000) end
  return string.format("%.0f m", math.floor(m / 10 + 0.5) * 10)
end

local function minutes(s)
  if s < 60 then return "under a minute" end
  return string.format("about %d min", math.floor(s / 60 + 0.5))
end

function Nav.Init(d)
  deps = d
end

function Nav.Available()
  return (deps.nativeVersion() or 0) >= 2800
end

function Nav.Summary()
  return state.summary
end

function Nav.Clear(why)
  if state.active or state.summary then
    pcall(function() Game.BCAI_NavClear() end)
    if why and deps.settings.navLog and state.active then deps.log("Route | cleared: " .. why) end
  end
  state.active = false
  state.summary = nil
  state.goal = nil
  state.points = {}
  state.status = {}
  state.offFor = 0
end

local function plan(sys, pos, fwd, dest, inCar, why)
  state.plannedAt = os.clock()
  local ok, result = pcall(function()
    return Game.BCAI_NavPlan(deps.autodriveSystem(), pos, fwd, dest, inCar)
  end)
  result = tostring(result)
  local planned = ok and result:sub(1, 6) == "route:"
  local before = state.summary
  state.active = planned
  state.summary = planned and result:sub(8) or ("no route: " .. result)
  state.goal = { x = dest.x, y = dest.y, z = dest.z }
  state.inCar = inCar
  state.offFor = 0
  -- (Trying again every 10 s without a route: only when that changes.)
  if deps.settings.navLog and not (why == "trying again" and state.summary == before) then
    local where = ""
    if why == "new destination" then
      local okInfo, info = pcall(function() return sys:NavDestinationInfo() end)
      where = okInfo and (" (" .. tostring(info) .. ")") or ""
    end
    deps.log("Route | " .. why .. ": " .. state.summary .. where)
  end
end

-- 4 times a second: the destination, where the player is on the route, the
-- line ahead; plans again when needed.
function Nav.Tick(dt)
  local settings = deps.settings
  if not settings.navGuide or not Nav.Available() then
    if state.active or state.summary then Nav.Clear("switched off") end
    return
  end
  state.timer = state.timer + dt
  if state.timer < 0.25 then return end
  local step = state.timer
  state.timer = 0
  local sys = deps.scriptSettings()
  local player = Game.GetPlayer()
  if not sys or not player then return end
  local okDest, dest = pcall(function() return sys:NavDestination() end)
  if not okDest or not dest or dest.w < 0.5 then
    if state.active or state.summary then Nav.Clear("no destination") end
    return
  end
  local vehicle = deps.mountedVehicle()
  local inCar = vehicle ~= nil
  local pos = inCar and vehicle:GetWorldPosition() or player:GetWorldPosition()
  local fwd = inCar and vehicle:GetWorldForward() or player:GetWorldForward()
  local why = nil
  if not state.goal or distance(state.goal, dest) > 5 then
    why = "new destination"
  elseif inCar and not state.inCar then
    why = "got in a car"
  elseif state.active and (state.status.off or 0) > 25 then
    state.offFor = state.offFor + step
    if state.offFor >= 1.5 then why = string.format("%.0f m off the route", state.status.off) end
  elseif not state.active and os.clock() - state.plannedAt > 10 then
    why = "trying again"
  end
  -- (Not more than every 2 s, whatever happens.)
  if why and os.clock() - state.plannedAt >= 2 then plan(sys, pos, fwd, dest, inCar, why) end
  if not state.active then
    state.points = {}
    return
  end
  local okStatus, status = pcall(function() return Game.BCAI_NavStatus(pos) end)
  state.status = okStatus and parse(status) or {}
  if (state.status.off or 0) <= 25 then state.offFor = 0 end
  local okPoints, points = pcall(function() return Game.BCAI_NavAhead(pos, 300.0, 3.0) end)
  state.points = okPoints and points or {}
end

-- The line on the road ahead.
local function drawLine(sys, w, h)
  if #state.points < 2 then return end
  local ok, segs = pcall(function() return sys:GuideLine(state.points) end)
  if not ok or not segs or #segs < 4 then return end
  ImGui.SetNextWindowPos(0, 0)
  ImGui.SetNextWindowSize(w, h)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize + ImGuiWindowFlags.NoMove
      + ImGuiWindowFlags.NoInputs + ImGuiWindowFlags.NoBackground + ImGuiWindowFlags.NoSavedSettings
      + ImGuiWindowFlags.NoBringToFrontOnFocus + ImGuiWindowFlags.NoScrollbar
  if ImGui.Begin("BCAI route line", flags) then
    local dl = ImGui.GetWindowDrawList()
    local n = #segs / 4
    for k = 0, n - 1 do
      local i = k * 4 + 1
      -- Thinner further along it.
      local width = 7 - 4.5 * k / math.max(n - 1, 1)
      local x1, y1, x2, y2 = segs[i] * w, segs[i + 1] * h, segs[i + 2] * w, segs[i + 3] * h
      ImGui.ImDrawListAddLine(dl, x1, y1 + 2, x2, y2 + 2, LINE_SHADOW, width + 2)
      ImGui.ImDrawListAddLine(dl, x1, y1, x2, y2, LINE, width)
    end
  end
  ImGui.End()
end

local TURN_TEXT = {
  ["left"] = "Turn left",
  ["right"] = "Turn right",
  ["slight left"] = "Keep left",
  ["slight right"] = "Keep right",
  ["U-turn"] = "Make a U-turn",
}

-- The next turn and what is left, at the top of the screen.
local function drawHint(w)
  local s = state.status
  if not s.left then return end
  local big, small
  if (s.off or 0) > 25 then
    big = "Off the route"
    small = "finding a new one"
  elseif s.turn and s["in"] then
    big = string.format("%s in %s", TURN_TEXT[s.turn] or s.turn, km(s["in"]))
  else
    big = "Destination in " .. km(s.left)
  end
  small = small or string.format("%s to go, %s", km(s.left), minutes(s.time or 0))
  ImGui.SetNextWindowPos(w * 0.5, 70, ImGuiCond.Always, 0.5, 0)
  ImGui.SetNextWindowBgAlpha(0.55)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.AlwaysAutoResize + ImGuiWindowFlags.NoInputs
      + ImGuiWindowFlags.NoFocusOnAppearing + ImGuiWindowFlags.NoSavedSettings + ImGuiWindowFlags.NoMove
  if ImGui.Begin("BCAI route hint", flags) then
    ImGui.SetWindowFontScale(1.4)
    ImGui.TextColored(1.0, 0.82, 0.25, 1, big)
    ImGui.SetWindowFontScale(1.0)
    ImGui.TextColored(0.8, 0.8, 0.8, 1, small)
  end
  ImGui.End()
end

function Nav.Draw()
  local settings = deps.settings
  if not settings.navGuide or not state.active or not deps.visible() then return end
  local sys = deps.scriptSettings()
  if not sys then return end
  local w, h = GetDisplayResolution()
  if settings.navLine then drawLine(sys, w, h) end
  if settings.navHints then drawHint(w) end
end

return Nav
