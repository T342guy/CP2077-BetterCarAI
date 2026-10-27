# Research notes (game v2.5)

<!-- ANCHOR: intro -->

Symbols were taken from public decompiled 2.31 scripts and every hooked symbol was
checked against the 2.5 `r6/cache/final.redscripts`. The `.reds` files compile
against the 2.5 bundle with the game's `scc.exe`.

<!-- ANCHOR_END: intro -->

## Why traffic panics from a tap

<!-- ANCHOR: traffic-panic -->

`VehicleObject.OnVehicleBumpEvent` → `HandleTrafficBump(impact)` (impact = velocity change, m/s):

- If `impact > AIGeneralSettings.minUnconsciousImpact` (6.5 m/s ≈ 23 km/h), the game rolls to
  knock the driver out (`TriggerUnconsciousBehaviorForPassengers`).
- Otherwise `EscalateBumpVehicleReaction`: the third bump within 60 s sends a `Bump` stim to the driver.

The driver's `ReactionManagerComponent.HandleCrowdReaction(stimEvent)` turns any crowd stim
received while mounted into a `HandleReactionEvent` on the vehicle →
`VehicleObject.OnHandleReactionEvent` → panic driving, bailing out, or "fear inside".
The same function also starts 2.x *aggressive crowd* combat.

**Hooks** (`r6/scripts/BetterCarAI/CalmTraffic.reds`):
- `HandleTrafficBump`: below the threshold, skip the knock-out roll and escalation and just honk.
- `HandleCrowdReaction`: drop mild stims (`Bump`, `VehicleHit`, `Driving`, `DodgeVehicle`,
  `VehicleHorn`, `TooCloseDistance`) for mounted NPCs unless the vehicle had a hard impact
  in the last 3 s or the player is in combat. Police (`IsPrevention`) is untouched.

<!-- ANCHOR_END: traffic-panic -->

## Autodrive

<!-- ANCHOR: autodrive -->

- `AutoDriveSystem` (script) extends `NativeAutodriveSystem` (native). The drive command
  (`DoStartDriveToPointAutonomousAutodrive`) only passes the destination. Speed, steering and
  avoidance are native.
- The native driver reads `Vehicle_Record.drivingParamsAutodrive` → `BaseDrivingParameters_Record`:
  - `vision` (`VehicleAIVisionSettings`): `useSpeedLimit`, `speedLimitSafetyMargin`,
    `maxDistance`, `minDistance`, `numSweeps`, `offsetSmoothingFastChangeSpeed`,
    `offsetSmoothingReturnToNeutralSpeed`, `sideOverlap*`, `maxStraightPathExtension`
  - `traction` (`VehicleAITractionEstimation`): `turningAccelFactor`, `forwardAccelFactor`, `brakingAccelFactor`
  - `boostStats` (`VehicleAIBoostSettings`): `maxTorqueBoost`, `maxLongTractionBoost`, `maxLatTractionBoost`
  - also `stopping`, `brake`/`accel` PIDs, `steering`, `panic`
- The drive node takes its command values from `AIDriveCommandsDelegate` (a `ScriptBehaviorDelegate`).
  `DoStartDriveToPointAutonomousAutodrive` only sets `targetPosition`; `maxSpeed`, `clearTrafficOnPath`,
  `forceGreenLights`, `useSpeedBasedLookupRange` and `boostDrivingStats` keep their defaults.
  `Autodrive.reds` sets them on start and also pushes `maxSpeed`/`clearTrafficOnPath` into the running
  command with a `DriveToPointAutonomousUpdate` (what vanilla uses when the destination changes).
  Whether the native node actually honours each field is unverified.
- Player cars such as the Caliburn have no `drivingParamsAutodrive` of their own (the native driver falls
  back to `drivingParamsGeneric` = `Driving.Default_4w`). Its vision `maxDistance` is 13 m and steering
  aims at most 15 m ahead, reached at 30 m/s; both are far too short for 150+ km/h.
- Vehicles also have `drivingParamsGeneric` (NPC AI) and `drivingParamsRace` (race AI).
- Vanilla turns autodrive off on any non-direct hit (`OnAutoDriveHitRequest`).
- `NativeAutodriveSystem.SetAutodriveEnabled(Bool, opt Bool)` is public. `EnableAutoDriveRequest`
  re-enables it after a lane-validity check.

TweakXL doesn't support 2.5, so `profiles.lua` clones these records at runtime with CET
(`TweakDB:CloneRecord`/`SetFlat`/`Update`) and points the mounted vehicle's
`drivingParamsAutodrive` at the clone for the current mode.

<!-- ANCHOR_END: autodrive -->

## Limits

<!-- ANCHOR: limits -->

- The path follower, lane changes and traffic-light handling are native. `clearTrafficOnPath` and
  `forceGreenLights` exist only on quest-issued AI commands, not on the player autodrive command.
- Ambient crowd traffic is moved by the native traffic system. The generic-params tuning
  mainly affects AI-commanded NPC vehicles (chases, quests, panic driving).
- It isn't known yet whether the native autodrive re-reads its params mid-drive, so the mod
  restarts autodrive when the params change.
- In-game test: aggressive autodrive capped at ~75 km/h (accelerate to 75, coast to 69, repeat) and swerved
  around stopped cars only a few metres away, before the delegate hook and longer vision were added.
- In-game test (Caliburn): autodrive using `Driving.Race_Driving` as its params revved high, barely
  accelerated, braked constantly and crawled through corners. `Driving.Default_4w` drove normally.
  So aggressive mode is built on the car's own autodrive params, and the race base is opt-in.
- Trace (aggressive, Caliburn, straight highway): `GetCurrentSpeed` swings 82 → 90 → 82 km/h with an
  ~8 s period, i.e. a controller hunting around ~24 m/s. Player autodrive has no
  `AIVehicleDriveToPointAutonomousCommand` on the vehicle's AI component (`cmd=none`), and
  `DoUpdateDriveToPointAutonomousAutodrive` never runs. `DoStartDriveToPointAutonomousAutodrive` does run
  (vanilla delegate `maxSpeed` = 0), but setting `maxSpeed` = 250 km/h there changes nothing.
  Vision `useSpeedLimit` false, or true with a −28 m/s safety margin, also changes nothing.
  So the ~24 m/s target is not the lane speed limit and not the command speed: it is set in the
  native autodrive node or in data we haven't found (the only known TweakDB group is
  `Vehicle.autodrive_settings`).
- CET's TweakDB editor shows nothing for "autodrive", so the target speed is most likely native.
- Workaround being tested: send `AIVehicleDriveToPointAutonomousCommand` (`needDriver = false`, `maxSpeed`,
  `clearTrafficOnPath`) to the player's vehicle, as `PreventionSystem.StartDriveToPoint` does for police.
- redscript 2.5 (`scc`) fails with "constant pool error: definition not found" when `@addField`/`@addMethod`
  target a class defined by the mod itself, so such members go directly in the class.
- In-game test: AI drive works on the player's car. It ran to the map destination at 160 km/h (cap 250),
  but spun out in high-speed turns. Assumed: the command reads `drivingParamsGeneric` (traffic-tuned to
  1.1× corner grip), so AI drive now points generic and autodrive at an `aidrive` clone with 0.8× corner
  grip, 0.85× braking and 1.5× lateral boost while it runs.
- In-game city test: AI drive accelerates hard but brakes from ~150 to ~17 km/h within ~4 s for every sharp
  turn or on-ramp. `CancelCommand` didn't stop a running command (vanilla uses `StopExecutingCommand` for
  `Executing` and `CancelCommand` only for `Enqueued`), and WASD didn't hand back control. AI drive now
  stops like vanilla autodrive: input listeners on `TurnX`/`Accelerate`/`Decelerate`.
- City test with AI drive: corner grip 1.1 → no change, 1.3 → launched out of a roundabout, while tight turns
  stayed at ~15–20 km/h. That floor matches the command's `minSpeed` (5 m/s then), so `minSpeed` looks like
  the driver's speed through tight path corners (police use 10 m/s). It is now a setting (default 36 km/h),
  corner grip is reset to 1.0, and AI drive scales `mildSteeringSpeed`/`fullSteeringSpeed` (0.75×) and
  steering reach (2.2×) against oversteer when the lane jogs sideways at junctions.
- Next city test: corners better, but it swerved only at the last moment, cut back into traffic, and didn't
  stop at the destination (the command ends on arrival with no braking). It also overcorrected at speed.
  Changes:
  - AI drive gets its own vision: 6× look-ahead (~80 m) and a 0.5× return-to-lane rate.
  - A governor (every 0.25 s from CET) pushes `DriveToPointAutonomousUpdate` with
    maxSpeed = sqrt(2·4·remaining), as `InVehicleDriveToPointAutonomousDecorator` does for running commands.
  - On arrival it calls `ForceBrakesUntilStoppedOrFor(10)`, like vanilla autodrive.
  - Calmer defaults: braking safety 0.65, min corner speed 30, steering speed 0.9, reach 2.0.
- Next test: much better overall; sharp lane-change turns sit at the 30 km/h min corner speed (expected), and
  a new side-to-side wiggle appeared at speed. AI drive now uses its own swerve speed (1.2× instead of 1.6×)
  and scales the steering `errorMagnitudeFor{Mild,Full}Steering` thresholds by 1.6 to ignore tiny path errors.
- AI drive hit overhanging railings in the slums. Likely cause: native autodrive skips lanes flagged
  `NoAutodrive` ("If set autodrive will not be allowed to use this lane"), but the NPC drive command ignores
  that flag, and vision only tracks vehicles, not static geometry. There's no script access to lane flags.
- Brake smoothing: AI drive scales `stopping.{mild,full}BrakingChangeSpeed` (brake ramp rate) by 0.65.
- AI drive is now the default for autodrive (CET takes over within 0.25 s of native autodrive starting).
  The autodrive HUD (`AutoDriveController`, minimap highlight, input context) only reads the
  `UI_AutodriveData` blackboard, so AI drive keeps `AutoDriveEnabled`/`FreeRoamEnabled` set and writes
  `DestinationRemainingLength`/`Time` itself (straight-line distance, time at the current speed). The HUD's
  hold-to-toggle then starts native autodrive, which AI drive treats as "switch off".
- First HUD test: the HUD flashed on and AI drive stopped within a second. Assumed cause: AI drive stopped
  whenever native autodrive still reported enabled, and native takes a moment to switch off. Now a still-on
  native autodrive is just switched off again; the autodrive button is detected directly (`HoldAutodrive`
  hold-complete), and native autodrive started in the next 2 s isn't taken over. The stop reason is logged
  (`AI drive ended: ...`) and shown as `lastStop` in the debug line.
- HUD test: works, but the distance/time counters flickered. AI drive now rewrites them every frame (from CET
  `onUpdate`), and the ETA uses a ~5 s average speed. `hudOverwrites` in the debug line counts how often
  something else (presumably native autodrive) replaced our distance between frames.
- Motorcycle test: AI drive didn't use the bike's tighter turning and the wobble got worse. Bikes
  (`vehicle as BikeObject`) now get their own `aibike` clone and `aiBike*` settings: slower steering (0.55×),
  more reach (2.6×) and damping (2.4×), gentler swerves (0.8×), more corner grip (1.15×), a 45 km/h min corner
  speed and less lateral assist (1.3×; extra lateral grip may fight the lean).
- Visual debug overlay (`Debug.reds` + CET ImGui draw list). The native path follower's route is not exposed
  (no script API for the vehicle's lane path), so it draws measurable things: destination, driven trail
  coloured by sideways slip / airborne, a 2 s prediction from speed and yaw rate, heading vs velocity, and
  7 look-ahead rays (±20°, vision `maxDistance` long) against the `Vehicle Chassis` and `World Static`
  collision presets (an upward-facing static hit = road rising). Projection uses vanilla
  `CameraSystem.ProjectPoint`; its conventions (NDC or pixels, axis signs, homogeneous W) are detected each
  frame from three known points and shown on the panel's last line (Codeware isn't installed).
- First overlay test: the panel showed but no lines. Unknown which step failed, so the panel now reports
  shape counts / on-screen counts and the projection state, and the overlay falls back to its own perspective
  projection (camera axes + `GetActiveCameraFOV`/`GetAspectRatio`) whenever `ProjectPoint` doesn't look like
  NDC. Debug toggles: force our projection, FOV horizontal vs vertical.
- Another wipe-out (minimap route looked normal). Added a stability governor to AI drive (4 Hz, on by
  default): lowers the command's maxSpeed via `DriveToPointAutonomousUpdate` when sliding (lateral speed >
  max(2, 8% of speed)), weaving (yaw > 12°/s flipping sign within 1.5 s above 20 m/s), airborne (> 1 m to
  ground) or before a crest (ground ~1 s ahead along the current turn is 1.5–8 m below the car's line;
  cap 90 km/h). It holds for 2 s after the last trigger, then recovers at 3 m/s per second.
- Overlay works. The wiggle (±15°/s yaw, ~1 s period) shows clearly. Trace evidence: with the HUD flag
  faked, `AutoDriveSystem.GetAutodriveEnabled()` reads true during AI drive (111/114 samples; before the HUD
  change 1124/1124 read false), while `DoStartDriveToPointAutonomousAutodrive` never fires mid-drive. So
  vanilla's autodrive "enabled" state is the `UI_AutodriveData.AutoDriveEnabled` blackboard flag, and AI
  drive's "keep native off" check was running vanilla's full autodrive shutdown (TPP camera soft reset,
  facts, save lock) 4× a second. That check is gone. Instead, vanilla clearing the flag (combat, weapon,
  damage, quest lock, player via the HUD) now stops AI drive like it stops autodrive, except
  `OnAutoDriveHitRequest` (any bump), which is ignored while AI drive runs.
- Governor: a spin (sideways speed > 70% of speed, or yaw > 90°/s) drops the cap to the min corner speed
  for 3 s so the car settles before it accelerates again.
- Overlay hides in menus (`RadialWheelController.OnIsInMenuChanged`), while paused and on foot.
- Drive recorder (`Recorder.reds`): a 60 s / 10 Hz ring buffer of CSV rows built from the overlay's
  measurements plus the command, governor and vehicle blackboard (`RPMValue`, `GearValue`,
  `SuspensionTransversalForce`, `SuspensionLongitudinalForce`, `Collision`). The AI's own steering input and
  planned path aren't readable from scripts. Auto-save 5 s after a spin, a collision (rising edge of
  `Collision`) or a > 8 m/s drop within 0.5 s, if AI drive was active within the last 3 s, at most every 15 s.
  `tools/analyze_recording.py` turns a file into a timeline and a PNG.
