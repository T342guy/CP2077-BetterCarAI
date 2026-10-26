-- Better Car AI - the CET windows: the main window (Autodrive+, its driving
-- style, NPC traffic, the overlay), the style picker shown when Autodrive+
-- starts, and two more windows opened from the main one: the older drivers'
-- tuning (the game's autodrive, AI drive, motorcycles) and the debug tools.
-- `app` is the shared state and actions from init.lua.

local Overlay = require("overlay")

local UI = {}

local GREY = { 0.65, 0.65, 0.65 }

local function grey(text) ImGui.TextColored(GREY[1], GREY[2], GREY[3], 1, text) end

local function wrappedGrey(text)
  ImGui.PushStyleColor(ImGuiCol.Text, GREY[1], GREY[2], GREY[3], 1)
  ImGui.TextWrapped(text)
  ImGui.PopStyleColor(1)
end

local function heading(text)
  ImGui.Spacing()
  ImGui.TextColored(0.55, 0.8, 1.0, 1, text)
end

-- Controls bound to app.settings[key]; changes are saved when the overlay
-- closes (or at once with live = true: read every frame while driving).
local function checkbox(app, label, key, live)
  local value, pressed = ImGui.Checkbox(label, app.settings[key])
  if pressed then
    app.settings[key] = value
    if live then app.saveSettings() else app.state.dirty = true end
  end
  return pressed
end

local function slider(app, label, key, min, max, fmt, live)
  local value, used = ImGui.SliderFloat(label, app.settings[key], min, max, fmt or "%.2f")
  if used then
    app.settings[key] = value
    if live then app.state.liveDirty = true else app.state.dirty = true end
  end
end

local function hint(text)
  if ImGui.IsItemHovered() then ImGui.SetTooltip(text) end
end

-- What each style does, in a sentence or two.
UI.STYLE_TEXT = {
  "Drives by the rules: about the speed limit, keeps right, overtakes only what stands still, gives "
    .. "junctions room, slows for crosswalks with someone by them and leaves a 2 s gap. It still doesn't see "
    .. "traffic lights.",
  "Fast and hard: well over the limit, brakes late, keeps right except to overtake, and overtakes slow "
    .. "cars on the other side of the road when it can see nothing is coming.",
  "Gets there as fast as it can without crashing: any lane, any gap, overtakes whenever nothing is coming, "
    .. "short following distance, and only slows at junctions for cars it sees crossing.",
}

-- Three buttons, the chosen one lit in its colour.
local function styleButtons(app, width)
  for i = 1, 3 do
    if i > 1 then ImGui.SameLine() end
    local c = Overlay.STYLE_COLOURS[i]
    local chosen = app.settings.ownStyle == i - 1
    local pushed = 0
    if chosen then
      ImGui.PushStyleColor(ImGuiCol.Button, c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, 1)
      ImGui.PushStyleColor(ImGuiCol.ButtonHovered, c[1] * 0.7, c[2] * 0.7, c[3] * 0.7, 1)
      pushed = 2
    end
    if ImGui.Button(Overlay.STYLE_NAMES[i], width or 110, 0) then app.setStyle(i - 1) end
    if pushed > 0 then ImGui.PopStyleColor(pushed) end
  end
end

-- The chosen style's settings (each style keeps its own).
local function styleSettings(app)
  local p = app.stylePrefix(app.settings.ownStyle)
  slider(app, "Top speed (km/h)", p .. "MaxSpeedKmh", 40, 300, "%.0f", true)
  hint("Never faster than this")
  slider(app, "Speed vs speed limit (x)", p .. "LaneSpeedFactor", 0.8, 4.0, "%.2f", true)
  hint("Each road's traffic speed times this (city roads are about 36 km/h, highways 61)")
  slider(app, "Cornering grip (g)", p .. "CornerGrip", 0.3, 2.2, "%.2f", true)
  hint("How hard it takes bends (the game's sports cars hold about 2 g); it also learns the car's grip and never asks for more")
  slider(app, "Braking for bends (m/s2)", p .. "BrakeDecel", 2.0, 8.0, "%.1f", true)
  slider(app, "Following distance (s)", p .. "FollowTime", 0.5, 3.0, "%.1f", true)
  if ImGui.Button("Reset " .. Overlay.STYLE_NAMES[app.settings.ownStyle + 1] .. " to its defaults") then
    for _, f in ipairs(app.STYLE_FIELDS) do app.settings[p .. f] = app.defaults[p .. f] end
    app.saveSettings()
  end
  grey("These apply at once, also while it drives")
end

local function statusLine(app)
  local hud = app.state.hud or {}
  if app.state.ownDriving then
    local c = Overlay.STYLE_COLOURS[(hud.style or app.settings.ownStyle) + 1]
    ImGui.TextColored(0.4, 0.9, 0.5, 1, "Driving")
    ImGui.SameLine()
    ImGui.Text(string.format("%.0f km/h, %s", hud.v or 0, hud.doing or ""))
    ImGui.SameLine()
    ImGui.TextColored(c[1], c[2], c[3], 1, "(" .. Overlay.STYLE_NAMES[(hud.style or app.settings.ownStyle) + 1] .. ")")
  elseif app.state.picker then
    ImGui.TextColored(1, 0.85, 0.4, 1, "Choosing a style...")
  else
    ImGui.TextColored(0.7, 0.7, 0.7, 1, "Off")
    local sys = app.scriptSettings()
    local last = sys and sys:OwnDriverStop() or ""
    if last ~= "" then
      ImGui.SameLine()
      grey("(last stop: " .. last:sub(1, 70) .. ")")
    end
  end
end

-- The main window.
function UI.DrawMain(app)
  local settings = app.settings
  ImGui.SetNextWindowSize(470, 0, ImGuiCond.FirstUseEver)
  if not ImGui.Begin("Better Car AI") then
    ImGui.End()
    return
  end
  local available = app.ownDriverAvailable()
  ImGui.TextColored(0.55, 0.8, 1.0, 1, "AUTODRIVE+")
  ImGui.SameLine()
  if available then
    statusLine(app)
  else
    ImGui.TextColored(1, 0.45, 0.3, 1, "needs the native plugin 0.19 or newer")
  end
  if available then
    local label = app.state.ownDriving and "Stop Autodrive+" or "Start Autodrive+"
    if ImGui.Button(label, 150, 28) then
      if app.state.ownDriving then app.toggleOwnDriver() else app.requestOwnDriver() end
    end
    ImGui.SameLine()
    wrappedGrey("Drives to the map destination, or along the road without one. Steering, throttle or brake take over.")

    heading("Driving style")
    styleButtons(app)
    wrappedGrey(UI.STYLE_TEXT[settings.ownStyle + 1])
    checkbox(app, "Ask which style each time it starts", "ownAskStyle", true)
    hint("A picker appears for a few seconds: steer left/right to choose, press the autodrive button again (or accelerate) to go, brake to cancel")
    checkbox(app, "The game's autodrive button starts Autodrive+", "ownTakeOverAutodrive")
    checkbox(app, "Show the game's autodrive HUD while it drives", "ownGameHud")
    hint("The wheel icon on the minimap and the side panel with distance and time left")
    if (app.nativeVersion() or 0) >= 3100 then
      checkbox(app, "Watch for people on foot", "ownPeople")
      hint("Stops for people crossing or stepping into the road, goes round someone standing in it, and in City "
        .. "slows for crosswalks with someone by them (all styles; people walking along the pavement don't count)")
    end
    if ImGui.CollapsingHeader(Overlay.STYLE_NAMES[settings.ownStyle + 1] .. " settings") then
      styleSettings(app)
    end
  end

  heading("Navigation")
  if app.nav.Available() then
    checkbox(app, "Plan my own route to the map destination", "navGuide", true)
    hint("The quickest way by time (junctions and turns across traffic count), ending on the pin's level, not on a "
      .. "highway or bridge above or below it. Autodrive+ always drives this planner's route.")
    if settings.navGuide then
      checkbox(app, "Draw it on the road ahead", "navLine", true)
      ImGui.SameLine()
      checkbox(app, "Show the next turn", "navHints", true)
      local summary = app.nav.Summary()
      wrappedGrey(summary and ("Now: " .. summary) or "No destination: set a pin on the map, or track a quest.")
    end
  else
    grey("Needs the native plugin 0.28 or newer")
  end

  heading("NPC traffic")
  checkbox(app, "Don't panic at light taps", "calmTraffic")
  hint("NPC drivers keep driving after a bump; they still flee from real crashes, shots and drawn weapons")
  if settings.calmTraffic then
    slider(app, "Panic above (km/h)", "panicImpactKmh", 5.0, 72.0, "%.0f")
    hint("A knock that changes a car's speed by more than this is a crash: the game's own reactions")
    checkbox(app, "Honk when tapped (and flash the lights at a second tap)", "honkOnTap")
    checkbox(app, "People nearby don't flee from a fender-bender", "calmPedestrians")
    local patience, used = ImGui.SliderInt("Taps before a driver loses patience", math.floor(settings.tapPatience + 0.5), 0, 10)
    if used then
      settings.tapPatience = patience
      app.state.dirty = true
    end
    hint("Within 30 s of each other. Then the driver reacts the game's way: drives off in a panic or gets out. 0: never")
  end
  checkbox(app, "Faster, smoother traffic", "trafficTuning")
  checkbox(app, "Calmer panic driving", "trafficCalmPanic")
  hint("Drivers fleeing a crash or gunfire go a little slower and look further ahead for bends, so they crash less")

  heading("Overlay")
  local draw, drawPressed = ImGui.Checkbox("Show the debug overlay", app.state.debugDraw)
  if drawPressed then app.setOverlay(draw) end
  if app.state.debugError then
    ImGui.TextColored(1, 0.4, 0.3, 1, "Overlay error: " .. app.state.debugError)
  end
  if app.state.debugDraw then
    ImGui.SameLine()
    local details = { "compact", "normal", "everything" }
    if ImGui.Button("Panel: " .. details[(settings.overlayDetail or 1) + 1]) then
      settings.overlayDetail = ((settings.overlayDetail or 1) + 1) % 3
      app.saveSettings()
    end
    checkbox(app, "Driven line", "overlayTrail", true)
    ImGui.SameLine()
    checkbox(app, "Plan / route", "overlayRoute", true)
    ImGui.SameLine()
    checkbox(app, "Sensor and road edges", "overlaySensor", true)
    checkbox(app, "Look-ahead rays", "overlayRays", true)
    ImGui.SameLine()
    checkbox(app, "Where it's heading (2 s)", "overlayPredict", true)
  end

  ImGui.Separator()
  if ImGui.Button(settings.uiAdvancedWindow and "Hide older drivers" or "Older drivers & tuning") then
    settings.uiAdvancedWindow = not settings.uiAdvancedWindow
    app.saveSettings()
  end
  hint("The game's own autodrive (tuned), AI drive, motorcycles and NPC traffic tuning")
  ImGui.SameLine()
  if ImGui.Button(settings.uiDebugWindow and "Hide debug tools" or "Debug tools") then
    settings.uiDebugWindow = not settings.uiDebugWindow
    app.saveSettings()
  end
  ImGui.SameLine()
  if ImGui.Button("Reset all") then app.resetAll() end
  if app.state.dirty then
    ImGui.TextColored(1, 0.8, 0.2, 1, "Changes apply when the overlay closes")
    ImGui.SameLine()
    if ImGui.Button("Apply now") then app.applyAll() end
  end
  grey("Hotkeys: CET > Bindings > Better Car AI")
  ImGui.End()
end

-- The style picker: three cards, the chosen one lit. Clickable while the CET
-- overlay is open; otherwise steering moves the choice, throttle starts,
-- brake cancels (the car is still the player's until it starts).
function UI.DrawPicker(app)
  local picker = app.state.picker
  if not picker then return end
  local w, h = GetDisplayResolution()
  ImGui.SetNextWindowPos(w / 2, h * 0.68, ImGuiCond.Always, 0.5, 0.5)
  ImGui.SetNextWindowBgAlpha(0.85)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.AlwaysAutoResize + ImGuiWindowFlags.NoSavedSettings
      + ImGuiWindowFlags.NoMove
  if not app.state.overlayOpen then flags = flags + ImGuiWindowFlags.NoInputs end
  if ImGui.Begin("BCAI style picker", flags) then
    ImGui.SetWindowFontScale(1.25)
    ImGui.Text("Autodrive+: choose a driving style")
    ImGui.SetWindowFontScale(1.0)
    ImGui.Spacing()
    for i = 1, 3 do
      if i > 1 then ImGui.SameLine() end
      local c = Overlay.STYLE_COLOURS[i]
      local chosen = picker.choice == i - 1
      if chosen then
        ImGui.PushStyleColor(ImGuiCol.Button, c[1] * 0.65, c[2] * 0.65, c[3] * 0.65, 1)
      else
        ImGui.PushStyleColor(ImGuiCol.Button, 0.18, 0.18, 0.2, 1)
      end
      if ImGui.Button((chosen and "> " or "") .. Overlay.STYLE_NAMES[i] .. (chosen and " <" or ""), 150, 40) then
        picker.choice = i - 1
        app.confirmPicker()
      end
      ImGui.PopStyleColor(1)
    end
    ImGui.PushTextWrapPos(470)
    ImGui.TextColored(0.8, 0.8, 0.8, 1, UI.STYLE_TEXT[picker.choice + 1])
    ImGui.PopTextWrapPos()
    ImGui.Spacing()
    if picker.phase == "release" then
      ImGui.TextColored(1, 0.85, 0.4, 1, "Let go of the controls to start...")
    else
      grey(string.format("Steer left/right to choose - autodrive button (G) again to start - brake to cancel - starts in %d s",
        math.max(1, math.ceil(picker.left))))
    end
  end
  ImGui.End()
end

local function tab(label, draw, app)
  if ImGui.BeginTabItem(label) then
    draw(app)
    ImGui.EndTabItem()
  end
end

-- A window with a close button bound to settings[key].
local function window(app, title, key, draw)
  local open, shown = ImGui.Begin(title, true, ImGuiWindowFlags.AlwaysAutoResize)
  if shown then draw() end
  ImGui.End()
  if not open then
    app.settings[key] = false
    app.saveSettings()
  end
end

-- --- Older drivers & tuning ---

local function drawVanillaTab(app)
  wrappedGrey("The game's own autodrive, tuned through its driving parameters. Used when Autodrive+ and AI drive are off.")
  ImGui.Text("Autodrive mode: " .. (app.settings.aggressive and "aggressive" or "normal"))
  ImGui.SameLine()
  if ImGui.Button(app.settings.aggressive and "Switch to normal" or "Switch to aggressive") then app.toggleAggressive() end
  heading("Normal")
  slider(app, "Max speed (km/h, 0 = the game's)", "normalMaxSpeedKmh", 0.0, 300.0, "%.0f")
  checkbox(app, "Obey speed limits", "normalObeySpeedLimit")
  slider(app, "Corner grip (normal)", "normalCornerGrip", 0.8, 1.4)
  slider(app, "Look-ahead (normal)", "normalLookahead", 0.8, 2.0)
  heading("Aggressive")
  slider(app, "Max speed (km/h)", "aggroMaxSpeedKmh", 60.0, 300.0, "%.0f")
  checkbox(app, "Make traffic clear the way", "aggroClearTraffic")
  checkbox(app, "Run red lights (experimental)", "aggroRunLights")
  checkbox(app, "Ignore speed limits", "aggroIgnoreSpeedLimit")
  slider(app, "Over the speed limit by (km/h)", "aggroSpeedLimitBonusKmh", 0.0, 200.0, "%.0f")
  checkbox(app, "Extra obstacle sweeps", "aggroExtraSweeps")
  checkbox(app, "Base on race AI params (experimental)", "aggroUseRaceParams")
  slider(app, "Corner grip", "aggroCornerGrip", 0.8, 1.5)
  slider(app, "Braking safety (< 1 brakes earlier)", "aggroBrakeSafety", 0.6, 1.2)
  slider(app, "Swerve speed", "aggroSwerve", 0.5, 3.0)
  slider(app, "Obstacle look-ahead", "aggroLookahead", 0.8, 5.0)
  slider(app, "High-speed steering reach", "aggroSteerReach", 1.0, 3.0)
  slider(app, "Power boost", "aggroPowerBoost", 0.8, 2.0)
end

local function drawTrafficTab(app)
  checkbox(app, "Tune NPC traffic driving", "trafficTuning")
  slider(app, "Corner grip (traffic)", "trafficCornerGrip", 0.8, 1.4)
  slider(app, "Look-ahead (traffic)", "trafficLookahead", 0.8, 2.0)
  checkbox(app, "Calmer panic driving", "trafficCalmPanic")
  slider(app, "Panic top speed (x)", "trafficPanicSpeed", 0.5, 1.0)
  slider(app, "Panic bend look-ahead (x)", "trafficPanicLookahead", 1.0, 3.0)
end

local function drawAIDriveTab(app)
  wrappedGrey("AI drive: the game's driving AI with the command police drivers use, tuned and speed-governed. "
    .. "Autodrive+ replaced it; it stays for comparison.")
  if ImGui.Button("Start / stop AI drive") then app.toggleAIDrive() end
  checkbox(app, "The game's autodrive starts AI drive", "aiDriveDefault")
  heading("Safety")
  checkbox(app, "Stability governor", "aiGovernor")
  hint("Slows down while sliding, weaving, airborne or before a crest")
  checkbox(app, "Slow down at learned trouble spots", "aiTroubleSpots")
  slider(app, "Trouble spot speed (km/h)", "aiTroubleSpotKmh", 30.0, 120.0, "%.0f")
  grey(#app.state.troubleSpots .. " spots learned")
  if #app.state.troubleSpots > 0 then
    ImGui.SameLine()
    if ImGui.Button("Forget all") then app.forgetTroubleSpots() end
  end
  if app.state.nativeRoute then
    checkbox(app, "Repair broken junctions in the lane map", "aiRepairLanes")
    grey(app.state.laneRepair or "Reconnects junction lanes wired to the wrong side of the road")
    checkbox(app, "Plan speed from the AI's route", "aiRoutePlanner")
    slider(app, "Route bend grip (g)", "aiRouteGrip", 0.4, 1.2, "%.2f")
  end
  heading("Handling")
  slider(app, "Min corner speed (km/h)", "aiMinSpeedKmh", 10.0, 80.0, "%.0f")
  slider(app, "Corner grip (AI drive)", "aiCornerGrip", 0.5, 1.3)
  slider(app, "Braking safety (AI drive)", "aiBrakeSafety", 0.5, 1.2)
  slider(app, "Stability assist", "aiStability", 1.0, 3.0)
  slider(app, "Obstacle look-ahead (AI drive)", "aiLookahead", 2.0, 10.0)
  slider(app, "Return-to-lane speed", "aiReturnRate", 0.2, 1.5)
  slider(app, "Swerve speed (AI drive)", "aiSwerve", 0.5, 2.5)
  slider(app, "Steering damping", "aiSteerDamping", 1.0, 3.0)
  slider(app, "Brake smoothing (lower = smoother)", "aiBrakeSmooth", 0.3, 1.5)
  slider(app, "Steering reach (AI drive)", "aiSteerReach", 1.0, 3.5)
  slider(app, "Steering speed (AI drive)", "aiSteerRate", 0.4, 1.5)
end

local function drawBikeTab(app)
  wrappedGrey("AI drive on motorcycles (bikes steer by leaning: slower, smoother steering).")
  slider(app, "Min corner speed (km/h, bike)", "aiBikeMinSpeedKmh", 10.0, 100.0, "%.0f")
  slider(app, "Corner grip (bike)", "aiBikeCornerGrip", 0.5, 1.6)
  slider(app, "Braking safety (bike)", "aiBikeBrakeSafety", 0.5, 1.2)
  slider(app, "Stability assist (bike)", "aiBikeStability", 1.0, 3.0)
  slider(app, "Obstacle look-ahead (bike)", "aiBikeLookahead", 2.0, 10.0)
  slider(app, "Return-to-lane speed (bike)", "aiBikeReturnRate", 0.2, 1.5)
  slider(app, "Swerve speed (bike)", "aiBikeSwerve", 0.3, 2.5)
  slider(app, "Steering damping (bike)", "aiBikeSteerDamping", 1.0, 4.0)
  slider(app, "Brake smoothing (bike)", "aiBikeBrakeSmooth", 0.3, 1.5)
  slider(app, "Steering reach (bike)", "aiBikeSteerReach", 1.0, 4.0)
  slider(app, "Steering speed (bike)", "aiBikeSteerRate", 0.2, 1.5)
end

function UI.DrawAdvanced(app)
  window(app, "Better Car AI: older drivers & tuning", "uiAdvancedWindow", function()
    if ImGui.BeginTabBar("bcai_advanced_tabs") then
      tab("Game's autodrive", drawVanillaTab, app)
      tab("NPC traffic", drawTrafficTab, app)
      tab("AI drive", drawAIDriveTab, app)
      tab("Motorcycles", drawBikeTab, app)
      ImGui.EndTabBar()
    end
    if app.state.dirty then
      ImGui.TextColored(1, 0.8, 0.2, 1, "Changes apply when the overlay closes")
      ImGui.SameLine()
      if ImGui.Button("Apply now") then app.applyAll() end
    end
  end)
end

-- --- Debug tools ---

local function drawOverlayTab(app)
  local state = app.state
  if state.nativeLanes then
    local lanes, lanesPressed = ImGui.Checkbox("Lane map (70 m around)", state.drawLanes)
    if lanesPressed then
      state.drawLanes = lanes
      if lanes then app.setOverlay(true) end
    end
    ImGui.SameLine()
    local pave, pavePressed = ImGui.Checkbox("with sidewalks", state.drawPavements)
    if pavePressed then state.drawPavements = pave end
    grey("lanes: pale blue road / amber junction / purple GPS-only / red no autodrive")
    grey("       grey sidewalk / green repaired; arrow = driving direction")
  else
    grey("Lane map: needs the native plugin 0.12 or newer")
  end
  heading("If the overlay's lines are misplaced or scaled wrong")
  local v, p = ImGui.Checkbox("Use our own projection", state.debugManualProjection)
  if p then state.debugManualProjection = v end
  v, p = ImGui.Checkbox("FOV is horizontal", state.debugFovHorizontal)
  if p then state.debugFovHorizontal = v end
  local sys = app.scriptSettings()
  local d = sys and sys:DebugState()
  if d then grey(d:Calibration()) end
end

local function drawRecorderTab(app)
  checkbox(app, "Drive recorder (keeps the last 60 s, saves AI drive wipe-outs)", "recorder")
  if ImGui.Button("Save recording now") then app.saveRecording("saved by hand") end
  grey("Saved as rec_<date>_<time>.csv in the CET mod folder")
  local trace, pressed = ImGui.Checkbox("Trace the game's autodrive to the log (every second)", app.state.trace)
  if pressed then app.state.trace = trace end
  heading("BetterCarAI.log")
  checkbox(app, "Autodrive+'s decisions and why (lane changes, passing, slowing down)", "ownLogEvents", true)
  checkbox(app, "Autodrive+'s raw numbers (twice a second)", "ownLog", true)
  checkbox(app, "What NPC drivers do when bumped", "trafficLog", true)
  checkbox(app, "Routes to the map destination, and why each was planned", "navLog", true)
  grey("In the CET mod folder; search for \"Autodrive+ |\", \"Autodrive+ raw:\", \"Traffic |\" or \"Route |\"")
  heading("Autodrive+'s latest decisions")
  if #app.state.events == 0 then grey("(none yet this drive)") end
  for _, e in ipairs(app.state.events) do ImGui.Text(e.text) end
end

local function drawVehicleTab(app)
  if ImGui.Button("Describe this car") then app.describeVehicle(false) end
  ImGui.SameLine()
  if ImGui.Button("...and write it to the log") then app.describeVehicle(true) end
  local sys = app.scriptSettings()
  if sys then ImGui.TextWrapped(sys:DescribeAutodrive()) end
  for _, line in ipairs(app.state.debugLines) do ImGui.Text(line) end
end

local function drawLanesTab(app)
  local version = app.nativeVersion()
  if not version then
    ImGui.TextWrapped("Native plugin not loaded. Build it with red4ext/build.sh; install.sh copies it to red4ext/plugins/BetterCarAI.")
    return
  end
  ImGui.Text(string.format("Plugin %d.%d.%d loaded", math.floor(version / 10000), math.floor(version / 100) % 100, version % 100))
  ImGui.TextWrapped("Lane repair: " .. (app.state.laneRepair or (app.settings.aiRepairLanes and "not run yet" or "off")))
  if ImGui.Button("Dump the AI's route") then
    app.nativeAction("Route", function() return Game.BCAI_DumpRoute(app.autodriveSystem(), app.carOrPlayerPosition()) end)
  end
  ImGui.SameLine()
  if ImGui.Button("Lanes within 60 m") then
    app.nativeAction("Lane query", function() return Game.BCAI_LaneQuery(app.autodriveSystem(), app.carOrPlayerPosition(), 60.0) end)
  end
  ImGui.SameLine()
  if ImGui.Button("All lanes in the city") then
    app.nativeAction("Lanes", function() return Game.BCAI_DumpLaneGraph(app.autodriveSystem()) end)
  end
  grey("Files go to red4ext/plugins/BetterCarAI/dumps")
end

local function drawOwnDriverTab(app)
  if not app.ownDriverAvailable() then
    ImGui.TextWrapped("Needs the native plugin 0.19 or newer.")
    return
  end
  ImGui.TextWrapped("Status: " .. tostring(Game.BCAI_DriverStatus()))
  if ImGui.Button("Dump Autodrive+'s plan") then
    app.nativeAction("Plan", function() return Game.BCAI_DriverDumpPath() end)
  end
  ImGui.SameLine()
  grey("to dumps/driver_path.txt")
  if ImGui.Button("Sensor test") then app.sensorTest() end
  ImGui.SameLine()
  grey("park 5-20 m behind a car, facing it")
  if app.state.sensorTest then ImGui.TextWrapped(app.state.sensorTest) end
  local group, changed = ImGui.InputText("Sensor collision group", app.settings.ownSensorGroup, 64)
  if changed then
    app.settings.ownSensorGroup = group
    app.state.dirty = true
  end
  local vehicle = app.mountedVehicle()
  if vehicle then
    local ok, c = pcall(function() return Game.BCAI_ReadControls(vehicle) end)
    if ok and c then
      local function show(v) return v <= -98 and "?" or string.format("%+.2f", v) end
      ImGui.Text("Car's controls now: throttle " .. show(c.x) .. "  brake " .. show(c.y) .. "  steer " .. show(c.z)
        .. "  handbrake " .. show(c.w))
    end
  end
  if app.state.ownDriving then
    heading("Last log line")
    ImGui.PushTextWrapPos(620)
    grey(tostring(Game.BCAI_DriverTelemetry()))
    ImGui.PopTextWrapPos()
  end
end

local function drawResearchTab(app)
  if not app.nativeVersion() then
    ImGui.TextWrapped("Needs the native plugin.")
    return
  end
  heading("The game's driving AI")
  if ImGui.Button("Scan for the AI's planned path") then app.nativeScanForPath() end
  ImGui.SameLine()
  if ImGui.Button("Dump car, AI component and drive command") then app.nativeDumpObjects() end
  if ImGui.Button("Dump vehicle AI types") then
    app.nativeAction("Type dump", function()
      return Game.BCAI_DumpTypes("vehicle,traffic,autopilot,autodrive,drive,steer,lane,navigation,AIVehicle,aiv")
    end)
  end
  ImGui.SameLine()
  if ImGui.Button("Dump native function addresses") then
    app.nativeAction("Native functions", function()
      return Game.BCAI_DumpNatives("autodrive,autopilot,vehicle,traffic,lane,AIVehicle,drive,navigation")
    end)
  end
  if app.nav.Available() then
    heading("The game's GPS (the route the map draws)")
    if ImGui.Button("Scan the GPS for its route") then
      app.nativeAction("GPS scan", function()
        local vehicle = app.mountedVehicle()
        local player = Game.GetPlayer()
        local thing = vehicle or player
        return Game.BCAI_ScanGPS(app.autodriveSystem(), thing:GetWorldPosition(), thing:GetWorldForward())
      end)
    end
    ImGui.SameLine()
    if ImGui.Button("Dump GPS and map types") then
      app.nativeAction("Type dump", function() return Game.BCAI_DumpTypes("gps,mappin,minimap,worldmap") end)
    end
    grey("Set a map pin first, so the GPS has a route; then send the dumps folder")
  end
  if not app.controlsAvailable() then return end
  heading("Car controls (what Autodrive+ drives through)")
  if ImGui.Button("1. Find the controls") then app.startControlProbe() end
  ImGui.SameLine()
  grey("close the overlay and drive 25 s")
  if ImGui.Button("2. Test the controls") then app.startControlTest() end
  ImGui.SameLine()
  grey("stopped, clear road ahead, hands off")
  local state = app.state
  if state.controlProbe then
    ImGui.Text(string.format("Probing: %d s left, %d samples", math.ceil(state.controlProbe.left), state.controlProbe.samples))
  end
  if state.controlTest then ImGui.Text("Testing...") end
  ImGui.TextWrapped("Fields: " .. tostring(Game.BCAI_ControlFields()))
  if state.controlReport then ImGui.TextWrapped("Probe: " .. state.controlReport) end
  for _, line in ipairs(state.controlTestReport or {}) do ImGui.Text(line) end
end

local TEST_PHASES = {
  ready = "Route ready: drive it (recording starts as you drive off)",
  player = "Recording your drive...",
  driven = "Your drive is saved",
  placing = "Putting the car back at the start...",
  ai = "Recording Autodrive+'s drive...",
  done = "Both drives saved",
}

local function drawDriveTestTab(app)
  local Test = app.driveTest
  local test = Test.Get()
  wrappedGrey("You drive a route, then Autodrive+ drives the same route from the same start. Both drives "
    .. "are recorded, so we can compare how fast each took the bends, how hard each braked, and how much grip "
    .. "the car really has. Send the test_*.txt and test_*.csv files from this mod's folder.")
  ImGui.Spacing()
  if not test then
    if ImGui.Button("New test to my map pin") then app.state.testError = Test.NewToPin() end
    hint("Put a pin on the map past the bends you want to compare, then press this. The game's GPS shows the way.")
    if ImGui.Button("Random route (1-2 km)") then app.state.testError = Test.New(700, 1500) end
    ImGui.SameLine()
    if ImGui.Button("Longer (2-4 km)") then app.state.testError = Test.New(1500, 3000) end
    hint("Follow the yellow line to the marker; the minimap's GPS still shows the way to your own pin, if you have one.")
    if app.state.testError then ImGui.TextColored(1, 0.5, 0.4, 1, app.state.testError) end
    hint("Stop on an ordinary road first. Recording starts as you drive off.")
    return
  end
  ImGui.Text("Test " .. test.id)
  ImGui.TextColored(1, 0.85, 0.4, 1, TEST_PHASES[test.phase] or test.phase)
  local summary = app.nav.Summary()
  if summary then wrappedGrey("Route: " .. summary) end
  if test.run then
    ImGui.Text(string.format("%.0f s, %d rows%s", test.run.t, #test.run.rows,
      test.run.replans > 0 and string.format(", off the route %d times", test.run.replans) or ""))
    if ImGui.Button("Stop and save") then Test.Stop() end
  elseif test.phase == "driven" or test.phase == "done" then
    styleButtons(app)
    if ImGui.Button("Let Autodrive+ drive it") then app.state.testError = Test.RunAI() end
    hint("Puts the car back at the start, then Autodrive+ drives the route in the style chosen")
  end
  if app.state.testError then ImGui.TextColored(1, 0.5, 0.4, 1, app.state.testError) end
  for _, name in ipairs(test.saved) do grey("saved " .. name) end
  if ImGui.Button("End the test") then
    Test.Clear()
    app.state.testError = nil
  end
end

function UI.DrawTools(app)
  window(app, "Better Car AI: debug tools", "uiDebugWindow", function()
    if ImGui.BeginTabBar("bcai_debug_tabs") then
      tab("Overlay", drawOverlayTab, app)
      tab("Autodrive+", drawOwnDriverTab, app)
      tab("Recorder & log", drawRecorderTab, app)
      tab("Vehicle", drawVehicleTab, app)
      tab("Lanes & routes", drawLanesTab, app)
      tab("Driving test", drawDriveTestTab, app)
      tab("Research", drawResearchTab, app)
      ImGui.EndTabBar()
    end
  end)
end

return UI
