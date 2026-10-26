-- Better Car AI - runtime TweakDB driving profiles.
--
-- Every Vehicle_Record points at BaseDrivingParameters records
-- (drivingParamsGeneric / drivingParamsAutodrive / drivingParamsRace). Those
-- point at sub-records the native driver reads: vision (look-ahead, speed
-- limit, lateral avoidance), traction (how much grip the AI assumes) and
-- boostStats (torque/grip assist). We clone and edit these through CET's
-- TweakDB API at runtime (written before TweakXL supported 2.5; it works
-- without TweakXL either way).

local Profiles = {}

local log = function(msg) spdlog.info("[BetterCarAI] " .. msg) end
Profiles.log = log

local originals = {}   -- flat path -> vanilla value (snapshot before we touch it)
local clones = {}      -- clone name -> { src = name, kind = "normal"|"aggressive" }
local trafficTargets = nil
local panicLogged = false
local missingFlats = {}

local SUB_FIELDS = { "vision", "traction", "boostStats", "steering", "stopping" }

local function idName(id)
  if id == nil then return nil end
  local s = TDBID.ToStringDEBUG(id)
  if s == nil or s == "" or s:sub(1, 1) == "<" then return nil end
  return s
end
Profiles.idName = idName

local function orig(path)
  local v = originals[path]
  if v == nil then
    v = TweakDB:GetFlat(path)
    if v == nil then
      if not missingFlats[path] then
        missingFlats[path] = true
        log("flat not found: " .. path)
      end
      return nil
    end
    originals[path] = v
  end
  return v
end

local function scale(src, dst, field, mult)
  local v = orig(src .. "." .. field)
  if type(v) == "number" then
    TweakDB:SetFlat(dst .. "." .. field, v * mult)
  end
end

local function addInt(src, dst, field, delta)
  local v = orig(src .. "." .. field)
  if type(v) == "number" then
    TweakDB:SetFlat(dst .. "." .. field, math.floor(v + delta))
  end
end

local function setBool(dst, field, value)
  TweakDB:SetFlat(dst .. "." .. field, value)
end

-- Sub-record names of a BaseDrivingParameters record, read from the vanilla snapshot.
local function subRecords(paramsName)
  local subs = {}
  for _, field in ipairs(SUB_FIELDS) do
    subs[field] = idName(orig(paramsName .. "." .. field))
  end
  return subs
end

-- Sub-record names a record points at right now (used for our own clones).
local function liveSubRecords(paramsName)
  local subs = {}
  for _, field in ipairs(SUB_FIELDS) do
    subs[field] = idName(TweakDB:GetFlat(paramsName .. "." .. field))
  end
  return subs
end

