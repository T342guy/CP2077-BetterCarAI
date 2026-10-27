-- Better Car AI - CET half: runs BCAI Autodrive+ (the native plugin's own
-- driving AI) in the chosen driving style, the settings windows (ui.lua),
-- the debug overlay (overlay.lua), and the older drivers: the game's
-- autodrive with TweakDB profiles (profiles.lua) and AI drive.

local Profiles = require("profiles")
local UI = require("ui")
local Overlay = require("overlay")
local Nav = require("nav")
local DriveTest = require("drivetest")
local log = Profiles.log

local SETTINGS_FILE = "settings.json"
-- Places where AI drive lost it on its own (see BCAISettings trouble spots).
local SPOTS_FILE = "trouble_spots.json"
-- Bump when defaults change in a way that saved settings should not override.
local SETTINGS_VERSION = 11

local defaults = {
  -- NPC traffic reactions (applied by the redscript half)
  calmTraffic = true,
  panicImpactKmh = 25.0,
  honkOnTap = true,
  -- People on foot don't flee from a light knock between cars nearby.
  calmPedestrians = true,
  -- The tap (within 30 s of the last) at which a driver loses patience and
  -- reacts the game's way (drives off in a panic, or gets out); 0: never.
  tapPatience = 4,
  -- Panic driving (fleeing a crash or gunfire): this x its top speed, and
  -- this x as far ahead looking for bends.
  trafficCalmPanic = true,
  trafficPanicSpeed = 0.85,
  trafficPanicLookahead = 1.5,

  -- NPC traffic driving
  trafficTuning = true,
  trafficCornerGrip = 1.10,
  trafficLookahead = 1.25,

  -- Autodrive, normal mode
  normalMaxSpeedKmh = 0.0,
  normalObeySpeedLimit = true,
  normalCornerGrip = 1.05,
  normalLookahead = 1.25,

  -- Autodrive, aggressive mode. Built on the car's own autodrive params: the
  -- race params rev and brake constantly when used for autodrive.
  aggressive = false,
  aggroUseRaceParams = false,
  aggroMaxSpeedKmh = 250.0,
  aggroClearTraffic = true,
  aggroRunLights = false,
  aggroIgnoreSpeedLimit = true,
  aggroSpeedLimitBonusKmh = 0.0,
  aggroExtraSweeps = false,
  aggroCornerGrip = 1.12,
  aggroBrakeSafety = 1.00,
  aggroSwerve = 1.60,
  -- Obstacle vision is only ~13 m in vanilla, so swerves come far too late.
  aggroLookahead = 3.00,
  aggroSteerReach = 1.80,

  -- Starting autodrive starts AI drive instead.
  aiDriveDefault = true,
  -- AI drive: fast, so corner more cautiously than autodrive.
  -- Braking early (not cornering slowly) is what stops high-speed spins.
  aiCornerGrip = 1.00,
  aiBrakeSafety = 0.65,
  aiStability = 2.00,
  -- The drive command's minSpeed: the driver takes tight corners at about this.
  aiMinSpeedKmh = 30.0,
  aiSteerReach = 2.00,
  -- Recordings: every high-speed wipe-out started with the driver snapping
  -- the wheel (0 to 100 deg/s of yaw in 0.3 s), so steer and swerve slower.
  aiSteerRate = 0.65,
  -- ~13 m vanilla vision x6 = ~80 m, about 2 s at 150 km/h.
  aiLookahead = 6.00,
  aiReturnRate = 0.50,
  -- Against the wiggle at speed: gentler swerves, ignore tiny path errors.
  aiSwerve = 0.70,
  aiSteerDamping = 1.60,
  aiBrakeSmooth = 0.65,
  -- AI drive on motorcycles. Bikes steer by leaning, so fast steering
  -- corrections set up a wobble: steer slower, aim further ahead, ignore small
  -- path errors. They also turn tighter, so they can corner faster.
  -- Lowers the speed cap while sliding, weaving, airborne or before a crest.
  aiGovernor = true,
  -- Remember where AI drive snapped the wheel or took a bend far too fast,
  -- and pass those spots at this speed from then on.
  aiTroubleSpots = true,
  -- The worst lane-path jog (a T-junction) still spun the car at 53 km/h.
  aiTroubleSpotKmh = 40.0,
  -- With the native plugin: slow down before bends and junction jogs in the
  -- AI's route ahead so each can be taken at this much cornering grip (g).
  aiRoutePlanner = true,
  aiRouteGrip = 0.8,
  -- With the native plugin: reconnect junctions whose lanes are wired to the
  -- wrong side of the road (the route jogs across the road there).
  aiRepairLanes = true,
  -- BCAI Autodrive+ (native plugin 0.16+): our own driving AI. Plans over the lane map and drives
  -- the car through its own controls.
  ownMaxSpeedKmh = 180.0,
  -- Cornering grip (g) the speed plan allows for in bends. (The game's
  -- cars hold about 2 g, the body 8-15 degrees into the bend: driving tests.)
  ownCornerGrip = 1.3,
  -- Speed on a road = its traffic speed x this (city 36 -> 79, highway 61 -> 134 km/h).
  ownLaneSpeedFactor = 2.2,
  ownBrakeDecel = 4.0,
  ownSensorGroup = "Vehicle",
  ownFollowTime = 1.5,
  -- Driving style: 0 city, 1 aggressive, 2 madman. The own* values above are
  -- the aggressive style's; city* and mad* the others'.
  ownStyle = 1,
  -- Show the style picker each time Autodrive+ starts.
  ownAskStyle = true,
  cityMaxSpeedKmh = 110.0,
  cityLaneSpeedFactor = 1.3,
  cityCornerGrip = 0.7,
  cityBrakeDecel = 3.0,
  cityFollowTime = 2.0,
  madMaxSpeedKmh = 250.0,
  madLaneSpeedFactor = 3.0,
  madCornerGrip = 1.7,
  madBrakeDecel = 5.0,
  madFollowTime = 0.8,
  -- Starting autodrive starts Autodrive+ (instead of AI drive).
  ownTakeOverAutodrive = false,
  -- Autodrive+ watches for people on foot (needs the native plugin 0.31):
  -- stops for people crossing, goes round someone standing in the road, and
  -- in City slows for crosswalks with someone by them.
  ownPeople = true,
  -- While Autodrive+ drives, show the game's autodrive HUD (the wheel icon on
  -- the minimap, the side panel with distance and time left).
  ownGameHud = true,
  -- Write what Autodrive+ does to the log twice a second, raw numbers
  -- ("Autodrive+ raw:" lines).
  ownLog = true,
  -- Write each decision Autodrive+ makes and why ("Autodrive+ |" lines:
  -- lane changes, passing, what it slows for, backing up, leaving the road).
  ownLogEvents = true,
  -- Write what NPC drivers did about being bumped ("Traffic |" lines).
  trafficLog = true,
  -- Our own route to the map destination (native plugin 0.28+): planned for
  -- time, ending on the pin's level; drawn on the road ahead, with the next
  -- turn at the top of the screen. Routes go to the log ("Route |" lines).
  navGuide = true,
  navLine = true,
  navHints = true,
  navLog = true,
  -- Which extra windows are open (CET overlay).
  uiAdvancedWindow = false,
  uiDebugWindow = false,
  -- The overlay: how much the panel shows (0 compact, 1 normal, 2
  -- everything) and which layers it draws.
  overlayDetail = 1,
  overlayTrail = true,
  overlayRoute = true,
  overlaySensor = true,
  overlayRays = true,
  overlayPredict = true,
  -- Keeps the last 60 s of driving; saves it automatically when AI drive wipes out.
  recorder = true,
  aiBikeMinSpeedKmh = 45.0,
  aiBikeCornerGrip = 1.15,
  aiBikeBrakeSafety = 0.70,
  aiBikeStability = 1.30,
  aiBikeSteerReach = 2.60,
  aiBikeSteerRate = 0.55,
  aiBikeLookahead = 6.00,
  aiBikeReturnRate = 0.40,
  aiBikeSwerve = 0.80,
  aiBikeSteerDamping = 2.40,
  aiBikeBrakeSmooth = 0.65,
  aggroPowerBoost = 1.25,
}

