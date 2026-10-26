// Better Car AI - BCAI Autodrive+, our own driving AI (needs the native plugin
// 0.19+). The plugin plans the route over the lane map, decides where its
// sensor looks and works the car's controls, called from CET; this half
// watches the player's inputs (to hand back control) and casts the sensor
// rays. It calls no plugin natives, so the scripts still compile without the
// plugin.

public class BCAIOwnDriver extends IScriptable {
  public let vehicle: wref<VehicleObject>;
  private let input: ref<BCAIInputWatch>;
  // How far the car's own front is ahead of its centre (the rays start
  // just past it, so they don't see the car itself).
  public let front: Float;
  // Gap to what is in the way (from the plugin, for the debug panel; < 0 none).
  public let obstacle: Float;
  // Collision group the vehicle rays look for ("Vehicle" hits only vehicles).
  public let sensorGroup: CName;
  // Its rear (from its centre) and half its width, measured like the front.
  public let rear: Float;
  public let halfWidth: Float;
  private let player: wref<PlayerPuppet>;
  // How far the body may reach from the centre along the car and to a side
  // (from its record's size; at least a car's).
  private let reach: Float;
  private let halfReach: Float;
  // Vehicles found nearby by each of the game's two searches, last time.
  public let fromTraffic: Int32;
  public let fromTargeting: Int32;
  private let targeted: array<wref<VehicleObject>>;
  private let targetedAt: Float;
  // People on foot are listed too (Autodrive+ watches for them): those the
  // targeting search found (within 100 m), not in a vehicle.
  public let people: Bool;
  private let peopleTargeted: array<wref<NPCPuppet>>;

  public func Begin(game: GameInstance, player: ref<PlayerPuppet>, vehicle: ref<VehicleObject>, sensorGroup: CName) -> Void {
    this.vehicle = vehicle;
    this.sensorGroup = IsNameValid(sensorGroup) ? sensorGroup : n"Vehicle";
    this.obstacle = -1.0;
    this.player = player;
    // (Rays from 6 m out; finding nothing there (a truck, whose body starts
    // further out), from 15 m. Its record's size can't say: the game gives
    // 3 x 1 m for cars 5.5 m long.)
    this.reach = 4.0;
    this.halfReach = 2.0;
    this.front = this.MeasureFront(game, vehicle, -1.0);
    this.rear = this.MeasureAlong(game, vehicle, -1.0, -1.0);
    if this.front < 0.0 || this.rear < 0.0 {
      this.reach = 13.0;
      if this.front < 0.0 {
        this.front = this.MeasureFront(game, vehicle, 3.0);
      };
      if this.rear < 0.0 {
        this.rear = this.MeasureAlong(game, vehicle, -1.0, 3.0);
      };
    };
    this.halfWidth = this.MeasureAcross(game, vehicle, 0.95);
    ArrayClear(this.targeted);
    ArrayClear(this.peopleTargeted);
    this.targetedAt = -1.0;
    // Steering, throttle or brake from the player hands control back.
    this.input = new BCAIInputWatch();
    this.input.Start(player);
  }

  public func End() -> Void {
    if IsDefined(this.input) {
      this.input.Stop();
      this.input = null;
    };
    this.vehicle = null;
    this.obstacle = -1.0;
  }

  // Player input that should take over, or "".
  public func Takeover() -> String {
    if !IsDefined(this.input) {
      return "";
    };
    if AbsF(this.input.throttle) > 0.3 || AbsF(this.input.brake) > 0.3 || AbsF(this.input.steer) > 0.3 {
      return "player input";
    };
    if this.input.autodriveButton {
      return "autodrive button";
    };
    return "";
  }

  // The car's front: rays from 6 m ahead back to its centre hit it there,
  // at a few heights (a sloping nose reaches furthest low down), the
  // furthest of them. Something else ahead would be hit first, so anything
  // further than the body can reach (4 m for a car; a truck's from its
  // record) is left out; nothing measured falls back to aFallback.
  private func MeasureFront(game: GameInstance, vehicle: ref<VehicleObject>, aFallback: Float) -> Float {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let fwd: Vector4 = vehicle.GetWorldForward();
    let up: Vector4 = vehicle.GetWorldUp();
    let heights: array<Float> = [0.45, 0.7, 0.95];
    let best: Float = -1.0;
    let i: Int32 = 0;
    while i < ArraySize(heights) {
      let trace: TraceResult;
      let centre: Vector4 = vehicle.GetWorldPosition() + up * heights[i];
      if sqs.SyncRaycastByCollisionGroup(centre + fwd * (this.reach + 2.0), centre, this.sensorGroup, trace, false, false) && TraceResult.IsValid(trace) {
        let front: Float = Vector4.Dot(Vector4.Vector3To4(trace.position) - centre, fwd);
        if front > 1.5 && front < this.reach && front > best {
          best = front;
        };
      };
      i += 1;
    };
    return best > 0.0 ? best : aFallback;
  }

