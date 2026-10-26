// Better Car AI - visual debug overlay (data side).
// The native driver's planned path isn't exposed to scripts, so this shows what
// we can measure: the destination, the line the car actually took, where its
// current motion carries it in the next 2 s, sideways slip, ground contact and
// what a fan of look-ahead rays hits (other cars vs. walls/railings).
// World-space results are refreshed by Tick() ~30x a second (the trail ~10x);
// Frame() projects them to the screen every frame for the CET half to draw.

public class BCAIDebug extends IScriptable {

  // Colour indices understood by the CET drawing code.
  public static func White() -> Float { return 1.0; }
  public static func Cyan() -> Float { return 2.0; }
  public static func Green() -> Float { return 3.0; }
  public static func Yellow() -> Float { return 4.0; }
  public static func Orange() -> Float { return 5.0; }
  public static func Red() -> Float { return 6.0; }
  public static func Grey() -> Float { return 7.0; }
  public static func Magenta() -> Float { return 8.0; }
  public static func Blue() -> Float { return 9.0; }
  // Lane map colours.
  public static func LaneRoad() -> Float { return 10.0; }
  public static func LaneJunction() -> Float { return 11.0; }
  public static func LaneGps() -> Float { return 12.0; }
  public static func LaneRepaired() -> Float { return 13.0; }

  // Trail of recent positions and how much the car was sliding at each.
  private let trail: array<Vector4>;
  private let trailSlip: array<Float>;
  private let trailAir: array<Bool>;

  // Current snapshot (world space).
  private let hasVehicle: Bool;
  private let pos: Vector4;
  private let fwd: Vector4;
  private let up: Vector4;
  private let vel: Vector4;
  private let speed: Float;
  private let lateral: Float;
  private let yawRate: Float; // rad/s, positive = turning left
  private let prevFwd: Vector4;
  private let prevTime: Float;
  private let clearance: Float; // origin to ground below, -1 = no ground found
  private let airborne: Bool;
  // What is under the car: 0 road, 1 dirt or gravel, 2 grass, 3 other, 4 nothing
  // found (and the material's name as the game calls it).
  private let surface: Int32;
  private let surfaceName: String;
  private let skidding: Bool;
  private let trailTime: Float;
  private let predicted: array<Vector4>;
  // The AI's planned route ahead (native plugin only), and Autodrive+'s
  // planned speed along it (m/s; empty for the game's route).
  private let route: array<Vector4>;
  private let routeSpeed: array<Float>;
  // What is in the way of Autodrive+, labelled.
  private let blockLabel: String;
  // Lanes around the car (native plugin only): per lane a header (kind,
  // point count, lane index, dead end) and its points.
  private let lanes: array<Vector4>;
  // BCAI Autodrive+: the road's edges along its path (left, right pairs) and
  // what its sensor remembers (W 1 vehicle, 2 wall, +2 the one in the way).
  private let ownRoom: array<Vector4>;
  private let ownSeen: array<Vector4>;

  // Look-ahead rays: start, end (hit point or full length) and what they hit.
  private let rayStart: array<Vector4>;
  private let rayEnd: array<Vector4>;
  private let rayKind: array<Int32>; // 0 clear, 1 vehicle, 2 wall, 3 ground
  private let nearestVehicle: Float;
  private let nearestWall: Float;
  public let lookahead: Float;

  // Drive info supplied by BCAISettings.
  private let hasTarget: Bool;
  private let target: Vector4;
  private let arrival: Float;
  private let lines: array<String>;

  // Per-frame projection.
  private let camPos: Vector4;
  private let camFwd: Vector4;
  private let camRight: Vector4;
  private let camUp: Vector4;
  private let tanH: Float;
  private let tanV: Float;
  private let signX: Float;
  private let signY: Float;
  private let useEngine: Bool;
  // Set from the CET debug section to calibrate our own projection.
  public let fovHorizontal: Bool;
  public let forceManual: Bool;
  // Which layers to draw (set from CET): the driven line, the look-ahead
  // rays, the route (or plan), the road's edges and sensor points, where the
  // car's motion takes it.
  public let hideTrail: Bool;
  public let hideRays: Bool;
  public let hideRoute: Bool;
  public let hideSensor: Bool;
  public let hidePredict: Bool;
  private let calib: String;
  private let texts: array<String>;
  private let buf: array<Float>;

