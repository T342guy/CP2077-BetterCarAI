#pragma once
// Shared plugin state: the RED4ext handle/SDK, our folder, and logging.

#include <RED4ext/RED4ext.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace BCAI
{
inline RED4ext::v1::PluginHandle gHandle = nullptr;
inline const RED4ext::v1::Sdk* gSdk = nullptr;

// red4ext/plugins/BetterCarAI (where the DLL is).
std::filesystem::path PluginDir();
// red4ext/plugins/BetterCarAI/dumps, created on first use.
std::filesystem::path DumpDir();

template<typename... Args>
void Log(const char* aFormat, Args... aArgs)
{
    if (gSdk && gHandle)
        gSdk->logger->InfoF(gHandle, aFormat, aArgs...);
}

template<typename... Args>
void Warn(const char* aFormat, Args... aArgs)
{
    if (gSdk && gHandle)
        gSdk->logger->WarnF(gHandle, aFormat, aArgs...);
}

template<typename... Args>
std::string Format(const char* aFormat, Args... aArgs)
{
    char buf[1024];
    std::snprintf(buf, sizeof(buf), aFormat, aArgs...);
    return buf;
}

// Reads memory that may be invalid without faulting (MemScan.cpp).
bool SafeRead(const void* aSrc, void* aDst, size_t aSize);

// Script-facing natives (Natives.cpp).
void RegisterNatives();

// Reflection dumps (RttiDump.cpp).
// Writes every class/enum whose name contains one of the comma-separated
// filters (case-insensitive) with parents, sizes, properties and functions.
int DumpTypes(const std::string& aFilters, std::filesystem::path& aOutPath);
// The RTTI-visible properties of a live object, `aDepth` levels deep.
std::string DumpObject(RED4ext::IScriptable* aObject, int aDepth);
// Code addresses (relative to the game exe) of native script functions whose
// class or name contains one of the filters, for disassembly.
int DumpNativeFunctions(const std::string& aFilters, std::filesystem::path& aOutPath);

struct Point3
{
    float x, y, z;
};

// worldTrafficLanePersistent (0xA0 bytes, see the RTTI dump).
namespace LaneLayout
{
constexpr size_t kSize = 0xA0;
constexpr size_t kOutLanes = 0x00; // array of 0xC-byte connections
constexpr size_t kInLanes = 0x10;
constexpr size_t kOutline = 0x20;     // array<Vector3>
constexpr size_t kAccumulated = 0x30; // array<Float>, one per segment
constexpr size_t kPolygon = 0x50;
constexpr size_t kNodeRefHash = 0x70;
constexpr size_t kLength = 0x78;
constexpr size_t kWidth = 0x7C;
constexpr size_t kFlags = 0x88; // worldTrafficLanePersistentFlags
constexpr size_t kLaneNumber = 0x8C;
constexpr size_t kSeqNumber = 0x8E;
constexpr size_t kReversed = 0x90;
constexpr size_t kMaxSpeed = 0x91;
} // namespace LaneLayout

// worldTrafficLanePersistentFlags
enum LaneFlag : uint16_t
{
    kLaneFromRoadSpline = 1,
    kLaneBidirectional = 2,
    kLanePatrolRoute = 4,
    kLanePavement = 8,
    kLaneRoad = 16,
    kLaneIntersection = 32,
    kLaneNeverDeadEnd = 64,
    kLaneTrafficDisabled = 128,
    kLaneCrossWalk = 256,
    kLaneGPSOnly = 512,
    kLaneShowDebug = 1024,
    kLaneBlockade = 2048,
    kLaneYield = 4096,
    kLaneNoAIDriving = 8192,
    kLaneHighway = 16384,
    kLaneNoAutodrive = 32768,
};
std::string LaneFlagNames(uint16_t aFlags);

// One worldTrafficConnectivityOutLane (0xC bytes): the target lane's index
// in the low 16 bits of the first word (the high bits vary, meaning
// unknown), then where the link leaves this lane and where it joins the
// target, as distances along each lane.
struct LaneOut
{
    uint32_t id;
    float at;
    float into;
    uint32_t Lane() const { return id & 0xFFFF; }
};

// The traffic lane table (worldTrafficPersistentData.lanes), copied in one
// read, with every lane's out-links. Found through the autodrive system the
// way the game's CheckCurrentLaneValidity reaches it (LaneMap.cpp).
class LaneGraph
{
public:
    // (Re)reads the table; false if it can't be found.
    bool Load(void* aAutodrive);
    bool Loaded() const { return mCount > 0; }
    uint32_t Count() const { return mCount; }
    uintptr_t Data() const { return mData; }
    uintptr_t Traffic() const { return mTraffic; }
    double LoadMs() const { return mLoadMs; }

    template<typename T>
    T Field(uint32_t aLane, size_t aOffset) const
    {
        T v;
        std::memcpy(&v, mRaw.data() + size_t(aLane) * LaneLayout::kSize + aOffset, sizeof(T));
        return v;
    }
    float Length(uint32_t i) const { return Field<float>(i, LaneLayout::kLength); }
    uint16_t Flags(uint32_t i) const { return Field<uint16_t>(i, LaneLayout::kFlags); }
    uint64_t Hash(uint32_t i) const { return Field<uint64_t>(i, LaneLayout::kNodeRefHash); }
    uint16_t Number(uint32_t i) const { return Field<uint16_t>(i, LaneLayout::kLaneNumber); }
    uint16_t Seq(uint32_t i) const { return Field<uint16_t>(i, LaneLayout::kSeqNumber); }
    bool Reversed(uint32_t i) const { return Field<uint8_t>(i, LaneLayout::kReversed) != 0; }
    uint8_t MaxSpeed(uint32_t i) const { return Field<uint8_t>(i, LaneLayout::kMaxSpeed); }

    // Read from game memory on each call.
    std::vector<Point3> Outline(uint32_t i) const;
    const LaneOut* Outs(uint32_t i, uint32_t& aCount) const;
    bool Connected(uint32_t aFrom, uint32_t aTo) const;
    // Index of the lane a pointer points at, or -1.
    int IndexOf(uintptr_t aPtr) const;
    // Lanes whose nodeRefHash is aHash (lanes of one road spline share it).
    const std::vector<uint32_t>* ByHash(uint64_t aHash) const;
    // The lane a worldTrafficLaneUID {Uint64 nodeRefHash; Uint16 laneNumber;
    // Uint16 seqNumber; Bool isReversed} (0x10 bytes at aUID) names, or -1.
    // Lanes of a junction share the hash and lane number, so all four count.
    int Resolve(const void* aUID) const;
    // Horizontal distance from the lane's outline to (x, y).
    float Distance(uint32_t i, float aX, float aY) const;
    // Lanes with part of their outline within aRadius of (x, y), using
    // bounding boxes kept from Load().
    std::vector<uint32_t> Near(float aX, float aY, float aRadius) const;
    // Lanes linking into lane i.
    uint32_t InCount(uint32_t i) const { return i < mCount ? mIns[i] : 0; }
    // One line: index, kind, length, speed, distance, start and end points.
    std::string Describe(uint32_t i, float aX, float aY) const;

private:
    uintptr_t mTraffic = 0;
    uintptr_t mData = 0;
    uint32_t mCount = 0;
    double mLoadMs = 0.0;
    std::vector<char> mRaw;
    std::vector<uint32_t> mOutStart; // per lane, into mOuts; mCount + 1 entries
    std::vector<LaneOut> mOuts;
    std::vector<uint32_t> mIns;
    struct Box
    {
        float minX, minY, maxX, maxY;
    };
    std::vector<Box> mBoxes;
    std::unordered_map<uint64_t, std::vector<uint32_t>> mByHash;
};
// The lane graph loaded by the last LaneQuery/LoadLanes call.
LaneGraph& Lanes();

// Memory scan for the AI's path (MemScan.cpp). Follows pointers from `aRoot`
// looking for arrays of world positions near `aOrigin` (the car), ranked by
// how path-like they are given the car's heading, and - once the lane graph
// is loaded - arrays of lanes (indices, pointers or UIDs) that form a chain
// of connected lanes, which is what a planned route looks like.
std::string ScanForPath(void* aRoot, size_t aRootSize, const char* aRootName, const RED4ext::Vector4& aOrigin,
                        const RED4ext::Vector4& aHeading, int aMaxDepth, std::filesystem::path& aOutPath);
// Traffic lane map (LaneMap.cpp): finds the lane table through the autodrive
// system and lists the lanes within aRadius of aPos to dumps/lanes_*.txt.
std::string LaneQuery(void* aAutodrive, const RED4ext::Vector4& aPos, float aRadius, std::filesystem::path& aOutPath);
// Loads Lanes() from the autodrive system; returns the lane count (0 = not found).
uint32_t LoadLanes(void* aAutodrive);
// Loads Lanes() unless it is loaded and still the game's current table.
bool EnsureLanes(void* aAutodrive);
// Repairs junctions wired to the wrong side of the road (LaneRepair.cpp):
// with aApply, once per lane table (later calls report the earlier result);
// without, restores the repaired lanes. Lists the changes in
// dumps/lane_repairs.txt. Returns a summary.
std::string RepairLanes(void* aAutodrive, bool aApply);
// Tests outside the game (no engine allocator) set this to append to a
// DynArray at `header` themselves.
inline void (*gArrayAppendOverride)(uintptr_t aHeader, const void* aValue, size_t aSize) = nullptr;
// Whether the repair changed lane i of the current table.
bool IsRepairedLane(uint32_t aLane);
// Lanes near aPos for the overlay (Natives.cpp encodes them).
struct LaneDraw
{
    uint32_t lane;
    uint32_t kind; // 1 road, 2 junction, 3 GPS-only, 4 no AI/autodrive, 5 pavement, 6 repaired
    bool deadEnd;   // drivable and nothing links out of its end
    std::vector<Point3> points;
};
std::vector<LaneDraw> LanesForDrawing(void* aAutodrive, const RED4ext::Vector4& aPos, float aRadius, bool aPavements);
// Writes every lane (flags, links, outline) to dumps/lanegraph.txt.
std::string DumpLaneGraph(void* aAutodrive, std::filesystem::path& aOutPath);

// The route the car is driving (Route.cpp): lane UIDs kept behind the
// autodrive system (autodrive+0x540->+0x30->+0x3C0, array at +0x20).
// Lanes of the route in order (resolved through Lanes(), which must be loaded).
std::vector<uint32_t> RouteLanes(void* aAutodrive);
// The route as one line: the lanes' outlines joined where their links are.
// aLaneOf, if given, gets the lane of each point.
std::vector<Point3> RouteLine(void* aAutodrive, std::vector<uint32_t>* aLaneOf);
// Points along the route from the car's position (projected onto it) for
// aAhead metres. Empty if there is no route or the car isn't near it.
std::vector<Point3> RouteAhead(void* aAutodrive, const RED4ext::Vector4& aPos, float aAhead);
// The speed to drive now so every bend in the next 400 m of the route can be
// taken at aLatAccel (m/s^2), braking at aDecel; and where the limiting bend is.
struct RoutePlan
{
    bool valid = false;     // the car is on a route
    float cap = 1000.0f;    // m/s
    float distance = -1.0f; // m to the limiting bend
    float radius = 0.0f;
    uint32_t points = 0;
};
RoutePlan PlanRouteSpeed(void* aAutodrive, const RED4ext::Vector4& aPos, float aLatAccel, float aDecel);
// Writes the route object and its arrays raw and decoded to dumps/route_*.txt.
std::string DumpRoute(void* aAutodrive, const RED4ext::Vector4& aPos, std::filesystem::path& aOutPath);

// The route to the map destination for the player (NavGuide.cpp), planned
// by the navigator (Navigator.hpp) whether Autodrive+ drives or not.
// Plans from aPos (in a car: heading aFwd; on foot: aFwd null) to aGoal.
// Returns "route: <summary>" or why not.
std::string NavPlan(void* aAutodrive, const Point3& aPos, const Point3* aFwd, const Point3& aGoal,
                    bool aTurnAround = false);
// The route from where the player is (aPos, projected onto it) for aAhead
// metres, a point every aStep metres.
std::vector<Point3> NavAhead(const Point3& aPos, float aAhead, float aStep);
// Where the player is on it, "key=value" pairs separated by ';': left (m to
// go), time (s, the planner's estimate), off (m from the line), and the next
// turn: turn (left, right, slight left, slight right, U-turn), in (m), angle
// (degrees, left positive). Empty without a route.
std::string NavStatus(const Point3& aPos);
void NavClear();
// A destination for a driving test: an ordinary road aMin..aMax metres away
// (straight line), reachable from the lane the car is on without a long way
// round. False if none was found.
bool NavRandom(void* aAutodrive, const Point3& aPos, const Point3& aFwd, float aMin, float aMax, uint32_t aSeed,
               Point3& aOut);
// Research: scans the game's GPS system (gamegpsGPSSystem) for lane routes
// like ScanForPath, to find where it keeps the route it draws on the map.
std::string ScanGPS(void* aAutodrive, const RED4ext::Vector4& aOrigin, const RED4ext::Vector4& aHeading,
                    std::filesystem::path& aOutPath);

// The car's control inputs (Controls.cpp): throttle, brake, steer, handbrake.
constexpr int kControlCount = 4;
// Where one input lives: at car+offset, or at offset into the object the
// pointer at car+ptrOffset points to; field = scale * input + bias.
struct ControlField
{
    int32_t ptrOffset = -1;
    int32_t offset = -1;
    float scale = 1.0f;
    float bias = 0.0f;
    bool Found() const { return offset >= 0; }
};
// Adds one sample of the car's memory against the player's inputs (a new
// car starts a new probe). Returns the sample count.
uint32_t ControlProbe(void* aVehicle, size_t aSize, const float aInputs[kControlCount]);
uint32_t ControlProbeReset();
// Ranks the fields that follow each input to dumps/controls_probe.txt and
// keeps the best (saved to controls.txt next to the DLL). Returns a summary.
std::string ControlProbeReport(std::filesystem::path& aOutPath);
std::string ControlFields();
// The inputs as the car holds them now (NaN where not found).
bool ReadControls(void* aVehicle, float aOut[kControlCount]);
// Writes these inputs into the car every frame for aSeconds (at most 5, so
// they stop if the caller does); aSeconds <= 0 lets go.
std::string SetControls(const RED4ext::Handle<RED4ext::IScriptable>& aVehicle, const float aValues[kControlCount],
                        float aSeconds);
std::string ControlStatus();
// Called every frame by the plugin's game state.
void ControlTick();
// Hooks the car's input reader so our inputs replace the player's right after
// it runs (at load; without it, ControlTick writes them once per frame).
bool InstallControlHook();

// BCAI Autodrive+, our own driving AI (Driver.hpp, Driver*.cpp).
struct DriverSettings
{
    float maxSpeed = 33.0f;        // m/s
    float latAccel = 6.9f;         // m/s^2 through bends
    float laneSpeedFactor = 1.5f;  // x the lane's traffic speed
    float decel = 4.0f;            // m/s^2 braking for bends and the destination
    float followTime = 1.5f;       // s behind the car ahead
    uint64_t car = 0;              // which car (what was learned about it restarts with another)
    // How it drives: 0 city (keeps right, passes only what stands still,
    // careful at junctions), 1 aggressive (also passes slow cars on the
    // other side of the road when it can see it is clear), 2 madman (takes
    // any lane and any gap, passes whenever nothing is coming).
    int style = 1;
};
struct DriverOutput
{
    float throttle = 0.0f, brake = 0.0f, steer = 0.0f, handbrake = 0.0f;
    float targetSpeed = 0.0f, speed = 0.0f, lateralError = 0.0f;
    int state = 0; // 0 driving, 1 arriving, 2 stopped (see DriverStatus), 3 stuck
};
// Plans a route from aPos (heading aForward) to aGoal, or along the road
// without one. Returns a summary.
std::string DriverStart(void* aAutodrive, const Point3& aPos, const Point3& aForward, const Point3* aGoal);
// One frame: the car's state. False once the driver has stopped.
bool DriverStep(const Point3& aPos, const Point3& aForward, const Point3& aVelocity, float aDt,
                const DriverSettings& aSettings, DriverOutput& aOut);
void DriverStop(const std::string& aWhy);
// Sensing: the rays to cast this frame (from just ahead of the car's front,
// aFront metres ahead of its centre, over the road ahead), kind 0 looking for
// vehicles, 1 for walls and other fixed things. DriverSense takes, per ray in
// the same order, where it hit (hit true) or its end.
struct DriverRay
{
    Point3 a, b;
    int kind;
};
std::vector<DriverRay> DriverRays(float aFront);
size_t DriverSense(const std::vector<Point3>& aEnds, const std::vector<bool>& aHit);
// The car's own body, measured when Autodrive+ starts: how far its front and
// rear are from its centre, half its width, and the height the forward rays
// should start at above its origin (m).
void DriverBody(float aFront, float aRear, float aHalfWidth, float aSensorHeight = 0.7f);
// The car's steering geometry from its records: wheelbase, steering lock in
// degrees, and mass. Used to keep Reach from exceeding what the wheel can do.
void DriverProfile(uintptr_t aCar, float aWheelbase, float aWheelTurnDeg, float aMass);
// Vehicles near the car as the game lists them (not just what the rays saw):
// where each is, which way it points and how it moves. Their size is learned
// from the rays that hit them. People on foot come in the same list (the
// rays don't see them).
struct DriverVehicle
{
    Point3 p, fwd, vel;
    bool parked = false; // the game says so, or nobody drives it
    bool bike = false;
    float length = 0.0f, width = 0.0f; // as its record gives them (0: not known)
    bool person = false; // someone on foot (their velocity is worked out from where they were listed)
};
// Returns how many vehicles it now follows.
size_t DriverVehicles(const std::vector<DriverVehicle>& aList);
// What the sensor remembers: points with kind (0 vehicle, 1 wall) and
// whether the point is what's in the way.
struct DriverSeenPoint
{
    Point3 p;
    int kind;
    bool blocking;
};
std::vector<DriverSeenPoint> DriverSeen();
struct DriverInfo
{
    float obstacle = -1.0f; // gap to what is in the way on the chosen path
    int kind = -1;          // what: 0 vehicle, 1 wall
    float offset = 0.0f;    // where it is heading, sideways from the line (left positive)
    float lateral = 0.0f;   // how far the car is off that
};
DriverInfo DriverState();
// The planned path ahead of the car's bumper, every aStep metres.
std::vector<Point3> DriverAhead(float aDistance, float aStep);
// The planned speed (m/s) at each of those points.
std::vector<float> DriverAheadSpeeds(float aDistance, float aStep);
// The road's edges either side of the path ahead (left, right pairs).
std::vector<Point3> DriverRoom(float aDistance, float aStep);
std::string DriverStatus();
// One line: speeds, what is ahead, steering and pedals, progress (km/h, m).
std::string DriverTelemetry();
// Metres Autodrive+ has driven off the mapped road this drive.
float DriverOffRoad();
// How far a car aHalf wide (each side), where this car is, reaches past the road's edge.
float DriverRoadOut(float aHalf);
// How often it chose a new sideways move of 2 m or more (lane changes and swerves).
int DriverBigMoves();
// How often it changed its mind: a sideways move of 1 m or more undone within 3 s.
int DriverDithers();
// What it is doing now, for the overlay: "key=value" pairs separated by ';'
// (speeds in km/h, distances in m). See Driver::Hud for the keys.
std::string DriverHud();
// What it decided since last asked (lane changes and why, passing, what holds
// the speed down, backing up, leaving the road ...), a line each: seconds into
// the drive, speed, metres along the route, what.
std::vector<std::string> DriverEvents();
// Writes the planned path and speed plan to dumps/driver_path.txt.
std::string DriverDumpPath(std::filesystem::path& aOutPath);
} // namespace BCAI
