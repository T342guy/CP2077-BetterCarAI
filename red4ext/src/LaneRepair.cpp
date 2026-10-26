// Repairs junctions whose lanes are wired to the wrong side of the road.
//
// The 2.5 lane map has junctions (all on roads with traffic disabled, so NPC
// traffic never shows it) where a road lane ends with no links while the
// junction lanes that should continue it start one lane width (~5 m) away,
// at the end of the opposite lane, and have no links into them. Routes still
// pass through (the GPS/autodrive planner bridges the gap), so the car is
// sent sideways across the road and back at the junction mouth: the steering
// snap at the T-junction.
//
// The fix: for each such pair, the junction lane's end is moved onto the road
// lane's end and the junction lane is redrawn as a cubic curve that keeps its
// direction at both ends (same number of points; a two-point lane stays
// straight). Its running lengths, length, area polygon and out-link positions
// are updated to match. Road lanes are never changed. Originals are kept so
// the repair can be undone. Written through WriteProcessMemory, which fails
// cleanly instead of faulting.
//
// Route planning follows the links, not the shapes, and these junctions have
// none to or from the road (the overlay shows the road lane as a dead end),
// so the planner routes around them. So the missing links are added too:
// an out-link on the road lane (or junction lane) and, if its layout can be
// learned from existing links, the matching in-link. The arrays are grown
// with the engine's own DynArray realloc (through RED4ext's DynArray), so
// the game frees them normally. Undo shrinks them back.

#include "Plugin.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>