  public func Init() -> Void {
    this.lookahead = 80.0;
    this.arrival = 15.0;
  }

  // Points with the planned speed in W (m/s), when withSpeed.
  public func SetRoute(points: array<Vector4>, withSpeed: Bool) -> Void {
    let i: Int32 = 0;
    ArrayClear(this.route);
    ArrayClear(this.routeSpeed);
    while i < ArraySize(points) {
      if withSpeed {
        ArrayPush(this.routeSpeed, points[i].W);
      };
      ArrayPush(this.route, new Vector4(points[i].X, points[i].Y, points[i].Z, 1.0));
      i += 1;
    };
  }

  public func SetBlockLabel(label: String) -> Void {
    this.blockLabel = label;
  }

  public func SetLanes(lanes: array<Vector4>) -> Void {
    this.lanes = lanes;
  }

  public func SetOwnDriver(room: array<Vector4>, seen: array<Vector4>) -> Void {
    this.ownRoom = room;
    this.ownSeen = seen;
  }

  private func LaneColour(kind: Int32) -> Float {
    switch kind {
      case 2: return BCAIDebug.LaneJunction();
      case 3: return BCAIDebug.LaneGps();
      case 4: return BCAIDebug.Red();
      case 5: return BCAIDebug.Grey();
      case 6: return BCAIDebug.LaneRepaired();
      default: return BCAIDebug.LaneRoad();
    }
  }

  // Each lane as a line with an arrowhead at its end, its number near the
  // car, and a red "dead end" where nothing links out of a driving lane.
  private func DrawLanes(game: GameInstance) -> Void {
    let lift: Vector4 = new Vector4(0.0, 0.0, 0.25, 0.0);
    let i: Int32 = 0;
    let n: Int32;
    let k: Int32;
    let head: Vector4;
    let colour: Float;
    let last: Vector4;
    let back: Vector4;
    let side: Vector4;
    let mid: Vector4;
    while i < ArraySize(this.lanes) {
      head = this.lanes[i];
      n = Cast<Int32>(head.Y);
      if n < 2 || i + n >= ArraySize(this.lanes) {
        return;
      };
      colour = this.LaneColour(Cast<Int32>(head.X));
      k = 2;
      while k <= n {
        this.Line(game, this.lanes[i + k - 1] + lift, this.lanes[i + k] + lift, colour, Cast<Int32>(head.X) == 6 ? 3.0 : 2.0);
        k += 1;
      };
      last = this.lanes[i + n] + lift;
      back = Vector4.Normalize(this.lanes[i + n - 1] - this.lanes[i + n]);
      side = Vector4.Cross(new Vector4(0.0, 0.0, 1.0, 0.0), back);
      this.Line(game, last, last + back * 1.5 + side * 0.7, colour, 2.0);
      this.Line(game, last, last + back * 1.5 - side * 0.7, colour, 2.0);
      if head.W > 0.5 {
        this.Marker(game, last, 5.0, BCAIDebug.Red(), "dead end");
      };
      mid = this.lanes[i + 1 + n / 2];
      if Vector4.Distance(mid, this.pos) < 35.0 {
        this.Marker(game, mid + lift, 1.0, colour, "#" + ToString(Cast<Int32>(head.Z)));
      };
      i += n + 1;
    };
  }

  public func Reset() -> Void {
    ArrayClear(this.trail);
    ArrayClear(this.trailSlip);
    ArrayClear(this.trailAir);
    this.prevTime = 0.0;
    this.trailTime = 0.0;
  }

  public func SetTarget(hasTarget: Bool, target: Vector4, arrival: Float) -> Void {
    this.hasTarget = hasTarget;
    this.target = target;
    this.arrival = arrival;
  }

  public func SetLines(lines: array<String>) -> Void {
    this.lines = lines;
  }

  public func GetLines() -> array<String> {
    return this.lines;
  }