- First recordings (Quadra Type-66, aggressive AI drive, five wipe-outs, two at the same T-junction):
  every one starts with yaw rate going from ~0 to 80–112 °/s within 0.2–0.3 s at 75–142 km/h, with the
  throttle still on (RPM rising, no downshift, no braking beforehand) and the destination 450–950 m away.
  Afterwards the car carries on along the same road, so these are not planned turns but steering snaps. Twice a
  car was 8–37 m ahead in the look-ahead rays (a late dodge). At the T-junction (−564, 3210) the rays were
  clear, so the snap came from the native path/vision itself. The car then slid 17 m/s sideways, hit a kerb
  and was airborne for ~3 s. Changes: AI drive steering speed 0.9 → 0.65× and swerve speed 1.2 → 0.7×
  (settings v7 resets both), and the governor caps speed to a 2 s gap behind a car within ±4° ahead
  (`car ahead N m`, lifts as soon as the lane is clear).
- Recorder false alarms: hard braking (the AI's, or the player's right after taking over) passed the "> 8 m/s
  within 0.5 s" slowdown test, and tight low-speed turns passed the "> 90 °/s" spin test. Triggers are now:
  spin = above 36 km/h, sliding at more than half the speed sideways or turn rate × speed > 20 m/s²
  (~2 g); collision above 36 km/h; impact = losing > 12 m/s within 0.5 s. Taking over with input clears the
  "AI drive was active" window. The save reason carries the trigger time (`spin at t=274.82`). Re-run over
  the first six recordings, this keeps the four real wipe-outs and drops the two false saves.
- No script access to the AI's steering: `VehicleDef` has no steering value, and the steering records
  (`MaxWheelTurnDeg` etc.) are static data. The analyser estimates the wheel angle from turn rate and speed
  (2.8 m wheelbase) and shows cornering demand (turn rate × speed) against ~1 g of grip.
- Repeat run through the T-junction after slowing AI steering (0.9 → 0.65×) and swerve (1.2 → 0.7×): the snap
  was practically identical (−1 → −28 → −78 → −112 °/s at 125 km/h, same spot). The same save reload gives the
  same run, and the snap ignored the steering settings, so they can't fix it: it is a kink in the lane path
  through that junction, followed at any speed. Another incident: a ~30° right bend taken at 137 km/h with no
  braking (2.6 g), where the AI braked properly for a similar bend at ~92 km/h, so its corner planning seems to
  look too short at very high speed.
- Trouble spots: the governor learns where AI drive turned harder than ~1.8 g (turn rate × speed > 17.5 m/s²)
  above 58 km/h while not yet sliding, with no car within ±12° / 60 m in the last 1.5 s (a dodge), and not within
  5 s of a slide or the last learn. It stores the position and heading from the previous tick (where the turn
  began). CET saves them to `trouble_spots.json` and re-sends them after each load. When AI drive approaches a
  spot in the same direction (within 10 m sideways, 6 m vertically, heading within ~45°), the cap brakes it at
  ~4 m/s² to the spot speed (default 60 km/h) 15 m before the spot and holds it until 35 m past. The minimap at the T-junction confirms the cause: the route jogs into the
  mouth of the side road and back out onto the main road, two sideways kinks a few tens of metres apart. Replayed over the
  first seven recordings, this learns exactly the three non-dodge incidents and skips both dodges.
- Seven more Javelina crashes in the city (with trouble spots on): the T-junction still spun at 53 km/h after
  slowing for the spot (94 °/s, 2.5 g), so the spot speed is now 40 km/h. Two took bends at 123–130 km/h without
  braking. One was a late dodge at 108 km/h. One took off over a crest at 140 km/h: "crest ahead" fired but it
  had only slowed to 126 km/h at take-off. The rest were snaps at 77–142 km/h. No new spots were learned,
  because the Javelina's constant mild slide kept triggering the "recovering" cooldown. That cooldown now needs
  a spin or sideways speed over 30% of forward speed.
- The vehicle blackboard `Collision` flag can stay true for the rest of the drive after a crash. Only its
  rising edge means anything (the recorder trigger already used it; the analyser now does too).

<!-- ANCHOR_END: limits -->

## RED4ext plugin

<!-- ANCHOR: plugin -->

- RED4ext v1.30.0 runs on 2.5 (product 2.50, file 3.0.88.46235) and loads addresses from the game's own
  `bin/x64/cyberpunk2077_addresses.json` (1,032,136 addresses). RED4ext.SDK master supports 2.5 (#227).
- Build: clang-cl + lld-link from a Fedora toolbox, with the MSVC CRT/Windows SDK from `xwin splat`. The SDK
  needs a case-fix include overlay (`RED4ext/rtti` vs `RTTI`, `AnimKeyFrames` vs `animKeyFrames`), which
  build.sh generates. The DLL links the CRT statically and imports only KERNEL32/USER32.
- The SDK's generated headers only cover RTTI-visible fields. `vehicleAutopilot` (0x3C0 bytes) and
  `vehicleDriver` (0x28) have none. `vehicleAutonomousData` exposes the command fields (targetPosition,
  clearTrafficOnPath, useSpeedBasedLookupRange, ignoreNoAIDrivingLanes, boostDrivingStats, ...), but not the path.
  So the path follower's state has to be located by scanning memory (`BCAI_ScanForPath`) or by reverse
  engineering.
- Path scans v0.1–0.3 (city, AI drive at speed). Nothing was a route. Found instead:
  - Big scattered point sets (up to 35,000 elements, 300 m jumps): traffic and map data.
  - The destination: a cluster of points that stays put while the car closes in.
  - A 6-point fixed-point (int32 / 2^17) record about 11 m long per vehicle, stride 64, with copies for the
    car and for cars ahead in the lane. It looks like per-vehicle traffic lane slots.
  - A 22-point float3 polyline with ~2.8 m spacing, 170–200 m ahead, 59% on the road the car drove next.
    Matching scans against a recording made 20 s after each scan (`path scan follow-up`) is what tells these
    apart.
- That polyline is a traffic lane outline. RTTI layout (from the type dump):
  `worldTrafficLanePersistent` [0xA0]: outLanes +0x00, inLanes +0x10, outline `array<Vector3>` +0x20,
  accumulatedLengths `array<Float>` +0x30, polygon +0x50, nodeRefHash +0x70, length +0x78, width +0x7C,
  flags +0x88, laneNumber +0x8C, isReversed +0x90, maxSpeed +0x91.
  `worldTrafficPersistentData.lanes` is the whole lane table. `worldTrafficLaneUID` = {nodeRefHash, laneNumber}.
  `worldTrafficSplineNode` (the source data) carries `noAIDriving` / `noAutodrive`.
  `worldTrafficGlobalPathPosition` {worldPosition, pathIdx} suggests path positions are stored as an index
  into a lane path.
- Scanner v0.4 recognises lane objects anywhere by their layout (outline and running-length arrays of equal
  count, total length = last running length). It reports single lane pointers, arrays of lane pointers
  (likely the route) and small lane-by-value arrays, each with the full outline. Roots are read only up to their
  RTTI class size: v0.3 read 4 KB past small objects, e.g. the 0x90-byte drive command, into unrelated neighbours.
- Crash (v0.4.0, scan pressed before AI drive started): access violation in the plugin's `memcpy` (minidump:
  BetterCarAI.dll+0x4C8FD, an AVX copy, called from the lane check's 0x80-byte read), at 0x62491A18. The memory
  map had said the page was readable. Under Wine, a test exe shows that clang-cl with `/EHsc` drops
  `__try/__except` around both `memcpy` and a volatile copy loop (the process dies), while `/EHa` keeps them.
  `ReadProcessMemory(GetCurrentProcess(), ...)` fails cleanly and costs ~0.06 µs per call. The scanner now copies
  only through ReadProcessMemory, and the plugin builds with `/EHa`.
  `red4ext/tests/scan_harness.cpp` (target `scan_harness`, run under Wine) scans a fake object graph with
  freed, no-access and guard pages, garbage pointers and the crash address. It survives, leaves the guard
  page intact, and finds a fake three-lane route in order.
- Crash reports under Proton: `compatdata/1091500/pfx/drive_c/users/steamuser/AppData/Local/REDEngine/
  ReportQueue/<time>/` (report.txt, stacktrace.txt, Cyberpunk2077.dmp). The minidump's exception and module
  streams give the faulting module and offset.
- Native function addresses: the address map (`cyberpunk2077_addresses.json`, 1.1 M hashes) names only 84
  symbols. Two of them help: `rtti::NativeMemberFunction::NativeMemberFunction` (exe+0x6BDCE4) stores the C++
  handler in a global array at exe+0x3B7BD30 (the SDK's `CBaseFunction_Handlers` hash resolves to the array
  itself; the SDK's `GetHandler` dereferences it once too often), with the slot index at function+0xF8.
  Natives can also be found statically: the name string in .rdata, then the registration code that loads it,
  which passes the handler to the constructor.
- `gameNativeAutodriveSystem::CheckCurrentLaneValidity` = exe+0x21C7F40 (registered at exe+0x21C5A4D):
  - Gets the autodrive vehicle and its fixed-point world position (scale 1/131072).
  - traffic = autodrive+0x520 → +0x78 → +0x170.
  - Calls exe+0x42FBCC(traffic, Vector4 pos, float radius (xmm2), out array, filter, ...). Results are 0x30
    bytes; field 0 is a `worldTrafficLanePersistent*`.
  - For each result: point-in-polygon on lane+0x50 with radius vehicle+0x68C × const; then lane+0x88 bit 15.
  - Result 0 = on a valid lane; 1 = only on lanes with bit 15 set (no autodrive); 2 = not on a lane.
  So lane flags 0x8000 = noAutodrive.
- Plugin v0.6 `BCAI_LaneQuery`: follows that pointer chain, finds lane tables (DynArrays of lanes by value)
  within 3 pointers of the traffic system, and lists the lanes near a point. Output per lane: outline, flags,
  maxSpeed and raw out-lane connections.
- The game's lane lookup (exe+0x42FBCC) reads `container = *(traffic+0x1D8)` (`worldTrafficPersistentData`) and the
  lane table as the DynArray at container+0x8. v0.6's table search reached it but rejected it: its lane check
  assumed one running length per point. Read directly (v0.7), the table in 2.5 holds **33,953 lanes: the
  whole city**, so a lane's index is a stable id.
  - `accumulatedLengths` has one entry per segment (outline count − 1). The first is the first segment's length
    and the last is `length`.
  - `flags` is `worldTrafficLanePersistentFlags`: 1 spline, 2 bidirectional, 4 patrol, 8 pavement, 16 road,
    32 intersection, 64 neverDeadEnd, 128 trafficDisabled, 256 crosswalk, 512 gpsOnly, 1024 showDebug,
    2048 blockade, 4096 yield, 8192 noAIDriving, 16384 highway, 32768 noAutodrive. Seen near a junction:
    driving lanes 0x0011 (road), junction lanes 0x0031 (+intersection), GPS-only junction links 0x02B1,
    sidewalks 0x000B / 0x002B / 0x012B (pavement, crosswalk), and paths 0xA00A (pavement + noAIDriving +
    noAutodrive).
  - The fields after the flags are `neighborGroupIndex` +0x8A, `laneNumber` +0x8C, `seqNumber` +0x8E,
    `isReversed` +0x90, `maxSpeed` +0x91, `subGraphId` +0x96 and `deadEndStart` +0x9C. Most lanes come in
    forward/reversed pairs with the same outline backwards.
  - `worldTrafficConnectivityOutLane` (0xC bytes, no RTTI fields) is {u32 id, float at, float into}. The id's
    low 16 bits are the target lane's index. Its high bits are usually 0xA4FF and sometimes 0xA47F; their
    meaning is unknown. `at` is where the link leaves this lane (usually its full length; less for crosswalk
    lanes crossing the road). `into` is where it joins the target (usually 0). This was checked against
    lanes that start where another ends: e.g. lane 7494 → 30038..30042, its five junction lanes.
- Plugin v0.8 keeps a `LaneGraph`: the whole table copied in one read (5.4 MB, a few ms) plus every lane's
  out-links. The path scan uses it to recognise **routes**: arrays whose consecutive elements name linked
  lanes, as u16 indices (or the low half of u32 ids like the out-links), lane pointers into the table, or
  `worldTrafficLaneUID`s. Scanning from `autodrive` also covers the traffic system. The harness
  checks all three forms against a fake table reached through the same pointer chain.
- **The route (v0.8 scans, 4 runs over junctions):** `autodrive+0x540 → +0x30 → +0x3C0` is an object holding
  the car's whole route as `worldTrafficLaneUID`s ({Uint64 nodeRefHash, Uint16 laneNumber, Uint16 seqNumber,
  Bool isReversed}):
  - +0x20: one 0x28-byte entry per lane (UID + 0x18 unknown bytes). Seen: 17 lanes from where AI drive started
    to the destination ~1.4 km away. It includes `gpsOnly|trafficDisabled` junction links, and the car was
    on it (0.1–0.9 m) in two scans 300 m apart.
  - +0x10: 0x10-byte UIDs, each lane repeated (99 entries for those 17 lanes), meaning unknown.
  - Junction lanes share the hash and lane number and differ only in `seqNumber` (lane+0x8E). v0.8 ignored
    the seq and matched some junction entries to the wrong branch, so a few steps showed as unlinked.
  - The scan from `autodrive` found it in 2 of 4 runs. The other two timed out first: a 1 s breadth-first
    budget doesn't always reach depth 3.
  - Other hits: `aicomponent+0x38→+0x68→+0x4A0→+0xF0` is a range of 32-byte entries with lane pointers at
    +0x18, holding the current lane and the junction branches ahead (likely the follower's local lane
    candidates). `aicomponent+0x1B8→+0x58` has 24-byte UID arrays of 4–5 lanes near the car, and the drive
    command's +0x38→+0x188→+0x500 has route-like UID arrays of a road 110 m away (possibly another
    vehicle's).
