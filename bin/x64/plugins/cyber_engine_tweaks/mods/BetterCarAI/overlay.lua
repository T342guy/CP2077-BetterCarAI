-- Better Car AI - the visual debug overlay: lines and markers drawn over the
-- world (from the scripts' BCAIDebug), and the info panel (from the scripts'
-- measurements and, while Autodrive+ drives, the plugin's own account of
-- what it is doing).

local Overlay = {}

-- ImGui colours are 0xAABBGGRR; passed as signed 32-bit ints.
local function rgba(r, g, b, a)
  local c = a * 16777216 + b * 65536 + g * 256 + r
  if c >= 2147483648 then c = c - 4294967296 end
  return c
end

-- Indexed as the scripts' BCAIDebug colour numbers.
local COLOURS = {
  rgba(255, 255, 255, 230), -- 1 white: heading, labels
  rgba(60, 220, 255, 230),  -- 2 cyan: travel direction, predicted path
  rgba(80, 230, 90, 230),   -- 3 green: gripping
  rgba(255, 220, 50, 230),  -- 4 yellow: mild slide, road rising ahead, plan slowing a little
  rgba(255, 140, 30, 240),  -- 5 orange: vehicle, plan slowing
  rgba(255, 50, 50, 240),   -- 6 red: wall, heavy slide, plan slowing hard
  rgba(150, 150, 150, 130), -- 7 grey: clear ray, road edges
  rgba(240, 70, 240, 240),  -- 8 magenta: destination, airborne, what is in the way
  rgba(70, 140, 255, 230),  -- 9 blue: the route / plan at speed
  rgba(150, 180, 220, 200), -- 10 lane map: road lane
  rgba(255, 190, 60, 220),  -- 11 lane map: junction lane
  rgba(190, 110, 255, 220), -- 12 lane map: GPS-only lane
  rgba(120, 255, 120, 240), -- 13 lane map: repaired lane
}
local TEXT_SHADOW = rgba(0, 0, 0, 200)

-- Style colours (r, g, b, 0..1) and names, as in the settings window.
Overlay.STYLE_COLOURS = { { 0.35, 0.85, 0.45 }, { 1.0, 0.6, 0.2 }, { 1.0, 0.3, 0.3 } }
Overlay.STYLE_NAMES = { "City", "Aggressive", "Madman" }

-- "key=value;key=value" from BCAI_DriverHud, numbers as numbers.
function Overlay.ParseHud(text)
  local t = {}
  for k, v in tostring(text or ""):gmatch("([%w_]+)=([^;]*)") do
    t[k] = tonumber(v) or v
  end
  return t
end

