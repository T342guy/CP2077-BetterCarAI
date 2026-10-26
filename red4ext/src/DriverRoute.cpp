// BCAI Autodrive+: the route over the lane map (A* over lanes and their
// links, or straight on along the road when cruising), the line to drive
// along it (the room either side, the lanes alongside, smoothed the way a
// driver would take it) and the speed plan.

#include "Driver.hpp"

namespace BCAI::Autodrive
{
// (Re)plans from aPos, or from aFrom (a lane and distance along it).
bool Driver::Plan(const LaneGraph& aGraph, const Point3& aPos, const Point3* aFwd, const Spot* aFrom, std::string& aMsg)
{
    // The lane under the car going its way; the next best ones too, in case
    // that one leads nowhere (a dead end).
    std::vector<Spot> starts;
    if (aFrom)
        starts.push_back(*aFrom);
    else
        starts = FindLanes(aGraph, mGeo, aPos, aFwd, 15.0f);
    if (starts.empty())
    {
        aMsg = "no drivable lane under the car";
        return false;
    }
    Spot start = starts.front();
    std::vector<Leg> legs;
    size_t expanded = 0;
    if (mHasGoal)
    {
        Route route;
        bool planned = false;
        for (size_t k = 0; k < starts.size() && k < 4 && !planned; ++k)
        {
            Route tried;
            planned = gPlanOverride ? gPlanOverride(aGraph, mGeo, starts[k], mGoal, tried)
                                    : PlanRoute(aGraph, mGeo, starts[k], mGoal, RouteOptions{}, tried);
            if (planned || k == 0)
                route = std::move(tried);
            if (planned)
                start = starts[k];
        }
        if (!planned)
        {
            aMsg = route.why;
            return false;
        }
        legs = std::move(route.legs);
        expanded = route.searched;
        Note("route: %s", DescribeRoute(route).c_str());
    }
    else
        legs = Cruise(aGraph, mGeo, start, kCruiseLength);
    Build(aGraph, legs, aPos);
    if (mPath.size() < 2)
    {
        aMsg = "route too short";
        return false;
    }
    mProgress = 0.0f;
    // The car is where the new line starts; what it saw moves onto it.
    mShift = Shift{};
    mShiftAge = 0.0f;
    mPassing = false;
    mTracks.clear();
    float lateral;
    Locate(aPos, lateral);
    for (Seen& o : mSeen)
        if (!Project(o.p, mProgress - kBehind, mProgress + 160.0f, o.s, o.l))
            o.time = -1e9f;
    aMsg = Format("route: %zu lanes, %.0f m%s", legs.size(), mS.back(),
                  mHasGoal ? Format(", %zu lanes searched", expanded).c_str() : " (cruising)");
    status = mHasGoal ? "driving to the destination" : "cruising";
    return true;
}

void Driver::Build(const LaneGraph& aGraph, const std::vector<Leg>& aLegs, const Point3& aPos)
{
    // The legs' outlines joined, then every metre.
    std::vector<PathPoint> raw;
    std::vector<float> rawLimit;
    auto add = [&](const PathPoint& p, float limit)
    {
        if (!raw.empty() && Dist2(raw.back().p, p.p) < 0.2f)
            return;
        raw.push_back(p);
        rawLimit.push_back(limit);
    };
    for (const auto& leg : aLegs)
    {
        const auto& geo = mGeo.Get(aGraph, leg.lane);
        if (geo.pts.size() < 2 || leg.to <= leg.from)
            continue;
        float limit = aGraph.MaxSpeed(leg.lane);
        if (limit < 5.0f)
            limit = 10.0f;
        add({geo.At(leg.from), leg.lane, leg.from}, limit);
        for (size_t i = 0; i < geo.pts.size(); ++i)
            if (geo.s[i] > leg.from && geo.s[i] < leg.to)
                add({geo.pts[i], leg.lane, geo.s[i]}, limit);
        add({geo.At(leg.to), leg.lane, leg.to}, limit);
    }
    mPath.clear();
    mS.clear();
    mLimit.clear();
    if (raw.size() < 2)
        return;
    std::vector<float> rawS(raw.size(), 0.0f);
    for (size_t k = 1; k < raw.size(); ++k)
        rawS[k] = rawS[k - 1] + Dist2(raw[k - 1].p, raw[k].p);
    size_t k = 0;
    for (float s = 0.0f; s <= rawS.back(); s += 1.0f)
    {
        while (k + 2 < raw.size() && rawS[k + 1] < s)
            ++k;
        float len = rawS[k + 1] - rawS[k];
        float t = len > 1e-4f ? std::clamp((s - rawS[k]) / len, 0.0f, 1.0f) : 0.0f;
        bool sameLane = raw[k].lane == raw[k + 1].lane;
        PathPoint pt;
        pt.p = Lerp(raw[k].p, raw[k + 1].p, t);
        pt.lane = raw[k].lane;
        pt.laneS = sameLane ? raw[k].laneS + (raw[k + 1].laneS - raw[k].laneS) * t : raw[k].laneS;
        mPath.push_back(pt);
        mLimit.push_back(rawLimit[k + 1]);
    }
    if (mPath.size() >= 2)
    {
        MeasureRoom(aGraph);
        Point3 n = NormalAt(0);
        Flow((aPos.x - mPath[0].p.x) * n.x + (aPos.y - mPath[0].p.y) * n.y);
    }
    mS.resize(mPath.size());
    for (size_t i = 0; i < mPath.size(); ++i)
        mS[i] = float(i);
    Profile();
}

// Left of the path's direction at point i.
Point3 Driver::NormalAt(size_t i) const
{
    size_t a = i > 0 ? i - 1 : 0, b = std::min(i + 1, mPath.size() - 1);
    float dx = mPath[b].p.x - mPath[a].p.x, dy = mPath[b].p.y - mPath[a].p.y, l = std::hypot(dx, dy);
    return l > 1e-4f ? Point3{-dy / l, dx / l, 0} : Point3{0, 1, 0};
}

// How far the car may move off the line at each point: samples every
// half metre along the line square to the path there count as road when
// within half a lane of some road lane (of any direction: inside a
// junction every turn's lanes cover it), and the room runs out to the
// first sample that isn't, or that is on a sidewalk. "Same" room stops
// where the nearest lane runs the other way (oncoming traffic).
void Driver::MeasureRoom(const LaneGraph& aGraph)
{
    const float kReach = 12.0f, kStep = 0.5f, kHalfLane = 2.6f, kSidewalk = 1.6f;
    struct Seg
    {
        Point3 a, b;
        float dx, dy, l2;
        int kind; // 0 road, 1 sidewalk
        bool traffic; // a lane traffic drives (not a shoulder, parking or GPS-only lane)
    };
    std::vector<Seg> segs;
    size_t segsAt = 0;
    const int samples = int(kReach / kStep);
    for (size_t i = 0; i < mPath.size(); ++i)
    {
        PathPoint& pt = mPath[i];
        pt.junction = (aGraph.Flags(pt.lane) & kLaneIntersection) != 0;
        // The lane pieces near this stretch of the path.
        if (i == 0 || i - segsAt >= 8)
        {
            segs.clear();
            segsAt = i;
            for (uint32_t lane : aGraph.Near(pt.p.x, pt.p.y, kReach + 14.0f))
            {
                uint16_t flags = aGraph.Flags(lane);
                bool sidewalk = (flags & kLanePavement) && !(flags & kLaneCrossWalk);
                if (!(Drivable(flags) || sidewalk))
                    continue;
                const auto& geo = mGeo.Get(aGraph, lane);
                for (size_t k = 0; k + 1 < geo.pts.size(); ++k)
                {
                    const Point3 &a = geo.pts[k], &b = geo.pts[k + 1];
                    float dx = b.x - a.x, dy = b.y - a.y;
                    segs.push_back({a, b, dx, dy, dx * dx + dy * dy, sidewalk ? 1 : 0,
                                    !(flags & (kLaneTrafficDisabled | kLaneNoAIDriving | kLaneNoAutodrive |
                                               kLaneGPSOnly))});
                }
            }
        }
        Point3 n = NormalAt(i), t{n.y, -n.x, 0};
        // Per sample: 0 off the road, 1 road going our way (or across),
        // 2 oncoming road, 3 sidewalk.
        auto classify = [&](float l)
        {
            float x = pt.p.x + n.x * l, y = pt.p.y + n.y * l;
            float bestRoad = 1e9f, bestWalk = 1e9f, roadDot = 1.0f;
            for (const Seg& g : segs)
            {
                float u = g.l2 > 1e-6f ? std::clamp(((x - g.a.x) * g.dx + (y - g.a.y) * g.dy) / g.l2, 0.0f, 1.0f) : 0.0f;
                float z = g.a.z + (g.b.z - g.a.z) * u;
                if (std::fabs(z - pt.p.z) > 3.0f)
                    continue;
                float px = g.a.x + g.dx * u - x, py = g.a.y + g.dy * u - y, d = px * px + py * py;
                if (g.kind == 1)
                    bestWalk = std::min(bestWalk, d);
                else if (d < bestRoad)
                {
                    bestRoad = d;
                    float len = std::sqrt(g.l2);
                    roadDot = len > 1e-3f ? (g.dx * t.x + g.dy * t.y) / len : 1.0f;
                }
            }
            if (bestWalk < kSidewalk * kSidewalk && bestWalk < bestRoad)
                return 3;
            if (bestRoad > kHalfLane * kHalfLane)
                return 0;
            return roadDot < -0.5f ? 2 : 1;
        };
        auto side = [&](float dir, float& aAll, float& aSame)
        {
            float all = 0.0f, same = -1.0f;
            for (int k = 1; k <= samples; ++k)
            {
                int c = classify(dir * k * kStep);
                if (c == 0 || c == 3)
                    break;
                all = k * kStep;
                if (c == 2 && same < 0.0f)
                    same = (k - 1) * kStep;
            }
            if (same < 0.0f)
                same = all;
            // The car's centre stays a half car width inside the edges.
            aAll = std::max(all - mHalfWidth, 0.3f) * dir;
            aSame = std::clamp(same - mHalfWidth, 0.3f, std::max(all - mHalfWidth, 0.3f)) * dir;
        };
        side(1.0f, pt.hi, pt.hiSame);
        side(-1.0f, pt.lo, pt.loSame);
        // The lanes alongside going the same way (pieces square to this
        // point running parallel), and whether roads cross here.
        float found[24];
        int nFound = 0;
        for (const Seg& g : segs)
        {
            if (g.kind != 0 || g.l2 < 1e-4f)
                continue;
            float len = std::sqrt(g.l2), dot = (g.dx * t.x + g.dy * t.y) / len;
            float u = ((pt.p.x - g.a.x) * g.dx + (pt.p.y - g.a.y) * g.dy) / g.l2, uc = std::clamp(u, 0.0f, 1.0f);
            float z = g.a.z + (g.b.z - g.a.z) * uc;
            if (std::fabs(z - pt.p.z) > 3.0f)
                continue;
            float x = g.a.x + g.dx * uc - pt.p.x, y = g.a.y + g.dy * uc - pt.p.y;
            if (pt.junction && std::fabs(dot) < 0.7f && x * x + y * y < kHalfLane * kHalfLane)
                pt.crossing = true;
            if (!g.traffic || dot < 0.95f || u < -0.05f || u > 1.05f || nFound == 24)
                continue;
            float l = x * n.x + y * n.y;
            if (l > pt.loSame - 1.5f && l < pt.hiSame + 1.5f)
                found[nFound++] = l;
        }
        std::sort(found, found + nFound);
        for (int a = 0; a < nFound;)
        {
            int b = a;
            float sum = 0.0f;
            while (b < nFound && found[b] - found[a] < 1.5f)
                sum += found[b++];
            float mid = sum / float(b - a);
            if (pt.laneN < 6)
                pt.lanes[pt.laneN++] = mid;
            a = b;
        }
    }
    // A lane missing for a metre shouldn't open or close the road: the
    // smallest room within 2 m either side.
    std::vector<PathPoint> was = mPath;
    for (size_t i = 0; i < mPath.size(); ++i)
        for (size_t j = (i >= 2 ? i - 2 : 0); j <= std::min(i + 2, mPath.size() - 1); ++j)
        {
            mPath[i].hi = std::min(mPath[i].hi, was[j].hi);
            mPath[i].hiSame = std::min(mPath[i].hiSame, was[j].hiSame);
            mPath[i].lo = std::max(mPath[i].lo, was[j].lo);
            mPath[i].loSame = std::max(mPath[i].loSame, was[j].loSame);
        }
}

// The line to drive: the lane path smoothed the way a driver would take
// it, bending less where the lanes turn sharply (most in junctions) but
// pulled back towards the lane, so it doesn't run along the road's edges.
// It stays on its own side of the road, with a margin from the edges. It starts where the car is
// (aStartLateral off the lane) and ends on the lane.
void Driver::Flow(float aStartLateral)
{
    size_t n = mPath.size();
    if (n < 10)
        return;
    std::vector<Point3> nrm(n);
    for (size_t i = 0; i < n; ++i)
        nrm[i] = NormalAt(i);
    // 1 in a junction, fading out over 15 m either side.
    std::vector<float> nearJunction(n, 0.0f);
    float last = -1e9f;
    for (size_t i = 0; i < n; ++i)
    {
        if (mPath[i].junction)
            last = float(i);
        nearJunction[i] = std::clamp(1.0f - (float(i) - last) / 15.0f, 0.0f, 1.0f);
    }
    last = 1e9f;
    for (size_t i = n; i-- > 0;)
    {
        if (mPath[i].junction)
            last = float(i);
        nearJunction[i] = std::max(nearJunction[i], std::clamp(1.0f - (last - float(i)) / 15.0f, 0.0f, 1.0f));
    }
    // The room measured reaches past the lanes' real edges (it counts
    // as road up to 2.6 m from a lane's middle): keep 0.7 m more inside.
    const float kMargin = 0.7f;
    std::vector<float> lo(n), hi(n), o(n, 0.0f), pull(n);
    for (size_t i = 0; i < n; ++i)
    {
        const PathPoint& pt = mPath[i];
        // (In a junction, where every turn's lanes count as road, no more
        // than 1.5 m off the lane either.)
        lo[i] = std::min(pt.loSame + kMargin, 0.0f);
        hi[i] = std::max(pt.hiSame - kMargin, 0.0f);
        if (pt.junction)
        {
            lo[i] = std::max(lo[i], -1.5f);
            hi[i] = std::min(hi[i], 1.5f);
        }
        // How hard it is held to the lane, per metre^4 of bending: the
        // line strays from the lane over about 15 m on the road, 25 m
        // in junctions.
        pull[i] = 1.0f / std::pow(15.0f + 10.0f * nearJunction[i], 4.0f);
    }
    float start = std::clamp(aStartLateral, -6.0f, 6.0f);
    const size_t pin = 3;
    for (size_t i = 0; i < pin; ++i)
    {
        o[i] = start;
        lo[i] = std::min(lo[i], start);
        hi[i] = std::max(hi[i], start);
    }
    // Least bending against the pull to the lane: each point moves to
    // where the line through its neighbours (k and 2k away) would put it,
    // drawn back towards the lane (bending k metres apart weighs k^4 as
    // much). Coarse steps first, then finer.
    auto q = [&](size_t j) { return Point3{mPath[j].p.x + nrm[j].x * o[j], mPath[j].p.y + nrm[j].y * o[j], 0}; };
    for (size_t k : {12, 6, 3, 2, 1})
        for (int it = 0; it < 60; ++it)
            for (size_t i = pin; i + pin < n; ++i)
            {
                Point3 a = q(i >= k ? i - k : 0), b = q(std::min(i + k, n - 1));
                Point3 a2 = q(i >= 2 * k ? i - 2 * k : 0), b2 = q(std::min(i + 2 * k, n - 1));
                float mx = (4.0f * (a.x + b.x) - a2.x - b2.x) / 6.0f - mPath[i].p.x;
                float my = (4.0f * (a.y + b.y) - a2.y - b2.y) / 6.0f - mPath[i].p.y;
                float k4 = float(k * k) * float(k * k);
                float want = 6.0f * (mx * nrm[i].x + my * nrm[i].y) / (6.0f + pull[i] * k4);
                o[i] = std::clamp(o[i] + 0.5f * (want - o[i]), lo[i], hi[i]);
            }
    // The flowed line, again every metre; the room moves with it.
    std::vector<Point3> line(n);
    std::vector<float> along(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        line[i] = {mPath[i].p.x + nrm[i].x * o[i], mPath[i].p.y + nrm[i].y * o[i], mPath[i].p.z};
        if (i)
            along[i] = along[i - 1] + Dist2(line[i - 1], line[i]);
    }
    std::vector<PathPoint> path;
    std::vector<float> limit;
    size_t k = 0;
    for (float s = 0.0f; s <= along.back(); s += 1.0f)
    {
        while (k + 2 < n && along[k + 1] < s)
            ++k;
        float len = along[k + 1] - along[k];
        float t = len > 1e-4f ? std::clamp((s - along[k]) / len, 0.0f, 1.0f) : 0.0f;
        const PathPoint &a = mPath[k], &b = mPath[k + 1];
        auto mix = [&](float x, float y) { return x + (y - x) * t; };
        PathPoint pt = t < 0.5f ? a : b;
        pt.p = Lerp(line[k], line[k + 1], t);
        float off = mix(o[k], o[k + 1]);
        pt.lo = mix(a.lo, b.lo) - off;
        pt.hi = mix(a.hi, b.hi) - off;
        pt.loSame = mix(a.loSame, b.loSame) - off;
        pt.hiSame = mix(a.hiSame, b.hiSame) - off;
        pt.flow = off;
        path.push_back(pt);
        limit.push_back(mLimit[std::min(k + 1, mLimit.size() - 1)]);
    }
    if (path.size() >= 2)
    {
        mPath = std::move(path);
        mLimit = std::move(limit);
    }
}

void Driver::Profile()
{
    mProfileLat = LatAccel();
    mProfileDecel = Decel();
    mProfileTurn = mCar.Reach();
    size_t n = mPath.size();
    mSpeed.assign(n, 0.0f);
    float cap = mSettings.maxSpeed;
    // The tightest the car turns at each speed (m/s): how far it turns at
    // full lock (the steering fit, or tighter as seen: CarResponse::Reach),
    // the wheel's limit there (Steer), a little held back for correcting. A
    // tighter bend is taken slower than grip alone would ask (a long truck
    // can't take a city corner at the speed its tyres would allow). (0.29
    // went by the fit alone, which held the game's cars to a crawl at city
    // corners.)
    float turnMost[71];
    for (int sp = 0; sp <= 70; ++sp)
    {
        float wheel = std::clamp(1.3f - 0.02f * float(sp), 0.45f, 1.0f);
        turnMost[sp] = 0.9f * mCar.Reach() * std::pow(wheel, mCar.steer.p) / CarResponse::SpeedTurn(float(sp));
    }
    for (size_t i = 0; i < n; ++i)
    {
        float v = std::min(cap, std::max(mLimit[i] * mSettings.laneSpeedFactor, 8.0f));
        if (i >= kBendWindow && i + kBendWindow < n)
        {
            const Point3 &a = mPath[i - kBendWindow].p, &b = mPath[i].p, &c = mPath[i + kBendWindow].p;
            float turn = std::fabs(Wrap(std::atan2(c.y - b.y, c.x - b.x) - std::atan2(b.y - a.y, b.x - a.x)));
            // The two chords point along the path w/2 either side of i.
            float k = turn / float(kBendWindow);
            if (k > 1e-3f)
            {
                v = std::min(v, std::sqrt(LatAccel() / k));
                int sp = int(std::min(v, 70.0f));
                while (sp > 3 && turnMost[sp] < k)
                    --sp;
                v = std::min(v, float(std::max(sp, 3)));
            }
        }
        mSpeed[i] = std::max(v, 3.0f);
    }
    // Stop at the destination (or where the road ends).
    mSpeed[n - 1] = 0.0f;
    // Brake in time for every slower point ahead.
    for (size_t i = n - 1; i-- > 0;)
        mSpeed[i] = std::min(mSpeed[i], std::sqrt(mSpeed[i + 1] * mSpeed[i + 1] + 2.0f * Decel()));
}

// Distance along the path (searching aFrom..aTo) of the point nearest
// aPos, and its signed offset (left positive).
bool Driver::Project(const Point3& aPos, float aFrom, float aTo, float& aS, float& aLateral) const
{
    if (mPath.size() < 2)
        return false;
    size_t from = Index(aFrom), to = std::min(Index(aTo), mPath.size() - 2);
    float best = 1e9f;
    for (size_t i = from; i <= to; ++i)
    {
        const Point3 &a = mPath[i].p, &b = mPath[i + 1].p;
        float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        float t = l2 > 1e-6f ? std::clamp(((aPos.x - a.x) * dx + (aPos.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
        float px = a.x + dx * t, py = a.y + dy * t;
        float d = std::hypot(aPos.x - px, aPos.y - py);
        if (std::fabs(a.z - aPos.z) > 5.0f)
            d += 20.0f;
        if (d < best)
        {
            best = d;
            aS = float(i) + t;
            float l = std::sqrt(l2);
            aLateral = l > 1e-4f ? (dx * (aPos.y - a.y) - dy * (aPos.x - a.x)) / l : d;
        }
    }
    return best < 1e8f;
}

// Moves mProgress to the car (searching a window around the last spot so
// a path that passes near itself doesn't make it jump). Signed lateral
// offset from the line, left positive.
bool Driver::Locate(const Point3& aPos, float& aLateral)
{
    float s;
    if (!Project(aPos, mProgress - 10.0f, mProgress + 80.0f, s, aLateral))
        return false;
    mProgress = s;
    return true;
}
} // namespace BCAI::Autodrive