local function applyValues(cloneName, s)
  local info = clones[cloneName]
  local src = subRecords(info.src)
  local dst = liveSubRecords(cloneName)
  -- AI drive is aggressive driving too, but with its own cornering safety.
  -- Bikes get their own AI drive values (aiBike* instead of ai*).
  local aiDrive = info.kind == "aidrive" or info.kind == "aibike"
  local aggressive = info.kind == "aggressive" or aiDrive
  local ai = function(key)
    if info.kind == "aibike" then return s["aiBike" .. key] end
    return s["ai" .. key]
  end

  if src.vision and dst.vision then
    if aggressive then
      local obey = orig(src.vision .. ".useSpeedLimit")
      -- AI drive runs much faster, so it needs to see much further to swerve
      -- in time, and should drift back into lane slowly so it doesn't cut
      -- back into the car it just passed.
      local look = aiDrive and ai("Lookahead") or s.aggroLookahead
      local back = aiDrive and ai("ReturnRate") or s.aggroSwerve
      setBool(dst.vision, "useSpeedLimit", not s.aggroIgnoreSpeedLimit and obey ~= false)
      scale(src.vision, dst.vision, "maxDistance", look)
      scale(src.vision, dst.vision, "maxStraightPathExtension", look)
      scale(src.vision, dst.vision, "offsetSmoothingFastChangeSpeed", aiDrive and ai("Swerve") or s.aggroSwerve)
      scale(src.vision, dst.vision, "offsetSmoothingReturnToNeutralSpeed", back)
      addInt(src.vision, dst.vision, "numSweeps", s.aggroExtraSweeps and 2 or 0)
      -- The safety margin is subtracted from the road's limit, so a negative
      -- margin lets the car drive over it when speed limits are obeyed.
      local margin = orig(src.vision .. ".speedLimitSafetyMargin")
      if type(margin) == "number" then
        TweakDB:SetFlat(dst.vision .. ".speedLimitSafetyMargin", margin - s.aggroSpeedLimitBonusKmh / 3.6)
      end
    else
      local obey = orig(src.vision .. ".useSpeedLimit")
      setBool(dst.vision, "useSpeedLimit", s.normalObeySpeedLimit and obey ~= false)
      scale(src.vision, dst.vision, "maxDistance", s.normalLookahead)
      scale(src.vision, dst.vision, "maxStraightPathExtension", s.normalLookahead)
    end
  end

  if src.traction and dst.traction then
    local grip = aggressive and s.aggroCornerGrip or s.normalCornerGrip
    local brake = aggressive and s.aggroBrakeSafety or 1.0
    -- Under-estimating grip makes the AI take corners slower and brake
    -- earlier: this is what keeps it from spinning out at high speed.
    if aiDrive then grip, brake = ai("CornerGrip"), ai("BrakeSafety") end
    scale(src.traction, dst.traction, "turningAccelFactor", grip)
    scale(src.traction, dst.traction, "forwardAccelFactor", aggressive and s.aggroCornerGrip or grip)
    scale(src.traction, dst.traction, "brakingAccelFactor", brake)
  end

  -- Vanilla steering aims 15 m ahead from 30 m/s up, which gets twitchy at
  -- motorway speed; stretch it so the aim point keeps scaling with speed.
  if src.steering and dst.steering then
    local reach = aggressive and s.aggroSteerReach or 1.0
    if aiDrive then reach = ai("SteerReach") end
    scale(src.steering, dst.steering, "maxTargetDistance", reach)
    scale(src.steering, dst.steering, "speedForMaxDistance", reach)
    -- How fast the steering moves towards its target: lower = less snap
    -- (and less oversteer) when the lane jogs sideways at junctions.
    local rate = aiDrive and ai("SteerRate") or 1.0
    scale(src.steering, dst.steering, "mildSteeringSpeed", rate)
    scale(src.steering, dst.steering, "fullSteeringSpeed", rate)
    -- Path error needed before steering mildly / fully: higher ignores small
    -- deviations, which damps the side-to-side wiggle at speed.
    local damping = aiDrive and ai("SteerDamping") or 1.0
    scale(src.steering, dst.steering, "errorMagnitudeForMildSteering", damping)
    scale(src.steering, dst.steering, "errorMagnitudeForFullSteering", damping)
  end

  -- How fast brake pressure ramps up: lower = smoother, less abrupt braking.
  -- (The AI-drive braking safety already makes it start braking earlier.)
  if src.stopping and dst.stopping then
    local ramp = aiDrive and ai("BrakeSmooth") or 1.0
    scale(src.stopping, dst.stopping, "mildBrakingChangeSpeed", ramp)
    scale(src.stopping, dst.stopping, "fullBrakingChangeSpeed", ramp)
  end

  if src.boostStats and dst.boostStats then
    local power = aggressive and s.aggroPowerBoost or 1.0
    local grip = aggressive and s.aggroCornerGrip or 1.0
    if aiDrive then grip = ai("Stability") end
    scale(src.boostStats, dst.boostStats, "maxTorqueBoost", power)
    scale(src.boostStats, dst.boostStats, "maxLongTractionBoost", grip)
    scale(src.boostStats, dst.boostStats, "maxLatTractionBoost", grip)
  end

  for _, name in pairs(dst) do
    if name then TweakDB:Update(name) end
  end
  TweakDB:Update(cloneName)
end

-- Clone a params record (and its sub-records, so shared vanilla records stay untouched).
local function ensureClone(srcName, kind, s)
  local cloneName = "BCAI_" .. srcName:gsub("%.", "_") .. "_" .. kind
  if clones[cloneName] then return cloneName end

  if TweakDB:GetRecord(cloneName) == nil and not TweakDB:CloneRecord(cloneName, srcName) then
    log("failed to clone " .. srcName)
    return nil
  end
  for field, subName in pairs(subRecords(srcName)) do
    if subName then
      local subClone = cloneName .. "_" .. field
      if TweakDB:GetRecord(subClone) ~= nil or TweakDB:CloneRecord(subClone, subName) then
        TweakDB:SetFlat(cloneName .. "." .. field, TweakDBID.new(subClone))
      else
        log("failed to clone " .. subName)
      end
    end
  end

  clones[cloneName] = { src = srcName, kind = kind }
  applyValues(cloneName, s)
  log("created " .. cloneName .. " from " .. srcName)
  return cloneName
