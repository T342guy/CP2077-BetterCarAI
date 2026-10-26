// The route planner of plugin 0.27 (shortest distance, the lane nearest the
// destination), kept to compare the navigator against (scan_harness nav).
#pragma once

#include "../src/Navigator.hpp"

#include <queue>

namespace OldNav
{
using namespace BCAI;
using namespace BCAI::Autodrive;

inline float LaneCost(uint16_t aFlags)
{
    float c = 1.0f;
    if (aFlags & (kLaneNoAutodrive | kLaneNoAIDriving))
        c *= 6.0f;
    if (aFlags & kLaneTrafficDisabled)
        c *= 1.3f;
    if (aFlags & kLaneGPSOnly)
        c *= 1.1f;
    return c;
}

inline std::vector<Leg> AStar(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart,
                              const std::vector<Spot>& aGoals, const Point3& aGoalPos, size_t& aExpanded,
                              size_t& aReached)
{
    std::vector<Leg> legs;
    aExpanded = 0;
    for (size_t k = 0; k < aGoals.size(); ++k)
        if (aStart.lane == aGoals[k].lane && aGoals[k].s >= aStart.s)
        {
            legs.push_back({aStart.lane, aStart.s, aGoals[k].s});
            aReached = k;
            return legs;
        }
    const uint32_t n = aGraph.Count();
    std::vector<float> g(n, 1e30f), entry(n, 0.0f), parentAt(n, 0.0f);
    std::vector<uint32_t> parent(n, UINT32_MAX);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[aStart.lane] = 0.0f;
    entry[aStart.lane] = aStart.s;
    open.push({0.0f, aStart.lane});
    bool found = false;
    while (!open.empty() && aExpanded < 400000)
    {
        auto [f, u] = open.top();
        open.pop();
        ++aExpanded;
        for (size_t k = 0; k < aGoals.size() && !found && u != aStart.lane; ++k)
            if (u == aGoals[k].lane && aGoals[k].s >= entry[u] - 0.5f)
            {
                found = true;
                aReached = k;
            }
        if (found)
            break;
        const float cost = LaneCost(aGraph.Flags(u));
        uint32_t count;
        const LaneOut* outs = aGraph.Outs(u, count);
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t v = outs[k].Lane();
            if (v >= n || !Drivable(aGraph.Flags(v)) || outs[k].at < entry[u] - 0.5f)
                continue;
            float ng = g[u] + std::max(outs[k].at - entry[u], 0.0f) * cost + 0.5f;
            if (ng >= g[v])
                continue;
            g[v] = ng;
            entry[v] = outs[k].into;
            parent[v] = u;
            parentAt[v] = outs[k].at;
            Point3 p = aGeo.Get(aGraph, v).At(outs[k].into);
            open.push({ng + Dist2(p, aGoalPos), v});
        }
    }
    if (!found)
        return legs;
    const Spot& goal = aGoals[aReached];
    float to = goal.s;
    for (uint32_t v = goal.lane; v != UINT32_MAX; v = parent[v])
    {
        legs.push_back({v, v == aStart.lane ? aStart.s : entry[v], to});
        if (v == aStart.lane)
            break;
        to = parentAt[v];
    }
    std::reverse(legs.begin(), legs.end());
    return legs;
}

// Driver::Plan's destination part in 0.27.
inline bool Plan(const LaneGraph& aGraph, GeoCache& aGeo, const Spot& aStart, const Point3& aGoal, Route& aOut)
{
    aOut = Route{};
    std::vector<Spot> goals;
    for (float r : {30.0f, 80.0f, 200.0f})
        if (!(goals = FindLanes(aGraph, aGeo, aGoal, nullptr, r)).empty())
            break;
    if (goals.empty())
        return false;
    size_t reached = 0, expanded = 0;
    aOut.legs = AStar(aGraph, aGeo, aStart, {goals.front()}, aGoal, expanded, reached);
    aOut.searched = expanded;
    if (aOut.legs.empty() && goals.size() > 1)
    {
        std::vector<Spot> others(goals.begin() + 1, goals.begin() + std::min<size_t>(goals.size(), 13));
        aOut.legs = AStar(aGraph, aGeo, aStart, others, aGoal, expanded, reached);
        aOut.searched += expanded;
        ++reached;
    }
    if (aOut.legs.empty())
    {
        aOut.why = "no route (0.27 planner)";
        return false;
    }
    aOut.end = goals[reached];
    const LaneGeo& geo = aGeo.Get(aGraph, aOut.end.lane);
    aOut.endGap = aOut.end.dist;
    aOut.endDz = geo.At(aOut.end.s).z - aGoal.z;
    Measure(aGraph, aGeo, aOut);
    return true;
}
} // namespace OldNav