-- Keys reset to the new defaults when migrating from an older settings version.
local migrations = {
  [2] = { "aggroUseRaceParams", "aggroBrakeSafety", "aggroLookahead" },
  [3] = { "aggroLookahead" },
  [4] = { "aiCornerGrip", "aiBrakeSafety", "aiStability" },
  [5] = { "aiCornerGrip" },
  [6] = { "aiBrakeSafety", "aiMinSpeedKmh", "aiSteerReach", "aiSteerRate" },
  [7] = { "aiSteerRate", "aiSwerve" },
  [8] = { "aiTroubleSpotKmh" },
  [9] = { "ownMaxSpeedKmh", "ownLaneSpeedFactor", "ownCornerGrip" },
  [10] = {},
  -- (0.30: the styles' cornering grip, measured in driving tests.)
  [11] = { "cityCornerGrip", "ownCornerGrip", "madCornerGrip" },
}

local settings = {}
local state = {
  ready = false,
  overlayOpen = false,
  dirty = false,
  pollTimer = 0,
  trace = false,
  aiDriveTimer = 0,
  traceTimer = 0,
  debugDraw = false,
  debugTimer = 0,
  debugError = nil,
  debugFovHorizontal = false,
  debugManualProjection = false,
  inMenu = false,
  troubleSpots = {},
  debugLines = {},
  -- Autodrive+'s account of what it is doing (overlay.lua's ParseHud).
  hud = {},
  -- Its latest decisions, newest last: { text, at = os.clock() }.
  events = {},
  -- The style picker while it is shown: { choice, left, phase }.
  picker = nil,
}

local function saveSettings()
  local file = io.open(SETTINGS_FILE, "w")
  if not file then return end
  settings.version = SETTINGS_VERSION
  file:write(json.encode(settings))
  file:close()
end

local function loadSettings()
  for k, v in pairs(defaults) do settings[k] = v end
  local file = io.open(SETTINGS_FILE, "r")
  if not file then return end
  local ok, saved = pcall(json.decode, file:read("*a"))
  file:close()
  if not ok or type(saved) ~= "table" then return end
  for k, v in pairs(saved) do
    if defaults[k] ~= nil and type(v) == type(defaults[k]) then settings[k] = v end
  end
  local version = type(saved.version) == "number" and saved.version or 1
  if version < SETTINGS_VERSION then
    for v = version + 1, SETTINGS_VERSION do
      for _, k in ipairs(migrations[v] or {}) do settings[k] = defaults[k] end
    end
    saveSettings()
    Profiles.log("migrated settings from version " .. version)
  end
end

local function scriptSettings()
  local container = Game.GetScriptableSystemsContainer()
  return container and container:Get("BCAISettings")
end

local function saveTroubleSpots()
  local file = io.open(SPOTS_FILE, "w")
  if not file then return end
  file:write(json.encode(state.troubleSpots))
  file:close()
end

local function loadTroubleSpots()
  local file = io.open(SPOTS_FILE, "r")
  if not file then return end
  local ok, spots = pcall(json.decode, file:read("*a"))
  file:close()
  if ok and type(spots) == "table" then state.troubleSpots = spots end
end

-- Saves spots the scripts learned since last time, and hands the whole list
-- to a freshly loaded game (scriptable systems start empty on every load).
local function syncTroubleSpots(sys)
  local learned = sys:TakeNewTroubleSpots() or {}
  for _, line in ipairs(learned) do
    local x, y, z, dx, dy = line:match("([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)")
    if x then
      table.insert(state.troubleSpots, { x = tonumber(x), y = tonumber(y), z = tonumber(z),
        dx = tonumber(dx), dy = tonumber(dy), learned = os.date("%Y-%m-%d %H:%M") })
      log(string.format("learned trouble spot at (%.0f, %.0f)", tonumber(x), tonumber(y)))
    end
  end
  if #learned > 0 then
    saveTroubleSpots()
    sys:ShowMessage("AI drive: trouble spot learned, will slow down here next time")
  end
  if sys:TroubleSpotCount() ~= #state.troubleSpots then
    sys:ClearTroubleSpots()
    for _, p in ipairs(state.troubleSpots) do sys:AddTroubleSpot(p.x, p.y, p.z, p.dx, p.dy) end
  end
end

-- The RED4ext plugin (optional). Its natives exist only while it is loaded.
local function nativeVersion()
  local ok, version = pcall(function() return Game.BCAI_NativeVersion() end)
  if ok and type(version) == "number" and version > 0 then return version end
  return nil
end

local function pushToScripts()
  local sys = scriptSettings()
  if not sys then return end
  sys:SetTroubleSpots(settings.aiTroubleSpots, settings.aiTroubleSpotKmh / 3.6)
  syncTroubleSpots(sys)
  sys:SetCalmTrafficEnabled(settings.calmTraffic)
  sys:SetPanicImpactThreshold(settings.panicImpactKmh / 3.6)
  sys:SetHonkOnTap(settings.honkOnTap)
  sys:SetTrafficReactions(settings.calmPedestrians, math.floor(settings.tapPatience + 0.5), settings.trafficLog)
  sys:SetAggressiveMode(settings.aggressive)
  sys:SetAutodriveSpeeds(settings.normalMaxSpeedKmh / 3.6, settings.aggroMaxSpeedKmh / 3.6)
  sys:SetAggroTraffic(settings.aggroClearTraffic, settings.aggroRunLights)
  sys:SetStabilityGovernor(settings.aiGovernor)
  sys:SetAutodrivePlusTakeover(settings.ownTakeOverAutodrive and (nativeVersion() or 0) >= 1900)
  sys:SetAutodrivePlusPeople(settings.ownPeople and (nativeVersion() or 0) >= 3100)
  sys:SetAIDriveMinSpeed((state.aiDriveBike and settings.aiBikeMinSpeedKmh or settings.aiMinSpeedKmh) / 3.6)
end

local function mountedVehicle()
  local player = Game.GetPlayer()
  return player and player:GetMountedVehicle()
end

