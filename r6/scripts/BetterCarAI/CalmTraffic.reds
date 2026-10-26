// Better Car AI - NPC drivers, and the people around them, take a light tap
// in their stride. A tapped driver honks; tapped again soon after, honks and
// flashes the lights; keep tapping the same car and its driver loses patience
// (the game's own reaction follows: driving off in a panic, getting out, or
// with aggressive crowds on, getting out to fight). Real crashes, gunfire,
// drawn weapons, combat and the police go through the vanilla logic.
//
// How the game reacts to a knock (2.5 scripts):
// - VehicleObject.HandleTrafficBump (traffic cars): above
//   AIGeneralSettings.minUnconsciousImpact (6.5 m/s) a roll for knocking the
//   driver out; else a voice line; and a honk.
// - VehicleComponent.OnGridDestruction (any car that dents): the dented car
//   calls CrowdIllegalAction to everyone within 10 m (a small dent) or
//   VehicleHit within 5 m (a bigger one). This is what made drivers and
//   people nearby panic from a tap.
// - ReactionManagerComponent.HandleCrowdReaction: a mounted NPC's crowd stim
//   becomes the vehicle's HandleReactionEvent (panic driving, bailing out,
//   fear inside), or aggressive-crowd combat; one on foot starts its fear.

@addField(VehicleObject)
private let m_bcaiLastHardImpactTime: Float;

@addField(VehicleObject)
private let m_bcaiTaps: Int32;

@addField(VehicleObject)
private let m_bcaiLastTapTime: Float;

@addField(VehicleObject)
private let m_bcaiAngryUntil: Float;

@addField(VehicleObject)
private let m_bcaiLastDent: Float;

@addMethod(VehicleObject)
public final func BCAI_Now() -> Float {
  return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGame()));
}

@addMethod(VehicleObject)
public final func BCAI_HadRecentHardImpact(window: Float) -> Bool {
  if this.m_bcaiLastHardImpactTime <= 0.0 {
    return false;
  };
  return this.BCAI_Now() - this.m_bcaiLastHardImpactTime <= window;
}

@addMethod(VehicleObject)
public final func BCAI_MarkHardImpact() -> Void {
  this.m_bcaiLastHardImpactTime = this.BCAI_Now();
}

// Lost patience with being tapped: its driver reacts the game's way for a while.
@addMethod(VehicleObject)
public final func BCAI_IsAngry() -> Bool {
  return this.BCAI_Now() < this.m_bcaiAngryUntil;
}

@addMethod(VehicleObject)
public final func BCAI_LastDent() -> Float {
  return this.m_bcaiLastDent;
}

@addMethod(VehicleObject)
public final func BCAI_SetDent(dent: Float) -> Void {
  this.m_bcaiLastDent = dent;
}

// For the log: the car's name, or its record.
@addMethod(VehicleObject)
public final func BCAI_Name() -> String {
  let name: String = this.GetDisplayName();
  if StrLen(name) == 0 {
    name = TDBID.ToStringDEBUG(this.GetRecordID());
  };
  return this.IsPlayerVehicle() ? "your " + name : name;
}

@addMethod(VehicleComponent)
public final func BCAI_HonkAndFlash() -> Void {
  this.HonkAndFlash();
}

// Any knock on any car (yours too, which isn't traffic): a hard one is
// remembered on both cars, so the reactions to it go through.
@wrapMethod(VehicleObject)
protected cb func OnVehicleBumpEvent(evt: ref<VehicleBumpEvent>) -> Bool {
  let settings: ref<BCAISettings> = BCAISettings.Get(this.GetGame());
  if IsDefined(settings) && settings.calmTrafficEnabled && evt.impactVelocityChange >= settings.panicImpactThreshold {
    if !this.BCAI_HadRecentHardImpact(1.0) {
      settings.NoteTraffic(s"crash: \(this.BCAI_Name()) hit, speed change \(RoundF(evt.impactVelocityChange * 3.6)) km/h (the game's reactions)");
    };
    this.BCAI_MarkHardImpact();
    if IsDefined(evt.hitVehicle) {
      evt.hitVehicle.BCAI_MarkHardImpact();
    };
  };
  return wrappedMethod(evt);
}

