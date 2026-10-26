// Memory scan for the AI's planned path.
//
// The native path follower (vehicleAutopilot etc.) keeps its state in memory
// the RTTI doesn't describe. A planned path is still recognisable: an ordered
// array of 3D points that starts near the car, runs ahead of it and has
// roughly even spacing. So starting from an object (the vehicle, its AI
// component, the drive command) this follows pointers breadth-first and
// reports
//   - dynamic arrays ({ptr, capacity, size}) and {begin, end} ranges whose
//     elements hold positions near the car, as floats or as the engine's
//     fixed-point world coordinates (int32 / 2^17), ranked by how path-like
//     they are, with their points; and
//   - float positions stored inline in the objects themselves,
// each with the pointer chain that reaches it, so a hit can be read directly
// from then on.
//
// It also recognises traffic lanes (worldTrafficLanePersistent) wherever a
// pointer leads to one, and - with the lane graph loaded (LaneMap.cpp) -
// arrays naming lanes by index, pointer or UID whose consecutive lanes are
// linked in the graph: the native driver follows traffic lanes, so its
// route should be such a chain.
//
// Reads are checked against the memory map and done with ReadProcessMemory,
// because the game threads can free memory while we look at it. The scan stops after a
// time budget so the game only hitches briefly.

#include "Plugin.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BCAI
{
namespace
{
constexpr size_t kRootBytes = 0x1000;
constexpr size_t kObjectBytes = 0x600;
constexpr size_t kMaxVisited = 40000;
constexpr double kTimeBudgetMs = 1000.0; // per root
constexpr float kNearRadius = 400.0f; // metres around the car, horizontally
constexpr float kNearHeight = 60.0f;
constexpr float kFixedPointScale = 1.0f / 131072.0f; // FixedPoint: 17 fractional bits

// Copies memory that may be freed or protected under us. ReadProcessMemory on
// our own process fails cleanly instead of faulting (and costs ~0.1 us). Do
// not use __try around memcpy here: clang-cl with /EHsc treats the copy as
// unable to fault and drops the handler, which crashed the game.
bool GuardedCopy(void* aDst, const void* aSrc, size_t aSize)
{
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), aSrc, aDst, aSize, &copied) && copied == aSize;
}

class Reader
{
public:
    // Copies up to aSize bytes; returns how many could be read (stops at the
    // first unreadable page).
    size_t Read(uintptr_t aAddr, void* aDst, size_t aSize)
    {
        size_t done = 0;
        while (done < aSize)
        {
            uintptr_t end = 0;
            if (!RegionReadable(aAddr + done, end))
                break;
            size_t chunk = std::min<size_t>(aSize - done, end - (aAddr + done));
            if (!GuardedCopy(static_cast<char*>(aDst) + done, reinterpret_cast<void*>(aAddr + done), chunk))
                break;
            done += chunk;
        }
        return done;
    }

    bool Readable(uintptr_t aAddr, size_t aSize)
    {
        uintptr_t end = aAddr + aSize;
        while (aAddr < end)
        {
            uintptr_t regionEnd = 0;
            if (!RegionReadable(aAddr, regionEnd))
                return false;
            aAddr = regionEnd;
        }
        return true;
    }

private:
    struct Region
    {
        uintptr_t end;
        bool readable;
    };