  public func Speed() -> Float { return this.speed; }
  public func Lateral() -> Float { return this.lateral; }
  public func YawRateDeg() -> Float { return Rad2Deg(this.yawRate); }
  public func Clearance() -> Float { return this.clearance; }
  public func IsAirborne() -> Bool { return this.airborne; }
  public func Lookahead() -> Float { return this.lookahead; }
  public func Surface() -> Int32 { return this.surface; }
  public func SurfaceName() -> String { return this.surfaceName; }
  public func IsSkidding() -> Bool { return this.skidding; }
  public func NearestVehicle() -> Float { return this.nearestVehicle; }
  public func NearestWall() -> Float { return this.nearestWall; }
  public func HasVehicle() -> Bool { return this.hasVehicle; }
  public func Position() -> Vector4 { return this.pos; }
  public func Forward() -> Vector4 { return this.fwd; }
  public func Velocity() -> Vector4 { return this.vel; }

  // The look-ahead rays as "kind:distance" (kind 0 clear, 1 car, 2 wall,
  // 3 road rising), left to right, separated by '|'.
  public func RaysCsv() -> String {
    let out: String = "";
    let i: Int32 = 0;
    while i < ArraySize(this.rayKind) {
      if i > 0 {
        out += "|";
      };
      out += ToString(this.rayKind[i]) + ":" + FloatToStringPrec(Vector4.Distance(this.rayStart[i], this.rayEnd[i]), 1);
      i += 1;
    };
    return out;
  }
  public func Calibration() -> String {
    if !this.hasVehicle {
      return "projection: not in a vehicle";
    };
    return NotEquals(this.calib, "") ? this.calib : "projection: not run yet";
  }

  public func Tick(game: GameInstance, vehicle: wref<VehicleObject>) -> Void {
    let now: Float = EngineTime.ToFloat(GameInstance.GetSimTime(game));
    let right: Vector4;
    let dt: Float;
    let cross: Vector4;
    let flatFwd: Vector4;
    this.hasVehicle = IsDefined(vehicle);
    if !this.hasVehicle {
      this.Reset();
      return;
    };
    this.pos = vehicle.GetWorldPosition();
    this.fwd = vehicle.GetWorldForward();
    this.up = vehicle.GetWorldUp();
    right = vehicle.GetWorldRight();
    this.vel = vehicle.GetLinearVelocity();
    this.speed = Vector4.Length(this.vel);
    this.lateral = Vector4.Dot(this.vel, right);

    // Yaw rate from how far the heading turned since the last tick.
    dt = now - this.prevTime;
    flatFwd = Vector4.Normalize(new Vector4(this.fwd.X, this.fwd.Y, 0.0, 0.0));
    if this.prevTime > 0.0 && dt > 0.01 && dt < 1.0 {
      cross = Vector4.Cross(this.prevFwd, flatFwd);
      this.yawRate = cross.Z / dt;
    } else {
      this.yawRate = 0.0;
    };
    this.prevFwd = flatFwd;
    this.prevTime = now;

    this.ProbeGround(game, vehicle);
    this.CastRays(game);
    this.Predict(flatFwd);

    // The trail: a point every 0.1 s, the last 15 s.
    if now - this.trailTime >= 0.1 || now < this.trailTime {
      this.trailTime = now;
      ArrayPush(this.trail, this.pos);
      // (The slip angle, degrees: sideways speed against speed.)
      ArrayPush(this.trailSlip, this.speed > 3.0 ? Rad2Deg(AsinF(MinF(AbsF(this.lateral) / this.speed, 1.0))) : 0.0);
      ArrayPush(this.trailAir, this.airborne);
      while ArraySize(this.trail) > 150 {
        ArrayErase(this.trail, 0);
        ArrayErase(this.trailSlip, 0);
        ArrayErase(this.trailAir, 0);
      };
    };
  }

  // Airborne as the game's physics says (wheels off the ground). The ground
  // under the car: fixed world geometry first, then the terrain (dirt roads
  // and open ground aren't part of the world's static meshes), and what it is
  // made of.
  private func ProbeGround(game: GameInstance, vehicle: wref<VehicleObject>) -> Void {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let hit: TraceResult;
    let terrain: TraceResult;
    let start: Vector4 = this.pos + new Vector4(0.0, 0.0, 1.0, 0.0);
    let end: Vector4 = this.pos + new Vector4(0.0, 0.0, -3.0, 0.0);
    let found: Bool = sqs.SyncRaycastByCollisionPreset(start, end, n"World Static", hit) && TraceResult.IsValid(hit);
    if !found {
      found = sqs.SyncRaycastByCollisionGroup(start, end, n"Terrain", terrain, false, false) && TraceResult.IsValid(terrain);
      if found {
        hit = terrain;
      };
    };
    if found {
      this.clearance = this.pos.Z - hit.position.Z;
      this.surfaceName = NameToString(hit.material);
      this.surface = BCAIDebug.SurfaceKind(this.surfaceName);
    } else {
      this.clearance = -1.0;
      this.surfaceName = "";
      this.surface = 4;
    };
    this.airborne = vehicle.IsInAir();
    this.skidding = vehicle.IsSkidding(0.3);
  }

