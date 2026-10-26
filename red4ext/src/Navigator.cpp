// BCAI navigation: A* over the lane map for the quickest way to a destination
// (see Navigator.hpp).
//
// Cost: seconds. Driving a lane takes its length at the speed expected on it
// (highways faster, junctions slower), times a penalty for lanes AI isn't
// meant to drive. Entering a junction costs a little (lights, giving way),
// turning across traffic more, and a U-turn a lot. The search ends at any of
// the lanes around the destination, each with a cost for how far from the
// destination it ends, so the route goes to the lane that is quickest to
// reach and nearest the pin together, not to the nearest lane come what may.

#include "Navigator.hpp"

#include <queue>

namespace BCAI::Autodrive
{
namespace
{
// Nothing on the map is driven faster than this (m/s): the search's estimate
// of the time left never overshoots.
constexpr float kTopSpeed = 27.0f;
// Turning back more than this (radians, 160 degrees) is a U-turn.
constexpr float kUTurn = 2.8f;

// How much more than its length a lane costs to drive.
float LaneCost(uint16_t aFlags)
{
    float c = 1.0f;
    // Autodrive may not use these (e.g. under overhanging railings).
    if (aFlags & (kLaneNoAutodrive | kLaneNoAIDriving))
        c *= 6.0f;
    // Junction links traffic doesn't use (roads without traffic, out in the
    // Badlands, are as quick as any).
    if ((aFlags & kLaneTrafficDisabled) && (aFlags & kLaneIntersection))
        c *= 1.3f;
    if (aFlags & kLaneGPSOnly)
        c *= 1.1f;
    return c;
}

// The speed to expect on a lane (m/s): about what Autodrive+ drives on it
// (1.5 x the map's limit, 15 in town and 17 on highways), slower through
// junctions.
float LaneSpeed(const LaneGraph& aGraph, uint32_t aLane)
{
    uint16_t flags = aGraph.Flags(aLane);
    float v = std::clamp(aGraph.MaxSpeed(aLane) * ((flags & kLaneHighway) ? 1.6f : 1.4f), 8.0f, 27.0f);
    if (flags & kLaneIntersection)
        v = std::min(v, 11.0f);
    return v;
}

// The level direction from distance s0 to s1 along a lane.
Point3 Heading(const LaneGeo& aGeo, float s0, float s1)
{
    Point3 a = aGeo.At(std::clamp(s0, 0.0f, aGeo.Length())), b = aGeo.At(std::clamp(s1, 0.0f, aGeo.Length()));
    float dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
    if (l < 0.1f)
    {
        Point3 dir;
        aGeo.At(s1, &dir);
        return dir;
    }
    return {dx / l, dy / l, 0.0f};
}

// From a to b, left positive.
float Angle(const Point3& a, const Point3& b)
{
    return std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y);
}

bool IsJunction(uint16_t aFlags)
{
    return (aFlags & kLaneIntersection) != 0;
}

// How much the road turns going from lane u (leaving it at aAt) onto v (at
// aInto): into a junction, all of the junction lane's turn; between roads,
// the kink at the link.
float LinkAngle(const LaneGraph& aGraph, GeoCache& aGeo, uint32_t u, float aAt, uint32_t v, float aInto)
{
    const LaneGeo& from = aGeo.Get(aGraph, u);
    const LaneGeo& to = aGeo.Get(aGraph, v);
    Point3 out = Heading(from, aAt - 6.0f, aAt);
    if (IsJunction(aGraph.Flags(v)))
        return Angle(out, Heading(to, to.Length() - 6.0f, to.Length()));
    return Angle(out, Heading(to, aInto, aInto + 6.0f));
}

int Classify(float aAngle)
{
    float a = std::fabs(aAngle);
    if (a > kUTurn)
        return Manoeuvre::kUTurn;
    if (a > 0.9f)
        return aAngle > 0 ? Manoeuvre::kLeft : Manoeuvre::kRight;
    if (a > 0.35f)
        return aAngle > 0 ? Manoeuvre::kSlightLeft : Manoeuvre::kSlightRight;
    return Manoeuvre::kStraight;
}

// What going from u onto v costs on top of driving them (s, or m when
// planning for distance). Traffic keeps right, so turning left crosses it.
float LinkPenalty(const LaneGraph& aGraph, GeoCache& aGeo, uint32_t u, float aAt, uint32_t v, float aInto,
                  bool aFastest)
{
    uint16_t fu = aGraph.Flags(u), fv = aGraph.Flags(v);
    float angle = LinkAngle(aGraph, aGeo, u, aAt, v, aInto);
    float a = std::fabs(angle);
    float t = 0.0f;
    if (a > kUTurn)
        t += 45.0f;
    else if (IsJunction(fv))
    {
        if (!IsJunction(fu))
            t += 3.0f;
        if (a > 0.5f)
            t += angle > 0 ? 4.0f * std::min(a / 1.57f, 1.3f) : 1.5f;
        // A hairpin is taken at walking pace.
        if (a > 1.9f)
            t += 3.0f;
    }
    if (fv & kLaneYield)
        t += 1.5f;
    // For distance, about what the time would let a car cover in town.
    return aFastest ? t : t * 3.0f;
}
} // namespace