    // Whether aAddr is readable, and where its region ends. Regions are kept
    // in an ordered map so repeated lookups don't call VirtualQuery again.
    bool RegionReadable(uintptr_t aAddr, uintptr_t& aEnd)
    {
        auto it = mRegions.upper_bound(aAddr);
        if (it != mRegions.begin())
        {
            --it;
            if (aAddr < it->second.end)
            {
                aEnd = it->second.end;
                return it->second.readable;
            }
        }
        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t begin = aAddr & ~uintptr_t(0xFFF);
        Region r{begin + 0x1000, false};
        if (VirtualQuery(reinterpret_cast<void*>(aAddr), &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            r.end = begin + mbi.RegionSize;
            DWORD prot = mbi.Protect & 0xFF;
            r.readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && prot != PAGE_NOACCESS &&
                         prot != PAGE_EXECUTE && prot != 0;
        }
        mRegions[begin] = r;
        aEnd = r.end;
        return r.readable;
    }

    std::map<uintptr_t, Region> mRegions;
};

using Point = Point3;

constexpr size_t kLaneSize = LaneLayout::kSize;
constexpr size_t kLaneOutline = LaneLayout::kOutline;
constexpr size_t kLaneAccumulated = LaneLayout::kAccumulated;
constexpr size_t kLaneLength = LaneLayout::kLength;
constexpr size_t kLaneFlags = LaneLayout::kFlags;
constexpr size_t kLaneNumber = LaneLayout::kLaneNumber;
constexpr size_t kLaneReversed = LaneLayout::kReversed;
constexpr size_t kLaneMaxSpeed = LaneLayout::kMaxSpeed;

struct DynArrayHeader
{
    uintptr_t data;
    uint32_t capacity;
    uint32_t size;
};

struct LaneHit
{
    std::string chain;
    std::string kind;
    std::vector<uintptr_t> lanes;
};

// An array whose elements name lanes (by index, pointer or UID) where
// consecutive lanes are linked: a route through the lane graph.
struct RouteHit
{
    std::string chain;
    std::string kind;
    std::vector<uint32_t> lanes; // lane indices, in array order
    uint32_t links = 0;          // consecutive pairs that are connected lanes
    bool backwards = false;      // linked last-to-first
    float nearest = 1e9f;        // closest lane to the car
    int score = 0;
};

struct Hit
{
    std::string chain; // how to get there from the root
    std::string what;  // description
    std::vector<Point> points;
    int score = 0; // higher = more path-like; 1 = inline point
};

enum class Encoding
{
    Float,
    Fixed
};

class Scanner
{
public:
    Scanner(const RED4ext::Vector4& aOrigin, const RED4ext::Vector4& aHeading, int aMaxDepth, const LaneGraph* aLanes)
        : mOrigin(aOrigin)
        , mMaxDepth(aMaxDepth)
        , mLanes(aLanes && aLanes->Loaded() ? aLanes : nullptr)
    {
        float len = std::sqrt(aHeading.X * aHeading.X + aHeading.Y * aHeading.Y);
        mHeadX = len > 0.0f ? aHeading.X / len : 0.0f;
        mHeadY = len > 0.0f ? aHeading.Y / len : 0.0f;
        HMODULE exe = GetModuleHandleW(nullptr);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<char*>(exe) + dos->e_lfanew);
        mImageBegin = reinterpret_cast<uintptr_t>(exe);
        mImageEnd = mImageBegin + nt->OptionalHeader.SizeOfImage;
    }

    void Run(uintptr_t aRoot, const std::string& aName, size_t aRootBytes)
    {
        LARGE_INTEGER freq, start, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        // Breadth-first, so a time-out still covers the closest objects.
        std::deque<Item> queue;
        queue.push_back({aRoot, aName, 0, aRootBytes});
        while (!queue.empty() && mVisited.size() < kMaxVisited)
        {
            QueryPerformanceCounter(&now);
            if (double(now.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart) > kTimeBudgetMs)
            {
                mTimedOut = true;
                break;
            }
            Item item = std::move(queue.front());
            queue.pop_front();
            ScanObject(item, queue);
        }
        QueryPerformanceCounter(&now);
        mElapsedMs += double(now.QuadPart - start.QuadPart) * 1000.0 / double(freq.QuadPart);
    }

    std::vector<Hit>& Hits() { return mHits; }
    std::vector<LaneHit>& LaneHits() { return mLaneHits; }
    std::vector<RouteHit>& RouteHits() { return mRouteHits; }