namespace BCAI
{
namespace
{
using namespace LaneLayout;

struct DynArrayHeader
{
    uintptr_t data;
    uint32_t capacity;
    uint32_t size;
};

struct Vec2
{
    float x, y;
};

bool SafeWrite(uintptr_t aDst, const void* aSrc, size_t aSize)
{
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(aDst), aSrc, aSize, &written) &&
           written == aSize;
}

template<typename T>
bool ReadArray(const DynArrayHeader& aArr, std::vector<T>& aOut, uint32_t aMax = 4096)
{
    aOut.clear();
    if (aArr.size == 0 || aArr.size > aMax || aArr.size > aArr.capacity)
        return false;
    aOut.resize(aArr.size);
    return SafeRead(reinterpret_cast<const void*>(aArr.data), aOut.data(), aOut.size() * sizeof(T));
}

float Dist2D(const Point3& a, const Point3& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

// Unit direction over the first (aStart) or last ~2 m of an outline.
Vec2 EndDirection(const std::vector<Point3>& aPts, bool aStart)
{
    const Point3& end = aStart ? aPts.front() : aPts.back();
    Point3 other = aStart ? aPts.back() : aPts.front();
    for (size_t k = 1; k < aPts.size(); ++k)
    {
        const Point3& p = aStart ? aPts[k] : aPts[aPts.size() - 1 - k];
        if (Dist2D(end, p) > 2.0f)
        {
            other = p;
            break;
        }
    }
    float dx = aStart ? other.x - end.x : end.x - other.x, dy = aStart ? other.y - end.y : end.y - other.y;
    float n = std::hypot(dx, dy);
    return n > 0.0f ? Vec2{dx / n, dy / n} : Vec2{0.0f, 0.0f};
}

// aPts redrawn as a cubic from p0 to p3 keeping the end directions, evenly
// spaced, same number of points.
std::vector<Point3> Redraw(const std::vector<Point3>& aPts, const Point3& p0, const Point3& p3)
{
    size_t n = aPts.size();
    if (n <= 2)
        return {p0, p3};
    Vec2 t0 = EndDirection(aPts, true), t1 = EndDirection(aPts, false);
    float k = Dist2D(p0, p3) * 0.45f;
    Vec2 p1{p0.x + t0.x * k, p0.y + t0.y * k}, p2{p3.x - t1.x * k, p3.y - t1.y * k};
    constexpr int kDense = 200;
    std::vector<Point3> dense;
    std::vector<float> arc;
    for (int j = 0; j <= kDense; ++j)
    {
        float u = float(j) / kDense, a = (1 - u) * (1 - u) * (1 - u), b = 3 * (1 - u) * (1 - u) * u,
              c = 3 * (1 - u) * u * u, d = u * u * u;
        dense.push_back({a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y,
                         p0.z + (p3.z - p0.z) * u});
        arc.push_back(j ? arc.back() + Dist2D(dense[j - 1], dense[j]) : 0.0f);
    }
    std::vector<Point3> out;
    size_t j = 1;
    for (size_t i = 0; i < n; ++i)
    {
        float s = arc.back() * float(i) / float(n - 1);
        while (j + 1 < dense.size() && arc[j] < s)
            ++j;
        float span = arc[j] - arc[j - 1];
        float f = span > 0.0f ? std::clamp((s - arc[j - 1]) / span, 0.0f, 1.0f) : 0.0f;
        out.push_back({dense[j - 1].x + (dense[j].x - dense[j - 1].x) * f,
                       dense[j - 1].y + (dense[j].y - dense[j - 1].y) * f,
                       dense[j - 1].z + (dense[j].z - dense[j - 1].z) * f});
    }
    out.front() = p0;
    out.back() = p3;
    return out;
}

struct Fix
{
    uint32_t lane;
    bool start;      // move the start (else the end)
    Point3 target;
    uint32_t partner; // the road lane it now joins
};

// What one repaired lane looked like before, to undo it.
struct Original
{
    uint32_t lane;
    std::vector<Point3> outline;
    std::vector<float> accumulated;
    std::vector<Vec2> polygon;
    std::vector<LaneOut> outs;
    float length;
};

// A link array we grew, to undo.
struct AddedLink
{
    uintptr_t header; // the lane's DynArray header
    uint32_t oldSize;
};

struct State
{
    uintptr_t table = 0; // lane table the repair was applied to
    std::vector<Original> originals;
    std::vector<AddedLink> added;
    std::vector<std::string> log;
};
State gState;

constexpr float kMinGap = 1.5f;
constexpr float kMaxGap = 10.0f;
constexpr float kMaxJunctionLane = 80.0f;

std::vector<Fix> FindFixes(const LaneGraph& aLanes)
{
    uint32_t count = aLanes.Count();
    std::vector<uint32_t> ins(count, 0);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t n;
        const LaneOut* outs = aLanes.Outs(i, n);
        for (uint32_t k = 0; k < n; ++k)
            ++ins[outs[k].Lane()];
    }
    // Outlines of drivable lanes, and their starts on a 10 m grid.
    std::vector<std::vector<Point3>> outline(count);
    std::map<std::pair<int, int>, std::vector<uint32_t>> starts;
    auto cell = [](const Point3& p) { return std::make_pair(int(std::floor(p.x / 10.0f)), int(std::floor(p.y / 10.0f))); };
    for (uint32_t i = 0; i < count; ++i)
    {
        if (aLanes.Flags(i) & kLanePavement)
            continue;
        outline[i] = aLanes.Outline(i);
        if (outline[i].size() >= 2)
            starts[cell(outline[i].front())].push_back(i);
    }
    auto startsNear = [&](const Point3& p, float r) {
        std::vector<std::pair<float, uint32_t>> out;
        auto [cx, cy] = cell(p);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                auto it = starts.find({cx + dx, cy + dy});
                if (it == starts.end())
                    continue;
                for (uint32_t i : it->second)
                {
                    const Point3& q = outline[i].front();
                    float d = Dist2D(p, q);
                    if (d < r && std::fabs(q.z - p.z) < 3.0f)
                        out.push_back({d, i});
                }
            }
        return out;
    };

    std::vector<Fix> fixes;
    std::set<std::pair<uint32_t, bool>> seen;
    for (uint32_t a = 0; a < count; ++a)
    {
        uint32_t nOuts;
        aLanes.Outs(a, nOuts);
        if (nOuts || outline[a].size() < 2)
            continue;
        const Point3& end = outline[a].back();
        auto nearby = startsNear(end, kMaxGap);
        if (std::any_of(nearby.begin(), nearby.end(), [](auto& c) { return c.first < 0.5f; }))
            continue; // something starts right here: not a broken end
        Vec2 dir = EndDirection(outline[a], false);
        for (auto [gap, c] : nearby)
        {
            if (gap < kMinGap || ins[c] || c == a)
                continue;
            Vec2 cdir = EndDirection(outline[c], true);
            if (cdir.x * dir.x + cdir.y * dir.y <= 0.5f)
                continue; // not continuing the same way (e.g. the opposite lane at a road end)
            // Only junction lanes are redrawn; road lanes keep their shape.
            if ((aLanes.Flags(c) & kLaneIntersection) && aLanes.Length(c) < kMaxJunctionLane)
            {
                if (seen.insert({c, true}).second)
                    fixes.push_back({c, true, end, a});
            }
            else if ((aLanes.Flags(a) & kLaneIntersection) && aLanes.Length(a) < kMaxJunctionLane)
            {
                if (seen.insert({a, false}).second)
                    fixes.push_back({a, false, outline[c].front(), c});
            }
        }
    }
    return fixes;
}

