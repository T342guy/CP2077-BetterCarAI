// BCAI Autodrive+: the interface the plugin uses (Plugin.hpp), starting and
// stopping, and each frame: how the car moves, steering, the speed to go,
// the pedals, backing up when stuck, and learning how the car answers its
// controls. Driver.hpp says where the other parts are.

#include "Driver.hpp"

#include <mutex>

namespace BCAI
{
namespace Autodrive
{
std::string Driver::Start(const LaneGraph& aGraph, const Point3& aPos, const Point3& aForward, const Point3* aGoal)
{
    CarResponse car = mCar; // what it learned about the car stays
    float front = mFront, rear = mRear, halfWidth = mHalfWidth;
    std::deque<Event> events = std::move(mEvents); // (and what the log hasn't taken yet)
    *this = Driver{};
    mCar = car;
    mEvents = std::move(events);
    mFront = front;
    mRear = rear;
    mHalfWidth = halfWidth;
    mHasGoal = aGoal != nullptr;
    if (aGoal)
        mGoal = *aGoal;
    std::string msg;
    float fl = std::hypot(aForward.x, aForward.y);
    Point3 f = fl > 1e-3f ? Point3{aForward.x / fl, aForward.y / fl, 0} : Point3{1, 0, 0};
    mPos = aPos;
    mFwd = f;
    if (!Plan(aGraph, aPos, &f, nullptr, msg))
    {
        Note("could not start: %s", msg.c_str());
        return status = msg;
    }
    active = true;
    Note("start: %s, %s", msg.c_str(), mHasGoal ? "to the destination" : "cruising along the road");
    return msg;
}

void Driver::Stop(const std::string& aWhy)
{
    if (active)
        Note("stopped: %s (%.0f m driven off the road, %d replans, %d moves of 2 m or more, %d changes of mind)",
             aWhy.c_str(), mOffRoad, mReplans, mBigMoves, mDithers);
    active = false;
    status = aWhy;
}

bool Driver::Step(const LaneGraph& aGraph, const Point3& aPos, const Point3& aFwd, const Point3& aVel, float aDt,
                  const DriverSettings& aSet, DriverOutput& aOut)
{
    aOut = DriverOutput{};
    if (!active)
        return false;
    const Motion m = Feel(aPos, aFwd, aVel, aDt, aSet);
    aOut.speed = m.v;
    UseSettings(aSet);

    // Where the car is along the path (and whether it has arrived).
    float lateral;
    if (int route = KeepOnRoute(aGraph, aPos, m, aOut, lateral); route != 0)
        return route > 0;

    // What the sensor saw: forget the old, then pick where to drive.
    ForgetOldSightings();
    mLateral = lateral;
    bool offRoad;
    {
        // Driven off the mapped road (more than half a metre past it).
        const PathPoint& pt = mPath[Index(mProgress)];
        offRoad = lateral < std::min(pt.lo, 0.0f) - 0.5f || lateral > std::max(pt.hi, 0.0f) + 0.5f;
        if (offRoad)
            mOffRoad += std::fabs(m.v) * m.dt;
    }
    Avoid(std::max(m.v, 0.0f), m.dt);
    aOut.lateralError = lateral - mShift.At(mProgress);
    mLastLateral = aOut.lateralError;

    float maxSteer, wantRate;
    float maxRate = Steer(aPos, m, maxSteer, wantRate);
    float followNeed = 0.0f;
    float targetSpeed = TargetSpeed(m, maxSteer, wantRate, followNeed);
    aOut.targetSpeed = targetSpeed;
    Pedals(m, targetSpeed, followNeed, aOut);
    if (!Unstick(m, maxRate, aOut))
        return false;
    aOut.steer = mSteer;
    aOut.state = mStuck > 4.0f ? 3 : 0;
    NoteFrame(m, aOut, offRoad);
    mLast = aOut;
    mLastWantRate = wantRate;
    mLastPlanSpeed = SpeedAt(mProgress);
    mLastSlip = m.slip;
    return true;
}

// How the car moves: speed, turn rate, acceleration, the slope, the controls
// as the car feels them; and what that teaches about the car.
Motion Driver::Feel(const Point3& aPos, const Point3& aFwd, const Point3& aVel, float aDt, const DriverSettings& aSet)
{
    Motion m;
    m.dt = std::clamp(aDt, 0.001f, 0.1f);
    const float dt = m.dt;
    float fl = std::hypot(aFwd.x, aFwd.y);
    m.f = fl > 1e-3f ? Point3{aFwd.x / fl, aFwd.y / fl, 0} : Point3{1, 0, 0};
    const Point3& f = m.f;
    m.left = {-f.y, f.x, 0};
    m.v = aVel.x * f.x + aVel.y * f.y;
    m.vLat = aVel.x * m.left.x + aVel.y * m.left.y;
    const float v = m.v;
    mPos = aPos;
    mFwd = f;
    mClock += dt;
    if (aSet.car != mCar.car)
        mCar.Reset(aSet.car);
    // Uphill slows the car by this much (m/s^2).
    float f3 = std::sqrt(aFwd.x * aFwd.x + aFwd.y * aFwd.y + aFwd.z * aFwd.z);
    m.gravity = f3 > 1e-3f ? -9.81f * aFwd.z / f3 : 0.0f;
    mSlope = fl > 1e-3f ? std::clamp(aFwd.z / fl, -1.0f, 1.0f) : 0.0f;

    // Turn rate, from the heading (counter-clockwise = left = positive),
    // and acceleration, from the speed.
    float heading = std::atan2(f.y, f.x);
    if (mHaveHeading)
    {
        float r = Wrap(heading - mHeading) / dt;
        mYawRate += (r - mYawRate) * std::min(1.0f, dt / 0.08f);
        mAccel += ((v - mLastV) / dt - mAccel) * std::min(1.0f, dt / 0.25f);
    }
    mHeading = heading;
    mLastV = v;
    mHaveHeading = true;
    // The controls as the car feels them: the game turns the wheel and
    // builds up the pedals with some delay.
    mSteerFelt += (mSteer - mSteerFelt) * std::min(1.0f, dt / 0.2f);
    mThrottleFelt += (mLast.throttle - mThrottleFelt) * std::min(1.0f, dt / 0.25f);
    mBrakeFelt += (mLast.brake - mBrakeFelt) * std::min(1.0f, dt / 0.25f);
    // How long the pedals have stayed as they are (coasting, throttle
    // alone, brake alone): only then does the speed show what they do.
    int pedals = mLast.brake > 0.0f ? 2 : mLast.throttle > 0.0f ? 1 : 0;
    mPedalTime = pedals == mPedals ? mPedalTime + dt : 0.0f;
    mPedals = pedals;
    m.slip = v > 1.0f ? std::atan2(m.vLat, v) : 0.0f;
    LearnResponse(v, m.slip, m.gravity, dt);
    return m;
}

// New settings (or newly learned limits): the speed plan again.
void Driver::UseSettings(const DriverSettings& aSet)
{
    if (mSettings.maxSpeed != aSet.maxSpeed || mSettings.latAccel != aSet.latAccel ||
        mSettings.laneSpeedFactor != aSet.laneSpeedFactor || mSettings.decel != aSet.decel ||
        std::fabs(LatAccel() - mProfileLat) > 0.25f || std::fabs(Decel() - mProfileDecel) > 0.25f)
    {
        mSettings = aSet;
        Profile();
    }
    mSettings.followTime = aSet.followTime;
    mSettings.style = std::clamp(aSet.style, 0, 2);
    NoteSettings(aSet);
}

// Finds the car on the path (planning again when it is lost), stops at the
// destination and, cruising, extends the road ahead. 0: driving on; 1:
// arriving (this frame's controls are set); -1: stopped.
int Driver::KeepOnRoute(const LaneGraph& aGraph, const Point3& aPos, const Motion& m, DriverOutput& aOut, float& aLateral)
{
    bool located = Locate(aPos, aLateral);
    if (!located || std::fabs(aLateral - mShift.At(mProgress)) > 8.0f)
    {
        std::string lost = located ? Format("off its line by %.1f m", aLateral - mShift.At(mProgress))
                                   : std::string("lost its place on the route");
        std::string msg;
        if (!Plan(aGraph, aPos, &m.f, nullptr, msg) || !Locate(aPos, aLateral))
        {
            Stop("lost the route: " + msg);
            return -1;
        }
        ++mReplans;
        Note("%s, planned again: %s", lost.c_str(), msg.c_str());
    }
    float remaining = mS.back() - mProgress;
    if (mHasGoal && remaining < 4.0f)
    {
        aOut.handbrake = 1.0f;
        aOut.state = 1;
        if (!mNoted.arriving)
            Note("arriving: %.1f m to the destination, stopping", remaining);
        mNoted.arriving = true;
        if (std::fabs(m.v) < 0.5f)
        {
            Stop("arrived");
            return -1;
        }
        return 1;
    }
    // Cruising: extend the road ahead before it runs out.
    if (!mHasGoal && remaining < 300.0f)
    {
        std::string msg;
        size_t i = Index(mProgress);
        Spot from{mPath[i].lane, mPath[i].laneS, 0};
        if (Plan(aGraph, aPos, &m.f, &from, msg))
            Locate(aPos, aLateral);
    }
    return 0;
}

void Driver::ForgetOldSightings()
{
    mSeen.erase(std::remove_if(mSeen.begin(), mSeen.end(),
                               [&](const Seen& o)
                               {
                                   // Close by, the rays may not hit something standing
                                   // again (beside it, or going round it): keep it
                                   // longer (a ray through where it was clears it
                                   // sooner). A moving car's old spots go soon (the
                                   // rays look at each spot about every 0.1 s).
                                   bool keep = o.kind != 0 ||
                                               (o.s < mProgress + mFront + 25.0f && o.time - o.born > 0.5f);
                                   return mClock - o.time > (keep ? 3.0f : 0.4f) || o.s < mProgress - kBehind;
                               }),
                mSeen.end());
}

// Steering: pure pursuit gives the curvature to drive; the learned response
// turns it into a steering input, and the difference to the curvature the
// car does drive trims it. Returns how far the wheel may turn this frame.
float Driver::Steer(const Point3& aPos, const Motion& m, float& aMaxSteer, float& aWantRate)
{
    const float v = m.v, dt = m.dt;
    const Point3 &f = m.f, &left = m.left;
    float lookahead = std::clamp(4.0f + 0.5f * std::fabs(v), 5.0f, 40.0f);
    Point3 target = Driven(mProgress + lookahead);
    float dx = target.x - aPos.x, dy = target.y - aPos.y;
    float xl = dx * f.x + dy * f.y, yl = dx * left.x + dy * left.y;
    float d2 = std::max(xl * xl + yl * yl, 1.0f);
    float curvature = 2.0f * yl / d2;
    aWantRate = v * curvature;
    aMaxSteer = std::clamp(1.3f - 0.02f * std::fabs(v), 0.45f, 1.0f);
    float curvCmd = curvature;
    if (v > 3.0f)
    {
        float curvErr = curvature - mYawRate / v;
        if (std::fabs(mSteer) < aMaxSteer - 0.02f)
            mCurvTrim = std::clamp(mCurvTrim + 0.6f * curvErr * dt, -0.01f, 0.01f);
        curvCmd += 0.3f * curvErr + mCurvTrim;
    }
    else
        mCurvTrim *= 1.0f - std::min(1.0f, dt);
    float steerLeft = std::copysign(mCar.steer.X(std::fabs(curvCmd) * CarResponse::SpeedTurn(v)), curvCmd);
    if (v < 0.0f)
        steerLeft = -steerLeft;
    float steer = std::clamp(-steerLeft * mSteerSign, -aMaxSteer, aMaxSteer); // TurnX: + is right
    float maxRate = std::clamp(3.5f - 0.07f * std::fabs(v), 0.6f, 3.5f) * dt;
    mSteer += std::clamp(steer - mSteer, -maxRate, maxRate);
    LearnSteerSign(v, dt);
    return maxRate;
}

// The speed to go now: the plan just ahead, capped by what is in the way,
// a junction to be able to stop at, going round, dropping back and grip.
// Sets mSpeedWhy (what holds it down) and how hard following has to slow.
float Driver::TargetSpeed(const Motion& m, float aMaxSteer, float aWantRate, float& aFollowNeed)
{
    const float v = m.v, dt = m.dt;
    const StyleRules& rules = Rules();
    // The profile just ahead (the car reacts with some delay).
    float targetSpeed = 1e9f;
    for (float ahead = 0.0f; ahead <= std::max(0.5f * v, 1.0f); ahead += 1.0f)
        targetSpeed = std::min(targetSpeed, SpeedAt(mProgress + ahead));
    // (What holds the speed down, for the overlay.)
    {
        float road = std::min(mSettings.maxSpeed, std::max(mLimit[Index(mProgress)] * mSettings.laneSpeedFactor, 8.0f));
        mSpeedWhy = targetSpeed >= road - 0.5f ? (road >= mSettings.maxSpeed - 0.1f ? "top speed" : "speed limit")
                    : mHasGoal && mS.back() - mProgress < 150.0f ? "arriving"
                                                                  : "bend ahead";
    }
    auto cap = [&](float aSpeed, const char* aWhy)
    {
        if (aSpeed < targetSpeed)
        {
            targetSpeed = aSpeed;
            mSpeedWhy = aWhy;
        }
    };
    // Following: what is in the way and how fast it goes.
    aFollowNeed = 0.0f; // the slowing down needed to stop behind it (or match its speed), m/s^2
    if (mBlock >= 0.0f)
    {
        // How fast it goes: the car's measured speed along the path
        // (something new counts as standing still until measured; walls
        // don't move).
        float lead = 0.0f;
        if (mBlockMeasured)
            lead = std::max(mBlockU, 0.0f);
        else if (mBlockKind == 0 && mLeadGap >= 0.0f && std::fabs(mBlock - mLeadGap) < 3.0f)
            // The car followed a moment ago, seen afresh (not measured
            // again yet): it can't have stopped dead.
            lead = std::max(mLeadSpeed - 6.0f * dt, 0.0f);
        mLeadSpeed += (lead - mLeadSpeed) * std::min(1.0f, dt / 0.3f);
        if (lead == 0.0f)
            mLeadSpeed = 0.0f;
        mLeadGap = mLeadSpeed > 0.0f ? mBlock : -1.0f;
        // Further back from something standing still, so there is room
        // to steer round it, slowing down more gently.
        // (Something not measured yet: firmer, later, as it is most
        // likely moving.)
        bool moving = mLeadSpeed >= 1.0f;
        bool known = mBlockKind != 0 || mBlockMeasured;
        const float minGap = moving ? rules.followGap : 7.0f,
                    b = moving || !known ? std::min(6.0f, 0.75f * BrakeMax()) : std::min(4.0f, 0.5f * BrakeMax());
        float gap = mBlock - minGap;
        float safe = gap > 0.0f ? std::sqrt(mLeadSpeed * mLeadSpeed + 2.0f * b * gap) : 0.0f;
        float wanted = minGap + v * mSettings.followTime;
        if (moving && mBlock < wanted)
            safe = std::min(safe, std::max(0.0f, mLeadSpeed + 0.4f * (mBlock - wanted)));
        // (Coming the other way, while the car moves back out of its
        // lane: not stopping there, where it can't move sideways.)
        if (mBlockMeasured && mBlockU < -1.0f && mReturning)
            safe = std::max(safe, std::min(std::max(v, 0.0f), 5.0f));
        cap(safe, mBlockKind == 1 ? "something in the way" : moving ? "following" : "stopping behind");
        if (v > mLeadSpeed)
            aFollowNeed = (v * v - mLeadSpeed * mLeadSpeed) / (2.0f * std::max(gap, 1.0f));
    }
    else
        mLeadGap = -1.0f;
    // (Able to stop before a junction where a car across may go.)
    if (mCautionGap >= 0.0f)
        cap(std::sqrt(2.0f * rules.cautionDecel * std::max(mCautionGap - 3.0f, 0.0f)) + rules.cautionCreep,
            "junction: car across");
    // Going round something standing: past it at no more than about
    // 30 km/h (in case the way round closes; a madman about 40).
    if (mRoundGap >= 0.0f)
        cap(std::sqrt(rules.roundBase + 8.0f * std::max(mRoundGap - 5.0f, 0.0f)), "going round");
    if (mDropBack >= 0.0f)
        cap(mDropBack, "dropping back");

    // Out of grip (sliding, or the wheel at its limit and the car still
    // turning too little): bring the speed down gradually until it grips.
    bool noGrip = v > 8.0f && (std::fabs(m.slip) > 0.1f ||
                               (std::fabs(mSteer) >= aMaxSteer - 0.02f && aWantRate * mYawRate >= 0.0f &&
                                std::fabs(mYawRate) < 0.7f * std::fabs(aWantRate)));
    if (noGrip)
        mGripSpeed = std::min(mGripSpeed, v) - 3.0f * dt;
    else
        mGripSpeed = std::min(mGripSpeed + 3.0f * dt, 100.0f);
    // This car's grip: where it slid, or ran wide with the wheel at its
    // limit, in a real corner (not the wheel's own limit at low sideways
    // acceleration); creeping back up while it corners near that cleanly.
    {
        float side = std::fabs(v * mYawRate), g = mCar.grip > 0.0f ? mCar.grip : mSettings.latAccel;
        bool slid = v > 8.0f && side > 4.0f &&
                    (std::fabs(m.slip) > 0.1f || (noGrip && std::fabs(mSteer) >= aMaxSteer - 0.02f));
        if (slid && side < g)
            mCar.grip = g + (std::max(side, 0.45f * 9.81f) - g) * std::min(1.0f, dt / 0.3f);
        else if (mCar.grip > 0.0f && !noGrip && side > 0.85f * g)
            mCar.grip = std::min(g + 0.1f * dt, mSettings.latAccel);
    }
    cap(mGripSpeed, "losing grip");
    NoteSpeed(targetSpeed, noGrip, m.slip);
    return targetSpeed;
}

// The pedals for the target speed. The acceleration wanted: close the speed
// gap in a bit over a second, plus the slowing down the plan asks for right
// here; it changes smoothly (harder only when braking hard).
void Driver::Pedals(const Motion& m, float aTargetSpeed, float aFollowNeed, DriverOutput& aOut)
{
    const float v = m.v, dt = m.dt, targetSpeed = aTargetSpeed;
    float err = targetSpeed - v;
    float span = std::max(5.0f, 0.5f * v);
    float planAccel = (SpeedAt(mProgress + span) - SpeedAt(mProgress)) / span * std::max(v, 0.0f);
    // (Likewise for what it follows, once that takes more than gentle
    // slowing: else it would run on a few m/s too fast all the way.)
    float followAccel = targetSpeed < v + 2.0f ? -std::max(aFollowNeed - 1.0f, 0.0f) : 0.0f;
    float want = std::clamp(0.8f * err + std::min({planAccel, followAccel, 0.0f}), -9.0f, 4.0f);
    // No speeding up only to brake again shortly: if a slower stretch
    // ahead couldn't be reached by gently slowing down from here, coast.
    float coastTo = 1e9f;
    for (float ahead = 5.0f; ahead <= 160.0f && mProgress + ahead <= mS.back(); ahead += 5.0f)
    {
        float vAhead = SpeedAt(mProgress + ahead);
        coastTo = std::min(coastTo, std::sqrt(vAhead * vAhead + 2.0f * 1.5f * ahead));
    }
    if (v >= coastTo)
        want = std::min(want, CarResponse::Lookup(mCar.coast, v) + m.gravity);
    float down = (want < -3.0f ? 25.0f : 8.0f) * dt;
    mAccelWant += std::clamp(want - mAccelWant, -down, 8.0f * dt);
    if (targetSpeed < 0.5f && v < 1.5f)
    {
        // Stopped or nearly: hold it with the handbrake (the brake
        // pedal reverses a stopped car).
        aOut.handbrake = 1.0f;
        mAccelTrim = 0.0f;
        mAccelWant = 0.0f;
    }
    else
    {
        // What the pedals have to add to coasting and the slope, with a
        // slow trim for what the learned response gets wrong (against
        // the wanted acceleration as delayed as the car's answer).
        mAccelLagged += (mAccelWant - mAccelLagged) * std::min(1.0f, dt / 0.4f);
        float force = mAccelWant + mAccelTrim - m.gravity - CarResponse::Lookup(mCar.coast, v);
        if (force > 0.0f)
            aOut.throttle = std::clamp(force / CarResponse::Lookup(mCar.full, v), 0.0f, 1.0f);
        else if (force < -0.25f)
            aOut.brake = mCar.brake.X(-force);
        bool saturated = aOut.throttle >= 1.0f || aOut.brake >= 1.0f;
        if (v > 2.0f && !saturated)
            mAccelTrim = std::clamp(mAccelTrim + 0.4f * (mAccelLagged - mAccel) * dt, -1.5f, 1.5f);
        if (v < 1.0f && aOut.brake > 0.0f)
        {
            aOut.brake = 0.0f;
            aOut.handbrake = 1.0f;
        }
    }
    // Traction: ease off while sliding.
    if (v > 5.0f)
    {
        float slip = std::fabs(m.slip);
        if (slip > 0.25f)
            aOut.throttle = 0.0f;
        else if (slip > 0.12f)
            aOut.throttle *= 0.3f;
    }
}

// Stuck (pressing on without moving), or boxed in (stopped close behind
// something standing with no way past it): back up a few metres, wheel
// straight, unless something is close behind. False when it gives up.
bool Driver::Unstick(const Motion& m, float aMaxRate, DriverOutput& aOut)
{
    const float v = m.v, dt = m.dt;
    mStuck = (aOut.throttle > 0.4f && std::fabs(v) < 0.3f) ? mStuck + dt : 0.0f;
    bool boxed = std::fabs(v) < 0.5f && mBlock >= 0.0f && mBlock < 12.0f && mLeadSpeed < 0.5f && mNoWay && mDoor;
    mBoxed = boxed ? mBoxed + dt : 0.0f;
    if (mReverse <= 0.0f && (mStuck > 2.5f || mBoxed > 2.5f) && mRearClear > 2.0f)
    {
        Note("backing up (%s), %.1f m clear behind, %s %.1f m ahead, time %d",
             mStuck > 2.5f ? "pressing on without moving" : "boxed in behind something with no way past",
             std::min(mRearClear, 99.0f), mBlockKind == 1 ? "wall" : "car", mBlock, mReversals + 1);
        mReverse = 8.0f;
        mReverseGap = mBlock;
        mStuck = mBoxed = 0.0f;
        if (++mReversals > 6)
        {
            Stop("stuck: backed up 6 times without getting past");
            return false;
        }
    }
    if (mReverse > 0.0f)
    {
        mReverse -= dt;
        // (Until a way past opens up, something is close behind, or it
        // has backed up 20 m.)
        if (mRearClear < 1.5f || !mNoWay || (mReverseGap >= 0.0f && mBlock >= mReverseGap + 20.0f))
            mReverse = std::min(mReverse, 0.0f);
        if (mReverse <= 0.0f)
            Note("done backing up: %s", mRearClear < 1.5f ? "something close behind"
                                        : !mNoWay        ? "a way past opened up"
                                        : mReverseGap >= 0.0f && mBlock >= mReverseGap + 20.0f ? "backed up 20 m"
                                                                                                : "8 s up");
        // The brake pedal reverses a stopped car; about 2 m/s.
        aOut = DriverOutput{};
        aOut.speed = v;
        aOut.targetSpeed = -2.0f;
        if (mReverse > 0.0f && v > -2.0f)
            aOut.brake = std::clamp(0.3f + 0.3f * (v + 2.0f), 0.0f, 0.6f);
        else if (mReverse <= 0.0f)
            aOut.handbrake = std::fabs(v) > 0.3f ? 1.0f : 0.0f;
        mSteer += std::clamp(-mSteer, -aMaxRate, aMaxRate);
        mAccelWant = mAccelTrim = 0.0f;
    }
    return true;
}

// Learns how the car answers its controls, from steady stretches without
// sliding: curvature for a steering input, slowing for a brake input,
// speeding up at full throttle and coasting.
void Driver::LearnResponse(float v, float aSlip, float aGravity, float dt)
{
    if (v < 3.0f || std::fabs(aSlip) > 0.06f || mLast.handbrake > 0.0f)
        return;
    float keep = std::exp(-dt / 45.0f);
    float pedals = mAccel - aGravity; // what the engine, brakes and drag did
    float coast = CarResponse::Lookup(mCar.coast, v);
    bool settled = mPedalTime > 0.8f;
    if (settled && mPedals == 0)
        CarResponse::Learn(mCar.coast, v, std::clamp(pedals, -4.0f, 0.5f), std::min(1.0f, dt / 2.0f));
    else if (settled && mPedals == 1 && mThrottleFelt > 0.5f && std::fabs(mLast.throttle - mThrottleFelt) < 0.1f)
    {
        float full = (pedals - coast) / mThrottleFelt;
        if (full > 0.3f)
            CarResponse::Learn(mCar.full, v, std::min(full, 12.0f), std::min(1.0f, dt / 3.0f));
    }
    else if (mPedals == 2 && mPedalTime > 0.5f && mBrakeFelt > 0.005f && std::fabs(mLast.brake - mBrakeFelt) < 0.1f)
    {
        float decel = coast - pedals;
        if (decel > 0.3f)
            mCar.brake.Add(mBrakeFelt, std::min(decel, 15.0f), dt, keep);
    }
    // Steering: once the sign is known, while the wheel holds still.
    float steerLeft = -mSteerFelt * mSteerSign;
    float curv = mYawRate / v;
    if (mSignKnown && v > 4.0f && std::fabs(steerLeft) > 0.05f && std::fabs(mSteer - mSteerFelt) < 0.05f &&
        std::fabs(aSlip) < 0.05f && steerLeft * curv > 0.0f && std::fabs(mYawRate) > 0.01f &&
        std::fabs(mYawRate * v) < 6.0f)
        mCar.steer.Add(std::fabs(steerLeft), std::fabs(curv) * CarResponse::SpeedTurn(v), dt, keep);
}

// Positive TurnX should turn the car right (clockwise). If a few seconds
// of driving show the opposite, flip it.
void Driver::LearnSteerSign(float v, float dt)
{
    if (mSignKnown || v < 4.0f || std::fabs(mSteer) < 0.15f)
        return;
    mSignEvidence += -mSteer * mYawRate * dt;
    if (mSignEvidence > 0.3f)
        mSignKnown = true;
    else if (mSignEvidence < -0.3f)
    {
        mSteerSign = -mSteerSign;
        mSteer = -mSteer;
        mSignKnown = true;
        Note("steering turned the other way than expected: flipped it");
        Warn("Autodrive+: steering turned the other way than expected, flipped");
    }
}
} // namespace Autodrive

namespace
{
std::mutex gDriverMutex;
Autodrive::Driver gDriver;
} // namespace

std::string DriverStart(void* aAutodrive, const Point3& aPos, const Point3& aForward, const Point3* aGoal)
{
    std::lock_guard lock(gDriverMutex);
    if (aAutodrive && !EnsureLanes(aAutodrive))
        return "lane map not found";
    std::string result = gDriver.Start(Lanes(), aPos, aForward, aGoal);
    Log("Autodrive+: %s", result.c_str());
    return result;
}

bool DriverStep(const Point3& aPos, const Point3& aForward, const Point3& aVelocity, float aDt,
                const DriverSettings& aSettings, DriverOutput& aOut)
{
    std::lock_guard lock(gDriverMutex);
    bool running = gDriver.Step(Lanes(), aPos, aForward, aVelocity, aDt, aSettings, aOut);
    if (!running && aOut.state == 0)
        aOut.state = 2;
    return running;
}

void DriverStop(const std::string& aWhy)
{
    std::lock_guard lock(gDriverMutex);
    if (gDriver.active)
        gDriver.Stop(aWhy);
}

std::vector<DriverRay> DriverRays(float aFront)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Rays(aFront);
}