-- The mounted car's physics as its records give them (Settings.reds
-- CarData), parsed, with a one-line summary; read again when the car changes.
local function carData()
  local vehicle = mountedVehicle()
  local sys = scriptSettings()
  if not vehicle or not sys then return nil end
  local okId, key = pcall(function() return TDBID.ToStringDEBUG(vehicle:GetRecordID()) end)
  if state.carData and okId and state.carData.key == key then return state.carData end
  local ok, text = pcall(function() return sys:CarData() end)
  if not ok or not text or text == "" then return nil end
  local c = { key = okId and key or "?", raw = tostring(text) }
  for k, v in c.raw:gmatch("([%w_]+)=([^;]*)") do c[k] = tonumber(v) or v end
  local parts = {}
  local mass = (c.mass or 0) > 0 and c.mass or c.recordMass
  if mass then table.insert(parts, string.format("%.0f kg", mass)) end
  if (c.length or 0) > 1 then table.insert(parts, string.format("%.1f x %.1f m", c.length, c.width or 0)) end
  if c.frontFriction and c.backFriction then
    local f, b = c.frontFriction * (c.frontLat or 1), c.backFriction * (c.backLat or 1)
    table.insert(parts, string.format("tyres %.2f front, %.2f rear (sideways friction)", f, b))
  end
  -- (Braking torque at the four wheels over the tyre radius and the mass:
  -- what the brakes could do, if the tyres hold.)
  if c.frontBrake and c.backBrake and c.tireRadius and c.tireRadius > 0 and mass and mass > 0 then
    table.insert(parts, string.format("brakes up to %.1f m/s2", 2 * (c.frontBrake + c.backBrake) / c.tireRadius / mass))
  end
  if c.engineTorque then table.insert(parts, string.format("engine %.0f Nm", c.engineTorque)) end
  if c.wheelTurn then table.insert(parts, string.format("steering lock %.0f deg", c.wheelTurn)) end
  c.summary = table.concat(parts, ", ")
  state.carData = c
  return c
end

local function autodriveSystem()
  local ok, sys = pcall(function()
    return Game.GetScriptableSystemsContainer():Get(CName.new("AutoDriveSystem"))
  end)
  return ok and sys or nil
end

-- What the plugin looks at: the car, its AI component, the drive command and
-- the autodrive system (the plugin adds the traffic system behind it).
local function nativeRoots()
  local vehicle = mountedVehicle()
  if not vehicle then return nil, {} end
  local roots = { { "vehicle", vehicle } }
  local ai = vehicle:GetAIComponent()
  if ai then table.insert(roots, { "aicomponent", ai }) end
  local sys = scriptSettings()
  local cmd = sys and sys:GetDriveCommand()
  if cmd then table.insert(roots, { "drivecommand", cmd }) end
  local autodrive = autodriveSystem()
  if autodrive then table.insert(roots, { "autodrive", autodrive }) end
  return vehicle, roots
end

-- Scans for the AI's planned path from each root (best while AI drive is
-- running). Results go to red4ext/plugins/BetterCarAI/dumps.
local function nativeScanForPath()
  if not nativeVersion() then
    log("path scan: native plugin not loaded")
    return
  end
  local vehicle, roots = nativeRoots()
  if not vehicle then
    log("path scan: not in a vehicle")
    return
  end
  -- The lane graph lets the scan recognise routes as chains of linked lanes.
  local autodrive = autodriveSystem()
  local okLanes, lanes = pcall(function() return autodrive and Game.BCAI_LoadLanes(autodrive) or 0 end)
  log("path scan: lane graph " .. (okLanes and tostring(lanes) .. " lanes" or "unavailable"))
  local origin = vehicle:GetWorldPosition()
  local heading = vehicle:GetWorldForward()
  for _, root in ipairs(roots) do
    -- Each scan stops after ~1 s, nearest objects first.
    local ok, result = pcall(function() return Game.BCAI_ScanForPath(root[2], root[1], origin, heading, 5) end)
    log("path scan: " .. tostring(result))
    if not ok then break end
  end
  local sys = scriptSettings()
  if sys then sys:ShowMessage("Path scan written to red4ext/plugins/BetterCarAI/dumps") end
  -- A recording of where the car actually goes next shows which scanned
  -- array is its route: the route's points lie on that trajectory.
  -- Not restarted by more scans meanwhile; the 60 s recording covers those too.
  if settings.recorder and not state.scanFollowUp then state.scanFollowUp = 20.0 end
end

local function nativeDumpObjects()
  local vehicle, roots = nativeRoots()
  if not vehicle then return end
  for _, root in ipairs(roots) do
    local ok, result = pcall(function() return Game.BCAI_DumpObject(root[2], 2) end)
    log("object dump " .. root[1] .. ": " .. tostring(result))
  end
end

-- Re-point the mounted vehicle at the current mode's params; restart autodrive
-- if it is running so the native driver picks them up.
local function syncVehicle(restart)
  -- AI drive owns the vehicle's params while it runs.
  if state.aiDriveVehicle then return end
  local vehicle = mountedVehicle()
  if not vehicle then return end
  local changed = Profiles.ApplyToVehicle(vehicle, settings)
  if changed and restart then
    local sys = scriptSettings()
    if sys then sys:RestartAutodrive() end
  end
end

local function toggleAggressive()
  settings.aggressive = not settings.aggressive
  saveSettings()
  pushToScripts()
  syncVehicle(true)
  local sys = scriptSettings()
  if sys then
    sys:ShowMessage(settings.aggressive and "Game's autodrive: aggressive tuning" or "Game's autodrive: normal tuning")
  end
end

-- Experimental: drive with the AI command police use, whose speed cap the
-- native driver honours (player autodrive is stuck at ~24 m/s).
local function toggleAIDrive()
  local sys = scriptSettings()
  if not sys then return end
  if not sys:IsCommandDriving() then state.aiDriveBike = sys:IsMountedBike() end
  pushToScripts()
  local msg
  if sys:IsCommandDriving() then
    sys:StopCommandDrive()
    Profiles.SetAIDrive(state.aiDriveVehicle, settings, false)
    state.aiDriveVehicle = nil
    state.aiDriveBike = false
    msg = "AI drive: OFF"
  else
    local vehicle = mountedVehicle()
    if vehicle then
      state.aiDriveVehicle = Profiles.idName(vehicle:GetRecordID())
      Profiles.SetAIDrive(state.aiDriveVehicle, settings, true, state.aiDriveBike)
    end
    msg = sys:StartCommandDrive()
  end
  sys:ShowMessage(msg)
  log(msg)
end

local function ownDriverAvailable()
  return (nativeVersion() or 0) >= 1900
end

-- Driving styles (plugin 0.26+): each keeps its own speed, cornering,
-- braking and following settings under its prefix.
local STYLE_PREFIX = { [0] = "city", "own", "mad" }
local STYLE_FIELDS = { "MaxSpeedKmh", "LaneSpeedFactor", "CornerGrip", "BrakeDecel", "FollowTime" }

local function stylesAvailable()
  return (nativeVersion() or 0) >= 2600
end

local function stylePrefix(style)
  return STYLE_PREFIX[style] or "own"
end

-- Tells the plugin the style, when it isn't the one it has.
local function sendStyle()
  if not stylesAvailable() or state.styleSent == settings.ownStyle then return end
  local ok = pcall(function() Game.BCAI_DriverStyle(settings.ownStyle) end)
  if ok then state.styleSent = settings.ownStyle end
end

local function setStyle(style)
  settings.ownStyle = math.max(0, math.min(2, style))
  saveSettings()
  sendStyle()
  local sys = scriptSettings()
  if sys and state.ownDriving then
    sys:ShowMessage("Autodrive+ style: " .. Overlay.STYLE_NAMES[settings.ownStyle + 1])
  end
end