// A dent: remembered (the stims it sends are judged by it), and a big one
// counts as a crash (a wall has no bump event to tell).
@wrapMethod(VehicleComponent)
protected cb func OnGridDestruction(evt: ref<VehicleGridDestructionEvent>) -> Bool {
  let vehicle: ref<VehicleObject> = this.GetVehicle();
  let settings: ref<BCAISettings> = BCAISettings.Get(vehicle.GetGame());
  if IsDefined(settings) && settings.calmTrafficEnabled {
    let biggest: Float = 0.0;
    let i: Int32 = 0;
    while i < ArraySize(evt.desiredChange) {
      biggest = MaxF(biggest, evt.desiredChange[i]);
      i += 1;
    };
    vehicle.BCAI_SetDent(biggest);
    if biggest >= settings.crashDent {
      if !vehicle.BCAI_HadRecentHardImpact(1.0) {
        settings.NoteTraffic(s"crash: \(vehicle.BCAI_Name()) dented \(biggest) (the game's reactions)");
      };
      vehicle.BCAI_MarkHardImpact();
    };
  };
  return wrappedMethod(evt);
}

// Vanilla: any bump above AIGeneralSettings.minUnconsciousImpact (6.5 m/s)
// rolls for knocking the driver out, otherwise a voice line and a honk.
@wrapMethod(VehicleObject)
private final func HandleTrafficBump(impact: Float) -> Void {
  let settings: ref<BCAISettings> = BCAISettings.Get(this.GetGame());
  if !IsDefined(settings) || !settings.calmTrafficEnabled || this.IsPrevention() {
    wrappedMethod(impact);
    return;
  };
  if impact >= settings.panicImpactThreshold || settings.IsPlayerInCombat() {
    this.BCAI_MarkHardImpact();
    wrappedMethod(impact);
    return;
  };
  this.BCAI_Tapped(impact, settings);
}

// A light tap on a traffic car: a honk, then a honk and a flash of the
// lights, then (at settings.tapPatience taps without a long enough break)
// the driver loses patience.
@addMethod(VehicleObject)
private final func BCAI_Tapped(impact: Float, settings: ref<BCAISettings>) -> Void {
  let game: GameInstance = this.GetGame();
  let now: Float = this.BCAI_Now();
  if now - this.m_bcaiLastTapTime > settings.tapMemory {
    this.m_bcaiTaps = 0;
  };
  // (One knock often comes as a few bump events in a moment.)
  if now - this.m_bcaiLastTapTime > 0.75 {
    this.m_bcaiTaps += 1;
  };
  this.m_bcaiLastTapTime = now;
  let driver: wref<GameObject> = VehicleComponent.GetDriverMounted(game, this.GetEntityID());
  if !IsDefined(driver as NPCPuppet) || !ScriptedPuppet.IsActive(driver) || this.IsPlayerDriver() {
    return;
  };
  let kmh: Int32 = RoundF(impact * 3.6);
  if settings.tapPatience > 0 && this.m_bcaiTaps >= settings.tapPatience {
    if !this.BCAI_IsAngry() {
      settings.NoteTraffic(s"tap \(this.m_bcaiTaps) on \(this.BCAI_Name()) (\(kmh) km/h): the driver lost patience (the game's reaction)");
    };
    this.m_bcaiAngryUntil = now + 10.0;
    // What the game itself sends a driver bumped once too often.
    let player: ref<GameObject> = GameInstance.GetPlayerSystem(game).GetLocalPlayerMainGameObject();
    let broadcaster: ref<StimBroadcasterComponent> = IsDefined(player) ? player.GetStimBroadcasterComponent() : null;
    if IsDefined(broadcaster) {
      broadcaster.SendDrirectStimuliToTarget(player, gamedataStimType.Bump, driver);
    };
    return;
  };
  let reaction: String = "nothing";
  if settings.honkOnTap && !GameObject.IsCooldownActive(this, n"bcaiTapHonk") {
    GameObject.StartCooldown(this, n"bcaiTapHonk", 3.0);
    if this.m_bcaiTaps >= 2 {
      this.m_vehicleComponent.BCAI_HonkAndFlash();
      reaction = "honk and flash";
    } else {
      this.m_vehicleComponent.PlayDelayedHonk(TweakDBInterface.GetFloat(t"vehicles.honking.collisionHonkDuration", 1.50), TweakDBInterface.GetFloat(t"vehicles.honking.collisionHonkDelay", 0.50));
      reaction = "honk";
    };
  };
  if !GameObject.IsCooldownActive(this, n"bumpCooldown") {
    GameObject.StartCooldown(this, n"bumpCooldown", 1.0);
    GameObject.PlayVoiceOver(driver, n"vehicle_bump", n"Scripts:BCAI_Tapped", 0.0, EMPTY_ENTITY_ID(), true);
  };
  settings.NoteTraffic(s"tap \(this.m_bcaiTaps) on \(this.BCAI_Name()) (\(kmh) km/h): \(reaction)");
}

