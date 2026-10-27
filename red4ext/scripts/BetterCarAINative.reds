// Better Car AI - natives from the RED4ext plugin (red4ext/plugins/BetterCarAI).
// The plugin adds this file to the script compilation itself, so it only
// exists while the plugin is loaded; the main mod works without it.

// 10000 * major + 100 * minor + patch
public static native func BCAI_NativeVersion() -> Int32
// Writes the classes/enums whose names contain one of the comma-separated
// filters to dumps/types_*.txt. Returns a summary and the file path.
public static native func BCAI_DumpTypes(filters: String) -> String
// Writes the code addresses (relative to the game exe) of native script
// functions whose class or name matches a filter to dumps/natives_*.txt.
public static native func BCAI_DumpNatives(filters: String) -> String
// Finds the traffic lane table through the autodrive system and writes the
// lanes within `radius` of `pos` to dumps/lanes_*.txt. Returns a summary.
public static native func BCAI_LaneQuery(autodrive: ref<IScriptable>, pos: Vector4, radius: Float) -> String
// Loads the lane graph (used by BCAI_ScanForPath to find lane routes).
// Returns the number of lanes, 0 if the table wasn't found.
public static native func BCAI_LoadLanes(autodrive: ref<IScriptable>) -> Int32
// Writes the route the car is driving (lanes from start to destination) raw
// and decoded to dumps/route_*.txt. Returns a summary.
public static native func BCAI_DumpRoute(autodrive: ref<IScriptable>, pos: Vector4) -> String
// Points along that route from `pos` (projected onto it) for `ahead` metres;
// empty without a route or when the car is off it.
public static native func BCAI_RouteAhead(autodrive: ref<IScriptable>, pos: Vector4, ahead: Float) -> array<Vector4>
// Speed plan from the route: X = the speed (m/s) to drive now so every bend
// in the next 400 m can be taken at `latAccel` m/s^2 braking at `decel`
// (-1 = not on a route), Y = metres to the limiting bend, Z = its radius,
// W = route points ahead.
public static native func BCAI_RoutePlan(autodrive: ref<IScriptable>, pos: Vector4, latAccel: Float, decel: Float) -> Vector4
// Repairs junctions whose lanes are wired to the wrong side of the road (the
// route jogs sideways across the road there), once per lane table; with
// apply = false, undoes it. Returns a summary; details in dumps/lane_repairs.txt.
public static native func BCAI_RepairLanes(autodrive: ref<IScriptable>, apply: Bool) -> String
// Lanes within `radius` of `pos` for drawing: per lane a header Vector4
// (kind: 1 road, 2 junction, 3 GPS-only, 4 no AI/autodrive, 5 pavement,
// 6 repaired; point count; lane index; 1 if nothing links out of its end),
// then its points.
public static native func BCAI_LanesNear(autodrive: ref<IScriptable>, pos: Vector4, radius: Float, pavements: Bool) -> array<Vector4>
// Writes every lane in the city (flags, links, outline) to dumps/lanegraph.txt.
public static native func BCAI_DumpLaneGraph(autodrive: ref<IScriptable>) -> String
// Writes an object's RTTI-visible properties (following references `depth`
// levels) to dumps/object_*.txt. Returns the file path.
public static native func BCAI_DumpObject(obj: ref<IScriptable>, depth: Int32) -> String
// Scans memory reachable from `obj` for arrays of points near `origin` (the
// AI's planned path), ranked using the car's `heading`, and - after
// BCAI_LoadLanes - for chains of linked lanes. With name "autodrive" it
// also scans the traffic system. Writes
// dumps/pathscan_*.txt; returns a summary and the path.
public static native func BCAI_ScanForPath(obj: ref<IScriptable>, name: String, origin: Vector4, heading: Vector4, depth: Int32) -> String
// Control inputs, as a Vector4: X throttle (0..1), Y brake (0..1), Z steer
// (-1..1, as the TurnX input), W handbrake (0..1).
// Adds one sample of the car's memory against the player's inputs while they
// drive; a new car starts a new probe. Returns the sample count.
public static native func BCAI_ControlProbe(vehicle: ref<IScriptable>, inputs: Vector4) -> Int32
public static native func BCAI_ControlProbeReset() -> Int32
// Ranks the car's fields by how closely they follow each input
// (dumps/controls_probe.txt) and keeps the best for Autodrive+ (controls.txt
// next to the plugin). Returns a summary.
public static native func BCAI_ControlProbeReport() -> String
// Where the inputs were found.
public static native func BCAI_ControlFields() -> String
// The inputs as the car holds them now (-99 where not found).
public static native func BCAI_ReadControls(vehicle: ref<IScriptable>) -> Vector4
// Writes these inputs into the car every frame for `seconds` (at most 5, so
// they stop if the caller does); seconds <= 0 lets go.
public static native func BCAI_SetControls(vehicle: ref<IScriptable>, controls: Vector4, seconds: Float) -> String
// Frames written, and how often the game changed each input in between.
public static native func BCAI_ControlStatus() -> String
// BCAI Autodrive+, our own driving AI. Plans a route from `pos` (heading `forward`) to `goal`
// over the lane map, or along the road when hasGoal is false. Returns a
// summary.
public static native func BCAI_DriverStart(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, goal: Vector4, hasGoal: Bool) -> String
// One frame: the car's position, forward vector and velocity, and settings
// (X max speed m/s, Y cornering m/s^2, Z x lane speed limit, W braking
// m/s^2). Drives the car. Returns X target speed (m/s), Y metres off the
// planned line (left +), Z steering, W state (0 driving, 1 arriving,
// 2 stopped: see BCAI_DriverStatus, 3 stuck).
public static native func BCAI_DriverStep(vehicle: ref<IScriptable>, pos: Vector4, forward: Vector4, velocity: Vector4, dt: Float, settings: Vector4, followTime: Float) -> Vector4
// The planned path from just ahead of the bumper, every `step` metres (W: the
// planned speed there, m/s).
public static native func BCAI_DriverAhead(distance: Float, step: Float) -> array<Vector4>
// The sensor rays to cast this frame, from just ahead of the car's front
// (`front` m ahead of its centre): pairs of start (W 0: vehicles, 1: walls)
// and end.
public static native func BCAI_DriverRays(front: Float) -> array<Vector4>
// Per ray, in order: where it hit (W 1) or its end (W 0). Returns how many
// points the sensor remembers.
public static native func BCAI_DriverSense(ends: array<Vector4>) -> Int32
// The car's own body when Autodrive+ starts: X front, Y rear (from its
// centre), Z half its width, W the height its forward rays start at.
public static native func BCAI_DriverBody(body: Vector4) -> Bool
// The car's steering geometry: X wheelbase (m), Y steering lock (degrees),
// Z mass (kg). Keeps the learned full-lock turn within the wheel geometry.
public static native func BCAI_DriverProfile(vehicle: ref<IScriptable>, profile: Vector4) -> Bool
// Vehicles the game lists nearby, three entries each: position (W 1 parked,
// +2 a bike, +4 a person on foot), forward, velocity. Returns how many the
// driver follows.
public static native func BCAI_DriverVehicles(list: array<Vector4>) -> Int32
// What the sensor remembers (W 1 vehicle, 2 wall, +2 the one in the way; 5
// a person on foot, 6 the one in the way).
public static native func BCAI_DriverSeen() -> array<Vector4>
// X gap to what is in the way (< 0 nothing), Y what (0 vehicle, 1 wall),
// Z where it is heading sideways from its line, W how far off that it is.
public static native func BCAI_DriverInfo() -> Vector4
// The road's left and right edges along the path ahead, in pairs.
public static native func BCAI_DriverRoom(distance: Float, step: Float) -> array<Vector4>
// Stops Autodrive+ and lets go of the controls; returns its status.
public static native func BCAI_DriverStop(why: String) -> String
public static native func BCAI_DriverStatus() -> String
// Writes the planned path and speed plan to dumps/driver_path.txt.
public static native func BCAI_DriverDumpPath() -> String
// One line on what Autodrive+ did last frame and why (speeds in km/h).
public static native func BCAI_DriverTelemetry() -> String
// The driving style from the next frame: 0 city (keeps right, passes only
// what stands still, careful at junctions), 1 aggressive (also overtakes slow
// cars on the other side of the road when it can see it is clear), 2 madman
// (any lane, any gap). Returns the style set.
public static native func BCAI_DriverStyle(style: Int32) -> Int32
// What Autodrive+ is doing now, as "key=value" pairs separated by ';' (speeds
// in km/h, distances in m): active, style, v, target, limit, why (what holds
// the speed down), doing, lane/tolane/lanes (counted from the right), ahead,
// aheadv, aheadkind, bend, bendv, left, offroad, replans, moves, dithers,
// throttle, brake, steer, slip, grip, oncoming, junction, status.
public static native func BCAI_DriverHud() -> String
// What Autodrive+ decided since last asked (plugin 0.27+), a line each:
// seconds into the drive, speed, metres along the route, what and why (lane
// changes, passing, what holds the speed down, backing up, leaving the road).
public static native func BCAI_DriverEvents() -> String

