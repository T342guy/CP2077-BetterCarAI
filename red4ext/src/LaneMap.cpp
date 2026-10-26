// Access to the traffic lane map (worldTrafficLanePersistent), found the way
// the game's own gameNativeAutodriveSystem::CheckCurrentLaneValidity
// (exe+0x21C7F40 in 2.5) reaches it:
//   traffic = *(*(*(autodriveSystem + 0x520) + 0x78) + 0x170)
//   exe+0x42FBCC(traffic, position, radius, out results, ...) returns
//   0x30-byte results whose first field is a lane pointer; it then tests the
//   car against lane+0x50 (polygon) and lane+0x88 bit 15 (no autodrive).
// That lookup (exe+0x42FBCC) shows where the lanes are:
//   container = *(traffic + 0x1D8)            (worldTrafficPersistentData)
//   lanes     = DynArray at container + 0x8   (0xA0 bytes each)
//   spatial index at container + 0x58, returning uint16 lane indices.
// Instead of calling it (its arguments are engine-internal containers), this
// reads the lane table directly. In 2.5 it holds every lane of the city
// (~34000), so a lane's index is a stable id; out-links refer to lanes by it.

#include "Plugin.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <unordered_set>

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

template<typename T>
bool Read(uintptr_t aAddr, T& aOut)
{
    return aAddr && SafeRead(reinterpret_cast<const void*>(aAddr), &aOut, sizeof(T));
}

bool LooksLikePointer(uintptr_t v)
{
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull && (v & 7) == 0;
}

double MsSince(const LARGE_INTEGER& aStart)
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - aStart.QuadPart) * 1000.0 / double(freq.QuadPart);
}

// A worldTrafficLanePersistent by its contents: an outline, one running
// length per segment (the first = the first segment's length), and a total
// length equal to the last running length. Only for the fallback search.
bool IsLane(uintptr_t aAddr)
{
    char raw[0x80];
    if (!SafeRead(reinterpret_cast<const void*>(aAddr), raw, sizeof(raw)))
        return false;
    DynArrayHeader outline, accumulated;
    std::memcpy(&outline, raw + kOutline, sizeof(outline));
    std::memcpy(&accumulated, raw + kAccumulated, sizeof(accumulated));
    float length;
    std::memcpy(&length, raw + kLength, 4);
    if (!LooksLikePointer(outline.data) || !LooksLikePointer(accumulated.data) || outline.size < 2 ||
        outline.size > 4096 || outline.size > outline.capacity || accumulated.size + 1 != outline.size ||
        !std::isfinite(length) || length <= 0.05f || length > 10000.0f)
        return false;
    float last = 0.0f, first = 0.0f;
    Point3 pts[2]{};
    if (!Read(accumulated.data + (accumulated.size - 1) * 4, last) || !Read(accumulated.data, first) ||
        !Read(outline.data, pts))
        return false;
    float gap = std::sqrt((pts[1].x - pts[0].x) * (pts[1].x - pts[0].x) + (pts[1].y - pts[0].y) * (pts[1].y - pts[0].y) +
                          (pts[1].z - pts[0].z) * (pts[1].z - pts[0].z));
    return std::fabs(last - length) < std::max(0.5f, length * 0.01f) && std::fabs(gap - first) < 0.05f + 0.02f * gap &&
           std::fabs(pts[0].x) < 20000.0f && std::fabs(pts[0].y) < 20000.0f;
}

uintptr_t TrafficSystem(uintptr_t aAutodrive)
{
    uintptr_t a = 0, b = 0, c = 0;
    if (!Read(aAutodrive + 0x520, a) || !LooksLikePointer(a) || !Read(a + 0x78, b) || !LooksLikePointer(b) ||
        !Read(b + 0x170, c) || !LooksLikePointer(c))
        return 0;
    return c;
}

