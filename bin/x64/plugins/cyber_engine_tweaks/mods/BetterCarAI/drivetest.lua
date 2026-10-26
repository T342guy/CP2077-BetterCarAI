-- Better Car AI - the driving test: a route for the player to drive (to the
-- map pin, or somewhere random; shown with the route line and turn hints),
-- then Autodrive+ drives the same route from the same start. Both drives are recorded, 20 rows a
-- second, for tools/drive_test.py to compare: the time, the speed through
-- each bend, how hard the car was cornered and braked, where it slid. The
-- player's drive also shows how much grip the car really has.
--
-- Files (in this mod's folder): test_<id>_route.txt (the route, a point
-- every 2 m), test_<id>_player.csv and test_<id>_ai_<style>.csv.

local Test = {}

-- What init.lua hands over: settings, state (init's), log, scriptSettings,
-- mountedVehicle, nativeVersion, autodriveSystem, nav, startOwnDriver,
-- stopOwnDriver, carData, styleName.
local deps = nil

-- The test under way: id, start {pos, fwd}, dest, phase, the run being
-- recorded and what was saved. Phases: "ready" (waiting for the player to
-- drive off), "player" (recording the player), "driven" (the player's drive
-- saved), "placing" (the car put back at the start, settling), "ai"
-- (recording Autodrive+), "done".
local test = nil

local SAMPLE = 0.05 -- seconds between rows
local ARRIVED = 15  -- metres from the end of the route that count as there

function Test.Init(d)
  deps = d
end

function Test.Get()
  return test
end

local function xyz(v)
  return { x = v.x, y = v.y, z = v.z }
end

local function fmt(v)
  return string.format("(%.0f, %.0f, %.0f)", v.x, v.y, v.z)
end

local function speedOf(vehicle)
  local ok, v = pcall(function() return vehicle:GetCurrentSpeed() end)
  return ok and math.abs(v or 0) or 0
end

local function begin(sys, vehicle, dest, toPin)
  local pos, fwd = vehicle:GetWorldPosition(), vehicle:GetWorldForward()
  test = {
    id = os.date("%Y%m%d_%H%M%S"),
    start = { pos = xyz(pos), fwd = xyz(fwd) },
    dest = xyz(dest),
    toPin = toPin,
    phase = "ready",
    saved = {},
    replans = 0,
  }
  deps.nav.SetDestination(test.dest)
  -- (A random destination gets a marker; the GPS on the minimap still leads
  -- to the player's own pin, if there is one.)
  if not toPin then pcall(function() sys:ShowTestPin(Vector4.new(dest.x, dest.y, dest.z, 1)) end) end
  pcall(function() sys:StartInputWatch() end)
  deps.log(string.format("Driving test %s: from %s to %s%s", test.id, fmt(test.start.pos), fmt(test.dest),
    toPin and " (the map pin)" or ""))
  sys:ShowMessage(toPin and "Driving test: drive to your pin" or "Driving test: follow the yellow line to the marker")
end

local function canStart()
  local sys = deps.scriptSettings()
  local vehicle = deps.mountedVehicle()
  if not sys or not vehicle then return nil, nil, "get in a car first" end
  if (deps.nativeVersion() or 0) < 2900 then return nil, nil, "needs the native plugin 0.29 or later" end
  if deps.state.ownDriving then return nil, nil, "switch Autodrive+ off first" end
  return sys, vehicle, nil
end

-- A new test to the map pin (or the quest's destination): you choose the
-- roads and bends. Returns nil, or why not.
function Test.NewToPin()
  local sys, vehicle, err = canStart()
  if err then return err end
  Test.Clear()
  local dest = deps.nav.Destination(sys)
  if not dest or dest.w < 0.5 then return "set a pin on the map first" end
  begin(sys, vehicle, dest, true)
  return nil
end

-- A new test: a destination minDist..maxDist metres away by road, from
-- where the car is. Returns nil, or why not.
function Test.New(minDist, maxDist)
  local sys, vehicle, err = canStart()
  if err then return err end
  Test.Clear()
  local pos, fwd = vehicle:GetWorldPosition(), vehicle:GetWorldForward()
  local seed = math.floor(os.time() % 100000)
  local ok, dest = pcall(function()
    return Game.BCAI_NavRandom(deps.autodriveSystem(), pos, fwd, minDist, maxDist, seed)
  end)
  if not ok or not dest or dest.w < 0.5 then
    return "no destination found from here (stop on an ordinary road, facing along it, and try again)"
  end
  begin(sys, vehicle, dest, false)
  return nil
end

-- The route as planned from the start, a point every 2 m.
local function saveRoute()
  local okPts, points = pcall(function()
    local p = test.start.pos
    return Game.BCAI_NavAhead(Vector4.new(p.x, p.y, p.z, 1), 20000, 2)
  end)
  if not okPts or not points or #points < 2 then return end
  local name = "test_" .. test.id .. "_route.txt"
  local file = io.open(name, "w")
  if not file then return end
  file:write(string.format("# BCAI driving test %s: %s\n", test.id, tostring(deps.nav.Summary())))
  file:write(string.format("# start %.2f %.2f %.2f heading %.4f %.4f; destination %.2f %.2f %.2f\n",
    test.start.pos.x, test.start.pos.y, test.start.pos.z, test.start.fwd.x, test.start.fwd.y,
    test.dest.x, test.dest.y, test.dest.z))
  for _, p in ipairs(points) do file:write(string.format("%.2f %.2f %.2f\n", p.x, p.y, p.z)) end
  file:close()
  test.routeFile = name
  table.insert(test.saved, name)
end

local function beginRun(who)
  test.run = { who = who, rows = {}, t = 0, since = 0, started = os.date("%Y-%m-%d %H:%M:%S"),
    route = deps.nav.Summary(), replans = 0 }
  if who ~= "player" then
    test.run.style = deps.styleName()
  else
    -- (Watching the player's inputs afresh: the style picker shares the watch.)
    local sys = deps.scriptSettings()
    if sys then pcall(function() sys:StartInputWatch() end) end
  end
end

-- One row: time, position, heading, velocity, speed, the controls (the
-- player's inputs, or what Autodrive+ set), how far along and off the
-- route, and whether the game calls the car skidding.
local function sample(vehicle)
  local run = test.run
  local p, f, v = vehicle:GetWorldPosition(), vehicle:GetWorldForward(), vehicle:GetLinearVelocity()
  local throttle, brake, steer, handbrake = 0, 0, 0, 0
  if run.who == "player" then
    local ok, input = pcall(function() return deps.scriptSettings():InputWatchValues() end)
    if ok and input then throttle, brake, steer, handbrake = input.x, input.y, input.z, input.w end
  else
    local hud = deps.state.hud or {}
    throttle, brake, steer = hud.throttle or 0, hud.brake or 0, hud.steer or 0
  end
  local status = deps.nav.Status() or {}
  local okSkid, skid = pcall(function() return vehicle:IsSkidding(0.3) end)
  table.insert(run.rows, string.format("%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.0f,%.1f,%d",
    run.t, p.x, p.y, p.z, f.x, f.y, f.z, v.x, v.y, v.z, speedOf(vehicle), throttle, brake, steer, handbrake,
    status.left or -1, status.off or -1, (okSkid and skid) and 1 or 0))
end

local function endRun(why)
  local run = test.run
  test.run = nil
  if not run or #run.rows == 0 then return end
  local name = "test_" .. test.id .. "_" .. (run.who == "player" and "player" or ("ai_" .. (run.style or "?"))) .. ".csv"
  local file = io.open(name, "w")
  if not file then
    deps.log("Driving test: could not write " .. name)
    return
  end
  local car = deps.carData()
  file:write(string.format("# BCAI driving test %s, %s%s, %s, ended: %s\n", test.id, run.who,
    run.style and (" (" .. run.style .. ")") or "", run.started, why))
  file:write(string.format("# route: %s; planned again %d times on the way\n", tostring(deps.nav.Summary()),
    run.replans))
  if car then file:write("# car: " .. car.raw .. "\n") end
  file:write("t,x,y,z,fx,fy,fz,vx,vy,vz,speed,throttle,brake,steer,handbrake,left,off,skid\n")
  for _, row in ipairs(run.rows) do file:write(row, "\n") end
  file:close()
  table.insert(test.saved, name)
  deps.log(string.format("Driving test %s: %s's drive saved (%s, %.0f s): %s", test.id, run.who, why, run.t, name))
  local sys = deps.scriptSettings()
  if sys then sys:ShowMessage(string.format("Driving test: %s's drive saved (%.0f s)", run.who, run.t)) end
end

-- Every frame.
function Test.Tick(dt)
  if not test then return end
  local vehicle = deps.mountedVehicle()
  if not vehicle then
    if test.run then endRun("left the car") end
    if test.phase == "player" then test.phase = "driven" end
    return
  end
  local speed = speedOf(vehicle)
  if test.phase == "ready" then
    -- (The route once the line is planned to the test's destination, not
    -- still to wherever it went before.)
    local goal = deps.nav.Goal()
    local planned = goal and math.abs(goal.x - test.dest.x) + math.abs(goal.y - test.dest.y) < 5
    if not test.routeFile and planned then saveRoute() end
    -- (Recording starts as the car drives off.)
    if speed > 0.5 then
      if not test.routeFile and planned then saveRoute() end
      test.phase = "player"
      beginRun("player")
    end
  elseif test.phase == "placing" then
    test.wait = test.wait + dt
    test.still = speed < 0.3 and test.still + dt or 0
    if test.still >= 1.5 or test.wait > 6 then
      test.phase = "ai"
      beginRun("ai")
      deps.startOwnDriver()
      if not deps.state.ownDriving then
        endRun("Autodrive+ did not start")
        test.phase = "driven"
      end
    end
  end
  local run = test.run
  if not run then return end
  if not test.routeFile and test.phase == "player" then
    local goal = deps.nav.Goal()
    if goal and math.abs(goal.x - test.dest.x) + math.abs(goal.y - test.dest.y) < 5 then
      saveRoute()
    end
  end
  -- (Off the route, the line plans a new one: counted, so the comparison
  -- knows the drives may have taken different roads.)
  local summary = deps.nav.Summary()
  if summary ~= run.route then
    if run.route then run.replans = run.replans + 1 end
    run.route = summary
  end
  run.t = run.t + dt
  run.since = run.since + dt
  if run.since >= SAMPLE then
    run.since = 0
    sample(vehicle)
  end
  local left = (deps.nav.Status() or {}).left
  if test.phase == "player" and left and left < ARRIVED and speed < 1.5 then
    endRun("arrived")
    test.phase = "driven"
  elseif test.phase == "ai" and not deps.state.ownDriving then
    endRun(left and left < ARRIVED and "arrived" or "Autodrive+ stopped")
    test.phase = "done"
  end
end

-- The player's drive so far, saved now.
function Test.Stop()
  if not test then return end
  if test.run then
    local who = test.run.who
    endRun("stopped by hand")
    if who == "ai" then
      deps.stopOwnDriver("driving test stopped", "Autodrive+: OFF")
      test.phase = "done"
    else
      test.phase = "driven"
    end
  end
end

-- Autodrive+ drives the same route: the car back at the start, facing the
-- same way, then Autodrive+ in the style chosen.
function Test.RunAI()
  if not test or test.run then return "a drive is being recorded" end
  local sys = deps.scriptSettings()
  if not sys then return "scripts not loaded" end
  local p, f = test.start.pos, test.start.fwd
  local err = sys:TeleportVehicle(Vector4.new(p.x, p.y, p.z + 0.3, 1), Vector4.new(f.x, f.y, f.z, 0))
  if err ~= "" then return err end
  test.phase = "placing"
  test.wait, test.still = 0, 0
  return nil
end

function Test.Clear()
  if not test then return end
  if test.run then Test.Stop() end
  deps.nav.SetDestination(nil)
  local sys = deps.scriptSettings()
  if sys then
    pcall(function() sys:StopInputWatch() end)
    pcall(function() sys:HideTestPin() end)
  end
  test = nil
end

return Test