end

local function autodriveSource(vehName, kind, s)
  if kind == "aggressive" and s.aggroUseRaceParams then
    local race = idName(orig(vehName .. ".drivingParamsRace"))
    if race then return race end
  end
  return idName(orig(vehName .. ".drivingParamsAutodrive"))
      or idName(orig(vehName .. ".drivingParamsGeneric"))
end

-- Point a vehicle record's autodrive params at our normal/aggressive clone.
-- Returns true if the record changed (autodrive must restart to pick it up).
function Profiles.ApplyToVehicle(vehicle, s)
  local vehName = idName(vehicle:GetRecordID())
  if not vehName then return false end

  local kind = s.aggressive and "aggressive" or "normal"
  local src = autodriveSource(vehName, kind, s)
  if not src then return false end

  local cloneName = ensureClone(src, kind, s)
  if not cloneName then return false end

  local path = vehName .. ".drivingParamsAutodrive"
  if idName(TweakDB:GetFlat(path)) == cloneName then return false end
  TweakDB:SetFlat(path, TweakDBID.new(cloneName))
  TweakDB:Update(vehName)
  log(vehName .. " autodrive -> " .. cloneName)
  return true
end

-- AI drive (the police drive command) reads the vehicle's generic params, so
-- point both generic and autodrive at the AI-drive clone while it runs.
function Profiles.SetAIDrive(vehName, s, on, bike)
  if not vehName then return end
  local generic = idName(orig(vehName .. ".drivingParamsGeneric"))
  if not generic then return end
  if on then
    local cloneName = ensureClone(generic, bike and "aibike" or "aidrive", s)
    if not cloneName then return end
    TweakDB:SetFlat(vehName .. ".drivingParamsGeneric", TweakDBID.new(cloneName))
    TweakDB:SetFlat(vehName .. ".drivingParamsAutodrive", TweakDBID.new(cloneName))
    log(vehName .. " AI drive -> " .. cloneName)
  else
    TweakDB:SetFlat(vehName .. ".drivingParamsGeneric", TweakDBID.new(generic))
    log(vehName .. " AI drive params restored")
  end
  TweakDB:Update(vehName)
end

function Profiles.ReapplyClones(s)
  for cloneName in pairs(clones) do
    applyValues(cloneName, s)
  end
end

