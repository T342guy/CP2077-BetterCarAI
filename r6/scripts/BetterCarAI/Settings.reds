// Better Car AI - shared settings.
// Defaults live here so the script half works even without CET; the CET mod
// pushes the user's saved values in through the public setters below.

public class BCAISettings extends ScriptableSystem {

  // --- NPC traffic reactions ---
  public let calmTrafficEnabled: Bool;
  // Impacts below this velocity change (m/s) are treated as a tap.
  public let panicImpactThreshold: Float;
  // Seconds after a hard impact during which mild stims still cause panic.
  public let hardImpactMemory: Float;
  public let honkOnTap: Bool;
  // People on foot don't flee from a light knock between cars nearby.
  public let calmPedestrians: Bool;
  // The tap (within tapMemory seconds of the last) at which a driver loses
  // patience and the game's own reaction follows; 0: never.
  public let tapPatience: Int32;
  public let tapMemory: Float;
  // A dent this big (the game's deformation, 0..1) counts as a crash.
  public let crashDent: Float;
  // What NPC drivers did about being bumped, for the CET log ("Traffic |").
  public let trafficLog: Bool;
  private let trafficNotes: array<String>;

  // --- Autodrive ---
  public let aggressiveMode: Bool;
  // Speed cap (m/s) passed to the autodrive command; 0 keeps the vanilla cap.
  public let normalMaxSpeed: Float;
  public let aggroMaxSpeed: Float;
  // Ask traffic ahead to make way (what quest/chase drivers use).
  public let aggroClearTraffic: Bool;
  public let aggroRunLights: Bool;
  // AI drive: the command's minSpeed, which the driver uses through tight corners.
  public let aiMinSpeed: Float;

  // --- Diagnostics (shown by the CET debug trace) ---
  public let diagStarts: Int32;
  public let diagUpdates: Int32;
  public let diagUpdatesSent: Int32;
  public let diagVanillaMaxSpeed: Float;
  public let diagAppliedMaxSpeed: Float;

  private func OnAttach() -> Void {
    this.calmTrafficEnabled = true;
    this.panicImpactThreshold = 7.0; // ~25 km/h
    this.hardImpactMemory = 3.0;
    this.honkOnTap = true;
    this.calmPedestrians = true;
    this.tapPatience = 4;
    this.tapMemory = 30.0;
    this.crashDent = 0.1;
    this.trafficLog = true;
    this.aggressiveMode = false;
    this.normalMaxSpeed = 0.0;
    this.aggroMaxSpeed = 70.0; // ~250 km/h, i.e. as fast as the car goes
    this.aggroClearTraffic = true;
    this.aggroRunLights = false;
    this.aiMinSpeed = 10.0;
    this.aiGovernor = true;
  }