  // 0 road (asphalt, concrete, metal, tiles...), 1 dirt, gravel or sand,
  // 2 grass, 3 anything else, from a physics material's name.
  public static func SurfaceKind(material: String) -> Int32 {
    let m: String = StrLower(material);
    if StrContains(m, "dirt") || StrContains(m, "gravel") || StrContains(m, "sand") || StrContains(m, "mud")
      || StrContains(m, "soil") || StrContains(m, "terrain") || StrContains(m, "ground") || StrContains(m, "rock") {
      return 1;
    };
    if StrContains(m, "grass") || StrContains(m, "foliage") || StrContains(m, "plant") {
      return 2;
    };
    if StrContains(m, "asphalt") || StrContains(m, "concrete") || StrContains(m, "road") || StrContains(m, "metal")
      || StrContains(m, "steel") || StrContains(m, "tile") || StrContains(m, "brick") || StrContains(m, "stone")
      || StrContains(m, "pavement") || StrContains(m, "cobble") || StrContains(m, "wood") || StrContains(m, "plastic") {
      return 0;
    };
    return 3;
  }

  // The fan tilts with the car (up and down slopes, leaning in turns).
  private func CastRays(game: GameInstance) -> Void {
    let sqs: ref<SpatialQueriesSystem> = GameInstance.GetSpatialQueriesSystem(game);
    let left: Vector4 = Vector4.Normalize(Vector4.Cross(this.up, this.fwd));
    let origin: Vector4 = this.pos + this.fwd * 3.0 + this.up * 0.8;
    let angles: array<Float>;
    let dir: Vector4;
    let end: Vector4;
    let carHit: TraceResult;
    let wallHit: TraceResult;
    let noHit: TraceResult;
    let carDist: Float;
    let wallDist: Float;
    let kind: Int32;
    let a: Float;
    let i: Int32 = 0;
    ArrayClear(this.rayStart);
    ArrayClear(this.rayEnd);
    ArrayClear(this.rayKind);
    this.nearestVehicle = -1.0;
    this.nearestWall = -1.0;
    ArrayPush(angles, -20.0);
    ArrayPush(angles, -10.0);
    ArrayPush(angles, -4.0);
    ArrayPush(angles, 0.0);
    ArrayPush(angles, 4.0);
    ArrayPush(angles, 10.0);
    ArrayPush(angles, 20.0);
    while i < ArraySize(angles) {
      a = Deg2Rad(angles[i]);
      dir = this.fwd * CosF(a) + left * SinF(a);
      end = origin + dir * this.lookahead;
      carHit = noHit;
      wallHit = noHit;
      sqs.SyncRaycastByCollisionPreset(origin, end, n"Vehicle Chassis", carHit);
      sqs.SyncRaycastByCollisionPreset(origin, end, n"World Static", wallHit);
      carDist = TraceResult.IsValid(carHit) ? Vector4.Distance(origin, Vector4.Vector3To4(carHit.position)) : -1.0;
      wallDist = TraceResult.IsValid(wallHit) ? Vector4.Distance(origin, Vector4.Vector3To4(wallHit.position)) : -1.0;
      kind = 0;
      if carDist >= 0.0 && (wallDist < 0.0 || carDist <= wallDist) {
        kind = 1;
        end = Vector4.Vector3To4(carHit.position);
        if this.nearestVehicle < 0.0 || carDist < this.nearestVehicle {
          this.nearestVehicle = carDist;
        };
      } else {
        if wallDist >= 0.0 {
          end = Vector4.Vector3To4(wallHit.position);
          // A mostly upward-facing hit is the road rising (a crest or ramp).
          if wallHit.normal.Z > 0.6 {
            kind = 3;
          } else {
            kind = 2;
            if this.nearestWall < 0.0 || wallDist < this.nearestWall {
              this.nearestWall = wallDist;
            };
          };
        };
      };
      ArrayPush(this.rayStart, origin);
      ArrayPush(this.rayEnd, end);
      ArrayPush(this.rayKind, kind);
      i += 1;
    };
  }

