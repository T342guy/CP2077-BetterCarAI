// Runs the path scanner over a fake object graph full of traps (freed pages,
// no-access pages, guard pages, garbage pointers) plus a fake lane table
// reached like the game's (autodrive+0x520->+0x78->+0x170->+0x1D8->+0x8) and
// fake routes through it, to check under Wine that a scan can't crash the
// game and still finds the routes.
#include "../src/Plugin.hpp"
#include "nav_old.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <vector>

// Main.cpp registers natives with the game; not needed here.
void BCAI::RegisterNatives() {}

#ifndef _WIN32
// The native Linux build (tests/build_linux_sim.sh) drives only: no game, no
// scanner, so what Main.cpp and MemScan.cpp provide on Windows is simple here.
std::filesystem::path BCAI::PluginDir()
{
    return std::filesystem::current_path();
}

std::filesystem::path BCAI::DumpDir()
{
    auto dir = PluginDir() / "dumps";
    std::filesystem::create_directories(dir);
    return dir;
}

bool BCAI::SafeRead(const void* aSrc, void* aDst, size_t aSize)
{
    std::memcpy(aDst, aSrc, aSize);
    return true;
}
#endif

// No engine allocator here: grow lane link arrays with realloc.
static void AppendWithRealloc(uintptr_t aHeader, const void* aValue, size_t aSize)
{
    auto* data = reinterpret_cast<char**>(aHeader);
    auto* capacity = reinterpret_cast<uint32_t*>(aHeader + 8);
    auto* size = reinterpret_cast<uint32_t*>(aHeader + 12);
    if (*size >= *capacity)
    {
        char* grown = static_cast<char*>(malloc((*size + 4) * aSize));
        if (*size)
            std::memcpy(grown, *data, *size * aSize);
        *data = grown;
        *capacity = *size + 4;
    }
    std::memcpy(*data + *size * aSize, aValue, aSize);
    ++*size;
}

struct DynArr
{
    void* data;
    uint32_t cap;
    uint32_t size;
};

#ifdef _WIN32
static void* Page(DWORD prot)
{
    return VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, prot);
}
#endif

// Fills a worldTrafficLanePersistent with a straight outline: n points, one
// running length per segment (as in the game), out-links to `outs`.
static void FillLane(char* lane, float x0, float y0, float dx, float dy, int n, std::vector<uint32_t> outs = {})
{
    auto* pts = static_cast<float*>(calloc(n * 3, sizeof(float)));
    auto* acc = static_cast<float*>(calloc(n - 1, sizeof(float)));
    for (int i = 0; i < n; ++i)
    {
        pts[i * 3] = x0 + dx * i;
        pts[i * 3 + 1] = y0 + dy * i;
        pts[i * 3 + 2] = 10.0f;
        if (i)
            acc[i - 1] = std::sqrt(dx * dx + dy * dy) * i;
    }
    DynArr outline{pts, uint32_t(n), uint32_t(n)}, accumulated{acc, uint32_t(n - 1), uint32_t(n - 1)};
    std::memcpy(lane + 0x20, &outline, sizeof(outline));
    std::memcpy(lane + 0x30, &accumulated, sizeof(accumulated));
    std::memcpy(lane + 0x78, &acc[n - 2], 4);
    *reinterpret_cast<uint16_t*>(lane + 0x88) = 0x11; // spline|road
    lane[0x91] = 50;
    if (!outs.empty())
    {
        auto* links = static_cast<uint32_t*>(calloc(outs.size() * 3, 4));
        for (size_t k = 0; k < outs.size(); ++k)
        {
            links[k * 3] = 0xA4FF0000u | outs[k];
            std::memcpy(&links[k * 3 + 1], lane + 0x78, 4);
        }
        DynArr out{links, uint32_t(outs.size()), uint32_t(outs.size())};
        std::memcpy(lane + 0x00, &out, sizeof(out));
    }
}

static char* MakeLane(float x0, float y0, float dx, float dy, int n)
{
    auto* lane = static_cast<char*>(calloc(1, 0xA0));
    FillLane(lane, x0, y0, dx, dy, n);
    return lane;
}

// With a lanegraph.txt (from BCAI_DumpLaneGraph): rebuild that lane table in
// memory (behind a fake autodrive system) and run the junction repair on it.
static char* LoadDump(const char* aPath)
{
    FILE* f = fopen(aPath, "r");
    if (!f)
        return printf("cannot open %s\n", aPath), nullptr;
    std::vector<char*> lanes;
    static char line[1 << 20];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#')
            continue;
        auto* lane = static_cast<char*>(calloc(1, 0xA0));
        unsigned idx, flags, maxSpeed, number, seq, rev;
        float len, width;
        unsigned long long hash;
        char* p = line;
        int used = 0;
        sscanf(p, "%u 0x%x %f %f %u %u %u %u %llx |%n", &idx, &flags, &len, &width, &maxSpeed, &number, &seq, &rev,
               &hash, &used);
        p += used;
        std::vector<uint32_t> outs;
        std::vector<float> outAt, outInto;
        unsigned o;
        float at, into;
        while (sscanf(p, " %u:%f:%f%n", &o, &at, &into, &used) == 3)
            outs.push_back(o), outAt.push_back(at), outInto.push_back(into), p += used;
        p = strchr(p, '|') + 1;
        std::vector<float> pts;
        float x, y, z;
        while (sscanf(p, " %f,%f,%f%n", &x, &y, &z, &used) == 3)
            pts.push_back(x), pts.push_back(y), pts.push_back(z), p += used;
        uint32_t n = uint32_t(pts.size() / 3);
        auto* outline = static_cast<float*>(malloc(pts.size() * 4 + 4));
        std::memcpy(outline, pts.data(), pts.size() * 4);
        auto* acc = static_cast<float*>(malloc(n * 4 + 4));
        float total = 0;
        for (uint32_t k = 1; k < n; ++k)
        {
            float* a = outline + (k - 1) * 3;
            float* b = outline + k * 3;
            total += std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
            acc[k - 1] = total;
        }
        auto* links = static_cast<uint32_t*>(calloc(outs.size() * 3 + 1, 4));
        for (size_t k = 0; k < outs.size(); ++k)
        {
            links[k * 3] = 0xA4FF0000u | outs[k];
            std::memcpy(&links[k * 3 + 1], &outAt[k], 4);
            std::memcpy(&links[k * 3 + 2], &outInto[k], 4);
        }
        DynArr outArr{links, uint32_t(outs.size()), uint32_t(outs.size())};
        DynArr outlineArr{outline, n, n}, accArr{acc, n ? n - 1 : 0, n ? n - 1 : 0};
        std::memcpy(lane + 0x00, &outArr, sizeof(outArr));
        std::memcpy(lane + 0x20, &outlineArr, sizeof(outlineArr));
        std::memcpy(lane + 0x30, &accArr, sizeof(accArr));
        std::memcpy(lane + 0x70, &hash, 8);
        std::memcpy(lane + 0x78, &len, 4);
        std::memcpy(lane + 0x7C, &width, 4);
        *reinterpret_cast<uint16_t*>(lane + 0x88) = uint16_t(flags);
        *reinterpret_cast<uint16_t*>(lane + 0x8C) = uint16_t(number);
        *reinterpret_cast<uint16_t*>(lane + 0x8E) = uint16_t(seq);
        lane[0x90] = char(rev);
        lane[0x91] = char(maxSpeed);
        lanes.push_back(lane);
    }
    fclose(f);
    auto* table = static_cast<char*>(malloc(lanes.size() * 0xA0));
    for (size_t i = 0; i < lanes.size(); ++i)
        std::memcpy(table + i * 0xA0, lanes[i], 0xA0);
    auto* container = static_cast<char*>(calloc(1, 0x110));
    DynArr tableArr{table, uint32_t(lanes.size()), uint32_t(lanes.size())};
    std::memcpy(container + 0x8, &tableArr, sizeof(tableArr));
    auto* traffic = static_cast<char*>(calloc(1, 0x800));
    std::memcpy(traffic + 0x1D8, &container, 8);
    auto* b = static_cast<char*>(calloc(1, 0x200));
    std::memcpy(b + 0x170, &traffic, 8);
    auto* a = static_cast<char*>(calloc(1, 0x100));
    std::memcpy(a + 0x78, &b, 8);
    auto* autodrive = static_cast<char*>(calloc(1, 0x600));
    std::memcpy(autodrive + 0x520, &a, 8);
    printf("loaded %u lanes from the dump\n", BCAI::LoadLanes(autodrive));
    printf("repair: %s\n", BCAI::RepairLanes(autodrive, true).c_str());
    return autodrive;
}

