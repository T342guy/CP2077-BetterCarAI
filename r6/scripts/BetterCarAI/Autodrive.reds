// Better Car AI - autodrive command parameters.
//
// Vanilla autodrive only hands the native driver a destination: the command's
// speed cap, traffic clearing and look-ahead mode stay at their defaults. The
// behaviour delegate below carries those values into the drive node, and the
// DriveToPointAutonomousUpdate pushes them into an already running command.

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaSaved: Bool;

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaMaxSpeed: Float;

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaClearTraffic: Bool;

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaForceGreenLights: Bool;

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaSpeedBasedLookup: Bool;

@addField(AIDriveCommandsDelegate)
private let bcaiVanillaBoostStats: Bool;

// Params last sent through an update; -1 forces the first send.
@addField(AIDriveCommandsDelegate)
private let bcaiSentMaxSpeed: Float;

@addField(AIDriveCommandsDelegate)
private let bcaiSentClearTraffic: Bool;

@addMethod(AIDriveCommandsDelegate)
private final func BCAI_ApplyAutodriveParams(game: GameInstance) -> Void {
  let settings: ref<BCAISettings> = BCAISettings.Get(game);
  if !IsDefined(settings) {
    return;
  };
  if !this.bcaiVanillaSaved {
    this.bcaiVanillaSaved = true;
    this.bcaiVanillaMaxSpeed = this.maxSpeed;
    this.bcaiVanillaClearTraffic = this.clearTrafficOnPath;
    this.bcaiVanillaForceGreenLights = this.forceGreenLights;
    this.bcaiVanillaSpeedBasedLookup = this.useSpeedBasedLookupRange;
    this.bcaiVanillaBoostStats = this.boostDrivingStats;
  };
  if settings.aggressiveMode {
    this.clearTrafficOnPath = settings.aggroClearTraffic;
    this.forceGreenLights = settings.aggroRunLights;
    this.useSpeedBasedLookupRange = true;
    this.boostDrivingStats = true;
  } else {
    this.clearTrafficOnPath = this.bcaiVanillaClearTraffic;
    this.forceGreenLights = this.bcaiVanillaForceGreenLights;
    this.useSpeedBasedLookupRange = this.bcaiVanillaSpeedBasedLookup;
    this.boostDrivingStats = this.bcaiVanillaBoostStats;
  };
  this.maxSpeed = settings.GetAutodriveMaxSpeed() > 0.0 ? settings.GetAutodriveMaxSpeed() : this.bcaiVanillaMaxSpeed;
  settings.diagVanillaMaxSpeed = this.bcaiVanillaMaxSpeed;
  settings.diagAppliedMaxSpeed = this.maxSpeed;
}

@wrapMethod(AIDriveCommandsDelegate)
public final func DoStartDriveToPointAutonomousAutodrive(context: ScriptExecutionContext) -> Bool {
  let result: Bool = wrappedMethod(context);
  let game: GameInstance = ScriptExecutionContext.GetOwner(context).GetGame();
  this.BCAI_ApplyAutodriveParams(game);
  this.bcaiSentMaxSpeed = -1.0;
  if IsDefined(BCAISettings.Get(game)) {
    BCAISettings.Get(game).diagStarts += 1;
  };
  return result;
}

@wrapMethod(AIDriveCommandsDelegate)
public final func DoUpdateDriveToPointAutonomousAutodrive(context: ScriptExecutionContext) -> Bool {
  let result: Bool = wrappedMethod(context);
  let owner: ref<gamePuppet> = ScriptExecutionContext.GetOwner(context);
  let game: GameInstance = owner.GetGame();
  let vehicle: wref<VehicleObject>;
  let autoDriveSystem: ref<AutoDriveSystem>;
  let update: ref<DriveToPointAutonomousUpdate>;

  this.BCAI_ApplyAutodriveParams(game);
  if IsDefined(BCAISettings.Get(game)) {
    BCAISettings.Get(game).diagUpdates += 1;
  };
  if this.bcaiSentMaxSpeed == this.maxSpeed && Equals(this.bcaiSentClearTraffic, this.clearTrafficOnPath) {
    return result;
  };
  VehicleComponent.GetVehicle(game, owner, vehicle);
  autoDriveSystem = GameInstance.GetScriptableSystemsContainer(game).Get(n"AutoDriveSystem") as AutoDriveSystem;
  if !IsDefined(vehicle) || !IsDefined(autoDriveSystem) {
    return result;
  };
  update = new DriveToPointAutonomousUpdate();
  update.targetPosition = Cast<Vector4>(autoDriveSystem.GetAutodriveDestination());
  update.maxSpeed = this.maxSpeed;
  update.minSpeed = this.minSpeed;
  update.clearTrafficOnPath = this.clearTrafficOnPath;
  vehicle.GetAIComponent().SetDriveToPointAutonomousUpdate(update);
  this.bcaiSentMaxSpeed = this.maxSpeed;
  this.bcaiSentClearTraffic = this.clearTrafficOnPath;
  BCAISettings.Get(game).diagUpdatesSent += 1;
  return result;
}

// While AI drive runs, vanilla thinks autodrive is on (it reads the HUD flag
// AI drive keeps set), so any bump would switch it off. AI drive copes with
// bumps itself, so ignore those while it drives; everything else (combat,
// weapons, damage, quests) still stops it.
@wrapMethod(AutoDriveSystem)
private final func OnAutoDriveHitRequest(request: ref<AutoDriveHitRequest>) -> Void {
  let settings: ref<BCAISettings> = BCAISettings.Get(this.GetGameInstance());
  if IsDefined(settings) && settings.IsCommandDriving() {
    return;
  };
  wrappedMethod(request);
}