// The largest array of lanes stored by value (stride 0xA0) reachable from
// the traffic system within a few pointers - for game versions where
// traffic+0x1D8->+0x8 has moved.
DynArrayHeader FindLaneTable(uintptr_t aTraffic)
{
    DynArrayHeader best{};
    struct Item
    {
        uintptr_t addr;
        int depth;
    };
    std::deque<Item> queue{{aTraffic, 0}};
    std::unordered_set<uintptr_t> visited;
    while (!queue.empty() && visited.size() < 4000)
    {
        Item item = queue.front();
        queue.pop_front();
        if (!visited.insert(item.addr).second)
            continue;
        std::vector<char> buf(item.depth == 0 ? 0x2000 : 0x400);
        size_t len = 0;
        for (size_t off = 0; off < buf.size(); off += 0x100)
        {
            if (!SafeRead(reinterpret_cast<const void*>(item.addr + off), buf.data() + off, 0x100))
                break;
            len = off + 0x100;
        }
        for (size_t off = 0; off + 16 <= len; off += 8)
        {
            DynArrayHeader h;
            std::memcpy(&h, buf.data() + off, sizeof(h));
            if (!LooksLikePointer(h.data))
                continue;
            if (h.size >= 50 && h.size <= h.capacity && h.size <= 65536 && h.size > best.size && IsLane(h.data) &&
                IsLane(h.data + kSize) && IsLane(h.data + size_t(h.size - 1) * kSize))
                best = h;
            if (item.depth < 3)
                queue.push_back({h.data, item.depth + 1});
        }
    }
    return best;
}
} // namespace

std::string LaneFlagNames(uint16_t aFlags)
{
    static const char* names[16] = {"spline", "bidirectional", "patrol", "pavement", "road", "intersection",
                                    "neverDeadEnd", "trafficDisabled", "crosswalk", "gpsOnly", "showDebug",
                                    "blockade", "yield", "noAIDriving", "highway", "noAutodrive"};
    std::string out;
    // "spline" is on almost everything and says nothing about use.
    for (int bit = 1; bit < 16; ++bit)
        if (aFlags & (1u << bit))
            out += (out.empty() ? "" : "|") + std::string(names[bit]);
    return out.empty() ? "-" : out;
}

LaneGraph& Lanes()
{
    static LaneGraph graph;
    return graph;
}

bool LaneGraph::Load(void* aAutodrive)
{
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    *this = LaneGraph();
    mTraffic = TrafficSystem(reinterpret_cast<uintptr_t>(aAutodrive));
    if (!mTraffic)
        return false;
    uintptr_t container = 0;
    DynArrayHeader table{};
    if (!Read(mTraffic + 0x1D8, container) || !LooksLikePointer(container) || !Read(container + 0x8, table) ||
        !LooksLikePointer(table.data) || table.size == 0 || table.size > table.capacity || table.size > 65536)
        table = FindLaneTable(mTraffic);
    if (!table.size)
        return false;

    mRaw.resize(size_t(table.size) * kSize);
    if (!SafeRead(reinterpret_cast<const void*>(table.data), mRaw.data(), mRaw.size()))
    {
        mRaw.clear();
        return false;
    }
    mData = table.data;
    mCount = table.size;

    mOutStart.reserve(mCount + 1);
    mOuts.reserve(size_t(mCount) * 2);
    for (uint32_t i = 0; i < mCount; ++i)
    {
        mOutStart.push_back(uint32_t(mOuts.size()));
        auto outs = Field<DynArrayHeader>(i, kOutLanes);
        if (outs.size && outs.size <= 64 && outs.size <= outs.capacity)
        {
            LaneOut buf[64];
            if (SafeRead(reinterpret_cast<const void*>(outs.data), buf, outs.size * sizeof(LaneOut)))
                for (uint32_t k = 0; k < outs.size; ++k)
                    if (buf[k].Lane() < table.size)
                        mOuts.push_back(buf[k]);
        }
        mByHash[Hash(i)].push_back(i);
    }
    mOutStart.push_back(uint32_t(mOuts.size()));
    mIns.assign(mCount, 0);
    for (auto& o : mOuts)
        ++mIns[o.Lane()];
    // Bounding boxes for Near().
    mBoxes.resize(mCount);
    for (uint32_t i = 0; i < mCount; ++i)
    {
        Box box{1e9f, 1e9f, -1e9f, -1e9f};
        for (auto& p : Outline(i))
            box = {std::min(box.minX, p.x), std::min(box.minY, p.y), std::max(box.maxX, p.x), std::max(box.maxY, p.y)};
        mBoxes[i] = box;
    }
    mLoadMs = MsSince(start);
    return true;
}

std::vector<Point3> LaneGraph::Outline(uint32_t i) const
{
    std::vector<Point3> pts;
    if (i >= mCount)
        return pts;
    auto outline = Field<DynArrayHeader>(i, kOutline);
    if (outline.size == 0 || outline.size > 4096)
        return pts;
    pts.resize(outline.size);
    if (!SafeRead(reinterpret_cast<const void*>(outline.data), pts.data(), pts.size() * sizeof(Point3)))
        pts.clear();
    return pts;
}

const LaneOut* LaneGraph::Outs(uint32_t i, uint32_t& aCount) const
{
    aCount = i < mCount ? mOutStart[i + 1] - mOutStart[i] : 0;
    return aCount ? mOuts.data() + mOutStart[i] : nullptr;
}