size_t DriverSense(const std::vector<Point3>& aEnds, const std::vector<bool>& aHit)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Sense(aEnds, aHit);
}

void DriverBody(float aFront, float aRear, float aHalfWidth)
{
    std::lock_guard lock(gDriverMutex);
    gDriver.SetBody(aFront, aRear, aHalfWidth);
}

size_t DriverVehicles(const std::vector<DriverVehicle>& aList)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.SetVehicles(aList);
}

std::vector<DriverSeenPoint> DriverSeen()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.SeenPoints();
}

DriverInfo DriverState()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.State();
}

std::vector<Point3> DriverAhead(float aDistance, float aStep)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Ahead(aDistance, aStep);
}

std::vector<float> DriverAheadSpeeds(float aDistance, float aStep)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.AheadSpeeds(aDistance, aStep);
}

std::vector<Point3> DriverRoom(float aDistance, float aStep)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Room(aDistance, aStep);
}

std::string DriverDumpPath(std::filesystem::path& aOutPath)
{
    std::lock_guard lock(gDriverMutex);
    aOutPath = DumpDir() / "driver_path.txt";
    return gDriver.Dump(aOutPath);
}

std::string DriverTelemetry()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Telemetry();
}

float DriverOffRoad()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.OffRoad();
}

float DriverRoadOut(float aHalf)
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.RoadOut(aHalf);
}

int DriverBigMoves()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.BigMoves();
}

int DriverDithers()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Dithers();
}

std::string DriverHud()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Hud();
}

std::vector<std::string> DriverEvents()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.TakeEvents();
}

std::string DriverStatus()
{
    std::lock_guard lock(gDriverMutex);
    return gDriver.Describe();
}
} // namespace BCAI