    // A lane's outline and properties, for the report.
    std::string DescribeLane(uintptr_t aLane)
    {
        char raw[kLaneSize];
        if (mReader.Read(aLane, raw, kLaneSize) != kLaneSize)
            return "unreadable";
        DynArrayHeader outline;
        std::memcpy(&outline, raw + kLaneOutline, sizeof(outline));
        float length;
        uint16_t flags, number;
        std::memcpy(&length, raw + kLaneLength, 4);
        std::memcpy(&flags, raw + kLaneFlags, 2);
        std::memcpy(&number, raw + kLaneNumber, 2);
        bool reversed = raw[kLaneReversed] != 0;
        uint8_t maxSpeed = static_cast<uint8_t>(raw[kLaneMaxSpeed]);
        uint32_t n = std::min<uint32_t>(outline.size, 512);
        std::vector<Point> pts(n);
        if (n && mReader.Read(outline.data, pts.data(), n * sizeof(Point)) != n * sizeof(Point))
            n = 0;
        float nearest = 1e9f;
        for (uint32_t i = 0; i < n; ++i)
            nearest = std::min(nearest, std::hypot(pts[i].x - mOrigin.X, pts[i].y - mOrigin.Y));
        std::string out = Format("lane @%p length %.1f m, %u points, nearest %.0f m, maxSpeed %u, flags 0x%X, "
                                 "number %u%s\n        outline:",
                                 reinterpret_cast<void*>(aLane), length, outline.size, nearest, maxSpeed, flags,
                                 number, reversed ? ", reversed" : "");
        for (uint32_t i = 0; i < n; ++i)
            out += Format(" %.1f,%.1f,%.1f", pts[i].x, pts[i].y, pts[i].z);
        return out;
    }
    size_t Visited() const { return mVisited.size(); }
    double ElapsedMs() const { return mElapsedMs; }
    bool TimedOut() const { return mTimedOut; }

private:
    struct Item
    {
        uintptr_t addr;
        std::string chain;
        int depth;
        size_t bytes;
    };

    // Whether aAddr holds a worldTrafficLanePersistent: a lane of the loaded
    // lane table, or by its contents: an outline, one running length per
    // segment (the first = the first segment's length) and a total length
    // equal to the last running length.
    bool IsLane(uintptr_t aAddr)
    {
        if (mLanes && mLanes->IndexOf(aAddr) >= 0)
            return true;
        auto cached = mLaneCache.find(aAddr);
        if (cached != mLaneCache.end())
            return cached->second;
        bool lane = false;
        char raw[0x80];
        if (mReader.Read(aAddr, raw, sizeof(raw)) == sizeof(raw))
        {
            DynArrayHeader outline, accumulated;
            std::memcpy(&outline, raw + kLaneOutline, sizeof(outline));
            std::memcpy(&accumulated, raw + kLaneAccumulated, sizeof(accumulated));
            float length;
            std::memcpy(&length, raw + kLaneLength, 4);
            if (LooksLikePointer(outline.data) && LooksLikePointer(accumulated.data) && outline.size >= 2 &&
                outline.size <= 4096 && outline.size <= outline.capacity && accumulated.size + 1 == outline.size &&
                std::isfinite(length) && length > 0.1f && length < 10000.0f)
            {
                // The running lengths must match the outline: the first is the
                // distance between the first two points and the last is the
                // total. Random data doesn't do that.
                float last = 0.0f, first = 0.0f;
                Point pts[2]{};
                if (mReader.Read(accumulated.data + (accumulated.size - 1) * 4, &last, 4) == 4 &&
                    mReader.Read(accumulated.data, &first, 4) == 4 &&
                    mReader.Read(outline.data, pts, sizeof(pts)) == sizeof(pts))
                {
                    float gap = std::sqrt((pts[1].x - pts[0].x) * (pts[1].x - pts[0].x) +
                                          (pts[1].y - pts[0].y) * (pts[1].y - pts[0].y) +
                                          (pts[1].z - pts[0].z) * (pts[1].z - pts[0].z));
                    lane = std::fabs(last - length) < std::max(0.5f, length * 0.01f) && first > 0.01f &&
                           std::fabs(gap - first) < 0.05f + 0.02f * gap && std::isfinite(pts[0].x) &&
                           std::fabs(pts[0].x) < 20000.0f && std::fabs(pts[0].y) < 20000.0f;
                }
            }
        }
        mLaneCache[aAddr] = lane;
        return lane;
    }

