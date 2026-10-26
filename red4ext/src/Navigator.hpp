// BCAI navigation: routes over the game's traffic lane map, for Autodrive+
// and for the player's map destination.
//
// The game's GPS plans over the same lanes (each lane carries a GPS subgraph
// and strongly connected component), but it goes for the shortest way, and
// where roads are stacked it can end on the wrong one (a pin under a highway
// gets a route onto the highway). Ours plans for time (lane speeds,
// junctions, turns across traffic, U-turns), and picks the end of the route
// from all the lanes around the pin, by how far each is from it, in height
// too, and how long getting there takes.

#pragma once

#include "Plugin.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace BCAI::Autodrive
{
constexpr float kPi = 3.14159265f;

inline float Wrap(float a)
{
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return a;
}

inline float Dist2(const Point3& a, const Point3& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

inline Point3 Lerp(const Point3& a, const Point3& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

inline bool Drivable(uint16_t aFlags)
{
    return (aFlags & kLaneRoad) && !(aFlags & (kLanePavement | kLaneBlockade));
}

// A lane's outline with the distance along it at each point.
struct LaneGeo
{
    std::vector<Point3> pts;
    std::vector<float> s;
    float Length() const { return s.empty() ? 0.0f : s.back(); }

    Point3 At(float aS, Point3* aDir = nullptr) const
    {
        if (pts.size() < 2)
            return pts.empty() ? Point3{} : pts[0];
        size_t i = std::upper_bound(s.begin(), s.end(), aS) - s.begin();
        i = std::clamp<size_t>(i, 1, pts.size() - 1);
        float len = s[i] - s[i - 1];
        float t = len > 1e-4f ? std::clamp((aS - s[i - 1]) / len, 0.0f, 1.0f) : 0.0f;
        if (aDir)
        {
            float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y, l = std::hypot(dx, dy);
            *aDir = l > 1e-4f ? Point3{dx / l, dy / l, 0} : Point3{1, 0, 0};
        }
        return Lerp(pts[i - 1], pts[i], t);
    }

    // Distance along the lane of the point nearest (x, y); its horizontal
    // distance in aDist and the lane direction there in aDir.
    float Project(float x, float y, float& aDist, Point3& aDir) const
    {
        aDist = 1e9f;
        float best = 0.0f;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
            float l2 = dx * dx + dy * dy;
            float t = l2 > 1e-6f ? std::clamp(((x - pts[i].x) * dx + (y - pts[i].y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
            float px = pts[i].x + dx * t, py = pts[i].y + dy * t;
            float d = std::hypot(x - px, y - py);
            if (d < aDist)
            {
                aDist = d;
                best = s[i] + (s[i + 1] - s[i]) * t;
                float l = std::sqrt(l2);
                aDir = l > 1e-3f ? Point3{dx / l, dy / l, 0} : Point3{1, 0, 0};
            }
        }
        return best;
    }
};

class GeoCache
{
public:
    const LaneGeo& Get(const LaneGraph& aGraph, uint32_t aLane)
    {
        if (mData != aGraph.Data() || mCount != aGraph.Count())
        {
            mCache.clear();
            mData = aGraph.Data();
            mCount = aGraph.Count();
        }
        auto it = mCache.find(aLane);
        if (it != mCache.end())
            return it->second;
        LaneGeo geo;
        geo.pts = aGraph.Outline(aLane);
        geo.s.resize(geo.pts.size());
        float total = 0.0f;
        for (size_t i = 0; i < geo.pts.size(); ++i)
        {
            if (i)
                total += std::hypot(geo.pts[i].x - geo.pts[i - 1].x, geo.pts[i].y - geo.pts[i - 1].y);
            geo.s[i] = total;
        }
        return mCache.emplace(aLane, std::move(geo)).first->second;
    }

private:
    uintptr_t mData = 0;
    uint32_t mCount = 0;
    std::unordered_map<uint32_t, LaneGeo> mCache;
};

// One piece of the route: lane, from where to where along it.
struct Leg
{
    uint32_t lane;
    float from, to;
};

struct Spot
{
    uint32_t lane;
    float s;
    float dist;
};

// A turn on the route: where (metres along it), how much the road turns
// (radians, left positive) and what kind of turn it is.
struct Manoeuvre
{
    enum Kind : int
    {
        kStraight, // through a junction
        kLeft,
        kRight,
        kSlightLeft,
        kSlightRight,
        kUTurn,
    };
    float at = 0.0f;
    float angle = 0.0f;
    int kind = kStraight;
    bool junction = false; // through a junction (not a bend in the road)
    uint32_t lane = 0;     // the lane it turns onto
};
const char* ManoeuvreName(int aKind);

struct RouteOptions
{
    bool fastest = true; // plan for time; false: for distance
    bool avoidHighways = false;
};

struct Route
{
    std::vector<Leg> legs;
    std::vector<Manoeuvre> turns;
    float length = 0.0f; // m
    float time = 0.0f;   // s, the planner's estimate
    float highway = 0.0f; // m of it on highways
    // Where it ends: the lane, how far that is from the destination
    // (sideways and in height) and whether the destination's height was used.
    Spot end{};
    float endGap = 0.0f, endDz = 0.0f;
    float endWalk = -1.0f; // a pin off the roads' levels: the walk to it along the pavements (< 0: none)
    bool turnAround = false; // it starts by turning round (the route guide, a player driving)
    bool heightKnown = true;
    int candidates = 0;    // lanes around the destination it could end on
    size_t searched = 0;   // lanes the search looked at
    std::string why;       // when no route: why not
};

// The lanes under a position, going roughly along aHeading (if given), the
// best first. A destination (aHeading null) may be off the road and at
// another height (a map pin on a roof): there height only counts against a
// lane.
std::vector<Spot> FindLanes(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aPos, const Point3* aHeading,
                            float aRadius);
bool FindLane(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aPos, const Point3* aHeading, float aRadius,
              Spot& aOut);

// Turning round on the spot, as the planner counts it (s): a quick turn in
// the road, with time to wait for a gap.
constexpr float kTurnRoundTime = 15.0f;

// For a player driving at aPos facing aFwd: the route from a lane the other
// way beside the car (not a highway's, across its barrier) replaces aRoute
// (planned on from the car's lane, if aPlanned) when it saves more than
// turning round takes, rather than driving on to the next place to turn
// (round the block, or to a roundabout across town). It then starts with a
// U-turn and turnAround set. Returns whether there is a route.
bool PlanTurnRound(const LaneGraph& aGraph, GeoCache& aGeo, const Point3& aPos, const Point3& aFwd,
                   const Point3& aGoal, Route& aRoute, bool aPlanned);
// The quickest (or shortest) way from aStart to the destination aGoal.
bool PlanRoute(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, const Point3& aGoal,
               const RouteOptions& aOptions, Route& aOut);
// Tests can plan Autodrive+'s routes another way: the simulator's city scenes
// are laid out along the routes the 0.27 planner took (tests/nav_old.hpp).
inline bool (*gPlanOverride)(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, const Point3& aGoal,
                             Route& aOut) = nullptr;
// Fills in the route's length, time, highway share and turns from its legs.
void Measure(const LaneGraph& aGraph, GeoCache& aGeo, Route& aRoute);
// Without a destination: keep to the road, straight on at junctions.
std::vector<Leg> Cruise(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, float aLength);
// The legs as one line, a point every aStep metres.
std::vector<Point3> RoutePoints(const LaneGraph& aGraph, GeoCache& aGeo, const std::vector<Leg>& aLegs, float aStep);
// One line: length, time, turns, where it ends.
std::string DescribeRoute(const Route& aRoute);
} // namespace BCAI::Autodrive