const char* ManoeuvreName(int aKind)
{
    switch (aKind)
    {
    case Manoeuvre::kLeft:
        return "left";
    case Manoeuvre::kRight:
        return "right";
    case Manoeuvre::kSlightLeft:
        return "slight left";
    case Manoeuvre::kSlightRight:
        return "slight right";
    case Manoeuvre::kUTurn:
        return "U-turn";
    default:
        return "straight on";
    }
}

std::vector<Spot> FindLanes(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aPos, const Point3* aHeading,
                            float aRadius)
{
    std::vector<std::pair<float, Spot>> found;
    for (uint32_t lane : aGraph.Near(aPos.x, aPos.y, aRadius))
    {
        if (!Drivable(aGraph.Flags(lane)))
            continue;
        const auto& geo = aGeo.Get(aGraph, lane);
        if (geo.pts.size() < 2)
            continue;
        float d;
        Point3 dir{};
        float s = geo.Project(aPos.x, aPos.y, d, dir);
        Point3 at = geo.At(s);
        float dz = std::fabs(at.z - aPos.z);
        if (aHeading && dz > 4.0f)
            continue;
        float score = d + (aHeading ? 0.0f : 0.3f * dz);
        if (aHeading)
        {
            float dot = dir.x * aHeading->x + dir.y * aHeading->y;
            if (dot < 0.3f)
                continue;
            score += 8.0f * (1.0f - dot);
        }
        // Prefer lanes AI is meant to drive.
        if (aGraph.Flags(lane) & (kLaneNoAutodrive | kLaneNoAIDriving))
            score += 3.0f;
        found.push_back({score, {lane, s, d}});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Spot> out;
    for (const auto& f : found)
        out.push_back(f.second);
    return out;
}

bool FindLane(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aPos, const Point3* aHeading, float aRadius,
              Spot& aOut)
{
    std::vector<Spot> found = FindLanes(aGraph, aGeo, aPos, aHeading, aRadius);
    if (found.empty())
        return false;
    aOut = found.front();
    return true;
}

namespace
{
// A lane the route may end on, and what ending there costs.
struct End
{
    Spot spot;
    float gap, dz, cost;
};

// The lanes around the destination, and what ending on each costs: how far
// it is from the pin, and whether it is on the pin's level (a pin far above
// or below every road, on a roof or without a real height, counts as on the
// street right by it).
std::vector<End> Ends(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aGoal, bool aFastest, bool& aHeightKnown)
{
    std::vector<End> ends;
    for (float radius : {120.0f, 300.0f})
    {
        for (uint32_t lane : aGraph.Near(aGoal.x, aGoal.y, radius))
        {
            if (!Drivable(aGraph.Flags(lane)))
                continue;
            const auto& geo = aGeo.Get(aGraph, lane);
            if (geo.pts.size() < 2)
                continue;
            float d;
            Point3 dir{};
            float s = geo.Project(aGoal.x, aGoal.y, d, dir);
            if (d > radius)
                continue;
            ends.push_back({{lane, s, d}, d, geo.At(s).z - aGoal.z, 0.0f});
        }
        if (!ends.empty())
            break;
    }
    float nearestDz = 1e9f, nearestGap = 1e9f;
    for (const End& e : ends)
    {
        nearestDz = std::min(nearestDz, std::fabs(e.dz));
        nearestGap = std::min(nearestGap, e.gap);
    }
    // Without a height, the pin is taken to be on the lowest road right by it
    // (only the nearest ones: streets slope).
    aHeightKnown = nearestDz <= 6.0f;
    float base = 0.0f;
    if (!aHeightKnown)
    {
        base = 1e9f;
        for (const End& e : ends)
            if (e.gap <= nearestGap + 10.0f)
                base = std::min(base, e.dz);
    }
    for (End& e : ends)
    {
        // How far it is off the pin's level: a pin may be a few metres above
        // its road (a pavement, a ground floor), a road above it is another.
        float dz = e.dz - base;
        float off = std::max({dz - 1.5f, -dz - 4.0f, 0.0f});
        uint16_t flags = aGraph.Flags(e.spot.lane);
        // Seconds it is worth driving to end nearer the pin: a little for the
        // first 40 m, then a lot.
        float cost = 0.6f * std::min(e.gap, 40.0f) + 2.0f * std::max(e.gap - 40.0f, 0.0f) +
                     (off > 0.0f ? 45.0f + 4.0f * off : 0.0f);
        // A highway is no place to stop, unless the pin is on it.
        if ((flags & kLaneHighway) && !(e.gap < 8.0f && off <= 0.0f))
            cost += 25.0f;
        if (IsJunction(flags))
            cost += 4.0f;
        if (flags & (kLaneNoAutodrive | kLaneNoAIDriving))
            cost += 10.0f;
        // In metres, planning for distance: about what that time covers in town.
        e.cost = aFastest ? cost : 6.0f * cost;
    }
    return ends;
}
} // namespace

bool PlanRoute(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, const Point3& aGoal,
               const RouteOptions& aOptions, Route& aOut)
{
    aOut = Route{};
    const bool fastest = aOptions.fastest;
    std::vector<End> ends = Ends(aGraph, aGeo, aGoal, fastest, aOut.heightKnown);
    aOut.candidates = int(ends.size());
    if (ends.empty())
    {
        aOut.why = "no drivable lane near the destination";
        return false;
    }
    const uint32_t n = aGraph.Count();
    std::unordered_map<uint32_t, std::vector<int>> endsOn;
    for (int k = 0; k < int(ends.size()); ++k)
        endsOn[ends[k].spot.lane].push_back(k);

    // Driving lane u from s0 to s1.
    auto drive = [&](uint32_t u, float s0, float s1)
    {
        float len = std::max(s1 - s0, 0.0f) * LaneCost(aGraph.Flags(u));
        if (aOptions.avoidHighways && (aGraph.Flags(u) & kLaneHighway))
            len *= 3.0f;
        return fastest ? len / LaneSpeed(aGraph, u) : len;
    };
    auto estimate = [&](const Point3& p) { return fastest ? Dist2(p, aGoal) / kTopSpeed : Dist2(p, aGoal); };
    // The estimate has to stay under the real cost left: ending away from the
    // pin is never cheaper than driving there (0.6 s/m against 1/27).

    std::vector<float> g(n, 1e30f), entry(n, 0.0f), parentAt(n, 0.0f);
    std::vector<uint32_t> parent(n, UINT32_MAX);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[aStart.lane] = 0.0f;
    entry[aStart.lane] = aStart.s;
    open.push({estimate(aGeo.Get(aGraph, aStart.lane).At(aStart.s)), aStart.lane});
    float best = 1e30f;
    int bestEnd = -1;
    size_t expanded = 0;
    while (!open.empty() && expanded < 400000)
    {
        auto [f, u] = open.top();
        open.pop();
        if (f >= best)
            break;
        ++expanded;
        if (auto it = endsOn.find(u); it != endsOn.end())
            for (int k : it->second)
            {
                const End& e = ends[k];
                if (e.spot.s < entry[u] - (u == aStart.lane ? 0.0f : 0.5f))
                    continue;
                float total = g[u] + drive(u, entry[u], e.spot.s) + e.cost;
                if (total < best)
                {
                    best = total;
                    bestEnd = k;
                }
            }
        uint32_t count;
        const LaneOut* outs = aGraph.Outs(u, count);
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t v = outs[k].Lane();
            if (v >= n || v == aStart.lane || !Drivable(aGraph.Flags(v)) || outs[k].at < entry[u] - 0.5f)
                continue;
            float ng = g[u] + drive(u, entry[u], outs[k].at) +
                       LinkPenalty(aGraph, aGeo, u, outs[k].at, v, outs[k].into, fastest);
            if (ng >= g[v])
                continue;
            g[v] = ng;
            entry[v] = outs[k].into;
            parent[v] = u;
            parentAt[v] = outs[k].at;
            open.push({ng + estimate(aGeo.Get(aGraph, v).At(outs[k].into)), v});
        }
    }
    aOut.searched = expanded;
    if (bestEnd < 0)
    {
        aOut.why = Format("no route from lane %u to any of the %zu lanes near the destination (%zu lanes searched)",
                          aStart.lane, ends.size(), expanded);
        return false;
    }
    const End& end = ends[bestEnd];
    float to = end.spot.s;
    for (uint32_t v = end.spot.lane; v != UINT32_MAX; v = parent[v])
    {
        aOut.legs.push_back({v, v == aStart.lane ? aStart.s : entry[v], to});
        if (v == aStart.lane)
            break;
        to = parentAt[v];
    }
    std::reverse(aOut.legs.begin(), aOut.legs.end());
    aOut.end = end.spot;
    aOut.endGap = end.gap;
    aOut.endDz = end.dz;

    Measure(aGraph, aGeo, aOut);
    return true;
}

