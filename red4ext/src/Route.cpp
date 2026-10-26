// The route the car is driving, as found by the path scan (v0.8, see
// docs/RESEARCH.md): behind the autodrive system,
//   route = *(*(*(autodrive + 0x540) + 0x30) + 0x3C0)
// holds DynArrays of worldTrafficLaneUIDs:
//   +0x10  0x10-byte elements: the UIDs alone, a lane repeated many times
//          (meaning of the repeats unknown)
//   +0x20  0x28-byte elements starting with a UID: one per lane, start to
//          destination, including the GPS-only junction links.
// The route's lane outlines, joined in order, are the line the AI drives.

#include "Plugin.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace BCAI
{
namespace
{
constexpr size_t kRouteLaneStride = 0x28;
constexpr uint32_t kMaxRouteLanes = 4096;

struct DynArrayHeader
{
    uintptr_t data;
    uint32_t capacity;
    uint32_t size;
};

template<typename T>
bool Read(uintptr_t aAddr, T& aOut)
{
    return aAddr && SafeRead(reinterpret_cast<const void*>(aAddr), &aOut, sizeof(T));
}

bool LooksLikePointer(uintptr_t v)
{
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull && (v & 7) == 0;
}

uintptr_t RouteObject(void* aAutodrive)
{
    uintptr_t a = 0, b = 0, route = 0;
    if (!aAutodrive || !Read(reinterpret_cast<uintptr_t>(aAutodrive) + 0x540, a) || !LooksLikePointer(a) ||
        !Read(a + 0x30, b) || !LooksLikePointer(b) || !Read(b + 0x3C0, route) || !LooksLikePointer(route))
        return 0;
    return route;
}

// The +0x20 array of the route object, raw.
bool RouteElements(uintptr_t aRoute, std::vector<char>& aOut, uint32_t& aCount)
{
    DynArrayHeader arr{};
    aCount = 0;
    if (!Read(aRoute + 0x20, arr) || !LooksLikePointer(arr.data) || arr.size == 0 || arr.size > arr.capacity ||
        arr.size > kMaxRouteLanes)
        return false;
    aOut.resize(size_t(arr.size) * kRouteLaneStride);
    if (!SafeRead(reinterpret_cast<const void*>(arr.data), aOut.data(), aOut.size()))
        return false;
    aCount = arr.size;
    return true;
}

std::string Hex(const char* aData, size_t aSize)
{
    std::string out;
    for (size_t i = 0; i < aSize; ++i)
        out += Format(i ? " %02X" : "%02X", static_cast<unsigned char>(aData[i]));
    return out;
}
} // namespace

std::vector<uint32_t> RouteLanes(void* aAutodrive)
{
    std::vector<uint32_t> lanes;
    auto& graph = Lanes();
    if (!EnsureLanes(aAutodrive))
        return lanes;
    uintptr_t route = RouteObject(aAutodrive);
    std::vector<char> raw;
    uint32_t count = 0;
    if (!graph.Loaded() || !route || !RouteElements(route, raw, count))
        return lanes;
    for (uint32_t i = 0; i < count; ++i)
        if (int lane = graph.Resolve(raw.data() + size_t(i) * kRouteLaneStride); lane >= 0)
            lanes.push_back(uint32_t(lane));
    return lanes;
}

namespace
{
float Dist3(const Point3& a, const Point3& b)
{
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
}

Point3 Lerp(const Point3& a, const Point3& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

void Append(std::vector<Point3>& aLine, std::vector<uint32_t>* aLaneOf, const Point3& p, uint32_t aLane)
{
    if (!aLine.empty() && std::hypot(p.x - aLine.back().x, p.y - aLine.back().y) < 0.2f)
        return;
    aLine.push_back(p);
    if (aLaneOf)
        aLaneOf->push_back(aLane);
}

// The part of an outline between distances s0 and s1 along it.
void AppendPart(const std::vector<Point3>& aPts, float s0, float s1, uint32_t aLane, std::vector<Point3>& aLine,
                std::vector<uint32_t>* aLaneOf)
{
    float s = 0.0f;
    for (size_t k = 0; k + 1 < aPts.size(); ++k)
    {
        float len = Dist3(aPts[k], aPts[k + 1]);
        float a = s, b = s + len;
        s = b;
        if (b < s0 || len <= 0.0f)
            continue;
        if (a > s1)
            break;
        Append(aLine, aLaneOf, a >= s0 ? aPts[k] : Lerp(aPts[k], aPts[k + 1], (s0 - a) / len), aLane);
        if (b > s1)
        {
            Append(aLine, aLaneOf, Lerp(aPts[k], aPts[k + 1], (s1 - a) / len), aLane);
            return;
        }
    }
    Append(aLine, aLaneOf, aPts.back(), aLane);
}

const LaneOut* FindLink(const LaneGraph& aGraph, uint32_t aFrom, uint32_t aTo)
{
    uint32_t n;
    const LaneOut* outs = aGraph.Outs(aFrom, n);
    for (uint32_t k = 0; k < n; ++k)
        if (outs[k].Lane() == aTo)
            return &outs[k];
    return nullptr;
}

// Points every aStep metres along a polyline.
std::vector<Point3> Resample(const std::vector<Point3>& aLine, float aStep)
{
    std::vector<Point3> out;
    if (aLine.empty())
        return out;
    out.push_back(aLine.front());
    float carry = 0.0f; // distance walked since the last output point
    for (size_t k = 0; k + 1 < aLine.size(); ++k)
    {
        float len = Dist3(aLine[k], aLine[k + 1]);
        float pos = aStep - carry;
        while (pos <= len)
        {
            out.push_back(Lerp(aLine[k], aLine[k + 1], pos / len));
            pos += aStep;
        }
        carry = len - (pos - aStep);
    }
    return out;
}

// Where along the route the car was last time, so a route that doubles back
// near itself (a jog) doesn't make the car jump ahead or back.
struct Progress
{
    uintptr_t route = 0;
    size_t points = 0;
    float along = -1.0f;
};
Progress gProgress;

// Turning sharpness along points 1 m apart: heading change between the
// 4 m before and the 4 m after each point, per metre.
constexpr int kBendWindow = 4;
float Curvature(const std::vector<Point3>& aPts, size_t i)
{
    const Point3 &a = aPts[i - kBendWindow], &b = aPts[i], &c = aPts[i + kBendWindow];
    float h1 = std::atan2(b.y - a.y, b.x - a.x), h2 = std::atan2(c.y - b.y, c.x - b.x);
    float d = h2 - h1;
    while (d > 3.14159265f)
        d -= 6.2831853f;
    while (d < -3.14159265f)
        d += 6.2831853f;
    // The chords a->b and b->c point along the route at i - w/2 and i + w/2,
    // so the heading changes by d over w metres (not 2w).
    return std::fabs(d) / float(kBendWindow);
}
} // namespace

std::vector<Point3> RouteLine(void* aAutodrive, std::vector<uint32_t>* aLaneOf)
{
    std::vector<Point3> line;
    auto& graph = Lanes();
    auto lanes = RouteLanes(aAutodrive);
    for (size_t k = 0; k < lanes.size(); ++k)
    {
        auto pts = graph.Outline(lanes[k]);
        if (pts.size() < 2)
            continue;
        float total = 0.0f;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            total += Dist3(pts[i], pts[i + 1]);
        // Where the links enter and leave this lane (lane changes and
        // crossings link mid-lane; most links are end to start).
        float s0 = 0.0f, s1 = total;
        if (k > 0)
            if (const LaneOut* in = FindLink(graph, lanes[k - 1], lanes[k]))
                s0 = std::clamp(in->into, 0.0f, total);
        if (k + 1 < lanes.size())
            if (const LaneOut* out = FindLink(graph, lanes[k], lanes[k + 1]))
                s1 = std::clamp(out->at, 0.0f, total);
        if (s1 < s0 + 0.5f)
            s0 = 0.0f, s1 = total;
        AppendPart(pts, s0, s1, lanes[k], line, aLaneOf);
    }
    return line;
}

std::vector<Point3> RouteAhead(void* aAutodrive, const RED4ext::Vector4& aPos, float aAhead)
{
    std::vector<Point3> out;
    std::vector<Point3> line = RouteLine(aAutodrive, nullptr);
    if (line.size() < 2)
        return out;
    std::vector<float> arc(line.size(), 0.0f);
    for (size_t k = 1; k < line.size(); ++k)
        arc[k] = arc[k - 1] + Dist3(line[k - 1], line[k]);

    uintptr_t route = RouteObject(aAutodrive);
    bool same = gProgress.route == route && gProgress.points == line.size() && gProgress.along >= 0.0f;
    // The car's place on the route: the nearest segment at about its height,
    // near where it was last time if the route hasn't changed.
    auto project = [&](float aFrom, float aTo, size_t& aSeg, float& aT) {
        float best = 1e9f;
        for (size_t k = 0; k + 1 < line.size(); ++k)
        {
            if (arc[k + 1] < aFrom || arc[k] > aTo)
                continue;
            const Point3 &a = line[k], &b = line[k + 1];
            float sx = b.x - a.x, sy = b.y - a.y, len2 = sx * sx + sy * sy;
            float t = len2 > 0.0f ? std::clamp(((aPos.X - a.x) * sx + (aPos.Y - a.y) * sy) / len2, 0.0f, 1.0f) : 0.0f;
            float z = a.z + (b.z - a.z) * t;
            if (std::fabs(z - aPos.Z) > 6.0f)
                continue;
            float d = std::hypot(a.x + sx * t - aPos.X, a.y + sy * t - aPos.Y);
            if (d < best)
                best = d, aSeg = k, aT = t;
        }
        return best;
    };
    size_t seg = 0;
    float t = 0.0f;
    float dist = same ? project(gProgress.along - 20.0f, gProgress.along + 120.0f, seg, t) : 1e9f;
    if (dist > 15.0f)
        dist = project(0.0f, 1e9f, seg, t);
    if (dist > 30.0f)
    {
        gProgress = {};
        return out;
    }
    gProgress = {route, line.size(), arc[seg] + (arc[seg + 1] - arc[seg]) * t};

    out.push_back(Lerp(line[seg], line[seg + 1], t));
    float left = aAhead;
    for (size_t k = seg + 1; k < line.size() && left > 0.0f; ++k)
    {
        float step = Dist3(out.back(), line[k]);
        if (step > left && step > 0.0f)
        {
            out.push_back(Lerp(out.back(), line[k], left / step));
            break;
        }
        out.push_back(line[k]);
        left -= step;
    }
    return out;
}

RoutePlan PlanRouteSpeed(void* aAutodrive, const RED4ext::Vector4& aPos, float aLatAccel, float aDecel)
{
    RoutePlan plan;
    auto ahead = RouteAhead(aAutodrive, aPos, 400.0f);
    auto pts = Resample(ahead, 1.0f);
    if (pts.size() < 2 * kBendWindow + 2)
        return plan;
    plan.valid = true;
    plan.points = uint32_t(ahead.size());
    for (size_t i = kBendWindow; i + kBendWindow < pts.size(); ++i)
    {
        float k = Curvature(pts, i);
        if (k < 1e-3f)
            continue;
        // Fast enough to round it at aLatAccel, braking at aDecel until 10 m before.
        float v = std::sqrt(aLatAccel / k);
        float allowed = std::sqrt(v * v + 2.0f * aDecel * std::max(float(i) - 10.0f, 0.0f));
        if (allowed < plan.cap)
        {
            plan.cap = allowed;
            plan.distance = float(i);
            plan.radius = 1.0f / k;
        }
    }
    return plan;
}

std::string DumpRoute(void* aAutodrive, const RED4ext::Vector4& aPos, std::filesystem::path& aOutPath)
{
    auto& graph = Lanes();
    if (!EnsureLanes(aAutodrive))
        return "lane graph not loaded";
    uintptr_t route = RouteObject(aAutodrive);
    if (!route)
        return "no route object (autodrive+0x540->+0x30->+0x3C0)";

    SYSTEMTIME now;
    GetLocalTime(&now);
    aOutPath = DumpDir() / Format("route_%04d%02d%02d_%02d%02d%02d.txt", now.wYear, now.wMonth, now.wDay, now.wHour,
                                  now.wMinute, now.wSecond);
    std::ofstream out(aOutPath);
    out << Format("# route object @%p, car at (%.1f, %.1f, %.1f)\n", reinterpret_cast<void*>(route), aPos.X, aPos.Y,
                  aPos.Z);
    char head[0x100];
    if (SafeRead(reinterpret_cast<const void*>(route), head, sizeof(head)))
    {
        out << "# route object, first 0x100 bytes:\n";
        for (size_t off = 0; off < sizeof(head); off += 16)
            out << Format("#   +0x%02zX  ", off) << Hex(head + off, 16) << "\n";
        // Every DynArray-looking header: its first elements, raw.
        for (size_t off = 0; off + 16 <= sizeof(head); off += 8)
        {
            DynArrayHeader arr;
            std::memcpy(&arr, head + off, sizeof(arr));
            if (!LooksLikePointer(arr.data) || arr.size == 0 || arr.size > arr.capacity || arr.size > 100000)
                continue;
            char first[0x80];
            if (!SafeRead(reinterpret_cast<const void*>(arr.data), first, sizeof(first)))
                continue;
            out << Format("# +0x%02zX: DynArray of %u (capacity %u), first 0x80 bytes:\n", off, arr.size, arr.capacity);
            for (size_t k = 0; k < sizeof(first); k += 16)
                out << "#     " << Hex(first + k, 16) << "\n";
        }
    }

    // The 0x10-byte UID list at +0x10, run-length summarised.
    DynArrayHeader uids{};
    if (Read(route + 0x10, uids) && LooksLikePointer(uids.data) && uids.size && uids.size <= uids.capacity &&
        uids.size <= 100000)
    {
        std::vector<char> raw(size_t(uids.size) * 0x10);
        if (SafeRead(reinterpret_cast<const void*>(uids.data), raw.data(), raw.size()))
        {
            out << Format("\n## +0x10: %u entries of 0x10 bytes (runs of the same lane)\n", uids.size);
            int runLane = -2;
            uint32_t runStart = 0;
            for (uint32_t i = 0; i <= uids.size; ++i)
            {
                int lane = i < uids.size ? graph.Resolve(raw.data() + size_t(i) * 0x10) : -3;
                if (lane == runLane)
                    continue;
                if (runLane != -2)
                    out << Format("  [%u..%u] ", runStart, i - 1)
                        << (runLane >= 0 ? graph.Describe(uint32_t(runLane), aPos.X, aPos.Y) : "unresolved") << "\n"
                        << "      first raw: " << Hex(raw.data() + size_t(runStart) * 0x10, 0x10) << "\n";
                runLane = lane;
                runStart = i;
            }
        }
    }

    // The per-lane list at +0x20.
    std::vector<char> raw;
    uint32_t count = 0;
    if (RouteElements(route, raw, count))
    {
        out << Format("\n## +0x20: %u entries of 0x28 bytes (the route, start to destination)\n", count);
        int prev = -1;
        for (uint32_t i = 0; i < count; ++i)
        {
            const char* e = raw.data() + size_t(i) * kRouteLaneStride;
            int lane = graph.Resolve(e);
            bool linked = prev >= 0 && lane >= 0 && graph.Connected(uint32_t(prev), uint32_t(lane));
            const LaneOut* link = prev >= 0 && lane >= 0 ? FindLink(graph, uint32_t(prev), uint32_t(lane)) : nullptr;
            out << Format("  [%u]%s ", i, i == 0 ? "" : linked ? Format(" linked @%.1f>%.1f", link->at, link->into).c_str() : " NOT LINKED")
                << (lane >= 0 ? graph.Describe(uint32_t(lane), aPos.X, aPos.Y) : "unresolved") << "\n"
                << "      raw: " << Hex(e, kRouteLaneStride);
            float f[6];
            std::memcpy(f, e + 0x10, sizeof(f));
            out << Format("\n      +0x10.. as floats: %g %g %g %g %g %g\n", f[0], f[1], f[2], f[3], f[4], f[5]);
            prev = lane;
        }
    }
    // Bends along the whole route sharper than a 50 km/h corner at 0.8 g:
    // junction turns, and kinks in the lane path.
    std::vector<uint32_t> laneOf;
    auto line = RouteLine(aAutodrive, &laneOf);
    out << Format("\n## route line: %zu points; bends needing under 50 km/h at 0.8 g\n", line.size());
    {
        // Resample keeping track of the lane each point came from.
        std::vector<Point3> pts;
        std::vector<uint32_t> ptLane;
        for (size_t k = 0; k + 1 < line.size(); ++k)
        {
            float len = Dist3(line[k], line[k + 1]);
            for (float s = 0.0f; s < len; s += 1.0f)
            {
                pts.push_back(Lerp(line[k], line[k + 1], s / len));
                ptLane.push_back(laneOf[k + 1]);
            }
        }
        float limit = 50.0f / 3.6f, accel = 0.8f * 9.81f;
        size_t i = kBendWindow;
        while (i + kBendWindow < pts.size())
        {
            float k = Curvature(pts, i);
            if (k < 1e-3f || std::sqrt(accel / k) >= limit)
            {
                ++i;
                continue;
            }
            // One line per bend: its sharpest point.
            size_t sharpest = i;
            while (i + kBendWindow < pts.size() && Curvature(pts, i) >= 1e-3f && std::sqrt(accel / Curvature(pts, i)) < limit)
            {
                if (Curvature(pts, i) > Curvature(pts, sharpest))
                    sharpest = i;
                ++i;
            }
            float ks = Curvature(pts, sharpest);
            out << Format("  bend at (%.1f, %.1f, %.1f): radius %.1f m -> %.0f km/h, lane ", pts[sharpest].x,
                          pts[sharpest].y, pts[sharpest].z, 1.0f / ks, std::sqrt(accel / ks) * 3.6f)
                << graph.Describe(ptLane[sharpest], aPos.X, aPos.Y) << "\n";
        }
    }
    auto plan = PlanRouteSpeed(aAutodrive, aPos, 0.8f * 9.81f, 4.0f);
    if (plan.valid)
        out << Format("\n## speed plan now (0.8 g, 4 m/s2): cap %.0f km/h for a %.1f m radius %.0f m ahead\n",
                      std::min(plan.cap, 999.0f) * 3.6f, plan.radius, plan.distance);

    auto ahead = RouteAhead(aAutodrive, aPos, 200.0f);
    out << Format("\n## route ahead of the car (200 m): %zu points\n ", ahead.size());
    for (auto& p : ahead)
        out << Format(" %.1f,%.1f,%.1f", p.x, p.y, p.z);
    out << "\n";
    std::string summary =
        Format("route: %u lanes, %zu points ahead of the car", count, ahead.size());
    Log("%s -> %ls", summary.c_str(), aOutPath.c_str());
    return summary;
}
} // namespace BCAI