// Writes a lane's outline, running lengths, length, polygon and out-link
// positions for a new outline. aOld is the current outline.
bool WriteLane(const LaneGraph& aLanes, uint32_t aLane, const std::vector<Point3>& aOld,
               const std::vector<Point3>& aNew, Original* aSave)
{
    uintptr_t lane = aLanes.Data() + size_t(aLane) * kSize;
    auto outlineArr = aLanes.Field<DynArrayHeader>(aLane, kOutline);
    auto accArr = aLanes.Field<DynArrayHeader>(aLane, kAccumulated);
    auto polyArr = aLanes.Field<DynArrayHeader>(aLane, kPolygon);
    auto outsArr = aLanes.Field<DynArrayHeader>(aLane, kOutLanes);
    if (outlineArr.size != aNew.size() || aOld.size() != aNew.size() || accArr.size + 1 != aNew.size())
        return false;
    std::vector<float> acc;
    std::vector<Vec2> polygon;
    std::vector<LaneOut> outs;
    ReadArray(accArr, acc);
    ReadArray(polyArr, polygon, 16384);
    ReadArray(outsArr, outs, 64);
    float oldLength = aLanes.Length(aLane);
    if (aSave)
        *aSave = {aLane, aOld, acc, polygon, outs, oldLength};

    std::vector<float> newAcc;
    float total = 0.0f;
    for (size_t k = 1; k < aNew.size(); ++k)
    {
        total += std::sqrt((aNew[k].x - aNew[k - 1].x) * (aNew[k].x - aNew[k - 1].x) +
                           (aNew[k].y - aNew[k - 1].y) * (aNew[k].y - aNew[k - 1].y) +
                           (aNew[k].z - aNew[k - 1].z) * (aNew[k].z - aNew[k - 1].z));
        newAcc.push_back(total);
    }
    // Polygon vertices move with their nearest outline point.
    for (auto& v : polygon)
    {
        size_t best = 0;
        float bestD = 1e9f;
        for (size_t k = 0; k < aOld.size(); ++k)
            if (float d = std::hypot(aOld[k].x - v.x, aOld[k].y - v.y); d < bestD)
                bestD = d, best = k;
        v.x += aNew[best].x - aOld[best].x;
        v.y += aNew[best].y - aOld[best].y;
    }
    // Links that leave at the end of the lane leave at its new end.
    for (auto& o : outs)
        if (std::fabs(o.at - oldLength) < 0.05f)
            o.at = total;

    bool ok = SafeWrite(outlineArr.data, aNew.data(), aNew.size() * sizeof(Point3)) &&
              SafeWrite(accArr.data, newAcc.data(), newAcc.size() * sizeof(float)) &&
              SafeWrite(lane + kLength, &total, sizeof(float));
    if (ok && !polygon.empty() && polyArr.size == polygon.size())
        ok = SafeWrite(polyArr.data, polygon.data(), polygon.size() * sizeof(Vec2));
    if (ok && !outs.empty() && outsArr.size == outs.size())
        ok = SafeWrite(outsArr.data, outs.data(), outs.size() * sizeof(LaneOut));
    return ok;
}

// worldTrafficConnectivityInLane is 0x10 bytes with no RTTI fields. Its
// layout is learned from lanes with exactly one in-link, comparing that
// entry with the one out-link that points at the lane: which word holds the
// source lane's index (low 16 bits), which hold the out-link's `at` and
// `into`, and which are constant.
struct InLayout
{
    bool valid = false;
    int idWord = -1;
    uint32_t idHigh = 0;
    int atWord = -1;
    int intoWord = -1;
    uint32_t constant[4] = {};
    std::string description;
};

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