- Plugin v0.9 reads the route directly: `BCAI_RouteAhead(autodrive, pos, metres)` joins the route lanes'
  outlines, projects the car onto them and returns the points ahead. The overlay draws them in blue.
  `BCAI_DumpRoute` writes the route object, both arrays raw and decoded, and whether consecutive lanes link.
- In-game check (v0.9 overlay, T-junction at about (-558, 3181)): the blue route line jogs sideways across the
  road and back. The recording from the same run (rec_20261024_174432) has the snap there: 2 → 117°/s at
  64–80 km/h. So the lane path itself is broken at that junction, as suspected.
- v0.10:
  - **Route line:** lanes are cut where their links leave and join (`at` / `into`), not joined end to end.
    The car's position along the route is tracked, so a route that doubles back near itself doesn't make the
    projection jump.
  - **Speed planner:** `BCAI_RoutePlan` resamples the next 400 m at 1 m. Curvature is the heading change
    between the 4 m before and the 4 m after each point, per metre. Each bend allows v = √(a_lat / κ), and
    braking at 4 m/s² until 10 m before it gives the cap now. CET calls it at 4 Hz and the governor takes the
    minimum of it and its other caps (setting "Route bend grip", default 0.8 g, floor = AI min speed).
  - `BCAI_DumpRoute` lists every bend on the route sharper than a 50 km/h corner at 0.8 g, with its lane.
    Recordings save a route dump alongside.
  - `BCAI_DumpLaneGraph` writes all 33,953 lanes (flags, links, outline) to `dumps/lanegraph.txt` for
    offline analysis.
- **Root cause of the T-junction jog** (lane map dump, junction at (-556, 3180)):
  - The south road's lanes are right: northbound #8032 is on the east side. But #8032 **ends with no
    out-links**.
  - The junction lanes that should continue it (#16383 left turn, gpsOnly; #16384 straight on) start
    **5 m west**, at the start of the southbound lane #8033, and have **no in-links**.
  - The other direction is mirrored: #16381 and #16388 lead into the south road but end on #8032's end
    instead of #8033's start, which has no in-links.
  - So the junction is wired to the wrong side of the road. The route still goes through (the planner bridges
    the gap), so it steps 5 m sideways and 0.8 m back at the junction mouth: the Z on the overlay and the
    117°/s snap.
  - Citywide search: a drivable lane with no out-links, a lane starting 1.5–10 m from its end with no in-links,
    heading the same way (dot > 0.5), and nothing starting within 0.5 m. This finds 40 such ends, **all on
    roads with traffic disabled**, which is why NPC traffic never shows it. 42 lane ends in 11 junctions
    belong to junction lanes; the rest are road-to-road gaps, left alone. The other two learned trouble spots
    are not this defect.
- **Plugin v0.11 lane repair** (`BCAI_RepairLanes`, setting "Repair broken junctions in the lane map", on by
  default):
  - Moves each such junction lane's end onto the road lane's end and redraws it as a cubic curve keeping
    its end directions (same point count; two-point lanes stay straight).
  - Rewrites the running lengths, length, area polygon (each vertex moves with its nearest outline point)
    and the `at` of links that left at the old end.
  - Road lanes are never changed. Writes go through WriteProcessMemory.
  - Runs once per lane table (checked every second, re-done if a save load rebuilt the table), and can be
    undone from saved originals. Lists its changes in `dumps/lane_repairs.txt`.
  - On the dumped map it repairs 40 lanes in 10 ms. The harness can load a `lanegraph.txt` and run the
    repair on it (`scan_harness.exe lanegraph.txt`).
  - Missing links are not added (that needs engine allocations). The planner already routes through these
    junctions, so the geometry is what matters.
  - Before/after: `docs/src/images/tjunction_repair.png`.
- v0.11 in game: `lane_repairs.txt` shows the 40 lanes repaired at load, including #16381/16383/16384/16388.
  The user still saw the jog at the T-junction. The blue route line reads lane outlines live, so the route
  there must use lanes other than the repaired ones; it needs a route dump at the junction.
- v0.12 adds a lane-map overlay (`BCAI_LanesNear`; the lane graph keeps per-lane bounding boxes). It draws
  every lane within 70 m by kind, with direction arrows, lane numbers near the car, dead ends and repaired
  lanes. It also adds a route dump hotkey.
- v0.12 lane-map overlay at the T-junction (user screenshot) showed **"dead end"** on the northbound lane:
  #8032 has no out-links, and #16381/#16388 none into #8033. Route planning follows links, so to the planner
  the south road isn't connected to the junction and the route goes around it. Shape repair alone can't fix
  that.
- **v0.13 adds the missing links** for every repaired junction lane, in the direction of travel. The road lane
  gets an out-link to a junction lane whose start moved; a junction lane whose end moved gets one to the road
  lane.
  - Out-link = {(0xA4FF << 16) | target, at = source length, into = 0}, where 0xA4FF is the most common high
    half.
  - The arrays grow through RED4ext's `DynArray::PushBack`, i.e. the engine's `DynArray_Realloc` (hash
    0x7AA013D2 = `red::DynamicBuffer::ResizeBuffer` in the 2.5 address map). It uses the array's own
    allocator, which the engine stores in the entries pointer (capacity 0) or after the buffer, so the game
    frees the grown arrays normally.
  - In-link entries (`worldTrafficConnectivityInLane`, 0x10 bytes, no RTTI fields) are built from a layout
    learned at runtime. Lanes with exactly one in-link are compared with the out-link pointing at them to
    find which word holds the source index, `at` and `into`, and which are constant (≥ 98 % agreement).
    If the layout isn't understood, only out-links are added. `lane_repairs.txt` logs the learned layout
    with examples.
  - Undo shrinks the arrays back to their old sizes.
  - On the dumped map, 42 links are added and the T-junction has no dead ends left. Link-only searches:
    8032 → 16384 → 9212 (north), 8032 → 16383 → 14079 (west), 14080 → 16381 → 8033.
- **Crash on quit** (reports at 10:19, 10:32 and 11:38 on 2026-10-24): each followed an `install.sh` run while
  the game was running.
  - Two minidumps fault in Wine's `call_tls_callbacks` (ntdll+0x47828, Proton 11) reading the plugin's TLS
    directory (RVA 0x92F90). `AddressOfCallBacks` held 0x41 in one and the ASCII bytes "operatio" in the
    other. The third faults inside the plugin's own code, called from the game's shutdown.
  - Proton reads the DLL from the file on disk, so `cp` over the installed DLL (same inode) changed the
    image under the running game.
  - install.sh now copies to a new file and renames it into place, so a running game keeps the old file.
  - Quit crashes from before the plugin existed (2026-10-20, older game build) are in the game's own code.

<!-- ANCHOR_END: plugin -->

## BCAI Autodrive+, our own driving AI (plugin 0.14+)

<!-- ANCHOR: v014 -->

Goal: replace the game's native path follower with our own driver, BCAI Autodrive+. It plans over the
repaired lane graph, steers along the lane line and sets its speed from the route speed planner.
It drives the car the way the player does, through the car's own throttle, brake and steering
inputs, so the game's real vehicle physics still applies.

- The SDK's hand-written `vehicleBaseObject` (from an older game version) has `acceleration` at
  0x264 and `deceleration` at 0x268. In 2.5 the class is 0x20 bytes bigger (0xBC0) and `archetype`
  has moved from 0x3A0 to 0x3B0, so those offsets are stale. The native function table gives no
  handler addresses to disassemble from, so the inputs are found by watching memory instead.
- Control probe (`Controls.cpp`): while the player drives, every frame compares each float in the
  car object, and in the first 0x200 bytes of every object it points to, against the player's
  `Accelerate`/`Decelerate`/`TurnX`/`Handbrake` inputs (correlation and a linear fit).
  - The best field per input goes to `controls.txt` next to the plugin.
  - The full ranking goes to `dumps/controls_probe.txt`.
- Controls: a RED4ext game state (Running) writes the chosen inputs every frame. Writes stop 5 s
  after the last call. Between our writes it counts how often the game changed each field; if it
  rewrites them every frame, our writes need a hook after the game's input handling instead.
- Control test (CET debug window, "Research" tab): throttle, steer +, steer −, brake, measuring
  the change in speed and heading for each.
- First in-game probe (2377 samples): throttle `car+0x264`, brake `car+0x268`, handbrake `car+0x26C` and
  steering `car+0x278` are exact copies of the inputs (r = 1.000), the same offsets as the SDK's older layout.
  `car+0x270` and `car+0x280` hold throttle − brake. Writing the inputs once per frame from a game state did
  nothing in the control test.
- Cause: the car's input reader (2.5: `exe+0x1BC03C`, found by searching the exe for code writing 0x264,
  0x268, 0x26C and 0x278) runs every frame:
  - it clears 0x264..0x2AA;
  - it reads the input actions by name hash from the input object at `car+0x598` and writes throttle,
    brake, handbrake, throttle − brake and steering;
  - at the end it applies the accelerate quickhack (flag `car+0x6F1`: throttle 1).
  
  Cars without an input object return right after clearing.
- Plugin 0.15 hooks the reader, found at load by its first 36 bytes, which must be unique in `.text`. It
  calls the original and then writes our inputs when Autodrive+ is driving that car, so the physics
  sees ours.

### Autodrive+ (plugin 0.16)

- **Route:** A* over lanes (`Driver.cpp`).
  - A link costs the distance driven along its lane from where the car entered that lane (link `into`)
    to where the link leaves it (link `at`).
  - Lane cost factors: ×6 for noAutodrive/noAIDriving, ×1.3 for trafficDisabled, ×1.1 for gpsOnly.
    Pavements and blockades are excluded.
  - Without a destination it cruises, taking the least-turning link at each lane end.
  - The path is resampled every metre.
- **Speed plan:**
  - lane `maxSpeed` (traffic m/s: 10 city, 15, 17 highway) × a factor, capped at the top speed;
  - √(grip/κ) in bends;
  - braking ahead of time at a set deceleration;
  - a stop at the destination.
- **Steering:** pure pursuit (look-ahead 4 + 0.4·v m) gives the turn rate wanted. The difference to
  the measured turn rate (from the heading) corrects the wheel, so it doesn't matter how much the
  game cuts steering at speed. Steering is rate-limited and capped at speed. Positive TurnX turns
  right; the driver flips the sign if driving shows otherwise.
- **Pedals:**
  - The acceleration wanted is the speed error per second plus the slowdown the plan asks for.
  - Below about −0.6 m/s² it brakes, otherwise it coasts or uses the throttle.
  - It holds the car with the handbrake when stopped, because the brake pedal reverses a
    stopped car.
  - It eases off the throttle when sliding, and slows down when the car turns less than asked.
- **Following:** raycasts along the path ahead (collision group `Vehicle`, ignoring surfaces facing
  up) give the distance to the next car. Its speed comes from how that distance changes.
  - Safe speed is √(v_lead² + 2·6·(d − 4)).
  - Below the following distance it slows to v_lead + 0.4·(d − wanted).
- **Bug found in the route speed planner:** the curvature from two 4 m chords was divided by 8 m
  instead of 4 m. That halved the curvature, so planned corner speeds were about 40% too high.
  It is fixed in `Route.cpp` too.
- **Offline simulation** (`scan_harness lanegraph.txt drive …`, bicycle model with speed-sensitive
  steering and 9 m/s² grip):
  - 2.5 km and 3.3 km drives across the city arrive within 4 m of the goal, staying ≤ 1 m off the
    line (0.1 m mean) at up to 6.3 m/s².
  - It cruises 18 km with ≤ 1.1 m error.
  - From 92 km/h it stops about 4 m behind a stopped car.

### How the game answers the controls (plugin 0.18)

The first in-game telemetry (v0.17) showed that the car's controls aren't linear:

- **Steering** follows a curve. The path curvature driven is about **0.17 · steer² / (1 + (v/25)²)**
  per metre. The fit was the same at 26, 37, 60 and 65 km/h. So steer 0.05 at 60 km/h barely turns the
  car (yaw 0.005 rad/s, where a linear wheel would give 0.1). The old controller assumed a linear wheel
  with weak turn-rate feedback. It let the car drift about 1 m off the line, then corrected late.
- **Brake** is steep: brake 0.2 already slowed the car at about 6–7 m/s².
- **Full throttle** gave about 3.3 m/s² at 15–30 km/h, falling to about 2 m/s² at 65 km/h.
- The understeer guard ("turning less than asked: slow down") mistook the weak small-angle steering
  for lost grip. It dropped the target speed to v − 2 and braked, then let go and floored it. That
  was the none-to-full braking.

**Controller since plugin 0.18 (`CarResponse` in `Driver.cpp`):**

- It learns, while driving, how the car answers each control:
  - curvature = k · steer^p, fitted by least squares in log space, with old samples fading after
    about 45 s and a pull towards the prior (0.17, 2);
  - brake deceleration = k · brake^p (prior 8, 0.3);
  - full-throttle acceleration and coasting deceleration, by speed in 5 m/s steps.