-- Autodrive+'s decisions since last asked (plugin 0.27+): to the log and
-- the overlay's list.
local function takeOwnEvents()
  if (nativeVersion() or 0) < 2700 then return end
  local ok, text = pcall(function() return Game.BCAI_DriverEvents() end)
  if not ok or not text or text == "" then return end
  for line in tostring(text):gmatch("[^\n]+") do
    if settings.ownLogEvents then log("Autodrive+ | " .. line) end
    table.insert(state.events, { text = line, at = os.clock() })
    if #state.events > 8 then table.remove(state.events, 1) end
  end
end

local function stopOwnDriver(why, message)
  local sys = scriptSettings()
  pcall(function() Game.BCAI_DriverStop(why) end)
  takeOwnEvents()
  if sys then sys:EndOwnDriver(why) end
  state.ownDriving = false
  state.hud = {}
  log("Autodrive+ stopped: " .. why)
  -- (The button press that stopped it may also come through as a request
  -- to start: not for a moment.)
  state.requestQuietUntil = os.clock() + 1.5
  if sys then sys:ShowMessage(message or ("Autodrive+: OFF (" .. why:sub(1, 60) .. ")")) end
end

local function startOwnDriver()
  local sys = scriptSettings()
  if not sys or not ownDriverAvailable() then return end
  if state.aiDriveVehicle then toggleAIDrive() end
  local err = sys:BeginOwnDriver(settings.ownSensorGroup)
  if err ~= "" then
    sys:ShowMessage("Autodrive+: " .. err)
    return
  end
  local vehicle = sys:OwnDriverVehicle()
  -- The pin itself (the game's autodrive point may be on the wrong level),
  -- or a driving test's destination.
  local goal = Nav.Destination(sys)
  -- The car's own size first (the road room it plans with depends on it).
  local body = sys:OwnDriverBody()
  pcall(function() Game.BCAI_DriverBody(body) end)
  -- Steering geometry from the records: wheelbase (from the measured body),
  -- steering lock and mass. The plugin uses it as the ceiling on the learned
  -- full-lock turn, so a long truck cannot "learn" a tighter turn than its
  -- wheels allow.
  local car = carData()
  if car and (car.wheelTurn or 0) > 0 then
    local wheelbase = 0.9 * (body.x + body.y)
    local mass = car.mass or car.recordMass or 0
    pcall(function() Game.BCAI_DriverProfile(vehicle, Vector4.new(wheelbase, car.wheelTurn, mass, 0)) end)
  end
  local ok, result = pcall(function()
    return Game.BCAI_DriverStart(autodriveSystem(), vehicle:GetWorldPosition(), vehicle:GetWorldForward(), goal, goal.w > 0.5)
  end)
  result = tostring(result)
  if not ok or result:sub(1, 5) ~= "route" then
    sys:EndOwnDriver("did not start")
    sys:ShowMessage("Autodrive+: " .. result)
    log("Autodrive+ did not start: " .. result)
    takeOwnEvents()
    return
  end
  state.ownDriving = true
  state.events = {}
  state.ownLogTimer = 0
  state.styleSent = nil
  sendStyle()
  local msg = "Autodrive+: ON, " .. Overlay.STYLE_NAMES[settings.ownStyle + 1] .. " (" .. result .. ")"
  log(msg .. string.format(", own body: front %.1f m, rear %.1f m from centre, %.2f m wide, rays %.1f m up%s",
      body.x, body.y, 2 * body.z, body.w, (body.x + body.y > 7.0) and " (long: swing-aware)" or "")
    .. (goal.w > 0.5 and ("; destination: " .. tostring(sys:NavDestinationInfo())) or ""))
  state.ownVehicleTimer = 0
  state.ownVehiclesFailed = false
  if settings.ownGameHud then sys:ShowOwnDriverHud(true, goal.w < 0.5) end
  if car then log("Autodrive+: the car's records: " .. car.summary .. " (" .. car.raw .. ")") end
  sys:ShowMessage(msg)
end

local function toggleOwnDriver()
  local sys = scriptSettings()
  if not sys or not ownDriverAvailable() then return end
  if state.ownDriving then
    stopOwnDriver("switched off", "Autodrive+: OFF")
  else
    startOwnDriver()
  end
end

-- The style picker: shown for a few seconds before Autodrive+ starts. The
-- player's inputs choose (the car is still theirs until it starts): steering
-- moves the choice, the autodrive button again or throttle starts, brake
-- cancels; at the end of the time it starts with the choice. Then it waits
-- for the controls to be let go (they would take over again at once).
local PICKER_SECONDS = 6.0

local function closePicker()
  local sys = scriptSettings()
  if sys then sys:StopInputWatch() end
  state.picker = nil
end

local function confirmPicker()
  local picker = state.picker
  if not picker then return end
  settings.ownStyle = picker.choice
  saveSettings()
  picker.phase = "release"
  picker.left = 3.0
end

-- Starts Autodrive+, through the picker when that is on.
local function requestOwnDriver()
  local sys = scriptSettings()
  if not sys or not ownDriverAvailable() or state.ownDriving then return end
  if not settings.ownAskStyle or not stylesAvailable() then return startOwnDriver() end
  if not mountedVehicle() then
    sys:ShowMessage("Autodrive+: not in a vehicle")
    return
  end
  sys:StartInputWatch()
  state.picker = { choice = settings.ownStyle, left = PICKER_SECONDS, phase = "choose", steer = 0 }
end

local function pickerTick(dt)
  local picker = state.picker
  local sys = scriptSettings()
  if not sys or not mountedVehicle() then return closePicker() end
  -- (The autodrive button again: start with the style chosen.)
  local again = sys:TakeAutodrivePlusRequest()
  local input = sys:InputWatchValues()
  picker.left = picker.left - dt
  if picker.phase == "choose" then
    if input.y > 0.5 then
      closePicker()
      sys:ShowMessage("Autodrive+: cancelled")
      return
    end
    -- (Steering: one step per push; TurnX is + to the right.)
    if math.abs(input.z) > 0.5 and math.abs(picker.steer) <= 0.5 then
      picker.choice = math.max(0, math.min(2, picker.choice + (input.z > 0 and 1 or -1)))
    end
    picker.steer = input.z
    if again or input.x > 0.5 or picker.left <= 0 then confirmPicker() end
  elseif (math.abs(input.x) < 0.2 and math.abs(input.y) < 0.2 and math.abs(input.z) < 0.2) or picker.left <= 0 then
    closePicker()
    startOwnDriver()
  end
end

-- Every frame while Autodrive+ drives: sense, then let the plugin drive.
local function ownDriverTick(dt)
  local sys = scriptSettings()
  if not sys then return end
  local why = sys:OwnDriverCheck()
  if why ~= "" then return stopOwnDriver(why) end
  -- The autodrive button again: switch off.
  if sys:TakeAutodrivePlusRequest() then return stopOwnDriver("autodrive button", "Autodrive+: OFF") end
  local vehicle = sys:OwnDriverVehicle()
  sendStyle()
  local p = stylePrefix(settings.ownStyle)
  local driveSettings = Vector4.new(settings[p .. "MaxSpeedKmh"] / 3.6, settings[p .. "CornerGrip"] * 9.81,
    settings[p .. "LaneSpeedFactor"], settings[p .. "BrakeDecel"])
  local ok, result = pcall(function()
    return Game.BCAI_DriverStep(vehicle, vehicle:GetWorldPosition(), vehicle:GetWorldForward(),
      vehicle:GetLinearVelocity(), dt, driveSettings, settings[p .. "FollowTime"])
  end)
  if not ok then return stopOwnDriver("error: " .. tostring(result)) end
  state.ownResult = result
  if result.w >= 2 and result.w < 3 then
    local status = tostring(Game.BCAI_DriverStatus())
    return stopOwnDriver(status, status:sub(1, 7) == "arrived" and "Autodrive+: arrived" or nil)
  end
  -- The sensor: the plugin says where to look, the scripts cast the rays,
  -- the plugin remembers what they hit (used from the next frame).
  ok, result = pcall(function()
    local rays = Game.BCAI_DriverRays(sys:OwnDriverFront())
    Game.BCAI_DriverSense(sys:OwnDriverCast(rays))
    local info = Game.BCAI_DriverInfo()
    sys:SetOwnDriverObstacle(info.x)
  end)
  if not ok then return stopOwnDriver("sensor error: " .. tostring(result)) end
  -- What it is doing, for the overlay and the settings window.
  if stylesAvailable() then
    local okHud, hud = pcall(function() return Game.BCAI_DriverHud() end)
    state.hud = okHud and Overlay.ParseHud(hud) or {}
  end
  -- The game's autodrive HUD; the game clearing its autodrive flag (combat,
  -- a drawn weapon, heavy damage, a quest) hands back control as it would.
  if settings.ownGameHud and not sys:RefreshOwnDriverHud(state.hud.left or -1) then
    return stopOwnDriver("the game switched autodrive off (combat, a weapon, damage or a quest)")
  end
  takeOwnEvents()
  -- The vehicles the game knows of nearby, ten times a second. (Failing here
  -- only costs that: the rays still see them.)
  state.ownVehicleTimer = (state.ownVehicleTimer or 0) + dt
  if state.ownVehicleTimer >= 0.1 and not state.ownVehiclesFailed then
    state.ownVehicleTimer = 0
    ok, result = pcall(function() return Game.BCAI_DriverVehicles(sys:OwnDriverVehicles()) end)
    if not ok then
      state.ownVehiclesFailed = true
      log("Autodrive+: the game's vehicle list failed (" .. tostring(result) .. "); rays only")
    end
  end
  if settings.ownLog then
    state.ownLogTimer = (state.ownLogTimer or 0) + dt
    if state.ownLogTimer >= 0.5 then
      state.ownLogTimer = 0
      log("Autodrive+ raw: " .. tostring(Game.BCAI_DriverTelemetry()) .. " | vehicles found by " .. tostring(sys:OwnDriverVehicleSources()))
    end
  end