  // Where the car ends up in 2 s if it keeps its current speed and turn rate.
  private func Predict(flatFwd: Vector4) -> Void {
    let p: Vector4 = this.pos;
    let dir: Vector4 = flatFwd;
    let step: Float = 0.2;
    let turn: Float = this.yawRate * step;
    let i: Int32 = 0;
    ArrayClear(this.predicted);
    ArrayPush(this.predicted, p);
    while i < 10 {
      dir = dir * CosF(turn) + Vector4.Cross(new Vector4(0.0, 0.0, 1.0, 0.0), dir) * SinF(turn);
      p = p + dir * (this.speed * step);
      ArrayPush(this.predicted, p);
      i += 1;
    };
  }

  // --- Projection ---

  private func BeginFrame(game: GameInstance) -> Bool {
    let cam: ref<CameraSystem> = GameInstance.GetCameraSystem(game);
    let t: Transform;
    let c: Vector4;
    let r: Vector4;
    let u: Vector4;
    let probe: Vector4;
    let fov: Float;
    let aspect: Float;
    if !IsDefined(cam) {
      this.calib = "projection: no camera system";
      return false;
    };
    if !cam.GetActiveCameraWorldTransform(t) {
      this.calib = "projection: no camera transform";
      return false;
    };
    this.camPos = Transform.GetPosition(t);
    this.camFwd = cam.GetActiveCameraForward();
    this.camRight = cam.GetActiveCameraRight();
    this.camUp = cam.GetActiveCameraUp();

    // Our own perspective projection from the camera's axes and FOV.
    fov = cam.GetActiveCameraFOV();
    if fov > 3.2 {
      fov = Deg2Rad(fov); // degrees, not radians
    };
    aspect = cam.GetAspectRatio();
    if aspect < 0.5 || aspect > 5.0 {
      aspect = 16.0 / 9.0;
    };
    if this.fovHorizontal {
      this.tanH = TanF(fov * 0.5);
      this.tanV = this.tanH / aspect;
    } else {
      this.tanV = TanF(fov * 0.5);
      this.tanH = this.tanV * aspect;
    };

    // The engine's ProjectPoint, if it behaves like NDC (centre maps to ~0 and
    // a 1 m offset at 10 m moves the point a sensible amount), is exact; work
    // out its axis directions from known points, else use our projection.
    probe = this.camPos + this.camFwd * 10.0;
    c = this.RawProject(cam, probe);
    r = this.RawProject(cam, probe + this.camRight);
    u = this.RawProject(cam, probe + this.camUp);
    this.signX = r.X >= c.X ? 1.0 : -1.0;
    this.signY = u.Y < c.Y ? 1.0 : -1.0;
    this.useEngine = !this.forceManual && AbsF(c.X) < 0.15 && AbsF(c.Y) < 0.15
      && AbsF(r.X - c.X) > 0.02 && AbsF(r.X - c.X) < 1.0 && AbsF(u.Y - c.Y) > 0.02 && AbsF(u.Y - c.Y) < 1.0;
    this.calib = "projection: " + (this.useEngine ? "engine" : "manual")
      + " fov=" + FloatToStringPrec(Rad2Deg(fov), 1) + (this.fovHorizontal ? "h" : "v")
      + " aspect=" + FloatToStringPrec(aspect, 2)
      + " | engine c=(" + FloatToStringPrec(c.X, 3) + "," + FloatToStringPrec(c.Y, 3) + "," + FloatToStringPrec(c.Z, 3) + "," + FloatToStringPrec(c.W, 3)
      + ") dx=" + FloatToStringPrec(r.X - c.X, 3) + " dy=" + FloatToStringPrec(u.Y - c.Y, 3);
    return true;
  }

  private func RawProject(cam: ref<CameraSystem>, p: Vector4) -> Vector4 {
    let v: Vector4 = cam.ProjectPoint(new Vector4(p.X, p.Y, p.Z, 1.0));
    // Homogeneous clip coordinates need the perspective divide.
    if AbsF(v.W) > 0.0001 && AbsF(v.W - 1.0) > 0.01 {
      v = new Vector4(v.X / v.W, v.Y / v.W, v.Z / v.W, 1.0);
    };
    return v;
  }