void Measure(const LaneGraph& aGraph, GeoCache& aGeo, Route& aRoute)
{
    aRoute.turns.clear();
    aRoute.length = aRoute.time = aRoute.highway = 0.0f;
    // A junction (one or more junction lanes in a row) or a sharp kink
    // between roads at a time.
    const auto& legs = aRoute.legs;
    float at = 0.0f;
    for (size_t i = 0; i < legs.size(); ++i)
    {
        const Leg& leg = legs[i];
        float len = std::max(leg.to - leg.from, 0.0f);
        aRoute.time += len * LaneCost(aGraph.Flags(leg.lane)) / LaneSpeed(aGraph, leg.lane);
        if (aGraph.Flags(leg.lane) & kLaneHighway)
            aRoute.highway += len;
        if (i > 0)
        {
            const Leg& prev = legs[i - 1];
            aRoute.time += LinkPenalty(aGraph, aGeo, prev.lane, prev.to, leg.lane, leg.from, true);
            bool junction = IsJunction(aGraph.Flags(leg.lane));
            if (!(junction && IsJunction(aGraph.Flags(prev.lane))))
            {
                // Through to the first road lane after the junction.
                size_t j = i;
                while (junction && j + 1 < legs.size() && IsJunction(aGraph.Flags(legs[j].lane)))
                    ++j;
                const LaneGeo& before = aGeo.Get(aGraph, prev.lane);
                const LaneGeo& after = aGeo.Get(aGraph, legs[j].lane);
                Point3 in = Heading(before, prev.to - 6.0f, prev.to);
                Point3 out = IsJunction(aGraph.Flags(legs[j].lane))
                                 ? Heading(after, legs[j].to - 6.0f, legs[j].to)
                                 : Heading(after, legs[j].from, legs[j].from + 6.0f);
                float angle = Angle(in, out);
                int kind = Classify(angle);
                if (junction || kind == Manoeuvre::kUTurn)
                    aRoute.turns.push_back({at, angle, kind, junction, legs[j].lane});
            }
        }
        at += len;
    }
    aRoute.length = at;
}

