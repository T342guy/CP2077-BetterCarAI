// The route to the map destination for the player (BCAI_Nav* natives):
// planned with the navigator (Navigator.hpp) whether Autodrive+ drives or not,
// followed as the player moves, for the overlay's route line and turn hints.
// Also the research scan for where the game's GPS keeps its own route.

#include "Navigator.hpp"

#include <RED4ext/GameEngine.hpp>

namespace BCAI
{
using namespace Autodrive;

namespace
{
struct Guide
{
    Route route;
    std::vector<Point3> points; // every metre
    Point3 goal{};
    size_t at = 0;    // the point the player is at
    float off = 0.0f; // how far the player is from the line (m)
    bool valid = false;
};
Guide gGuide;
GeoCache gGuideGeo;

// The point of the line nearest aPos, searched from a little behind where the
// player was to well ahead (a route passing near itself can't make it jump).
void Follow(const Point3& aPos)
{
    auto& pts = gGuide.points;
    size_t from = gGuide.at > 40 ? gGuide.at - 40 : 0;
    size_t to = std::min(pts.size(), gGuide.at + 400);
    float best = 1e9f;
    size_t bestI = gGuide.at;
    for (size_t i = from; i < to; ++i)
        if (float d = Dist2(pts[i], aPos) + 0.5f * std::fabs(pts[i].z - aPos.z); d < best)
            best = d, bestI = i;
    gGuide.at = bestI;
    gGuide.off = best;
}
} // namespace

std::string NavPlan(void* aAutodrive, const Point3& aPos, const Point3* aFwd, const Point3& aGoal)
{
    gGuide.valid = false;
    auto& graph = Lanes();
    if (!EnsureLanes(aAutodrive) || !graph.Loaded())
        return "the lane map isn't loaded";
    // In a car: the lane it is on, going its way. On foot: any lane nearby.
    Spot start{};
    if (!(aFwd && FindLane(graph, gGuideGeo, aPos, aFwd, 15.0f, start)))
    {
        std::vector<Spot> nearby = FindLanes(graph, gGuideGeo, aPos, nullptr, 60.0f);
        if (nearby.empty())
            return "no road near you";
        start = nearby.front();
    }
    Route route;
    if (!PlanRoute(graph, gGuideGeo, start, aGoal, RouteOptions{}, route))
        return route.why;
    gGuide.route = std::move(route);
    gGuide.points = RoutePoints(graph, gGuideGeo, gGuide.route.legs, 1.0f);
    gGuide.goal = aGoal;
    gGuide.at = 0;
    gGuide.off = 0.0f;
    gGuide.valid = gGuide.points.size() >= 2;
    if (!gGuide.valid)
        return "route too short";
    Log("route to (%.0f, %.0f, %.0f): %s", aGoal.x, aGoal.y, aGoal.z, DescribeRoute(gGuide.route).c_str());
    return "route: " + DescribeRoute(gGuide.route);
}

std::vector<Point3> NavAhead(const Point3& aPos, float aAhead, float aStep)
{
    std::vector<Point3> out;
    if (!gGuide.valid)
        return out;
    Follow(aPos);
    size_t step = std::max<size_t>(1, size_t(aStep));
    size_t end = std::min(gGuide.points.size(), gGuide.at + size_t(aAhead) + 1);
    for (size_t i = gGuide.at; i < end; i += step)
        out.push_back(gGuide.points[i]);
    if (end > gGuide.at && (end - 1 - gGuide.at) % step)
        out.push_back(gGuide.points[end - 1]);
    return out;
}

std::string NavStatus(const Point3& aPos)
{
    if (!gGuide.valid)
        return "";
    Follow(aPos);
    const Route& route = gGuide.route;
    // The points are a metre apart, so the index is the distance along it.
    float at = float(gGuide.at), length = float(gGuide.points.size() - 1);
    float left = std::max(length - at, 0.0f);
    std::string out = Format("left=%.0f;time=%.0f;off=%.1f", left, route.time * left / std::max(length, 1.0f),
                             gGuide.off);
    // The next turn worth telling (straight on through a junction isn't).
    for (const Manoeuvre& m : route.turns)
        if (m.at >= at - 2.0f && m.kind != Manoeuvre::kStraight)
        {
            out += Format(";turn=%s;in=%.0f;angle=%.0f", ManoeuvreName(m.kind), std::max(m.at - at, 0.0f),
                          m.angle * 180.0f / kPi);
            break;
        }
    return out;
}

void NavClear()
{
    gGuide.valid = false;
    gGuide.points.clear();
}

std::string ScanGPS(void* aAutodrive, const RED4ext::Vector4& aOrigin, const RED4ext::Vector4& aHeading,
                    std::filesystem::path& aOutPath)
{
    // The route recogniser needs the lane map.
    EnsureLanes(aAutodrive);
    auto* engine = RED4ext::CGameEngine::Get();
    auto* game = engine && engine->framework ? engine->framework->gameInstance : nullptr;
    auto* type = RED4ext::CRTTISystem::Get()->GetClass("gamegpsGPSSystem");
    if (!game || !type)
        return "no game instance or GPS system type";
    RED4ext::IScriptable* gps = game->GetSystem(type);
    if (!gps)
        return "no GPS system";
    Log("GPS system at %p, class size 0x%X", static_cast<void*>(gps), type->size);
    return ScanForPath(gps, type->size, "gps", aOrigin, aHeading, 4, aOutPath);
}
} // namespace BCAI