  private func Depth(p: Vector4) -> Float {
    return Vector4.Dot(p - this.camPos, this.camFwd);
  }

  // Screen position as fractions of the screen (0..1, y down).
  private func ToScreen(game: GameInstance, p: Vector4) -> Vector4 {
    let v: Vector4;
    let d: Vector4;
    let z: Float;
    if this.useEngine {
      v = this.RawProject(GameInstance.GetCameraSystem(game), p);
      return new Vector4(0.5 + 0.5 * v.X * this.signX, 0.5 + 0.5 * v.Y * this.signY, 0.0, 0.0);
    };
    d = p - this.camPos;
    z = MaxF(Vector4.Dot(d, this.camFwd), 0.01);
    return new Vector4(0.5 + 0.5 * Vector4.Dot(d, this.camRight) / (z * this.tanH),
      0.5 - 0.5 * Vector4.Dot(d, this.camUp) / (z * this.tanV), 0.0, 0.0);
  }

  private func Line(game: GameInstance, from: Vector4, to: Vector4, colour: Float, width: Float) -> Void {
    let near: Float = 0.5;
    let a: Vector4 = from;
    let b: Vector4 = to;
    let da: Float = this.Depth(a);
    let db: Float = this.Depth(b);
    let sa: Vector4;
    let sb: Vector4;
    if da < near && db < near {
      return;
    };
    // Clip the part behind the camera.
    if da < near {
      a = a + (b - a) * ((near - da) / (db - da));
    } else {
      if db < near {
        b = b + (a - b) * ((near - db) / (da - db));
      };
    };
    sa = this.ToScreen(game, a);
    sb = this.ToScreen(game, b);
    ArrayPush(this.buf, 1.0);
    ArrayPush(this.buf, sa.X);
    ArrayPush(this.buf, sa.Y);
    ArrayPush(this.buf, sb.X);
    ArrayPush(this.buf, sb.Y);
    ArrayPush(this.buf, colour);
    ArrayPush(this.buf, width);
    ArrayPush(this.buf, 0.0);
  }

  private func Marker(game: GameInstance, p: Vector4, radius: Float, colour: Float, text: String) -> Void {
    let s: Vector4;
    if this.Depth(p) < 0.5 {
      return;
    };
    s = this.ToScreen(game, p);
    ArrayPush(this.buf, 2.0);
    ArrayPush(this.buf, s.X);
    ArrayPush(this.buf, s.Y);
    ArrayPush(this.buf, radius);
    ArrayPush(this.buf, 0.0);
    ArrayPush(this.buf, colour);
    ArrayPush(this.buf, 2.0);
    ArrayPush(this.buf, 0.0);
    if NotEquals(text, "") {
      ArrayPush(this.texts, text);
      ArrayPush(this.buf, 3.0);
      ArrayPush(this.buf, s.X);
      ArrayPush(this.buf, s.Y);
      ArrayPush(this.buf, radius + 4.0);
      ArrayPush(this.buf, 0.0);
      ArrayPush(this.buf, colour);
      ArrayPush(this.buf, 0.0);
      ArrayPush(this.buf, Cast<Float>(ArraySize(this.texts)));
    };
  }

  // Slip angle in degrees. The game's cars corner hardest at 8-15 degrees
  // (driving tests: about 2 g); past about 17 they slide.
  private func SlipColour(slip: Float, air: Bool) -> Float {
    if air {
      return BCAIDebug.Magenta();
    };
    if slip > 17.0 {
      return BCAIDebug.Red();
    };
    if slip > 8.0 {
      return BCAIDebug.Yellow();
    };
    return BCAIDebug.Green();
  }

