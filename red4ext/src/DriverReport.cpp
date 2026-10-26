// BCAI Autodrive+: what it is doing and why, for the overlay (Hud), the log
// (Telemetry, Describe, and the decisions it makes as events) and the plan
// dump; and the points the overlay draws.

#include "Driver.hpp"

#include <cstring>

namespace BCAI::Autodrive
{
// How far a car aHalf wide on each side, where this one is, reaches past
// the mapped road's edge (0 on it).
float Driver::RoadOut(float aHalf) const
{
    if (mPath.empty())
        return 0.0f;
    const PathPoint& pt = mPath[Index(mProgress)];
    float hi = std::max(pt.hi, 0.0f) + mHalfWidth, lo = std::min(pt.lo, 0.0f) - mHalfWidth;
    return std::max({0.0f, mLateral + aHalf - hi, lo - (mLateral - aHalf)});
}

std::vector<DriverSeenPoint> Driver::SeenPoints() const
{
    std::vector<DriverSeenPoint> out;
    for (size_t i = 0; i < mSeen.size(); ++i)
        out.push_back({mSeen[i].p, mSeen[i].kind, int(i) == mBlockPoint});
    // (And the people on foot the game listed.)
    for (size_t k = 0; k < mKnown.size(); ++k)
        if (mKnown[k].person)
            out.push_back({KnownAt(mKnown[k]), 2, mBlockKind == 2 && mBlockKnown == int(k)});
    return out;
}

DriverInfo Driver::State() const
{
    DriverInfo info;
    info.obstacle = active ? mBlock : -1.0f;
    info.kind = mBlockKind;
    info.offset = mShift.to;
    info.lateral = mLastLateral;
    return info;
}

// Points along the path ahead of the car (from just ahead of its bumper),
// with the sideways move it is making.
std::vector<Point3> Driver::Ahead(float aDistance, float aStep) const
{
    std::vector<Point3> out;
    if (mPath.size() < 2)
        return out;
    for (float s = mFront + 0.3f; s <= aDistance && mProgress + s <= mS.back(); s += aStep)
        out.push_back(Driven(mProgress + s));
    return out;
}

// The planned speed at those points (m/s).
std::vector<float> Driver::AheadSpeeds(float aDistance, float aStep) const
{
    std::vector<float> out;
    if (mPath.size() < 2)
        return out;
    for (float s = mFront + 0.3f; s <= aDistance && mProgress + s <= mS.back(); s += aStep)
        out.push_back(SpeedAt(mProgress + s));
    return out;
}

// The road's edges along the path ahead: left, right, left, right...
std::vector<Point3> Driver::Room(float aDistance, float aStep) const
{
    std::vector<Point3> out;
    if (mPath.size() < 2)
        return out;
    for (float s = 0.0f; s <= aDistance && mProgress + s <= mS.back(); s += aStep)
    {
        float at = mProgress + s;
        const PathPoint& pt = mPath[Index(at)];
        Point3 c = At(at), n = Normal(at);
        float hi = pt.hi + mHalfWidth, lo = pt.lo - mHalfWidth;
        out.push_back({c.x + n.x * hi, c.y + n.y * hi, c.z});
        out.push_back({c.x + n.x * lo, c.y + n.y * lo, c.z});
    }
    return out;
}

std::string Driver::Describe() const
{
    return Format("%s; %s, %.0f m left, %d replans, steer sign %s", status.c_str(),
                  mHasGoal ? "to the destination" : "cruising", mPath.empty() ? 0.0f : mS.back() - mProgress,
                  mReplans, mSignKnown ? (mSteerSign > 0 ? "normal" : "flipped") : "unconfirmed");
}

// What it did last frame and why, for the log.
std::string Driver::Telemetry() const
{
    if (!active)
        return status;
    float v = mLast.speed;
    std::string ahead = "-";
    if (mBlock >= 0.0f)
    {
        const Seen* o = mBlockPoint >= 0 && mBlockPoint < int(mSeen.size()) ? &mSeen[mBlockPoint] : nullptr;
        ahead = Format("%s %.1f m (%+.1f sideways", mBlockKind == 0 ? "car" : mBlockKind == 2 ? "person" : "wall", mBlock,
                       o ? o->l - mShift.At(o->s) : 0.0f);
        if (mBlockMeasured)
            ahead += Format(", moving %.0f km/h along, %+.1f m/s across%s", mBlockU * 3.6f, mBlockW,
                            mBlockKnown >= 0 ? ", listed by the game" : "");
        ahead += ")";
    }
    const PathPoint& pt = mPath[Index(mProgress)];
    if (mReverse > 0.0f)
        ahead += Format(" (backing up, %.1f m clear behind)", std::min(mRearClear, 99.0f));
    if (mCautionGap >= 0.0f)
        ahead += Format(" (junction in %.0f m, a car across may go)", mCautionGap);
    if (mWideGap >= 0.0f)
        ahead += Format(" (a turn in %.0f m swings it wide, traffic coming)", mWideGap);
    if (mStartHits > 0)
        ahead += Format(" (%d rays started inside something)", mStartHits);
    if (mOffRoad > 0.5f)
        ahead += Format(" (%.0f m driven off the road so far)", mOffRoad);
    if (mCrosswalkGap >= 0.0f)
        ahead += Format(" (crosswalk in %.0f m, someone by it)", mCrosswalkGap);
    if (!mKnown.empty())
        ahead += Format(" (%zu vehicles and people listed by the game)", mKnown.size());
    return Format("v %.1f target %.1f (plan %.1f) ahead %s lead %.1f | heading for %+.1f m (now %+.1f, road "
                  "%+.1f..%+.1f, own side %+.1f..%+.1f%s, line %+.1f from the lane), %zu seen, %zu cars tracked | off %+.2f m, steer %+.2f, turn %+.2f wanted %+.2f, slip %+.2f | "
                  "accel %+.1f wanted %+.1f trim %+.1f | throttle %.2f brake %.2f handbrake %.0f | %.0f of %.0f m, %d replans | "
                  "learned: curvature %.2f*steer^%.1f, brake %.1f*brake^%.2f m/s2, full throttle %.1f, coasting "
                  "%+.1f m/s2, grip %.1f m/s2 | body %.1f/%.1f m, %.2f m wide",
                  v * 3.6f, mLast.targetSpeed * 3.6f, mLastPlanSpeed * 3.6f, ahead.c_str(), mLeadSpeed * 3.6f,
                  mShift.to, mShift.At(mProgress), pt.lo, pt.hi, pt.loSame, pt.hiSame, pt.junction ? ", junction" : "", pt.flow, mSeen.size(), mTracks.size(),
                  mLast.lateralError, mLast.steer, mYawRate, mLastWantRate, mLastSlip, mAccel, mAccelWant,
                  mAccelTrim, mLast.throttle, mLast.brake, mLast.handbrake, mProgress,
                  mPath.empty() ? 0.0f : mS.back(), mReplans, mCar.steer.k, mCar.steer.p, mCar.brake.k, mCar.brake.p,
                  CarResponse::Lookup(mCar.full, v), CarResponse::Lookup(mCar.coast, v), LatAccel(), mFront,
                  mRear, 2.0f * mHalfWidth);
}

// What it is doing, for the overlay: "key=value" pairs separated by ';'.
//   style 0..2; v, target, limit (km/h; limit: the road's, times the
//   speed factor, capped at the top speed); why: what holds the speed
//   down; doing: what it is up to; lane, lanes: which lane it is in and
//   heads for, counted from the right, of how many going its way;
//   ahead, aheadv, aheadkind (car, parked, stopped, wall): what is in
//   the way on its path; bend, bendv: the next bend slower than now;
//   left: metres to the destination (-1 cruising); offroad, replans,
//   moves, dithers: this drive so far; throttle, brake, steer (-1..1);
//   slip (degrees); grip (g, as learned); oncoming (1 in the oncoming
//   lanes); people: how many on foot are on the road ahead, or stepping
//   onto it; crosswalk: metres to a crosswalk with someone by it (-1 none).
std::string Driver::Hud() const
{
    if (!active || mPath.size() < 2)
        return Format("active=0;status=%s", status.c_str());
    // Lanes going this way here, and which the car is in and heads for.
    int n, lane = LaneNumber(mLateral, n), toLane = LaneNumber(mShift.to, n);
    bool ownSide = OnOwnSide();
    float now = mShift.At(mProgress);
    std::string doing;
    if (mReverse > 0.0f)
        doing = "backing up";
    else if (mLast.state == 1)
        doing = "arriving";
    else if (mDropBack >= 0.0f)
        doing = "dropping back";
    else if (!ownSide)
        doing = mPassMoving ? "overtaking" : "passing on the wrong side";
    else if (mRoundGap >= 0.0f)
        doing = "going round";
    else if (std::fabs(mShift.to - now) > 1.0f)
        doing = mShift.to > now ? "moving left" : "moving right";
    else if (mPassing)
        doing = mPassMoving ? "overtaking" : "going round";
    else if (std::fabs(mLast.speed) < 0.5f && mLast.targetSpeed < 0.5f)
        doing = mBlockKind == 2 && mBlock >= 0.0f ? "waiting for someone on foot"
                : mCautionGap >= 0.0f ? "waiting at junction" : mWideGap >= 0.0f ? "waiting to swing wide" : "waiting";
    else if (mBlock >= 0.0f && mLeadSpeed >= 1.0f && mLast.targetSpeed < mLastPlanSpeed - 1.0f)
        doing = "following";
    else if (mCautionGap >= 0.0f)
        doing = "junction ahead";
    else
        doing = "cruising";
    std::string aheadKind = "-";
    float aheadV = 0.0f;
    if (mBlock >= 0.0f)
    {
        aheadKind = mBlockKind == 1 ? "wall" : mBlockKind == 2 ? "person" : mLeadSpeed >= 1.0f ? "car" : "stopped";
        aheadV = mLeadSpeed;
        for (const Body& b : mBodies)
            if (mBlockKnown >= 0 && b.known == mBlockKnown && b.parked)
                aheadKind = "parked";
    }
    // The next bend it slows for.
    float bend = -1.0f, bendV = 0.0f, v = std::max(mLast.speed, 0.0f);
    for (float a = 5.0f; a <= 300.0f && mProgress + a < mS.back(); a += 5.0f)
        if (float sp = SpeedAt(mProgress + a); sp < std::max(v, mLastPlanSpeed) - 3.0f)
        {
            for (float b = a; b <= a + 60.0f && mProgress + b < mS.back(); b += 2.0f)
                if (SpeedAt(mProgress + b) < sp)
                {
                    sp = SpeedAt(mProgress + b);
                    a = b;
                }
            bend = a;
            bendV = sp;
            break;
        }
    float road = std::min(mSettings.maxSpeed, std::max(mLimit[Index(mProgress)] * mSettings.laneSpeedFactor, 8.0f));
    int people = 0;
    for (const Body& b : mBodies)
        people += b.kind == 2;
    return Format("active=1;style=%d;v=%.0f;target=%.0f;limit=%.0f;why=%s;doing=%s;lane=%d;tolane=%d;lanes=%d;"
                  "ahead=%.0f;aheadv=%.0f;aheadkind=%s;bend=%.0f;bendv=%.0f;left=%.0f;offroad=%.0f;replans=%d;"
                  "moves=%d;dithers=%d;throttle=%.2f;brake=%.2f;steer=%.2f;slip=%.1f;grip=%.2f;oncoming=%d;"
                  "junction=%.0f;people=%d;crosswalk=%.0f;status=%s",
                  mSettings.style, v * 3.6f, mLast.targetSpeed * 3.6f, road * 3.6f, mSpeedWhy, doing.c_str(),
                  lane, toLane, n, mBlock, aheadV * 3.6f, aheadKind.c_str(), bend, bendV * 3.6f,
                  mHasGoal ? mS.back() - mProgress : -1.0f, mOffRoad, mReplans, mBigMoves, mDithers, mLast.throttle,
                  mLast.brake, mLast.steer, mLastSlip * 57.3f, LatAccel() / 9.81f, ownSide ? 0 : 1, mCautionGap,
                  people, mCrosswalkGap, status.c_str());
}

// The whole plan, a line per metre: position, lane, room, speed limit and plan.
std::string Driver::Dump(const std::filesystem::path& aPath) const
{
#ifdef _WIN32
    FILE* f = _wfopen(aPath.c_str(), L"w");
#else
    FILE* f = std::fopen(aPath.c_str(), "w");
#endif
    if (!f)
        return "could not write " + aPath.string();
    fprintf(f, "# s x y z lane laneS limit speed lo hi loSame hiSame junction flow crossing crosswalk swingF swingR "
               "lanes... (m, m/s); car at s=%.1f\n",
            mProgress);
    for (size_t i = 0; i < mPath.size(); ++i)
    {
        const PathPoint& pt = mPath[i];
        fprintf(f, "%zu %.2f %.2f %.2f %u %.1f %.1f %.1f %.2f %.2f %.2f %.2f %d %.2f %d %d %.2f %.2f", i, pt.p.x,
                pt.p.y, pt.p.z, pt.lane, pt.laneS, mLimit[i], mSpeed[i], pt.lo, pt.hi, pt.loSame, pt.hiSame,
                pt.junction ? 1 : 0, pt.flow, pt.crossing ? 1 : 0, pt.crosswalk ? 1 : 0, pt.swingF, pt.swingR);
        for (int j = 0; j < pt.laneN; ++j)
            fprintf(f, " %.1f", pt.lanes[j]);
        fprintf(f, "\n");
    }
    fclose(f);
    return Format("%zu points -> %s", mPath.size(), aPath.string().c_str());
}

int Driver::LaneNumber(float aOffset, int& aCount) const
{
    aCount = 0;
    if (mPath.size() < 2)
        return 0;
    const PathPoint& pt = mPath[Index(mProgress)];
    // (Sorted right to left; at most six.)
    float lanes[6];
    int n = std::clamp(pt.laneN, 0, 6);
    for (int j = 0; j < n; ++j)
    {
        int k = j;
        for (; k > 0 && lanes[k - 1] > pt.lanes[j]; --k)
            lanes[k] = lanes[k - 1];
        lanes[k] = pt.lanes[j];
    }
    aCount = n;
    int best = 0;
    for (int j = 1; j < n; ++j)
        if (std::fabs(lanes[j] - aOffset) < std::fabs(lanes[best] - aOffset))
            best = j;
    return n > 0 && std::fabs(lanes[best] - aOffset) < 2.0f ? best + 1 : 0;
}

std::vector<std::string> Driver::TakeEvents()
{
    std::vector<std::string> out;
    for (const Event& e : mEvents)
        out.push_back(Format("%6.1f s %4.0f km/h %6.0f m  %s", e.time, e.v * 3.6f, e.s, e.what.c_str()));
    mEvents.clear();
    return out;
}

std::string Driver::WhatIs(const Way& aWay) const
{
    if (aWay.block < 0.0f)
        return aWay.contact ? (aWay.contactAlongside ? "a car alongside" : Format("a car %.0f m on", aWay.clear))
                            : "nothing";
    const Body* body = nullptr;
    for (const Body& b : mBodies)
        if (aWay.blockKnown >= 0 ? b.known == aWay.blockKnown : b.known < 0 && b.point == aWay.point)
            body = &b;
    std::string what;
    if (aWay.kind == 1)
        what = "a wall";
    else if (aWay.kind == 2)
        what = body && std::fabs(body->w) > 0.4f ? Format("someone crossing on foot at %.0f km/h", std::fabs(body->w) * 3.6f)
                                                 : "someone on foot";
    else if (body && body->parked)
        what = "a parked car";
    else if (!aWay.blockMeasured)
        what = "a car (not measured yet)";
    else if (std::fabs(aWay.blockU) < 0.5f && std::fabs(aWay.blockW) < 0.5f)
        what = body && body->stillFor > 0.5f ? Format("a car stopped for %.0f s", body->stillFor) : "a stopped car";
    else if (std::fabs(aWay.blockW) > 1.5f)
        what = Format("a car crossing at %.0f km/h", std::fabs(aWay.blockW) * 3.6f);
    else if (aWay.blockU < -1.0f)
        what = Format("an oncoming car at %.0f km/h", -aWay.blockU * 3.6f);
    else
        what = Format("a car at %.0f km/h", aWay.blockU * 3.6f);
    what += Format(" %.0f m ahead", aWay.block);
    if (aWay.blockKnown >= 0)
        what += " (listed by the game)";
    if (aWay.contact && aWay.contactAlongside)
        what += ", and a car alongside";
    return what;
}

// A new sideways move: where from and to, why, and what was in the way of
// the one it had. (Small moves along the lane aren't news.)
void Driver::NoteMove(const Situation& c, const Shift& aWas, const Way& aChosen, bool aHome, float aBest, float aKept)
{
    if (std::fabs(mShift.to - aWas.to) < 1.0f && !c.emergency)
        return;
    const char* why;
    if (aHome)
        why = !c.ownSide ? "back to its own side" : c.homeWhy;
    else if (c.emergency)
        why = "emergency: too close to stop";
    else if (aChosen.endsOncoming)
        why = mPassMoving ? "overtaking" : "going round";
    else if (c.keptWay.off && c.keptWay.offAt < c.offSoon)
        why = "the road runs out";
    else if (c.keptWay.endsOncoming && !c.mayPass)
        why = "out of the oncoming lanes";
    else if (c.keptWay.contact)
        why = c.urgent ? "dodging" : "getting past";
    else if (c.laneEnds)
        why = "its lane ends";
    else
        why = "a clearer way";
    int n;
    int from = LaneNumber(mLateral, n), to = LaneNumber(mShift.to, n);
    std::string lanes = n > 0 ? Format("lane %d to %d of %d", from, to, n) : std::string("no lanes mapped here");
    std::string score = aHome ? std::string() : Format("; scored %.0f against %.0f for keeping on", aBest, aKept);
    Note("move (%s): %s, %+.1f to %+.1f m over %.0f m; in the way before: %s%s", why, lanes.c_str(), aWas.to,
         mShift.to, mShift.len, WhatIs(c.keptWay).c_str(), score.c_str());
}

// Passing begun, given up, waiting for the oncoming lanes to clear, done.
void Driver::NotePassing(const Situation& c, bool aWasPassing, bool aWasMoving)
{
    if (mPassing && !aWasPassing)
    {
        if (mPassMoving)
            Note("pass: overtaking %s, %.0f m to get past", WhatIs(c.homeWay).c_str(), c.passNeed);
        else
        {
            const Body* b = c.homeBody;
            std::string why = !b                  ? std::string("something standing")
                              : b->kind == 1      ? std::string("a wall")
                              : b->kind == 2      ? Format("someone standing in the road for %.0f s", b->stillFor)
                              : b->parked         ? std::string("a parked car")
                                                  : Format("a car stopped %.0f s in its lane, not queueing", b->stillFor);
            Note("pass: going round %s %.0f m ahead", why.c_str(), c.homeWay.block);
        }
    }
    else if (!mPassing && aWasPassing)
        Note("pass: %s", c.headOn ? Format("given up, something coming the other way %.0f m ahead", c.keptNow.clear).c_str()
                         : c.ownSide && c.homeWay.block < 0.0f ? "done"
                         : aWasMoving ? "called off (no longer clear)"
                                      : "called off");
    bool waiting = mPassing && !c.mayPass;
    if (waiting != mNoted.waitingToPass)
    {
        if (waiting)
        {
            float gap, speed;
            Oncoming(400.0f, gap, speed);
            Note("pass: waiting, %s", gap >= 0.0f ? Format("something coming the other way %.0f m ahead at %.0f km/h",
                                                           gap, speed * 3.6f).c_str()
                                                  : "the way round isn't clear");
        }
        else if (mPassing)
            Note("pass: clear, going");
    }
    mNoted.waitingToPass = waiting;
}

// The style and settings it drives with, when they change (sliders: once
// they have stayed put for a second).
void Driver::NoteSettings(const DriverSettings& aSet)
{
    auto differ = [](const DriverSettings& a, const DriverSettings& b)
    {
        return a.maxSpeed != b.maxSpeed || a.latAccel != b.latAccel || a.laneSpeedFactor != b.laneSpeedFactor ||
               a.decel != b.decel || a.followTime != b.followTime;
    };
    if (differ(mNoted.seen, aSet))
    {
        mNoted.seen = aSet;
        mNoted.seenSince = mClock;
    }
    bool style = mSettings.style != mNoted.style;
    if (!style && !(differ(mNoted.settings, aSet) && mClock - mNoted.seenSince >= 1.0f))
        return;
    mNoted.style = mSettings.style;
    mNoted.settings = aSet;
    Note("%s: %s; top speed %.0f km/h, %.1f x the speed limit, corners at %.2f g, brakes for bends at %.1f m/s2, "
         "follows %.1f s behind",
         style ? "style" : "settings", Rules().name, aSet.maxSpeed * 3.6f, aSet.laneSpeedFactor, aSet.latAccel / 9.81f,
         aSet.decel, aSet.followTime);
}

// What holds the speed down, when that is something on the road (not the
// limit or a bend) and has for a moment; losing grip; what it learned of
// the car's grip.
void Driver::NoteSpeed(float aTarget, bool aNoGrip, float aSlip)
{
    auto notable = [](const char* w)
    {
        return w && (!strcmp(w, "following") || !strcmp(w, "stopping behind") || !strcmp(w, "something in the way") ||
                     !strcmp(w, "junction: car across") || !strcmp(w, "going round") || !strcmp(w, "dropping back") ||
                     !strcmp(w, "losing grip") || !strcmp(w, "wide turn: oncoming") || !strcmp(w, "person ahead") ||
                     !strcmp(w, "crosswalk"));
    };
    if (strcmp(mSpeedWhy, mNoted.whyNow))
    {
        mNoted.whyNow = mSpeedWhy;
        mNoted.whySince = mClock;
    }
    if (strcmp(mNoted.whyNow, mNoted.why) && mClock - mNoted.whySince >= 0.7f)
    {
        const char* w = mNoted.whyNow;
        if (notable(w))
        {
            std::string detail;
            if (!strcmp(w, "junction: car across"))
                detail = Format("junction %.0f m ahead", mCautionGap);
            else if (!strcmp(w, "going round"))
                detail = Format("%.0f m to it", mRoundGap);
            else if (!strcmp(w, "wide turn: oncoming"))
                detail = Format("turn %.0f m ahead", mWideGap);
            else if (!strcmp(w, "person ahead"))
                detail = Format("someone on foot %.0f m ahead", mBlock);
            else if (!strcmp(w, "crosswalk"))
                detail = Format("crosswalk %.0f m ahead, someone by it", mCrosswalkGap);
            else if (!strcmp(w, "dropping back"))
                detail = "behind a car alongside";
            else if (!strcmp(w, "losing grip"))
                detail = Format("slip %.0f degrees", aSlip * 57.3f);
            else
                detail = Format("%s %.0f m ahead, %s", mBlockKind == 1 ? "something" : "a car", mBlock,
                                mLeadSpeed >= 1.0f ? Format("going %.0f km/h", mLeadSpeed * 3.6f).c_str() : "standing");
            Note("speed: %s, %.0f km/h (%s)", w, aTarget * 3.6f, detail.c_str());
        }
        else if (notable(mNoted.why))
            Note("speed: free again (%s, %.0f km/h)", w, aTarget * 3.6f);
        mNoted.why = w;
    }
    if (aNoGrip && !mNoted.noGrip)
        Note("losing grip: slip %.0f degrees, turning %.0f of the %.0f degrees/s wanted; slowing until it grips",
             aSlip * 57.3f, std::fabs(mYawRate) * 57.3f, std::fabs(mLastWantRate) * 57.3f);
    mNoted.noGrip = aNoGrip;
    if (mCar.grip > 0.0f && std::fabs(mCar.grip - mNoted.grip) > 0.5f)
    {
        Note("learned this car's grip: %.2f g", mCar.grip / 9.81f);
        mNoted.grip = mCar.grip;
    }
}

// Each frame: leaving the mapped road (once 3 m off it) and coming back;
// braking hard.
void Driver::NoteFrame(const Motion& m, const DriverOutput& aOut, bool aOffRoad)
{
    if (aOffRoad && !mNoted.offRoad)
        mNoted.offFrom = mOffRoad;
    mNoted.offRoad = aOffRoad;
    if (aOffRoad && !mNoted.offNoted && mOffRoad - mNoted.offFrom >= 3.0f)
    {
        const PathPoint& pt = mPath[Index(mProgress)];
        float past = std::max(std::min(pt.lo, 0.0f) - mLateral, mLateral - std::max(pt.hi, 0.0f));
        Note("off the mapped road: %.1f m past its %s edge%s", past, mLateral > 0.0f ? "left" : "right",
             pt.junction ? ", in a junction" : "");
        mNoted.offNoted = true;
    }
    else if (!aOffRoad && mNoted.offNoted)
    {
        Note("back on the road after %.0f m off it", mOffRoad - mNoted.offFrom);
        mNoted.offNoted = false;
    }
    if (mAccelWant < -5.0f && mClock - mNoted.hardBrakeAt > 3.0f)
    {
        Note("braking hard (%.1f m/s2) for: %s; aiming for %.0f km/h", -mAccelWant, mSpeedWhy, aOut.targetSpeed * 3.6f);
        mNoted.hardBrakeAt = mClock;
    }
}
} // namespace BCAI::Autodrive