end

-- Writes the drive recorder's last 60 s to rec_<date>_<time>.csv in this mod's folder.
local function saveRecording(reason)
  local sys = scriptSettings()
  if not sys then return end
  local lines = sys:RecorderLines() or {}
  if #lines == 0 then
    sys:ShowMessage("Recorder: nothing recorded yet")
    return
  end
  local name = "rec_" .. os.date("%Y%m%d_%H%M%S") .. ".csv"
  local file = io.open(name, "w")
  if not file then
    log("could not write " .. name)
    return
  end
  local vehicle = mountedVehicle()
  file:write("# reason: " .. reason .. "\n")
  file:write("# vehicle: " .. tostring(vehicle and Profiles.idName(vehicle:GetRecordID())) .. "\n")
  file:write("# settings: " .. json.encode(settings) .. "\n")
  file:write("# last stop: " .. tostring(sys:GetLastStop()) .. "\n")
  file:write(sys:RecorderHeader() .. "\n")
  for _, line in ipairs(lines) do file:write(line .. "\n") end
  file:close()
  log("recording saved: " .. name .. " (" .. reason .. ", " .. #lines .. " samples)")
  -- The route at that moment, for the analyser (plugin 0.10+).
  if (nativeVersion() or 0) >= 1000 and vehicle then
    local ok, result = pcall(function() return Game.BCAI_DumpRoute(autodriveSystem(), vehicle:GetWorldPosition()) end)
    log("route with the recording: " .. tostring(result))
  end
  sys:ShowMessage("Recording saved: " .. name)
end

-- Autodrive+, step 1 (plugin 0.14+): find where the car keeps its
-- throttle, brake and steering, then check that writing them drives the car.
local CONTROL_PROBE_SECONDS = 25
local CONTROL_TEST = {
  { name = "throttle", seconds = 2.5, controls = { 0.7, 0, 0, 0 } },
  { name = "steer +0.8", seconds = 2.0, controls = { 0.5, 0, 0.8, 0 } },
  { name = "steer -0.8", seconds = 2.0, controls = { 0.5, 0, -0.8, 0 } },
  { name = "brake", seconds = 2.5, controls = { 0, 1, 0, 0 } },
}

local function controlsAvailable()
  return (nativeVersion() or 0) >= 1400
end

local function headingDeg(vehicle)
  local f = vehicle:GetWorldForward()
  return math.deg(math.atan2(f.y, f.x))
end

local function startControlProbe()
  local sys = scriptSettings()
  if not sys or not controlsAvailable() then return end
  if not mountedVehicle() then
    sys:ShowMessage("Control probe: get in a car first")
    return
  end
  Game.BCAI_ControlProbeReset()
  sys:StartInputWatch()
  state.controlTest = nil
  state.controlProbe = { delay = 3.0, left = CONTROL_PROBE_SECONDS, nextMessage = 0, samples = 0 }
  sys:ShowMessage("Control probe: close the overlay, then drive")
  log("control probe started")
end

local function finishControlProbe(sys)
  sys:StopInputWatch()
  local ok, report = pcall(function() return Game.BCAI_ControlProbeReport() end)
  state.controlProbe = nil
  state.controlReport = tostring(report)
  log("control probe: " .. state.controlReport)
  sys:ShowMessage("Control probe done: " .. state.controlReport:sub(1, 80))
end

-- Every frame while probing: the player's inputs against the car's memory.
local function controlProbeTick(dt)
  local probe = state.controlProbe
  local sys = scriptSettings()
  if not sys then return end
  if probe.delay > 0 then
    probe.delay = probe.delay - dt
    return
  end
  local vehicle = mountedVehicle()
  -- The overlay holds the game's input, so those frames say nothing.
  if vehicle and not state.overlayOpen then
    local ok, n = pcall(function() return Game.BCAI_ControlProbe(vehicle, sys:InputWatchValues()) end)
    if ok then probe.samples = n end
    probe.left = probe.left - dt
  end
  probe.nextMessage = probe.nextMessage - dt
  if probe.nextMessage <= 0 then
    probe.nextMessage = 5
    sys:ShowMessage(string.format("Control probe: %d s - throttle, brake, steer both ways, handbrake", math.ceil(probe.left)))
  end
  if probe.left <= 0 then finishControlProbe(sys) end
end

local function startControlTest()
  local sys = scriptSettings()
  if not sys or not controlsAvailable() then return end
  local vehicle = mountedVehicle()
  if not vehicle then
    sys:ShowMessage("Control test: get in a car first")
    return
  end
  local fields = Game.BCAI_ControlFields()
  if not fields:find("throttle car") then
    sys:ShowMessage("Control test: run the control probe first")
    return
  end
  sys:StartInputWatch()
  state.controlProbe = nil
  state.controlTest = { delay = 3.0, phase = 0, results = {} }
  sys:ShowMessage("Control test in 3 s: close the overlay, hands off, clear road ahead")
  log("control test started, fields: " .. fields)
end

local function endControlTest(sys, why)
  local test = state.controlTest
  state.controlTest = nil
  sys:StopInputWatch()
  local vehicle = mountedVehicle()
  if vehicle then pcall(function() Game.BCAI_SetControls(vehicle, Vector4.new(0, 0, 0, 0), 0) end) end
  local status = tostring(Game.BCAI_ControlStatus())
  local lines = { "Control test " .. why .. ":" }
  for _, r in ipairs(test.results) do table.insert(lines, "  " .. r) end
  table.insert(lines, "  plugin: " .. status)
  state.controlTestReport = lines
  for _, line in ipairs(lines) do log(line) end
  sys:ShowMessage("Control test " .. why .. ": " .. (test.results[1] or "no phases ran"):sub(1, 70))
end

-- Every frame while testing: hold each phase's controls and measure the car.
local function controlTestTick(dt)
  local test = state.controlTest
  local sys = scriptSettings()
  if not sys then return end
  local vehicle = mountedVehicle()
  if not vehicle then return endControlTest(sys, "stopped (left the car)") end
  if state.overlayOpen then return end
  if test.delay > 0 then
    test.delay = test.delay - dt
    return
  end
  local input = sys:InputWatchValues()
  if math.abs(input.x) > 0.3 or math.abs(input.y) > 0.3 or math.abs(input.z) > 0.3 then
    return endControlTest(sys, "stopped (player input)")
  end
  local speed = math.abs(vehicle:GetCurrentSpeed()) * 3.6
  local heading = headingDeg(vehicle)
  if test.phase == 0 or test.left <= 0 then
    if test.phase > 0 then
      local turn = (heading - test.startHeading + 540) % 360 - 180
      table.insert(test.results, string.format("%-10s speed %5.1f -> %5.1f km/h, turned %+5.1f deg (%s)",
        CONTROL_TEST[test.phase].name, test.startSpeed, speed, turn, turn > 0 and "left" or "right"))
    end
    test.phase = test.phase + 1
    if test.phase > #CONTROL_TEST then return endControlTest(sys, "done") end
    test.left = CONTROL_TEST[test.phase].seconds
    test.startSpeed = speed
    test.startHeading = heading
    sys:ShowMessage("Control test: " .. CONTROL_TEST[test.phase].name)
  end
  local c = CONTROL_TEST[test.phase].controls
  Game.BCAI_SetControls(vehicle, Vector4.new(c[1], c[2], c[3], c[4]), 0.5)
  test.left = test.left - dt
end

local function applyAll()
  saveSettings()
  pushToScripts()
  Profiles.ApplyTraffic(settings)
  Profiles.ReapplyClones(settings)
  syncVehicle(false)
  local sys = scriptSettings()
  if sys then sys:RestartAutodrive() end
  state.dirty = false
end

-- Autodrive holds ~24 m/s whatever the command or vision params say, so look
-- for the setting that holds it: log every likely flat that actually exists.
local function probeAutodriveSettings()
  local groups = { "Vehicle.autodrive_settings", "Vehicle.autodrive", "Vehicle.autodriveSettings",
                   "AutoDrive", "Autodrive", "Vehicle.auto_drive_settings" }
  local fields = { "healthDeactivationThreshold", "maxSpeed", "speed", "targetSpeed", "defaultSpeed",
                   "cruiseSpeed", "topSpeed", "minSpeed", "maxSpeedFactor", "speedFactor", "speedLimit",
                   "speedMultiplier", "maxSpeedMultiplier", "autodriveSpeed", "autodriveMaxSpeed",
                   "maxAutodriveSpeed", "highwaySpeed", "maxSpeedHighway", "maxSpeedCity",
                   "citySpeed", "speedInTraffic", "trafficSpeed", "desiredSpeed", "maxVelocity",
                   "velocity", "drivingParams", "drivingParameters", "useSpeedLimit",
                   "speedLimitMultiplier", "clearTrafficOnPath", "maxSpeedKmh", "maxSpeedMps" }
  local found = 0
  for _, g in ipairs(groups) do
    for _, f in ipairs(fields) do
      local v = TweakDB:GetFlat(g .. "." .. f)
      if v ~= nil then
        found = found + 1
        log("probe: " .. g .. "." .. f .. " = " .. tostring(v))
      end
    end
  end
  log("probe: " .. found .. " autodrive flats found")
end

registerForEvent("onInit", function()
  -- Fires when the game enters or leaves its menus (map, inventory, pause...).
  Observe("RadialWheelController", "OnIsInMenuChanged", function(_, isInMenu)
    state.inMenu = isInMenu
  end)
  probeAutodriveSettings()
  loadSettings()
  loadTroubleSpots()
  Profiles.ApplyTraffic(settings)
  state.ready = true
  log("loaded, style " .. Overlay.STYLE_NAMES[settings.ownStyle + 1] .. ", game autodrive aggressive=" .. tostring(settings.aggressive))
end)

local function setOverlay(on)
  state.debugDraw = on
  state.debugError = nil
end

local function cycleOverlayDetail()
  settings.overlayDetail = ((settings.overlayDetail or 1) + 1) % 3
  saveSettings()
end

-- Autodrive+ and its styles.
registerHotkey("BCAI_ToggleAutodrivePlus", "Autodrive+: start / stop", function()
  if state.picker then return closePicker() end
  if state.ownDriving then toggleOwnDriver() else requestOwnDriver() end
end)
registerHotkey("BCAI_NextStyle", "Autodrive+: next driving style", function()
  setStyle((settings.ownStyle + 1) % 3)
  local sys = scriptSettings()
  if sys and not state.ownDriving then sys:ShowMessage("Autodrive+ style: " .. Overlay.STYLE_NAMES[settings.ownStyle + 1]) end
end)
registerHotkey("BCAI_StyleCity", "Autodrive+: city style", function() setStyle(0) end)
registerHotkey("BCAI_StyleAggressive", "Autodrive+: aggressive style", function() setStyle(1) end)
registerHotkey("BCAI_StyleMadman", "Autodrive+: madman style", function() setStyle(2) end)
-- The overlay.
registerHotkey("BCAI_ToggleDebugDraw", "Overlay: show / hide", function() setOverlay(not state.debugDraw) end)
registerHotkey("BCAI_OverlayDetail", "Overlay: compact / normal / everything", cycleOverlayDetail)
registerHotkey("BCAI_ToggleLaneMap", "Overlay: lane map on / off (native plugin)", function()
  state.drawLanes = not state.drawLanes
  -- The lane map is drawn by the overlay, so switch that on with it.
  if state.drawLanes then setOverlay(true) end
end)
registerHotkey("BCAI_SaveRecording", "Save drive recording (last 60 s)", function()
  saveRecording("saved by hand")
end)
-- The older drivers.
registerHotkey("BCAI_ToggleAIDrive", "Older: AI drive on / off", toggleAIDrive)
registerHotkey("BCAI_ToggleAggressive", "Older: the game's autodrive normal / aggressive", toggleAggressive)
-- Research.
registerHotkey("BCAI_ScanPath", "Research: scan for the AI's planned path", function()
  nativeScanForPath()
end)
registerHotkey("BCAI_DumpRoute", "Research: dump the AI's route here", function()
  local vehicle = mountedVehicle()
  local player = Game.GetPlayer()
  local where = vehicle and vehicle:GetWorldPosition() or (player and player:GetWorldPosition())
  local ok, result = pcall(function() return Game.BCAI_DumpRoute(autodriveSystem(), where) end)
  log("route dump: " .. tostring(result))
  local sys = scriptSettings()
  if sys then sys:ShowMessage("Route: " .. tostring(result):sub(1, 80)) end
end)
registerHotkey("BCAI_FindControls", "Research: find the car's controls (drive 25 s)", function()
  startControlProbe()
end)
registerHotkey("BCAI_TestControls", "Research: test the car's controls (hands off)", function()
  startControlTest()
end)

-- The overlay's measurements: 30 times a second while it shows (10 for the
-- recorder alone), with Autodrive+'s plan, the road's edges, what its sensor
-- sees and the lane map.
local function overlayTick(dt)
  local interval = state.debugDraw and (1 / 30) or 0.1
  state.debugTimer = state.debugTimer + dt
  if state.debugTimer < interval then return end
  state.debugTimer = 0
  local sys = scriptSettings()
  if not sys then return end
  local vehicle = mountedVehicle()
  sys:SetDebugProjection(state.debugFovHorizontal, state.debugManualProjection)
  sys:SetDebugLayers(settings.overlayTrail, settings.overlayRays, settings.overlayRoute, settings.overlaySensor,
    settings.overlayPredict)
  sys:DebugTick(vehicle and Profiles.LiveLookahead(vehicle, state.aiDriveVehicle ~= nil) or 0)
  if state.debugDraw and state.ownDriving then
    local ok, points = pcall(function() return Game.BCAI_DriverAhead(150.0, 2.0) end)
    pcall(function() sys:SetRouteAhead(ok and points or {}, stylesAvailable()) end)
    local okRoom, room = pcall(function() return Game.BCAI_DriverRoom(80.0, 4.0) end)
    local okSeen, seen = pcall(function() return Game.BCAI_DriverSeen() end)
    pcall(function() sys:SetOwnDriverDebug(okRoom and room or {}, okSeen and seen or {}) end)
    pcall(function() sys:SetBlockLabel(Overlay.BlockLabel(state.hud)) end)
  elseif state.debugDraw and state.nativeRoute and vehicle then
    pcall(function() sys:SetOwnDriverDebug({}, {}) end)
    pcall(function() sys:SetBlockLabel("") end)
    local ok, points = pcall(function()
      return Game.BCAI_RouteAhead(autodriveSystem(), vehicle:GetWorldPosition(), 150.0)
    end)
    pcall(function() sys:SetRouteAhead(ok and points or {}, false) end)
  end
  -- The lane map around the car, refreshed twice a second.
  state.laneDrawTime = (state.laneDrawTime or 0) + interval
  if state.debugDraw and state.drawLanes and state.nativeLanes and state.laneDrawTime >= 0.5 then
    state.laneDrawTime = 0
    local player = Game.GetPlayer()
    local where = vehicle and vehicle:GetWorldPosition() or (player and player:GetWorldPosition())
    local ok, lanes = pcall(function()
      return Game.BCAI_LanesNear(autodriveSystem(), where, 70.0, state.drawPavements)
    end)
    pcall(function() sys:SetDebugLanes(ok and lanes or {}) end)
    state.lanesShown = true
  elseif state.lanesShown and not (state.debugDraw and state.drawLanes) then
    pcall(function() sys:SetDebugLanes({}) end)
    state.lanesShown = false
  end
  if settings.recorder then
    state.recorderTime = (state.recorderTime or 0) + interval
    if state.recorderTime >= 0.1 then
      state.recorderTime = 0
      sys:RecorderTick()
      local reason = sys:RecorderTakePending()
      if reason and reason ~= "" then saveRecording(reason) end
    end
  end