InLayout LearnInLayout(const LaneGraph& aLanes)
{
    InLayout layout;
    uint32_t count = aLanes.Count();
    // The one predecessor of each lane with a single in-link.
    std::vector<int64_t> pred(count, -1);
    std::vector<LaneOut> predLink(count);
    for (uint32_t x = 0; x < count; ++x)
    {
        uint32_t n;
        const LaneOut* outs = aLanes.Outs(x, n);
        for (uint32_t k = 0; k < n; ++k)
            if (aLanes.InCount(outs[k].Lane()) == 1)
                pred[outs[k].Lane()] = x, predLink[outs[k].Lane()] = outs[k];
    }
    uint32_t samples = 0, idHit[4] = {}, atHit[4] = {}, intoHit[4] = {};
    uint32_t first[4] = {}, same[4] = {};
    std::map<uint32_t, uint32_t> highCounts[4];
    std::string examples;
    for (uint32_t y = 0; y < count && samples < 3000; ++y)
    {
        if (pred[y] < 0)
            continue;
        auto ins = aLanes.Field<DynArrayHeader>(y, kInLanes);
        uint32_t w[4];
        if (ins.size != 1 || !SafeRead(reinterpret_cast<const void*>(ins.data), w, sizeof(w)))
            continue;
        const LaneOut& o = predLink[y];
        for (int i = 0; i < 4; ++i)
        {
            idHit[i] += (w[i] & 0xFFFF) == uint32_t(pred[y]);
            highCounts[i][w[i] >> 16]++;
            atHit[i] += w[i] == Bits(o.at);
            intoHit[i] += w[i] == Bits(o.into);
            if (samples == 0)
                first[i] = w[i];
            same[i] += w[i] == first[i];
        }
        if (samples < 3)
            examples += Format(" [#%lld->#%u: in %08X %08X %08X %08X, out %08X at %.2f into %.2f]", pred[y], y, w[0],
                               w[1], w[2], w[3], o.id, o.at, o.into);
        ++samples;
    }
    layout.description = Format("in-link layout from %u lanes:", samples);
    if (samples < 50)
    {
        layout.description += " too few samples";
        return layout;
    }
    auto good = [&](uint32_t hits) { return hits * 100 >= samples * 98; };
    bool used[4] = {};
    for (int i = 0; i < 4 && layout.idWord < 0; ++i)
        if (good(idHit[i]))
            layout.idWord = i, used[i] = true;
    for (int i = 0; i < 4 && layout.atWord < 0; ++i)
        if (!used[i] && good(atHit[i]))
            layout.atWord = i, used[i] = true;
    for (int i = 0; i < 4 && layout.intoWord < 0; ++i)
        if (!used[i] && good(intoHit[i]))
            layout.intoWord = i, used[i] = true;
    bool ok = layout.idWord >= 0;
    for (int i = 0; i < 4; ++i)
    {
        if (used[i])
            continue;
        if (good(same[i]))
            layout.constant[i] = first[i];
        else
            ok = false; // a word we can't explain
    }
    if (layout.idWord >= 0)
    {
        uint32_t best = 0;
        for (auto& [high, n] : highCounts[layout.idWord])
            if (n > best)
                best = n, layout.idHigh = high;
    }
    layout.valid = ok;
    layout.description += Format(" id word %d (high bits %04X), at word %d, into word %d, %s;%s", layout.idWord,
                                 layout.idHigh, layout.atWord, layout.intoWord,
                                 ok ? "consistent" : "NOT understood, in-links skipped", examples.c_str());
    return layout;
}

// The most common high half of out-link ids (0xA4FF in 2.5).
uint32_t OutIdHigh(const LaneGraph& aLanes)
{
    std::map<uint32_t, uint32_t> counts;
    for (uint32_t x = 0; x < aLanes.Count(); ++x)
    {
        uint32_t n;
        const LaneOut* outs = aLanes.Outs(x, n);
        for (uint32_t k = 0; k < n; ++k)
            counts[outs[k].id >> 16]++;
    }
    uint32_t best = 0, high = 0xA4FF;
    for (auto& [h, n] : counts)
        if (n > best)
            best = n, high = h;
    return high;
}

// Appends an element to a lane's link array through the engine's realloc.
template<typename T>
void Append(const LaneGraph& aLanes, uint32_t aLane, size_t aOffset, const T& aValue)
{
    uintptr_t header = aLanes.Data() + size_t(aLane) * kSize + aOffset;
    auto* arr = reinterpret_cast<RED4ext::DynArray<T>*>(header);
    gState.added.push_back({header, arr->size()});
    if (gArrayAppendOverride)
        gArrayAppendOverride(header, &aValue, sizeof(T));
    else
        arr->PushBack(aValue);
}