public static func BCAI_IsMildTrafficStim(stimType: gamedataStimType) -> Bool {
  switch stimType {
    case gamedataStimType.Bump:
    case gamedataStimType.VehicleHit:
    case gamedataStimType.Driving:
    case gamedataStimType.DodgeVehicle:
    case gamedataStimType.VehicleHorn:
    case gamedataStimType.TooCloseDistance:
      return true;
    default:
      return false;
  };
}

// Whether this NPC should shrug off a crowd stim: drivers, the mild ones a
// tap sends; drivers and people on foot, the alarm a lightly dented car
// raises. Not after a hard impact on either car, nor once the driver has
// lost patience, nor in combat, nor for the police.
@addMethod(ReactionManagerComponent)
private final func BCAI_StaysCalm(stimEvent: ref<StimuliEvent>) -> Bool {
  let owner: ref<GameObject> = this.GetOwner();
  let game: GameInstance = owner.GetGame();
  let settings: ref<BCAISettings> = BCAISettings.Get(game);
  if !IsDefined(settings) || !settings.calmTrafficEnabled || !IsDefined(stimEvent) || settings.IsPlayerInCombat() || owner.IsPrevention() {
    return false;
  };
  let stimType: gamedataStimType = stimEvent.GetStimType();
  let source: ref<VehicleObject> = stimEvent.sourceObject as VehicleObject;
  let dent: Bool = IsDefined(source) && (Equals(stimType, gamedataStimType.CrowdIllegalAction) || Equals(stimType, gamedataStimType.VehicleHit));
  if dent && (source.BCAI_HadRecentHardImpact(settings.hardImpactMemory) || source.IsPrevention()) {
    return false;
  };
  let vehicle: wref<VehicleObject>;
  if VehicleComponent.GetVehicle(game, owner, vehicle) && IsDefined(vehicle) {
    if vehicle.IsPrevention() || vehicle.BCAI_IsAngry() || vehicle.BCAI_HadRecentHardImpact(settings.hardImpactMemory) {
      return false;
    };
    if !BCAI_IsMildTrafficStim(stimType) && !dent {
      return false;
    };
    if dent && !GameObject.IsCooldownActive(source, n"bcaiCalmNote") {
      GameObject.StartCooldown(source, n"bcaiCalmNote", 3.0);
      settings.NoteTraffic(s"calm: drivers near \(source.BCAI_Name()) ignored a light knock (dent \(source.BCAI_LastDent()))");
    };
    return true;
  };
  if dent && settings.calmPedestrians {
    if !GameObject.IsCooldownActive(source, n"bcaiCalmWalkNote") {
      GameObject.StartCooldown(source, n"bcaiCalmWalkNote", 3.0);
      settings.NoteTraffic(s"calm: people near \(source.BCAI_Name()) ignored a light knock (dent \(source.BCAI_LastDent()))");
    };
    return true;
  };
  return false;
}

// Mounted NPCs forward crowd stims to their vehicle as HandleReactionEvent,
// which starts panic driving / bailing out / aggressive-crowd combat; NPCs on
// foot start their fear reaction.
@wrapMethod(ReactionManagerComponent)
private final func HandleCrowdReaction(stimEvent: ref<StimuliEvent>) -> Void {
  if this.BCAI_StaysCalm(stimEvent) {
    return;
  };
  wrappedMethod(stimEvent);
}