  // How far the car's body reaches from its centre along its length (aDir 1
  // forward, -1 back): rays from 6 m out back to the centre, at a few
  // heights, the furthest hit (within its reach; aFallback if none).
  private func MeasureAlong(game: GameInstance, vehicle: ref<VehicleObject>, aDir: Float, aFallback: Float) -> Float {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let fwd: Vector4 = vehicle.GetWorldForward() * aDir;
    let up: Vector4 = vehicle.GetWorldUp();
    let heights: array<Float> = [0.45, 0.7, 0.95];
    let best: Float = -1.0;
    let i: Int32 = 0;
    while i < ArraySize(heights) {
      let trace: TraceResult;
      let centre: Vector4 = vehicle.GetWorldPosition() + up * heights[i];
      if sqs.SyncRaycastByCollisionGroup(centre + fwd * (this.reach + 2.0), centre, this.sensorGroup, trace, false, false) && TraceResult.IsValid(trace) {
        let reach: Float = Vector4.Dot(Vector4.Vector3To4(trace.position) - centre, fwd);
        if reach > 1.0 && reach < this.reach && reach > best {
          best = reach;
        };
      };
      i += 1;
    };
    return best > 0.0 ? best : aFallback;
  }

  // Half the car's width: rays from 3 m out either side back to its middle,
  // at a few heights and along its length (mirrors included), the widest.
  private func MeasureAcross(game: GameInstance, vehicle: ref<VehicleObject>, aFallback: Float) -> Float {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let fwd: Vector4 = vehicle.GetWorldForward();
    let up: Vector4 = vehicle.GetWorldUp();
    let right: Vector4 = vehicle.GetWorldRight();
    let heights: array<Float> = [0.5, 0.8, 1.05];
    let alongs: array<Float> = [-0.3 * this.reach, 0.0, 0.3 * this.reach];
    let sides: array<Float> = [-1.0, 1.0];
    let best: Float = -1.0;
    let h: Int32 = 0;
    while h < ArraySize(heights) {
      let a: Int32 = 0;
      while a < ArraySize(alongs) {
        let k: Int32 = 0;
        while k < ArraySize(sides) {
          let trace: TraceResult;
          let middle: Vector4 = vehicle.GetWorldPosition() + up * heights[h] + fwd * alongs[a];
          let out: Vector4 = right * sides[k];
          if sqs.SyncRaycastByCollisionGroup(middle + out * (this.halfReach + 1.0), middle, this.sensorGroup, trace, false, false) && TraceResult.IsValid(trace) {
            let reach: Float = Vector4.Dot(Vector4.Vector3To4(trace.position) - middle, out);
            if reach > 0.4 && reach < this.halfReach && reach > best {
              best = reach;
            };
          };
          k += 1;
        };
        a += 1;
      };
      h += 1;
    };
    return best > 0.0 ? best : aFallback;
  }

  // A vehicle's size as its record gives it: X length, Y width, Z height
  // (m; 0 when it has none).
  public static func RecordSize(v: wref<VehicleObject>) -> Vector4 {
    let record: wref<Vehicle_Record> = IsDefined(v) ? v.GetRecord() : null;
    if IsDefined(record) && IsDefined(record.Destruction()) {
      let d: Vector3 = record.Destruction().VehicleDimensions();
      return new Vector4(MaxF(d.X, d.Y), MinF(d.X, d.Y), d.Z, 0.0);
    };
    return new Vector4(0.0, 0.0, 0.0, 0.0);
  }

  // The car's body for the plugin: X front, Y rear, Z half width.
  public func Body() -> Vector4 {
    return new Vector4(this.front, this.rear, this.halfWidth, 0.0);
  }