  // Records of 8 floats: kind (1 line, 2 circle, 3 text), x1, y1, x2/radius,
  // y2, colour, width, text index (1-based into FrameTexts()). The first
  // record is a header (kind 0). Coordinates are fractions of the screen.
  public func Frame(game: GameInstance) -> array<Float> {
    let colour: Float;
    let labelled: Bool = false;
    let i: Int32;
    ArrayClear(this.texts);
    ArrayClear(this.buf);
    if !this.hasVehicle || !this.BeginFrame(game) {
      return this.buf;
    };
    ArrayPush(this.buf, 0.0);
    ArrayPush(this.buf, 0.0);
    i = 0;
    while i < 6 {
      ArrayPush(this.buf, 0.0);
      i += 1;
    };

    // Trail: green = gripping, yellow = cornering hard, red = sliding, magenta = airborne.
    i = this.hideTrail ? ArraySize(this.trail) : 1;
    while i < ArraySize(this.trail) {
      this.Line(game, this.trail[i - 1], this.trail[i], this.SlipColour(this.trailSlip[i], this.trailAir[i]), 3.0);
      i += 1;
    };

    // Look-ahead rays.
    i = this.hideRays ? ArraySize(this.rayStart) : 0;
    while i < ArraySize(this.rayStart) {
      colour = BCAIDebug.Grey();
      if this.rayKind[i] == 1 {
        colour = BCAIDebug.Orange();
      } else {
        if this.rayKind[i] == 2 {
          colour = BCAIDebug.Red();
        } else {
          if this.rayKind[i] == 3 {
            colour = BCAIDebug.Yellow();
          };
        };
      };
      this.Line(game, this.rayStart[i], this.rayEnd[i], colour, this.rayKind[i] == 0 ? 1.0 : 2.0);
      if this.rayKind[i] != 0 {
        this.Marker(game, this.rayEnd[i], 4.0, colour, this.rayKind[i] == 3 ? "" : FloatToStringPrec(Vector4.Distance(this.rayStart[i], this.rayEnd[i]), 0) + "m");
      };
      i += 1;
    };

    this.DrawLanes(game);

    // The route the AI is following, slightly above the road; Autodrive+'s
    // coloured by the speed it plans there against the speed now (blue: as
    // fast or faster, then yellow, orange, red: much slower), with the
    // slowest point ahead labelled.
    i = this.hideRoute ? ArraySize(this.route) : 1;
    let slowest: Int32 = -1;
    while i < ArraySize(this.route) {
      colour = BCAIDebug.Blue();
      if i < ArraySize(this.routeSpeed) {
        let ratio: Float = this.routeSpeed[i] / MaxF(this.speed, 5.0);
        colour = ratio >= 0.9 ? BCAIDebug.Blue() : ratio >= 0.7 ? BCAIDebug.Yellow() : ratio >= 0.45 ? BCAIDebug.Orange() : BCAIDebug.Red();
        if slowest < 0 || this.routeSpeed[i] < this.routeSpeed[slowest] {
          slowest = i;
        };
      };
      this.Line(game, this.route[i - 1] + new Vector4(0.0, 0.0, 0.4, 0.0), this.route[i] + new Vector4(0.0, 0.0, 0.4, 0.0), colour, 4.0);
      i += 1;
    };
    if slowest >= 0 && this.routeSpeed[slowest] < this.speed - 2.0 {
      this.Marker(game, this.route[slowest] + new Vector4(0.0, 0.0, 0.6, 0.0), 3.0, BCAIDebug.White(),
        FloatToStringPrec(this.routeSpeed[slowest] * 3.6, 0) + " km/h");
    };

    // Autodrive+: the road's edges, and what its sensor sees (orange cars,
    // red walls; the one in the way bigger, magenta).
    i = this.hideSensor ? ArraySize(this.ownRoom) : 2;
    while i + 1 < ArraySize(this.ownRoom) {
      this.Line(game, this.ownRoom[i - 2] + new Vector4(0.0, 0.0, 0.3, 0.0), this.ownRoom[i] + new Vector4(0.0, 0.0, 0.3, 0.0), BCAIDebug.Grey(), 1.0);
      this.Line(game, this.ownRoom[i - 1] + new Vector4(0.0, 0.0, 0.3, 0.0), this.ownRoom[i + 1] + new Vector4(0.0, 0.0, 0.3, 0.0), BCAIDebug.Grey(), 1.0);
      i += 2;
    };
    i = this.hideSensor ? ArraySize(this.ownSeen) : 0;
    while i < ArraySize(this.ownSeen) {
      let kind: Int32 = Cast<Int32>(this.ownSeen[i].W + 0.5);
      let point: Vector4 = new Vector4(this.ownSeen[i].X, this.ownSeen[i].Y, this.ownSeen[i].Z, 1.0);
      if kind > 2 {
        this.Marker(game, point, 6.0, BCAIDebug.Magenta(), labelled ? "" : this.blockLabel);
        labelled = true;
      } else {
        this.Marker(game, point, 2.5, kind == 2 ? BCAIDebug.Red() : BCAIDebug.Orange(), "");
      };
      i += 1;
    };

    // Where its current motion is taking it over the next 2 s.
    i = this.hidePredict ? ArraySize(this.predicted) : 1;
    while i < ArraySize(this.predicted) {
      this.Line(game, this.predicted[i - 1], this.predicted[i], BCAIDebug.Cyan(), 2.0);
      i += 1;
    };

    // Heading (white) vs. actual travel direction (cyan): the gap is the slide.
    this.Line(game, this.pos + this.up * 1.2, this.pos + this.up * 1.2 + this.fwd * 6.0, BCAIDebug.White(), 2.0);
    if this.speed > 1.0 {
      this.Line(game, this.pos + this.up * 1.2, this.pos + this.up * 1.2 + Vector4.Normalize(this.vel) * 6.0, BCAIDebug.Cyan(), 2.0);
    };
    if this.airborne {
      this.Marker(game, this.pos + this.up * 2.0, 10.0, BCAIDebug.Magenta(), "AIRBORNE");
    };
    // What is in the way, when the sensor has no point on it (a car the
    // game listed): the label just ahead on the path.
    if !labelled && NotEquals(this.blockLabel, "") && ArraySize(this.route) > 0 {
      this.Marker(game, this.route[0] + new Vector4(0.0, 0.0, 1.5, 0.0), 1.0, BCAIDebug.Magenta(), this.blockLabel);
    };

    // Destination: a line to it and a tall pillar on it.
    if this.hasTarget {
      this.Line(game, this.pos + this.up * 0.5, this.target + new Vector4(0.0, 0.0, 0.5, 0.0), BCAIDebug.Magenta(), 1.0);
      this.Line(game, this.target, this.target + new Vector4(0.0, 0.0, 30.0, 0.0), BCAIDebug.Magenta(), 3.0);
      this.Marker(game, this.target + new Vector4(0.0, 0.0, 30.0, 0.0), 8.0, BCAIDebug.Magenta(),
        "destination " + FloatToStringPrec(Vector4.Distance(this.pos, this.target), 0) + "m");
    };
    return this.buf;
  }

