-- Better Car AI - the route to the map destination (BCAI navigation, native
-- plugin 0.28+). The plugin's navigator plans it (for time, and ending on the
-- pin's level rather than on a road above or below it, where the game's GPS
-- can go wrong); this draws it on the road ahead with the next turn, and
-- plans again when the destination moves or the player leaves the route.
-- Autodrive+ drives the same planner's route.

local Nav = {}

-- What init.lua hands over: settings, log, scriptSettings, autodriveSystem,
-- mountedVehicle, nativeVersion, visible (whether to draw over the game now),
-- overlayDrawsPath (the debug overlay draws Autodrive+'s path now),
-- ownDriving (Autodrive+ drives now).
local deps = nil

local state = {
  active = false,
  summary = nil,  -- the route ("route: ...") or why there is none
  goal = nil,     -- where it was planned to
  inCar = false,  -- planned from a car (from its lane) or on foot
  from = nil,     -- where it was planned from
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

-- Where the route was planned to ({x, y, z}), or nil.
function Nav.Goal()
  return state.active and state.goal or nil
end

-- Where the route goes: a driving test's destination while one is set
-- (drivetest.lua), else the map's (Vector4, W 1; W 0 without one).
local override = nil

function Nav.SetDestination(dest)
  override = dest and Vector4.new(dest.x, dest.y, dest.z, 1.0) or nil
end

function Nav.Destination(sys)
  if override then return override end
  local ok, dest = pcall(function() return sys:NavDestination() end)
  return ok and dest or Vector4.new(0, 0, 0, 0)
end

-- Where the player is on the route (BCAI_NavStatus, parsed: left, time, off,
-- turn, in), refreshed 4 times a second.
function Nav.Status()
  return state.status
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
  -- (Forward's W 1: the player drives and may turn round, if that is
  -- quicker than driving on to the next place to turn; plugin 0.30.
  -- Autodrive+ doesn't turn round, so not while it drives, nor in a driving
  -- test, where it drives the same route after.)
  if inCar and not override and not deps.ownDriving() and (deps.nativeVersion() or 0) >= 3000 then
    fwd = Vector4.new(fwd.x, fwd.y, fwd.z, 1)
  end
  local ok, result = pcall(function()
    return Game.BCAI_NavPlan(deps.autodriveSystem(), pos, fwd, dest, inCar)
  end)
  result = tostring(result)
  local planned = ok and result:sub(1, 6) == "route:"
  local before = state.summary
  state.active = planned
  state.summary = planned and result:sub(8) or ("no route: " .. result)
  state.goal = { x = dest.x, y = dest.y, z = dest.z }
  state.from = { x = pos.x, y = pos.y, z = pos.z }
  state.inCar = inCar
  state.offFor = 0
  -- (Trying again every 10 s without a route: only when that changes.)
  if deps.settings.navLog and not (why == "trying again" and state.summary == before) then
    local where = ""
    if why == "new destination" and not override then
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
  local dest = Nav.Destination(sys)
  if not dest or dest.w < 0.5 then
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
  elseif state.active and inCar and (state.status.off or 0) > 25 then
    state.offFor = state.offFor + step
    if state.offFor >= 1.5 then why = string.format("%.0f m off the route", state.status.off) end
  elseif state.active and not inCar and state.from and distance(state.from, pos) > 80 then
    -- (On foot the route starts at a road nearby: planned again only after
    -- walking a fair way.)
    why = "walked on"
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

-- The line on the road ahead: a ribbon about 0.35 m wide as it looks from
-- here (thinner further off), starting a few metres ahead of the car (not
-- through it), fading towards its end, with a chevron every 24 m.
local STEP = 3        -- metres between the points (BCAI_NavAhead)
local SKIP = 3        -- segments left out under and just ahead of the car
local CHEVRON = 8     -- a chevron every this many segments

local function drawLine(sys, w, h)
  if #state.points < 2 then return end
  local ok, segs = pcall(function() return sys:GuideLine(state.points) end)
  if not ok or not segs or #segs < 6 then return end
  ImGui.SetNextWindowPos(0, 0)
  ImGui.SetNextWindowSize(w, h)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize + ImGuiWindowFlags.NoMove
      + ImGuiWindowFlags.NoInputs + ImGuiWindowFlags.NoBackground + ImGuiWindowFlags.NoSavedSettings
      + ImGuiWindowFlags.NoBringToFrontOnFocus + ImGuiWindowFlags.NoScrollbar
  if ImGui.Begin("BCAI route line", flags) then
    local dl = ImGui.GetWindowDrawList()
    -- (About the screen height over the view's vertical field of view.)
    local focal = 0.9 * h
    for k = 0, #segs / 6 - 1 do
      local i = k * 6 + 1
      local seg = segs[i + 5]
      if seg >= SKIP then
        local depth = math.max(segs[i + 4], 0.5)
        local x1, y1, x2, y2 = segs[i] * w, segs[i + 1] * h, segs[i + 2] * w, segs[i + 3] * h
        local width = math.max(1.5, math.min(10, focal * 0.35 / depth))
        local fade = math.min(1, (seg - SKIP + 1) / 3) * math.max(0.25, 1 - seg * STEP / 320)
        local colour = rgba(255, 205, 60, math.floor(215 * fade))
        ImGui.ImDrawListAddLine(dl, x1, y1 + 1.5, x2, y2 + 1.5, rgba(40, 30, 0, math.floor(110 * fade)), width + 2)
        ImGui.ImDrawListAddLine(dl, x1, y1, x2, y2, colour, width)
        -- (Round joins, so the bends don't show gaps.)
        ImGui.ImDrawListAddCircleFilled(dl, x2, y2, width / 2, colour)
        local dx, dy = x2 - x1, y2 - y1
        local len = math.sqrt(dx * dx + dy * dy)
        if seg % CHEVRON == 0 and width > 2.5 and len > 1 then
          -- A chevron pointing along the route, darker on the ribbon.
          dx, dy = dx / len, dy / len
          local size = width * 1.4
          local cx, cy = (x1 + x2) / 2 + dx * size * 0.5, (y1 + y2) / 2 + dy * size * 0.5
          local dark = rgba(90, 60, 0, math.floor(230 * fade))
          for _, side in ipairs({ 1, -1 }) do
            ImGui.ImDrawListAddLine(dl, cx, cy, cx - (dx + side * dy) * size, cy - (dy - side * dx) * size, dark,
              math.max(1.5, width * 0.35))
          end
        end
      end
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
  elseif s.turn == "U-turn" and (s["in"] or 99) < 15 then
    big = "Turn round"
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
  -- (Not while the debug overlay draws Autodrive+'s own path on the road.)
  if settings.navLine and not deps.overlayDrawsPath() then drawLine(sys, w, h) end
  if settings.navHints then drawHint(w) end
end

return Nav