struct InEntry
{
    uint32_t w[4];
};

// Links aFrom -> aTo (out on aFrom, in on aTo) unless already linked.
std::string AddLink(const LaneGraph& aLanes, const InLayout& aIn, uint32_t aOutHigh, uint32_t aFrom, uint32_t aTo)
{
    if (aLanes.Connected(aFrom, aTo))
        return Format("#%u -> #%u already linked", aFrom, aTo);
    float at = aLanes.Length(aFrom), into = 0.0f;
    Append(aLanes, aFrom, kOutLanes, LaneOut{(aOutHigh << 16) | aTo, at, into});
    if (!aIn.valid)
        return Format("linked #%u -> #%u (out-link only)", aFrom, aTo);
    InEntry e{};
    for (int i = 0; i < 4; ++i)
        e.w[i] = aIn.constant[i];
    e.w[aIn.idWord] = (aIn.idHigh << 16) | aFrom;
    if (aIn.atWord >= 0)
        e.w[aIn.atWord] = Bits(at);
    if (aIn.intoWord >= 0)
        e.w[aIn.intoWord] = Bits(into);
    Append(aLanes, aTo, kInLanes, e);
    return Format("linked #%u -> #%u", aFrom, aTo);
}

bool Restore(const LaneGraph& aLanes, const Original& aOrig)
{
    uintptr_t lane = aLanes.Data() + size_t(aOrig.lane) * kSize;
    auto outlineArr = aLanes.Field<DynArrayHeader>(aOrig.lane, kOutline);
    auto accArr = aLanes.Field<DynArrayHeader>(aOrig.lane, kAccumulated);
    auto polyArr = aLanes.Field<DynArrayHeader>(aOrig.lane, kPolygon);
    auto outsArr = aLanes.Field<DynArrayHeader>(aOrig.lane, kOutLanes);
    bool ok = outlineArr.size == aOrig.outline.size() &&
              SafeWrite(outlineArr.data, aOrig.outline.data(), aOrig.outline.size() * sizeof(Point3)) &&
              SafeWrite(lane + kLength, &aOrig.length, sizeof(float));
    if (ok && accArr.size == aOrig.accumulated.size() && !aOrig.accumulated.empty())
        ok = SafeWrite(accArr.data, aOrig.accumulated.data(), aOrig.accumulated.size() * sizeof(float));
    if (ok && polyArr.size == aOrig.polygon.size() && !aOrig.polygon.empty())
        ok = SafeWrite(polyArr.data, aOrig.polygon.data(), aOrig.polygon.size() * sizeof(Vec2));
    if (ok && outsArr.size == aOrig.outs.size() && !aOrig.outs.empty())
        ok = SafeWrite(outsArr.data, aOrig.outs.data(), aOrig.outs.size() * sizeof(LaneOut));
    return ok;
}
} // namespace

bool IsRepairedLane(uint32_t aLane)
{
    if (gState.table != Lanes().Data())
        return false;
    for (auto& o : gState.originals)
        if (o.lane == aLane)
            return true;
    return false;
}

std::vector<LaneDraw> LanesForDrawing(void* aAutodrive, const RED4ext::Vector4& aPos, float aRadius, bool aPavements)
{
    std::vector<LaneDraw> out;
    if (!EnsureLanes(aAutodrive))
        return out;
    auto& lanes = Lanes();
    for (uint32_t i : lanes.Near(aPos.X, aPos.Y, aRadius))
    {
        uint16_t flags = lanes.Flags(i);
        bool pavement = flags & kLanePavement;
        if (pavement && !aPavements)
            continue;
        LaneDraw d{i, 1, false, lanes.Outline(i)};
        if (d.points.size() < 2 || std::fabs(d.points.front().z - aPos.Z) > 25.0f)
            continue;
        if (IsRepairedLane(i))
            d.kind = 6;
        else if (pavement)
            d.kind = 5;
        else if (flags & (kLaneNoAutodrive | kLaneNoAIDriving))
            d.kind = 4;
        else if (flags & kLaneGPSOnly)
            d.kind = 3;
        else if (flags & kLaneIntersection)
            d.kind = 2;
        uint32_t nOuts;
        lanes.Outs(i, nOuts);
        d.deadEnd = !pavement && nOuts == 0;
        out.push_back(std::move(d));
    }
    return out;
}