std::vector<Leg> Cruise(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, float aLength)
{
    std::vector<Leg> legs;
    uint32_t lane = aStart.lane;
    float from = aStart.s, total = 0.0f;
    for (int guard = 0; guard < 200 && total < aLength; ++guard)
    {
        const auto& geo = aGeo.Get(aGraph, lane);
        uint32_t count;
        const LaneOut* outs = aGraph.Outs(lane, count);
        // The link out of the lane's end that turns least.
        Point3 endDir;
        geo.At(geo.Length(), &endDir);
        int best = -1;
        float bestScore = 1e9f;
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t v = outs[k].Lane();
            if (v >= aGraph.Count() || !Drivable(aGraph.Flags(v)) || outs[k].at < geo.Length() - 1.0f)
                continue;
            const auto& next = aGeo.Get(aGraph, v);
            Point3 dir;
            next.At(std::min(outs[k].into + 5.0f, next.Length()), &dir);
            float score = 1.0f - (dir.x * endDir.x + dir.y * endDir.y);
            score += 0.5f * (LaneCost(aGraph.Flags(v)) - 1.0f);
            if (score < bestScore)
            {
                bestScore = score;
                best = int(k);
            }
        }
        float to = best >= 0 ? outs[best].at : geo.Length();
        legs.push_back({lane, from, to});
        total += std::max(to - from, 0.0f);
        if (best < 0)
            break;
        lane = outs[best].Lane();
        from = outs[best].into;
    }
    return legs;
}