-- The shapes the scripts projected this frame.
local function drawWorld(sys, w, h)
  local buf = sys:DebugFrame() or {}
  local texts = sys:DebugFrameTexts() or {}
  local stats = { floats = #buf, shapes = 0, onScreen = 0 }
  if #buf < 8 then return stats end
  ImGui.SetNextWindowPos(0, 0)
  ImGui.SetNextWindowSize(w, h)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize + ImGuiWindowFlags.NoMove
      + ImGuiWindowFlags.NoInputs + ImGuiWindowFlags.NoBackground + ImGuiWindowFlags.NoSavedSettings
      + ImGuiWindowFlags.NoBringToFrontOnFocus + ImGuiWindowFlags.NoScrollbar
  if ImGui.Begin("BCAI debug draw", flags) then
    local dl = ImGui.GetWindowDrawList()
    for i = 9, #buf - 7, 8 do
      local kind, col = buf[i], COLOURS[buf[i + 5]] or COLOURS[1]
      local x, y = buf[i + 1] * w, buf[i + 2] * h
      stats.shapes = stats.shapes + 1
      if x >= 0 and x <= w and y >= 0 and y <= h then stats.onScreen = stats.onScreen + 1 end
      if kind == 1 then
        ImGui.ImDrawListAddLine(dl, x, y, buf[i + 3] * w, buf[i + 4] * h, col, buf[i + 6])
      elseif kind == 2 then
        ImGui.ImDrawListAddCircle(dl, x, y, buf[i + 3], col, 16, buf[i + 6])
      elseif kind == 3 and texts[buf[i + 7]] then
        -- (A dark copy underneath, so labels read on bright scenery.)
        local tx, ty = x + buf[i + 3], y - 8
        ImGui.ImDrawListAddText(dl, tx + 1, ty + 1, TEXT_SHADOW, texts[buf[i + 7]])
        ImGui.ImDrawListAddText(dl, tx, ty, col, texts[buf[i + 7]])
      end
    end
  end
  ImGui.End()
  return stats
end

local function km(m)
  if m >= 1000 then return string.format("%.1f km", m / 1000) end
  return string.format("%.0f m", m)
end

local function colouredText(rgb, text)
  ImGui.TextColored(rgb[1], rgb[2], rgb[3], 1, text)
end

-- A labelled bar (fraction 0..1) in a colour.
local function bar(label, fraction, rgb, text)
  ImGui.Text(label)
  ImGui.SameLine(70)
  ImGui.PushStyleColor(ImGuiCol.PlotHistogram, rgb[1], rgb[2], rgb[3], 0.9)
  ImGui.ProgressBar(math.max(0, math.min(1, fraction)), 170, 14, text or "")
  ImGui.PopStyleColor(1)
end

local GREY = { 0.65, 0.65, 0.65 }
local WHITE = { 1, 1, 1 }
local DOING_COLOURS = {
  ["cruising"] = { 0.6, 0.85, 1.0 },
  ["following"] = { 1.0, 0.85, 0.4 },
  ["overtaking"] = { 1.0, 0.6, 0.2 },
  ["passing on the wrong side"] = { 1.0, 0.45, 0.3 },
  ["going round"] = { 1.0, 0.75, 0.3 },
  ["dropping back"] = { 1.0, 0.75, 0.3 },
  ["backing up"] = { 1.0, 0.4, 0.4 },
  ["waiting"] = { 1.0, 0.85, 0.4 },
  ["waiting at junction"] = { 1.0, 0.85, 0.4 },
}

-- Lane n of m counted from the right, and where it is heading.
local function laneText(hud)
  if not hud.lanes or hud.lanes < 1 or not hud.lane or hud.lane < 1 then return "between lanes" end
  local text = string.format("lane %d of %d", hud.lane, hud.lanes)
  if hud.tolane and hud.tolane > 0 and hud.tolane ~= hud.lane then
    text = text .. string.format(" -> %d", hud.tolane)
  end
  return text .. " (1 = rightmost)"
end

local function aheadText(hud)
  if not hud.ahead or hud.ahead < 0 then return "clear" end
  local what = ({ car = "car", parked = "parked car", stopped = "stopped car", wall = "wall" })[hud.aheadkind] or "something"
  if hud.aheadkind == "car" then
    return string.format("%s %s ahead, %.0f km/h", what, km(hud.ahead), hud.aheadv or 0)
  end
  return string.format("%s %s ahead", what, km(hud.ahead))
end

-- The label on what is in the way, drawn on it in the world.
function Overlay.BlockLabel(hud)
  if not hud or hud.active ~= 1 or not hud.ahead or hud.ahead < 0 then return "" end
  if hud.aheadkind == "car" then return string.format("%.0f m, %.0f km/h", hud.ahead, hud.aheadv or 0) end
  return string.format("%s %.0f m", hud.aheadkind or "", hud.ahead)
end

-- The latest decisions (BCAI_DriverEvents lines), newest last, how long
-- ago, without the time/speed/place columns; shorter ones in the normal panel.
local function eventLines(events, count, width)
  local first = math.max(1, #events - count + 1)
  for i = first, #events do
    local e = events[i]
    local what = e.text:match("^%s*[%d%.]+ s%s+%-?%d+ km/h%s+%-?%d+ m%s+(.*)$") or e.text
    if #what > width then what = what:sub(1, width - 3) .. "..." end
    local age = os.clock() - e.at
    local fade = age < 5 and 1.0 or 0.6
    ImGui.TextColored(fade, fade, fade, 1, string.format("%3.0fs ago  %s", age, what))
  end
end

local function groundLine(d)
  if not d then return end
  if d:IsAirborne() then
    ImGui.TextColored(0.95, 0.3, 0.95, 1, "AIRBORNE")
  else
    local kind = d:Surface()
    local names = { [0] = "road", "dirt", "grass", "other ground", "no ground found" }
    local rgb = kind == 0 and GREY or { 1.0, 0.8, 0.4 }
    colouredText(rgb, "on " .. (names[kind] or "?") .. (d:SurfaceName() ~= "" and (" (" .. d:SurfaceName() .. ")") or ""))
  end
  ImGui.SameLine()
  ImGui.TextColored(0.65, 0.65, 0.65, 1, string.format("  slip %.1f m/s, turning %.0f deg/s%s", d:Lateral(), d:YawRateDeg(),
    d:IsSkidding() and ", skidding" or ""))
end

-- The info panel. ctx: settings, state, mode ("Autodrive+", "AI drive"...),
-- hud (parsed BCAI_DriverHud while Autodrive+ drives), events (its latest
-- decisions), debug (BCAIDebug),
-- lines (the scripts' own lines), stats (what was drawn).
function Overlay.DrawPanel(ctx)
  local settings, hud, d = ctx.settings, ctx.hud or {}, ctx.debug
  local detail = settings.overlayDetail or 1
  ImGui.SetNextWindowPos(20, 240, ImGuiCond.FirstUseEver)
  ImGui.SetNextWindowBgAlpha(0.72)
  local flags = ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.AlwaysAutoResize + ImGuiWindowFlags.NoFocusOnAppearing
  if not ctx.overlayOpen then flags = flags + ImGuiWindowFlags.NoInputs end
  if not ImGui.Begin("Better Car AI panel", flags) then
    ImGui.End()
    return
  end
  local speed = d and d:Speed() * 3.6 or 0
  local own = hud.active == 1
  -- Header: what drives, and in which style.
  ImGui.Text("BETTER CAR AI")
  ImGui.SameLine()
  colouredText(GREY, " " .. ctx.mode)
  if own and hud.style then
    ImGui.SameLine()
    colouredText(Overlay.STYLE_COLOURS[hud.style + 1], " " .. Overlay.STYLE_NAMES[hud.style + 1])
  end
  if detail == 0 then
    -- One line: speed, what it does, what is ahead.
    local text = string.format("%.0f km/h", speed)
    if own then text = text .. string.format(" (target %.0f) - %s - %s", hud.target or 0, hud.doing or "", aheadText(hud)) end
    ImGui.Text(text)
    if d and d:IsAirborne() then ImGui.SameLine() ImGui.TextColored(0.95, 0.3, 0.95, 1, " AIRBORNE") end
    ImGui.End()
    return
  end
  ImGui.Separator()
  -- Speed, large.
  ImGui.SetWindowFontScale(1.7)
  ImGui.Text(string.format("%3.0f", speed))
  ImGui.SetWindowFontScale(1.0)
  ImGui.SameLine()
  if own then
    ImGui.Text(string.format("km/h   target %.0f   road %.0f", hud.target or 0, hud.limit or 0))
  else
    ImGui.Text("km/h")
  end
  if own then
    colouredText(DOING_COLOURS[hud.doing] or WHITE, ((hud.doing or "?"):gsub("^%l", string.upper)))
    if hud.why and hud.why ~= "" and (hud.target or 0) < (hud.limit or 0) - 1 then
      ImGui.SameLine()
      colouredText(GREY, "  (slower for: " .. hud.why .. ")")
    end
    ImGui.Text("Ahead: " .. aheadText(hud))
    ImGui.Text("Lane: " .. laneText(hud))
    if hud.oncoming == 1 then ImGui.SameLine() ImGui.TextColored(1, 0.45, 0.3, 1, "  in the oncoming lanes") end
    if hud.bend and hud.bend >= 0 then
      ImGui.Text(string.format("Next slow-down: %s ahead, to %.0f km/h", km(hud.bend), hud.bendv or 0))
    end
    if hud.junction and hud.junction >= 0 then
      ImGui.TextColored(1, 0.85, 0.4, 1, string.format("Junction %s ahead: a car across may go", km(hud.junction)))
    end
    if hud.left and hud.left >= 0 then
      local eta = speed > 5 and hud.left / (speed / 3.6) or nil
      ImGui.Text("Destination: " .. km(hud.left) .. (eta and string.format(" (about %d min)", math.max(1, math.floor(eta / 60 + 0.5))) or ""))
    else
      colouredText(GREY, "No destination: following the road")
    end
    ImGui.Spacing()
    bar("Throttle", hud.throttle or 0, { 0.3, 0.85, 0.4 })
    bar("Brake", hud.brake or 0, { 0.95, 0.3, 0.3 })
    bar("Steer", ((hud.steer or 0) + 1) / 2, { 0.4, 0.7, 1.0 }, string.format("%+.2f", hud.steer or 0))
    if ctx.events and #ctx.events > 0 then
      ImGui.Spacing()
      colouredText(GREY, "Latest decisions:")
      eventLines(ctx.events, detail >= 2 and 6 or 3, detail >= 2 and 110 or 70)
    end
  elseif ctx.mode ~= "manual driving" then
    for _, line in ipairs(ctx.lines or {}) do ImGui.Text(line) end
  end
  ImGui.Spacing()
  groundLine(d)
  if detail >= 2 then
    ImGui.Separator()
    if own then
      ImGui.Text(string.format("This drive: %.0f m off the road, %d replans, %d lane changes, %d changes of mind",
        hud.offroad or 0, hud.replans or 0, hud.moves or 0, hud.dithers or 0))
      ImGui.Text(string.format("Grip (learned) %.2f g, slip angle %.1f deg", hud.grip or 0, hud.slip or 0))
      colouredText(GREY, "Status: " .. tostring(hud.status or ""))
    end
    if d then
      ImGui.Text(string.format("Look-ahead rays (%.0f m): car %s, wall %s", d:Lookahead(),
        d:NearestVehicle() < 0 and "-" or string.format("%.0f m", d:NearestVehicle()),
        d:NearestWall() < 0 and "-" or string.format("%.0f m", d:NearestWall())))
    end
    if own then
      for _, line in ipairs(ctx.lines or {}) do colouredText(GREY, line) end
    end
    local s = ctx.stats or {}
    colouredText(GREY, string.format("drawn: %d shapes, %d on screen", s.shapes or 0, s.onScreen or 0))
    ImGui.Separator()
    colouredText(GREY, "trail: green grip / yellow-red sliding / magenta airborne")
    colouredText(GREY, "plan: blue at speed / yellow-orange-red slowing; magenta: in the way")
    colouredText(GREY, "rays: orange car / red wall / yellow road rising / grey clear")
    colouredText(GREY, "cyan: where the car's motion takes it in 2 s; white: heading")
  end
  ImGui.End()
end

function Overlay.DrawWorld(sys)
  local w, h = GetDisplayResolution()
  return drawWorld(sys, w, h)
end

return Overlay