  public func FrameTexts() -> array<String> {
    return this.texts;
  }

  // A line through the world (lifted by lift metres) as screen segments, 4
  // floats each (x1, y1, x2, y2 as fractions of the screen), the part behind
  // the camera cut off. For the route line, which shows without the overlay.
  // withDepth: 6 floats per segment, the 4 above, then how far its start is
  // from the camera (m) and which segment of the polyline it is.
  public func ProjectPolyline(game: GameInstance, points: array<Vector4>, lift: Float, opt withDepth: Bool) -> array<Float> {
    let out: array<Float>;
    let near: Float = 0.5;
    let i: Int32 = 1;
    let up: Vector4 = new Vector4(0.0, 0.0, lift, 0.0);
    if ArraySize(points) < 2 || !this.BeginFrame(game) {
      return out;
    };
    while i < ArraySize(points) {
      let a: Vector4 = new Vector4(points[i - 1].X, points[i - 1].Y, points[i - 1].Z, 1.0) + up;
      let b: Vector4 = new Vector4(points[i].X, points[i].Y, points[i].Z, 1.0) + up;
      let da: Float = this.Depth(a);
      let db: Float = this.Depth(b);
      if da >= near || db >= near {
        if da < near {
          a = a + (b - a) * ((near - da) / (db - da));
        } else {
          if db < near {
            b = b + (a - b) * ((near - db) / (da - db));
          };
        };
        let sa: Vector4 = this.ToScreen(game, a);
        let sb: Vector4 = this.ToScreen(game, b);
        ArrayPush(out, sa.X);
        ArrayPush(out, sa.Y);
        ArrayPush(out, sb.X);
        ArrayPush(out, sb.Y);
        if withDepth {
          ArrayPush(out, MaxF(da, near));
          ArrayPush(out, Cast<Float>(i - 1));
        };
      };
      i += 1;
    };
    return out;
  }
}