static int RepairDump(const char* aPath)
{
    char* autodrive = LoadDump(aPath);
    if (!autodrive)
        return 1;
    std::filesystem::path out;
    BCAI::DumpLaneGraph(autodrive, out);
    printf("repaired lane graph: %s\n", out.string().c_str());
    return 0;
}

// Our driver on the dumped city: a bicycle-model car (steering that weakens
// with speed, limited grip, a lagging wheel) from (x0, y0) heading (hx, hy)
// to (x1, y1), or cruising without a goal. A stopped car can be put on the
// route `stopAt` metres ahead.
static int DriveDump(const char* aPath, float x0, float y0, float hx, float hy, bool aGoal, float x1, float y1,
                     float aStopAt)
{
    char* autodrive = LoadDump(aPath);
    if (!autodrive)
        return 1;
    // The height of the road under the start (going its way) and under the
    // goal: the lane nearest sideways, where the point projects onto it.
    auto height = [](float x, float y, const BCAI::Point3* aHeading)
    {
        using namespace BCAI::Autodrive;
        static GeoCache geo;
        float z = 0.0f, best = 1e9f;
        for (const Spot& spot : FindLanes(BCAI::Lanes(), geo, {x, y, 0.0f}, nullptr, 20.0f))
        {
            const LaneGeo& g = geo.Get(BCAI::Lanes(), spot.lane);
            BCAI::Point3 dir;
            BCAI::Point3 p = g.At(spot.s, &dir);
            if (spot.dist < best && (!aHeading || dir.x * aHeading->x + dir.y * aHeading->y > 0.3f))
                best = spot.dist, z = p.z;
        }
        return z;
    };
    BCAI::Point3 heading{hx / std::hypot(hx, hy), hy / std::hypot(hx, hy), 0.0f};
    float z0 = height(x0, y0, &heading), z1 = height(x1, y1, nullptr);
    BCAI::Point3 pos{x0, y0, z0}, goal{x1, y1, z1};
    float hl = std::hypot(hx, hy);
    float psi = std::atan2(hy / hl, hx / hl);
    // This car: BCAI_CAR="length,width,grip,acceleration scale,top speed[,steerK,steerP,brakeK,brakeP]"
    // (m, m/s^2, m/s), or a preset: "hellhound" (long, wide, heavy, grippy,
    // turns wide) or "maimai" (tiny, turns tightly, slow, accelerates weakly).
    struct SimCar
    {
        float length = 4.6f, width = 1.9f, grip = 9.0f, accel = 1.0f, top = 99.0f;
        float steerK = 0.17f, steerP = 2.0f, brakeK = 8.0f, brakeP = 0.3f;
    } car;
    if (const char* e = getenv("BCAI_CAR"))
    {
        if (!strcmp(e, "hellhound"))
            car = {5.8f, 2.4f, 10.0f, 0.75f, 55.0f, 0.12f, 2.0f, 6.5f, 0.35f};
        else if (!strcmp(e, "maimai"))
            car = {3.1f, 1.55f, 6.5f, 0.5f, 33.0f, 0.26f, 2.0f, 8.5f, 0.3f};
        else
            sscanf(e, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &car.length, &car.width, &car.grip, &car.accel, &car.top,
                   &car.steerK, &car.steerP, &car.brakeK, &car.brakeP);
    }
    if (const char* e = getenv("BCAI_SIM"))
        sscanf(e, "%f,%f,%f,%f", &car.steerK, &car.steerP, &car.brakeK, &car.brakeP);
    // As the scripts measure it when Autodrive+ starts.
    BCAI::DriverBody(0.5f * car.length, 0.5f * car.length, 0.5f * car.width);
    printf("start: %s\n", BCAI::DriverStart(autodrive, pos, {hx, hy, 0}, aGoal ? &goal : nullptr).c_str());
    std::filesystem::path pathOut;
    printf("plan: %s\n", BCAI::DriverDumpPath(pathOut).c_str());
    // Other cars (4.6 x 1.9 m boxes) along the route: one aStopAt metres
    // ahead (BCAI_LEAD=speed in m/s makes it drive on along the route;
    // BCAI_OBST_L shifts it sideways, left +), and BCAI_CARS="along,lateral,
    // speed[,sideways speed[,starts when the car is this close[,length,width
    // [,1: standing in traffic, not parked]]]];..." more.
    // Cars driving along the route brake for this car when it is ahead of
    // them in their lane, as the game's traffic does (BCAI_BLIND=1: they
    // don't, and drive into it from behind).
    struct Other
    {
        float along, lat, u, w, trigger;
        bool going;
        BCAI::Point3 c{}, dir{}, vel{};
        float length = 4.6f, width = 1.9f;
        float cruise = 0.0f; // the speed it drives at when nothing is in front of it
        bool stopped = false; // standing in the lane (traffic waiting, broken down), not parked
        // On a part of the route far from this car's that passes close by
        // (where the route comes back near itself): in the game it would be
        // on that other road, so it isn't there for this car.
        bool hidden = false;
    };
    std::vector<Other> others;
    std::vector<BCAI::Point3> track = BCAI::DriverAhead(6000.0f, 1.0f);
    if (aStopAt > 0)
        others.push_back({aStopAt - 3.0f, getenv("BCAI_OBST_L") ? float(atof(getenv("BCAI_OBST_L"))) : 0.0f,
                          getenv("BCAI_LEAD") ? float(atof(getenv("BCAI_LEAD"))) : 0.0f, 0.0f, 0.0f, true});
    if (const char* e = getenv("BCAI_CARS"))
        for (const char* p = e; *p;)
        {
            Other o{0, 0, 0, 0, 0, true};
            float stopped = 0.0f;
            int n = sscanf(p, "%f,%f,%f,%f,%f,%f,%f,%f", &o.along, &o.lat, &o.u, &o.w, &o.trigger, &o.length, &o.width,
                           &stopped);
            o.stopped = stopped > 0.5f;
            if (n >= 2)
            {
                o.along -= 3.0f; // the list starts at the bumper
                o.going = o.trigger <= 0.0f;
                o.cruise = o.u;
                others.push_back(o);
            }
            const char* next = strchr(p, ';');
            if (!next)
                break;
            p = next + 1;
        }
    for (Other& o : others)
        o.cruise = o.u;
    bool haveObstacle = !others.empty() && track.size() > 4;
    static bool blind = getenv("BCAI_BLIND") != nullptr;
    // This car along the route and sideways of it (left +), for the traffic behind it.
    float ownAlong = 0.0f, ownLat = 0.0f;
    auto locate = [&]()
    {
        float best = 1e9f;
        size_t from = size_t(std::max(ownAlong - 10.0f, 0.0f)), to = std::min(size_t(ownAlong + 40.0f), track.size() - 1);
        for (size_t i = from; i + 1 <= to; ++i)
        {
            const BCAI::Point3 &a = track[i], &b = track[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y, l2 = std::max(dx * dx + dy * dy, 1e-6f);
            float t = std::clamp(((pos.x - a.x) * dx + (pos.y - a.y) * dy) / l2, 0.0f, 1.0f);
            float px = a.x + dx * t, py = a.y + dy * t, d = std::hypot(pos.x - px, pos.y - py);
            if (d < best)
            {
                best = d;
                ownAlong = float(i) + t;
                ownLat = (dx * (pos.y - a.y) - dy * (pos.x - a.x)) / std::sqrt(l2);
            }
        }
    };
    auto place = [&](Other& o)
    {
        size_t i = std::min(size_t(std::max(o.along, 0.0f)), track.size() - 2);
        float t = std::clamp(o.along - float(i), 0.0f, 1.0f);
        BCAI::Point3 a = track[i], b = track[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y, l = std::max(std::hypot(dx, dy), 1e-3f);
        BCAI::Point3 f{dx / l, dy / l, 0}, left{-f.y, f.x, 0};
        o.c = {a.x + dx * t + left.x * o.lat, a.y + dy * t + left.y * o.lat, a.z + (b.z - a.z) * t};
        float ang = (o.u != 0.0f || o.w != 0.0f) ? std::atan2(o.w, std::max(o.u, 0.0f)) : 0.0f;
        o.dir = {f.x * std::cos(ang) + left.x * std::sin(ang), f.y * std::cos(ang) + left.y * std::sin(ang), 0};
        float u = o.going ? o.u : 0.0f, w = o.going ? o.w : 0.0f;
        o.vel = {f.x * u + left.x * w, f.y * u + left.y * w, 0};
    };
    // Two boxes (half length and half width) overlap (separating axes).
    auto overlap = [](BCAI::Point3 c1, BCAI::Point3 d1, float hl1, float hw1, BCAI::Point3 c2, BCAI::Point3 d2,
                      float hl2, float hw2)
    {
        BCAI::Point3 axes[4] = {d1, {-d1.y, d1.x, 0}, d2, {-d2.y, d2.x, 0}};
        auto extent = [](BCAI::Point3 d, BCAI::Point3 ax, float hl, float hw)
        { return hl * std::fabs(d.x * ax.x + d.y * ax.y) + hw * std::fabs(-d.y * ax.x + d.x * ax.y); };
        for (auto& ax : axes)
        {
            float dist = std::fabs((c2.x - c1.x) * ax.x + (c2.y - c1.y) * ax.y);
            if (dist > extent(d1, ax, hl1, hw1) + extent(d2, ax, hl2, hw2))
                return false;
        }
        return true;
    };
    int contacts = 0;
    bool touching = false;
    BCAI::DriverSettings set;
    if (getenv("BCAI_FAST")) // the in-game defaults: 180 km/h, 2.2 x traffic speed, 0.8 g
    {
        set.maxSpeed = 50.0f;
        set.laneSpeedFactor = 2.2f;
        set.latAccel = 0.8f * 9.81f;
    }
    // BCAI_STYLE=city|aggressive|madman: the style, with its in-game settings
    // (top speed, x traffic speed, cornering, braking, following time).
    if (const char* e = getenv("BCAI_STYLE"))
    {
        if (!strcmp(e, "city"))
            set = {110.0f / 3.6f, 0.6f * 9.81f, 1.3f, 3.0f, 2.0f, 0, 0};
        else if (!strcmp(e, "aggressive"))
            set = {180.0f / 3.6f, 0.8f * 9.81f, 2.2f, 4.0f, 1.5f, 0, 1};
        else if (!strcmp(e, "madman"))
            set = {250.0f / 3.6f, 0.95f * 9.81f, 3.0f, 5.0f, 0.8f, 0, 2};
    }
    const float dt = 1.0f / 60.0f;
    float v = 0.0f, delta = 0.0f, t = 0.0f, maxLat = 0.0f, maxErr = 0.0f, sumErr = 0.0f, dist = 0.0f;
    float maxSpeed = 0.0f, closest = 1e9f;
    int steps = 0, state = 0, pedalSwitches = 0;
    float jerkSum = 0.0f, steerRate = 0.0f;
    uint64_t digest = 1469598103934665603ull;
    float slide = 0.0f; // sideways drift when out of grip (left +)
    float offRoad = 0.0f;
    BCAI::DriverOutput out;
    for (; t < 900.0f; t += dt, ++steps)
    {
        BCAI::Point3 fwd{std::cos(psi), std::sin(psi), 0};
        BCAI::Point3 vel{fwd.x * v - fwd.y * slide, fwd.y * v + fwd.x * slide, 0};
        BCAI::Point3 oc{};
        if (haveObstacle)
            locate();
        if (haveObstacle)
        {
            bool touch = false;
            int touched = -1;
            for (Other& o : others)
            {
                place(o);
                if (!o.going && std::hypot(o.c.x - pos.x, o.c.y - pos.y) < o.trigger)
                    o.going = true;
                if (o.going && !blind && o.cruise > 0.0f && o.w == 0.0f)
                {
                    // Following this car when it is ahead in the lane (a
                    // plain car-following model), else back to its speed.
                    float a = 2.0f * (1.0f - std::pow(std::max(o.u, 0.0f) / o.cruise, 4.0f));
                    float gap = ownAlong - o.along - 0.5f * (car.length + o.length);
                    if (gap > -0.5f * car.length && gap < 150.0f &&
                        std::fabs(ownLat - o.lat) < 0.5f * (car.width + o.width) + 0.3f)
                    {
                        float want = 2.0f + 1.2f * o.u + o.u * (o.u - v) / (2.0f * std::sqrt(2.0f * 3.0f));
                        a -= 2.0f * std::pow(std::max(want, 0.0f) / std::max(gap, 0.5f), 2.0f);
                    }
                    o.u = std::max(o.u + std::max(a, -8.0f) * dt, 0.0f);
                }
                if (o.going)
                {
                    o.along += o.u * dt;
                    o.lat += o.w * dt;
                    // Gone off either end of the route (or far to the side): out of the way.
                    // (Past the end of the route it would drive on along some other road.)
                    if (o.along < -50.0f || o.along > float(track.size()) - 2.0f || std::fabs(o.lat) > 60.0f)
                        o.lat = 1000.0f;
                }
                float d = std::hypot(o.c.x - pos.x, o.c.y - pos.y);
                o.hidden = std::fabs(o.along - ownAlong) > 60.0f && d < 30.0f;
                if (o.hidden)
                    continue;
                if (d < closest)
                {
                    closest = d;
                    oc = o.c;
                }
                if (overlap(pos, fwd, 0.5f * car.length, 0.5f * car.width, o.c, o.dir, 0.5f * o.length, 0.5f * o.width))
                {
                    touch = true;
                    touched = int(&o - others.data());
                }
            }
            if (touch && !touching)
            {
                ++contacts;
                const Other& o = others[touched];
                printf("  ! t %5.1f s at (%.1f, %.1f): contact with another car, speed %.1f km/h (car %d: %.0f m along, "
                       "%+.1f sideways, %.1f m/s, %+.1f m/s across; this car %.0f m along)\n",
                       t, pos.x, pos.y, v * 3.6f, touched, o.along + 3.0f, o.lat, o.u, o.w, dist);
            }
            touching = touch;
        }
        // BCAI_LIST=1: the game's vehicle list (every 0.1 s, within 120 m);
        // BCAI_LIST=still: with every velocity reading 0 (traffic moved
        // without physics).
        static bool list = getenv("BCAI_LIST") != nullptr;
        static bool listStill = list && !strcmp(getenv("BCAI_LIST"), "still");
        if (list && haveObstacle && steps % 6 == 0)
        {
            std::vector<BCAI::DriverVehicle> listed;
            for (const Other& o : others)
                if (std::fabs(o.lat) < 999.0f && !o.hidden && std::hypot(o.c.x - pos.x, o.c.y - pos.y) < 120.0f)
                    listed.push_back({o.c, o.dir, listStill ? BCAI::Point3{} : o.vel,
                                      o.cruise == 0.0f && o.w == 0.0f && !o.stopped, o.width < 1.0f});
            BCAI::DriverVehicles(listed);
        }
        // BCAI_OTHERS_AT=t: where this car and the others are at t (along the route, sideways, speed).
        static float othersAt = getenv("BCAI_OTHERS_AT") ? float(atof(getenv("BCAI_OTHERS_AT"))) : -1.0f;
        if (othersAt >= 0.0f && t >= othersAt && t - dt < othersAt)
        {
            printf("    at t %.1f this car %.1f along, %+.1f sideways, %.1f m/s\n", t, ownAlong, ownLat, v);
            for (const Other& o : others)
                printf("      car %d: %.1f along, %+.1f sideways, %.1f m/s%s\n", int(&o - others.data()), o.along + 3.0f,
                       o.lat, o.u, o.hidden ? " (hidden)" : "");
        }
        bool running = BCAI::DriverStep(pos, fwd, vel, dt, set, out);
        // BCAI_EVENTS: what the driver decided, as it does.
        static bool events = getenv("BCAI_EVENTS") != nullptr;
        if (events)
            for (const std::string& e : BCAI::DriverEvents())
                printf("    > %s\n", e.c_str());
        // (BCAI_DIGEST: a hash of every control output, to tell two builds
        // that should drive the same apart.)
        for (float x : {out.throttle, out.brake, out.steer, out.handbrake, out.targetSpeed})
        {
            uint32_t bits;
            memcpy(&bits, &x, 4);
            digest = (digest ^ bits) * 1099511628211ull;
        }
        // Off the road, judged by this car's real width (more than half a metre past the edge).
        if (BCAI::DriverRoadOut(0.5f * car.width) > 0.5f)
            offRoad += std::fabs(v) * dt;
        static float dumpAt = getenv("BCAI_DUMP_AT") ? float(atof(getenv("BCAI_DUMP_AT"))) : -1.0f;
        if (dumpAt >= 0.0f && t >= dumpAt && t - dt < dumpAt)
            printf("  plan at t %.0f s: %s\n", t, BCAI::DriverDumpPath(pathOut).c_str());
        // The sensor: the driver's rays against the other car (a 4.6 x 1.9 m box).
        std::vector<BCAI::Point3> ends;
        std::vector<bool> hits;
        for (const auto& ray : BCAI::DriverRays(0.5f * car.length))
        {
            BCAI::Point3 end = ray.b;
            bool hit = false;
            float nearest = 2.0f;
            for (const Other& o : others)
            {
                if (!haveObstacle || ray.kind != 0)
                    break;
                if (o.hidden)
                    continue;
                const BCAI::Point3 &oc = o.c, &od = o.dir;
                // Into the box's frame, then the slab test.
                auto local = [&](const BCAI::Point3& p)
                {
                    float x = p.x - oc.x, y = p.y - oc.y;
                    return BCAI::Point3{x * od.x + y * od.y, -x * od.y + y * od.x, 0};
                };
                BCAI::Point3 a = local(ray.a), b = local(ray.b);
                float t0 = 0.0f, t1 = 1.0f;
                bool miss = false;
                for (int axis = 0; axis < 2 && !miss; ++axis)
                {
                    float p0 = axis ? a.y : a.x, d = axis ? b.y - a.y : b.x - a.x,
                          half = axis ? 0.5f * o.width : 0.5f * o.length;
                    if (std::fabs(d) < 1e-6f)
                        miss = std::fabs(p0) > half;
                    else
                    {
                        float u0 = (-half - p0) / d, u1 = (half - p0) / d;
                        if (u0 > u1)
                            std::swap(u0, u1);
                        t0 = std::max(t0, u0);
                        t1 = std::min(t1, u1);
                        miss = t0 > t1;
                    }
                }
                if (!miss && t0 < nearest)
                {
                    nearest = t0;
                    hit = true;
                    end = {ray.a.x + (ray.b.x - ray.a.x) * t0, ray.a.y + (ray.b.y - ray.a.y) * t0,
                           ray.a.z + (ray.b.z - ray.a.z) * t0};
                }
            }
            ends.push_back(end);
            hits.push_back(hit);
        }
        size_t seen = BCAI::DriverSense(ends, hits);
        static float teleFrom = getenv("BCAI_TELE") ? float(atof(getenv("BCAI_TELE"))) : -1.0f;
        static float teleLen = getenv("BCAI_TELE_LEN") ? float(atof(getenv("BCAI_TELE_LEN"))) : 12.0f;
        if (teleFrom >= 0 && t >= teleFrom && t < teleFrom + teleLen && steps % 30 == 0)
        {
            int hitCount = 0;
            for (bool h : hits)
                hitCount += h;
            std::string tele = BCAI::DriverTelemetry();
            printf("    t %5.1f rays %zu hit %d seen %zu | obstacle at (%.1f, %.1f), %.1f m | %s\n", t, ends.size(),
                   hitCount, seen, oc.x, oc.y, std::hypot(oc.x - pos.x, oc.y - pos.y),
                   tele.substr(0, tele.find(" | off")).c_str());
        }
        // Where the road is under the car (keeps z on the route).
        auto ahead = BCAI::DriverAhead(6.0f, 1.0f);
        if (!ahead.empty())
            pos.z = ahead.front().z;
        if (!running)
        {
            state = out.state;
            break;
        }
        // The car, answering its controls the way the game did in testing:
        // curvature 0.17·steer^2 (less at speed), a little brake already
        // brakes hard (8·brake^0.3), the wheel and pedals lag (BCAI_CAR, BCAI_SIM
        // for another car).
        const float simK = car.steerK, simP = car.steerP, simBK = car.brakeK, simBP = car.brakeP;
        delta += (-out.steer - delta) * std::min(1.0f, dt / 0.12f); // the wheel, as an input
        static float thr = 0.0f, brk = 0.0f;
        thr += (out.throttle - thr) * std::min(1.0f, dt / 0.2f);
        brk += (out.brake - brk) * std::min(1.0f, dt / 0.15f);
        float curv = std::copysign(simK * std::pow(std::fabs(delta), simP), delta) / (1.0f + (v / 25.0f) * (v / 25.0f));
        float r = v * curv;
        // Out of grip: it turns no more than the grip allows and drifts wide
        // (sideways, away from the turn) the more it asks for.
        slide = 0.0f;
        if (std::fabs(v * r) > car.grip)
        {
            float over = std::fabs(v * r) / car.grip - 1.0f;
            slide = -std::copysign(std::min(over, 0.6f) * 0.35f * v, r);
            r = std::copysign(car.grip / std::max(std::fabs(v), 0.1f), r);
        }
        maxLat = std::max(maxLat, std::fabs(v * r));
        float a = thr * car.accel * std::max(1.0f, 3.8f - 0.11f * v) * std::clamp(1.0f - std::pow(v / car.top, 3.0f), 0.0f, 1.0f) -
                  0.3f - 0.0006f * v * v;
        if (v > 0.0f)
            a -= (brk > 0.001f ? simBK * std::pow(brk, simBP) : 0.0f) + out.handbrake * 5.0f;
        else if (brk > 0.0f)
            a -= brk * 3.0f; // reversing
        v += a * dt;
        // Smoothness: switching between the pedals, jerk, and steering speed.
        static int lastPedal = 0;
        int pedal = out.brake > 0.05f ? -1 : out.throttle > 0.05f ? 1 : lastPedal;
        if (pedal != lastPedal && lastPedal != 0)
            ++pedalSwitches;
        lastPedal = pedal;
        static float lastA = 0.0f, lastSteer = 0.0f;
        jerkSum += steps > 0 ? std::fabs(a - lastA) : 0.0f;
        steerRate += std::fabs(out.steer - lastSteer);
        lastA = a;
        lastSteer = out.steer;
        if (out.handbrake > 0.0f && std::fabs(v) < 0.2f)
            v = 0.0f;
        psi += r * dt;
        pos.x += (std::cos(psi) * v - std::sin(psi) * slide) * dt;
        pos.y += (std::sin(psi) * v + std::cos(psi) * slide) * dt;
        dist += std::fabs(v) * dt;
        maxSpeed = std::max(maxSpeed, v);
        static float traceFrom = getenv("BCAI_TRACE") ? float(atof(getenv("BCAI_TRACE"))) : -1.0f;
        static float traceLen = getenv("BCAI_TRACE_LEN") ? float(atof(getenv("BCAI_TRACE_LEN"))) : 5.0f;
        if (traceFrom >= 0 && t >= traceFrom && t < traceFrom + traceLen && steps % 6 == 0)
            printf("    t %5.1f (%.1f,%.1f) v %5.1f tgt %5.1f off %+5.2f steer %+5.2f wheel %+5.2f yaw %+5.2f lat %4.1f thr %.2f brk %.2f\n",
                   t, pos.x, pos.y, v * 3.6f, out.targetSpeed * 3.6f, out.lateralError, out.steer, delta,
                   r, std::fabs(v * r), out.throttle, out.brake);
        static float lastReport = -10.0f;
        if (std::fabs(out.lateralError) > 2.5f && t - lastReport > 3.0f)
        {
            lastReport = t;
            printf("  ! t %5.1f s at (%.1f, %.1f): off line %+.2f m, speed %.1f km/h (target %.1f), steer %+.2f, "
                   "cornering %.1f m/s^2\n", t, pos.x, pos.y, out.lateralError, v * 3.6f, out.targetSpeed * 3.6f,
                   out.steer, std::fabs(v * r));
        }
        maxErr = std::max(maxErr, std::fabs(out.lateralError));
        sumErr += std::fabs(out.lateralError);
        if (steps % 3600 == 0 && steps > 0)
        {
            std::string tele = BCAI::DriverTelemetry();
            size_t at = tele.find("learned:");
            printf("  t %5.0f s  %s\n", t, at == std::string::npos ? tele.c_str() : tele.c_str() + at);
        }
        if (steps % 600 == 0)
            printf("  t %5.0f s  speed %5.1f km/h (target %5.1f)  off line %+5.2f m  steer %+5.2f thr %.2f brk %.2f\n",
                   t, v * 3.6f, out.targetSpeed * 3.6f, out.lateralError, out.steer, out.throttle, out.brake);
    }
    if (getenv("BCAI_EVENTS"))
        for (const std::string& e : BCAI::DriverEvents())
            printf("    > %s\n", e.c_str());
    printf("ended after %.0f s, %.0f m, state %d: %s\n", t, dist, state, BCAI::DriverStatus().c_str());
    printf("  average %.0f km/h, top %.0f km/h, off the line: max %.2f m, mean %.2f m, max cornering %.1f m/s^2\n",
           dist / std::max(t, 1.0f) * 3.6f, maxSpeed * 3.6f, maxErr, sumErr / std::max(steps, 1), maxLat);
    printf("  smoothness: %d switches between throttle and brake, jerk %.2f m/s^3 on average, steering moved %.3f/s\n",
           pedalSwitches, jerkSum / std::max(t, 1.0f), steerRate / std::max(t, 1.0f));
    printf("  off the road: %.0f m, %d moves of 2 m or more, %d changes of mind\n", offRoad, BCAI::DriverBigMoves(),
           BCAI::DriverDithers());
    if (haveObstacle)
        printf("  %zu other cars: %d contacts, closest centre-to-centre %.1f m\n", others.size(), contacts, closest);
    printf("  end (%.1f, %.1f), %.1f m from the goal\n", pos.x, pos.y, aGoal ? std::hypot(pos.x - x1, pos.y - y1) : 0.0f);
    if (getenv("BCAI_DIGEST"))
        printf("  digest %016llx\n", (unsigned long long)digest);
    return 0;
}

// Routes on the dumped city: aTrips random trips (a start on a road, a
// destination 300-3000 m away up to 15 m off a road other than a highway, at
// that road's height plus up to 3 m) planned by the 0.27 planner and the navigator.
// BCAI_NAV_Z=none puts the destination at height 0 (a map pin without a real
// height); BCAI_NAV_SHOW=1 prints every trip; BCAI_NAV_PATH=k writes trip k's
// two routes to dumps/nav_old.txt and dumps/nav_new.txt.
static int NavDump(const char* aPath, int aTrips, unsigned aSeed)
{
    using namespace BCAI::Autodrive;
    using BCAI::Point3;
    if (!LoadDump(aPath))
        return 1;
    const BCAI::LaneGraph& graph = BCAI::Lanes();
    GeoCache geo;
    std::mt19937 rng(aSeed);
    auto uniform = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    std::vector<uint32_t> roads;
    for (uint32_t i = 0; i < graph.Count(); ++i)
    {
        uint16_t f = graph.Flags(i);
        if (Drivable(f) && !(f & (BCAI::kLaneIntersection | BCAI::kLaneNoAutodrive | BCAI::kLaneNoAIDriving |
                                  BCAI::kLaneTrafficDisabled)) &&
            geo.Get(graph, i).Length() > 30.0f)
            roads.push_back(i);
    }
    const bool noHeight = getenv("BCAI_NAV_Z") && !strcmp(getenv("BCAI_NAV_Z"), "none");
    const bool show = getenv("BCAI_NAV_SHOW") != nullptr;
    const int pathTrip = getenv("BCAI_NAV_PATH") ? atoi(getenv("BCAI_NAV_PATH")) : -1;
    struct Totals
    {
        int ok = 0, uturns = 0, lefts = 0, junctions = 0, levelTrips = 0, wrongLevel = 0, longer = 0;
        double length = 0, time = 0, gap = 0, ms = 0;
    } totals[2];
    int both = 0;
    for (int trip = 0; trip < aTrips; ++trip)
    {
        uint32_t a = roads[rng() % roads.size()];
        const LaneGeo& ga = geo.Get(graph, a);
        Spot start{a, uniform(5.0f, ga.Length() - 5.0f), 0.0f};
        Point3 from = ga.At(start.s);
        uint32_t b = a;
        float sb = 0.0f;
        for (int tries = 0; tries < 1000; ++tries)
        {
            b = roads[rng() % roads.size()];
            sb = uniform(0.0f, geo.Get(graph, b).Length());
            float d = Dist2(from, geo.Get(graph, b).At(sb));
            if (d > 300.0f && d < 3000.0f && !(graph.Flags(b) & BCAI::kLaneHighway))
                break;
        }
        const LaneGeo& gb = geo.Get(graph, b);
        Point3 dir;
        Point3 onRoad = gb.At(sb, &dir);
        float side = uniform(-15.0f, 15.0f);
        Point3 goal{onRoad.x - dir.y * side, onRoad.y + dir.x * side, noHeight ? 0.0f : onRoad.z + uniform(0.0f, 3.0f)};
        // Another road above or below within 12 m: ending on the right one counts.
        bool stacked = false;
        for (uint32_t other : graph.Near(goal.x, goal.y, 12.0f))
        {
            const LaneGeo& go = geo.Get(graph, other);
            float d;
            Point3 od;
            float s = go.Project(goal.x, goal.y, d, od);
            if (Drivable(graph.Flags(other)) && d < 12.0f && std::fabs(go.At(s).z - onRoad.z) > 6.0f)
                stacked = true;
        }
        Route routes[2];
        bool ok[2], wrong[2] = {false, false};
        for (int k = 0; k < 2; ++k)
        {
            auto t0 = std::chrono::steady_clock::now();
            ok[k] = k == 0 ? OldNav::Plan(graph, geo, start, goal, routes[k])
                           : PlanRoute(graph, geo, start, goal, RouteOptions{}, routes[k]);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            Totals& t = totals[k];
            t.ms += ms;
            if (!ok[k])
                continue;
            ++t.ok;
            if (stacked)
            {
                ++t.levelTrips;
                const LaneGeo& ge = geo.Get(graph, routes[k].end.lane);
                if (std::fabs(ge.At(routes[k].end.s).z - onRoad.z) > 4.0f)
                    ++t.wrongLevel, wrong[k] = true;
            }
        }
        if (ok[0] && ok[1])
        {
            ++both;
            for (int k = 0; k < 2; ++k)
            {
                Totals& t = totals[k];
                t.length += routes[k].length;
                t.time += routes[k].time;
                t.gap += routes[k].endGap;
                for (const Manoeuvre& m : routes[k].turns)
                {
                    t.uturns += m.kind == Manoeuvre::kUTurn;
                    t.lefts += m.kind == Manoeuvre::kLeft;
                    t.junctions += m.junction;
                }
            }
            totals[1].longer += routes[1].time > routes[0].time + 1.0f;
        }
        if (show)
            for (int k = 0; k < 2; ++k)
                printf("trip %d %s: (%.0f,%.0f) -> (%.0f,%.0f,%.0f)%s: %s\n", trip, k ? "new" : "old", from.x, from.y,
                       goal.x, goal.y, goal.z,
                       wrong[k] ? " stacked, WRONG LEVEL" : stacked ? " stacked" : "",
                       ok[k] ? DescribeRoute(routes[k]).c_str() : "no route");
        if (trip == pathTrip)
            for (int k = 0; k < 2; ++k)
            {
                std::filesystem::path out = BCAI::DumpDir() / (k ? "nav_new.txt" : "nav_old.txt");
                FILE* f = fopen(out.string().c_str(), "w");
                fprintf(f, "# s x y z (%s)\n", ok[k] ? DescribeRoute(routes[k]).c_str() : "no route");
                int i = 0;
                for (const Point3& p : RoutePoints(graph, geo, routes[k].legs, 2.0f))
                    fprintf(f, "%d %.2f %.2f %.2f\n", 2 * i++, p.x, p.y, p.z);
                fclose(f);
                printf("trip %d %s route -> %s (goal %.1f,%.1f)\n", trip, k ? "new" : "old", out.string().c_str(),
                       goal.x, goal.y);
            }
    }
    printf("%d trips, %d routed by both%s\n", aTrips, both, noHeight ? " (destinations without a height)" : "");
    for (int k = 0; k < 2; ++k)
    {
        const Totals& t = totals[k];
        double n = std::max(both, 1);
        printf("  %s: routed %d, %.0f m, %.0f s on average, per trip %.2f junctions, %.2f left turns, %.3f U-turns, "
               "ends %.1f m from the pin, wrong level %d of %d, %.2f ms a plan\n",
               k ? "navigator" : "0.27     ", t.ok, t.length / n, t.time / n, t.junctions / n, t.lefts / n,
               t.uturns / n, t.gap / n, t.wrongLevel, t.levelTrips, t.ms / aTrips);
    }
    printf("  the navigator's route is slower (by its own measure) on %d trips\n", totals[1].longer);
    return 0;
}

// One trip, both planners, every leg: scan_harness lanegraph.txt navone x0 y0 hx hy x1 y1
static int NavOne(const char* aPath, float x0, float y0, float hx, float hy, float x1, float y1)
{
    using namespace BCAI::Autodrive;
    using BCAI::Point3;
    if (!LoadDump(aPath))
        return 1;
    const BCAI::LaneGraph& graph = BCAI::Lanes();
    GeoCache geo;
    auto height = [&](float x, float y)
    {
        std::vector<Spot> under = FindLanes(graph, geo, {x, y, 0.0f}, nullptr, 20.0f);
        return under.empty() ? 0.0f : geo.Get(graph, under.front().lane).At(under.front().s).z;
    };
    float hl = std::hypot(hx, hy);
    Point3 pos{x0, y0, 0.0f}, fwd{hx / hl, hy / hl, 0.0f}, goal{x1, y1, 0.0f};
    pos.z = height(x0, y0);
    goal.z = height(x1, y1);
    Spot start;
    if (!FindLane(graph, geo, pos, &fwd, 15.0f, start))
        return printf("no lane under the start\n"), 1;
    for (int k = 0; k < 2; ++k)
    {
        Route route;
        bool ok = k == 0 ? OldNav::Plan(graph, geo, start, goal, route)
                         : PlanRoute(graph, geo, start, goal, RouteOptions{}, route);
        printf("%s: %s\n", k ? "navigator" : "0.27", ok ? DescribeRoute(route).c_str() : route.why.c_str());
        for (const Leg& leg : route.legs)
            printf("   lane %5u %-40s %6.1f .. %6.1f  speed %u\n", leg.lane, BCAI::LaneFlagNames(graph.Flags(leg.lane)).c_str(),
                   leg.from, leg.to, graph.MaxSpeed(leg.lane));
        for (const Manoeuvre& m : route.turns)
            printf("   at %6.0f m: %s (%.0f deg)%s\n", m.at, ManoeuvreName(m.kind), m.angle * 57.3f,
                   m.junction ? " junction" : "");
    }
    return 0;
}

int main(int argc, char** argv)
{
    BCAI::gArrayAppendOverride = &AppendWithRealloc;
    // BCAI_ROUTES=0.27: Autodrive+ plans with the 0.27 planner (the city
    // scenes' cars are placed along its routes).
    if (getenv("BCAI_ROUTES") && !strcmp(getenv("BCAI_ROUTES"), "0.27"))
        BCAI::Autodrive::gPlanOverride = &OldNav::Plan;
    // scan_harness lanegraph.txt drive x0 y0 hx hy [x1 y1 | cruise] [stopAt]
    if (argc > 6 && std::strcmp(argv[2], "drive") == 0)
    {
        bool goal = argc > 8 && std::strcmp(argv[7], "cruise") != 0;
        float stopAt = argc > (goal ? 9 : 8) ? float(atof(argv[goal ? 9 : 8])) : 0.0f;
        return DriveDump(argv[1], float(atof(argv[3])), float(atof(argv[4])), float(atof(argv[5])),
                         float(atof(argv[6])), goal, goal ? float(atof(argv[7])) : 0, goal ? float(atof(argv[8])) : 0,
                         stopAt);
    }
    if (argc > 8 && std::strcmp(argv[2], "navone") == 0)
        return NavOne(argv[1], float(atof(argv[3])), float(atof(argv[4])), float(atof(argv[5])), float(atof(argv[6])),
                      float(atof(argv[7])), float(atof(argv[8])));
    // scan_harness lanegraph.txt nav trips [seed]
    if (argc > 3 && std::strcmp(argv[2], "nav") == 0)
        return NavDump(argv[1], atoi(argv[3]), argc > 4 ? unsigned(atoi(argv[4])) : 1u);
    if (argc > 1)
        return RepairDump(argv[1]);
#ifndef _WIN32
    printf("usage: drive_sim lanegraph.txt [drive x0 y0 hx hy [x1 y1 | cruise] [stopAt]]\n");
    return 1;
#else

    // Traps
    void* freed = Page(PAGE_READWRITE);
    VirtualFree(freed, 0, MEM_RELEASE);
    void* noAccess = Page(PAGE_NOACCESS);
    void* guard = Page(PAGE_READWRITE | PAGE_GUARD);
    void* nearEnd = static_cast<char*>(Page(PAGE_READWRITE)) + 4096 - 8; // object straddling into a hole

    // A route of three lanes joined end to end, and an object pointing at it.
    std::vector<char*> lanes = {MakeLane(0, 0, 3, 0, 10), MakeLane(27, 0, 3, 1, 10), MakeLane(54, 9, 2, 3, 10)};
    auto* routeArray = static_cast<char**>(calloc(3, sizeof(char*)));
    for (int i = 0; i < 3; ++i)
        routeArray[i] = lanes[i];
    auto* follower = static_cast<char*>(calloc(1, 0x100));
    DynArr route{routeArray, 3, 3};
    std::memcpy(follower + 0x40, &route, sizeof(route));
    std::memcpy(follower + 0x60, &lanes[1], 8); // "current lane"

    // Root: pointers to everything, some garbage, a self-loop.
    auto* root = static_cast<uintptr_t*>(calloc(64, 8));
    uintptr_t traps[] = {uintptr_t(freed), uintptr_t(noAccess), uintptr_t(guard), uintptr_t(nearEnd),
                         0x62491A18, 0x00007FFFFFFF0000ull, 0x10008, uintptr_t(root), uintptr_t(follower)};
    for (size_t i = 0; i < sizeof(traps) / sizeof(traps[0]); ++i)
        root[i] = traps[i];
    // A DynArray header whose data pointer is freed memory, and a range into no-access memory.
    DynArr badArr{freed, 100, 50};
    std::memcpy(&root[20], &badArr, sizeof(badArr));
    root[24] = uintptr_t(noAccess);
    root[25] = uintptr_t(noAccess) + 512;

    // A lane table of 300 lanes far away, except a route 120 -> 121 -> 122
    // (and a branch 121 -> 250) near the car, linked by out-link ids.
    constexpr uint32_t kLanes = 300;
    auto* table = static_cast<char*>(calloc(kLanes, 0xA0));
    for (uint32_t i = 0; i < kLanes; ++i)
        FillLane(table + i * 0xA0, 5000.0f + i * 50.0f, 5000.0f, 3, 0, 5, {(i + 7) % kLanes});
    FillLane(table + 120 * 0xA0, 0, 0, 3, 0, 10, {121});
    FillLane(table + 121 * 0xA0, 27, 0, 3, 1, 10, {122, 250});
    FillLane(table + 122 * 0xA0, 54, 9, 2, 3, 10, {7});
    auto* container = static_cast<char*>(calloc(1, 0x110));
    DynArr tableArr{table, kLanes, kLanes};
    std::memcpy(container + 0x8, &tableArr, sizeof(tableArr));
    auto* traffic = static_cast<char*>(calloc(1, 0x800));
    std::memcpy(traffic + 0x1D8, &container, 8);
    auto* b = static_cast<char*>(calloc(1, 0x200));
    std::memcpy(b + 0x170, &traffic, 8);
    auto* a = static_cast<char*>(calloc(1, 0x100));
    std::memcpy(a + 0x78, &b, 8);
    auto* autodrive = static_cast<char*>(calloc(1, 0x600));
    std::memcpy(autodrive + 0x520, &a, 8);
    uint32_t loaded = BCAI::LoadLanes(autodrive);
    printf("lanes loaded: %u (expect %u), 121->122 linked: %d, 122->121: %d\n", loaded, kLanes,
           BCAI::Lanes().Connected(121, 122), BCAI::Lanes().Connected(122, 121));

    // Routes as the out-link ids (u32), as u16 indices in 8-byte structs,
    // and as lane pointers into the table.
    auto* ids = static_cast<uint32_t*>(calloc(4, 4));
    ids[0] = 0xA4FF0000u | 120, ids[1] = 0xA4FF0000u | 121, ids[2] = 0xA4FF0000u | 122, ids[3] = 0xA4FF0000u | 7;
    auto* structs = static_cast<uint16_t*>(calloc(3 * 4, 2));
    structs[0] = 120, structs[4] = 121, structs[8] = 250, structs[1] = 9, structs[5] = 9, structs[9] = 9;
    auto* ptrs = static_cast<char**>(calloc(3, sizeof(char*)));
    ptrs[0] = table + 120 * 0xA0, ptrs[1] = table + 121 * 0xA0, ptrs[2] = table + 122 * 0xA0;
    auto* agent = static_cast<char*>(calloc(1, 0x100));
    DynArr idArr{ids, 4, 4}, structArr{structs, 3, 3}, ptrArr{ptrs, 3, 3};
    std::memcpy(agent + 0x10, &idArr, sizeof(idArr));
    std::memcpy(agent + 0x20, &structArr, sizeof(structArr));
    std::memcpy(agent + 0x30, &ptrArr, sizeof(ptrArr));
    root[30] = uintptr_t(agent);

    // The route object behind the autodrive system: 0x28-byte elements
    // starting with lane UIDs {hash, laneNumber, seqNumber, isReversed}.
    for (uint32_t i : {120u, 121u, 122u})
    {
        uint64_t hash = 0xABCD0000ull + (i == 122 ? 121 : i); // 121 and 122 share a spline
        *reinterpret_cast<uint64_t*>(table + i * 0xA0 + 0x70) = hash;
        *reinterpret_cast<uint16_t*>(table + i * 0xA0 + 0x8C) = 4;
        *reinterpret_cast<uint16_t*>(table + i * 0xA0 + 0x8E) = uint16_t(i == 122 ? 1 : 0);
    }
    BCAI::LoadLanes(autodrive); // again, for the hashes
    auto* routeElems = static_cast<char*>(calloc(3, 0x28));
    for (uint32_t k = 0; k < 3; ++k)
    {
        uint64_t hash = 0xABCD0000ull + (k == 2 ? 121 : 120 + k);
        std::memcpy(routeElems + k * 0x28, &hash, 8);
        *reinterpret_cast<uint16_t*>(routeElems + k * 0x28 + 8) = 4;
        *reinterpret_cast<uint16_t*>(routeElems + k * 0x28 + 0xA) = uint16_t(k == 2 ? 1 : 0);
    }
    auto* routeObj = static_cast<char*>(calloc(1, 0x100));
    DynArr routeArr{routeElems, 3, 3};
    std::memcpy(routeObj + 0x20, &routeArr, sizeof(routeArr));
    auto* r2 = static_cast<char*>(calloc(1, 0x400));
    std::memcpy(r2 + 0x3C0, &routeObj, 8);
    auto* r1 = static_cast<char*>(calloc(1, 0x40));
    std::memcpy(r1 + 0x30, &r2, 8);
    std::memcpy(autodrive + 0x540, &r1, 8);
    auto routeLanes = BCAI::RouteLanes(autodrive);
    RED4ext::Vector4 carPos{10, 0.5f, 10, 1};
    auto ahead = BCAI::RouteAhead(autodrive, carPos, 40.0f);
    printf("route lanes:");
    for (auto l : routeLanes)
        printf(" %u", l);
    printf(" (expect 120 121 122); ahead %zu points, first (%.1f,%.1f), last (%.1f,%.1f) (expect (10,0) .. 40 m on)\n",
           ahead.size(), ahead.empty() ? 0 : ahead.front().x, ahead.empty() ? 0 : ahead.front().y,
           ahead.empty() ? 0 : ahead.back().x, ahead.empty() ? 0 : ahead.back().y);
    // 120 -> 121 turns 18 deg, 121 -> 122 another 38 deg, each at a corner:
    // the plan should limit speed for the sharper one, 55 m ahead.
    auto plan = BCAI::PlanRouteSpeed(autodrive, carPos, 0.8f * 9.81f, 4.0f);
    printf("plan: valid %d, cap %.1f km/h, bend %.0f m ahead, radius %.1f m\n", plan.valid, plan.cap * 3.6f,
           plan.distance, plan.radius);
    std::filesystem::path routeOut;
    printf("%s\n", BCAI::DumpRoute(autodrive, carPos, routeOut).c_str());

    // A junction wired to the wrong side: road lane 200 heads north and ends
    // at (1000,1000) with no links; junction lane 201 (6 points) starts 5 m to
    // the left with nothing linking into it, and continues north to (995,1015).
    std::memset(table + 200 * 0xA0, 0, 16);  // 200: no out-links
    std::memset(table + 194 * 0xA0, 0, 16);  // 194 linked into 201; now nothing does
    FillLane(table + 200 * 0xA0, 1000, 900, 0, 25, 5);
    FillLane(table + 201 * 0xA0, 995, 1000, 0, 3, 6, {208});
    *reinterpret_cast<uint16_t*>(table + 201 * 0xA0 + 0x88) = 0x31; // road|intersection
    BCAI::LoadLanes(autodrive);
    float oldLen = BCAI::Lanes().Length(201);
    printf("repair: %s\n", BCAI::RepairLanes(autodrive, true).c_str());
    auto fixed = BCAI::Lanes().Outline(201);
    uint32_t nOut;
    const BCAI::LaneOut* outs201 = BCAI::Lanes().Outs(201, nOut);
    auto* acc201 = *reinterpret_cast<float**>(table + 201 * 0xA0 + 0x30);
    printf("lane 201: start (%.1f,%.1f) (expect 1000,1000), end (%.1f,%.1f), length %.2f -> %.2f, last running "
           "length %.2f, out-link at %.2f (expect = length)\n",
           fixed.front().x, fixed.front().y, fixed.back().x, fixed.back().y, oldLen, BCAI::Lanes().Length(201),
           acc201[4], nOut ? outs201[0].at : -1.0f);
    printf("200 -> 201 linked after repair: %d (expect 1)\n", BCAI::Lanes().Connected(200, 201));
    printf("again: %s\n", BCAI::RepairLanes(autodrive, true).c_str());
    printf("undo: %s\n", BCAI::RepairLanes(autodrive, false).c_str());
    auto restored = BCAI::Lanes().Outline(201);
    printf("lane 201 after undo: start (%.1f,%.1f) (expect 995,1000), length %.2f, 200 -> 201 linked %d (expect 0)\n",
           restored.front().x, restored.front().y, BCAI::Lanes().Length(201), BCAI::Lanes().Connected(200, 201));

    // Control probe: a car with throttle at +0x274, brake at +0x278 and a
    // physics object (at +0x300) whose +0x1C follows the steering with a
    // lag; speed builds up with throttle. Plus pointers into traps.
    {
        BCAI::ControlProbeReset();
        auto* car = static_cast<char*>(calloc(1, 0x400));
        auto* physics = static_cast<char*>(calloc(1, 0x200));
        std::memcpy(car + 0x300, &physics, 8);
        std::memcpy(car + 0x308, &freed, 8);
        std::memcpy(car + 0x310, &noAccess, 8);
        std::memcpy(car + 0x318, &nearEnd, 8);
        float speed = 0, steer = 0;
        uint32_t n = 0;
        for (int i = 0; i < 600; ++i)
        {
            float in[4] = {(i / 40) % 2 ? 1.0f : 0.0f, (i / 70) % 3 == 2 ? 1.0f : 0.0f,
                           (i / 25) % 4 == 1 ? 1.0f : ((i / 25) % 4 == 3 ? -1.0f : 0.0f), 0.0f};
            steer += (in[2] - steer) * 0.5f;
            speed = std::max(0.0f, speed + in[0] * 0.3f - in[1] * 0.6f - 0.02f);
            *reinterpret_cast<float*>(car + 0x274) = in[0];
            *reinterpret_cast<float*>(car + 0x278) = in[1];
            *reinterpret_cast<float*>(physics + 0x1C) = steer;
            *reinterpret_cast<float*>(car + 0x2A0) = speed;
            *reinterpret_cast<float*>(car + 0x2A4) = float(i); // a clock
            n = BCAI::ControlProbe(car, 0x400, in);
        }
        std::filesystem::path probeOut;
        printf("control probe (%u samples): %s\n", n, BCAI::ControlProbeReport(probeOut).c_str());
        printf("expect throttle car+0x274, brake car+0x278, steer car+0x300->+0x1C, handbrake unused\n");
        *reinterpret_cast<float*>(car + 0x274) = 0.25f;
        *reinterpret_cast<float*>(physics + 0x1C) = -0.5f;
        float read[4];
        BCAI::ReadControls(car, read);
        printf("read back: throttle %.2f (expect 0.25), steer %.2f (expect -0.50)\n", read[0], read[2]);
        printf("fields: %s\n", BCAI::ControlFields().c_str());
    }

    RED4ext::Vector4 origin{5, 1, 10, 1}, heading{1, 0, 0, 0};
    std::filesystem::path out;
    std::string summary = BCAI::ScanForPath(root, 64 * 8, "harness", origin, heading, 5, out);
    printf("survived: %s\n", summary.c_str());
    printf("report: %ls\n", out.c_str());
    // The guard page must still be a guard page (reading it would have cleared it).
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(guard, &mbi, sizeof(mbi));
    printf("guard page intact: %s\n", (mbi.Protect & PAGE_GUARD) ? "yes" : "NO");
    return 0;
#endif
}