// BCAI navigation (plugin 0.28+): our own route to the map destination,
// planned for time over the lane map and ending on the pin's level (the same
// planner Autodrive+ drives with). inCar: start on the lane the car is on,
// going its way; otherwise from any road near pos. Returns "route: ..." or
// why not.
public static native func BCAI_NavPlan(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, goal: Vector4, inCar: Bool) -> String
// The route from pos (projected onto it) for ahead metres, a point every step metres.
public static native func BCAI_NavAhead(pos: Vector4, ahead: Float, step: Float) -> array<Vector4>
// Where pos is on the route: "key=value" pairs separated by ';': left (m to
// go), time (s), off (m from the line), and the next turn: turn (left, right,
// slight left, slight right, U-turn), in (m), angle (degrees, left +).
public static native func BCAI_NavStatus(pos: Vector4) -> String
public static native func BCAI_NavClear() -> Bool

// Plugin 0.29+: a destination for a driving test, an ordinary road minDist..maxDist
// metres away that the car can reach (W 1; W 0 when none was found).
public static native func BCAI_NavRandom(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, minDist: Float, maxDist: Float, seed: Int32) -> Vector4
// Research: scans the game's GPS system for the route it draws on the map
// (lane routes, like BCAI_ScanForPath) to dumps/pathscan_gps_*.txt.
public static native func BCAI_ScanGPS(autodrive: ref<IScriptable>, origin: Vector4, heading: Vector4) -> String