    // Arrays whose elements are (or contain) pointers to lanes, or lanes by value.
    void TestLaneArray(uintptr_t aData, uint32_t aCount, const std::string& aChain, const char* aKind)
    {
        uint32_t n = std::min<uint32_t>(aCount, 256);
        LaneHit best;
        for (uint32_t stride : {8u, 16u, 24u, 32u, 40u, 48u, 64u})
        {
            size_t bytes = size_t(stride) * n;
            if (!mReader.Readable(aData, bytes))
                continue;
            std::vector<char> buf(bytes);
            if (mReader.Read(aData, buf.data(), bytes) != bytes)
                continue;
            for (uint32_t sub = 0; sub + 8 <= stride; sub += 8)
            {
                std::vector<uintptr_t> lanes;
                for (uint32_t i = 0; i < n; ++i)
                {
                    uintptr_t ptr = *reinterpret_cast<const uintptr_t*>(buf.data() + size_t(i) * stride + sub);
                    if (LooksLikePointer(ptr) && IsLane(ptr))
                        lanes.push_back(ptr);
                }
                if (lanes.size() >= 2 && lanes.size() * 2 >= n && lanes.size() > best.lanes.size())
                {
                    best.chain = aChain;
                    best.kind = Format("%s of %u lane pointers (stride %u, at +%u)", aKind, aCount, stride, sub);
                    best.lanes = std::move(lanes);
                }
            }
        }
        // Lanes stored by value (a copy of part of the lane table).
        if (best.lanes.empty() && aCount <= 512 && IsLane(aData) && IsLane(aData + kLaneSize))
        {
            best.chain = aChain;
            best.kind = Format("%s of %u lanes by value", aKind, aCount);
            for (uint32_t i = 0; i < std::min<uint32_t>(aCount, 64); ++i)
                best.lanes.push_back(aData + i * kLaneSize);
        }
        if (!best.lanes.empty())
        {
            if (mLanes && best.lanes.size() >= 2)
            {
                std::vector<uint32_t> indices;
                for (uintptr_t lane : best.lanes)
                    if (int i = mLanes->IndexOf(lane); i >= 0)
                        indices.push_back(uint32_t(i));
                RouteHit route;
                if (ScoreRoute(indices, route))
                {
                    route.chain = best.chain;
                    route.kind = best.kind;
                    mRouteHits.push_back(std::move(route));
                }
            }
            mLaneHits.push_back(std::move(best));
        }
    }

    float LaneDistance(uint32_t aLane)
    {
        auto it = mLaneDistance.find(aLane);
        if (it != mLaneDistance.end())
            return it->second;
        return mLaneDistance[aLane] = mLanes->Distance(aLane, mOrigin.X, mOrigin.Y);
    }

    // Fills aOut if aLanes reads as a route: most consecutive pairs (ignoring
    // repeats) are linked lanes, in one direction, near the car.
    bool ScoreRoute(std::vector<uint32_t> aLanes, RouteHit& aOut)
    {
        if (!mLanes || aLanes.size() < 2)
            return false;
        uint32_t fwd = 0, back = 0, steps = 0;
        for (size_t i = 0; i + 1 < aLanes.size(); ++i)
        {
            if (aLanes[i] == aLanes[i + 1])
                continue;
            ++steps;
            fwd += mLanes->Connected(aLanes[i], aLanes[i + 1]);
            back += mLanes->Connected(aLanes[i + 1], aLanes[i]);
        }
        uint32_t links = std::max(fwd, back);
        if (links == 0 || links * 10 < steps * 6)
            return false;
        float nearest = 1e9f;
        for (size_t i = 0; i < aLanes.size() && i < 64; ++i)
            nearest = std::min(nearest, LaneDistance(aLanes[i]));
        if (nearest > 300.0f)
            return false;
        aOut.links = links;
        aOut.backwards = back > fwd;
        aOut.nearest = nearest;
        aOut.lanes = std::move(aLanes);
        aOut.score = int(links) * 20 + (nearest < 8.0f ? 500 : nearest < 50.0f ? 200 : 0) +
                     int(10.0f * float(links) / float(std::max(steps, 1u)));
        return true;
    }