bool LaneGraph::Connected(uint32_t aFrom, uint32_t aTo) const
{
    uint32_t n;
    const LaneOut* outs = Outs(aFrom, n);
    for (uint32_t k = 0; k < n; ++k)
        if (outs[k].Lane() == aTo)
            return true;
    return false;
}

int LaneGraph::IndexOf(uintptr_t aPtr) const
{
    if (aPtr < mData || aPtr >= mData + size_t(mCount) * kSize || (aPtr - mData) % kSize)
        return -1;
    return int((aPtr - mData) / kSize);
}

const std::vector<uint32_t>* LaneGraph::ByHash(uint64_t aHash) const
{
    auto it = mByHash.find(aHash);
    return it == mByHash.end() ? nullptr : &it->second;
}

int LaneGraph::Resolve(const void* aUID) const
{
    uint64_t hash;
    uint16_t number, seq;
    uint8_t reversed;
    std::memcpy(&hash, aUID, 8);
    std::memcpy(&number, static_cast<const char*>(aUID) + 8, 2);
    std::memcpy(&seq, static_cast<const char*>(aUID) + 0xA, 2);
    std::memcpy(&reversed, static_cast<const char*>(aUID) + 0xC, 1);
    const std::vector<uint32_t>* candidates = hash ? ByHash(hash) : nullptr;
    if (!candidates)
        return -1;
    for (uint32_t c : *candidates)
        if (Number(c) == number && Seq(c) == seq && Reversed(c) == (reversed != 0))
            return int(c);
    return -1;
}

std::vector<uint32_t> LaneGraph::Near(float aX, float aY, float aRadius) const
{
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < mCount && i < mBoxes.size(); ++i)
    {
        const Box& b = mBoxes[i];
        if (aX < b.minX - aRadius || aX > b.maxX + aRadius || aY < b.minY - aRadius || aY > b.maxY + aRadius)
            continue;
        if (Distance(i, aX, aY) <= aRadius)
            out.push_back(i);
    }
    return out;
}

float LaneGraph::Distance(uint32_t i, float aX, float aY) const
{
    // Distance to the polyline, not just its points: lanes can be long and
    // straight with only two points.
    auto pts = Outline(i);
    float best = 1e9f;
    for (size_t k = 0; k < pts.size(); ++k)
    {
        if (k + 1 == pts.size())
        {
            best = std::min(best, std::hypot(pts[k].x - aX, pts[k].y - aY));
            break;
        }
        float sx = pts[k + 1].x - pts[k].x, sy = pts[k + 1].y - pts[k].y;
        float len2 = sx * sx + sy * sy;
        float t = len2 > 0.0f ? std::clamp(((aX - pts[k].x) * sx + (aY - pts[k].y) * sy) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, std::hypot(pts[k].x + sx * t - aX, pts[k].y + sy * t - aY));
    }
    return best;
}

std::string LaneGraph::Describe(uint32_t i, float aX, float aY) const
{
    if (i >= mCount)
        return Format("#%u (out of range)", i);
    auto pts = Outline(i);
    std::string ends = pts.empty() ? "no outline"
                                   : Format("(%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f)", pts.front().x, pts.front().y,
                                            pts.front().z, pts.back().x, pts.back().y, pts.back().z);
    return Format("#%u %s, %.1f m, maxSpeed %u, lane %u%s, %.1f m from the car, %s", i, LaneFlagNames(Flags(i)).c_str(),
                  Length(i), MaxSpeed(i), Number(i), Reversed(i) ? " reversed" : "", Distance(i, aX, aY),
                  ends.c_str());
}

uint32_t LoadLanes(void* aAutodrive)
{
    auto& lanes = Lanes();
    if (!aAutodrive || !lanes.Load(aAutodrive))
        return 0;
    Log("lane graph: %u lanes, traffic system @%p, %.0f ms", lanes.Count(), reinterpret_cast<void*>(lanes.Traffic()),
        lanes.LoadMs());
    return lanes.Count();
}

bool EnsureLanes(void* aAutodrive)
{
    auto& lanes = Lanes();
    if (lanes.Loaded())
    {
        // Same traffic system and the same table (a save load can replace them).
        uintptr_t container = 0;
        DynArrayHeader table{};
        if (TrafficSystem(reinterpret_cast<uintptr_t>(aAutodrive)) == lanes.Traffic() &&
            Read(lanes.Traffic() + 0x1D8, container) && Read(container + 0x8, table) && table.data == lanes.Data() &&
            table.size == lanes.Count())
            return true;
    }
    return LoadLanes(aAutodrive) > 0;
}