end

registerForEvent("onUpdate", function(dt)
  if not state.ready then return end
  if state.ownDriving then
    ownDriverTick(dt)
  elseif state.picker then
    pickerTick(dt)
  elseif settings.ownTakeOverAutodrive then
    -- Starting autodrive asked for Autodrive+ (the scripts held native autodrive back).
    local sys = scriptSettings()
    if sys and sys:TakeAutodrivePlusRequest() and os.clock() >= (state.requestQuietUntil or 0) then requestOwnDriver() end
  end
  if state.controlProbe then controlProbeTick(dt) end
  if state.controlTest then controlTestTick(dt) end
  if state.scanFollowUp then
    state.scanFollowUp = state.scanFollowUp - dt
    if state.scanFollowUp <= 0 then
      state.scanFollowUp = nil
      saveRecording("path scan follow-up")
    end
  end
  -- The autodrive HUD counters are rewritten every frame so they don't flicker.
  if state.aiDriveVehicle then
    local sys = scriptSettings()
    if sys then sys:RefreshAutodriveHud() end
  end
  -- AI drive's approach speed needs updating more often than once a second.
  state.aiDriveTimer = state.aiDriveTimer + dt
  if state.aiDriveTimer >= 0.25 then
    state.aiDriveTimer = 0
    local sys = scriptSettings()
    if sys and state.aiDriveVehicle then
      local vehicle = mountedVehicle()
      local ok, plan = false, nil
      if settings.aiRoutePlanner and state.nativeRoute and vehicle then
        ok, plan = pcall(function()
          return Game.BCAI_RoutePlan(autodriveSystem(), vehicle:GetWorldPosition(), settings.aiRouteGrip * 9.81, 4.0)
        end)
      end
      if ok and plan then
        sys:SetRouteCap(plan.x, plan.y, plan.z)
      else
        sys:SetRouteCap(-1.0, 0.0, 0.0)
      end
      sys:UpdateCommandDrive()
    elseif sys then
      -- Take over when the player starts autodrive. Only once per activation,
      -- so a failed start doesn't retry every tick.
      local active = sys:ShouldTakeOverAutodrive()
      if active and not state.autodriveSeen and not state.ownDriving and not state.picker and settings.aiDriveDefault then
        toggleAIDrive()
      end
      state.autodriveSeen = active
    end
  end
  if state.debugDraw or settings.recorder then overlayTick(dt) end
  Nav.Tick(dt)
  DriveTest.Tick(dt)
  if state.trace then
    state.traceTimer = state.traceTimer + dt
    if state.traceTimer >= 1.0 then
      state.traceTimer = 0
      local sys = scriptSettings()
      if sys and (sys:IsAutodriveActive() or sys:IsCommandDriving()) then
        log("trace: " .. sys:DescribeAutodrive())
      end
    end
  end
  -- Saved style settings, a moment after the last slider move.
  if state.liveDirty then
    state.liveSaveTimer = (state.liveSaveTimer or 0) + dt
    if state.liveSaveTimer >= 1.0 then
      state.liveDirty = false
      state.liveSaveTimer = 0
      saveSettings()
    end
  end
  state.pollTimer = state.pollTimer + dt
  if state.pollTimer < 1.0 then return end
  state.pollTimer = 0
  -- Scriptable systems are recreated on every save load, so keep them in sync.
  pushToScripts()
  -- Plugin 0.10+ reads the AI's route (overlay, speed planner).
  local version = nativeVersion() or 0
  state.nativeRoute = version >= 1000
  state.nativeLanes = version >= 1200
  -- Plugin 0.11+ repairs broken junctions, once per loaded lane map (the
  -- plugin notices a new map after a save load; repeat calls are cheap).
  if version >= 1100 then
    local apply = settings.aiRepairLanes
    if apply or state.laneRepairApplied then
      local ok, result = pcall(function() return Game.BCAI_RepairLanes(autodriveSystem(), apply) end)
      if ok and result ~= state.laneRepair then
        state.laneRepair = result
        log("lane repair: " .. tostring(result))
      end
      state.laneRepairApplied = apply and ok
    end
  end
  local sys = scriptSettings()
  -- What NPC drivers did about being bumped.
  if sys then
    for _, line in ipairs(sys:TakeTrafficNotes() or {}) do log("Traffic | " .. line) end
  end
  -- Ended by player input, arrival or leaving the car: restore the params.
  if state.aiDriveVehicle and not (sys and sys:IsCommandDriving()) then
    Profiles.SetAIDrive(state.aiDriveVehicle, settings, false)
    state.aiDriveVehicle = nil
    state.aiDriveBike = false
    log("AI drive ended: " .. (sys and sys:GetLastStop() or "?"))
  end
  -- Covers getting into a different car before autodrive is started.
  syncVehicle(false)
end)