std::string RepairLanes(void* aAutodrive, bool aApply)
{
    if (!EnsureLanes(aAutodrive))
        return "lane graph not loaded";
    auto& lanes = Lanes();
    if (!aApply)
    {
        if (gState.table != lanes.Data())
        {
            gState = {}; // a new table (save loaded): nothing of ours in it
            return "no repairs applied";
        }
        size_t restored = 0;
        for (auto it = gState.originals.rbegin(); it != gState.originals.rend(); ++it)
            restored += Restore(lanes, *it);
        // Links: shrink the arrays back (the grown buffers stay with the game).
        for (auto it = gState.added.rbegin(); it != gState.added.rend(); ++it)
            SafeWrite(it->header + 0xC, &it->oldSize, sizeof(uint32_t));
        std::string summary = Format("%zu of %zu repaired lanes restored", restored, gState.originals.size());
        gState = {};
        LoadLanes(aAutodrive);
        Log("lane repair: %s", summary.c_str());
        return summary;
    }
    if (gState.table == lanes.Data())
    {
        // Same address, but a save load can rebuild the map in the same
        // place: still repaired only if a repaired lane still differs from
        // its original.
        bool still = gState.originals.empty();
        if (!still)
        {
            auto now = lanes.Outline(gState.originals.front().lane);
            auto& orig = gState.originals.front().outline;
            still = now.size() == orig.size() && !now.empty() && Dist2D(now.front(), orig.front()) + Dist2D(now.back(), orig.back()) > 0.01f;
        }
        if (still)
            return Format("already repaired: %zu lanes", gState.originals.size());
    }

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    gState = {};
    gState.table = lanes.Data();
    auto fixes = FindFixes(lanes);
    // Both ends of a lane can need moving: apply per lane.
    std::map<uint32_t, std::vector<Fix>> byLane;
    for (auto& f : fixes)
        byLane[f.lane].push_back(f);
    size_t done = 0, failed = 0;
    for (auto& [lane, list] : byLane)
    {
        auto old = lanes.Outline(lane);
        if (old.size() < 2)
        {
            ++failed;
            continue;
        }
        Point3 p0 = old.front(), p3 = old.back();
        for (auto& f : list)
            (f.start ? p0 : p3) = f.target;
        auto updated = Redraw(old, p0, p3);
        Original saved;
        if (WriteLane(lanes, lane, old, updated, &saved))
        {
            ++done;
            gState.originals.push_back(std::move(saved));
            for (auto& f : list)
            {
                const Point3& from = f.start ? old.front() : old.back();
                gState.log.push_back(Format("#%u %s: %s moved %.1f m from (%.1f, %.1f, %.1f) onto lane #%u", lane,
                                            LaneFlagNames(lanes.Flags(lane)).c_str(), f.start ? "start" : "end",
                                            Dist2D(from, f.target), from.x, from.y, from.z, f.partner));
            }
        }
        else
        {
            ++failed;
            gState.log.push_back(Format("#%u: write failed", lane));
        }
    }
    // The missing links, in the direction of travel: road lane -> junction
    // lane where a junction lane's start moved, junction lane -> road lane
    // where its end moved.
    InLayout inLayout = LearnInLayout(lanes);
    gState.log.push_back(inLayout.description);
    uint32_t outHigh = OutIdHigh(lanes);
    LoadLanes(aAutodrive); // the new lengths, for the links' `at`
    size_t linked = 0;
    for (auto& f : fixes)
    {
        uint32_t from = f.start ? f.partner : f.lane, to = f.start ? f.lane : f.partner;
        std::string result = AddLink(lanes, inLayout, outHigh, from, to);
        linked += result.rfind("linked", 0) == 0;
        gState.log.push_back("  " + result);
    }
    QueryPerformanceCounter(&end);
    std::string summary = Format("%zu junction lanes repaired, %zu links added%s%s, %.0f ms", done, linked,
                                 inLayout.valid ? "" : " (out-links only)",
                                 failed ? Format(", %zu failed", failed).c_str() : "",
                                 double(end.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart));
    std::ofstream out(DumpDir() / "lane_repairs.txt");
    out << "# " << summary << "\n";
    for (auto& line : gState.log)
        out << line << "\n";
    LoadLanes(aAutodrive); // pick up the new lengths and link positions
    Log("lane repair: %s", summary.c_str());
    return summary;
}
} // namespace BCAI