  // Vehicles near the car, as the game knows them, three entries each:
  // position (W 1 parked, +2 a bike, +4 a person on foot), forward,
  // velocity. From two searches: the traffic system's (cars driving in
  // traffic, every call) and the targeting system's (anything targetable, a
  // few times a second; people on foot too while `people` is on, listed with
  // no velocity: the plugin works it out from where they were).
  public func Vehicles(game: GameInstance) -> array<Vector4> {
    let out: array<Vector4>;
    let found: array<wref<VehicleObject>>;
    if !IsDefined(this.vehicle) {
      return out;
    };
    let pos: Vector4 = this.vehicle.GetWorldPosition();
    let fwd: Vector4 = this.vehicle.GetWorldForward();
    let right: Vector4 = this.vehicle.GetWorldRight();
    // The traffic system: a box from 40 m behind to 200 m ahead (overtaking
    // on the other side of the road needs to see that far), 30 m to either
    // side (as the game asks it for the crowd around a thrown body).
    let box: array<Vector4>;
    let entities: array<wref<Entity>>;
    ArrayPush(box, pos - fwd * 40.0 + new Vector4(0.0, 0.0, -3.0, 0.0));
    ArrayPush(box, pos + fwd * 200.0 + new Vector4(0.0, 0.0, 5.0, 0.0));
    ArrayPush(box, pos + right * 30.0);
    ArrayPush(box, pos - right * 30.0);
    GameInstance.GetTrafficSystem(game).FindEntitiesNearPlane(this.vehicle, box, pos, right, 30.0, 64u, entities);
    this.fromTraffic = 0;
    let i: Int32 = 0;
    while i < ArraySize(entities) {
      let v: wref<VehicleObject> = entities[i] as VehicleObject;
      if this.IsOther(found, v) {
        ArrayPush(found, v);
        this.fromTraffic += 1;
      };
      i += 1;
    };
    // The targeting system: everything targetable within 100 m (but people,
    // unless it watches for them).
    let now: Float = EngineTime.ToFloat(GameInstance.GetSimTime(game));
    if IsDefined(this.player) && (this.targetedAt < 0.0 || now - this.targetedAt > 0.25) {
      let query: TargetSearchQuery;
      let parts: array<TS_TargetPartInfo>;
      this.targetedAt = now;
      ArrayClear(this.targeted);
      ArrayClear(this.peopleTargeted);
      query.testedSet = TargetingSet.Complete;
      query.searchFilter = this.people ? TSF_Not(TSFMV.Obj_Player) : TSF_And(TSF_Not(TSFMV.Obj_Player), TSF_Not(TSFMV.Obj_Puppet));
      query.maxDistance = 100.0;
      query.filterObjectByDistance = true;
      query.includeSecondaryTargets = false;
      query.ignoreInstigator = true;
      GameInstance.GetTargetingSystem(game).GetTargetParts(this.player, query, parts);
      i = 0;
      while i < ArraySize(parts) {
        let component: wref<TargetingComponent> = TS_TargetPartInfo.GetComponent(parts[i]);
        if IsDefined(component) {
          let v: wref<VehicleObject> = component.GetEntity() as VehicleObject;
          if this.IsOther(this.targeted, v) {
            ArrayPush(this.targeted, v);
          };
          let npc: wref<NPCPuppet> = component.GetEntity() as NPCPuppet;
          if this.people && this.IsPerson(game, npc, pos) {
            ArrayPush(this.peopleTargeted, npc);
          };
        };
        i += 1;
      };
    };
    this.fromTargeting = ArraySize(this.targeted);
    i = 0;
    while i < ArraySize(this.targeted) {
      if this.IsOther(found, this.targeted[i]) {
        ArrayPush(found, this.targeted[i]);
      };
      i += 1;
    };
    i = 0;
    while i < ArraySize(found) {
      let v: wref<VehicleObject> = found[i];
      let p: Vector4 = v.GetWorldPosition();
      let flags: Float = (v.IsVehicleParked() ? 1.0 : 0.0) + (IsDefined(v as BikeObject) ? 2.0 : 0.0);
      p.W = flags;
      // (Its size in the W of the other two: the plugin takes it from 0.29.)
      let size: Vector4 = BCAIOwnDriver.RecordSize(v);
      let fwd: Vector4 = v.GetWorldForward();
      let vel: Vector4 = v.GetLinearVelocity();
      fwd.W = size.X;
      vel.W = size.Y;
      ArrayPush(out, p);
      ArrayPush(out, fwd);
      ArrayPush(out, vel);
      i += 1;
    };
    i = 0;
    while i < ArraySize(this.peopleTargeted) {
      let npc: wref<NPCPuppet> = this.peopleTargeted[i];
      if IsDefined(npc) {
        let p: Vector4 = npc.GetWorldPosition();
        let fwd: Vector4 = npc.GetWorldForward();
        p.W = 4.0;
        fwd.W = 0.0;
        ArrayPush(out, p);
        ArrayPush(out, fwd);
        ArrayPush(out, new Vector4(0.0, 0.0, 0.0, 0.0));
      };
      i += 1;
    };
    return out;
  }

  // Someone on foot to watch for: alive and awake, not in a vehicle, not
  // listed yet. (Within 100 m, as the search: a truck at 60 km/h needs about
  // 40 m to stop for someone.)
  private func IsPerson(game: GameInstance, npc: wref<NPCPuppet>, pos: Vector4) -> Bool {
    if !IsDefined(npc) || !ScriptedPuppet.IsActive(npc) || VehicleComponent.IsMountedToVehicle(game, npc) {
      return false;
    };
    if Vector4.Distance(npc.GetWorldPosition(), pos) > 100.0 {
      return false;
    };
    let i: Int32 = 0;
    while i < ArraySize(this.peopleTargeted) {
      if this.peopleTargeted[i] == npc {
        return false;
      };
      i += 1;
    };
    return true;
  }