- Samples count only while there's no sliding and the pedals have held one state for 0.5–0.8 s.
  Gravity on slopes (from the forward vector's pitch) is taken out.
- **Steering:** the curvature wanted (pure pursuit), plus 0.3 × the curvature error and a slow trim,
  goes through the inverse of the learned curve.
- **Speed:** the acceleration wanted is 0.8 × the speed error, plus the plan's slowdown, jerk-limited
  (8 m/s³, or 25 m/s³ when braking hard).
  - Throttle = (wanted − coasting − slope) / full-throttle acceleration.
  - Brake comes from the inverse brake curve.
  - There is a 0.25 m/s² dead band before the brake, and a slow trim against the measured
    acceleration.
- **Grip guard:** it trips only on a slip angle over 0.1 rad, or with the wheel at its limit and the
  car turning less than 70% of what it should. It then lowers the speed cap by 3 m/s per second
  instead of jumping.
- What it learned stays across restarts while you stay in the same car, and starts over in another
  car. The telemetry line ends with what it has learned.

**Offline simulation** (the harness car now has the game's curves; `BCAI_SIM=k,p,brakeK,brakeP` tries
other cars):

- Same 3.3 km drive: ≤ 0.96 m off the line (0.09 m mean), 11 throttle/brake switches in 214 s.
- The fits match the simulated cars. Steering (0.25, 1) was learned as (0.26, 1.0), (0.12, 3) as
  (0.11, 2.8) and (0.2, 1.5) as (0.20, 1.5). Brake (10, 0.12) was learned as (8.3, 0.13).
- All those cars stay within 1.45 m of the line.
- **Harness bug fixed:** the simulated car took its road height from `DriverAhead(3 m)`, which has
  been empty since that list starts at the bumper. The car kept its starting height, and once the
  road dropped 5 m, `Locate` ignored the right part of the path.

### Flowing line, road room and getting round things (plugin 0.19)

In-game test of 0.18:
- It never went above 86 km/h, because the plan capped speed at the lane's traffic speed × 1.6.
- It braked on and off because the speed plan rose and fell between bends.
- It froze in a turn when the old path-following sensor reported a "car" 0.0 m ahead.
- It followed the lanes' sharp junction corners exactly.

What changed:

- **Road room** (`MeasureRoom`): at every metre of the route, samples every 0.5 m along the line square to it.
  - A sample is road if it is within 2.6 m of any road lane, of any direction. Inside a junction, every turn's lanes cover the area.
  - The room runs out from the line to the first sample that isn't road, or is nearer a sidewalk.
  - The "own side" room stops where the nearest lane runs the other way, so it ends at the oncoming lanes.
  - Lane widths in the map are a flat 5 m and can't be used. Highway lanes are 5 m apart.
  - An earlier version checked only lanes crossing the square line. It was too unstable in junctions, and missed lanes because it checked height at the start of segments that can be hundreds of metres long on slopes.
- **Flowing line** (`Flow`): the lane path bent as little as possible within that room.
  - Each point moves towards (4·(q[i−k] + q[i+k]) − q[i−2k] − q[i+2k]) / 6, at k = 12, 6, 3, 2, 1. This gives wide entry, apex, wide exit.
  - An earlier version simply pulled each point towards the midpoint of its neighbours. That only shortcuts, so it hugged the inside of a 125° hairpin tighter than a car can turn.
  - Off junctions the line stays within about 1 m of the lane, on its own side. Only inside a junction may it use the whole area.
  - The speed plan uses the flowed line's curvature, so corners are faster.
- **Sensor** (`Rays`/`Sense`):
  - The plugin picks the rays: 14 per frame from a pattern over the road ahead looking for vehicles (collision group "Vehicle"), and 9 looking for walls ("World Static", hits facing up count as road). Once a car is in the way, 8 more aim across and along it.
  - The scripts cast them, and the plugin remembers the hits as points along the path.
  - The car's own outline is ignored, and so are hits more than 3.5 m above or below the road at that point.
  - A ray that passed a remembered point (within 0.3 m) clears it.
  - Vehicle points last 1 s, or 3 s when beside the car (where the rays can't see again); wall points last 3 s.
- **Cars as outlines** (`Gather`/`Follow`):
  - Points within a car's length group into one outline.
  - Until the points span a car's width, the outline grows by the missing width on both sides, so it is cautious while only part of the car has been seen. It also gets a car's length beyond the nearest point.
  - Outlines are followed from frame to frame, and their speed along the path is measured over 0.5 s windows.
  - A moving car is checked where the car will meet it: the gap is scaled by v / (v − u), and so is its length.
- **Avoidance** (`Avoid`):
  - Candidates: sideways moves to every 0.5 m within ±6.5 m, as smooth S-curves 8 + 2.2·v metres long. Quicker ones at half that length are tried only when something is close, and none needs more grip than the cornering setting.
  - Score: 2 × clear distance, +30 if fully clear, −500 if it leaves the road before 12 m past the thing being passed, −1.2/m off the line, and −0.3/m of change.
  - The oncoming lanes are used only while the line is blocked by something measured to stand still.
  - A move is kept for at least 1 s (0.3 s when something is close) and needs 3 points more to be replaced. It doesn't turn back on itself unless it can no longer stop.
  - It returns to the line once the line is clear all the way ahead.
  - Following uses the gap on the chosen path and the measured speed of what is in it. It stops 7 m back from something standing still, so it can still steer round.
- **Speed:**
  - It coasts instead of speeding up when a slower stretch within 160 m couldn't be reached by slowing at 1.5 m/s².
  - New defaults: 180 km/h top speed, 2.2 × traffic speed, 0.8 g cornering (settings version 9 moves the old defaults over).
- **Simulation** (harness `drive`, with the game's control curves; `BCAI_LEAD`, `BCAI_OBST_L` put another car on the route, which answers the driver's rays as a 4.6 × 1.9 m box):
  - Both test routes stay within 0.8 m of the line, with 1–7 throttle/brake switches per drive.
  - The obstacle sweeps put a stopped car, a slow car (4–12 m/s) or a car offset sideways by +1.5, −1.5 or −3 m every 200 m along both routes, at the old and the new speed defaults. That is 12 sweeps, 336 runs.
  - 328 got past cleanly (≥ 2.5 m centre to centre) or stopped behind it.
  - 5 touched or scraped the other car. 3 of those involve a car parked 1.5 m into the oncoming side; the others were a 5 m/s car and a car 1.5 m to the right, at the faster defaults.
  - 3 stopped and waited where they saw no way past (one when already partly alongside).

### Traffic, junctions and a faster simulator (plugin 0.20)

In-game test of 0.19: smooth driving and good use of the pedals, and it got round a single car, but:
- the planned line kept changing past junctions, so it swerved;
- among several cars beside and ahead it swerved and crashed (the log shows a car in the next lane reported as "car 0.0 m ahead", and the wheel swinging ±3–5 m off the line at 75 km/h before the crash);
- the flowed line ran along the road's edges;
- the sensor rays didn't tilt with the car, so on steep slopes they hit the road.

Causes found and what changed:

- **Outlines were far too wide.** A car seen as one point grew by a whole car width on both sides (3.8 m), so a car in the next lane covered this one's lane. The outline now grows away from this car (the rays see a car's near side), and only 30% of the missing width towards it. A parked car's outline is remembered as it is seen bit by bit (`Track::ext*`), so it no longer jumps between frames.
- **Junction clutter caused the swerving.** Kerbs, corner buildings and parked cars beside the road made points the planner swerved round, and crossing cars were treated as standing obstacles (their speed along the road is near 0). Now:
  - hits beside the road are dropped (except across a junction);
  - tracks measure sideways speed too (both sides of the outline must move the same way);
  - crossing cars are waited for, not swerved round;
  - in or near a junction it doesn't swing out round moving traffic.
- **Clearance is now driven ahead in time** (`Clearance`):
  - every car where it will be, at this car's speed, speeding up (2 m/s²) and slowing down (3 m/s²), checked over the stretch where the two cars are level;
  - the move starts from where the car really is, closing on the plan over 12 m;
  - a moving car has 0.8 s of room in front of it, so it isn't cut up;
  - a car alongside may stay as close as it is, but get no closer.
- **Sensing all round:**
  - the lanes beside and behind it, 8–40 m back (4 rays a frame), so it doesn't change lanes into a car coming up;
  - short rays out of both sides (the blind spot);
  - the roads across the next junction, 9–22 m either side (6 a frame);
  - car rays out to 130 m;
  - rays start at the bumper tilted with the car (`mSlope`), and each ray is raised so it clears the road along its whole length (over crests).
- **Speeds are confirmed before use.** An oncoming car first showed its front, then more of its side, so its ends seemed to move apart and it read as standing. A near-end speed is now accepted only when it holds over two half-second windows, and a car counts as measured after 1 s. Until then, a car on the road in the oncoming lanes is assumed to come at 80% of the speed limit, and a car beside or behind is assumed to keep pace.
- **Parked means watched for a second without going anywhere:** its near end never moved away and came nearer by 2.5 m at most. The clock restarts whenever the car is seen moving.
- **Using the oncoming lane:**
  - only past something parked;
  - abandoned (and not tried again for 4 s) if something coming the other way would get there first;
  - a move only counts as "into the oncoming lanes" if it stays there after it ends, so creeping out in small steps no longer slips through;
  - as an emergency escape it scores worse than braking in time, but better than a collision it can't stop for.
  - Overtaking moving cars through the oncoming lane was tried and dropped: in simulation it caused head-ons with oncoming cars it couldn't yet see.
- **Speed:**
  - past something parked, at most about 30 km/h (slowing at 4 m/s²);
  - near a junction with a car across it that may go (moving, or not watched long enough), slow enough to stop before the junction.
- **Moves:** the sharpest S-curve the car can steer limits every move (radius about 11 m, 7 m when nearly stopped); when it would otherwise hit something it can't stop for, harder moves are allowed (up to 1.1 × the cornering grip).
- **Backing up:** when stopped close behind something standing, with no way past but a gap beside it, it reverses straight (rear rays watch for anything within 1.5 m) until a way opens, up to 20 m. It gives up after 6 tries. In a jam with no gap it waits. Physically stuck (throttle without moving) also backs up.
- **Flowed line:** least bending against a pull back to the lane (the line strays over about 15 m, 25 m near junctions), with 0.7 m kept from the measured road edges, which reach 0.85 m past the real lane edges. On test route 2, points within 0.4 m of the road edge went from 4% to 0%.
- **Steering:** pure-pursuit lookahead 4 + 0.5·v (was 0.4·v), for stability at speed.

Two rules were tried and dropped after the bisect below: braking to drop behind a car alongside the planned move, and letting a disastrous current move be replaced by one blocked sooner.

**Native simulator.** The driving harness now also builds as a Linux program (`red4ext/tests/build_linux_sim.sh` → `red4ext/build-linux/drive_sim`, same arguments as `scan_harness.exe … drive`). It uses small stand-ins for the Windows and RED4ext headers in `red4ext/tests/linux/`.
- A run takes 0.3 s against 27 s under Wine, and runs go 24 at a time.
- Single runs are chaotic: floating-point differences between the two builds can flip a run between a touch and a clean pass. So results are counted over many runs:
  - an obstacle every 25 m along both routes, for five obstacle kinds (1,080 runs);
  - 400 random traffic scenes with 3–8 cars: parked (some half in the lane), slow, in the other lane, oncoming, and crossing at the route's junctions without stopping.

| | 0.19 planner | 0.20 |
|---|---|---|
| Single obstacles, clean | 1029/1080 (27 touches, 24 never arrived) | 1045/1080 (16 touches, 20 never arrived) |
| Traffic, touches above 40 km/h | 59 | 21 |
| Touching a crossing car | 72, average 53 km/h | 78, average 30 km/h |
| Touching an oncoming car | 21, average 32 km/h | 11, average 17 km/h |
| Touching a slow car ahead | 28, average 21 km/h | 8, average 15 km/h |
| Touching a car half in the lane (scrapes while passing) | 6, average 40 km/h | 12, average 12 km/h |
| Traffic scenes that never arrived | 13 | 9 |

The simulated crossing cars never stop and are timed to meet the car, so most of those touches can't be avoided without slowing at every junction. The simulator still has artefacts: other cars at a fixed sideways offset fold back on themselves through tight turns, and simulated cars don't avoid each other.

<!-- ANCHOR_END: v014 -->

## A drive on hilly roads (plugin 0.21)

<!-- ANCHOR: v021 -->

The first in-game drive with 0.20 (two Quadras and a Hellhound, trace on) showed three faults the simulator can't reproduce, because its sensor model has no own car and no traffic that drops out of view.

- **Seeing its own nose.** The log showed "car 0.1–0.5 m ahead" (the purple dot on the hood) whenever the car stood or drove on certain slopes, so it braked to a stop, backed up, drove on and stopped again at the same spot. A ray that starts inside a collision shape hits it where it starts, and the rays start only 0.3 m past the front, which was measured once at 0.7 m up (1.9 m for the Quadra, whose nose reaches further lower down). Now:
  - hits within 0.4 m of where their ray starts are ignored and counted (the trace says "N rays started inside something");
  - the front is measured at 0.45, 0.7 and 0.95 m up, and the furthest is used.
- **Junction caution.** Any car not yet measured that was more than 3 m to the side within a junction counted as possibly crossing, so a car just seen in the next lane slowed it to 7 km/h, even when already in the junction. Through a long interchange it crawled for 30 s. Now caution needs a car coming across towards the line (measured), or one not yet measured that is more than 7.5 m to the side (on the roads across, not in the lanes beside). It no longer applies once the car is in the junction.
- **A car ahead seen afresh.** A followed car that the tracker lost and found again counted as standing until measured again (1 s), and the follow rule then asked for 0 km/h at once: full braking at 50 km/h behind a car 25 m ahead going the same speed. Now a new track where the followed car was keeps its speed, falling by at most 6 m/s². For things that really stand still, braking follows the stopping distance at 4 m/s² instead of asking for 0 at once.
- **Debug fan.** The overlay's ray fan now tilts with the car (it was drawn level, so on steep grades it hit the road ahead).

Simulator, against 0.20: single obstacles 1051/1080 clean (12 touches, 17 never arrived) against 1045 (16, 20). Traffic stays within the run-to-run spread: on two sets of 400 scenes, 123 and 110 scenes with a touch, against 115 and 107. A first version of the caution rule, which also skipped cars not yet measured unless it could stop gently, made touches with crossing cars faster (average 43 km/h against 31) and was dropped.

<!-- ANCHOR_END: v021 -->

## Lanes, junctions and overtaking (plugin 0.22)

<!-- ANCHOR: v022 -->

From a drive with 0.21 (Herrera Outlaw, city and highway, trace on):

- **"Junction" lanes everywhere.** The lane map flags long stretches of plain road, highways included, as intersection lanes: on test route 2, 1,266 of 3,288 path points. Real crossings are much rarer. Each path point now also records whether roads cross there: a junction point with a drivable lane piece within 2.6 m running at more than about 45° to the path. Route 2 has 65 such points, and the simulator's old junction spots for crossing cars were mostly on plain road. Rules that meant junctions now use crossings:
  - caution for cars that may cross (so it no longer brakes on the highway for cars on ramps beside it);
  - no swinging out round moving traffic. This used to cost 40 points per metre beyond 1 m, so on the highway it overtook in the middle of the next lane.
- **Keeping to a lane.** The route is a single lane (the A* search over the lane map), and every move was scored by its distance from that lane, so the car always drifted back to it, often the rightmost. Each path point now records the middles of the lanes beside it that go the same way (lane pieces square to the point, running parallel, merged within 1.5 m).
  - The lane to keep is the one the car is in, followed metre by metre, if it goes on for max(150 m, 8 s) or to the end of the route. Otherwise it is the route's lane, which leaves time to move over before a turn or where the lane ends.
  - Moves are scored by their distance from that lane, and lose up to 6 points for ending between lanes.
  - A gentle move is as long as a comfortable lane change needs (up to 150 m), instead of being ruled out at speed. At 90 km/h it had only been able to move 3 m, half way into the next lane.
- **Flowed line in junctions.** In junction lanes the line could use the whole road, so it cut wide turns through the oncoming lanes (up to 3 m in the trace). It now keeps to its own side there too, no more than 1.5 m off its lane.
- **Cars moving sideways.** A car that only gets in the way by moving across (a turning car beside it, whose sideways speed comes partly from the curve) now counts as in the way where the two would meet, not at its current gap. Before, a car 9 m to the side 2 m ahead asked for 0 km/h at once.
- **Far cars not measured yet.** Braking for something standing still uses 4 m/s² once it is measured as standing, and 6 m/s² (later) before that. At 106 km/h it had started braking for an unmeasured car 97 m ahead.

The simulator's crossing cars now set off from real crossings on both routes (`crossings_D1.txt`, `crossings_D2.txt` in the scratch tools). Against 0.21, over 400 scenes on each of two seeds: 89 and 95 scenes with a touch against 86 and 92, touches with crossing cars at the same average speed (31 and 28 km/h), and 1054/1080 clean single obstacles against 1051.

<!-- ANCHOR_END: v022 -->

## Highway lane drops and the shoulder (plugin 0.23)

<!-- ANCHOR: v023 -->

0.22 on the highway zigzagged between lanes, overtook on the shoulder and hit a railing at 130 km/h. From its trace and a simulator reproduction:

- **Moves longer than the look-ahead.** 0.22 let a gentle lane change stretch up to 150 m, but the road check only covered the horizon (up to 100 m), so a move whose end ran off the road passed. The check now covers the whole move, and on past its end for as long as moving back would take plus 30 m. A move that ends off the road is never "on the road", however far off the car already is.
- **Passing shortcut.** Round something in the way, the road only had to be wide enough until just past it, on the assumption that the car then returns to its line. With lane keeping it stays in the new lane, so a lane drop just beyond went unseen (reproduced in the simulator: 29 m driven off the road). The shortcut now only applies round something standing (parked or a wall).
- **Railing thrown away.** Hits more than a car's width outside the mapped road were dropped as "beside the road", even when the car itself was out there. Hits within 2.2 m of the car's own line are now always kept.
- **Lane middles.** Only lanes traffic drives count (not traffic-disabled, no-AI, no-autodrive or GPS-only lanes, which include shoulders and ramp links). The route's own lane always counts, and the nearest middle is taken over three points 20 m apart, so a lane missing at one point doesn't flip the choice. That flicker made the 0 m and +5 m options swap places every second.
- **Lane to keep.** The lane the car is in is kept if it goes on for max(100 m, 5 s); the road check above already makes sure there is room to move back out. Returning to the route's lane waits until a move under way is within 0.5 m of done.

The simulator now also counts metres driven off the mapped road and how many sideways moves of 2 m or more it chose. There are new highway scenes on three 3.6–4.3 km highway routes with 5–12 cars in the lanes either side and its own (slower or about its speed): `hwytraffic.py` and `hwy_score.sh` in the scratch tools. Over 200 scenes: 0.22 had 5 with a touch and 4.4 big moves per drive (up to 35 in one); 0.23 has 3 with a touch, 1.8 per drive (up to 15), and no metres off the road. City scenes and single obstacles are unchanged (94 and 95 scenes with a touch against 89 and 95; 1050/1080 clean).

<!-- ANCHOR_END: v023 -->

## The game's vehicle list, and other cars (plugin 0.24)

<!-- ANCHOR: v024 -->

Two changes: Autodrive+ now asks the game where nearby vehicles are, instead of only guessing from ray hits, and it fits itself to the car it drives.

**Finding vehicles (from the decompiled 2.5 scripts).** A ray hit (`TraceResult`) carries no entity, so it can't say which car it hit. Two script searches can list vehicles:
- `TrafficSystem.FindEntitiesNearPlane(queryEntity, boxPoints, planePoint, planeNormal, planeDistanceMax, maxCount, entities)`. The game uses it to find the crowd around a thrown body (`ComputeNearbyCrowdNPCs`). Autodrive+ asks it, every 0.1 s, for a box from 40 m behind to 120 m ahead, 30 m either side.
- `TargetingSystem.GetTargetParts(player, query, parts)` with `TargetingSet.Complete` and a filter for anything but the player and people, within 100 m, every 0.25 s. This works only if vehicles have targeting components; not yet confirmed in game.

For each `VehicleObject` found (not the car itself), the scripts pass its position, forward and `GetLinearVelocity()`. They also pass `IsVehicleParked()`, the game's own word on parked cars, which replaces "watched standing still for a second", and whether it is a `BikeObject`. The log line ends with how many each search found, so the next drive shows which one works. If neither returns vehicles, nothing changes: the rays still see everything.

**In the plugin.**
- Listed vehicles are matched from list to list by where each should be by now. They become outlines along the path at once, with exact speed along and across the path, with no second of watching first.
- Their size starts as an average car (or bike). It grows to take in any ray hit within 1.5 m of the outline, so a bus or truck gets its real length.
- Ray hits on a listed vehicle no longer make separate outlines. Hits on anything unlisted (parked cars the searches miss, walls) work as before.
- Traffic far from the player may be moved without physics, its velocity reading 0 while it moves. When the game says 0 but the position changed, the speed comes from the movement. Tested in the simulator with every velocity reading 0: 45 of 400 scenes with a touch, against 48 with true velocities.

**Fitting the car.**
- The scripts measure the car's body when Autodrive+ starts: front and rear (rays from 6 m out back to the centre at 0.45, 0.7 and 0.95 m up, the furthest hit) and half its width (rays from 3 m out either side at three heights and three points along it, mirrors included).
- The plugin uses these for the road room, clearances, sensing (what counts as its own body), and the rear and blind-spot rays. Before, it used a fixed half width of 1.0 m and a rear equal to the front.
- Handling, as learned while driving, never more than the settings allow:
  - grip: the sideways acceleration where it slid, or ran wide with the wheel at its limit, above 4 m/s²;
  - braking: the learned full-brake deceleration, for the emergency and follow distances and the speed plan;
  - speeding up: 0.8 × the learned full-throttle acceleration (at most 2 m/s²) in the clearance check;
  - turning: the comfortable S-curve shrinks for a car that turns less than the default.
  - The speed plan is redone when grip or braking changes by 0.25 m/s².

**Planner fixes found on the way** (the exact positions showed them):
- Steering into a car alongside cost nothing (it was "avoided by dropping back", which nothing did). It now costs as much as a crash it can't stop for.
- Past a moving car, the lane moved into has to go on until it is well past (gap plus 8 m, at the speed difference).
- Running off the road far ahead, past the end of a move, is a lesser penalty (150) than now (500). When its side of the road runs out and the way back is blocked by a car alongside, it drops back behind that car.
- A move that has gone off the road can always give way to one that hasn't. Before, it couldn't if the other move was blocked as soon; that kept it riding the shoulder behind a slow car on a two-lane road.
- The thing in the way was found by its ray-point index, which every listed vehicle shares (-1).

**Simulator.** The batch scripts now live in the repo (`red4ext/tests/sim/`; the session copies were wiped once). The simulator takes:
- `BCAI_CAR=hellhound` (5.8 × 2.4 m, grip 10 m/s², 75% acceleration, turns 30% less, brakes 6.5 m/s²) or `maimai` (3.1 × 1.55 m, grip 6.5 m/s², half the acceleration, top speed 120 km/h, turns 50% more);
- other cars' sizes (`BCAI_CARS` fields 6 and 7);
- a vehicle list (`BCAI_LIST=1`, or `still` for velocities reading 0).

Off-road metres are judged against each car's real width. Results, 0.23 → 0.24 (rays only) → 0.24 with the vehicle list:

| | Default car | Hellhound-like | Mai Mai-like |
|---|---|---|---|
| Single obstacles, clean of 1,080 | 1055 → 1049 → 1057 | 1043 → 1044 → 1053 | 1058 → 1058 → 1064 |
| City traffic, scenes with a touch (two seeds, 400 each) | 89/90 → 92/90 → 48/63 | 108/111 → 101/104 → 54/72 | 77/75 → 72/82 → 37/37 |
| City traffic, metres off the road | 109/274 → 203/200 → 53/105 | 396/576 → 278/332 → 206/209 | 174/133 → 120/111 → 32/29 |
| Highway, scenes with a touch of 200 | 3 → 4 → 2 | 6 → 7 → 7 | 1 → 2 → 1 |
| Highway, big sideways moves per drive | 1.8 → 2.1 → 0.8 | 2.3 → 1.3 → 0.7 | 0.8 → 0.8 → 0.4 |

The Hellhound-like car's highway off-road metres (about 270 in 200 drives) are 6 m per drive at one tight bend on one route, where a 2.4 m-wide car that turns wide doesn't fit inside the mapped room.

<!-- ANCHOR_END: v024 -->

## A fairer simulator, overtaking and braking (plugin 0.25)

<!-- ANCHOR: v025 -->

**Simulator realism.** Three faults in the simulated traffic made touches that couldn't happen in the game, for every car:
- Simulated cars never slowed down, so at tight bends they drove into the back of this car. Cars on the route now follow it with a plain car-following model when it is ahead of them in their lane (`BCAI_BLIND=1` turns that off).
- Cars past the end of the route piled up at its last point, where this car stops. They now leave the route there.
- Route D1 passes back near its own start. Cars placed on that far part of the route drove through this car at the start. A car more than 60 m from this car along the route but within 30 m of it is now hidden: in the game it would be on that other road.

Those faults made 20–35% of the city touches and most of the Hellhound's highway touches. With them fixed, 0.24 scores 36/43 (default), 38/46 (Hellhound-like) and 22/17 (Mai Mai-like) city scenes with a touch, and 0, 3 and 0 highway scenes. The big car's apparent gap was mostly the faults.

**Planner fixes** (found by replaying the Hellhound-like car's failures):
- A pass past a moving car needs road for the pass and then for moving back (before, only for the pass). It is planned with the car's own acceleration, so a slow car starts fewer passes it can't finish.
- Beside a car, the penalty grows with how much closer a move comes (30 per metre), so it no longer swings toward a car alongside when every way counts as touching.
- When its side of the road runs out ahead (or turns into the oncoming lanes) and the way back is blocked by a car going its way, beside it or just ahead, it drops back behind that car. Running out of road is then the lesser penalty, if there is room to slow down first.
- The same move again, from where the car is now, may replace the move under way when that one gets past and the move under way doesn't. Before, a car stopped behind a parked car kept a swing-out that started too late and sat there.
- After backing up, the move under way starts where the car is. Before, it still started where the car had stopped, so backing up gained no room to steer out.
- The rule against switching to a way that is blocked sooner doesn't apply when what blocks the current way is coming the other way. A wide car going round a car parked half in its lane, partly in the oncoming lane, now pulls back in behind the parked car for oncoming traffic.
- Braking for something ahead adds the deceleration it needs (above 1 m/s²) to the speed controller, as the speed plan already did for bends. Before, the controller only answered the speed error, so it ran about 5 m/s over the braking curve and stopped late.

**Results** (0.24 → 0.25 on the corrected simulator, with the vehicle list; `tests/sim/battery.sh`):

| | Default car | Hellhound-like | Mai Mai-like |
|---|---|---|---|
| Single obstacles, clean of 1,080 | 1060 → 1062 | 1057 → 1061 | 1066 → 1066 |
| City traffic, scenes with a touch (two seeds, 400 each) | 36/43 → 24/19 | 38/46 → 20/19 | 22/17 → 12/6 |
| City traffic, metres off the road | 53/78 → 39/12 | 206/209 → 160/156 | 32/21 → 67/22 |
| Highway, scenes with a touch of 200 | 0 → 0 | 3 → 0 | 0 → 0 |
| Average speed, city / highway (km/h) | 48.9 / 80.2 → 48.6 / 79.7 | 45.8 / 74.4 → 45.4 / 73.9 | 42.1 / 63.7 → 41.7 / 63.7 |

The Mai Mai-like car's extra off-road metres on seed 7 come from one scene (T0069) that fails either way: 0.24 loses the route there, 0.25 gets stuck after 46 m off the road.

The Hellhound-like car's highway off-road metres are almost all at one spot on route H3: the game's route makes a U-turn on the highway (radius about 3 m), which no wide car can follow. That is a route problem (the game's navpath), not a driving one.

<!-- ANCHOR_END: v025 -->

## Driving styles, keeping right and not changing its mind (plugin 0.26)

<!-- ANCHOR: v026 -->

From the first drives in the game (read from the Autodrive+ lines in `BetterCarAI.log`):
- **Flip-flopping:** the lane it headed for stepped back and forth (+5 → +2.5 → +1 → 0 → +2 m) within a few seconds. Small changes in how clear each lane looked outweighed the costs of switching.
- **Middle lane:** on roads with three lanes or more it kept the route's own lane, which the game's routes often put in the middle.
- **"Overtaking on single-lane roads":** it stopped behind a traffic car standing in its lane and waited until the player took over. Only cars the game marks as parked counted as standing, so it never went round.

**Styles** (`DriverSettings.style`, set with `BCAI_DriverStyle`; each style has its own speed settings in CET):
- *City* (1.3× traffic speed, top 110 km/h, 0.6 g, 2 s gap) keeps right whenever more than one lane goes its way, and goes round only what stands still (or a car crawling at under a third of its speed). A car stopped in its lane counts as standing after 20 s. It gives way at junctions where a car stands across the road, waiting to pull out.
- *Aggressive* (the earlier defaults: 2.2×, 180 km/h, 0.8 g, 1.5 s) keeps right only where there are three lanes or more. On two-lane roads, keeping right put it next to the side streets, and the simulator's crossing cars, which pull out at the last moment, hit it more often. A stopped car counts as standing after 5 s. It also overtakes slow cars on the other side of the road: only when no roads cross on the way, the road ahead is straight enough to see along, nothing is seen coming for the length and time of the pass and the way back, and the pass is short enough that a car coming at the speed limit from beyond what the game lists (about 180 m; the vehicle search now reaches 200 m ahead) would still be far enough off.
- *Madman* (3×, 250 km/h, 0.95 g, 0.8 s) keeps whatever lane it is in, overtakes whenever nothing is seen coming, waits 1.5 s before going round a stopped car, even one in a queue, goes round things at up to about 40 km/h and slows at junctions only for cars seen crossing.

**Not standing in a queue:** a stopped car with another stopped car just in front of it, or a junction where roads cross within 30 m past it, is queueing. City and aggressive wait behind it.

**Not changing its mind** (all styles):
- After a lane change, the next one waits 3 s (city), 2 s (aggressive) or 1 s (madman). Getting out of trouble doesn't wait: something too close to stop for, the road running out, or the oncoming lanes without a pass under way.
- A move needs a clearly better score (+8, +5 or +3).
- It only moves into a lane that has looked clear for a moment: nothing it would run into within 2 s, kept per half-metre target (1.5, 1 or 0.5 s).
- The simulator counts *changes of mind*: a move of 1 m or more undone within 3 s.

**Safety fixes found on the way:**
- *Mirror check:* no moving into a lane in front of a faster car too close behind it (closer than 3 m + 0.5 s + what it needs to brake to this car's speed at 3 m/s²). Before, a faster car behind was assumed to just pass, but the game's traffic brakes for a car that cuts in.
- *A lane that leaves the road* (merging away, or out of the road's room) no longer counts as one that goes on. The car looks ahead for its lane ending 150 m or 6 s ahead (was 100 m or 5 s), and moving into a lane needs it to go on for 200 m (or 1.5× that). When its lane is ending and a car beside it blocks the way over, it starts dropping back at once, by enough to be behind that car before the end (2–8 m/s slower; before, always 2 m/s, only once the road's end showed up within about 90 m).
- *Oncoming traffic before a pass:* going round something (or on past the next car once out) starts only when nothing seen coming would get there before the car is past and back. Before, standing still it only looked 35 m ahead, and it pulled out with an oncoming car 70 m away.
- *Turning back for an oncoming car,* it keeps at least walking pace until it is back on its own side. It can't move sideways when stopped, so stopping half in the oncoming lane guaranteed the touch.
- A car standing *across* the road at a junction (listed by the game, pointing across) may pull out: city and aggressive approach so they could stop. Aggressive assumes it can brake harder (7 m/s²), so it slows less.

**Results** (0.25 with the same simulator and settings → 0.26; city scenes with a touch on seeds 7/11, 400 each; changes of mind per city drive):

| Style | Car | City touches | Changes of mind | City km/h | Highway touches (of 200) |
|---|---|---|---|---|---|
| Aggressive (each scene's settings) | default | 24/19 → 15/14 | 0.76 → 0.46 | 48 → 47 | 0 → 0 |
| | Hellhound-like | 20/19 → 17/15 | 0.80 → 0.40 | 45 → 45 | 0 → 0 |
| | Mai Mai-like | 12/6 → 2/3 | 0.80 → 0.40 | 42 → 41 | 0 → 0 |
| City | default | 14/16 → 8/12 | 0.62 → 0.24 | 46 → 45 | 0 → 0 |
| | Hellhound-like | 9/23 → 16/19 | 0.58 → 0.35 | 43 → 43 | 2 → 0 |
| | Mai Mai-like | 9/6 → 7/8 | 0.63 → 0.21 | 41 → 41 | 1 → 0 |
| Madman | default | 21/22 → 23/27 | 0.90 → 0.43 | 50 → 61 | 0 → 0 |
| | Hellhound-like | 34/23 → 38/38 | 1.03 → 0.48 | 46 → 56 | 2 → 1 |
| | Mai Mai-like | 9/12 → 10/13 | 1.61 → 0.96 | 42 → 49 | 0 → 1 |

Changes of mind on the highway stay at or below 0.04 per drive, except a fixed one at H3's U-turn. Highway sideways moves of 2 m or more rise from about 0.7 to 4–7 per drive in city and aggressive: keeping right means moving back after every pass.

Left over:
- The Hellhound-like car in the city style: off-road went from 116 to 258 m over the two seeds. In some scenes (T0053) it stops mid swing-out behind a car parked half in its lane and waits with its side in the oncoming lane; it would have to creep or back into its own lane while steering.
- The Mai Mai-like car in the madman style runs 19 m wide at one bend on D1 in most scenes, before it has learned that it has far less grip than the 0.95 g madman assumes. The same happens in 0.25 with those settings. In the game the grip it learns is kept for the next drive.
- No traffic lights, in any style.

**Overlay:** airborne now comes from the game's `VehicleObject.IsInAir()`. The ground under the car is found with a ray against the world's static meshes, then against the `Terrain` collision group (dirt roads and open ground aren't static meshes, which is why they read as airborne before). It is named from the hit's physics material: road, dirt, grass or other. The plugin's `BCAI_DriverHud` reports what it is doing for the panel, and `BCAI_DriverAhead` puts the planned speed in W so the line can be coloured.

<!-- ANCHOR_END: v026 -->

## Code layout, the decision log, NPC reactions (plugin 0.27)

<!-- ANCHOR: v027 -->

**Driver split.** `Driver.cpp` (3,300 lines, one class) is now `Driver.hpp` (shared types, the style table `kStyles`, the class) and five files: `Driver.cpp` (interface, Step split into Feel / KeepOnRoute / Steer / TargetSpeed / Pedals / Unstick), `DriverRoute.cpp` (A*, the line, room, speed plan), `DriverSense.cpp` (rays, vehicle list, bodies), `DriverLanes.cpp` (Avoid split into Survey / JudgePassing / Score / Options / Choose / Commit / WatchJunction / FindDoor), `DriverReport.cpp` (overlay, log, events). Every style-dependent number lives in `kStyles`. The split was checked to drive exactly as before: `tests/sim/same.sh` compares 1,620 drives (all cars and styles) by a hash of every control output, with `-ffp-contract=off` builds (else moving code between files changes which multiply-adds get fused). 1,620 of 1,620 matched, and again after adding the event log.

**Decision log.** `BCAI_DriverEvents()` returns what Autodrive+ decided since last asked; CET writes them as `Autodrive+ | <s into the drive> <km/h> <m along the route> <what>`, shows the latest on the overlay panel, and the simulator prints them with `BCAI_EVENTS=1`. Moves of 1 m or more (from which lane to which, why, what was in the way of the line it had, the two scores), passing (begun with what and why, waiting for oncoming traffic, done, given up), what holds the speed down (once it has for 0.7 s), hard braking, losing grip and the grip learned, leaving the mapped road (after 3 m) and coming back, backing up and why it stopped, replanning, the style and settings, how the drive ended. `tools/drive_log.py` sums a log up per drive.

**Bugs the log found** (both since 0.26):
- *Gentle moves dropped by rounding.* A gentle move is made exactly as long as a comfortable lane change needs, then checked against that same limit; rounding made some 1 ulp too sharp, so from frame to frame some lanes weren't among the options at all. The "looked clear for 1.5 s" timer of those lanes reset every time, so lane changes at speed (and swinging out round a parked car) were put off until the car was close. The check now has 0.1% slack. In T0053 (Hellhound, city) the swing round a parked car now starts 69 m before it instead of 14 m.
- *Never clear to come back.* While out in the oncoming lanes, the move back to its own lane runs through them, and "home is clear" required no metres there, so the timer for its own lane reset every frame; coming back relied on a nearby target happening to win. In T0083 it drove 400 m in the oncoming lane after going round a parked car and ran into a car there.

**Other changes:**
- Waiting for the oncoming lanes while out, it may now go back in behind what blocks it (a rule against switching to a way blocked as soon blocked that); it may use quicker moves back (as when urgent); and a move that would leave it stopped half in the oncoming lanes scores like one ending there.
- A row of things standing in the way is gone round as one when the gaps are too short to tuck back in (the car's length and a move out and back), using the game's list of parked cars up to 200 m ahead, for whether the oncoming lanes are clear for long enough.
- When the lane nearest the map pin can't be reached (seen in game: a GPS-only junction link in a traffic-disabled area, fed only by another closed lane; 14,500 lanes searched each time), it routes to the nearest of the next dozen lanes near the pin that can be reached.

**Results** (0.26 → 0.27, same simulator; touches city seeds 7+11 summed over the three cars; metres off the road in the city):

| Style | City-style touches | Aggressive touches | Madman touches | Off-road m in city scenes | Changes of mind (default car, s7/s11) |
|---|---|---|---|---|---|
| City | 70 → 70 | | | 396 → 271 | 0.27/0.20 → 0.25/0.18 |
| Aggressive | | 81 → 82 | | 610 → 562 | 0.51/0.45 → 0.39/0.30 |
| Madman | | | 149 → 147 | 7,055 → 7,024 | 0.46/0.39 → 0.38/0.32 |

Single obstacles: as clean or cleaner except madman Hellhound (7 → 10 of 1,080). Highway: three new touches, all at H3's U-turn (the route doubles back there), where the Hellhound now also makes one more move, seen as a change of mind. T0053 still ends with the oncoming car (which never brakes in the simulator) running into the Hellhound stopped about 1 m out: the gap between the two parked cars is too short for that car to get fully back in at the speed it was going.

**NPC reactions to a tap** (2.5 scripts):
- `VehicleObject.HandleTrafficBump` (traffic cars only): above 6.5 m/s a roll to knock the driver out; else a voice line and a honk. Already handled.
- `VehicleComponent.OnGridDestruction`, on **any** car that dents: a dent smaller than 0.03 calls `CrowdIllegalAction` to everyone within 10 m, a bigger one `VehicleHit` within 5 m (source: the dented car). `CrowdIllegalAction` wasn't among the stims the mod filtered, so a tap still sent tapped drivers and people nearby into panic. `damageMultiplier > 0` also makes a hit event, and a reaction within 2 s of a hit is high-priority panic.
- Now: vehicle-sourced `CrowdIllegalAction`/`VehicleHit` are ignored by drivers and by people on foot unless either car had a hard impact (velocity change above the panic setting, from `OnVehicleBumpEvent` on any car, or a dent of 0.1 or more), the driver has lost patience, the player is in combat, or police are involved. Running someone over still reaches people (that stim comes from the player, not a car).
- Taps escalate: honk, then honk and flash (`VehicleComponent.HonkAndFlash`, reached through an added method), and at the 4th tap within 30 s the driver gets the `Bump` stim from the player (what vanilla sends a driver bumped too often) and its reactions go through for 10 s: panic driving, getting out, or with aggressive crowds on, combat (the game's own checks: within 5 m of the player, no heat, not in combat).
- Panic driving (`BaseDrivingParameters.panic`: `topSpeed`, `boostMaxSpeed`, `curvatureLookupRange`) is tuned in CET: 0.85× speed, 1.5× bend look-up, so fleeing drivers crash less. The vanilla values are written to the log once. The dent scale (0.1 = a crash) is a guess; `Traffic |` lines log the dent sizes to tune it.

<!-- ANCHOR_END: v027 -->

## Navigation: our own routes to the map destination (plugin 0.28)

<!-- ANCHOR: v028 -->

**The game's GPS.** The route on the minimap and world map comes from `gamegpsGPSSystem` (native, ticked as `[U]/GPSSystem/Tick`, settings in `gamegpsSettings`). The minimap's widgets for it (`m_gpsPlayerTrackedPathWidget`, `m_gpsQuestPathWidget`, `m_gpsDelamainPathWidget`) are native too. No script function returns or sets the route. Scripts can only reach the destination:
- `MappinSystem.GetManuallyTrackedMappinID()`;
- `AutoDriveSystem.GetAutodriveDestinationMappinID()`, `GetAutodriveDestinationType()` (None / PlayerTracked / Quest) and `GetAutodriveDestination()`;
- `IMappin.GetWorldPosition()`;
- `WorldMapMenuGameController.GetTravelDistance()`.

The GPS plans over the traffic lane table: every `worldTrafficLanePersistent` has `playerGPSInfo` at +0x84, which is {`subGraphId`, `stronglyConnectedComponentId`}, and the `gpsOnly` lanes (flag 512) are junction links meant only for it. `CGameEngine::Get` (hash 0x97F209D6) is in the 2.5 address map, so the plugin can get the GPS system by type (`GameInstance::GetSystem`). `BCAI_ScanGPS` scans it for lane routes the way the path scans found the autodrive route (debug window, *Research* tab; set a pin first). Finding where it keeps the route it draws is the next step towards putting our route on the minimap.

**Heights in the lane map.** All 21,226 links between drivable lanes join lanes at the same height: the largest step is 1.7 m, on a noAutodrive path. So a route can't jump between stacked roads partway; a GPS line on the wrong level must come from where it starts or ends. Lane speed limits (`maxSpeed`) hardly differ: 15 on 11,899 town lanes and 17 on 2,700 highway lanes, plus a few at 8–14.

**The navigator** (`Navigator.cpp`; Autodrive+ and the route guide both use it): A* over lanes as before, but:
- *Time, not distance.* Each lane costs its length at the pace Autodrive+ drives it: 1.4× the limit in town, 1.6× on highways, at most 11 m/s in junction lanes. The old flag factors stay (6× for noAutodrive/noAI lanes, 1.1× for gpsOnly links), but 1.3× for traffic-disabled lanes now applies only to junction links: whole Badlands roads have traffic disabled and are as quick as any.
- *Link costs:*
  - entering a junction: 3 s;
  - turning right: 1.5 s;
  - turning left: up to 5 s, scaled by the angle;
  - a hairpin (over 110°): 3 s more;
  - a U-turn (over 160°): 45 s;
  - a yield lane: 1.5 s.
  A U-turn at 143° first made a pair of sharp rights count as one.
- *The end of the route.* Every drivable lane within 120 m of the pin (300 m if none) is a possible end, and the search finishes at whichever is cheapest including what ending there costs:
  - 0.6 s per metre from the pin for the first 40 m, then 2 s per metre;
  - another level: 45 s plus 4 s per metre beyond the tolerance. A lane more than 1.5 m above the pin, or 4 m below it, is another level, since a pin often sits on a pavement or a ground floor above its road;
  - a highway: 25 s, unless the pin is on it;
  - a junction lane: 4 s;
  - a noAutodrive lane: 10 s.

  A pin far from every road in height (more than 6 m from all of them: a roof, or a pin without a real height) counts as on the lowest road within 10 m of the nearest one. The estimate (straight distance at 27 m/s) stays below the real cost, since ending away from the pin costs more per metre than driving there.
- *Turns* are listed per junction (one or more junction lanes) with their angle (`Manoeuvre`), for the turn hints and the log.

**Results.**
- *Planned trips.* Over 1,500 random trips (`drive_sim nav`, seeds 1–3, destinations up to 15 m off a non-highway road at that road's height plus up to 3 m), against the 0.27 planner:
  - the planner's time: 257 → 229 s;
  - distance: 3,075 → 2,950 m;
  - junctions per trip: 19.1 → 16.4;
  - left turns per trip: 3.0 → 2.1;
  - U-turns per trip: 0.039 → 0.029;
  - plan time: about 0.4 → 1 ms.
  - Where another road runs within 12 m of the destination more than 6 m above or below it, the old planner ended on the wrong level in 47 of 240 trips and the navigator in none. Ends are 8–9 m from the pin on average instead of 4–5: the old one took the nearest lane even when it ran the wrong way and needed a loop round the block to reach.
- *Driven trips.* Over 720 random trips (`navdrive.sh`, three seeds), the simulated car arrived after 159 s instead of 176 s on average (8–12% sooner per seed). 364 trips were more than 5 s quicker and 57 more than 5 s slower. The slower ones mostly took a longer highway that was no faster than the town road.
- *Pins without a height* (`BCAI_NAV_Z=none`): ends 8–9 m from the pin. On the wrong level against the generated road in 10–18 of 80–90 trips, but with no height that road is a guess anyway (the navigator picks the street).

**The driving battery.**
- *City scenes.* The navigator takes a different route for D2 (2,940 m instead of 3,277), and its city scenes are laid out for the old one: crossing cars set off at the old route's junctions, and "other lane" cars assume its multi-lane road. On the new D2, seed 7 had 17 drives with a touch instead of 8, mostly crossing cars starting mid-block. So the city scenes now keep the 0.27 routes (`BCAI_ROUTES=0.27`, set by `score.sh` and `traffic_score.sh`). On those routes 0.28 drives exactly as the 0.27 planner build does: all 400 result lines of seed 7 are identical.
- *Highway scenes* (navigator routes; H3 no longer doubles back), touches, average speed and off-road metres, old planner → navigator:

  | Car | Touches | Average speed | Off-road |
  |---|---|---|---|
  | Default | 2 → 0 | 82 → 87 km/h | 0 → 0 m |
  | Hellhound | 0 → 2 | 75 → 80 km/h | 330 → 0 m |
  | Maimai | 0 → 0 | 64 → 68 km/h | 0 → 0 m |

  Both Hellhound touches come at the end of the route, from a simulated car running through the arriving car.

**Simulator fix.** The drive harness took the start's and the goal's height from the nearest lane *point* within 20 m, which could be on a road above or below. It now projects onto the nearest lane going the start's way (for the goal, any lane). This fixed 14 of 19 "no lane under the car" failures in random trips. It also made one trip look twice as slow, because the goal had been put on the level above.

**In game (0.28).**
- *Autodrive+.* It now plans to the pin's own position (`BCAISettings.NavDestination`: the autodrive destination's mappin, else the manually tracked one), not to `GetAutodriveDestination()`, which may already have been moved to a road by the game. Both are written to the log on start.
- *Route guide (CET `nav.lua`).*
  - It plans with `BCAI_NavPlan` when the destination appears or moves, when you get in a car (starting from the car's lane, going its way), or after 1.5 s more than 25 m off the route.
  - It draws the next 300 m of the route as a yellow line on the road (projected by `BCAIDebug.ProjectPolyline`, without the debug overlay), with the next turn at the top of the screen.
  - It writes `Route |` lines to the log.
- *Untested in game:* all of it. In particular, which height a custom map pin really has, and how often the game's autodrive point differs from the pin.

<!-- ANCHOR_END: v028 -->

## Pedals, the game's autodrive HUD, trucks and walkways (plugin 0.29)

<!-- ANCHOR: v029 -->

From the user's notes on 0.28.

**Pedals.** The debug HUD showed the brake going to 100% for half a second and then off again, and the throttle jumping. The log showed the cause, following a car at 63 km/h about 40 m back:
- The target speed flipped between about 60 and 97 km/h every second or two.
- The following rule used "the lead car's speed, plus 0.4 m/s per metre short of the wanted gap" inside the gap. Outside it used "the speed it could still stop from". Those two disagree where they hand over, and the wanted gap moves with the car's own speed.
- On top of that, the slowing needed to match the lead car's speed was *added* to the speed controller's own braking. Together they reached the −9 m/s² limit, which is a full brake.

0.29 changes:
- One gap rule on both sides of the wanted gap (0.4 m/s per metre short of it, 0.7 per metre past it).
- The harder of the two braking requests, not their sum.
- The wanted acceleration changes at 5 m/s³ (12 when it wants more than 3 m/s² of slowing, 25 only when it must stop hard).
- The throttle eases on (full in about 0.7 s) and off at 4/s.
- Slowing of up to 0.6 m/s² beyond coasting is left to coasting, so the brake doesn't flick on to hold a speed.

Each simulated drive now reports `pedals:`: travel per minute, seconds of hard braking per minute, and quick flips (a pedal held under a second before the other).

The battery, 0.28 → 0.29 (city: seeds 7 and 11 together):

| Car | City touches | Quick flips per city drive | City pedal travel | Hard brake | Highway touches | Obstacle touches |
|---|---|---|---|---|---|---|
| Default | 30 → 25 | 1.2 → 0.3 | 14 → 10 per min | −40% | 0 → 1 | 2 → 1 |
| Hellhound | 32 → 41 | 2.7 → 0.9 | 15 → 9 per min | −40% | 2 → 0 | 4 → 2 |
| Maimai | 7 → 4 | 0.5 → 0.15 | 12 → 7 per min | −35% | 0 → 0 | 0 → 0 |

- City touches in all: 69 → 70.
- City average speed is 1–3 km/h lower, and the highway 1–2 km/h.
- The Hellhound's off-road metres in the city dropped from 349 to 128.
- Its extra touches are mostly oncoming cars at about 10 km/h, at one tight right turn on D1 (about 825 m in). There it runs a metre wide on full lock, and the simulated oncoming car doesn't brake. 0.28 swings wide there too; whether a car is there depends on when it arrives.
- Raising the gap rule's gain past the wanted gap from 0.4 to 0.7 won back 2 km/h on the highway.
- The throttle ramp turned out not to cost speed: 2.5/s gave the same as 1.5/s.

**The game's autodrive HUD.** The wheel icon on the minimap (`MinimapContainerController.OnAutoDriveStateChanged`) and the side panel (`AutoDriveController`) only listen to the `UI_AutodriveData` blackboard:
- `AutoDriveEnabled`;
- `FreeRoamEnabled`;
- `DestinationRemainingLength` and `DestinationRemainingTime`.

AI drive already filled these in; Autodrive+ now does too (`ShowOwnDriverHud`, `RefreshOwnDriverHud`).

Setting `AutoDriveEnabled` also switches the player to the autodrive input context (`VehicleAutodriveContextDecisions`). The autodrive HUD listens to Accelerate, Decelerate and TurnX there, so taking over still works. The autodrive button is also watched directly (`HoldAutodrive`, hold complete). If the flag is cleared while it drives (the game's own reasons: combat, weapons, damage, quest locks), control goes back to the player.

**The car's records.** Scripts can read a car's physics from its TweakDB records:
- `Vehicle_Record.VehDriveModelData()`: `Total_mass`, `MaxWheelTurnDeg`, `BrakingFrictionFactor`, and per axle `WheelSetup().FrontPreset()/BackPreset()` with `TireFrictionCoef`, `FrictionMulLateral/Longitudinal` and `MaxBrakingTorque`;
- `VehEngineData()`: `EngineMaxTorque`, gears, max RPM;
- `VehWheelDimensionsSetup()`: tyre radius;
- `Destruction().VehicleDimensions()`: the body's size;
- `VehicleObject.GetTotalMass()`.

`BCAISettings.CarData` reads them for the debug panel and the log (`Autodrive+: the car's records: ...`). How tyre friction maps to grip in g isn't known yet, so Autodrive+ doesn't use these figures yet; the driving test is for that.

**Large vehicles.**
- Autodrive+ measured its own car's length with rays and ignored hits past 4 m from the centre, so a truck came out about car-sized. The rays now reach as far as the record's size says, falling back to that size. The plugin accepts bodies up to 12 m from the centre.
- Other vehicles in the game's list carry their record size (in the W of the forward and velocity vectors), so a truck is seen whole at once, not grown ray hit by ray hit.
- The speed plan also caps each bend's speed by how tight the car can turn at that speed: the learned steering response, the wheel's limit at speed, 10% held back.
- In the simulator (`BCAI_CAR=truck`: 12 m × 2.6 m, steering 0.12·steer², grip 0.76 g, city seed 7):
  - 0.28 left 160 of 400 drives unfinished, 0.29 left 15.
  - Most of the 0.28 failures were the harness: it read the road height from the path 6 m ahead of the car's centre, which is inside a 12 m truck, so the height never updated and the car "left" the road on the first ramp.
  - The truck touches another car in 65 of 400 city scenes (half of them oncoming cars, at about 7 km/h), against 9–16 for the default car. It doesn't swing wide or wait for a gap in oncoming traffic before a tight turn, and the simulated oncoming cars don't brake for a body in their lane.

**Lane changes for the route** start about 9 s ahead (150 m at least; before, 6 s), plus 40% for each further lane to cross. A comfortable lane change may now take up to 5 s of driving (before, at most 150 m).

**Walkway pins.**
- A quest pin at (−416, 715, 115) sits on a pavement lane of a megabuilding walkway. The nearest drivable roads are a highway at 75 m (41 m away) and streets at 46 m (52 m away). 0.28 treated its height as unknown and ended on the street 23 m from it across the ground, 69 m below. The user saw the route lead to "the edge of the bridge".
- Pavement lanes link to each other, and that walkway joins street-level pavements about 145 m away. So for a pin on a pavement with no road on its level within 25 m, the navigator now walks the pavement network from the pin, nearest first, up to 600 m and stopping 150 m past the first road found. It offers the roads beside those pavements as ends, costed at a second a metre of walk.
- That quest now ends 38 m from the pin, a 144 m walk along the pavements (69 m up).
- On 300 random pins on raised pavements (`navwalk`), 102 end a walk away (102 m on average, at most 427 m); the rest have no pavement link down within 600 m and keep the old ends.
- Ordinary trips are unchanged: identical results on 500 random trips.
- Counting the cheapest end in the A* estimate keeps a walk's cost from widening the search. Plans to raised pins take about 30 ms.

**The route guide.**
- On foot it plans again only after you've walked 80 m from where it last planned. The 0.28 log had a plan every 2 s while walking, because on foot you are always more than 25 m off the route.
- After a dead-end start lane (the log had "no route from lane 5282 … (1 lanes searched)"), it tries the next nearest lanes. Autodrive+ does the same.
- The line is a ribbon about 0.35 m wide as seen (from the camera depth per segment), starting 9 m ahead of the car, fading towards 300 m, with a chevron every 24 m. It is hidden while the debug overlay draws Autodrive+'s own path.

**The driving test** (*Debug tools > Driving test*, `drivetest.lua`, `BCAI_NavRandom`, `tools/drive_test.py`): see the README. It records the player and Autodrive+ on the same random route, to measure the grip the car really has; the user finds Autodrive+ far too careful in bends.

**Style picker:** the autodrive button now starts with the highlighted style instead of cancelling.

**Untested in game (0.29):**
- the HUD flag and input context;
- the car records' values, especially `VehicleDimensions` (axis order assumed: the larger of X and Y is the length);
- the teleport, the driving test's recording, and `IsSkidding(0.3)`;
- the ribbon line.

<!-- ANCHOR_END: v029 -->

## Grip, steering reach, the driving test and turning round (plugin 0.30)

<!-- ANCHOR: v030 -->

**What the first driving tests measured** (2026-10-25: Quadra Type-66 Avenger and GT, Aerondight; the player's drives):
- Sideways acceleration (from how the velocity turns, cross-checked from the driven line's curvature) reached about 2 g, e.g. a 20 m radius at 72 km/h.
- It grows with the body's slip angle (the car pointing into the bend): about 0.77 g at 3–6°, 1.2 g at 6–9°, 1.4–1.6 g at 9–12°, 1.7–2.0 g at 12–15°, then falls off past 20–30°. The game's cars corner hardest at 8–15° with the tyres squealing, and stay controllable.
- At low speed they turned on a 2–5 m radius (Autodrive+ itself managed 4 m), while the learned steering fit (about 0.15·steer²) said about 7 m at full lock.
- `IsSkidding(0.3)` is true most of the time, even under 3° of slip: no use as a slide signal.
- `Destruction().VehicleDimensions()` gives 3 × 1 for cars 5.5 m long: not the body's size.
- All three cars' records: tyre friction 1.0, lateral multiplier 0.90–0.93. Too little spread yet to map records to grip.

**Why Autodrive+ was afraid of corners (0.29):**
- Over 0.1 rad (5.7°) of slip counted as sliding: it slowed ("losing grip") and learned the grip down to whatever it cornered at, often 0.5–0.8 g, below the style's.
- 0.29's steering-reach cap took that 7 m figure at its word, so a city corner of about 7 m radius fell to the cap's floor of 3 m/s (11 km/h).

**0.30:**
- Sliding is slip well past what this car's cornering needs: the slip × the car's slip stiffness (sideways acceleration per radian while gripping, learned between 2° and 9°; 86 for the sports cars) more than 1.5 × the cornering + 2 m/s², for 0.15 s; or over 0.45 rad whatever it corners at. A low-grip car is caught at a few degrees; a 2 g car at 13° isn't sliding.
- Grip: a car new to Autodrive+ corners at up to 1 g (`kStartGrip`), rising 0.5 m/s² a second while it corners at 80% or more of that without sliding (0.1 past where it last slid), up to the style's. Where it slides, it drops to what it held there. Rising on gentle bends (half the grip) let the low-grip car climb to 1.17 g and meet a 1.3 g bend at 100 km/h.
- Styles: City 0.7 g, Aggressive 1.3 g, Madman 1.7 g (settings version 11 moves saved values to these).
- Steering reach: the tightest turn really seen (curvature × SpeedTurn ÷ steer^p with the wheel over half way, while gripping; rising over a second) replaces the fit's full lock when tighter. The cap applies to every vehicle again; removing it for cars made the simulated Hellhound (wide-turning) run 1 m wide at junctions.
- Lane changes plan with the cornering grip but at most 9.3 m/s² (`kMoveLat`), as 0.29's Madman.
- Two attempts at pursuing the path along the velocity instead of the body (raw slip, then slip smoothed over 0.4 s) made the car weave and lose the route at 90 km/h in the simulator; the slip it reads is changed by the steering itself. Not used.
- The debug trail colours by slip angle: green under 8°, yellow 8–17°, red past 17°.

**Simulator.** The car now has the game's body slip (`tan(lat/86)`) and the tests' grip: default 19 m/s², Hellhound 16, Maimai 13, truck 7.5, and `slippery` (9, below the styles). Batteries (city seeds 7 and 11, highway; 0.29 at its old style grips against 0.30), per 400 city drives:
- City style: unchanged within noise (touches 9/15 → 6/20, off-road 25/0 → 23/51 m).
- Madman, default car: touches 28/26 → 23/23; off-road 36/31 → 39/15 m. Aggressive: 15/16 → 18/10.
- Hellhound: off-road 70–260 → 400–450 m, nearly all 1–2 m clips of a junction edge (under the 3 m the log reports). Truck: 80–200 → 200–330 m. Slippery car (Madman): 37/202 → 126/441 m.
- Average speeds in the simulator barely move: its city drives are held by speed limits and traffic, and the 0.29 problems (slides at 6°, the 7 m reach) were in-game ones the old simulator didn't have.

**The driving test (0.29) was broken:** it saved the route before the line had re-planned to the test's destination (the file held the route to the player's own pin), `BCAI_NavAhead` stopped at 2 km, and the minimap's GPS led to the player's pin. 0.30 saves the route once `Nav.Goal()` is the test's destination, up to 20 km; adds *New test to my map pin*; marks a random test's destination with a map pin (`MappinSystem.RegisterMappin`, `QuestStaticMappinDefinition`, `CustomPositionVariant`; untested); counts replans in the CSV header. `tools/drive_test.py` measures cornering from the velocity, re-finds the route after leaving it, compares bends only where a drive was on the route, and prints cornering against slip angle.

**Turning round.** The route guide planned only from the car's lane going its way, so after a missed turn it sent the player on round the block or to a roundabout far off (the 0.29 test: 911 m → 2.7 → 3.5 → 3.8 km). `PlanTurnRound` also plans from a lane the other way within 12 m (not a highway's) and takes it when quicker by more than 15 s (`kTurnRoundTime`); the hint says *Turn round*. Only while the player drives (forward.W = 1 to `BCAI_NavPlan`), not for Autodrive+ (it can't turn round in the road) or in a driving test. `drive_sim navturn 500 3`: of 460 trips from mid-road, turning round won in 145, saving 66 s (828 m) there; 7 of 13 routes over 3× the straight line no longer were.

**Own car's size:** rays from 6 m, then from 15 m if nothing is found (a truck); the record size isn't used for it any more.

**Untested in game (0.30):** all of the above, the map pin for random tests, and how the in-game slip stiffness and full-lock turn are learned.

<!-- ANCHOR_END: v030 -->

## People on foot and wide turns (plugin 0.31)

<!-- ANCHOR: v031 -->

**People on foot.** Autodrive+ was blind to them: the targeting query in `BCAIOwnDriver.Vehicles` filtered out `Obj_Puppet`, vehicle rays use the `Vehicle` collision group and wall rays `World Static`.
- The script side now runs the query without the puppet filter while *Watch for people on foot* is on and lists each `NPCPuppet` that is `ScriptedPuppet.IsActive`, not `VehicleComponent.IsMountedToVehicle`, within 100 m (60 m was too short: a truck at 65 km/h saw a crosser 48 m out and couldn't stop). They go to `BCAI_DriverVehicles` in the vehicle format with W +4 and no velocity; the plugin works it out from listed positions over 0.25 s or more, smoothed, ignoring jumps over 8 m/s.
- A person is a body of kind 2 (0 vehicle, 1 wall), only while on the road or within 1 m of its edge (a long vehicle's ends swing over the kerb), or walking onto it within 4 s. Clearance gives them 0.5 m more room than the car's half width, follows them walking across for up to 6 s (cars crossing: 2.5 s), grows their box by 0.3 m a second (up to 1 m: they change their minds) and waits for anyone moving across at over 0.4 m/s instead of swerving round them. It stops 4 m short, planning the stop at min(4.5, 0.6 × braking) m/s² so it starts early.
- Someone standing in the road (not crossing) is gone round after the style's `standingAfter`, at about 15 km/h, keeping 0.5 m from them alongside; boxed in close behind them it may back up for room (not for someone crossing). The rays' hits are never matched to people (`Absorb`, `OnKnown`), and people are left out of the oncoming-traffic checks.
- Crosswalks: lane flag 256 (3,472 lanes, mostly `0x012B`). `MeasureRoom` marks path points where a crosswalk segment crosses the line (within 1.5 m, |dot| < 0.6). *City* (`StyleRules::crosswalkCare`) slows for a crosswalk within 40 m with a listed person within 4 m of it to √(2 · 2.5 · (gap − 2)) + 4 m/s; only slowing, never waiting (people stand at crosswalks for the lights).
- Simulator: `BCAI_PEOPLE` (listed only, no velocity, as in game); `people_score.sh` on `randpeople.py` scenes (300 per run: crossers at crosswalks and mid-block stepping off 25-90 m ahead, pavement walkers, people standing at the kerb; the styles in turn), each driven again without the people. A hit counts as one it could have stopped for when the person set off further away than half a second of driving plus braking at min(6, 0.9 × the car's braking) m/s², plus 3 m, and wasn't walking into the car's side. Seed 7, against the same build with people unlisted (as 0.30): hits it could have stopped for 119 → 1 (default car), 139 → 5 (Hellhound), 110 → 0 (Maimai), 151 → 9 (truck); all hits 206 → 46 (default). People cost 5-7% of the time; pavement walkers and standers about 0 (City under 1%, the crosswalks). D1 passes its own start again at about 1440 m, so a crosser near 95 m ends up standing in that later road: a ready-made test for going round someone standing.

**Long vehicles at tight turns.** The 12 m sim truck touched other cars in 67/61 of 400 city drives (seeds 7/11), 35/31 of them oncoming cars, at about 7 km/h. A replay showed why: on full lock in a right turn (radius about 8 m) the centre follows the line but the body is on its tangent, so the front end is about a²/2R out past the outside of the bend (6 m: 2.2 m), over the oncoming lane. `Clearance` checked the body as if it bent with the path.
- `Driver::Swing` gives every path point the rigid body's front and rear offsets off the line with the centre there (`PathPoint::swingF`, `swingR`; in the plan dump), only for vehicles over 7 m (`Long()`), so cars are untouched (`same.sh`: 1,620 drives, all the same).
- `Clearance` adds the swing, less 0.3 m (`SwingAt`, interpolated by (a/front)²): its 0.6 m margin already covers that much, and the full swing left the truck stuck behind parked cars on gentle bends it had passed cleanly.
- `WatchWideTurn`: the stretch ahead (80 m) where an end comes within 0.3 m of its own side's edge with oncoming lanes beyond (judging only then whether a car there would really be reached, from where it is); while any vehicle coming the other way would be in the swept stretch while the truck is (its planned speed through, from a standstill if waiting, plus 2.5 s, or 1.2 s for a madman), it stops 1 m before it ("wide turn: oncoming", "waiting to swing wide"). A 0.3 m margin past the edge was too late: the truck waited with its body already out.
- Results: touches 67/61 → 35/25, oncoming 35/31 → 5/3 (most of those left: simulated oncoming cars, which don't brake, driving into the truck waiting), highway unchanged; 3 more drives in 800 stuck behind a parked car the truck used to scrape past; single obstacles 1,044 → 1,051 clean of 1,080.
- Not done: moving the line towards the inside of bends for long vehicles.

**Untested in game (0.31):** whether the targeting query returns crowd NPCs, and how many; people's positions and walking speeds as the plugin works them out; the crosswalk slowing; the truck's waits.

<!-- ANCHOR_END: v031 -->

## Large-vehicle body, steering and grip (plugin 0.31.1)

<!-- ANCHOR: v0311 -->

The first large-vehicle tests (the user's 7.6 m, 3.03 m-wide vehicle) showed the wheel swinging lock to lock on a straight road, and the learned grip dropping from 0.95 g to 0.72 g in about a second while the car was still at full lock. The log had `off` swinging ±2 m with `wanted` up to 1.47 rad/s (about 1.8 g) against a learned grip of 5.1 m/s².

**Body and rays.** `BCAIOwnDriver.Begin` now measures the front/rear with reaches 4, 13, 25 and 40 m (not 4 then 13), samples heights up to 1.5 m (a truck's front at windscreen height) and never falls back to the old 3 m, which sat inside any vehicle longer than a car and put the forward rays in its own body. A failed measurement uses a long safe point and is visible in the start log. The body also carries the ray height (`Body().W`, 0.7 m for a car, up to 1.5 m for a long vehicle); the plugin keeps it across `DriverStart` and raises `SetBody`'s limits to 20 m front/rear and 3 m half width. This also makes `Long()`/`Swing()`/`WideTurnWait()` actually run on the vehicles they were added for.

**Steering and grip.** `Driver::Steer` now keeps the planned line's own curvature (`CurvatureAt`, including the sideways move) separate from the pure-pursuit correction. The understeer/grip guard uses the line's yaw rate (`mPathWantRate`), not the corrected demand: a 1-2 m lateral error used to look like lost grip and teach the car a grip it doesn't have. Only the correction is capped, at 0.9 × the learned grip; the planned bend itself is never capped. The look-ahead grows for a car that turns less (`0.15 / Reach`, up to 1.6×) and for a long body (up to 1.4×), so a heavy slow-steering vehicle starts its turn earlier instead of chasing and overcorrecting.

The start log now says `rays N.N m up` and marks a long vehicle as `(long: swing-aware)`.

**Second in-game test (the Bratsk semi, front 5.4 m / rear 4.1 m).** The swerve was gone, but the debug ray fan still started 3 m from the centre (inside the truck), and the driver's own forward rays had only the old 0.3 m clearance. The debug overlay now takes the measured front/rear and ray height from Autodrive+ and starts its fan ahead of the body; the plugin adds a length-proportional forward clearance (0.1 × length, so 0.95 m on the 9.5 m semi). `mStartHits` is reset every frame instead of accumulating over the drive, so the log's "rays started inside something" is now per frame.

The semi also still cut/ran wide in junctions. The steering inverse used the learned fit's full lock, even though the tightest turn actually seen (`Reach`/`turnSeen`, already used by the speed plan) can be tighter. For long vehicles the inverse now uses `Reach` as full lock, so the wheel is not held further over than the bend needs. Cars keep the fit.

**Simulator.** The same 40 city scenes as 0.31: 6 m off the road and 0.65 changes of mind per drive (0.31: 6 m and 0.95), the same 4 scenes with contact. A 7.6 m, 5.1 m/s², slow-wheel car on D1 went from 3.01 m maximum off line and 0.254 steering movement per second to 2.08 m and 0.205. The telemetry now prints `reach` after the learned steering fit.

**Untested in game (0.31.1):** the new reach fallback and ray height on a Behemoth/cement truck; the adaptive look-ahead and correction cap on those vehicles.

<!-- ANCHOR_END: v0311 -->

## Learning at real cornering, and the plan gradient (plugin 0.31.2)

<!-- ANCHOR: v0312 -->

**The red plan line.** It was not a grip estimate: `BCAIDebug` coloured each plan segment by `planned speed / current speed` (red below 45%). The speed plan floors at 3 m/s (10.8 km/h), so any junction bend the planner thought the car could not take painted its whole approach red and the car crawled there. The reference is now the plan’s speed at the first point, or the car’s speed if it is already faster, so a normal slowdown no longer colours the approach red.

**Learning the real steering response.** The steering fit only accepted samples below **2.9° of slip and 0.6 g** (`|slip| < 0.05`, `|yaw·v| < 6`). That excludes the 5-17° / up to 2 g cornering the driving tests called gripping, so a heavy vehicle never learned its real response: the fit stayed at the gentle-corner value, `Reach`/the speed plan stayed conservative, and the plan floored at 10.8 km/h. The fit now learns at up to **17° slip and 2.4 g**, with samples weighted down toward the slip limit, while the pedals still learn only in a straight line and true slides (`mSlideTime`) are still excluded. `turnSeen` and `slipStiff` use the same 17° envelope.

**Kink guard.** `Profile` still uses a 4 m window for cars, but a 9.5-12 m truck cannot follow a 4 m kink. It now compares the short-window curvature with one scaled to 70% of the body length; if the short window alone would floor the plan but the long one is much gentler, it uses the long one. A real tight corner still uses the short window and still slows.

**Simulator.** 40 city scenes: 0 m off the road and 0.85 changes of mind per drive (0.31.1: 6 m and 0.65), the same 4 contact scenes. The 9.5 m, 10 m/s², slow-wheel test car still uses the 3 m/s floor at one genuine tight junction; the kink guard only substitutes the long window when the short one is impossible and the long one is much gentler, so a real tight corner still slows. The wider learning envelope is the part that should raise `Reach` on the in-game truck.

**Untested in game (0.31.2):** the wider learning envelope and kink guard on the Behemoth/cement truck; whether the plan gradient now reads sensibly in the overlay.

<!-- ANCHOR_END: v0312 -->