    // Arrays of lane indices (u16, or the low half of a u32 like the
    // out-link ids) or lane UIDs (nodeRefHash, laneNumber, seq, reversed)
    // whose consecutive lanes are linked. Keeps the best layout per array.
    // aCount elements of unknown size, or, for a range, aBytes bytes.
    void TestLaneChain(uintptr_t aData, uint32_t aCount, size_t aBytes, const std::string& aChain, const char* aKind)
    {
        if (!mLanes || !mChainTested.insert(aData).second)
            return;
        const uint32_t count = mLanes->Count();
        RouteHit best;
        for (uint32_t stride : {2u, 4u, 8u, 12u, 16u, 20u, 24u, 32u, 40u, 48u, 64u, 80u, 96u})
        {
            uint32_t elements = aBytes ? uint32_t(aBytes / stride) : aCount;
            uint32_t n = std::min<uint32_t>(elements, 512);
            if (n < 2 || (aBytes && aBytes % stride))
                continue;
            size_t bytes = size_t(stride) * n;
            if (bytes > 64 * 1024 || !mReader.Readable(aData, bytes))
                continue;
            std::vector<char> buf(bytes);
            if (mReader.Read(aData, buf.data(), bytes) != bytes)
                continue;
            for (uint32_t sub = 0; sub + 2 <= stride; sub += 2)
            {
                std::vector<uint32_t> lanes;
                uint32_t bad = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    uint16_t v;
                    std::memcpy(&v, buf.data() + size_t(i) * stride + sub, 2);
                    if (v < count)
                        lanes.push_back(v);
                    else
                        ++bad;
                }
                RouteHit route;
                if (bad * 10 <= n && ScoreRoute(std::move(lanes), route) && route.score > best.score)
                {
                    best = std::move(route);
                    best.kind = Format("%s of %u, u16 lane index at +%u, stride %u", aKind, elements, sub, stride);
                }
            }
            // worldTrafficLaneUID { Uint64 nodeRefHash; Uint16 laneNumber; Uint16 seqNumber; Bool isReversed; }
            for (uint32_t sub = 0; stride >= 16 && sub + 16 <= stride; sub += 8)
            {
                std::vector<uint32_t> lanes;
                uint32_t bad = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    int lane = mLanes->Resolve(buf.data() + size_t(i) * stride + sub);
                    if (lane < 0)
                        ++bad;
                    else
                        lanes.push_back(uint32_t(lane));
                }
                RouteHit route;
                if (bad * 10 <= n && ScoreRoute(std::move(lanes), route) && route.score > best.score)
                {
                    best = std::move(route);
                    best.kind = Format("%s of %u, lane UID at +%u, stride %u", aKind, elements, sub, stride);
                }
            }
        }
        if (best.score > 0)
        {
            best.chain = aChain;
            mRouteHits.push_back(std::move(best));
        }
    }

    bool LooksLikePointer(uintptr_t v) const
    {
        return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull && (v & 7) == 0 && !(v >= mImageBegin && v < mImageEnd);
    }

    static bool Decode(const char* aData, Encoding aEnc, Point& aOut)
    {
        if (aEnc == Encoding::Float)
        {
            std::memcpy(&aOut, aData, sizeof(Point));
            return std::isfinite(aOut.x) && std::isfinite(aOut.y) && std::isfinite(aOut.z);
        }
        int32_t v[3];
        std::memcpy(v, aData, sizeof(v));
        aOut = {v[0] * kFixedPointScale, v[1] * kFixedPointScale, v[2] * kFixedPointScale};
        return true;
    }

    // Horizontal distance from the car, or -1 if not near it.
    float NearDistance(const Point& p) const
    {
        float dx = p.x - mOrigin.X, dy = p.y - mOrigin.Y, dz = p.z - mOrigin.Z;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > kNearRadius || std::fabs(dz) > kNearHeight)
            return -1.0f;
        // Exact zeros are almost always unrelated data.
        if (p.x == 0.0f && p.y == 0.0f)
            return -1.0f;
        return d;
    }

    // Tries element layouts (encoding, stride, sub-offset) for aCount
    // elements at aData and records the most path-like one.
    void TestArray(uintptr_t aData, uint32_t aCount, const std::string& aChain, const char* aKind)
    {
        static const uint32_t strides[] = {12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 96, 112, 128, 160, 192};
        uint32_t n = std::min<uint32_t>(aCount, 256);
        Hit best;
        for (uint32_t stride : strides)
        {
            size_t bytes = size_t(stride) * n;
            if (bytes > 64 * 1024 || !mReader.Readable(aData, bytes))
                continue;
            std::vector<char> buf(bytes);
            if (mReader.Read(aData, buf.data(), bytes) != bytes)
                continue;
            for (Encoding enc : {Encoding::Float, Encoding::Fixed})
                for (uint32_t sub = 0; sub + 12 <= stride; sub += 4)
                    if (QuickNear(buf, n, stride, sub, enc))
                        TryLayout(buf, n, stride, sub, enc, aCount, aChain, aKind, best);
        }
        if (best.score > 1)
            mHits.push_back(std::move(best));
    }

    // Cheap pre-check: are the first few elements near the car at all?
    bool QuickNear(const std::vector<char>& aBuf, uint32_t n, uint32_t aStride, uint32_t aSub, Encoding aEnc) const
    {
        uint32_t probe = std::min<uint32_t>(n, 6), nearCount = 0;
        for (uint32_t i = 0; i < probe; ++i)
        {
            Point p;
            if (Decode(aBuf.data() + size_t(i) * aStride + aSub, aEnc, p) && NearDistance(p) >= 0.0f)
                ++nearCount;
        }
        return nearCount * 10 >= probe * 7;
    }

    void TryLayout(const std::vector<char>& aBuf, uint32_t n, uint32_t aStride, uint32_t aSub, Encoding aEnc,
                   uint32_t aCount, const std::string& aChain, const char* aKind, Hit& aBest)
    {
        std::vector<Point> pts;
        pts.reserve(n);
        uint32_t nearCount = 0, ahead = 0;
        float minD = 1e9f, maxD = 0.0f;
        for (uint32_t i = 0; i < n; ++i)
        {
            Point p;
            if (!Decode(aBuf.data() + size_t(i) * aStride + aSub, aEnc, p))
                continue;
            float d = NearDistance(p);
            if (d < 0.0f)
                continue;
            ++nearCount;
            minD = std::min(minD, d);
            maxD = std::max(maxD, d);
            if ((p.x - mOrigin.X) * mHeadX + (p.y - mOrigin.Y) * mHeadY > 0.0f)
                ++ahead;
            pts.push_back(p);
        }
        // Mostly points near the car, spread out (not one repeated point).
        if (nearCount < 3 || nearCount * 10 < n * 8 || maxD - minD < 2.0f)
            return;

        std::vector<float> steps;
        float length = 0.0f;
        uint32_t bigSteps = 0;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            float sx = pts[i].x - pts[i - 1].x, sy = pts[i].y - pts[i - 1].y, sz = pts[i].z - pts[i - 1].z;
            float s = std::sqrt(sx * sx + sy * sy + sz * sz);
            steps.push_back(s);
            length += s;
            if (s > 30.0f)
                ++bigSteps;
        }
        std::vector<float> sorted = steps;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        float median = sorted[sorted.size() / 2];
        // A route: evenly spaced points, at least 30 m of them, starting
        // close to the car.
        bool ordered = median >= 0.3f && median <= 25.0f && bigSteps * 20 <= steps.size() && length >= 30.0f &&
                       minD < 50.0f;
        float aheadFrac = float(ahead) / float(nearCount);

        int score = int(nearCount) + (ordered ? 300 : 0) + (minD < 30.0f ? 150 : 0) + int(aheadFrac * 100.0f);
        if (score <= aBest.score)
            return;
        aBest.score = score;
        aBest.chain = aChain;
        aBest.points = std::move(pts);
        aBest.what = Format("%s of %u, stride %u, %s at +%u: %u/%u near, %.0f..%.0f m away, %.0f%% ahead, "
                            "median step %.1f m, %u steps > 30 m, length %.0f m%s",
                            aKind, aCount, aStride, aEnc == Encoding::Float ? "float3" : "fixed3", aSub, nearCount,
                            n, minD, maxD, aheadFrac * 100.0f, median, bigSteps, length,
                            ordered ? "  << ORDERED" : "");
    }

    void ScanObject(const Item& aItem, std::deque<Item>& aQueue)
    {
        if (!mVisited.insert(aItem.addr).second)
            return;
        std::vector<char> buf(aItem.bytes);
        size_t len = mReader.Read(aItem.addr, buf.data(), aItem.bytes);
        if (len < 16)
            return;

        // Inline float positions (e.g. a target point or the car's own transform).
        for (size_t off = 0; off + 12 <= len; off += 4)
        {
            Point p;
            if (!Decode(buf.data() + off, Encoding::Float, p))
                continue;
            float d = NearDistance(p);
            if (d >= 0.0f)
            {
                Hit hit;
                hit.chain = aItem.chain + Format("+0x%zX", off);
                hit.what = Format("inline point (%.1f, %.1f, %.1f), %.1f m from the car", p.x, p.y, p.z, d);
                hit.score = 1;
                mHits.push_back(std::move(hit));
                off += 8; // don't report the same point shifted by one float
            }
        }

        for (size_t off = 0; off + 16 <= len; off += 8)
        {
            uintptr_t ptr = *reinterpret_cast<const uintptr_t*>(buf.data() + off);
            if (!LooksLikePointer(ptr))
                continue;
            if (IsLane(ptr))
                mLaneHits.push_back({aItem.chain + Format("+0x%zX->", off), "pointer to a lane", {ptr}});
            // RED engine DynArray: { T* data; uint32 capacity; uint32 size; }
            uint32_t cap = *reinterpret_cast<const uint32_t*>(buf.data() + off + 8);
            uint32_t size = *reinterpret_cast<const uint32_t*>(buf.data() + off + 12);
            if (size >= 1 && size <= cap && cap <= 200000)
                TestLaneArray(ptr, size, aItem.chain + Format("+0x%zX->data", off), "DynArray");
            if (size >= 2 && size <= cap && cap <= 200000)
                TestLaneChain(ptr, size, 0, aItem.chain + Format("+0x%zX->data", off), "DynArray");
            if (size >= 3 && size <= cap && cap <= 200000)
                TestArray(ptr, size, aItem.chain + Format("+0x%zX->data", off), "DynArray");
            // { T* begin; T* end; } ranges
            uintptr_t end = *reinterpret_cast<const uintptr_t*>(buf.data() + off + 8);
            if (end > ptr && end - ptr <= 1024 * 1024 && LooksLikePointer(end))
            {
                if ((end - ptr) % 8 == 0)
                    TestLaneArray(ptr, uint32_t((end - ptr) / 8), aItem.chain + Format("+0x%zX->[begin,end)", off),
                                  "range");
                TestLaneChain(ptr, 0, end - ptr, aItem.chain + Format("+0x%zX->[begin,end)", off), "range");
                for (uint32_t stride : {12u, 16u, 32u, 48u, 64u})
                    if ((end - ptr) % stride == 0 && (end - ptr) / stride >= 3)
                    {
                        TestArray(ptr, uint32_t((end - ptr) / stride),
                                  aItem.chain + Format("+0x%zX->[begin,end)", off), "range");
                        break;
                    }
            }
            if (aItem.depth < mMaxDepth && !mVisited.count(ptr))
                aQueue.push_back({ptr, aItem.chain + Format("+0x%zX->", off), aItem.depth + 1, kObjectBytes});
        }
    }

    RED4ext::Vector4 mOrigin;
    float mHeadX = 0.0f;
    float mHeadY = 0.0f;
    int mMaxDepth;
    const LaneGraph* mLanes;
    uintptr_t mImageBegin = 0;
    uintptr_t mImageEnd = 0;
    Reader mReader;
    std::unordered_set<uintptr_t> mVisited;
    std::unordered_map<uintptr_t, bool> mLaneCache;
    std::vector<Hit> mHits;
    std::vector<LaneHit> mLaneHits;
    std::vector<RouteHit> mRouteHits;
    std::unordered_set<uintptr_t> mChainTested;
    std::unordered_map<uint32_t, float> mLaneDistance;
    double mElapsedMs = 0.0;
    bool mTimedOut = false;
};
} // namespace

