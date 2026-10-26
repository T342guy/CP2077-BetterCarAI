// BCAI Autodrive+: where to drive on the road. Each frame Avoid picks the
// sideways move to make: the one keeping the longest stretch ahead clear of
// what the sensor saw and on the road, preferring the lane to keep to and the
// move already under way, and keeping out of the oncoming lanes unless
// passing. It also decides passing (going round something standing, or
// overtaking a slow car), dropping back behind a car alongside, and caution
// at junctions, and sets what is in the way for the speed to follow.

#include "Driver.hpp"

#include <cstdio>
#include <cstdlib>

namespace BCAI::Autodrive
{
void Driver::Avoid(float v, float dt)
{
    Situation c = Survey(v, dt);
    if (c.laneEnds && !mNoted.laneEnds)
        Note("its lane ends in %.0f m", c.laneEndsAt);
    mNoted.laneEnds = c.laneEnds;
    JudgePassing(c);
    c.offSoon = 30.0f + 2.5f * v;
    c.dropTo = DropBackSpeed(c);
    // The move under way, as far as going round or past something needs.
    c.keptWay = Clearance(mShift, c.horizon, c.roadUntil, c.passNeed);
    c.canDrop = c.dropTo >= 0.0f &&
                ((c.keptWay.off && (v * v - c.dropTo * c.dropTo) / (2.0f * Decel()) + 20.0f < c.keptWay.offAt) ||
                 (c.keptWay.endsOncoming && !c.mayPass) || c.laneEnds);
    c.urgent = c.keptWay.contact && c.keptWay.block >= 0.0f && c.keptWay.block < 6.0f + 1.2f * v;
    // About to run into something it can't stop for: any move the car's
    // grip allows.
    c.emergency = c.keptWay.contact && !c.keptWay.contactAlongside &&
                  c.keptWay.clear < 2.0f + v * v / (2.0f * BrakeMax());
    std::vector<std::pair<Shift, Way>> options = Options(c);
    Way bestWay = c.keptWay;
    Way chosen = Choose(c, options, bestWay);
    CountMoves(c);
    Commit(c, chosen);
    WatchJunction(c);
    WatchWideTurn(c);
    WatchCrosswalks(c);
    FindDoor(c, bestWay);
}

// How far it looks, the lane to keep to and the move back to it, and what
// is in the way there.
Situation Driver::Survey(float v, float dt)
{
    Situation c;
    c.v = v;
    c.dt = dt;
    c.rules = &Rules();
    c.horizon = std::clamp(25.0f + 3.5f * v, 35.0f, 100.0f);
    c.len = std::clamp(8.0f + 2.2f * v, 10.0f, 75.0f);
    // Backed up behind where the move under way starts: it starts here
    // (the room backed into is for steering out).
    if (mProgress < mShift.start - 0.5f)
        mShift.start = mProgress;
    c.now = mShift.At(mProgress);
    // The speed it means to go over the next stretch.
    mWant = 1e9f;
    for (float ahead = 0.0f; ahead <= 60.0f; ahead += 10.0f)
        mWant = std::min(mWant, SpeedAt(mProgress + ahead));
    Gather(c.horizon);
    // How far ahead to be in the route's lane: about 9 s of driving (150 m
    // at least), and more for each further lane to cross (in traffic a
    // move can wait a while for a gap).
    {
        float lanesOver = std::floor(std::fabs(mLateral) / 3.5f);
        c.laneNeed = std::max(150.0f, 9.0f * v) * (1.0f + 0.4f * std::max(lanesOver - 1.0f, 0.0f));
    }
    c.laneHome = HomeLane(c.laneNeed, &c.homeWhy);
    // The lane the car is in runs out within that (it has to move over).
    {
        const PathPoint& pt = mPath[Index(mProgress)];
        float best, first;
        float o = NearestLane(pt, mLateral, best);
        c.laneEnds = best < 1.8f && std::fabs(o - c.laneHome) > 1.5f &&
                     !LaneGoesOn(o, c.laneNeed, first, false, &c.laneEndsAt);
    }
    const float now = c.now, len = c.len;
    c.home = Shift{now, c.laneHome, mProgress,
                   std::min(std::max({len, MinShiftLength(c.laneHome - now),
                                      std::sqrt(5.8f * std::fabs(c.laneHome - now) * v * v / (0.35f * MoveLat()))}),
                            std::max({len, 150.0f, 5.0f * v}))};
    c.homeWay = Clearance(c.home, c.horizon);
    for (const Body& b : mBodies)
        if (c.homeWay.block >= 0.0f &&
            (c.homeWay.blockKnown >= 0 ? b.known == c.homeWay.blockKnown : b.known < 0 && b.point == c.homeWay.point))
            c.homeBody = &b;
    return c;
}

// The oncoming lanes only to get past something standing still on the line
// (a wall, or a car seen in the same spot for a while), to overtake a slow
// car where that is safe, and to finish such a pass.
void Driver::JudgePassing(Situation& c)
{
    const float v = c.v;
    const Body* homeBody = c.homeBody;
    // Past something moving: the lane moved to has to go on until the car
    // is well past it.
    if (homeBody && homeBody->kind == 0 && !homeBody->parked && homeBody->u > 0.5f)
        c.passNeed = PassLength(*homeBody, v);
    c.standing = Standing(c);
    // Going round something: the road has to be wide enough until a bit
    // past it (then the car goes back to its line). (Only round something
    // standing: past something moving the car stays in the lane it moved
    // to, which has to go on.)
    if (c.standing)
    {
        float u = homeBody->u, want = std::max(mWant, v);
        float scale = u > 0.5f && want - u > 0.5f ? std::min(want / (want - u), 6.0f) : 1.0f;
        c.roadUntil = std::max(c.homeWay.clear, homeBody->s0 - mProgress - mFront) +
                      (RowEnd(c, *homeBody) - homeBody->s0 + kCarLength + 8.0f) * scale;
    }
    // (A pass under way goes on until the car is back on its own side,
    // unless something coming the other way would get there first.)
    c.ownSide = OnOwnSide();
    {
        // (Out of its own side but on the road: in the oncoming lanes, not
        // just off the edge.)
        const PathPoint& here = mPath[Index(mProgress)];
        c.inOncoming = !c.ownSide && mLateral >= std::min(here.lo, 0.0f) && mLateral <= std::max(here.hi, 0.0f);
    }
    c.keptNow = Clearance(mShift, c.horizon);
    c.headOn = !c.ownSide && c.keptNow.contact && c.keptNow.contactU < -1.0f &&
               c.keptNow.clear < 3.0f * std::max(v, 5.0f);
    if (c.headOn)
        mNoPassUntil = mClock + (mPassMoving ? 8.0f : 4.0f); // given up: not again straight away
    const bool wasPassing = mPassing, wasMoving = mPassMoving;
    bool slowPass = homeBody && !c.standing && homeBody->kind == 0 && homeBody->measured && c.passNeed > 0.0f &&
                    SlowPassClear(c);
    if ((c.homeWay.block < 0.0f && !c.homeWay.contact && c.ownSide) || c.headOn)
        mPassing = mPassMoving = false;
    else if ((c.standing || slowPass) && mClock > mNoPassUntil)
    {
        mPassMoving = mPassMoving || (!c.standing && !mPassing);
        mPassing = true;
    }
    else if (mPassing && mPassMoving && c.ownSide && !slowPass)
        mPassing = mPassMoving = false; // (not begun yet: called off)
    c.mayPass = mPassing && OncomingClear(c);
    NotePassing(c, wasPassing, wasMoving);
    // In and just before a junction where roads cross: no swinging out
    // round moving traffic. (The lane map marks long stretches of plain
    // road, highways too, as junction lanes; only crossings count.)
    for (float ahead = 0.0f; ahead <= 25.0f && !c.junction; ahead += 2.0f)
        c.junction = mPath[Index(mProgress + ahead)].crossing;
}

// Where a row of things standing in the way ends (along the path), from
// aFirst on: the next one counts while the gap to it is too short for the
// car to tuck back in between (its length and a move out and back), so a
// row of parked cars is gone round as one. (Past what the sensor gathers,
// the parked cars the game lists, up to 200 m ahead.)
float Driver::RowEnd(const Situation& c, const Body& aFirst) const
{
    const float r = mHalfWidth + 0.6f, line = c.laneHome;
    const float tuck = mFront + mRear + 2.0f * MinShiftLength(2.0f * kCarWidth);
    float end = aFirst.s1;
    for (bool grew = true; grew;)
    {
        grew = false;
        auto take = [&](float s0, float s1, float l0, float l1)
        {
            if (s1 > end && s0 < end + tuck && l1 > line - r && l0 < line + r)
            {
                end = s1;
                grew = true;
            }
        };
        for (const Body& b : mBodies)
            if (b.kind == 1 || b.parked ||
                (b.kind == 0 && b.measured && std::fabs(b.u) < 0.5f && b.stillFor > c.rules->standingAfter))
                take(b.s0, b.s1, b.l0, b.l1);
        for (const Known& kn : mKnown)
        {
            float s, l;
            if (!kn.parked || !Project(KnownAt(kn), end - 10.0f, end + tuck + 10.0f, s, l))
                continue;
            float along = std::max(kn.front, kn.rear), side = kn.half;
            take(s - along, s + along, l - side, l + side);
        }
    }
    return end;
}

// How far the car goes while getting past a moving car: speeding up to the
// speed it means to go as fast as this car can (a slow one takes longer),
// until it is 8 m past. 0 if it can't get past.
float Driver::PassLength(const Body& aBody, float v) const
{
    float want = std::max(mWant, v), u = aBody.u;
    if (want - u <= 0.5f)
        return 0.0f;
    float behind = aBody.s1 - mProgress + mRear + 8.0f, me = v, gained = 0.0f, went = 0.0f;
    for (float t = 0.0f; t < 40.0f && gained < behind && went < 400.0f; t += 0.1f)
    {
        me = std::min(me + AccelAt(me) * 0.1f, want);
        gained += (me - u) * 0.1f;
        went += me * 0.1f;
    }
    return std::min(went, 400.0f);
}

// Standing still: a wall, a parked car, or a car stopped in the lane for
// longer than waiting its turn takes (StyleRules::standingAfter), unless it
// stands in a queue: another car stopped just in front of it, or a junction
// where roads cross just past it (waiting for the lights; a madman goes
// round anyway).
bool Driver::Standing(const Situation& c) const
{
    const Body* homeBody = c.homeBody;
    const StyleRules& rules = *c.rules;
    if (!homeBody)
        return false;
    if (homeBody->kind == 1 || homeBody->parked)
        return true;
    // (Someone standing in the road, not crossing it: after as long.)
    if (homeBody->kind == 2)
        return homeBody->stillFor > rules.standingAfter && std::fabs(homeBody->w) < 0.3f;
    if (homeBody->kind != 0 || !homeBody->measured || std::fabs(homeBody->u) >= 0.5f ||
        homeBody->stillFor <= rules.standingAfter)
        return false;
    bool queue = false;
    for (float at = homeBody->s1; at <= homeBody->s1 + 30.0f && !queue && rules.queues; at += 2.0f)
        queue = mPath[Index(at)].crossing;
    for (const Body& b : mBodies)
        if (&b != homeBody && b.kind == 0 && b.s0 > homeBody->s1 - 1.0f && b.s0 < homeBody->s1 + 12.0f &&
            std::fabs(b.u) < 0.5f && b.l1 > homeBody->l0 - 1.0f && b.l0 < homeBody->l1 + 1.0f && rules.queues)
            queue = true;
    return !queue;
}

// Past a slow car on the other side of the road: when it is slow enough
// for the style, nothing is seen coming for as far as the pass takes, and
// (careful styles) no roads cross on the way, the road is straight enough
// to see along it and something coming at the speed limit from beyond what
// the game lists (about 180 m ahead) would still be far enough off. (Once
// under way, it takes a little more to call it off.)
bool Driver::SlowPassClear(const Situation& c) const
{
    const Body* homeBody = c.homeBody;
    const StyleRules& rules = *c.rules;
    const float v = c.v;
    float want = std::max(mWant, v), u = std::max(homeBody->u, 0.0f);
    bool slow = rules.slowShare > 0.0f ? u < rules.slowShare * want : u < want - rules.slowBy;
    float need = c.passNeed + std::sqrt(5.8f * 3.5f * want * want / (0.5f * MoveLat())) + 20.0f;
    float time = need / std::max(0.5f * (v + want), 3.0f) + 2.0f;
    float slack = mPassMoving ? 0.7f : 1.0f;
    bool clear = slow && need < 400.0f;
    for (float at = 0.0f; at <= need && clear; at += 2.0f)
    {
        const PathPoint& pt = mPath[Index(mProgress + at)];
        clear = !(pt.crossing && rules.carefulPass) &&
                !(rules.carefulPass && !mPassMoving && SpeedAt(mProgress + at) < 0.85f * std::min(want, mWant + 0.1f));
    }
    // What is seen coming: there and back before it gets there.
    float gap, speed;
    Oncoming(need + 60.0f * time, gap, speed);
    if (clear && gap >= 0.0f)
        clear = gap * slack > need + speed * time + 20.0f;
    if (clear && rules.carefulPass)
        clear = (need + 0.8f * mLimit[Index(mProgress + 0.5f * need)] * time) * slack < 180.0f;
    return clear;
}

// Not swinging out (or, already out, going on past the next car) while
// something coming the other way would get there before the car is past
// and back: as far as the way round goes and the way back takes, at the
// time that takes from the speed it has now. (Out beside something
// standing, with no way back, the scoring keeps it going.)
bool Driver::OncomingClear(const Situation& c) const
{
    const float v = c.v;
    float want = std::max(mWant, v), a = std::max(AccelAt(v), 1.0f);
    float need = std::min(c.roadUntil < 1e8f ? c.roadUntil : c.passNeed, 300.0f) +
                 std::sqrt(5.8f * 3.5f * want * want / (0.5f * MoveLat())) + 10.0f;
    float time = (std::sqrt(v * v + 2.0f * a * need) - v) / a + 1.0f;
    float gap, speed;
    Oncoming(need + 25.0f * time, gap, speed);
    return gap < 0.0f || gap > need + speed * time + 15.0f;
}

// Its side of the road runs out ahead (or turns into the oncoming lanes, or
// its lane ends) and the way back is blocked by a car going its way, beside
// it or just ahead: dropping back behind that car, then moving back, is the
// way out while there is room to slow down first. The speed to drop back
// to: slower than that car by enough to be behind it before the road or
// lane runs out (at least 2 m/s, at most 8). < 0: no such car.
float Driver::DropBackSpeed(const Situation& c) const
{
    const Way& homeWay = c.homeWay;
    const float v = c.v;
    if (!(homeWay.contact && homeWay.contactU > 0.5f &&
          (homeWay.contactAlongside || (homeWay.block >= 0.0f && homeWay.block < 3.0f + 0.5f * v))))
        return -1.0f;
    float endAt = c.laneEnds ? c.laneEndsAt : -1.0f;
    const Way& now = c.keptNow;
    if (now.off && (endAt < 0.0f || now.offAt < endAt))
        endAt = now.offAt;
    float behind = std::max(mProgress + mFront - homeWay.contactS0 + 3.0f, 2.0f);
    float by = endAt > 0.0f ? std::clamp(behind / std::max(endAt / std::max(v, 1.0f) - 1.0f, 1.0f) + 1.0f, 2.0f, 8.0f)
                            : 2.0f;
    return std::max(homeWay.contactU - by, 0.0f);
}

// How good a move is: the longer clear the better; leaving the road, running
// into something it can't stop for, squeezing a car alongside, the oncoming
// lanes (but to pass) and moves away from the lane to keep to count against.
// aKept: the move under way.
float Driver::Score(const Situation& c, const Shift& aShift, const Way& aWay, bool aKept) const
{
    const float v = c.v;
    // (Off the road: soon, worst of all; further on, where the road runs
    // out after the move, or where the car can drop back and move back
    // first, a lesser evil that can still be put right.)
    bool later = aWay.offAt >= c.offSoon || (c.canDrop && aKept);
    float s = 2.0f * aWay.clear + (aWay.contact ? 0.0f : 30.0f) - (aWay.off ? (later ? 150.0f : 500.0f) : 0.0f) -
              1.2f * std::fabs(aShift.to - c.laneHome);
    // Ending in a lane, not between two (unless passing in the oncoming
    // lanes).
    if (!aWay.endsOncoming)
        s -= c.rules->laneGapWeight * std::min(LaneGap(mProgress + 20.0f, aShift.to), 2.0f);
    // Running into something too close to stop for (braking at about
    // 8 m/s^2), or steering into a car alongside (braking doesn't help), is
    // worse than anything but leaving the road.
    if (aWay.contact && (aWay.contactAlongside || aWay.clear < 2.0f + v * v / (2.0f * BrakeMax())))
        s -= 400.0f;
    // (Beside a car: the less closer it comes, the better.)
    if (aWay.contact && aWay.contactAlongside)
        s -= 30.0f * std::min(aWay.squeeze, 4.0f);
    // Into the oncoming lanes only to pass, or to escape running into
    // something (on the way back from there, it's fine).
    s -= 0.3f * aWay.oncoming + (!c.mayPass && aWay.endsOncoming ? 350.0f : 0.0f);
    // (Nor stopped half way back from them, behind something standing in
    // the way, before the move is done.)
    if (!c.mayPass && !aWay.endsOncoming && aWay.contact && aWay.block >= 0.0f && std::fabs(aWay.blockU) < 0.5f)
    {
        float stopAt = mProgress + std::max(aWay.block - 7.0f, 0.0f);
        if (stopAt < aShift.start + aShift.len)
        {
            float d = aShift.At(stopAt);
            const PathPoint& pt = mPath[Index(stopAt)];
            if (d < std::min(pt.loSame, 0.0f) - 0.3f || d > std::max(pt.hiSame, 0.0f) + 0.3f)
                s -= 350.0f;
        }
    }
    if (c.junction && !c.standing)
        s -= 40.0f * std::max(std::fabs(aShift.to - c.laneHome) - 1.0f, 0.0f);
    return s;
}

// Every half metre either way, as a gentle move, and when something is
// close also a quicker one (half as long); none needing more grip than a
// comfortable lane change (a firmer one when urgent).
std::vector<std::pair<Shift, Way>> Driver::Options(const Situation& c) const
{
    const float v = c.v, now = c.now, len = c.len;
    std::vector<std::pair<Shift, Way>> options;
    for (int k = -13; k <= 13; ++k)
        for (float part : {1.0f, 0.5f, 0.3f})
        {
            // (Quicker ones too while out in the oncoming lanes without
            // leave to pass, to get back in before stopping.)
            bool hurry = c.urgent || c.emergency || (c.inOncoming && !c.mayPass);
            if ((part < 1.0f && !hurry) || (part < 0.5f && !c.emergency))
                continue;
            Shift shift{now, 0.5f * float(k), mProgress,
                        std::max({len * part, part < 1.0f ? 8.0f : 12.0f, MinShiftLength(0.5f * float(k) - now)})};
            // A gentle move as long as a comfortable lane change needs (a
            // whole lane over at speed takes a while).
            if (part == 1.0f)
                shift.len = std::min(
                    std::max(shift.len, std::sqrt(5.8f * std::fabs(shift.to - now) * v * v / (0.35f * MoveLat()))),
                    std::max(150.0f, 5.0f * v));
            // (A gentle move was made just long enough for this: a little
            // slack, or rounding drops it some frames and not others.)
            float lateralAccel = 5.8f * std::fabs(shift.to - now) * v * v / (shift.len * shift.len);
            if (lateralAccel > 1.001f * (c.emergency ? 1.1f : hurry ? 0.8f : 0.35f) * MoveLat())
                continue;
            options.push_back({shift, Clearance(shift, c.horizon, c.roadUntil, c.passNeed)});
        }
    return options;
}

// Picks between the move under way, the move back to the lane to keep to
// and the best of the options, and makes it the move under way. Returns how
// the chosen move judges; aBestWay: the best option's.
Way Driver::Choose(Situation& c, const std::vector<std::pair<Shift, Way>>& aOptions, Way& aBestWay)
{
    const float v = c.v, now = c.now, len = c.len;
    const Way& keptWay = c.keptWay;
    const StyleRules& rules = *c.rules;
    const Shift was = mShift;
    // Turning back on a move under way costs extra, so it doesn't dither.
    float moving = mShift.to - now;
    float kept = Score(c, mShift, keptWay, true);
    float best = -1e9f, bestTo = mShift.to, bestLen = len;
    for (const auto& [shift, way] : aOptions)
    {
        float s = Score(c, shift, way, false) - 0.3f * std::fabs(shift.to - mShift.to);
        if (shift.len < std::max({len, 12.0f, MinShiftLength(shift.to - now)}) - 0.01f)
            s -= 3.0f; // the quicker kind
        if (std::fabs(moving) > 0.3f && (shift.to - now) * moving < 0.0f)
            s -= 5.0f;
        if (s > best)
        {
            best = s;
            bestTo = shift.to;
            bestLen = shift.len;
            aBestWay = way;
        }
    }
    // Back onto the line as soon as it is clear all the way.
    // A move once chosen is kept for a second (the sensor sees a car a few
    // points at a time, and changing its mind every frame would get the car
    // nowhere), unless what it heads for is close.
    mShiftAge += c.dt;
    bool settled = mShiftAge > 1.0f;
    // Not changing its mind: after a lane change, the next one (other than
    // to get out of trouble) waits a while; back to its lane only once that
    // has been clear for a moment; and a new move has to be clearly better.
    c.trouble = c.urgent || c.emergency || (keptWay.off && keptWay.offAt < c.offSoon) ||
                (keptWay.endsOncoming && !c.mayPass);
    bool held = !c.trouble && mClock < mHoldUntil;
    // (Out in the oncoming lanes, the way back runs through them.)
    bool homeClear = !c.homeWay.contact && (c.homeWay.oncoming <= 0.0f || !c.ownSide) && !c.homeWay.off;
    float homeHold = !c.ownSide ? 0.2f : rules.clearFor;
    {
        auto soon = [&](const Way& w)
        { return (w.contact && w.clear < 2.0f * v + 15.0f) || (w.off && w.offAt < c.offSoon); };
        bool looked[53] = {};
        for (const auto& [shift, way] : aOptions)
        {
            int k = ClearBin(shift.to);
            looked[k] = true;
            if (soon(way))
                mClearSince[k] = mClock;
        }
        if (soon(c.homeWay) || !homeClear)
            mClearSince[ClearBin(c.laneHome)] = mClock;
        else
            looked[ClearBin(c.laneHome)] = true;
        for (int k = 0; k < 53; ++k)
            if (!looked[k])
                mClearSince[k] = mClock;
    }
    auto lookedClear = [&](float to) { return c.trouble || mClock - mClearSince[ClearBin(to)] >= homeHold; };
    float margin = c.trouble ? 3.0f : rules.switchMargin;
    // Going the other way round, once committed: only when what is ahead
    // can't be stopped for any more.
    bool flips = std::fabs(moving) > 0.3f && (bestTo - now) * moving < 0.0f &&
                 !(keptWay.block >= 0.0f && keptWay.block < 3.0f + v * v / 12.0f);
    Way chosen = keptWay;
    const Way& bestWay = aBestWay;
    // (Not half way through a move: it is finished first.)
    if (settled && std::fabs(mShift.to - c.laneHome) > 0.4f && std::fabs(mLateral - mShift.to) < 0.5f && homeClear &&
        lookedClear(c.laneHome) && !(held && c.ownSide))
    {
        mShift = c.home;
        mShiftAge = 0.0f;
        chosen = c.homeWay;
        NoteMove(c, was, chosen, true, best, kept);
    }
    else if ((settled || ((c.urgent || c.emergency) && mShiftAge > 0.3f && !flips) || (c.emergency && mShiftAge > 0.1f)) &&
             best > kept + margin && !(held && std::fabs(bestTo - mShift.to) >= 1.0f) &&
             (std::fabs(bestTo - now) < 1.5f || lookedClear(bestTo)) &&
             // (The same move again, from where the car is now, when that
             // one gets past and the one under way doesn't.)
             (std::fabs(bestTo - mShift.to) > 0.01f || bestLen < mShift.len || (keptWay.contact && !bestWay.contact)) &&
             // (Not for a way blocked sooner, unless what blocks this one
             // is coming the other way, or this one leaves the road or ends
             // in the oncoming lanes without leave to pass: then back in
             // behind what blocks it is better.)
             (!(bestWay.block >= 0.0f && keptWay.block >= 0.0f && bestWay.block < keptWay.block + 5.0f &&
                bestWay.contact && keptWay.blockU > -0.5f) ||
              (keptWay.off && !bestWay.off) || (keptWay.endsOncoming && !c.mayPass && !bestWay.endsOncoming)))
    {
        mShift = Shift{now, bestTo, mProgress, bestLen};
        mShiftAge = 0.0f;
        chosen = bestWay;
        NoteMove(c, was, chosen, false, best, kept);
    }
#ifdef BCAI_DEBUG_AVOID
    {
        static float dbgFrom = getenv("BCAI_DBG") ? float(atof(getenv("BCAI_DBG"))) : -1.0f;
        static float dbgLast = -1.0f;
        if (dbgFrom >= 0.0f && mClock >= dbgFrom && mClock < dbgFrom + 6.0f && mClock - dbgLast >= 0.1f)
        {
            dbgLast = mClock;
            auto w = [](const Way& a)
            {
                static char buf[4][200];
                static int k = 0;
                k = (k + 1) & 3;
                snprintf(buf[k], 200, "clear %.0f block %.1f contact %d along %d cu %.1f off %d at %.0f onc %.0f endsOnc %d",
                         a.clear, a.block, a.contact, a.contactAlongside, a.contactU, a.off, a.offAt, a.oncoming,
                         a.endsOncoming);
                return buf[k];
            };
            printf("      dbg %.1f now %+.2f lat %+.2f shift->%+.1f home %+.1f kept %.0f best %.0f->%+.1f | kept: %s | "
                   "home(%.0f): %s | best: %s | roadUntil %.0f passNeed %.0f mayPass %d | settled %d held %d "
                   "lookedClear %d (%.1f s) trouble %d\n",
                   mClock, now, mLateral, mShift.to, c.laneHome, kept, best, bestTo, w(keptWay),
                   Score(c, c.home, c.homeWay, false), w(c.homeWay), w(bestWay), c.roadUntil, c.passNeed, c.mayPass,
                   settled, held, lookedClear(bestTo), mClock - mClearSince[ClearBin(bestTo)], c.trouble);
            if (getenv("BCAI_DBG_OPT"))
                for (const auto& [shift, way] : aOptions)
                    if (std::fabs(shift.to - std::round(shift.to)) < 0.01f || getenv("BCAI_DBG_ALL"))
                        printf("        opt %+.1f len %.0f score %.0f: %s\n", shift.to, shift.len,
                               Score(c, shift, way, false), w(way));
        }
    }
#endif
    return chosen;
}

// Counts the moves: of 2 m or more; a lane change (or a move as big)
// starts the wait for the next; changing its mind (a move of 1 m or more
// undone within 3 s).
void Driver::CountMoves(const Situation& c)
{
    if (std::fabs(mShift.to - mLastTo) >= 2.0f)
    {
        ++mBigMoves;
        mLastTo = mShift.to;
    }
    if (std::fabs(mShift.to - mHeldTo) >= 1.5f)
    {
        mHoldUntil = mClock + mShift.len / std::max(c.v, 3.0f) + c.rules->holdAfterMove;
        mHeldTo = mShift.to;
    }
    if (std::fabs(mShift.to - mMindTo) >= 1.0f)
    {
        float by = mShift.to - mMindTo;
        if (by * mMindBy < 0.0f && mClock - mMindAt < 3.0f)
        {
            ++mDithers;
            Note("changed its mind: undid a %+.1f m move after %.1f s", mMindBy, mClock - mMindAt);
        }
        mMindBy = by;
        mMindAt = mClock;
        mMindTo = mShift.to;
    }
}

// What the speed follows from the chosen move: what is in the way, going
// round, dropping back behind a car alongside (its side of the road runs
// out further on, and the way back is blocked by that car).
void Driver::Commit(const Situation& c, const Way& aChosen)
{
    const Way& chosen = aChosen;
    mDropBack = c.canDrop && std::fabs(mShift.to - c.laneHome) > 0.4f && (chosen.off || chosen.endsOncoming || c.laneEnds)
                    ? c.dropTo
                : chosen.off && chosen.offAt >= c.offSoon && c.homeWay.contact && c.homeWay.contactAlongside
                    ? std::max(c.homeWay.contactU - 2.0f, 0.0f)
                    : -1.0f;
    mReturning = !c.ownSide && !chosen.endsOncoming;
    mBlock = chosen.block;
    mBlockKind = chosen.kind;
    mBlockPoint = chosen.point;
    mBlockTrack = chosen.track;
    mBlockU = chosen.blockU;
    mBlockW = chosen.blockW;
    mBlockMeasured = chosen.blockMeasured;
    mBlockKnown = chosen.blockKnown;
    mRoundGap = c.standing && c.homeWay.block >= 0.0f ? c.homeWay.block : -1.0f;
    mRoundPerson = c.homeBody && c.homeBody->kind == 2;
}

// A junction ahead with a car on the roads across it that may go (moving,
// or not watched long enough to know it waits): keep able to stop before
// the junction (sets mCautionGap).
void Driver::WatchJunction(const Situation& c)
{
    const StyleRules& rules = *c.rules;
    mCautionGap = -1.0f;
    for (float ahead = 0.0f; ahead <= 80.0f && mProgress + mFront + ahead <= mS.back(); ahead += 2.0f)
    {
        float at = mProgress + mFront + ahead;
        if (!mPath[Index(at)].junction)
            continue;
        // (Not once in it: carry on across. Nor where roads only merge or
        // fork.)
        if (ahead < 2.0f)
            break;
        float end = at;
        bool crossing = false;
        while (end < mS.back() && end < at + 60.0f && mPath[Index(end)].junction)
        {
            crossing = crossing || mPath[Index(end)].crossing;
            end += 2.0f;
        }
        if (!crossing)
            break;
        for (const Body& b : mBodies)
        {
            if (b.kind != 0 || b.parked || b.s1 < at - 5.0f || b.s0 > end + 5.0f || std::fabs(b.u) >= 3.0f)
                continue;
            float off = 0.5f * (b.l0 + b.l1) - mShift.At(b.s0);
            // Coming across towards the line; or not watched long enough to
            // know, out on the roads across (not in the lanes beside), while
            // there is still room to stop gently. (A madman only for one
            // seen coming across. One the game lists standing there pointing
            // across the road, waiting to pull out, may go too.)
            bool known = b.measured || b.w != 0.0f;
            bool across = known ? std::fabs(off) > kCarWidth + 1.0f && std::fabs(b.w) > 1.0f && b.w * off < 0.0f
                                : std::fabs(off) > 7.5f && rules.cautious;
            if (rules.cautious && b.known >= 0 && b.crosswise && std::fabs(b.u) < 0.5f && std::fabs(b.w) < 0.5f &&
                std::fabs(off) > kCarWidth + 1.0f)
                across = true;
            if (across)
                mCautionGap = ahead;
        }
        break;
    }
}

// A long vehicle about to turn so tightly that its ends swing over the
// oncoming lanes (Swing): it waits before the turn while something coming
// the other way would get there while it is in it (mWideGap).
void Driver::WatchWideTurn(const Situation& c)
{
    float was = mWideGap;
    mWideGap = WideTurnWait(c);
    if (mWideGap >= 0.0f && was < 0.0f)
        Note("waiting to swing wide: a turn %.0f m ahead swings its ends into the oncoming lanes, traffic coming",
             mWideGap);
    else if (mWideGap < 0.0f && was >= 0.0f)
        Note("swinging wide: the way is clear");
}

// How far before the turn to wait (< 0: no need).
float Driver::WideTurnWait(const Situation& c) const
{
    if (!Long())
        return -1.0f;
    // Where the centre is when an end is over (c0..c1), and where along
    // the line and how far sideways the ends sweep.
    float c0 = -1.0f, c1 = -1.0f, z0 = 1e9f, z1 = -1e9f, zl0 = 1e9f, zl1 = -1e9f;
    for (float ahead = 0.0f; ahead <= 80.0f && mProgress + ahead + mFront <= mS.back(); ahead += 1.0f)
    {
        float s = mProgress + ahead;
        bool over = false;
        for (float a : {mFront, -mRear})
        {
            float at = s + a, e = mShift.At(at) + SwingAt(s, at);
            const PathPoint& pt = mPath[Index(at)];
            // Near or past the edge of its own side, where there are
            // oncoming lanes (whether it reaches a car there is judged by
            // where that car really is).
            bool left = e > std::max(pt.hiSame, 0.0f) - 0.3f && pt.hi > pt.hiSame + 0.5f;
            bool right = e < std::min(pt.loSame, 0.0f) + 0.3f && pt.lo < pt.loSame - 0.5f;
            if (!left && !right)
                continue;
            over = true;
            z0 = std::min(z0, at - 2.0f);
            z1 = std::max(z1, at + 2.0f);
            zl0 = std::min(zl0, left ? pt.hiSame : e - mHalfWidth);
            zl1 = std::max(zl1, left ? e + mHalfWidth : pt.loSame);
        }
        if (over && c0 < 0.0f)
            c0 = s;
        if (over)
            c1 = s;
        else if (c0 >= 0.0f && s > c1 + 10.0f)
            break;
    }
    // (Already in it: carry on through.)
    if (c0 < 0.0f || c0 < mProgress + 1.0f)
        return -1.0f;
    // When it would be in it: the speed through it as planned (from a
    // standstill when it waits right before it), and a margin.
    float v = std::max(c.v, 0.0f), through = 1e9f;
    for (float s = c0; s <= c1; s += 2.0f)
        through = std::min(through, SpeedAt(s));
    through = std::max(through, 2.0f);
    float t0 = (c0 - mProgress) / std::max({v, through, 3.0f});
    float t1 = t0 + (c1 - c0 + mFront + mRear) / through + (v < 1.0f ? through / 1.5f : 0.0f);
    float margin = c.rules->cautious ? 2.5f : 1.2f;
    // Each vehicle coming the other way (listed, or seen by the rays): when
    // it is in the stretch the ends sweep.
    auto clash = [&](float s0, float s1, float l0, float l1, float u)
    {
        if (u > -1.0f || l1 < zl0 - 0.5f || l0 > zl1 + 0.5f)
            return false;
        float tIn = std::max(0.0f, (s0 - z1) / -u), tOut = (s1 - z0) / -u;
        return tOut > 0.0f && tIn < t1 + margin && tOut > t0 - margin;
    };
    bool wait = false;
    for (const Known& kn : mKnown)
    {
        float s, l;
        Point3 at = KnownAt(kn);
        if (kn.person || !Project(at, mProgress - 20.0f, mProgress + 200.0f, s, l) || std::fabs(at.z - At(s).z) > 3.5f)
            continue;
        Point3 n = Normal(s);
        float u = kn.vel.x * n.y - kn.vel.y * n.x, reach = std::max(kn.front, kn.rear);
        wait = wait || clash(s - reach, s + reach, l - kn.half, l + kn.half, u);
    }
    for (const Body& b : mBodies)
        if (b.kind == 0 && b.known < 0 && b.measured)
            wait = wait || clash(b.s0, b.s1, b.l0, b.l1, b.u);
    return wait ? c0 - mProgress : -1.0f;
}

// A crosswalk ahead with someone on foot by it (City): they may step out,
// so it slows to a speed it can stop from (mCrosswalkGap). Only slowing,
// never waiting: people stand at crosswalks for the lights, or just stand.
void Driver::WatchCrosswalks(const Situation& c)
{
    mCrosswalkGap = -1.0f;
    if (c.rules->crosswalkCare)
        for (float ahead = 0.0f; ahead <= 40.0f && mProgress + mFront + ahead <= mS.back() && mCrosswalkGap < 0.0f;
             ahead += 1.0f)
        {
            float at = mProgress + mFront + ahead;
            const PathPoint& pt = mPath[Index(at)];
            if (!pt.crosswalk)
                continue;
            Point3 n = Normal(at), here = At(at);
            float hiEdge = std::max(pt.hi, 0.0f) + mHalfWidth + 4.0f, loEdge = std::min(pt.lo, 0.0f) - mHalfWidth - 4.0f;
            for (const Known& kn : mKnown)
            {
                Point3 p = KnownAt(kn);
                float dx = p.x - here.x, dy = p.y - here.y;
                float along = dx * n.y - dy * n.x, side = dx * n.x + dy * n.y;
                if (kn.person && std::fabs(along) < 4.0f && side < hiEdge && side > loEdge && std::fabs(p.z - here.z) < 3.0f)
                    mCrosswalkGap = ahead;
            }
            ahead += 6.0f; // (past this crosswalk)
        }
    float at = mProgress + mFront + mCrosswalkGap;
    if (mCrosswalkGap >= 0.0f && std::fabs(at - mNoted.crosswalkAt) > 10.0f)
    {
        Note("crosswalk %.0f m ahead with someone by it: slowing", mCrosswalkGap);
        mNoted.crosswalkAt = at;
    }
}

// No way past what is ahead (mNoWay), but a gap beside it wide enough for
// the car (on the road; across the oncoming lanes only when it may pass):
// backing up gives the room to steer into it (mDoor).
void Driver::FindDoor(const Situation& c, const Way& aBestWay)
{
    const Body* homeBody = c.homeBody;
    mNoWay = c.keptWay.contact && aBestWay.contact && aBestWay.clear < c.keptWay.clear + 10.0f;
    mDoor = false;
    if (homeBody && mNoWay)
        for (float d = -6.5f; d <= 6.5f && !mDoor; d += 0.5f)
        {
            bool fits = true;
            for (float at = homeBody->s0 - 2.0f; at <= homeBody->s1 + 2.0f && fits; at += 1.0f)
            {
                const PathPoint& pt = mPath[Index(at)];
                float lo = c.mayPass ? pt.lo : std::min(pt.loSame, 0.0f),
                      hi = c.mayPass ? pt.hi : std::max(pt.hiSame, 0.0f);
                fits = d >= lo && d <= hi;
            }
            for (const Body& b : mBodies)
                if (fits && b.s1 > homeBody->s0 - 3.0f && b.s0 < homeBody->s1 + 3.0f)
                    fits = d < b.l0 - mHalfWidth - 0.4f || d > b.l1 + mHalfWidth + 0.4f;
            mDoor = fits;
        }
}

// The nearest vehicle on the road ahead within aRange metres coming the
// other way (listed by the game, or followed by the rays): its gap from
// the bumper and its speed towards the car (aGap < 0: none).
void Driver::Oncoming(float aRange, float& aGap, float& aSpeed) const
{
    aGap = -1.0f;
    aSpeed = 0.0f;
    auto take = [&](float gap, float speed)
    {
        if (gap > -2.0f && gap < aRange && (aGap < 0.0f || gap < aGap))
        {
            aGap = std::max(gap, 0.0f);
            aSpeed = speed;
        }
    };
    for (const Known& kn : mKnown)
    {
        float s, l;
        Point3 at = KnownAt(kn);
        if (kn.person || !Project(at, mProgress, mProgress + aRange + 10.0f, s, l) || std::fabs(at.z - At(s).z) > 3.5f)
            continue;
        const PathPoint& pt = mPath[Index(s)];
        Point3 n = Normal(s);
        float u = kn.vel.x * n.y - kn.vel.y * n.x;
        if (u < -1.0f && l < pt.hi + mHalfWidth + 1.0f && l > pt.lo - mHalfWidth - 1.0f)
            take(s - std::max(kn.front, kn.rear) - mProgress - mFront, -u);
    }
    for (const Body& b : mBodies)
        if (b.kind == 0 && b.known < 0 && b.measured && b.u < -1.0f)
            take(b.s0 - mProgress - mFront, -b.u);
}

// How far aOffset is from the middle of the nearest lane going the same
// way (the route's own lane always counts), the least of three points
// from aS on (lanes alongside come and go where lane pieces join).
float Driver::LaneGap(float aS, float aOffset) const
{
    float gap = std::fabs(aOffset);
    for (float d : {0.0f, 20.0f, 40.0f})
    {
        const PathPoint& pt = mPath[Index(std::min(aS + d, mS.back()))];
        for (int j = 0; j < pt.laneN; ++j)
            gap = std::min(gap, std::fabs(pt.lanes[j] - aOffset));
    }
    return gap;
}

// The middle of the lane going the same way nearest aOffset (within 1.8 m;
// else 0, the route's lane) and how far that is (aGap; 1.8 if none).
float Driver::NearestLane(const PathPoint& aPt, float aOffset, float& aGap) const
{
    float o = 0.0f;
    aGap = 1.8f;
    for (int j = 0; j < aPt.laneN; ++j)
        if (std::fabs(aPt.lanes[j] - aOffset) < aGap)
        {
            aGap = std::fabs(aPt.lanes[j] - aOffset);
            o = aPt.lanes[j];
        }
    return o;
}

// The middle of the lane the car is in, as an offset from the route's
// lane, if that lane goes on alongside for aNeed metres (or to the end
// of the route); else 0, the route's lane.
float Driver::KeptLane(float aNeed) const
{
    float best;
    float o = NearestLane(mPath[Index(mProgress)], mLateral, best);
    if (std::fabs(o) < 1.0f)
        return 0.0f;
    float first;
    return LaneGoesOn(o, aNeed, first) ? first : 0.0f;
}

// Whether the lane whose middle is aOffset from the line here goes on
// alongside for aNeed metres (or to the end of the route), and where its
// middle is 20 m on (aFirst). Followed metre by metre (a lane's middle
// moves a little at a time, and may go unseen for a few metres where
// lane pieces join). A lane that leaves the road's room on this side
// doesn't count; with aStrict, nor does one that drifts more than 2.5 m
// (merging into the next one, or turning off).
bool Driver::LaneGoesOn(float aOffset, float aNeed, float& aFirst, bool aStrict, float* aEndsAt) const
{
    float o = aOffset;
    float unseen = 0.0f;
    aFirst = o;
    for (float a = 1.0f; a <= aNeed; a += 1.0f)
    {
        if (aEndsAt)
            *aEndsAt = a;
        if (mProgress + a > mS.back() - 30.0f)
            break;
        const PathPoint& pt = mPath[Index(mProgress + a)];
        float next = o, gap = 1.0f;
        for (int j = 0; j < pt.laneN; ++j)
            if (std::fabs(pt.lanes[j] - o) < gap)
            {
                gap = std::fabs(pt.lanes[j] - o);
                next = pt.lanes[j];
            }
        if ((aStrict && std::fabs(next - aOffset) > 2.5f) || next < std::min(pt.loSame, 0.0f) - 0.5f ||
            next > std::max(pt.hiSame, 0.0f) + 0.5f)
            return false;
        if (gap >= 1.0f && (unseen += 1.0f) > 6.0f)
            return false;
        if (gap < 1.0f)
            unseen = 0.0f;
        o = next;
        if (a <= 20.0f)
            aFirst = o;
    }
    return true;
}

// The lane to keep to: the one the car is in, while it goes on the same
// way for long enough; else the route's own lane (in time for a turn, or
// where its lane ends). Where there are enough lanes for the style to keep
// right (StyleRules::keepRightFrom), the rightmost lane going the same way
// instead, when that goes on for longer still (keeping right, except to
// pass).
float Driver::HomeLane(float aNeed, const char** aWhy) const
{
    float kept = KeptLane(aNeed), longer = std::max(1.5f * aNeed, 200.0f), first;
    // (Not in that lane yet, only nearest to it: moving into a lane takes
    // it going on for longer.)
    if (std::fabs(kept) > 0.5f && std::fabs(mLateral - kept) > 1.0f && !LaneGoesOn(kept, longer, first, true))
        kept = 0.0f;
    if (aWhy)
        *aWhy = kept == 0.0f ? "the route's lane" : "the lane it is in";
    const PathPoint& here = mPath[Index(mProgress)];
    if (here.laneN < Rules().keepRightFrom)
        return kept;
    float right = kept;
    for (int j = 0; j < here.laneN; ++j)
        if (here.lanes[j] < right - 1.5f && LaneGoesOn(here.lanes[j], longer, first, true))
            right = first;
    if (aWhy && right != kept)
        *aWhy = "keeping right";
    return right;
}

// On its own side of the road (not in the oncoming lanes).
bool Driver::OnOwnSide() const
{
    const PathPoint& here = mPath[Index(mProgress)];
    return mLateral > std::min(here.loSame, 0.0f) - 0.3f && mLateral < std::max(here.hiSame, 0.0f) + 0.3f;
}

// The shortest smooth sideways move by aBy metres the car can steer (the S
// bends at most 6·by/len^2: a radius of about 11 m, 7 m when nearly
// stopped).
float Driver::MinShiftLength(float aBy) const
{
    // (Less for a car that turns less than the one these were set on.)
    float v = std::fabs(mLast.speed), ms = std::clamp(1.3f - 0.02f * v, 0.45f, 1.0f);
    float most = (v < 4.0f ? 0.14f : 0.09f) *
                 std::clamp(mCar.steer.k * std::pow(ms, mCar.steer.p) / (0.17f * ms * ms), 0.4f, 1.0f);
    return std::sqrt(6.0f * std::fabs(aBy) / most);
}

// Drives a sideways move ahead in time, at the speed the car goes and at
// the speed it means to go, against the boxes the sensor saw, each moving
// as measured (a car crossing the road is where it will have got to).
// What runs into the car shortens its clearance; what is in the way ahead
// (on the move's path now, or met there) sets the block to follow.
// Traffic crossing the road is waited for, not swerved round: it blocks
// but doesn't count against the move.
Way Driver::Clearance(const Shift& aShift, float aHorizon, float aRoadUntil, float aRoadAtLeast) const
{
    Way way;
    way.clear = aHorizon;
    // Where the car will be: the move, from where the car really is
    // (it lags the plan a little), closing in on it over 12 m.
    float lag = std::clamp(mLateral - aShift.At(mProgress), -3.0f, 3.0f);
    auto dAt = [&](float at) { return aShift.At(at) + lag * std::exp(-std::max(at - mProgress, 0.0f) / 12.0f); };
    // Off the road; the line itself always counts as on it.
    auto outside = [&](float at, float d)
    {
        const PathPoint& pt = mPath[Index(at)];
        return std::max({0.0f, std::min(pt.lo, 0.0f) - d, d - std::max(pt.hi, 0.0f)});
    };
    float already = outside(mProgress, aShift.At(mProgress));
    // (Anywhere from where the move ends on, as far as it looks.)
    for (float at = aShift.start + aShift.len; at <= mProgress + aHorizon && at <= mS.back(); at += 4.0f)
    {
        const PathPoint& pt = mPath[Index(at)];
        if (aShift.to < std::min(pt.loSame, 0.0f) - 0.3f || aShift.to > std::max(pt.hiSame, 0.0f) + 0.3f)
        {
            way.endsOncoming = true;
            break;
        }
    }
    // (The whole move, however long, and on past its end for as long as
    // moving back would take: a lane that ends soon after is no way to
    // go. Ending off the road counts however far off the car is already.)
    float moveEnd = aShift.start + aShift.len - mProgress;
    float vNow = std::max(mLast.speed, 3.0f);
    float back = std::sqrt(5.8f * std::fabs(aShift.to) * vNow * vNow / (0.5f * MoveLat()));
    float roadTo = moveEnd + 30.0f + back;
    // (Past a car: until well past it, and then moving back.)
    float passTo = aRoadAtLeast > 0.0f ? aRoadAtLeast + back : 0.0f;
    for (float s = 2.0f; s <= std::min(std::max({aHorizon, roadTo, passTo}), aRoadUntil) && mProgress + s <= mS.back();
         s += 2.0f)
    {
        float at = mProgress + s, d = dAt(at);
        float out = outside(at, d);
        if (out > already + 0.1f || (s >= moveEnd && out > 0.05f))
        {
            way.off = true;
            way.offAt = s;
            break;
        }
        if (s > aHorizon)
            continue;
        const PathPoint& pt = mPath[Index(at)];
        if (d < std::min(pt.loSame, 0.0f) - 0.1f || d > std::max(pt.hiSame, 0.0f) + 0.1f)
            way.oncoming += 2.0f;
    }
    const float rear = mRear;
    float v = std::max(mLast.speed, 3.0f), want = std::max(v, mWant), up = AccelAt(v);
    float reach = 1e9f, block = 1e9f;
    for (const Body& b : mBodies)
    {
        float gap = b.s0 - mProgress - mFront;
        if (gap > aHorizon)
            continue;
        // Behind: only a car in another lane can run into the car (one in
        // its own lane is to give way), and only one that is faster, or
        // close behind (not to be cut up when moving in front of it).
        if (b.s1 < mProgress - rear - 1.0f &&
            ((b.u <= v + 0.5f && b.s1 < mProgress - rear - 3.0f - 1.5f * std::max(b.u, 0.0f)) ||
             (b.l1 > mLateral - mHalfWidth - 0.6f && b.l0 < mLateral + mHalfWidth + 0.6f)))
            continue;
        float r = mHalfWidth + (b.kind == 0 ? 0.6f : b.kind == 2 ? 0.5f : 0.4f);
        // Alongside: its near side is where the rays saw it, and the car
        // may stay as close as it already is (but come no closer; past
        // someone on foot, not within half a metre).
        bool alongside = gap < 0.0f && b.kind != 1;
        bool leftOfUs = 0.5f * (b.seenL0 + b.seenL1) > mLateral;
        float nearNow = leftOfUs ? b.seenL0 - mLateral : mLateral - b.seenL1;
        auto hits = [&](float d, float lo, float hi)
        {
            if (!(d > lo - r && d < hi + r))
                return false;
            if (!alongside)
                return true;
            float nearThen = leftOfUs ? b.seenL0 - d : d - b.seenL1;
            way.squeeze = std::max(way.squeeze, nearNow - nearThen);
            return nearThen < nearNow - 0.1f || nearThen < mHalfWidth + (b.kind == 2 ? 0.5f : 0.15f);
        };
        bool inPath = false;
        if (gap > -1.0f)
            for (float at : {b.s0, 0.5f * (b.s0 + b.s1), b.s1})
            {
                inPath = inPath || hits(dAt(at), b.l0, b.l1);
            }
        float met = 1e9f;
        for (int run = 0; run < 3; ++run)
        {
            // Holding its speed, speeding up (at a modest 2 m/s^2) to the
            // speed it means to go, and slowing down (3 m/s^2, as when it
            // has to give way): what comes from behind matters then.
            float top = run == 1 ? want : v, tUp = (top - v) / up, tDown = v / 3.0f;
            auto travel = [&](float t)
            {
                if (run == 2)
                    return t < tDown ? v * t - 1.5f * t * t : v * tDown - 1.5f * tDown * tDown;
                return t < tUp ? v * t + 0.5f * up * t * t : v * tUp + 0.5f * up * tUp * tUp + top * (t - tUp);
            };
            for (float t = 0.0f; t <= 8.0f && travel(t) < std::min(met, aHorizon); t += 0.1f)
            {
                // Where the car and the box are level, the car's offset there.
                // (A moving car with a little room in front of it, not to
                // be cut up.)
                float me = mProgress + travel(t);
                float from = std::max(me - rear, b.s0 + b.u * t),
                      to = std::min(me + mFront, b.s1 + b.u * t + 0.8f * std::max(b.u, 0.0f));
                if (from > to)
                    continue;
                // (Someone on foot keeps walking across, for longer than a
                // car crossing is followed, but may change their mind: the
                // further ahead in time, the more room round them.)
                float side = (std::fabs(b.w) > (b.kind == 2 ? 0.3f : 0.6f) ? b.w : 0.0f) *
                             std::min(t, b.kind == 2 ? 6.0f : 2.5f);
                float grow = b.kind == 2 ? std::min(0.3f * t, 1.0f) : 0.0f;
                float lo = b.l0 + side - grow, hi = b.l1 + side + grow;
                // (A long vehicle's ends swing out in a bend, past the
                // margin r keeps: SwingAt.)
                float mid = 0.5f * (from + to);
                if (hits(dAt(from) + SwingAt(me, from, 0.3f), lo, hi) ||
                    hits(dAt(mid) + SwingAt(me, mid, 0.3f), lo, hi) || hits(dAt(to) + SwingAt(me, to, 0.3f), lo, hi))
                {
                    met = std::min(met, me - mProgress);
                    break;
                }
            }
        }
        // Moving into the lane of a faster car close behind: it would
        // have to brake hard for this car (the mirror check).
        if (met > 1e8f && b.kind == 0 && b.s1 < mProgress - rear + 0.5f && b.u > v + 0.5f &&
            !(mLateral > b.seenL0 - r && mLateral < b.seenL1 + r) && aShift.to > b.seenL0 - r &&
            aShift.to < b.seenL1 + r)
        {
            float closing = b.u - v, behind = mProgress - rear - b.s1;
            if (behind < 3.0f + 0.5f * b.u + closing * closing / 6.0f)
                met = 0.0f;
        }
        bool crossing = std::fabs(b.w) > (b.kind == 2 ? 0.4f : 1.5f);
        if (met < 1e8f && !crossing && met < reach)
        {
            reach = met;
            way.contactU = b.u;
            way.contactS0 = b.s0;
            way.contactAlongside = gap <= -1.0f;
        }
        // Something only in the way as it moves across: in the way
        // where the two would meet, not where it is now.
        float at = inPath || std::fabs(b.w) <= 0.6f ? gap : std::max(gap, met);
        if ((inPath || met < 1e8f) && gap > -1.0f && at < block)
        {
            block = at;
            way.kind = b.kind;
            way.point = b.point;
            way.track = b.track;
            way.blockU = b.u;
            way.blockW = b.w;
            way.blockMeasured = b.kind == 0 && b.measured;
            way.blockKnown = b.known;
        }
    }
    if (block < 1e8f)
        way.block = std::max(block, 0.0f);
    if (reach < 1e8f)
    {
        way.contact = true;
        way.clear = std::min(way.clear, reach);
    }
    return way;
}
} // namespace BCAI::Autodrive
