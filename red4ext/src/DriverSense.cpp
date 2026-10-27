// BCAI Autodrive+: the sensor. The rays to cast each frame and what they hit,
// the game's list of vehicles nearby, and both turned into boxes along the
// path (cars grouped from their points and followed from frame to frame to
// measure how they move; walls as points).

#include "Driver.hpp"

namespace BCAI::Autodrive
{

// The rays to cast this frame, a share of two fixed patterns over the
// road ahead (the whole pattern every few frames): from just ahead of the
// bumper to points either side of where the car is heading.
std::vector<DriverRay> Driver::Rays(float aFront)
{
    if (aFront > 0.5f)
        mFront = aFront;
    mStartHits = 0;
    mRays.clear();
    if (!active || mPath.size() < 2)
        return mRays;
    static const float kCarS[] = {6, 10, 15, 22, 30, 40, 55, 75, 100, 130};
    static const float kCarL[] = {-4.5f, -3.0f, -1.5f, 0.0f, 1.5f, 3.0f, 4.5f};
    static const float kWallS[] = {4, 8, 13, 19, 27, 37, 50};
    static const float kWallL[] = {-4.0f, -2.0f, 0.0f, 2.0f, 4.0f};
    constexpr int kCars = 10 * 7, kWalls = 7 * 5, kCarsEach = 14, kWallsEach = 9;
    // From the bumper, tilted with the car on a slope. A tall vehicle's
    // rays look from windscreen height so they don't start in its own body
    // or run along the road; a long nose also gets extra clearance.
    float reach = mFront + 0.3f + (Long() ? 0.1f * (mFront + mRear) : 0.0f);
    Point3 origin{mPos.x + mFwd.x * reach, mPos.y + mFwd.y * reach, mPos.z + mSensorHeight + mSlope * reach};
    // Each ray ends 0.7 m above the road, or higher where the road rises
    // in between (over a crest), so it never runs into the road itself.
    auto endZ = [&](float at)
    {
        float from = mProgress + reach, z = At(at).z + 0.7f;
        for (float f : {0.25f, 0.5f, 0.75f})
        {
            float road = At(from + f * (at - from)).z + 0.4f;
            z = std::max(z, origin.z + (road - origin.z) / f);
        }
        return std::min(z, At(at).z + 3.0f);
    };
    auto add = [&](float s, float l, int kind)
    {
        float at = mProgress + s;
        if (at > mS.back())
            return;
        const PathPoint& pt = mPath[Index(at)];
        float lat = std::clamp(mShift.At(at) + l, pt.lo - 1.5f, pt.hi + 1.5f);
        Point3 c = At(at), n = Normal(at);
        mRays.push_back({origin, {c.x + n.x * lat, c.y + n.y * lat, endZ(at)}, kind});
    };
    for (int k = 0; k < kCarsEach; ++k)
    {
        int i = (mCarCursor + k) % kCars;
        add(kCarS[i / 7], kCarL[i % 7], 0);
    }
    mCarCursor = (mCarCursor + kCarsEach) % kCars;
    for (int k = 0; k < kWallsEach; ++k)
    {
        int i = (mWallCursor + k) % kWalls;
        add(kWallS[i / 5], kWallL[i % 5], 1);
    }
    mWallCursor = (mWallCursor + kWallsEach) % kWalls;
    // The next junction ahead: along the roads across it, either way.
    for (float ahead = mFront + 5.0f; ahead <= 70.0f && mProgress + ahead <= mS.back(); ahead += 2.0f)
        if (mPath[Index(mProgress + ahead)].junction)
        {
            static const float kAcross[] = {-22.0f, -15.0f, -9.0f, 9.0f, 15.0f, 22.0f};
            for (int k = 0; k < 6; ++k)
            {
                int i = (mAcrossCursor + k) % 12;
                float at = std::min(mProgress + ahead + (i / 6) * 8.0f, mS.back());
                Point3 c = At(at), n = Normal(at);
                float lat = mShift.At(at) + kAcross[i % 6];
                mRays.push_back({origin, {c.x + n.x * lat, c.y + n.y * lat, endZ(at)}, 0});
            }
            mAcrossCursor = (mAcrossCursor + 6) % 12;
            break;
        }
    // Behind, in the lanes either side: from the back of the car.
    {
        static const float kBackS[] = {8, 16, 28, 40};
        static const float kBackL[] = {-7.0f, -3.5f, 3.5f, 7.0f};
        float back = mRear + 0.3f;
        Point3 rear{mPos.x - mFwd.x * back, mPos.y - mFwd.y * back, mPos.z + 0.7f - mSlope * back};
        for (int k = 0; k < 4; ++k)
        {
            int i = (mBackCursor + k) % 16;
            float at = mProgress - kBackS[i / 4];
            if (at < 0.0f)
                continue;
            const PathPoint& pt = mPath[Index(at)];
            float lat = std::clamp(mLateral + kBackL[i % 4], pt.lo - 1.5f, pt.hi + 1.5f);
            Point3 c = At(at), n = Normal(at);
            mRays.push_back({rear, {c.x + n.x * lat, c.y + n.y * lat, c.z + 0.7f}, 0});
        }
        mBackCursor = (mBackCursor + 4) % 16;
        // Right beside it (the blind spot): short rays out of its sides,
        // level with its front and its back.
        Point3 left{-mFwd.y, mFwd.x, 0};
        for (float along : {mFront - 0.5f, -mRear + 0.5f})
            for (float side : {-1.0f, 1.0f})
            {
                float out = mHalfWidth + 0.15f;
                Point3 a{mPos.x + mFwd.x * along + left.x * side * out, mPos.y + mFwd.y * along + left.y * side * out,
                         mPos.z + 0.7f + mSlope * along};
                mRays.push_back({a, {a.x + left.x * side * 4.5f, a.y + left.y * side * 4.5f, a.z}, 0});
            }
    }
    // The nearest cars probe the way ahead: a few more rays just past
    // either side of what was seen of each, to find its width and length
    // before choosing a way past.
    std::vector<const Body*> probe;
    for (const Body& b : mBodies)
    {
        float gap = b.s0 - mProgress - mFront, d = mShift.At(b.s0);
        if (b.kind == 0 && gap > -2.0f && gap < 50.0f && b.seenL1 > d - 5.0f && b.seenL0 < d + 5.0f)
            probe.push_back(&b);
    }
    std::sort(probe.begin(), probe.end(), [](const Body* a, const Body* b) { return a->s0 < b->s0; });
    for (size_t k = 0; k < probe.size() && k < 3; ++k)
        for (float ds : {0.5f, 2.5f})
            for (float lat : {probe[k]->seenL0 - 1.0f, probe[k]->seenL0 - 0.4f, probe[k]->seenL1 + 0.4f,
                              probe[k]->seenL1 + 1.0f})
            {
                float at = std::max(probe[k]->s0, mProgress + reach + 1.0f) + ds;
                if (at > mS.back())
                    continue;
                Point3 c = At(at), n = Normal(at);
                mRays.push_back({origin, {c.x + n.x * lat, c.y + n.y * lat, endZ(at)}, 0});
            }
    // Slow or backing up: what is behind, 6 m straight back.
    mRearFrom = SIZE_MAX;
    mRearClear = 1e9f;
    if (std::fabs(mLast.speed) < 3.0f || mReverse > 0.0f)
    {
        mRearFrom = mRays.size();
        float back = mRear + 0.8f;
        Point3 rear{mPos.x - mFwd.x * back, mPos.y - mFwd.y * back, mPos.z + 0.7f - mSlope * back};
        for (int kind : {0, 1})
            for (float lat : {-0.8f, 0.0f, 0.8f})
                mRays.push_back({rear,
                                 {rear.x - mFwd.x * 6.0f - mFwd.y * lat, rear.y - mFwd.y * 6.0f + mFwd.x * lat,
                                  rear.z - mSlope * 6.0f},
                                 kind});
    }
    return mRays;
}

// Where this frame's rays ended. A hit is remembered (unless it is the
// car itself); whatever was remembered along a ray's clear stretch is gone.
size_t Driver::Sense(const std::vector<Point3>& aEnds, const std::vector<bool>& aHit)
{
    Point3 left{-mFwd.y, mFwd.x, 0};
    for (size_t r = 0; r < mRays.size() && r < aEnds.size(); ++r)
    {
        const DriverRay& ray = mRays[r];
        const Point3& end = aEnds[r];
        bool hit = r < aHit.size() && aHit[r];
        if (r >= mRearFrom)
        {
            if (r == mRearFrom)
                mRearClear = 1e9f;
            float d = std::hypot(end.x - ray.a.x, end.y - ray.a.y);
            if (hit && d > 0.3f)
                mRearClear = std::min(mRearClear, d);
            continue;
        }
        // Clear up to (just short of) where it ended.
        float rx = end.x - ray.a.x, ry = end.y - ray.a.y, rl2 = rx * rx + ry * ry;
        if (rl2 > 0.25f)
        {
            float stop = 1.0f - (hit ? 0.6f / std::sqrt(rl2) : 0.0f);
            mSeen.erase(std::remove_if(mSeen.begin(), mSeen.end(),
                                       [&](const Seen& o)
                                       {
                                           if (o.kind != ray.kind || mClock - o.born < 0.25f)
                                               return false;
                                           float t = ((o.p.x - ray.a.x) * rx + (o.p.y - ray.a.y) * ry) / rl2;
                                           if (t < 0.0f || t > stop)
                                               return false;
                                           float px = ray.a.x + rx * t - o.p.x, py = ray.a.y + ry * t - o.p.y;
                                           return px * px + py * py < 0.09f;
                                       }),
                        mSeen.end());
        }
        if (!hit)
            continue;
        // A ray that starts inside something (the car's own nose, as it
        // pitches) hits it where it starts.
        if (Dist2(end, ray.a) < 0.4f)
        {
            ++mStartHits;
            continue;
        }
        float x = (end.x - mPos.x) * mFwd.x + (end.y - mPos.y) * mFwd.y;
        float y = (end.x - mPos.x) * left.x + (end.y - mPos.y) * left.y;
        if (x > -mRear - 1.0f && x < mFront + 0.25f && std::fabs(y) < mHalfWidth + 0.3f)
            continue; // our own car
        if (ray.kind == 0 && Absorb(end) >= 0)
            continue; // a vehicle the game listed: known already
        float s, l;
        if (!Project(end, mProgress - kBehind, mProgress + 160.0f, s, l) || std::fabs(l) > 25.0f ||
            std::fabs(end.z - At(s).z) > 3.5f)
            continue; // not on (or above) the road ahead
        const PathPoint& pt = mPath[Index(s)];
        // (In a junction, cars on the roads across count too.)
        // (Never what is on the car's own way, even off the mapped road.)
        bool onWay = std::fabs(l - mShift.At(s)) < mHalfWidth + 1.2f;
        if (!onWay &&
            (pt.junction ? std::fabs(l) > 25.0f : (l > pt.hi + mHalfWidth + 0.3f || l < pt.lo - mHalfWidth - 0.3f)))
            continue; // beside the road (kerbs, buildings, parked on the sidewalk)
        bool merged = false;
        for (Seen& o : mSeen)
            if (o.kind == ray.kind && Dist2(o.p, end) < 0.6f)
            {
                bool moved = Dist2(o.first, end) > 0.8f;
                o = {end, s, l, mClock, ray.kind, moved ? mClock : o.born, moved ? end : o.first};
                merged = true;
                break;
            }
        if (!merged)
            mSeen.push_back({end, s, l, mClock, ray.kind, mClock, end});
    }
    if (mSeen.size() > 600)
        mSeen.erase(mSeen.begin(), mSeen.begin() + (mSeen.size() - 600));
    return mSeen.size();
}

void Driver::SetBody(float aFront, float aRear, float aHalfWidth, float aSensorHeight)
{
    // (Up to a very long trailer's: 20 m from its centre, 3 m to a side.)
    if (aFront > 0.5f && aFront < 20.0f)
        mFront = aFront;
    if (aRear > 0.5f && aRear < 20.0f)
        mRear = aRear;
    if (aHalfWidth > 0.3f && aHalfWidth < 3.0f)
        mHalfWidth = aHalfWidth + 0.05f;
    if (aSensorHeight > 0.4f && aSensorHeight < 2.5f)
        mSensorHeight = aSensorHeight;
}

// The game's list of vehicles (and people) nearby: each matched to the one
// listed before where that one should be by now (keeping the size learned
// for it), or new.
size_t Driver::SetVehicles(const std::vector<DriverVehicle>& aList)
{
    std::vector<bool> matched(mKnown.size(), false);
    for (const DriverVehicle& v : aList)
    {
        int best = -1;
        float bestD = v.person ? 1.5f : 3.0f;
        for (size_t k = 0; k < mKnown.size(); ++k)
        {
            Point3 at = KnownAt(mKnown[k]);
            float d = Dist2(at, v.p);
            if (!matched[k] && mKnown[k].person == v.person && d < bestD && std::fabs(at.z - v.p.z) < 3.0f)
            {
                best = int(k);
                bestD = d;
            }
        }
        if (best < 0)
        {
            Known k;
            k.p = v.p;
            k.vel = v.vel;
            if (v.bike)
            {
                k.front = k.rear = 1.1f;
                k.half = 0.45f;
            }
            if (v.person)
            {
                k.front = k.rear = k.half = 0.3f;
                k.person = true;
                k.anchor = v.p;
                k.anchorAt = mClock;
            }
            mKnown.push_back(k);
            matched.push_back(true);
            best = int(mKnown.size() - 1);
        }
        matched[best] = true;
        Known& k = mKnown[best];
        float fl = std::hypot(v.fwd.x, v.fwd.y);
        if (v.person)
        {
            // Someone on foot: how they moved over the last quarter second
            // or more, smoothed (the game gives no velocity; a jump no one
            // walks or runs is a mix-up, not a speed).
            k.p = v.p;
            k.fwd = fl > 1e-3f ? Point3{v.fwd.x / fl, v.fwd.y / fl, 0} : Point3{1, 0, 0};
            k.listed = mClock;
            float span = mClock - k.anchorAt;
            if (std::hypot(v.vel.x, v.vel.y) > 0.3f)
                k.vel = v.vel;
            else if (span >= 0.25f)
            {
                Point3 moved{(v.p.x - k.anchor.x) / span, (v.p.y - k.anchor.y) / span, 0.0f};
                k.vel = std::hypot(moved.x, moved.y) > 8.0f
                            ? Point3{}
                            : Point3{k.vel.x + (moved.x - k.vel.x) * 0.6f, k.vel.y + (moved.y - k.vel.y) * 0.6f, 0.0f};
                k.anchor = v.p;
                k.anchorAt = mClock;
            }
            if (std::hypot(k.vel.x, k.vel.y) > 0.4f)
                k.stillSince = -1.0f;
            else if (k.stillSince < 0.0f)
                k.stillSince = mClock;
            continue;
        }
        // How it moved since last listed: traffic far from the player may
        // be moved without physics, its velocity reading 0 while it goes.
        float since = mClock - k.listed;
        Point3 moved = since > 0.03f && k.listed > 0.0f
                           ? Point3{(v.p.x - k.p.x) / since, (v.p.y - k.p.y) / since, (v.p.z - k.p.z) / since}
                           : v.vel;
        bool stillReading = std::hypot(v.vel.x, v.vel.y) < 0.3f;
        k.vel = stillReading && std::hypot(moved.x, moved.y) > 0.5f
                    ? Point3{0.5f * (k.vel.x + moved.x), 0.5f * (k.vel.y + moved.y), 0.5f * (k.vel.z + moved.z)}
                    : v.vel;
        k.p = v.p;
        k.fwd = fl > 1e-3f ? Point3{v.fwd.x / fl, v.fwd.y / fl, 0} : Point3{1, 0, 0};
        k.listed = mClock;
        k.parked = v.parked;
        k.bike = v.bike;
        // Its size as its record gives it: a truck is far longer than the
        // car it is first taken for (the rays only grow it bit by bit).
        if (v.length > 1.0f && v.width > 0.5f)
        {
            k.front = std::max(k.front, std::min(0.5f * v.length, 12.0f));
            k.rear = std::max(k.rear, std::min(0.5f * v.length, 12.0f));
            k.half = std::max(k.half, std::min(0.5f * v.width, 1.6f));
        }
        if (std::hypot(k.vel.x, k.vel.y) > 0.5f)
            k.stillSince = -1.0f;
        else if (k.stillSince < 0.0f)
            k.stillSince = mClock;
    }
    mKnown.erase(std::remove_if(mKnown.begin(), mKnown.end(), [&](const Known& k) { return mClock - k.listed > 0.6f; }),
                 mKnown.end());
    return mKnown.size();
}

// A ray hit on a listed vehicle (inside its outline, or within 1.5 m of
// it: then the outline grows to take it in). Which one, or -1.
int Driver::Absorb(const Point3& aHit)
{
    int best = -1;
    float need = 1.5f, bx = 0.0f, by = 0.0f;
    for (size_t k = 0; k < mKnown.size(); ++k)
    {
        const Known& kn = mKnown[k];
        Point3 at = KnownAt(kn);
        if (kn.person || std::fabs(aHit.z - at.z) > 5.0f)
            continue;
        float dx = aHit.x - at.x, dy = aHit.y - at.y;
        float x = dx * kn.fwd.x + dy * kn.fwd.y, y = -dx * kn.fwd.y + dy * kn.fwd.x;
        float out = std::max({0.0f, x - kn.front, -kn.rear - x, std::fabs(y) - kn.half});
        if (out < need)
        {
            need = out;
            best = int(k);
            bx = x;
            by = y;
        }
    }
    if (best >= 0 && need > 0.0f)
    {
        Known& kn = mKnown[best];
        kn.front = std::min(std::max(kn.front, bx + 0.05f), 12.0f);
        kn.rear = std::min(std::max(kn.rear, -bx + 0.05f), 12.0f);
        kn.half = std::min(std::max(kn.half, std::fabs(by) + 0.05f), 1.6f);
    }
    return best;
}

// What the sensor saw ahead, as boxes along the path: a car's points
// grouped, and grown to a whole car (its width mostly away from this
// car; its length beyond the nearest point) until
// the points themselves span one. Wall points stay points.
void Driver::Gather(float aHorizon)
{
    mBodies.clear();
    mTracks.erase(std::remove_if(mTracks.begin(), mTracks.end(),
                                 [&](const Track& t) { return mClock - t.lastSeen > 1.0f; }),
                  mTracks.end());
    for (size_t i = 0; i < mSeen.size(); ++i)
    {
        const Seen& o = mSeen[i];
        float gap = o.s - mProgress - mFront;
        if (gap < -kBehind || gap > aHorizon + 5.0f)
            continue;
        if (o.kind != 0)
        {
            mBodies.push_back({o.s, o.s, o.l, o.l, o.kind, int(i)});
            continue;
        }
        if (OnKnown(o.p))
            continue; // seen before the game listed it
        Body* into = nullptr;
        for (Body& b : mBodies)
            if (b.kind == 0 && o.s > b.s0 - 2.5f && o.s < std::max(b.s1, b.s0 + kCarLength) + 1.0f &&
                o.l > b.l0 - 2.0f && o.l < b.l1 + 2.0f)
            {
                into = &b;
                break;
            }
        if (!into)
            mBodies.push_back({o.s, o.s, o.l, o.l, 0, int(i)});
        else
        {
            if (o.s < into->s0)
                into->point = int(i);
            into->s0 = std::min(into->s0, o.s);
            into->s1 = std::max(into->s1, o.s);
            into->l0 = std::min(into->l0, o.l);
            into->l1 = std::max(into->l1, o.l);
        }
    }
    for (Body& b : mBodies)
        if (b.kind == 0)
        {
            Follow(b);
            b.seenL0 = b.l0;
            b.seenL1 = b.l1;
            // Mostly away from this car (the rays see its near side or
            // the far part of its back), some towards it.
            float pad = std::max(kCarWidth - (b.l1 - b.l0), 0.0f);
            float side = 0.5f * (b.l0 + b.l1) - mLateral;
            if (side > 1.2f)
            {
                b.l1 += pad;
                b.l0 -= 0.3f * pad;
            }
            else if (side < -1.2f)
            {
                b.l0 -= pad;
                b.l1 += 0.3f * pad;
            }
            else
            {
                b.l0 -= 0.5f * pad;
                b.l1 += 0.5f * pad;
            }
            b.s1 = std::max(b.s1, b.s0 + kCarLength);
            if (b.track >= 0 && mTracks[b.track].measured)
            {
                const Track& t = mTracks[b.track];
                b.measured = true;
                b.u = t.speed;
                b.w = t.lspeed;
                // Its near end only ever comes nearer as more of it is
                // seen (a couple of metres at most), never goes away.
                b.parked = mClock - t.stillSince > 1.0f && t.s0 - t.firstS0 < 0.8f && t.firstS0 - t.s0 < 2.5f &&
                           std::fabs(t.speed) < 1.5f && std::fabs(t.lspeed) < 1.0f;
                b.stillFor = b.parked ? mClock - t.stillSince : 0.0f;
            }
            else if (b.track >= 0 && mTracks[b.track].windows >= 1 && std::fabs(mTracks[b.track].lspeed) > 1.5f)
            {
                // Seen crossing the road after half a second: that is
                // enough to give way to it.
                b.w = mTracks[b.track].lspeed;
            }
            else
            {
                // Not measured yet: in the oncoming lanes, take it to be
                // coming this way at about the speed limit; beside or
                // behind, to be keeping up with this car (ahead, to be
                // standing still).
                const PathPoint& pt = mPath[Index(b.s0)];
                float c = 0.5f * (b.l0 + b.l1);
                bool onRoad = c < pt.hi + mHalfWidth + 1.0f && c > pt.lo - mHalfWidth - 1.0f;
                if (onRoad && (c > std::max(pt.hiSame, 0.0f) + 1.0f || c < std::min(pt.loSame, 0.0f) - 1.0f))
                    b.u = -0.8f * mLimit[Index(b.s0)];
                else if (b.s0 < mProgress + mFront)
                    b.u = std::max(mLast.speed, 0.0f);
            }
        }
    // The vehicles the game listed: their outlines along the path, and
    // how they move, known at once.
    for (size_t k = 0; k < mKnown.size(); ++k)
    {
        const Known& kn = mKnown[k];
        Point3 at = KnownAt(kn), f = kn.fwd, left{-f.y, f.x, 0};
        float s0 = 1e9f, s1 = -1e9f, l0 = 1e9f, l1 = -1e9f;
        bool onPath = true;
        for (float a : {kn.front, -kn.rear})
            for (float c : {kn.half, -kn.half})
            {
                Point3 q{at.x + f.x * a + left.x * c, at.y + f.y * a + left.y * c, at.z};
                float s, l;
                if (!(onPath = Project(q, mProgress - kBehind - 10.0f, mProgress + aHorizon + 20.0f, s, l)) ||
                    std::fabs(at.z - At(s).z) > 3.5f)
                {
                    onPath = false;
                    break;
                }
                s0 = std::min(s0, s);
                s1 = std::max(s1, s);
                l0 = std::min(l0, l);
                l1 = std::max(l1, l);
            }
        float gap = s0 - mProgress - mFront;
        if (!onPath || gap < -kBehind || gap > aHorizon + 5.0f)
            continue;
        const PathPoint& pt = mPath[Index(0.5f * (s0 + s1))];
        if (kn.person)
        {
            // Someone on foot: only on the road or right by its edge (a
            // long vehicle's ends swing over the kerb), or about to step
            // onto it (within 4 s at the pace they go), with a little room
            // round them. (Not the people walking along the pavement.)
            Point3 n = Normal(0.5f * (s0 + s1));
            float w = kn.vel.x * n.x + kn.vel.y * n.y, c = 0.5f * (l0 + l1);
            float hiEdge = std::max(pt.hi, 0.0f) + mHalfWidth, loEdge = std::min(pt.lo, 0.0f) - mHalfWidth;
            bool on = c < hiEdge + 1.0f && c > loEdge - 1.0f;
            bool stepping = (c >= hiEdge && w < -0.4f && c + 4.0f * w < hiEdge) ||
                            (c <= loEdge && w > 0.4f && c + 4.0f * w > loEdge);
            if (!on && !stepping)
                continue;
            Body b{s0 - 0.2f, s1 + 0.2f, l0 - 0.2f, l1 + 0.2f, 2, -1};
            b.known = int(k);
            b.measured = true;
            b.u = kn.vel.x * n.y - kn.vel.y * n.x;
            b.w = w;
            b.seenL0 = b.l0;
            b.seenL1 = b.l1;
            b.stillFor = kn.stillSince >= 0.0f ? mClock - kn.stillSince : 0.0f;
            mBodies.push_back(b);
            continue;
        }
        // (Beside the road: parked on the sidewalk, or on another road;
        // in a junction, cars on the roads across count.)
        if (pt.junction ? std::min(std::fabs(l0), std::fabs(l1)) > 25.0f
                        : (l0 > pt.hi + mHalfWidth + 0.3f || l1 < pt.lo - mHalfWidth - 0.3f))
            continue;
        Body b{s0, s1, l0, l1, 0, -1};
        b.known = int(k);
        b.measured = true;
        Point3 n = Normal(0.5f * (s0 + s1));
        b.u = kn.vel.x * n.y - kn.vel.y * n.x;
        b.w = kn.vel.x * n.x + kn.vel.y * n.y;
        b.seenL0 = l0;
        b.seenL1 = l1;
        b.parked = kn.parked;
        b.stillFor = kn.stillSince >= 0.0f ? mClock - kn.stillSince : 0.0f;
        b.crosswise = std::fabs(kn.fwd.x * n.y - kn.fwd.y * n.x) < 0.5f;
        mBodies.push_back(b);
    }
}

// On (or just by) a vehicle the game listed.
bool Driver::OnKnown(const Point3& p) const
{
    for (const Known& kn : mKnown)
    {
        if (kn.person)
            continue; // (the rays see no one on foot)
        Point3 at = KnownAt(kn);
        float dx = p.x - at.x, dy = p.y - at.y;
        float x = dx * kn.fwd.x + dy * kn.fwd.y, y = -dx * kn.fwd.y + dy * kn.fwd.x;
        if (x < kn.front + 0.5f && x > -kn.rear - 0.5f && std::fabs(y) < kn.half + 0.5f && std::fabs(p.z - at.z) < 5.0f)
            return true;
    }
    return false;
}

// Matches a car's outline to the car seen in the frames before (where
// that one should be by now, overlapping sideways), or starts a new one.
void Driver::Follow(Body& b)
{
    int best = -1;
    float bestD = 3.5f;
    for (size_t k = 0; k < mTracks.size(); ++k)
    {
        Track& t = mTracks[k];
        if (t.lastSeen == mClock)
            continue;
        float ago = mClock - t.lastSeen;
        float predicted = t.s0 + t.speed * ago, side = t.lspeed * ago;
        float d = std::fabs(b.s0 - predicted);
        if (d < bestD && b.l0 < t.l1 + side + 1.0f && b.l1 > t.l0 + side - 1.0f)
        {
            best = int(k);
            bestD = d;
        }
    }
    if (best < 0)
    {
        Track t;
        t.s0 = b.s0;
        t.l0 = b.l0;
        t.l1 = b.l1;
        t.since = t.lastSeen = t.anchorTime = t.stillSince = mClock;
        t.firstS0 = b.s0;
        t.anchorS = t.extS0 = b.s0;
        t.anchorS1 = t.extS1 = b.s1;
        t.anchorL0 = t.extL0 = b.l0;
        t.anchorL1 = t.extL1 = b.l1;
        mTracks.push_back(t);
        b.track = int(mTracks.size() - 1);
        return;
    }
    Track& t = mTracks[best];
    t.s0 = b.s0;
    t.l0 = b.l0;
    t.l1 = b.l1;
    t.lastSeen = mClock;
    // Alongside, the rays see only bits of it: keep what was measured.
    bool alongside = b.s0 < mProgress + mFront + 1.0f && b.s1 > mProgress - mRear - 1.0f;
    if (alongside)
    {
        t.anchorS = b.s0;
        t.anchorS1 = b.s1;
        t.anchorL0 = b.l0;
        t.anchorL1 = b.l1;
        t.anchorTime = mClock;
    }
    else if (mClock - t.anchorTime >= 0.5f)
    {
        // When both ends of its outline moved the same way, the lesser
        // of the two (more of a car coming into view only makes it
        // bigger). Otherwise its front end, if it went on moving the
        // same way for a second (not just a nearer part coming into view).
        auto both = [](float a, float b) { return a * b > 0.0f ? std::copysign(std::min(std::fabs(a), std::fabs(b)), a) : 0.0f; };
        float span = mClock - t.anchorTime;
        float front = (b.s0 - t.anchorS) / span, rate = both(front, (b.s1 - t.anchorS1) / span);
        if (rate == 0.0f && std::fabs(front) > 1.0f)
        {
            if (t.pending * front > 0.0f)
                rate = std::fabs(front) < std::fabs(t.pending) ? front : t.pending;
            t.pending = front;
        }
        else
            t.pending = 0.0f;
        float side = both(b.l0 - t.anchorL0, b.l1 - t.anchorL1) / span;
        t.speed = t.windows > 0 ? t.speed + (rate - t.speed) * 0.5f : rate;
        t.lspeed = t.windows > 0 ? t.lspeed + (side - t.lspeed) * 0.5f : side;
        t.speed = std::clamp(t.speed, -40.0f, 60.0f);
        t.lspeed = std::clamp(t.lspeed, -15.0f, 15.0f);
        t.measured = ++t.windows >= 2;
        if (std::fabs(t.speed) > 1.5f || std::fabs(t.lspeed) > 1.0f)
        {
            t.firstS0 = b.s0;
            t.stillSince = mClock;
        }
        t.anchorS = b.s0;
        t.anchorS1 = b.s1;
        t.anchorL0 = b.l0;
        t.anchorL1 = b.l1;
        t.anchorTime = mClock;
    }
    if (t.measured && std::fabs(t.speed) < 1.0f && std::fabs(t.lspeed) < 0.5f)
    {
        t.extS0 = std::min(t.extS0, b.s0);
        t.extS1 = std::max(t.extS1, b.s1);
        t.extL0 = std::min(t.extL0, b.l0);
        t.extL1 = std::max(t.extL1, b.l1);
        b.s0 = t.extS0;
        b.s1 = t.extS1;
        b.l0 = t.extL0;
        b.l1 = t.extL1;
    }
    else
    {
        t.extS0 = b.s0;
        t.extS1 = b.s1;
        t.extL0 = b.l0;
        t.extL1 = b.l1;
    }
    b.track = best;
}
} // namespace BCAI::Autodrive