registerForEvent("onOverlayOpen", function() state.overlayOpen = true end)
registerForEvent("onOverlayClose", function()
  state.overlayOpen = false
  if state.dirty then applyAll() end
  if state.liveDirty then
    state.liveDirty = false
    saveSettings()
  end
end)

-- Runs a plugin call, logs the result and shows its start on screen.
local function nativeAction(label, call)
  local ok, result = pcall(call)
  log(label .. ": " .. tostring(result))
  local sys = scriptSettings()
  if sys then sys:ShowMessage(label .. ": " .. tostring(result):sub(1, 80)) end
end

local function carOrPlayerPosition()
  local vehicle = mountedVehicle()
  local player = Game.GetPlayer()
  return vehicle and vehicle:GetWorldPosition() or (player and player:GetWorldPosition())
end

-- What the windows (ui.lua) work with.
local app = {
  settings = settings,
  state = state,
  defaults = defaults,
  STYLE_FIELDS = STYLE_FIELDS,
  stylePrefix = stylePrefix,
  saveSettings = saveSettings,
  applyAll = applyAll,
  scriptSettings = scriptSettings,
  mountedVehicle = mountedVehicle,
  autodriveSystem = autodriveSystem,
  carOrPlayerPosition = carOrPlayerPosition,
  nativeVersion = nativeVersion,
  ownDriverAvailable = ownDriverAvailable,
  controlsAvailable = controlsAvailable,
  toggleOwnDriver = toggleOwnDriver,
  requestOwnDriver = requestOwnDriver,
  confirmPicker = confirmPicker,
  setStyle = setStyle,
  setOverlay = setOverlay,
  toggleAIDrive = toggleAIDrive,
  toggleAggressive = toggleAggressive,
  saveRecording = saveRecording,
  nativeAction = nativeAction,
  nativeScanForPath = nativeScanForPath,
  nativeDumpObjects = nativeDumpObjects,
  startControlProbe = startControlProbe,
  startControlTest = startControlTest,
  nav = Nav,
  driveTest = DriveTest,
}

