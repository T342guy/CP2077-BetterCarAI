// Better Car AI - the player's driving inputs, for the native plugin's
// control probe: while the player drives, it matches these against the car's
// memory to find where the car keeps its throttle, brake and steering.

public class BCAIInputWatch extends IScriptable {
  private let player: wref<PlayerPuppet>;
  public let throttle: Float;
  public let brake: Float;
  public let steer: Float;
  public let handbrake: Float;
  // The autodrive button held (with the game's autodrive HUD shown, the
  // game's own request may not come through).
  public let autodriveButton: Bool;

  public func Start(player: ref<PlayerPuppet>) -> Void {
    this.player = player;
    player.RegisterInputListener(this, n"Accelerate");
    player.RegisterInputListener(this, n"Decelerate");
    player.RegisterInputListener(this, n"TurnX");
    player.RegisterInputListener(this, n"Handbrake");
    player.RegisterInputListener(this, n"HoldAutodrive");
  }

  public func Stop() -> Void {
    if IsDefined(this.player) {
      this.player.UnregisterInputListener(this, n"Accelerate");
      this.player.UnregisterInputListener(this, n"Decelerate");
      this.player.UnregisterInputListener(this, n"TurnX");
      this.player.UnregisterInputListener(this, n"Handbrake");
      this.player.UnregisterInputListener(this, n"HoldAutodrive");
    };
    this.autodriveButton = false;
    this.player = null;
    this.throttle = 0.0;
    this.brake = 0.0;
    this.steer = 0.0;
    this.handbrake = 0.0;
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    let value: Float = ListenerAction.GetValue(action);
    if Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      value = 0.0;
    };
    if ListenerAction.IsAction(action, n"HoldAutodrive") {
      if Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_HOLD_COMPLETE) {
        this.autodriveButton = true;
      };
      return false;
    };
    if ListenerAction.IsAction(action, n"Accelerate") {
      this.throttle = value;
    } else {
      if ListenerAction.IsAction(action, n"Decelerate") {
        this.brake = value;
      } else {
        if ListenerAction.IsAction(action, n"TurnX") {
          this.steer = value;
        } else {
          if ListenerAction.IsAction(action, n"Handbrake") {
            this.handbrake = value;
          };
        };
      };
    };
    return false;
  }
}