std::string DumpLaneGraph(void* aAutodrive, std::filesystem::path& aOutPath)
{
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    if (!EnsureLanes(aAutodrive))
        return "lane graph not loaded";
    auto& lanes = Lanes();
    aOutPath = DumpDir() / "lanegraph.txt";
    std::ofstream out(aOutPath);
    out << Format("# %u lanes. Per lane: index flags length width maxSpeed laneNumber seq reversed nodeRefHash\n",
                  lanes.Count());
    out << "#   | out links (lane:at:into) | outline x,y,z ...\n";
    for (uint32_t i = 0; i < lanes.Count(); ++i)
    {
        out << Format("%u 0x%04X %.2f %.2f %u %u %u %u %016llX |", i, lanes.Flags(i), lanes.Length(i),
                      lanes.Field<float>(i, kWidth), lanes.MaxSpeed(i), lanes.Number(i), lanes.Seq(i),
                      lanes.Reversed(i) ? 1 : 0, static_cast<unsigned long long>(lanes.Hash(i)));
        uint32_t n;
        const LaneOut* outs = lanes.Outs(i, n);
        for (uint32_t k = 0; k < n; ++k)
            out << Format(" %u:%.2f:%.2f", outs[k].Lane(), outs[k].at, outs[k].into);
        out << " |";
        for (auto& p : lanes.Outline(i))
            out << Format(" %.2f,%.2f,%.2f", p.x, p.y, p.z);
        out << "\n";
    }
    std::string summary = Format("%u lanes written in %.0f ms", lanes.Count(), MsSince(start));
    Log("lane graph dump: %s -> %ls", summary.c_str(), aOutPath.c_str());
    return summary;
}

std::string LaneQuery(void* aAutodrive, const RED4ext::Vector4& aPos, float aRadius, std::filesystem::path& aOutPath)
{
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    if (!LoadLanes(aAutodrive))
        return Lanes().Traffic() ? "no lane table found from the traffic system"
                                 : "traffic system not found (autodrive+0x520->+0x78->+0x170)";
    auto& lanes = Lanes();

    SYSTEMTIME now;
    GetLocalTime(&now);
    aOutPath = DumpDir() / Format("lanes_%04d%02d%02d_%02d%02d%02d.txt", now.wYear, now.wMonth, now.wDay, now.wHour,
                                  now.wMinute, now.wSecond);
    std::ofstream out(aOutPath);
    out << Format("# lane query at (%.1f, %.1f, %.1f), radius %.0f m\n", aPos.X, aPos.Y, aPos.Z, aRadius);
    out << Format("# %u lanes @%p (traffic system @%p), loaded in %.0f ms\n", lanes.Count(),
                  reinterpret_cast<void*>(lanes.Data()), reinterpret_cast<void*>(lanes.Traffic()), lanes.LoadMs());
    out << "# out: ->#lane @where the link leaves this lane (m) >where it joins the target (m)\n";

    size_t found = 0, noAutodrive = 0;
    for (uint32_t i = 0; i < lanes.Count(); ++i)
    {
        float nearest = lanes.Distance(i, aPos.X, aPos.Y);
        if (nearest > aRadius)
            continue;
        ++found;
        uint16_t flags = lanes.Flags(i);
        if (flags & kLaneNoAutodrive)
            ++noAutodrive;
        out << Format("lane #%u: %s, length %.1f m, width %.1f, maxSpeed %u, lane %u%s, %.1f m from the car\n", i,
                      LaneFlagNames(flags).c_str(), lanes.Length(i), lanes.Field<float>(i, kWidth), lanes.MaxSpeed(i),
                      lanes.Number(i), lanes.Reversed(i) ? " reversed" : "", nearest);
        out << "    outline:";
        for (auto& p : lanes.Outline(i))
            out << Format(" %.1f,%.1f,%.1f", p.x, p.y, p.z);
        uint32_t n;
        const LaneOut* outs = lanes.Outs(i, n);
        out << "\n    out:";
        for (uint32_t k = 0; k < n; ++k)
            out << Format(" ->#%u @%.1f>%.1f (0x%08X)", outs[k].Lane(), outs[k].at, outs[k].into, outs[k].id);
        out << "\n";
    }
    std::string summary = Format("%u lanes, %zu within %.0f m (%zu no-autodrive), %.0f ms", lanes.Count(), found,
                                 aRadius, noAutodrive, MsSince(start));
    out << "# " << summary << "\n";
    Log("lane query: %s -> %ls", summary.c_str(), aOutPath.c_str());
    return summary;
}
} // namespace BCAI