function app.resetAll()
  -- (Which windows are open isn't a driving setting.)
  local advanced, debug = settings.uiAdvancedWindow, settings.uiDebugWindow
  for k, v in pairs(defaults) do settings[k] = v end
  settings.uiAdvancedWindow, settings.uiDebugWindow = advanced, debug
  applyAll()
  sendStyle()
end

function app.forgetTroubleSpots()
  state.troubleSpots = {}
  saveTroubleSpots()
  local sys = scriptSettings()
  if sys then sys:ClearTroubleSpots() end
end

function app.describeVehicle(toLog)
  local vehicle = mountedVehicle()
  state.debugLines = vehicle and Profiles.Describe(vehicle) or { "Not in a vehicle" }
  if toLog then
    for _, line in ipairs(state.debugLines) do log(line) end
  end
end

function app.sensorTest()
  local sys = scriptSettings()
  if sys then
    state.sensorTest = tostring(sys:OwnDriverSensorTest())
    log(state.sensorTest)
  end
end

-- Only draw the overlay while actually driving: not in menus, while paused or on foot.
local function overlayVisible()
  if state.inMenu or not mountedVehicle() then return false end
  local ok, paused = pcall(function() return Game.GetSystemRequestsHandler():IsGamePaused() end)
  return not (ok and paused)
end

-- The route to the map destination (nav.lua): drawn while driving, like the overlay.
Nav.Init({
  settings = settings,
  log = log,
  scriptSettings = scriptSettings,
  autodriveSystem = autodriveSystem,
  mountedVehicle = mountedVehicle,
  nativeVersion = nativeVersion,
  visible = overlayVisible,
  overlayDrawsPath = function() return state.debugDraw and state.ownDriving and settings.overlayRoute end,
  ownDriving = function() return state.ownDriving end,
})

-- The driving test (drivetest.lua): a random route, driven by the player and
-- then by Autodrive+, both recorded.
DriveTest.Init({
  settings = settings,
  state = state,
  log = log,
  scriptSettings = scriptSettings,
  mountedVehicle = mountedVehicle,
  nativeVersion = nativeVersion,
  autodriveSystem = autodriveSystem,
  nav = Nav,
  startOwnDriver = startOwnDriver,
  stopOwnDriver = stopOwnDriver,
  carData = carData,
  styleName = function() return Overlay.STYLE_NAMES[settings.ownStyle + 1] end,
})

local function drawOverlay(sys)
  local stats = Overlay.DrawWorld(sys)
  local mode = "manual driving"
  if state.ownDriving then
    mode = "Autodrive+"
  elseif state.aiDriveVehicle then
    mode = "AI drive"
  elseif sys:IsAutodriveActive() then
    mode = "the game's autodrive"
  end
  Overlay.DrawPanel({
    settings = settings,
    hud = state.ownDriving and state.hud or {},
    events = state.ownDriving and state.events or {},
    debug = sys:DebugState(),
    lines = sys:DebugLines() or {},
    mode = mode,
    stats = stats,
    overlayOpen = state.overlayOpen,
    car = carData(),
  })
end

registerForEvent("onDraw", function()
  local okNav, errNav = pcall(Nav.Draw)
  if not okNav and not state.navDrawError then
    state.navDrawError = tostring(errNav)
    log("route line failed: " .. state.navDrawError)
  end
  if state.debugDraw and not state.debugError and overlayVisible() then
    local sys = scriptSettings()
    if sys then
      local ok, err = pcall(drawOverlay, sys)
      if not ok then
        -- Don't spam the log every frame; show the error in the settings window.
        state.debugError = tostring(err)
        log("debug overlay failed: " .. state.debugError)
      end
    end
  end
  if state.picker then
    local ok, err = pcall(UI.DrawPicker, app)
    if not ok then
      log("style picker failed: " .. tostring(err))
      closePicker()
      startOwnDriver()
    end
  end
  if not state.overlayOpen then return end
  UI.DrawMain(app)
  if settings.uiAdvancedWindow then UI.DrawAdvanced(app) end
  if settings.uiDebugWindow then UI.DrawTools(app) end
end)