std::vector<Point3> RoutePoints(const LaneGraph& aGraph, GeoCache& aGeo, const std::vector<Leg>& aLegs, float aStep)
{
    std::vector<Point3> raw;
    auto add = [&](const Point3& p)
    {
        if (raw.empty() || Dist2(raw.back(), p) >= 0.2f)
            raw.push_back(p);
    };
    for (const Leg& leg : aLegs)
    {
        const LaneGeo& geo = aGeo.Get(aGraph, leg.lane);
        if (geo.pts.size() < 2 || leg.to <= leg.from)
            continue;
        add(geo.At(leg.from));
        for (size_t i = 0; i < geo.pts.size(); ++i)
            if (geo.s[i] > leg.from && geo.s[i] < leg.to)
                add(geo.pts[i]);
        add(geo.At(leg.to));
    }
    if (raw.size() < 2 || aStep <= 0.0f)
        return raw;
    std::vector<Point3> out{raw.front()};
    float carry = 0.0f; // metres since the last point
    for (size_t i = 1; i < raw.size(); ++i)
    {
        float len = Dist2(raw[i - 1], raw[i]);
        float s = aStep - carry;
        for (; s <= len; s += aStep)
            out.push_back(Lerp(raw[i - 1], raw[i], s / len));
        carry = len - (s - aStep);
    }
    if (Dist2(out.back(), raw.back()) > 0.1f)
        out.push_back(raw.back());
    return out;
}

std::string DescribeRoute(const Route& aRoute)
{
    int counts[6] = {};
    for (const Manoeuvre& m : aRoute.turns)
        ++counts[m.kind];
    int junctions = 0;
    for (const Manoeuvre& m : aRoute.turns)
        junctions += m.junction;
    std::string turns;
    for (int kind : {Manoeuvre::kLeft, Manoeuvre::kRight, Manoeuvre::kSlightLeft, Manoeuvre::kSlightRight,
                     Manoeuvre::kUTurn})
        if (counts[kind])
            turns += Format("%s%d %s", turns.empty() ? "" : ", ", counts[kind], ManoeuvreName(kind));
    std::string end = Format("ends %.0f m from the destination", aRoute.endGap);
    if (std::fabs(aRoute.endDz) >= 2.0f)
        end += aRoute.heightKnown ? Format(", %.0f m %s it", std::fabs(aRoute.endDz), aRoute.endDz > 0 ? "above" : "below")
                                  : " (its height unknown: the street level)";
    return Format("%.0f m, about %d min %02d s, %d junctions%s%s%s, %s", aRoute.length, int(aRoute.time) / 60,
                  int(aRoute.time) % 60, junctions, turns.empty() ? "" : " (", turns.c_str(), turns.empty() ? "" : ")",
                  aRoute.highway > 0.0f ? Format("%s; %.0f%% highway", end.c_str(),
                                                 100.0f * aRoute.highway / std::max(aRoute.length, 1.0f))
                                              .c_str()
                                        : end.c_str());
}
} // namespace BCAI::Autodrive