bool SafeRead(const void* aSrc, void* aDst, size_t aSize)
{
    return GuardedCopy(aDst, aSrc, aSize);
}

std::string ScanForPath(void* aRoot, size_t aRootSize, const char* aRootName, const RED4ext::Vector4& aOrigin,
                        const RED4ext::Vector4& aHeading, int aMaxDepth, std::filesystem::path& aOutPath)
{
    if (!aRoot)
        return "no object to scan";
    int depth = std::clamp(aMaxDepth, 1, 6);
    auto& graph = Lanes();
    Scanner scanner(aOrigin, aHeading, depth, &graph);
    // Only the root object itself: reading past its end wanders into
    // whatever the allocator put next to it.
    scanner.Run(reinterpret_cast<uintptr_t>(aRoot), aRootName, std::clamp<size_t>(aRootSize, 0x40, kRootBytes * 4));
    // From the autodrive system, also the traffic system it leads to (not a
    // script object, so it can't be passed in): it manages the traffic
    // vehicles' lane paths.
    if (std::strcmp(aRootName, "autodrive") == 0 && graph.Traffic())
        scanner.Run(graph.Traffic(), "traffic", 0x800);
    auto& hits = scanner.Hits();
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });

    size_t arrays = std::count_if(hits.begin(), hits.end(), [](const Hit& h) { return h.score > 1; });
    size_t ordered = std::count_if(hits.begin(), hits.end(), [](const Hit& h) { return h.score >= 300; });
    SYSTEMTIME now;
    GetLocalTime(&now);
    aOutPath = DumpDir() / Format("pathscan_%s_%04d%02d%02d_%02d%02d%02d.txt", aRootName, now.wYear, now.wMonth,
                                  now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::ofstream out(aOutPath);
    out << "# Better Car AI path scan from " << aRootName << Format(" @%p", aRoot) << "\n";
    out << Format("# car at (%.2f, %.2f, %.2f) heading (%.3f, %.3f)\n", aOrigin.X, aOrigin.Y, aOrigin.Z, aHeading.X,
                  aHeading.Y);
    out << Format("# %zu objects visited, depth %d, %.0f ms%s\n", scanner.Visited(), depth, scanner.ElapsedMs(),
                  scanner.TimedOut() ? " (stopped at the time limit)" : "");
    out << Format("# lane graph: %s\n", graph.Loaded() ? Format("%u lanes", graph.Count()).c_str()
                                                        : "not loaded (no route search)");
    out << "# Chains: +0xNN = offset in the object, -> = follow the pointer stored there.\n\n";

    auto& routes = scanner.RouteHits();
    std::stable_sort(routes.begin(), routes.end(), [](const RouteHit& a, const RouteHit& b) { return a.score > b.score; });
    out << "## Lane routes: arrays whose consecutive lanes are linked (best first)\n";
    for (size_t r = 0; r < routes.size() && r < 40; ++r)
    {
        auto& h = routes[r];
        out << h.chain << "\n    " << h.kind
            << Format("\n    %zu lanes, %u links%s, nearest %.1f m from the car, score %d\n", h.lanes.size(), h.links,
                      h.backwards ? " (stored last-to-first)" : "", h.nearest, h.score);
        for (size_t i = 0; i < h.lanes.size() && i < 64; ++i)
            out << "    [" << i << "] " << graph.Describe(h.lanes[i], aOrigin.X, aOrigin.Y) << "\n";
    }
    out << "\n";

    auto& laneHits = scanner.LaneHits();
    std::stable_sort(laneHits.begin(), laneHits.end(),
                     [](const LaneHit& a, const LaneHit& b) { return a.lanes.size() > b.lanes.size(); });
    size_t laneArrays = std::count_if(laneHits.begin(), laneHits.end(), [](const LaneHit& h) { return h.lanes.size() > 1; });
    out << "## Lane references (lane arrays first)\n";
    size_t laneShown = 0;
    for (auto& h : laneHits)
    {
        if (laneShown++ >= 300)
            break;
        out << h.chain << "\n    " << h.kind << "\n";
        for (size_t i = 0; i < h.lanes.size() && i < 64; ++i)
            out << "    [" << i << "] " << scanner.DescribeLane(h.lanes[i]) << "\n";
    }
    out << "\n## Arrays of points near the car (most path-like first)\n";
    size_t pointsShown = 0;
    for (auto& h : hits)
    {
        if (h.score <= 1 || pointsShown++ >= 150)
            continue;
        out << h.chain << "\n    " << h.what << "\n    points:";
        for (auto& p : h.points)
            out << Format(" %.1f,%.1f,%.1f", p.x, p.y, p.z);
        out << "\n";
    }
    out << "\n## Inline points near the car\n";
    size_t inlineShown = 0;
    for (auto& h : hits)
        if (h.score == 1 && inlineShown++ < 300)
            out << h.chain << "  " << h.what << "\n";

    std::string summary = Format("%s: %zu lane routes, %zu lane refs (%zu lane arrays), %zu point arrays (%zu ordered), "
                                 "%zu objects, %.0f ms",
                                 aRootName, routes.size(), laneHits.size(), laneArrays, arrays, ordered,
                                 scanner.Visited(), scanner.ElapsedMs());
    Log("path scan %s -> %ls", summary.c_str(), aOutPath.c_str());
    return summary;
}
} // namespace BCAI
