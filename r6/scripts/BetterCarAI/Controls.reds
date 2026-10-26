// Better Car AI - the player's driving inputs, for the native plugin's
// control probe: while the player drives, it matches these against the car's
// memory to find where the car keeps its throttle, brake and steering.

public class BCAIInputWatch extends IScriptable {
  private let player: wref<PlayerPuppet>;
  public let throttle: Float;
  public let brake: Float;
  public let steer: Float;
  public let handbrake: Float;

  public func Start(player: ref<PlayerPuppet>) -> Void {
    this.player = player;
    player.RegisterInputListener(this, n"Accelerate");
    player.RegisterInputListener(this, n"Decelerate");
    player.RegisterInputListener(this, n"TurnX");
    player.RegisterInputListener(this, n"Handbrake");
  }

  public func Stop() -> Void {
    if IsDefined(this.player) {
      this.player.UnregisterInputListener(this, n"Accelerate");
      this.player.UnregisterInputListener(this, n"Decelerate");
      this.player.UnregisterInputListener(this, n"TurnX");
      this.player.UnregisterInputListener(this, n"Handbrake");
    };
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
