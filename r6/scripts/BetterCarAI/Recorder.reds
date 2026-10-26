// Better Car AI - drive recorder.
// Keeps the last 60 s of a drive (10 samples a second) as CSV lines. When the
// car spins out, crashes or stops dead during AI drive, it asks the CET half
// to save the recording a few seconds later, so the file covers both the
// lead-up and the aftermath.

public class BCAIRecorder extends IScriptable {

  private let lines: array<String>;
  private let startTime: Float;
  private let prevSpeeds: array<Float>;
  private let prevCollision: Bool;
  private let pendingReason: String;
  private let pendingAt: Float;
  private let lastEventAt: Float;
  private let lastAIDriveAt: Float;

  public static func Header() -> String {
    return "t,x,y,z,fwd_x,fwd_y,fwd_z,vel_x,vel_y,vel_z,speed_kmh,sideways_ms,yaw_dps,ground_m,airborne,"
      + "rays,nearest_car_m,nearest_wall_m,mode,cmd_state,cmd_max_kmh,cmd_min_kmh,sent_cap_kmh,"
      + "governor_cap_kmh,governor_reason,target_m,rpm,gear,susp_lat,susp_long,collision,event";
  }

  public func Reset() -> Void {
    ArrayClear(this.lines);
    ArrayClear(this.prevSpeeds);
    this.startTime = 0.0;
  }

  public func GetLines() -> array<String> {
    return this.lines;
  }

  // Asks for a save `delay` seconds from now (unless one is already pending).
  // The reason carries the event's recording time so the analyser can find it.
  public func Request(now: Float, reason: String, delay: Float) -> Void {
    if NotEquals(this.pendingReason, "") {
      return;
    };
    this.pendingReason = reason + " at t=" + FloatToStringPrec(now - this.startTime, 2);
    this.pendingAt = now + delay;
  }

  // The player braked, steered or pressed the autodrive button: whatever
  // happens next is their driving, not a wipe-out to save.
  public func PlayerTookOver() -> Void {
    this.lastAIDriveAt = -1000.0;
  }

  // The reason of a save that is due now, or "" (and clears it).
  public func TakePending(now: Float) -> String {
    let reason: String;
    if Equals(this.pendingReason, "") || now < this.pendingAt {
      return "";
    };
    reason = this.pendingReason;
    this.pendingReason = "";
    return reason;
  }

  public func Sample(now: Float, d: ref<BCAIDebug>, vehicle: wref<VehicleObject>, aiDriving: Bool, line: String) -> Void {
    let bb: ref<IBlackboard>;
    let def: ref<VehicleDef> = GetAllBlackboardDefs().Vehicle;
    let collision: Bool = false;
    let event: String = "";
    let speed: Float = d.Speed();
    let rpm: Float = 0.0;
    let gear: Int32 = 0;
    let suspLat: Float = 0.0;
    let suspLong: Float = 0.0;
    if !d.HasVehicle() {
      this.Reset();
      return;
    };
    if this.startTime <= 0.0 {
      this.startTime = now;
    };
    if aiDriving {
      this.lastAIDriveAt = now;
    };
    bb = vehicle.GetBlackboard();
    if IsDefined(bb) {
      rpm = bb.GetFloat(def.RPMValue);
      gear = bb.GetInt(def.GearValue);
      suspLat = bb.GetFloat(def.SuspensionTransversalForce);
      suspLong = bb.GetFloat(def.SuspensionLongitudinalForce);
      collision = bb.GetBool(def.Collision);
    };

    // Events, chosen so hard braking and tight low-speed turns don't count:
    // - spin: travelling half sideways, or turning harder than any tyre can
    //   hold (turn rate x speed > 20 m/s^2, about 2 g), above 36 km/h;
    // - collision: the vehicle's collision flag coming on above 36 km/h;
    // - impact: losing > 12 m/s within half a second (~2.4 g, more than brakes).
    if speed > 10.0 && (AbsF(d.Lateral()) > speed * 0.5 || Deg2Rad(AbsF(d.YawRateDeg())) * speed > 20.0) {
      event = "spin";
    };
    if collision && !this.prevCollision && speed > 10.0 {
      event = "collision";
    };
    if ArraySize(this.prevSpeeds) >= 5 && this.prevSpeeds[0] - speed > 12.0 {
      event = "impact";
    };
    this.prevCollision = collision;
    ArrayPush(this.prevSpeeds, speed);
    if ArraySize(this.prevSpeeds) > 5 {
      ArrayErase(this.prevSpeeds, 0);
    };
    // Only AI drive's wipe-outs are saved automatically (it may have just
    // been switched off by the crash itself), at most one every 15 s.
    if NotEquals(event, "") && now - this.lastAIDriveAt < 3.0 && now - this.lastEventAt > 15.0 {
      this.lastEventAt = now;
      this.Request(now, event, 5.0);
    };

    ArrayPush(this.lines, FloatToStringPrec(now - this.startTime, 2) + "," + line
      + "," + FloatToStringPrec(rpm, 0) + "," + ToString(gear)
      + "," + FloatToStringPrec(suspLat, 2) + "," + FloatToStringPrec(suspLong, 2)
      + "," + (collision ? "1" : "0") + "," + event);
    while ArraySize(this.lines) > 600 {
      ArrayErase(this.lines, 0);
    };
  }
}