  // A vehicle, not this car, and not in the list yet.
  private func IsOther(list: array<wref<VehicleObject>>, v: wref<VehicleObject>) -> Bool {
    if !IsDefined(v) || v == this.vehicle {
      return false;
    };
    let i: Int32 = 0;
    while i < ArraySize(list) {
      if list[i] == v {
        return false;
      };
      i += 1;
    };
    return true;
  }

  // Casts the plugin's rays: pairs of start (W 0: look for vehicles, 1: for
  // walls and other fixed things) and end. Per ray, where it hit (W 1) or
  // its end (W 0). Wall rays ignore surfaces facing up (the road itself).
  public func Cast(game: GameInstance, rays: array<Vector4>) -> array<Vector4> {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let out: array<Vector4>;
    let i: Int32 = 0;
    let a: Vector4;
    let b: Vector4;
    let wall: Bool;
    let hit: Bool;
    let p: Vector4;
    while i + 1 < ArraySize(rays) {
      let trace: TraceResult;
      a = rays[i];
      b = rays[i + 1];
      wall = a.W > 0.5;
      a.W = 1.0;
      b.W = 1.0;
      if wall {
        hit = sqs.SyncRaycastByCollisionPreset(a, b, n"World Static", trace, false, false) && TraceResult.IsValid(trace) && trace.normal.Z < 0.6;
      } else {
        hit = sqs.SyncRaycastByCollisionGroup(a, b, this.sensorGroup, trace, false, false) && TraceResult.IsValid(trace);
      };
      if hit {
        p = Vector4.Vector3To4(trace.position);
        p.W = 1.0;
      } else {
        p = b;
        p.W = 0.0;
      };
      ArrayPush(out, p);
      i += 2;
    };
    return out;
  }

  // From the car's front, straight ahead for 40 m, with each collision group
  // and preset that might see vehicles: which of them hit the car parked in
  // front, and which hit the road or the car itself.
  public static func SensorTest(game: GameInstance, vehicle: ref<VehicleObject>) -> String {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let fwd: Vector4 = vehicle.GetWorldForward();
    let a: Vector4 = vehicle.GetWorldPosition() + fwd * 3.2 + new Vector4(0.0, 0.0, 0.7, 0.0);
    let b: Vector4 = a + fwd * 40.0;
    let groups: array<CName> = [n"Vehicle", n"Chassis", n"Dynamic", n"Static", n"AI", n"Ragdoll", n"Terrain", n"Destructible"];
    let presets: array<CName> = [n"Vehicle Chassis", n"World Dynamic", n"World Static", n"Sight Blocker"];
    let out: String = "sensor test (from 3.2 m ahead of the car's centre, 0.7 m up, 40 m straight on):";
    let i: Int32 = 0;
    while i < ArraySize(groups) {
      let trace: TraceResult;
      if sqs.SyncRaycastByCollisionGroup(a, b, groups[i], trace, false, false) && TraceResult.IsValid(trace) {
        out += " group " + NameToString(groups[i]) + " hit " + FloatToStringPrec(Vector4.Distance(a, Vector4.Vector3To4(trace.position)), 1) + " m (" + NameToString(trace.material) + ", normal z " + FloatToStringPrec(trace.normal.Z, 2) + ");";
      } else {
        out += " group " + NameToString(groups[i]) + " -;";
      };
      i += 1;
    };
    i = 0;
    while i < ArraySize(presets) {
      let trace: TraceResult;
      if sqs.SyncRaycastByCollisionPreset(a, b, presets[i], trace, false, false) && TraceResult.IsValid(trace) {
        out += " preset " + NameToString(presets[i]) + " hit " + FloatToStringPrec(Vector4.Distance(a, Vector4.Vector3To4(trace.position)), 1) + " m (" + NameToString(trace.material) + ", normal z " + FloatToStringPrec(trace.normal.Z, 2) + ");";
      } else {
        out += " preset " + NameToString(presets[i]) + " -;";
      };
      i += 1;
    };
    return out;
  }
}

// Starting autodrive while "Starting autodrive starts Autodrive+" is on: don't
// let the native autodrive start (switching it off again makes it brake the
// car); CET starts Autodrive+ instead.
@wrapMethod(AutoDriveSystem)
private final func OnEnableAutoDriveRequest(request: ref<EnableAutoDriveRequest>) -> Void {
  let settings: ref<BCAISettings> = BCAISettings.Get(this.GetGameInstance());
  if !request.isDelamain && IsDefined(settings) && settings.RequestAutodrivePlus() {
    return;
  };
  wrappedMethod(request);
}