  public static func Get(game: GameInstance) -> ref<BCAISettings> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"BCAISettings") as BCAISettings;
  }

  public func SetCalmTrafficEnabled(value: Bool) -> Void {
    this.calmTrafficEnabled = value;
  }

  public func SetPanicImpactThreshold(value: Float) -> Void {
    this.panicImpactThreshold = MaxF(value, 0.0);
  }

  public func SetHonkOnTap(value: Bool) -> Void {
    this.honkOnTap = value;
  }

  public func SetTrafficReactions(calmPedestrians: Bool, tapPatience: Int32, log: Bool) -> Void {
    this.calmPedestrians = calmPedestrians;
    this.tapPatience = Max(tapPatience, 0);
    this.trafficLog = log;
  }

  // A line for the CET log (the last 40 are kept until CET takes them).
  public func NoteTraffic(line: String) -> Void {
    if !this.trafficLog {
      return;
    };
    if ArraySize(this.trafficNotes) >= 40 {
      ArrayErase(this.trafficNotes, 0);
    };
    ArrayPush(this.trafficNotes, line);
  }

  public func TakeTrafficNotes() -> array<String> {
    let out: array<String> = this.trafficNotes;
    ArrayClear(this.trafficNotes);
    return out;
  }

  public func SetAggressiveMode(value: Bool) -> Void {
    this.aggressiveMode = value;
  }

  public func SetAutodriveSpeeds(normalMaxSpeed: Float, aggroMaxSpeed: Float) -> Void {
    this.normalMaxSpeed = MaxF(normalMaxSpeed, 0.0);
    this.aggroMaxSpeed = MaxF(aggroMaxSpeed, 0.0);
  }

  public func SetAggroTraffic(clearTraffic: Bool, runLights: Bool) -> Void {
    this.aggroClearTraffic = clearTraffic;
    this.aggroRunLights = runLights;
  }

  public func SetAIDriveMinSpeed(value: Float) -> Void {
    this.aiMinSpeed = MaxF(value, 0.0);
  }

  public func GetAutodriveMaxSpeed() -> Float {
    return this.aggressiveMode ? this.aggroMaxSpeed : this.normalMaxSpeed;
  }

  public func IsPlayerInCombat() -> Bool {
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(this.GetGameInstance()).GetLocalPlayerMainGameObject() as PlayerPuppet;
    if !IsDefined(player) {
      return false;
    };
    return player.GetPlayerStateMachineBlackboard().GetInt(GetAllBlackboardDefs().PlayerStateMachine.Combat) == 1;
  }

  // The native autodrive reads its driving params when it starts, so after CET
  // swaps the TweakDB params we bounce autodrive to make them take effect.
  public func RestartAutodrive() -> Bool {
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"AutoDriveSystem") as AutoDriveSystem;
    if !IsDefined(autodrive) || !autodrive.GetAutodriveEnabled() || autodrive.GetAutodriveIsDelamain() {
      return false;
    };
    autodrive.SetAutodriveEnabled(false);
    autodrive.QueueRequest(new EnableAutoDriveRequest());
    return true;
  }

  // One-line snapshot of the running autodrive: actual speed, the native
  // command's values and whether our delegate hooks have fired.
  public func DescribeAutodrive() -> String {
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(this.GetGameInstance()).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    let cmd: ref<AIVehicleDriveToPointAutonomousCommand>;
    let out: String;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(this.GetGameInstance(), player, vehicle) || !IsDefined(vehicle) {
      return "not in a vehicle";
    };
    out = "autodrive=" + ToString(IsDefined(autodrive) && autodrive.GetAutodriveEnabled())
      + " aiDrive=" + ToString(this.IsCommandDriving())
      + " aggressive=" + ToString(this.aggressiveMode)
      + " speed=" + FloatToStringPrec(AbsF(vehicle.GetCurrentSpeed()) * 3.6, 1) + "km/h";
    if IsDefined(vehicle.GetAIComponent()) {
      cmd = vehicle.GetAIComponent().GetEnqueuedOrExecutingCommand(n"AIVehicleDriveToPointAutonomousCommand", true) as AIVehicleDriveToPointAutonomousCommand;
    };
    if IsDefined(cmd) {
      out += " cmd.state=" + ToString(EnumInt(cmd.state))
        + " cmd.maxSpeed=" + FloatToStringPrec(cmd.maxSpeed * 3.6, 1)
        + " cmd.minSpeed=" + FloatToStringPrec(cmd.minSpeed * 3.6, 1)
        + " cmd.clearTraffic=" + ToString(cmd.clearTrafficOnPath);
    } else {
      out += " cmd=none";
    };
    out += " lastStop=" + this.bcaiLastStop + " hudOverwrites=" + ToString(this.diagHudOverwrites);
    out += " hooks: starts=" + ToString(this.diagStarts)
      + " updates=" + ToString(this.diagUpdates)
      + " sent=" + ToString(this.diagUpdatesSent)
      + " vanillaMax=" + FloatToStringPrec(this.diagVanillaMaxSpeed * 3.6, 1)
      + " appliedMax=" + FloatToStringPrec(this.diagAppliedMaxSpeed * 3.6, 1);
    return out;
  }

  public func IsAutodriveActive() -> Bool {
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"AutoDriveSystem") as AutoDriveSystem;
    return IsDefined(autodrive) && autodrive.GetAutodriveEnabled();
  }

  // True when the player's own autodrive (not a Delamain cab ride) is running
  // and AI drive should take it over. Right after the player switched AI drive
  // off with the autodrive button, the HUD also starts native autodrive; that
  // one is switched off instead.
  public func ShouldTakeOverAutodrive() -> Bool {
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"AutoDriveSystem") as AutoDriveSystem;
    if !IsDefined(autodrive) || !autodrive.GetAutodriveEnabled() || autodrive.GetAutodriveIsDelamain() {
      return false;
    };
    if this.Now() < this.bcaiSuppressUntil {
      autodrive.SetAutodriveEnabled(false);
      return false;
    };
    return true;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
  }

  public func ShowMessage(message: String) -> Void {
    let msg: SimpleScreenMessage;
    msg.isShown = true;
    msg.duration = 2.0;
    msg.message = message;
    msg.type = SimpleMessageType.Autodrive;
    GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_Notifications).SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(msg), true);
  }

  // --- AI command drive (experimental) ---
  // Native player autodrive holds ~24 m/s and ignores every speed setting we can
  // reach. Police and quest drivers use AIVehicleDriveToPointAutonomousCommand
  // instead, whose maxSpeed the native driver does honour, so this drives the
  // player's car with that command (to the autodrive destination, or straight
  // down the road when none is set).

  private let bcaiDriveCmd: ref<AIVehicleDriveToPointAutonomousCommand>;

  private let bcaiDriveVehicle: wref<VehicleObject>;

  private let bcaiInputPlayer: wref<PlayerPuppet>;
  // The player's driving inputs while the native control probe runs.
  private let bcaiInputWatch: ref<BCAIInputWatch>;
  // BCAI Autodrive+, our own driving AI (native plugin 0.16+).
  private let bcaiOwnDriver: ref<BCAIOwnDriver>;
  public let bcaiOwnDriverStop: String;
  private let bcaiAutodrivePlusTakeover: Bool;
  // Autodrive+ lists people on foot to the plugin (0.31+).
  private let bcaiAutodrivePlusPeople: Bool;
  private let bcaiAutodrivePlusRequested: Bool;

  private let bcaiDriveTarget: Vector4;

  private let bcaiDriveIndefinitely: Bool;

  // Speed cap last pushed to the running command (m/s).
  private let bcaiSentCap: Float;

  // Whether we're showing the autodrive HUD for AI drive.
  private let bcaiHudShown: Bool;

  // Smoothed speed for the HUD's time estimate, and when it was last updated.
  private let bcaiHudSpeed: Float;
  private let bcaiHudTime: Float;
  // Distance we last wrote to the HUD, and how often something else replaced it.
  private let bcaiHudLength: Float;
  public let diagHudOverwrites: Int32;

  // When AI drive started (sim time).
  private let bcaiStartTime: Float;

  // Native autodrive started before this sim time is not taken over.
  private let bcaiSuppressUntil: Float;

  // Why AI drive last stopped (debug).
  public let bcaiLastStop: String;

  // --- Visual debug (see Debug.reds) ---

  private let bcaiDebug: ref<BCAIDebug>;

  // Called ~30x a second from CET while the debug overlay is on (~10x for
  // the recorder alone). lookahead is the vision range (m) of the params the
  // car is using right now.
  public func DebugTick(lookahead: Float) -> Void {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let vehicle: wref<VehicleObject>;
    let cmd: ref<AIVehicleDriveToPointAutonomousCommand>;
    let lines: array<String>;
    let d: ref<BCAIDebug>;
    let goal: Vector4;
    if !IsDefined(this.bcaiDebug) {
      this.bcaiDebug = new BCAIDebug();
      this.bcaiDebug.Init();
    };
    d = this.bcaiDebug;
    if lookahead > 1.0 {
      d.lookahead = lookahead;
    };
    if IsDefined(player) {
      VehicleComponent.GetVehicle(game, player, vehicle);
    };
    d.Tick(game, vehicle);
    if !IsDefined(vehicle) {
      ArrayPush(lines, "not in a vehicle");
      d.SetLines(lines);
      return;
    };

    if this.IsOwnDriving() {
      goal = this.AutodriveGoal();
      d.SetTarget(goal.W > 0.5, goal, BCAISettings.ArrivalDistance());
      ArrayPush(lines, "BCAI Autodrive+ " + (goal.W > 0.5 ? "(to destination)" : "(down the road)")
        + ", car ahead " + (this.OwnDriverObstacle() < 0.0 ? "-" : FloatToStringPrec(this.OwnDriverObstacle(), 0) + " m"));
    } else if this.IsCommandDriving() {
      d.SetTarget(!this.bcaiDriveIndefinitely, this.bcaiDriveTarget, BCAISettings.ArrivalDistance());
      ArrayPush(lines, "AI drive " + (this.bcaiDriveIndefinitely ? "(down the road)" : "(to destination)") + (IsDefined(vehicle as BikeObject) ? " - bike" : ""));
    } else {
      if IsDefined(autodrive) && autodrive.GetAutodriveEnabled() && NotEquals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.None) {
        d.SetTarget(true, Vector4.Vector3To4(autodrive.GetAutodriveDestination()), BCAISettings.ArrivalDistance());
      } else {
        d.SetTarget(false, vehicle.GetWorldPosition(), 0.0);
      };
      ArrayPush(lines, IsDefined(autodrive) && autodrive.GetAutodriveEnabled() ? "vanilla autodrive" : "manual driving");
    };
    ArrayPush(lines, "speed " + FloatToStringPrec(d.Speed() * 3.6, 0) + " km/h");
    if IsDefined(vehicle.GetAIComponent()) {
      cmd = vehicle.GetAIComponent().GetEnqueuedOrExecutingCommand(n"AIVehicleDriveToPointAutonomousCommand", true) as AIVehicleDriveToPointAutonomousCommand;
    };
    if IsDefined(cmd) {
      ArrayPush(lines, "command: max " + FloatToStringPrec(cmd.maxSpeed * 3.6, 0) + " / corner min " + FloatToStringPrec(cmd.minSpeed * 3.6, 0) + " km/h, state " + ToString(EnumInt(cmd.state)));
    };
    if this.IsCommandDriving() && !this.bcaiDriveIndefinitely {
      ArrayPush(lines, "approach cap " + FloatToStringPrec(this.bcaiSentCap * 3.6, 0) + " km/h, "
        + FloatToStringPrec(Vector4.Distance(vehicle.GetWorldPosition(), this.bcaiDriveTarget), 0) + " m to go");
    };
    if this.IsCommandDriving() {
      ArrayPush(lines, this.GetGovernorState());
    };
    ArrayPush(lines, "sideways " + FloatToStringPrec(d.Lateral(), 1) + " m/s, turning " + FloatToStringPrec(d.YawRateDeg(), 0) + " deg/s");
    ArrayPush(lines, d.IsAirborne() ? "AIRBORNE" : "on the ground: " + BCAISettings.SurfaceText(d.Surface(), d.SurfaceName()));
    ArrayPush(lines, "look-ahead " + FloatToStringPrec(d.lookahead, 0) + " m: car "
      + (d.NearestVehicle() < 0.0 ? "-" : FloatToStringPrec(d.NearestVehicle(), 0) + " m")
      + ", wall " + (d.NearestWall() < 0.0 ? "-" : FloatToStringPrec(d.NearestWall(), 0) + " m"));
    if NotEquals(this.bcaiLastStop, "") {
      ArrayPush(lines, "last stop: " + this.bcaiLastStop);
    };
    ArrayPush(lines, d.Calibration());
    d.SetLines(lines);
  }

  // --- Drive recorder (see Recorder.reds) ---

  private let bcaiRecorder: ref<BCAIRecorder>;

  private func V(v: Float, prec: Int32) -> String {
    return FloatToStringPrec(v, prec);
  }

  // Called ~10x a second from CET, right after DebugTick().
  public func RecorderTick() -> Void {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let vehicle: wref<VehicleObject>;
    let cmd: ref<AIVehicleDriveToPointAutonomousCommand>;
    let d: ref<BCAIDebug> = this.bcaiDebug;
    let p: Vector4;
    let f: Vector4;
    let v: Vector4;
    let mode: String;
    let line: String;
    if !IsDefined(this.bcaiRecorder) {
      this.bcaiRecorder = new BCAIRecorder();
    };
    if IsDefined(player) {
      VehicleComponent.GetVehicle(game, player, vehicle);
    };
    if !IsDefined(d) || !IsDefined(vehicle) || !d.HasVehicle() {
      this.bcaiRecorder.Reset();
      return;
    };
    if IsDefined(vehicle.GetAIComponent()) {
      cmd = vehicle.GetAIComponent().GetEnqueuedOrExecutingCommand(n"AIVehicleDriveToPointAutonomousCommand", true) as AIVehicleDriveToPointAutonomousCommand;
    };
    if this.IsOwnDriving() {
      mode = "autodrive+";
    } else if this.IsCommandDriving() {
      mode = "ai";
    } else {
      mode = IsDefined(autodrive) && autodrive.GetAutodriveEnabled() ? "autodrive" : "manual";
    };
    p = d.Position();
    f = d.Forward();
    v = d.Velocity();
    line = this.V(p.X, 2) + "," + this.V(p.Y, 2) + "," + this.V(p.Z, 2)
      + "," + this.V(f.X, 3) + "," + this.V(f.Y, 3) + "," + this.V(f.Z, 3)
      + "," + this.V(v.X, 2) + "," + this.V(v.Y, 2) + "," + this.V(v.Z, 2)
      + "," + this.V(d.Speed() * 3.6, 1) + "," + this.V(d.Lateral(), 2) + "," + this.V(d.YawRateDeg(), 1)
      + "," + this.V(d.Clearance(), 2) + "," + (d.IsAirborne() ? "1" : "0")
      + "," + d.RaysCsv() + "," + this.V(d.NearestVehicle(), 1) + "," + this.V(d.NearestWall(), 1)
      + "," + mode
      + "," + (IsDefined(cmd) ? ToString(EnumInt(cmd.state)) : "")
      + "," + (IsDefined(cmd) ? this.V(cmd.maxSpeed * 3.6, 0) : "")
      + "," + (IsDefined(cmd) ? this.V(cmd.minSpeed * 3.6, 0) : "")
      + "," + (this.IsCommandDriving() ? this.V(this.bcaiSentCap * 3.6, 0) : "")
      + "," + (this.IsCommandDriving() && this.aiGovernor ? this.V(this.GovernorCap() * 3.6, 0) : "")
      + "," + (this.IsCommandDriving() && this.aiGovernor ? this.GovernorLimitReason() : "")
      + "," + (this.IsCommandDriving() && !this.bcaiDriveIndefinitely ? this.V(Vector4.Distance(p, this.bcaiDriveTarget), 0) : "");
    this.bcaiRecorder.Sample(this.Now(), d, vehicle, this.IsCommandDriving(), line);
  }

  // Reason for an automatic save that is due now, or "".
  public func RecorderTakePending() -> String {
    if !IsDefined(this.bcaiRecorder) {
      return "";
    };
    return this.bcaiRecorder.TakePending(this.Now());
  }

  public func RecorderLines() -> array<String> {
    let empty: array<String>;
    if !IsDefined(this.bcaiRecorder) {
      return empty;
    };
    return this.bcaiRecorder.GetLines();
  }

  public func RecorderHeader() -> String {
    return BCAIRecorder.Header();
  }

  public func SetDebugProjection(fovHorizontal: Bool, forceManual: Bool) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.fovHorizontal = fovHorizontal;
      this.bcaiDebug.forceManual = forceManual;
    };
  }

  // Which of the overlay's layers to draw.
  public func SetDebugLayers(trail: Bool, rays: Bool, route: Bool, sensor: Bool, predict: Bool) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.hideTrail = !trail;
      this.bcaiDebug.hideRays = !rays;
      this.bcaiDebug.hideRoute = !route;
      this.bcaiDebug.hideSensor = !sensor;
      this.bcaiDebug.hidePredict = !predict;
    };
  }

  // The AI's route ahead of the car, from the native plugin (via CET); empty
  // clears it. withSpeed: W holds Autodrive+'s planned speed (m/s).
  public func SetRouteAhead(points: array<Vector4>, withSpeed: Bool) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.SetRoute(points, withSpeed);
    };
  }

  // The label on what is in Autodrive+'s way ("" none).
  public func SetBlockLabel(label: String) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.SetBlockLabel(label);
    };
  }

  // The overlay's measurements (speed, slip, ground, rays...) for the CET
  // panel; null before the overlay or the recorder first ran.
  public func DebugState() -> ref<BCAIDebug> {
    return this.bcaiDebug;
  }

  // "road (asphalt)", "dirt (gravel)", "no ground found"...
  public static func SurfaceText(kind: Int32, material: String) -> String {
    let name: String = kind == 0 ? "road" : kind == 1 ? "dirt" : kind == 2 ? "grass" : kind == 3 ? "other" : "no ground found";
    if kind < 4 && NotEquals(material, "") {
      name += " (" + material + ")";
    };
    return name;
  }

  // Lanes around the car for the overlay, from the native plugin (via CET).
  public func SetDebugLanes(lanes: array<Vector4>) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.SetLanes(lanes);
    };
  }

  // The route line on screen, 6 floats a segment (see BCAIDebug.ProjectPolyline).
  public func GuideLine(points: array<Vector4>) -> array<Float> {
    if !IsDefined(this.bcaiDebug) {
      this.bcaiDebug = new BCAIDebug();
      this.bcaiDebug.Init();
    };
    return this.bcaiDebug.ProjectPolyline(this.GetGameInstance(), points, 0.4, true);
  }

  public func DebugFrame() -> array<Float> {
    let empty: array<Float>;
    if !IsDefined(this.bcaiDebug) {
      return empty;
    };
    return this.bcaiDebug.Frame(this.GetGameInstance());
  }

  public func DebugFrameTexts() -> array<String> {
    let empty: array<String>;
    if !IsDefined(this.bcaiDebug) {
      return empty;
    };
    return this.bcaiDebug.FrameTexts();
  }

  public func DebugLines() -> array<String> {
    let empty: array<String>;
    if !IsDefined(this.bcaiDebug) {
      return empty;
    };
    return this.bcaiDebug.GetLines();
  }

  public func IsMountedBike() -> Bool {
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(this.GetGameInstance()).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(this.GetGameInstance(), player, vehicle) {
      return false;
    };
    return IsDefined(vehicle as BikeObject);
  }

  public func StartInputWatch() -> Bool {
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(this.GetGameInstance()).GetLocalPlayerMainGameObject() as PlayerPuppet;
    if !IsDefined(player) {
      return false;
    };
    this.StopInputWatch();
    this.bcaiInputWatch = new BCAIInputWatch();
    this.bcaiInputWatch.Start(player);
    return true;
  }

  public func StopInputWatch() -> Void {
    if IsDefined(this.bcaiInputWatch) {
      this.bcaiInputWatch.Stop();
      this.bcaiInputWatch = null;
    };
  }

  // X throttle, Y brake, Z steer, W handbrake (all 0 when not watching).
  public func InputWatchValues() -> Vector4 {
    if !IsDefined(this.bcaiInputWatch) {
      return new Vector4(0.0, 0.0, 0.0, 0.0);
    };
    return new Vector4(this.bcaiInputWatch.throttle, this.bcaiInputWatch.brake, this.bcaiInputWatch.steer, this.bcaiInputWatch.handbrake);
  }

  // Autodrive+ itself runs in the native plugin, driven from CET; these keep
  // its script half (input watch, sensor) and the autodrive takeover.
  public func SetAutodrivePlusTakeover(on: Bool) -> Void {
    this.bcaiAutodrivePlusTakeover = on;
  }

  // Whether Autodrive+ watches for people on foot (the game's people are
  // listed to the plugin with the vehicles).
  public func SetAutodrivePlusPeople(on: Bool) -> Void {
    this.bcaiAutodrivePlusPeople = on;
    if IsDefined(this.bcaiOwnDriver) {
      this.bcaiOwnDriver.people = on;
    };
  }

  // From the autodrive start request: true if Autodrive+ should start
  // instead (CET picks it up with TakeAutodrivePlusRequest).
  public func RequestAutodrivePlus() -> Bool {
    if !this.bcaiAutodrivePlusTakeover {
      return false;
    };
    this.bcaiAutodrivePlusRequested = true;
    return true;
  }

  public func TakeAutodrivePlusRequest() -> Bool {
    let requested: Bool = this.bcaiAutodrivePlusRequested;
    this.bcaiAutodrivePlusRequested = false;
    return requested;
  }

  // The map destination (W = 1), or W = 0 without one.
  public func AutodriveGoal() -> Vector4 {
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let goal: Vector4;
    if IsDefined(autodrive) && NotEquals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.None) {
      goal = Vector4.Vector3To4(autodrive.GetAutodriveDestination());
      goal.W = 1.0;
      return goal;
    };
    return new Vector4(0.0, 0.0, 0.0, 0.0);
  }

  // The map destination as the pin itself is placed (W = 1), or W = 0
  // without one: the autodrive destination's pin, else the pin the player
  // tracks. GetAutodriveDestination may already be moved onto a road (the
  // game's choice of level); the pin's own position keeps what was meant.
  public func NavDestination() -> Vector4 {
    let game: GameInstance = this.GetGameInstance();
    let mappins: ref<MappinSystem> = GameInstance.GetMappinSystem(game);
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let id: NewMappinID;
    if IsDefined(autodrive) && NotEquals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.None) {
      id = autodrive.GetAutodriveDestinationMappinID();
    };
    if id.value == 0ul && IsDefined(mappins) {
      id = mappins.GetManuallyTrackedMappinID();
    };
    let mappin: ref<IMappin> = id.value != 0ul && IsDefined(mappins) ? mappins.GetMappin(id) : null;
    if IsDefined(mappin) {
      let pos: Vector4 = mappin.GetWorldPosition();
      pos.W = 1.0;
      return pos;
    };
    return this.AutodriveGoal();
  }

  // For the log: what the destination is and where, the pin and the game's
  // autodrive point.
  public func NavDestinationInfo() -> String {
    let game: GameInstance = this.GetGameInstance();
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let pin: Vector4 = this.NavDestination();
    let info: String = s"pin (\(RoundF(pin.X)), \(RoundF(pin.Y)), \(RoundF(pin.Z)))";
    if IsDefined(autodrive) && NotEquals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.None) {
      let point: Vector3 = autodrive.GetAutodriveDestination();
      let kind: String = Equals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.Quest) ? "quest" : "your pin";
      info += s", \(kind), the game's autodrive point (\(RoundF(point.X)), \(RoundF(point.Y)), \(RoundF(point.Z)))";
    };
    return info;
  }

  // For the driving test: a marker on the map and minimap where the test's
  // route ends (the game's GPS still leads to the player's own pin, if any).
  private let testPin: NewMappinID;

  public func ShowTestPin(pos: Vector4) -> Void {
    let mappins: ref<MappinSystem> = GameInstance.GetMappinSystem(this.GetGameInstance());
    if !IsDefined(mappins) {
      return;
    };
    this.HideTestPin();
    let data: MappinData;
    data.mappinType = t"Mappins.QuestStaticMappinDefinition";
    data.variant = gamedataMappinVariant.CustomPositionVariant;
    data.active = true;
    data.visibleThroughWalls = true;
    this.testPin = mappins.RegisterMappin(data, pos);
    mappins.SetMappinActive(this.testPin, true);
  }

  public func HideTestPin() -> Void {
    let none: NewMappinID;
    let mappins: ref<MappinSystem> = GameInstance.GetMappinSystem(this.GetGameInstance());
    if this.testPin.value != 0ul && IsDefined(mappins) {
      mappins.UnregisterMappin(this.testPin);
    };
    this.testPin = none;
  }

  // For the driving test: the car put at pos facing forward (to drive the
  // same route again from the same start). "" or why not.
  public func TeleportVehicle(pos: Vector4, forward: Vector4) -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || !IsDefined(vehicle) {
      return "not in a car";
    };
    GameInstance.GetTeleportationFacility(game).Teleport(vehicle, pos, Vector4.ToRotation(forward));
    return "";
  }

  // The car's physics as its records give them, for the debug panel and
  // the log (and to compare with what Autodrive+ learns): key=value pairs,
  // "" when not in a car. Masses kg, torques Nm, radius m, angle degrees.
  public func CarData() -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || !IsDefined(vehicle) {
      return "";
    };
    let record: wref<Vehicle_Record> = vehicle.GetRecord();
    let size: Vector4 = BCAIOwnDriver.RecordSize(vehicle);
    let out: String = s"car=\(TDBID.ToStringDEBUG(vehicle.GetRecordID()));mass=\(vehicle.GetTotalMass());length=\(size.X);width=\(size.Y)";
    if !IsDefined(record) {
      return out;
    };
    let drive: wref<VehicleDriveModelData_Record> = record.VehDriveModelData();
    if IsDefined(drive) {
      out += s";recordMass=\(drive.Total_mass());wheelTurn=\(drive.MaxWheelTurnDeg());brakingFriction=\(drive.BrakingFrictionFactor());handbrake=\(drive.HandbrakeBrakingTorque())";
      let wheels: wref<VehicleWheelDrivingSetup_Record> = drive.WheelSetup();
      if IsDefined(wheels) {
        let front: wref<VehicleWheelDrivingPreset_Record> = wheels.FrontPreset();
        let back: wref<VehicleWheelDrivingPreset_Record> = wheels.BackPreset();
        if IsDefined(front) {
          out += s";frontFriction=\(front.TireFrictionCoef());frontLat=\(front.FrictionMulLateral());frontLong=\(front.FrictionMulLongitudinal());frontBrake=\(front.MaxBrakingTorque())";
        };
        if IsDefined(back) {
          out += s";backFriction=\(back.TireFrictionCoef());backLat=\(back.FrictionMulLateral());backLong=\(back.FrictionMulLongitudinal());backBrake=\(back.MaxBrakingTorque())";
        };
      };
    };
    let engine: wref<VehicleEngineData_Record> = record.VehEngineData();
    if IsDefined(engine) {
      out += s";engineTorque=\(engine.EngineMaxTorque());gears=\(engine.GetGearsCount());maxRPM=\(engine.MaxRPM())";
    };
    let dims: wref<VehicleWheelDimensionsSetup_Record> = record.VehWheelDimensionsSetup();
    if IsDefined(dims) && IsDefined(dims.FrontPreset()) {
      out += s";tireRadius=\(dims.FrontPreset().TireRadius())";
    };
    return out;
  }

  // Gets the car ready for Autodrive+ (stops AI drive and native autodrive).
  // Returns "" or why not.
  public func BeginOwnDriver(sensorGroup: String) -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || !IsDefined(vehicle) {
      return "not in a vehicle";
    };
    this.EndOwnDriver("restarted");
    this.StopCommandDrive();
    if IsDefined(autodrive) && autodrive.GetAutodriveEnabled() {
      autodrive.SetAutodriveEnabled(false);
    };
    this.bcaiOwnDriver = new BCAIOwnDriver();
    this.bcaiOwnDriver.Begin(game, player, vehicle, StringToName(sensorGroup));
    this.bcaiOwnDriver.people = this.bcaiAutodrivePlusPeople;
    this.bcaiOwnDriverStop = "";
    return "";
  }

  public func EndOwnDriver(why: String) -> Void {
    if this.bcaiHudShown && !IsDefined(this.bcaiDriveCmd) {
      this.ShowAutodriveHud(false, false);
    };
    if IsDefined(this.bcaiOwnDriver) {
      this.bcaiOwnDriver.End();
      this.bcaiOwnDriver = null;
      this.bcaiOwnDriverStop = why;
    };
  }

  public func IsOwnDriving() -> Bool {
    return IsDefined(this.bcaiOwnDriver) && IsDefined(this.bcaiOwnDriver.vehicle);
  }

  public func OwnDriverVehicle() -> ref<VehicleObject> {
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.vehicle : null;
  }

  // Every frame: why Autodrive+ should stop (player input, left the car), or "".
  public func OwnDriverCheck() -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(this.bcaiOwnDriver) {
      return "not running";
    };
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || vehicle != this.bcaiOwnDriver.vehicle {
      return "left the car";
    };
    return this.bcaiOwnDriver.Takeover();
  }

  // Casts Autodrive+'s sensor rays (from the plugin); per ray where it hit or ended.
  public func OwnDriverCast(rays: array<Vector4>) -> array<Vector4> {
    let none: array<Vector4>;
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.Cast(this.GetGameInstance(), rays) : none;
  }

  public func SetOwnDriverObstacle(gap: Float) -> Void {
    if IsDefined(this.bcaiOwnDriver) {
      this.bcaiOwnDriver.obstacle = gap;
    };
  }

  // For the overlay: the road's edges along Autodrive+'s path (left, right
  // pairs) and what its sensor remembers (W 1 vehicle, 2 wall, +2 in the way).
  public func SetOwnDriverDebug(room: array<Vector4>, seen: array<Vector4>) -> Void {
    if IsDefined(this.bcaiDebug) {
      this.bcaiDebug.SetOwnDriver(room, seen);
    };
  }

  public func OwnDriverObstacle() -> Float {
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.obstacle : -1.0;
  }

  public func OwnDriverFront() -> Float {
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.front : 0.0;
  }

  // The car's body (X front, Y rear, Z half width), measured at the start.
  public func OwnDriverBody() -> Vector4 {
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.Body() : new Vector4(0.0, 0.0, 0.0, 0.0);
  }

  // Vehicles near the car as the game knows them (see BCAIOwnDriver.Vehicles).
  public func OwnDriverVehicles() -> array<Vector4> {
    let none: array<Vector4>;
    return IsDefined(this.bcaiOwnDriver) ? this.bcaiOwnDriver.Vehicles(this.GetGameInstance()) : none;
  }

  // How many vehicles each of the game's searches found, for the log.
  public func OwnDriverVehicleSources() -> String {
    if !IsDefined(this.bcaiOwnDriver) {
      return "";
    };
    return "traffic search " + ToString(this.bcaiOwnDriver.fromTraffic) + ", targeting search " + ToString(this.bcaiOwnDriver.fromTargeting);
  }

  public func OwnDriverStop() -> String {
    return this.bcaiOwnDriverStop;
  }

  public func OwnDriverSensorTest() -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let vehicle: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || !IsDefined(vehicle) {
      return "Not in a vehicle";
    };
    return BCAIOwnDriver.SensorTest(game, vehicle);
  }

  public func GetLastStop() -> String {
    return this.bcaiLastStop;
  }

  public func IsCommandDriving() -> Bool {
    return IsDefined(this.bcaiDriveCmd) && IsDefined(this.bcaiDriveVehicle);
  }

  // The running AI drive command (for the native plugin's dumps and scans).
  public func GetDriveCommand() -> ref<AIVehicleDriveToPointAutonomousCommand> {
    return this.bcaiDriveCmd;
  }

  public func StartCommandDrive() -> String {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    let autodrive: ref<AutoDriveSystem> = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    let vehicle: wref<VehicleObject>;
    let cmd: ref<AIVehicleDriveToPointAutonomousCommand>;
    let evt: ref<AICommandEvent>;
    let target: Vector3;
    let indefinitely: Bool = false;

    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || !IsDefined(vehicle) || !IsDefined(vehicle.GetAIComponent()) {
      return "Not in a vehicle";
    };
    this.StopCommandDrive();

    if IsDefined(autodrive) && NotEquals(autodrive.GetAutodriveDestinationType(), gameAutodriveDestinationType.None) {
      target = autodrive.GetAutodriveDestination();
    } else {
      indefinitely = true;
      target = Vector4.Vector4To3(vehicle.GetWorldPosition() + vehicle.GetWorldForward() * 500.0);
    };
    if IsDefined(autodrive) && autodrive.GetAutodriveEnabled() {
      autodrive.SetAutodriveEnabled(false);
    };

    cmd = new AIVehicleDriveToPointAutonomousCommand();
    cmd.needDriver = false;
    cmd.useKinematic = false;
    cmd.targetPosition = target;
    cmd.maxSpeed = this.aggroMaxSpeed;
    cmd.minSpeed = MinF(this.aiMinSpeed, this.aggroMaxSpeed);
    cmd.clearTrafficOnPath = this.aggroClearTraffic;
    cmd.minimumDistanceToTarget = BCAISettings.ArrivalDistance();
    cmd.forcedStartSpeed = -1.0;
    cmd.driveDownTheRoadIndefinitely = indefinitely;
    if !vehicle.GetAIComponent().SendCommand(cmd) {
      evt = new AICommandEvent();
      evt.command = cmd;
      vehicle.QueueEvent(evt);
    };
    this.bcaiDriveCmd = cmd;
    this.bcaiDriveVehicle = vehicle;
    this.bcaiDriveTarget = Vector4.Vector3To4(target);
    this.bcaiDriveIndefinitely = indefinitely;
    this.bcaiSentCap = cmd.maxSpeed;
    this.bcaiStartTime = this.Now();
    this.ResetStabilityGovernor();
    // Like vanilla autodrive: steering, throttle or brake hands control back.
    this.bcaiInputPlayer = player;
    player.RegisterInputListener(this, n"TurnX");
    player.RegisterInputListener(this, n"Accelerate");
    player.RegisterInputListener(this, n"Decelerate");
    player.RegisterInputListener(this, n"HoldAutodrive");
    this.ShowAutodriveHud(true, indefinitely);
    return indefinitely ? "AI drive: ON (down the road)" : "AI drive: ON (to destination)";
  }

  public func StopCommandDrive() -> Void {
    let ai: ref<AIVehicleAgent>;
    if IsDefined(this.bcaiInputPlayer) {
      this.bcaiInputPlayer.UnregisterInputListener(this, n"TurnX");
      this.bcaiInputPlayer.UnregisterInputListener(this, n"Accelerate");
      this.bcaiInputPlayer.UnregisterInputListener(this, n"Decelerate");
      this.bcaiInputPlayer.UnregisterInputListener(this, n"HoldAutodrive");
      this.bcaiInputPlayer = null;
    };
    if this.bcaiHudShown {
      this.ShowAutodriveHud(false, false);
    };
    if IsDefined(this.bcaiDriveCmd) && IsDefined(this.bcaiDriveVehicle) {
      ai = this.bcaiDriveVehicle.GetAIComponent();
    };
    if IsDefined(ai) {
      // CancelCommand only drops queued commands; a running one must be stopped.
      if Equals(ai.GetCommandState(this.bcaiDriveCmd), AICommandState.Executing) {
        ai.StopExecutingCommand(this.bcaiDriveCmd, false);
      } else {
        ai.CancelCommand(this.bcaiDriveCmd);
      };
      ai.CancelOrInterruptCommand(n"AIVehicleDriveToPointAutonomousCommand", false, false);
    };
    this.bcaiDriveCmd = null;
    this.bcaiDriveVehicle = null;
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if !IsDefined(this.bcaiDriveCmd) {
      return false;
    };
    if (ListenerAction.IsAction(action, n"TurnX") || ListenerAction.IsAction(action, n"Accelerate") || ListenerAction.IsAction(action, n"Decelerate"))
        && AbsF(ListenerAction.GetValue(action)) > 0.3 {
      this.bcaiLastStop = "input " + NameToString(ListenerAction.GetName(action)) + "=" + FloatToStringPrec(ListenerAction.GetValue(action), 2);
      if IsDefined(this.bcaiRecorder) {
        this.bcaiRecorder.PlayerTookOver();
      };
      this.StopCommandDrive();
      this.ShowMessage("AI drive: OFF");
    } else {
      // The autodrive button, now that AI drive shows the autodrive HUD.
      if ListenerAction.IsAction(action, n"HoldAutodrive") && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_HOLD_COMPLETE) {
        this.bcaiLastStop = "autodrive button";
        this.bcaiSuppressUntil = this.Now() + 2.0;
        if IsDefined(this.bcaiRecorder) {
          this.bcaiRecorder.PlayerTookOver();
        };
        this.StopCommandDrive();
        this.ShowMessage("AI drive: OFF");
      };
    };
    return false;
  }

  // Native autodrive is off while AI drive runs, so its HUD (wheel icon,
  // minimap highlight, distance and time counters) would hide. The HUD only
  // reads this blackboard, so we fill it in ourselves.
  // (Autodrive+ shows it the same way, through ShowOwnDriverHud.)
  private func ShowAutodriveHud(on: Bool, freeRoam: Bool) -> Void {
    let bb: ref<IBlackboard> = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_AutodriveData);
    let def: ref<UI_AutodriveDataDef> = GetAllBlackboardDefs().UI_AutodriveData;
    this.bcaiHudShown = on;
    if !IsDefined(bb) {
      return;
    };
    if on {
      bb.SetBool(def.FreeRoamEnabled, freeRoam);
      this.bcaiHudSpeed = 0.0;
      this.bcaiHudTime = this.Now();
      this.bcaiHudLength = -1.0;
      this.diagHudOverwrites = 0;
    };
    if NotEquals(bb.GetBool(def.AutoDriveEnabled), on) {
      bb.SetBool(def.AutoDriveEnabled, on);
    };
  }

  // Called every frame from CET while AI drive runs, so the counters stay
  // steady even if native code writes its own (zero) values in between.
  public func RefreshAutodriveHud() -> Void {
    let bb: ref<IBlackboard>;
    let def: ref<UI_AutodriveDataDef>;
    let remaining: Float;
    let now: Float;
    let k: Float;
    if !this.IsCommandDriving() {
      return;
    };
    bb = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_AutodriveData);
    def = GetAllBlackboardDefs().UI_AutodriveData;
    if !IsDefined(bb) {
      return;
    };
    // Vanilla's autodrive state is this flag, so vanilla switching autodrive
    // off (combat, drawn weapon, heavy damage, quest lock, or the player via
    // the HUD) clears it. Treat that like vanilla does and hand back control.
    if !bb.GetBool(def.AutoDriveEnabled) {
      if this.Now() - this.bcaiStartTime < 0.5 {
        bb.SetBool(def.AutoDriveEnabled, true);
      } else {
        this.bcaiLastStop = "vanilla autodrive stopped (combat, weapon, damage, quest or player)";
        this.StopCommandDrive();
        this.ShowMessage("AI drive: OFF");
        return;
      };
    };
    if this.bcaiDriveIndefinitely {
      return;
    };
    if this.bcaiHudLength >= 0.0 && AbsF(bb.GetFloat(def.DestinationRemainingLength) - this.bcaiHudLength) > 0.01 {
      this.diagHudOverwrites += 1;
    };
    // Average the speed over ~5 s so the time estimate doesn't jump around.
    now = this.Now();
    k = ClampF((now - this.bcaiHudTime) / 5.0, 0.0, 1.0);
    this.bcaiHudTime = now;
    if this.bcaiHudSpeed <= 0.0 {
      this.bcaiHudSpeed = AbsF(this.bcaiDriveVehicle.GetCurrentSpeed());
    } else {
      this.bcaiHudSpeed += (AbsF(this.bcaiDriveVehicle.GetCurrentSpeed()) - this.bcaiHudSpeed) * k;
    };
    remaining = MaxF(Vector4.Distance(this.bcaiDriveVehicle.GetWorldPosition(), this.bcaiDriveTarget) - BCAISettings.ArrivalDistance(), 0.0);
    this.bcaiHudLength = remaining;
    bb.SetFloat(def.DestinationRemainingLength, remaining);
    bb.SetFloat(def.DestinationRemainingTime, remaining / MaxF(this.bcaiHudSpeed, 5.0));
  }

  // The game's autodrive HUD while Autodrive+ drives (wheel icon, minimap
  // highlight, distance and time counters), as for AI drive above.
  public func ShowOwnDriverHud(on: Bool, freeRoam: Bool) -> Void {
    if on || this.bcaiHudShown {
      this.ShowAutodriveHud(on, freeRoam);
    };
    this.bcaiStartTime = this.Now();
  }

  // Every frame from CET while Autodrive+ drives, with the metres left (< 0:
  // no destination). False once the game cleared its autodrive flag
  // (combat, a drawn weapon, heavy damage, a quest lock): hand back control
  // as vanilla does.
  public func RefreshOwnDriverHud(remaining: Float) -> Bool {
    let bb: ref<IBlackboard> = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_AutodriveData);
    let def: ref<UI_AutodriveDataDef> = GetAllBlackboardDefs().UI_AutodriveData;
    let now: Float = this.Now();
    let k: Float;
    let speed: Float;
    if !this.bcaiHudShown || !IsDefined(bb) || !IsDefined(this.bcaiOwnDriver) || !IsDefined(this.bcaiOwnDriver.vehicle) {
      return true;
    };
    if !bb.GetBool(def.AutoDriveEnabled) {
      if now - this.bcaiStartTime < 0.5 {
        bb.SetBool(def.AutoDriveEnabled, true);
      } else {
        this.ShowAutodriveHud(false, false);
        return false;
      };
    };
    if remaining < 0.0 {
      return true;
    };
    // (The speed averaged over about 5 s, so the time doesn't jump.)
    speed = AbsF(this.bcaiOwnDriver.vehicle.GetCurrentSpeed());
    k = ClampF((now - this.bcaiHudTime) / 5.0, 0.0, 1.0);
    this.bcaiHudTime = now;
    this.bcaiHudSpeed = this.bcaiHudSpeed <= 0.0 ? speed : this.bcaiHudSpeed + (speed - this.bcaiHudSpeed) * k;
    bb.SetFloat(def.DestinationRemainingLength, remaining);
    bb.SetFloat(def.DestinationRemainingTime, remaining / MaxF(this.bcaiHudSpeed, 5.0));
    return true;
  }

  public static func ArrivalDistance() -> Float {
    return 15.0;
  }

  // Called a few times a second from CET. Slows the car down on approach so
  // it can actually stop at the destination, brakes to a stop on arrival, and
  // drops the command once the player leaves the car or it has finished.
  public func UpdateCommandDrive() -> Bool {
    let game: GameInstance = this.GetGameInstance();
    let player: ref<PlayerPuppet>;
    let vehicle: wref<VehicleObject>;
    let current: ref<AIVehicleDriveToPointAutonomousCommand>;
    let update: ref<DriveToPointAutonomousUpdate>;
    let remaining: Float;
    let cap: Float;
    if !IsDefined(this.bcaiDriveCmd) {
      return false;
    };
    // Don't touch native autodrive here: its "enabled" state is the HUD
    // blackboard flag we keep set, and switching it off runs vanilla's whole
    // shutdown (camera reset and all). It was disabled once at the start.
    player = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject() as PlayerPuppet;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, vehicle) || vehicle != this.bcaiDriveVehicle {
      this.bcaiLastStop = "left the vehicle";
      this.StopCommandDrive();
      return true;
    };
    // An update may replace the running command; follow the new one.
    current = vehicle.GetAIComponent().GetEnqueuedOrExecutingCommand(n"AIVehicleDriveToPointAutonomousCommand", false) as AIVehicleDriveToPointAutonomousCommand;
    if IsDefined(current) {
      this.bcaiDriveCmd = current;
    } else {
      if Equals(this.bcaiDriveCmd.state, AICommandState.NotExecuting) || Equals(this.bcaiDriveCmd.state, AICommandState.Enqueued) {
        return false; // not picked up yet
      };
      if Equals(this.bcaiDriveCmd.state, AICommandState.Success) {
        this.BrakeToStop(vehicle, "AI drive: arrived");
      } else {
        this.bcaiLastStop = "command ended, state " + ToString(EnumInt(this.bcaiDriveCmd.state));
        this.StopCommandDrive();
      };
      return true;
    };
    cap = MinF(this.aggroMaxSpeed, this.UpdateStabilityCap(vehicle));
    if !this.bcaiDriveIndefinitely {
      remaining = Vector4.Distance(vehicle.GetWorldPosition(), this.bcaiDriveTarget) - BCAISettings.ArrivalDistance();
      if remaining < 5.0 {
        this.BrakeToStop(vehicle, "AI drive: arrived");
        return true;
      };
      // Fastest speed that can still stop in the remaining distance at a gentle
      // 4 m/s^2, so it is never carrying motorway speed into the destination.
      cap = MinF(cap, SqrtF(2.0 * 4.0 * remaining));
    };
    if AbsF(cap - this.bcaiSentCap) < 2.0 {
      return false;
    };
    update = new DriveToPointAutonomousUpdate();
    update.targetPosition = this.bcaiDriveTarget;
    update.driveDownTheRoadIndefinitely = this.bcaiDriveIndefinitely;
    update.minimumDistanceToTarget = BCAISettings.ArrivalDistance();
    update.maxSpeed = cap;
    update.minSpeed = MinF(this.aiMinSpeed, cap * 0.5);
    update.clearTrafficOnPath = this.aggroClearTraffic;
    vehicle.GetAIComponent().SetDriveToPointAutonomousUpdate(update);
    this.bcaiSentCap = cap;
    return false;
  }

  // --- Stability governor ---
  // The native driver plans speed for bends only: it doesn't notice the car
  // sliding, weaving after an overcorrection, or a crest it will fly off. This
  // watches for those and lowers the speed cap for a while, then lets it
  // recover gradually.

  public let aiGovernor: Bool;
  private let bcaiStabilityCap: Float;
  private let bcaiCalmUntil: Float;
  private let bcaiGovPrevFwd: Vector4;
  private let bcaiGovPrevTime: Float;
  private let bcaiLastYawSign: Float;
  private let bcaiLastYawTime: Float;
  public let bcaiGovernorReason: String;
  public let bcaiGovernorEvents: Int32;
  private let bcaiTrafficCap: Float;
  private let bcaiTrafficDist: Float;

  // Trouble spots: places where AI drive snapped the wheel or cornered far
  // beyond the tyres' grip with no car ahead. The lane path through some
  // junctions has a kink the native driver follows at any speed, always at
  // the same spot, and scripts can't see or fix the path. So the spot is
  // remembered (CET saves the list) and passed slowly from then on.
  public let aiTroubleSpots: Bool;
  private let troubleSpotSpeed: Float;
  private let troublePos: array<Vector4>;
  private let troubleDir: array<Vector4>;
  private let troubleNew: array<String>;
  private let bcaiSpotCap: Float;
  private let bcaiSpotDist: Float;
  private let bcaiGovPrevPos: Vector4;
  private let bcaiNoLearnUntil: Float;
  private let bcaiCarSeenAt: Float;

  // Route planner (native plugin only): CET passes the speed that lets every
  // bend in the AI's route ahead be taken at the set grip, from the route's
  // lane geometry. Unlike trouble spots it covers junction jogs the car has
  // never driven. Ignored once stale (plugin gone, CET stopped updating).
  private let bcaiRouteCap: Float;
  private let bcaiRouteDist: Float;
  private let bcaiRouteRadius: Float;
  private let bcaiRouteAt: Float;

  public func SetStabilityGovernor(on: Bool) -> Void {
    this.aiGovernor = on;
  }

  public func GetGovernorState() -> String {
    if !this.aiGovernor {
      return "governor off";
    };
    return "governor cap " + FloatToStringPrec(this.bcaiStabilityCap * 3.6, 0) + " km/h"
      + (NotEquals(this.bcaiGovernorReason, "") ? " (last: " + this.bcaiGovernorReason + ")" : "")
      + (this.bcaiSpotCap < this.aggroMaxSpeed ? ", trouble spot " + FloatToStringPrec(this.bcaiSpotDist, 0) + " m: " + FloatToStringPrec(this.bcaiSpotCap * 3.6, 0) + " km/h" : "")
      + (this.RouteCap() < this.aggroMaxSpeed ? ", route bend " + FloatToStringPrec(this.bcaiRouteDist, 0) + " m (radius " + FloatToStringPrec(this.bcaiRouteRadius, 0) + " m): " + FloatToStringPrec(this.RouteCap() * 3.6, 0) + " km/h" : "")
      + ", " + ToString(ArraySize(this.troublePos)) + " trouble spots"
      + (this.bcaiTrafficCap < this.bcaiStabilityCap ? ", car ahead " + FloatToStringPrec(this.bcaiTrafficDist, 0) + " m: " + FloatToStringPrec(this.bcaiTrafficCap * 3.6, 0) + " km/h" : "")
      + ", events " + ToString(this.bcaiGovernorEvents);
  }

  // cap < 0 = no route.
  public func SetRouteCap(cap: Float, distance: Float, radius: Float) -> Void {
    this.bcaiRouteCap = cap < 0.0 ? this.aggroMaxSpeed : MaxF(cap, this.aiMinSpeed);
    this.bcaiRouteDist = distance;
    this.bcaiRouteRadius = radius;
    this.bcaiRouteAt = this.Now();
  }

  private func RouteCap() -> Float {
    if this.bcaiRouteAt <= 0.0 || this.Now() - this.bcaiRouteAt > 1.0 {
      return this.aggroMaxSpeed;
    };
    return this.bcaiRouteCap;
  }

  public func SetTroubleSpots(on: Bool, speed: Float) -> Void {
    this.aiTroubleSpots = on;
    this.troubleSpotSpeed = speed;
  }

  public func TroubleSpotCount() -> Int32 {
    return ArraySize(this.troublePos);
  }

  public func ClearTroubleSpots() -> Void {
    ArrayClear(this.troublePos);
    ArrayClear(this.troubleDir);
  }

  public func AddTroubleSpot(x: Float, y: Float, z: Float, dx: Float, dy: Float) -> Void {
    ArrayPush(this.troublePos, new Vector4(x, y, z, 1.0));
    ArrayPush(this.troubleDir, Vector4.Normalize(new Vector4(dx, dy, 0.0, 0.0)));
  }

  // Spots learned since the last call, as "x,y,z,dx,dy", for CET to save.
  public func TakeNewTroubleSpots() -> array<String> {
    let out: array<String> = this.troubleNew;
    ArrayClear(this.troubleNew);
    return out;
  }

  private func LearnTroubleSpot(pos: Vector4, dir: Vector4) -> Void {
    let i: Int32 = 0;
    while i < ArraySize(this.troublePos) {
      if Vector4.Distance(this.troublePos[i], pos) < 25.0 && Vector4.Dot(this.troubleDir[i], dir) > 0.7 {
        return; // already known
      };
      i += 1;
    };
    this.AddTroubleSpot(pos.X, pos.Y, pos.Z, dir.X, dir.Y);
    ArrayPush(this.troubleNew, FloatToStringPrec(pos.X, 2) + "," + FloatToStringPrec(pos.Y, 2) + ","
      + FloatToStringPrec(pos.Z, 2) + "," + FloatToStringPrec(dir.X, 3) + "," + FloatToStringPrec(dir.Y, 3));
  }

  // Speed allowed by the trouble spots ahead: reach the spot speed 15 m before
  // a spot (braking at ~4 m/s^2 on the way) and hold it until 35 m past, since
  // a bad lane path usually jogs out and back again within a few tens of metres.
  private func TroubleSpotCap(pos: Vector4, flatFwd: Vector4) -> Float {
    let left: Vector4 = Vector4.Cross(new Vector4(0.0, 0.0, 1.0, 0.0), flatFwd);
    let cap: Float = this.aggroMaxSpeed;
    let d: Vector4;
    let along: Float;
    let allowed: Float;
    let i: Int32 = 0;
    this.bcaiSpotDist = -1.0;
    while i < ArraySize(this.troublePos) {
      d = this.troublePos[i] - pos;
      along = Vector4.Dot(d, flatFwd);
      if along > -35.0 && along < 500.0 && AbsF(Vector4.Dot(d, left)) < 10.0 && AbsF(d.Z) < 6.0
          && Vector4.Dot(this.troubleDir[i], flatFwd) > 0.7 {
        allowed = SqrtF(this.troubleSpotSpeed * this.troubleSpotSpeed + 2.0 * 4.0 * MaxF(along - 15.0, 0.0));
        if allowed < cap {
          cap = allowed;
          this.bcaiSpotDist = along;
        };
      };
      i += 1;
    };
    return cap;
  }

  // The governor's current cap and why, for the recorder ("" when not limiting).
  private func GovernorCap() -> Float {
    return MinF(MinF(MinF(this.bcaiStabilityCap, this.bcaiTrafficCap), this.bcaiSpotCap), this.RouteCap());
  }

  private func GovernorLimitReason() -> String {
    let route: Float = this.RouteCap();
    if route < this.bcaiStabilityCap && route <= this.bcaiTrafficCap && route <= this.bcaiSpotCap {
      return "route bend " + FloatToStringPrec(this.bcaiRouteDist, 0) + " m r" + FloatToStringPrec(this.bcaiRouteRadius, 0);
    };
    if this.bcaiSpotCap < this.bcaiStabilityCap && this.bcaiSpotCap <= this.bcaiTrafficCap {
      return "trouble spot " + FloatToStringPrec(this.bcaiSpotDist, 0) + " m";
    };
    if this.bcaiTrafficCap < this.bcaiStabilityCap {
      return "car ahead " + FloatToStringPrec(this.bcaiTrafficDist, 0) + " m";
    };
    return this.bcaiStabilityCap < this.aggroMaxSpeed - 0.5 ? this.bcaiGovernorReason : "";
  }

  private func ResetStabilityGovernor() -> Void {
    this.bcaiStabilityCap = this.aggroMaxSpeed;
    this.bcaiTrafficCap = this.aggroMaxSpeed;
    this.bcaiTrafficDist = -1.0;
    this.bcaiSpotCap = this.aggroMaxSpeed;
    this.bcaiSpotDist = -1.0;
    this.bcaiRouteAt = 0.0;
    this.bcaiCalmUntil = 0.0;
    this.bcaiNoLearnUntil = 0.0;
    this.bcaiCarSeenAt = -100.0;
    this.bcaiGovPrevTime = 0.0;
    this.bcaiLastYawTime = 0.0;
    this.bcaiGovernorReason = "";
    this.bcaiGovernorEvents = 0;
  }

  // Height of the ground below a point (Z), or a huge negative if none within depth.
  private func GroundZ(point: Vector4, depth: Float) -> Float {
    let hit: TraceResult;
    GameInstance.GetSpatialQueriesSystem(this.GetGameInstance()).SyncRaycastByCollisionPreset(
      point + new Vector4(0.0, 0.0, 2.0, 0.0), point - new Vector4(0.0, 0.0, depth, 0.0), n"World Static", hit);
    if !TraceResult.IsValid(hit) {
      return -100000.0;
    };
    return hit.position.Z;
  }

  private func UpdateStabilityCap(vehicle: wref<VehicleObject>) -> Float {
    let now: Float = this.Now();
    let pos: Vector4 = vehicle.GetWorldPosition();
    let fwd: Vector4 = vehicle.GetWorldForward();
    let vel: Vector4 = vehicle.GetLinearVelocity();
    let speed: Float = Vector4.Length(vel);
    let flatFwd: Vector4 = Vector4.Normalize(new Vector4(fwd.X, fwd.Y, 0.0, 0.0));
    let lateral: Float = AbsF(Vector4.Dot(vel, vehicle.GetWorldRight()));
    let yaw: Float = 0.0;
    let dt: Float = now - this.bcaiGovPrevTime;
    let ahead: Float;
    let expectedZ: Float;
    let groundAhead: Float;
    let drop: Float;
    let turn: Float;
    let dir: Vector4;
    let target: Float = -1.0;
    let spin: Bool;
    let reason: String;
    if !this.aiGovernor {
      this.bcaiTrafficCap = this.aggroMaxSpeed;
      this.bcaiSpotCap = this.aggroMaxSpeed;
      return this.aggroMaxSpeed;
    };
    // A car anywhere near our path in the last 1.5 s means a hard turn was a
    // dodge (the car has often left the rays by the time the turn peaks).
    if this.aiTroubleSpots && this.CarAheadDistance(pos, flatFwd, 60.0, 12.0) >= 0.0 {
      this.bcaiCarSeenAt = now;
    };
    if this.bcaiGovPrevTime > 0.0 && dt > 0.05 && dt < 1.0 {
      yaw = Vector4.Cross(this.bcaiGovPrevFwd, flatFwd).Z / dt;
      // Turning harder than ~1.8 g with no car ahead and not already sliding:
      // a path kink or a bend it took far too fast. Remember where it began.
      // Not while recovering from a slide (5 s), and not when dodging a car.
      if this.aiTroubleSpots && speed > 16.0 && AbsF(yaw) * speed > 17.5 && now > this.bcaiNoLearnUntil {
        if lateral < speed * 0.3 && now - this.bcaiCarSeenAt > 1.5 {
          this.LearnTroubleSpot(this.bcaiGovPrevPos, this.bcaiGovPrevFwd);
        };
        this.bcaiNoLearnUntil = now + 5.0;
      };
    };
    this.bcaiGovPrevFwd = flatFwd;
    this.bcaiGovPrevPos = pos;
    this.bcaiGovPrevTime = now;

    // Spinning: travelling more sideways than forwards, or rotating fast.
    // Crawl until it has settled so it can recover instead of flailing.
    spin = speed > 6.0 && (lateral > speed * 0.7 || AbsF(yaw) > Deg2Rad(90.0));
    if spin {
      target = 0.0;
    };
    // Sliding: sideways speed well beyond what normal cornering produces.
    if target < 0.0 && speed > 12.0 && lateral > MaxF(2.0, speed * 0.08) {
      target = speed * 0.8;
      reason = "sliding " + FloatToStringPrec(lateral, 1) + " m/s";
    };
    // Weaving: hard turns that keep flipping direction = overcorrection.
    if speed > 20.0 && AbsF(yaw) > Deg2Rad(12.0) {
      if this.bcaiLastYawSign != 0.0 && this.bcaiLastYawSign * yaw < 0.0 && now - this.bcaiLastYawTime < 1.5 {
        target = target < 0.0 ? speed * 0.8 : MinF(target, speed * 0.8);
        reason = "weaving";
      };
      this.bcaiLastYawSign = yaw > 0.0 ? 1.0 : -1.0;
      this.bcaiLastYawTime = now;
    };
    // Airborne: nothing under the car. Keep it from landing at full throttle.
    if speed > 10.0 && pos.Z - this.GroundZ(pos, 3.0) > 1.0 {
      target = target < 0.0 ? speed * 0.85 : MinF(target, speed * 0.85);
      reason = "airborne";
    };
    // Crest ahead: the road ~1 s ahead drops away below the line the car is
    // travelling on, so it would take off at this speed. The probe follows
    // the current turn so it stays on a curving road, and drops of more than
    // 8 m are ignored: that's the ground beside an elevated road, not a crest.
    if speed > 25.0 {
      ahead = ClampF(speed, 20.0, 60.0);
      turn = yaw * (ahead / speed) * 0.5;
      dir = flatFwd * CosF(turn) + Vector4.Cross(new Vector4(0.0, 0.0, 1.0, 0.0), flatFwd) * SinF(turn);
      expectedZ = pos.Z + fwd.Z * ahead;
      groundAhead = this.GroundZ(pos + dir * ahead + new Vector4(0.0, 0.0, MaxF(fwd.Z * ahead, 0.0), 0.0), 25.0);
      drop = expectedZ - groundAhead;
      if groundAhead > -99999.0 && drop > 1.5 + ahead * 0.04 && drop < 8.0 {
        target = target < 0.0 ? 25.0 : MinF(target, 25.0);
        reason = "crest ahead";
      };
    };

    if spin {
      reason = "spin";
    };
    // Real loss of control only: some cars (the Javelina) slide a little all
    // the time, which must not block learning.
    if spin || lateral > speed * 0.3 {
      this.bcaiNoLearnUntil = MaxF(this.bcaiNoLearnUntil, now + 5.0);
    };
    if target >= 0.0 {
      target = MaxF(target, this.aiMinSpeed + 3.0);
      if target < this.bcaiStabilityCap - 1.0 {
        this.bcaiGovernorEvents += 1;
      };
      this.bcaiStabilityCap = MinF(this.bcaiStabilityCap, target);
      this.bcaiGovernorReason = reason;
      this.bcaiCalmUntil = MaxF(this.bcaiCalmUntil, now + (spin ? 3.0 : 2.0));
    } else {
      if now > this.bcaiCalmUntil {
        // Recover ~3 m/s per second once things have been calm for 2 s.
        this.bcaiStabilityCap = MinF(this.aggroMaxSpeed, this.bcaiStabilityCap + 3.0 * MaxF(dt, 0.25));
      };
    };
    // Car ahead in our lane: keep a 2 s gap. Recordings show the native
    // driver dodging traffic with a near-instant steering snap, which spins the
    // car at motorway speed, so arrive slower and dodge at a survivable speed.
    // Not latched like the caps above: it lifts as soon as the lane is clear.
    this.bcaiTrafficDist = this.CarAheadDistance(pos, flatFwd, MaxF(25.0, speed * 2.5), 4.0);
    this.bcaiTrafficCap = this.aggroMaxSpeed;
    if this.bcaiTrafficDist >= 0.0 {
      this.bcaiTrafficCap = MaxF(this.bcaiTrafficDist / 2.0, this.aiMinSpeed + 3.0);
    };
    this.bcaiSpotCap = this.aiTroubleSpots ? this.TroubleSpotCap(pos, flatFwd) : this.aggroMaxSpeed;
    return this.GovernorCap();
  }

  // Distance to the nearest vehicle ahead within +-halfAngle degrees (five
  // rays; 4 degrees is ~2 m either side of the centre line at 30 m), or -1.
  private func CarAheadDistance(pos: Vector4, flatFwd: Vector4, range: Float, halfAngle: Float) -> Float {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(this.GetGameInstance());
    let left: Vector4 = Vector4.Cross(new Vector4(0.0, 0.0, 1.0, 0.0), flatFwd);
    let origin: Vector4 = pos + flatFwd * 3.0 + new Vector4(0.0, 0.0, 0.8, 0.0);
    let nearest: Float = -1.0;
    let hit: TraceResult;
    let noHit: TraceResult;
    let dist: Float;
    let a: Float;
    let i: Int32 = -2;
    while i <= 2 {
      a = Deg2Rad(halfAngle * 0.5 * Cast<Float>(i));
      hit = noHit;
      sqs.SyncRaycastByCollisionPreset(origin, origin + (flatFwd * CosF(a) + left * SinF(a)) * range, n"Vehicle Chassis", hit);
      if TraceResult.IsValid(hit) {
        dist = Vector4.Distance(origin, Vector4.Vector3To4(hit.position));
        if nearest < 0.0 || dist < nearest {
          nearest = dist;
        };
      };
      i += 1;
    };
    return nearest;
  }

  private func BrakeToStop(vehicle: wref<VehicleObject>, message: String) -> Void {
    this.bcaiLastStop = message;
    this.StopCommandDrive();
    if IsDefined(vehicle) {
      vehicle.ForceBrakesUntilStoppedOrFor(10.0);
    };
    this.ShowMessage(message);
  }
}