-- NPC traffic: tune the shared generic params in place (vanilla values are
-- snapshotted first, so sliders never compound).
function Profiles.ApplyTraffic(s)
  if trafficTargets == nil then
    trafficTargets = {}
    local seen = {}
    for _, rec in ipairs(TweakDB:GetRecords("gamedataVehicle_Record") or {}) do
      local generic = rec:DrivingParamsGeneric()
      local name = generic and idName(generic:GetID())
      if name and not seen[name] then
        seen[name] = true
        local subs = subRecords(name)
        subs.panic = idName(orig(name .. ".panic"))
        table.insert(trafficTargets, subs)
      end
    end
    log("traffic: found " .. #trafficTargets .. " generic driving param sets")
  end

  local grip = s.trafficTuning and s.trafficCornerGrip or 1.0
  local look = s.trafficTuning and s.trafficLookahead or 1.0
  local done = {}
  for _, subs in ipairs(trafficTargets) do
    if subs.traction and not done[subs.traction] then
      done[subs.traction] = true
      scale(subs.traction, subs.traction, "turningAccelFactor", grip)
      scale(subs.traction, subs.traction, "forwardAccelFactor", grip)
      TweakDB:Update(subs.traction)
    end
    if subs.vision and not done[subs.vision] then
      done[subs.vision] = true
      scale(subs.vision, subs.vision, "maxDistance", look)
      TweakDB:Update(subs.vision)
    end
    -- Panic driving (fleeing a crash or gunfire): not as fast, and looking
    -- further ahead for bends, so fleeing drivers crash less.
    if subs.panic and not done[subs.panic] then
      done[subs.panic] = true
      local speed = s.trafficCalmPanic and s.trafficPanicSpeed or 1.0
      local bends = s.trafficCalmPanic and s.trafficPanicLookahead or 1.0
      if not panicLogged then
        panicLogged = true
        log(string.format("traffic: panic driving %s: top speed %s, boost max speed %s, bend look-up %s",
          subs.panic, tostring(orig(subs.panic .. ".topSpeed")), tostring(orig(subs.panic .. ".boostMaxSpeed")),
          tostring(orig(subs.panic .. ".curvatureLookupRange"))))
      end
      scale(subs.panic, subs.panic, "topSpeed", speed)
      scale(subs.panic, subs.panic, "boostMaxSpeed", speed)
      scale(subs.panic, subs.panic, "curvatureLookupRange", bends)
      TweakDB:Update(subs.panic)
    end
  end
end

-- Obstacle vision range (m) of the params the vehicle uses right now: generic
-- while AI drive runs, autodrive otherwise.
function Profiles.LiveLookahead(vehicle, aiDrive)
  local vehName = idName(vehicle:GetRecordID())
  if not vehName then return 0 end
  local params = not aiDrive and idName(TweakDB:GetFlat(vehName .. ".drivingParamsAutodrive"))
      or idName(TweakDB:GetFlat(vehName .. ".drivingParamsGeneric"))
  local vision = params and idName(TweakDB:GetFlat(params .. ".vision"))
  local dist = vision and TweakDB:GetFlat(vision .. ".maxDistance")
  return type(dist) == "number" and dist or 0
end

-- Every flat of a driving params record, per sub-record (from the 2.5 record classes).
local SUB_FLATS = {
  vision = { "useSpeedLimit", "speedLimitSafetyMargin", "speedLimitDeceleration", "minDistance",
             "maxDistance", "maxStraightPathExtension", "numSweeps", "offsetSmoothingFastChangeSpeed",
             "offsetSmoothingReturnToNeutralSpeed", "sideOverlapLateralScale",
             "sideOverlapLongitudinalScale", "sideOverlapForwardOffset" },
  traction = { "turningAccelFactor", "forwardAccelFactor", "brakingAccelFactor" },
  boostStats = { "maxTorqueBoost", "maxLongTractionBoost", "maxLatTractionBoost" },
  accel = { "P", "I", "D", "outputSaturationLimit", "integratorClampingLimit" },
  brake = { "P", "I", "D", "outputSaturationLimit", "integratorClampingLimit" },
  stopping = { "mildBrakingChangeSpeed", "errorMagnitudeForMildBrakingChange",
               "fullBrakingChangeSpeed", "errorMagnitudeForFullBrakingChange", "decreaseMul" },
  steering = { "vehicleMinTurnSpeed", "vehicleMaxTurnSpeed", "minTargetDistance",
               "maxTargetDistance", "speedForMinDistance", "speedForMaxDistance",
               "mildSteeringSpeed", "fullSteeringSpeed", "errorMagnitudeForMildSteering",
               "errorMagnitudeForFullSteering", "increaseSteeringDistForAvoidance" },
}
local SUB_ORDER = { "vision", "traction", "boostStats", "accel", "brake", "stopping", "steering" }

local function describeParams(out, title, params)
  table.insert(out, title .. ": " .. tostring(params))
  if not params then return end
  for _, field in ipairs(SUB_ORDER) do
    local sub = idName(TweakDB:GetFlat(params .. "." .. field))
    if sub then
      local parts = {}
      for _, f in ipairs(SUB_FLATS[field]) do
        local v = TweakDB:GetFlat(sub .. "." .. f)
        if v ~= nil then
          if type(v) == "number" then v = string.format("%.3g", v) end
          table.insert(parts, f .. "=" .. tostring(v))
        end
      end
      table.insert(out, "  " .. field .. " (" .. sub .. "): " .. table.concat(parts, " "))
    end
  end
end

-- Values for the debug panel: the mounted vehicle's live autodrive params, plus
-- the vanilla generic/autodrive/race params to compare against.
function Profiles.Describe(vehicle)
  local out = {}
  local vehName = idName(vehicle:GetRecordID())
  if not vehName then return out end
  table.insert(out, "Vehicle: " .. vehName)
  describeParams(out, "Live autodrive", idName(TweakDB:GetFlat(vehName .. ".drivingParamsAutodrive")))
  for _, field in ipairs({ "drivingParamsAutodrive", "drivingParamsGeneric", "drivingParamsRace" }) do
    describeParams(out, "Vanilla " .. field, idName(orig(vehName .. "." .. field)))
  end
  return out
end

return Profiles
