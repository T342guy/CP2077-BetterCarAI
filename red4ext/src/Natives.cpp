// Global functions callable from redscript and CET (declared in
// BetterCarAINative.reds). Dumps go to red4ext/plugins/BetterCarAI/dumps.

#include "Plugin.hpp"

#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace BCAI
{
namespace
{
using RED4ext::CStackFrame;
using RED4ext::IScriptable;

std::string Narrow(const std::filesystem::path& aPath)
{
    return aPath.string();
}

// BCAI_NativeVersion() -> Int32: 10000 * major + 100 * minor + patch.
void NativeVersion(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    aFrame->code++; // ParamEnd
    if (aOut)
        *aOut = BCAI_VERSION_MAJOR * 10000 + BCAI_VERSION_MINOR * 100 + BCAI_VERSION_PATCH;
}

// BCAI_DumpTypes(filters: String) -> String (summary and file path)
void NativeDumpTypes(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::CString filters;
    RED4ext::GetParameter(aFrame, &filters);
    aFrame->code++;
    std::filesystem::path path;
    int count = DumpTypes(filters.c_str(), path);
    if (aOut)
        *aOut = Format("%d types -> %s", count, Narrow(path).c_str()).c_str();
}

// BCAI_DumpNatives(filters: String) -> String (summary and file path)
void NativeDumpNatives(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::CString filters;
    RED4ext::GetParameter(aFrame, &filters);
    aFrame->code++;
    std::filesystem::path path;
    int count = DumpNativeFunctions(filters.c_str(), path);
    if (aOut)
        *aOut = Format("%d native functions -> %s", count, Narrow(path).c_str()).c_str();
}

// BCAI_LaneQuery(autodrive: ref<IScriptable>, pos: Vector4, radius: Float) -> String
void NativeLaneQuery(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{};
    float radius = 50.0f;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &radius);
    aFrame->code++;
    std::filesystem::path path;
    std::string summary = autodrive ? LaneQuery(autodrive.instance, pos, radius, path) : "no autodrive system";
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

// BCAI_LoadLanes(autodrive: ref<IScriptable>) -> Int32 (lane count, 0 = not found)
void NativeLoadLanes(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::GetParameter(aFrame, &autodrive);
    aFrame->code++;
    uint32_t count = autodrive ? LoadLanes(autodrive.instance) : 0;
    if (aOut)
        *aOut = int32_t(count);
}

// BCAI_DumpRoute(autodrive: ref<IScriptable>, pos: Vector4) -> String
void NativeDumpRoute(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{};
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    aFrame->code++;
    std::filesystem::path path;
    std::string summary = autodrive ? DumpRoute(autodrive.instance, pos, path) : "no autodrive system";
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

// BCAI_RouteAhead(autodrive: ref<IScriptable>, pos: Vector4, ahead: Float) -> array<Vector4>
void NativeRouteAhead(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{};
    float ahead = 100.0f;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &ahead);
    aFrame->code++;
    if (!aOut || !autodrive)
        return;
    for (auto& p : RouteAhead(autodrive.instance, pos, std::clamp(ahead, 0.0f, 2000.0f)))
        aOut->PushBack({p.x, p.y, p.z, 1.0f});
}

// BCAI_RoutePlan(autodrive: ref<IScriptable>, pos: Vector4, latAccel: Float, decel: Float) -> Vector4:
// X = speed cap (m/s; -1 = not on a route), Y = metres to the limiting bend,
// Z = its radius, W = route points ahead.
void NativeRoutePlan(IScriptable*, CStackFrame* aFrame, RED4ext::Vector4* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{};
    float latAccel = 8.0f, decel = 4.0f;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &latAccel);
    RED4ext::GetParameter(aFrame, &decel);
    aFrame->code++;
    RoutePlan plan;
    if (autodrive)
        plan = PlanRouteSpeed(autodrive.instance, pos, std::max(latAccel, 0.5f), std::max(decel, 0.5f));
    if (aOut)
        *aOut = {plan.valid ? plan.cap : -1.0f, plan.distance, plan.radius, float(plan.points)};
}

// BCAI_RepairLanes(autodrive: ref<IScriptable>, apply: Bool) -> String
void NativeRepairLanes(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    bool apply = true;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &apply);
    aFrame->code++;
    std::string summary = autodrive ? RepairLanes(autodrive.instance, apply) : "no autodrive system";
    if (aOut)
        *aOut = summary.c_str();
}

// BCAI_LanesNear(autodrive: ref<IScriptable>, pos: Vector4, radius: Float, pavements: Bool) -> array<Vector4>
// Per lane: a header (kind, point count, lane index, 1 if a dead end), then its points.
void NativeLanesNear(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{};
    float radius = 60.0f;
    bool pavements = false;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &radius);
    RED4ext::GetParameter(aFrame, &pavements);
    aFrame->code++;
    if (!aOut || !autodrive)
        return;
    for (auto& d : LanesForDrawing(autodrive.instance, pos, std::clamp(radius, 1.0f, 300.0f), pavements))
    {
        aOut->PushBack({float(d.kind), float(d.points.size()), float(d.lane), d.deadEnd ? 1.0f : 0.0f});
        for (auto& p : d.points)
            aOut->PushBack({p.x, p.y, p.z, 1.0f});
    }
}

// BCAI_DumpLaneGraph(autodrive: ref<IScriptable>) -> String
void NativeDumpLaneGraph(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::GetParameter(aFrame, &autodrive);
    aFrame->code++;
    std::filesystem::path path;
    std::string summary = autodrive ? DumpLaneGraph(autodrive.instance, path) : "no autodrive system";
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

// BCAI_DumpObject(obj: ref<IScriptable>, depth: Int32) -> String (file path)
void NativeDumpObject(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> obj;
    int32_t depth = 1;
    RED4ext::GetParameter(aFrame, &obj);
    RED4ext::GetParameter(aFrame, &depth);
    aFrame->code++;
    if (!obj)
    {
        if (aOut)
            *aOut = "no object";
        return;
    }
    std::string text = DumpObject(obj.instance, depth);
    auto* cls = obj->GetType();
    SYSTEMTIME now;
    GetLocalTime(&now);
    auto path = DumpDir() / Format("object_%s_%02d%02d%02d.txt", cls ? cls->GetName().ToString() : "unknown",
                                   now.wHour, now.wMinute, now.wSecond);
    std::ofstream(path) << text;
    Log("object dump -> %ls", path.c_str());
    if (aOut)
        *aOut = Narrow(path).c_str();
}

// BCAI_ScanForPath(obj: ref<IScriptable>, name: String, origin: Vector4, heading: Vector4, depth: Int32) -> String
void NativeScanForPath(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> obj;
    RED4ext::CString name;
    RED4ext::Vector4 origin{};
    RED4ext::Vector4 heading{};
    int32_t depth = 3;
    RED4ext::GetParameter(aFrame, &obj);
    RED4ext::GetParameter(aFrame, &name);
    RED4ext::GetParameter(aFrame, &origin);
    RED4ext::GetParameter(aFrame, &heading);
    RED4ext::GetParameter(aFrame, &depth);
    aFrame->code++;
    std::filesystem::path path;
    auto* cls = obj ? obj->GetType() : nullptr;
    std::string summary =
        ScanForPath(obj.instance, cls ? cls->size : 0x100, name.c_str(), origin, heading, depth, path);
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

// Control inputs as a Vector4: X throttle, Y brake, Z steer, W handbrake.
void ToArray(const RED4ext::Vector4& aIn, float aOut[kControlCount])
{
    aOut[0] = aIn.X;
    aOut[1] = aIn.Y;
    aOut[2] = aIn.Z;
    aOut[3] = aIn.W;
}

// BCAI_ControlProbe(vehicle: ref<IScriptable>, inputs: Vector4) -> Int32 (samples so far)
void NativeControlProbe(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> vehicle;
    RED4ext::Vector4 inputs{};
    RED4ext::GetParameter(aFrame, &vehicle);
    RED4ext::GetParameter(aFrame, &inputs);
    aFrame->code++;
    float values[kControlCount];
    ToArray(inputs, values);
    auto* cls = vehicle ? vehicle->GetType() : nullptr;
    uint32_t n = vehicle ? ControlProbe(vehicle.instance, cls ? cls->size : 0x100, values) : 0;
    if (aOut)
        *aOut = int32_t(n);
}

// BCAI_ControlProbeReset() -> Int32 (samples dropped)
void NativeControlProbeReset(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    aFrame->code++;
    uint32_t n = ControlProbeReset();
    if (aOut)
        *aOut = int32_t(n);
}

// BCAI_ControlProbeReport() -> String
void NativeControlProbeReport(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    std::filesystem::path path;
    std::string summary = ControlProbeReport(path);
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

// BCAI_ControlFields() -> String
void NativeControlFields(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = ControlFields().c_str();
}

// BCAI_ReadControls(vehicle: ref<IScriptable>) -> Vector4 (-99 where not found)
void NativeReadControls(IScriptable*, CStackFrame* aFrame, RED4ext::Vector4* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> vehicle;
    RED4ext::GetParameter(aFrame, &vehicle);
    aFrame->code++;
    float values[kControlCount];
    if (!vehicle || !ReadControls(vehicle.instance, values))
        std::fill(std::begin(values), std::end(values), NAN);
    for (auto& v : values)
        if (!std::isfinite(v))
            v = -99.0f;
    if (aOut)
        *aOut = {values[0], values[1], values[2], values[3]};
}

// BCAI_SetControls(vehicle: ref<IScriptable>, controls: Vector4, seconds: Float) -> String
void NativeSetControls(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> vehicle;
    RED4ext::Vector4 controls{};
    float seconds = 0.0f;
    RED4ext::GetParameter(aFrame, &vehicle);
    RED4ext::GetParameter(aFrame, &controls);
    RED4ext::GetParameter(aFrame, &seconds);
    aFrame->code++;
    float values[kControlCount];
    ToArray(controls, values);
    std::string result = SetControls(vehicle, values, seconds);
    if (aOut)
        *aOut = result.c_str();
}

// BCAI_ControlStatus() -> String
void NativeControlStatus(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = ControlStatus().c_str();
}

Point3 ToPoint(const RED4ext::Vector4& v)
{
    return {v.X, v.Y, v.Z};
}

// BCAI_DriverStart(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, goal: Vector4, hasGoal: Bool) -> String
void NativeDriverStart(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{}, forward{}, goal{};
    bool hasGoal = false;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &forward);
    RED4ext::GetParameter(aFrame, &goal);
    RED4ext::GetParameter(aFrame, &hasGoal);
    aFrame->code++;
    Point3 g = ToPoint(goal);
    std::string result = DriverStart(autodrive.instance, ToPoint(pos), ToPoint(forward), hasGoal ? &g : nullptr);
    if (aOut)
        *aOut = result.c_str();
}

// The driving style set by BCAI_DriverStyle (0 city, 1 aggressive, 2 madman).
int gDriverStyle = 1;

// BCAI_DriverStyle(style: Int32) -> Int32: the style Autodrive+ drives in
// from the next frame; returns it.
void NativeDriverStyle(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    int32_t style = 1;
    RED4ext::GetParameter(aFrame, &style);
    aFrame->code++;
    gDriverStyle = std::clamp(style, 0, 2);
    if (aOut)
        *aOut = gDriverStyle;
}

// BCAI_DriverHud() -> String: what Autodrive+ is doing, as "key=value;..." pairs.
void NativeDriverHud(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = DriverHud().c_str();
}

// BCAI_DriverEvents() -> String: what Autodrive+ decided since last asked, a
// line each ("" when nothing).
void NativeDriverEvents(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    std::string all;
    for (const std::string& e : DriverEvents())
        all += e + "\n";
    *aOut = all.c_str();
}

// BCAI_DriverStep(vehicle: ref<IScriptable>, pos: Vector4, forward: Vector4, velocity: Vector4, dt: Float,
//                 settings: Vector4, followTime: Float) -> Vector4
void NativeDriverStep(IScriptable*, CStackFrame* aFrame, RED4ext::Vector4* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> vehicle;
    RED4ext::Vector4 pos{}, forward{}, velocity{}, settings{};
    float dt = 0.016f, followTime = 1.5f;
    RED4ext::GetParameter(aFrame, &vehicle);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &forward);
    RED4ext::GetParameter(aFrame, &velocity);
    RED4ext::GetParameter(aFrame, &dt);
    RED4ext::GetParameter(aFrame, &settings);
    RED4ext::GetParameter(aFrame, &followTime);
    aFrame->code++;
    DriverSettings set;
    set.maxSpeed = settings.X;
    set.latAccel = settings.Y;
    set.laneSpeedFactor = settings.Z;
    set.decel = settings.W;
    set.followTime = followTime;
    set.car = reinterpret_cast<uintptr_t>(vehicle.instance);
    set.style = gDriverStyle;
    DriverOutput out;
    bool running = vehicle && DriverStep(ToPoint(pos), ToPoint(forward), ToPoint(velocity), dt, set, out);
    float controls[kControlCount] = {out.throttle, out.brake, out.steer, out.handbrake};
    // Stopped: let go (the player's inputs take over at once).
    SetControls(vehicle, controls, running ? 0.3f : 0.0f);
    if (aOut)
        *aOut = {out.targetSpeed, out.lateralError, out.steer, float(running ? out.state : std::max(out.state, 2))};
}

// BCAI_DriverRays(front: Float) -> array<Vector4>: pairs of start (W: 0 look
// for vehicles, 1 for walls) and end.
void NativeDriverRays(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    float front = 2.4f;
    RED4ext::GetParameter(aFrame, &front);
    aFrame->code++;
    if (!aOut)
        return;
    for (const auto& ray : DriverRays(front))
    {
        aOut->PushBack({ray.a.x, ray.a.y, ray.a.z, float(ray.kind)});
        aOut->PushBack({ray.b.x, ray.b.y, ray.b.z, 1.0f});
    }
}

// BCAI_DriverSense(ends: array<Vector4>) -> Int32: per ray where it hit (W 1)
// or its end (W 0). Returns how many points the sensor remembers.
void NativeDriverSense(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::DynArray<RED4ext::Vector4> ends;
    RED4ext::GetParameter(aFrame, &ends);
    aFrame->code++;
    std::vector<Point3> points;
    std::vector<bool> hit;
    for (const auto& end : ends)
    {
        points.push_back(ToPoint(end));
        hit.push_back(end.W > 0.5f);
    }
    size_t count = DriverSense(points, hit);
    if (aOut)
        *aOut = int32_t(count);
}

// BCAI_DriverBody(body: Vector4) -> Bool: the car's own body, measured when
// Autodrive+ starts: X its front and Y its rear from its centre, Z half its
// width, W the height its forward rays start at above its origin (m).
void NativeDriverBody(IScriptable*, CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::Vector4 body{};
    RED4ext::GetParameter(aFrame, &body);
    aFrame->code++;
    DriverBody(body.X, body.Y, body.Z, body.W);
    if (aOut)
        *aOut = true;
}

// BCAI_DriverProfile(vehicle: ref<IScriptable>, profile: Vector4) -> Bool:
// X wheelbase (m), Y steering lock (degrees), Z mass (kg). Keeps the learned
// Reach from exceeding the wheel geometry.
void NativeDriverProfile(IScriptable*, CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::Handle<RED4ext::IScriptable> vehicle;
    RED4ext::Vector4 profile{};
    RED4ext::GetParameter(aFrame, &vehicle);
    RED4ext::GetParameter(aFrame, &profile);
    aFrame->code++;
    DriverProfile(reinterpret_cast<uintptr_t>(vehicle.instance), profile.X, profile.Y, profile.Z);
    if (aOut)
        *aOut = vehicle != nullptr;
}

// BCAI_DriverVehicles(list: array<Vector4>) -> Int32: the vehicles near the
// car, three entries each: position (W: 1 parked, +2 a bike, +4 a person on
// foot), forward (W: its length, 0 not known), velocity (W: its width).
// Returns how many vehicles (and people) the driver follows.
void NativeDriverVehicles(IScriptable*, CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::DynArray<RED4ext::Vector4> list;
    RED4ext::GetParameter(aFrame, &list);
    aFrame->code++;
    std::vector<DriverVehicle> vehicles;
    for (uint32_t i = 0; i + 2 < list.size(); i += 3)
    {
        int flags = int(list[i].W + 0.5f);
        vehicles.push_back({ToPoint(list[i]), ToPoint(list[i + 1]), ToPoint(list[i + 2]), (flags & 1) != 0,
                            (flags & 2) != 0, list[i + 1].W, list[i + 2].W, (flags & 4) != 0});
    }
    size_t count = DriverVehicles(vehicles);
    if (aOut)
        *aOut = int32_t(count);
}

// BCAI_DriverSeen() -> array<Vector4>: what the sensor remembers; W 1 a
// vehicle, 2 a wall, plus 2 for the one in the way; 5 a person on foot, 6
// the one in the way.
void NativeDriverSeen(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    for (const auto& seen : DriverSeen())
        aOut->PushBack({seen.p.x, seen.p.y, seen.p.z,
                        seen.kind == 2 ? (seen.blocking ? 6.0f : 5.0f) : float(seen.kind + 1 + (seen.blocking ? 2 : 0))});
}

// BCAI_DriverInfo() -> Vector4: X gap to what is in the way (< 0 nothing),
// Y what (0 vehicle, 1 wall), Z where it is heading sideways from its line,
// W how far off that it is (m, left +).
void NativeDriverInfo(IScriptable*, CStackFrame* aFrame, RED4ext::Vector4* aOut, int64_t)
{
    aFrame->code++;
    DriverInfo info = DriverState();
    if (aOut)
        *aOut = {info.obstacle, float(info.kind), info.offset, info.lateral};
}

// BCAI_DriverRoom(distance: Float, step: Float) -> array<Vector4>: the road's
// left and right edges along the path ahead, in pairs.
void NativeDriverRoom(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    float distance = 60.0f, step = 4.0f;
    RED4ext::GetParameter(aFrame, &distance);
    RED4ext::GetParameter(aFrame, &step);
    aFrame->code++;
    if (!aOut)
        return;
    for (const auto& p : DriverRoom(distance, std::max(step, 0.5f)))
        aOut->PushBack({p.x, p.y, p.z, 1.0f});
}

// BCAI_DriverAhead(distance: Float, step: Float) -> array<Vector4>
void NativeDriverAhead(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    float distance = 60.0f, step = 4.0f;
    RED4ext::GetParameter(aFrame, &distance);
    RED4ext::GetParameter(aFrame, &step);
    aFrame->code++;
    if (!aOut)
        return;
    // (W: the planned speed there, m/s.)
    auto points = DriverAhead(distance, std::max(step, 0.5f));
    auto speeds = DriverAheadSpeeds(distance, std::max(step, 0.5f));
    for (size_t i = 0; i < points.size(); ++i)
        aOut->PushBack({points[i].x, points[i].y, points[i].z, i < speeds.size() ? speeds[i] : 1.0f});
}

// BCAI_DriverStop(why: String) -> String
void NativeDriverStop(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::CString why;
    RED4ext::GetParameter(aFrame, &why);
    aFrame->code++;
    DriverStop(why.c_str());
    float zero[kControlCount] = {};
    SetControls({}, zero, 0.0f);
    if (aOut)
        *aOut = DriverStatus().c_str();
}

// BCAI_DriverStatus() -> String
void NativeDriverStatus(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = DriverStatus().c_str();
}

// BCAI_DriverDumpPath() -> String
void NativeDriverDumpPath(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    std::filesystem::path path;
    std::string result = DriverDumpPath(path);
    if (aOut)
        *aOut = result.c_str();
}

// BCAI_DriverTelemetry() -> String
void NativeDriverTelemetry(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = DriverTelemetry().c_str();
}

// BCAI_NavPlan(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, goal: Vector4, inCar: Bool) -> String:
// the route to the map destination ("route: ..." or why not).
void NativeNavPlan(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{}, forward{}, goal{};
    bool inCar = false;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &forward);
    RED4ext::GetParameter(aFrame, &goal);
    RED4ext::GetParameter(aFrame, &inCar);
    aFrame->code++;
    Point3 f = ToPoint(forward);
    // (forward.W 1: the player drives and may turn round.)
    std::string result = autodrive ? NavPlan(autodrive.instance, ToPoint(pos), inCar ? &f : nullptr, ToPoint(goal),
                                             forward.W > 0.5f)
                                   : "no autodrive system";
    if (aOut)
        *aOut = result.c_str();
}

// BCAI_NavRandom(autodrive: ref<IScriptable>, pos: Vector4, forward: Vector4, minDist: Float, maxDist: Float,
// seed: Int32) -> Vector4: a destination for a driving test (W 1), or W 0.
void NativeNavRandom(IScriptable*, CStackFrame* aFrame, RED4ext::Vector4* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 pos{}, forward{};
    float minDist = 800.0f, maxDist = 2000.0f;
    int32_t seed = 0;
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &forward);
    RED4ext::GetParameter(aFrame, &minDist);
    RED4ext::GetParameter(aFrame, &maxDist);
    RED4ext::GetParameter(aFrame, &seed);
    aFrame->code++;
    Point3 dest{};
    bool found = autodrive && NavRandom(autodrive.instance, ToPoint(pos), ToPoint(forward), minDist, maxDist,
                                        uint32_t(seed), dest);
    if (aOut)
        *aOut = {dest.x, dest.y, dest.z, found ? 1.0f : 0.0f};
}

// BCAI_NavAhead(pos: Vector4, ahead: Float, step: Float) -> array<Vector4>
void NativeNavAhead(IScriptable*, CStackFrame* aFrame, RED4ext::DynArray<RED4ext::Vector4>* aOut, int64_t)
{
    RED4ext::Vector4 pos{};
    float ahead = 150.0f, step = 2.0f;
    RED4ext::GetParameter(aFrame, &pos);
    RED4ext::GetParameter(aFrame, &ahead);
    RED4ext::GetParameter(aFrame, &step);
    aFrame->code++;
    if (!aOut)
        return;
    // (Up to 20 km: the driving test saves the whole route.)
    for (const Point3& p : NavAhead(ToPoint(pos), std::clamp(ahead, 0.0f, 20000.0f), std::max(step, 1.0f)))
        aOut->PushBack({p.x, p.y, p.z, 1.0f});
}

// BCAI_NavStatus(pos: Vector4) -> String (see NavStatus)
void NativeNavStatus(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Vector4 pos{};
    RED4ext::GetParameter(aFrame, &pos);
    aFrame->code++;
    if (aOut)
        *aOut = NavStatus(ToPoint(pos)).c_str();
}

// BCAI_NavClear() -> Bool
void NativeNavClear(IScriptable*, CStackFrame* aFrame, bool* aOut, int64_t)
{
    aFrame->code++;
    NavClear();
    if (aOut)
        *aOut = true;
}

// BCAI_ScanGPS(autodrive: ref<IScriptable>, origin: Vector4, heading: Vector4) -> String
void NativeScanGPS(IScriptable*, CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    RED4ext::Handle<IScriptable> autodrive;
    RED4ext::Vector4 origin{}, heading{};
    RED4ext::GetParameter(aFrame, &autodrive);
    RED4ext::GetParameter(aFrame, &origin);
    RED4ext::GetParameter(aFrame, &heading);
    aFrame->code++;
    std::filesystem::path path;
    std::string summary = ScanGPS(autodrive.instance, origin, heading, path);
    if (aOut)
        *aOut = (summary + " -> " + Narrow(path)).c_str();
}

template<typename T>
RED4ext::CGlobalFunction* Make(const char* aName, RED4ext::ScriptingFunction_t<T> aFunc)
{
    auto* func = RED4ext::CGlobalFunction::Create(aName, aName, aFunc);
    func->flags = {.isNative = true, .isStatic = true};
    return func;
}
} // namespace

void RegisterNatives()
{
    auto* rtti = RED4ext::CRTTISystem::Get();

    auto* version = Make("BCAI_NativeVersion", &NativeVersion);
    version->SetReturnType("Int32");
    rtti->RegisterFunction(version);

    auto* types = Make("BCAI_DumpTypes", &NativeDumpTypes);
    types->AddParam("String", "filters");
    types->SetReturnType("String");
    rtti->RegisterFunction(types);

    auto* natives = Make("BCAI_DumpNatives", &NativeDumpNatives);
    natives->AddParam("String", "filters");
    natives->SetReturnType("String");
    rtti->RegisterFunction(natives);

    auto* lanes = Make("BCAI_LaneQuery", &NativeLaneQuery);
    lanes->AddParam("handle:IScriptable", "autodrive");
    lanes->AddParam("Vector4", "pos");
    lanes->AddParam("Float", "radius");
    lanes->SetReturnType("String");
    rtti->RegisterFunction(lanes);

    auto* loadLanes = Make("BCAI_LoadLanes", &NativeLoadLanes);
    loadLanes->AddParam("handle:IScriptable", "autodrive");
    loadLanes->SetReturnType("Int32");
    rtti->RegisterFunction(loadLanes);

    auto* dumpRoute = Make("BCAI_DumpRoute", &NativeDumpRoute);
    dumpRoute->AddParam("handle:IScriptable", "autodrive");
    dumpRoute->AddParam("Vector4", "pos");
    dumpRoute->SetReturnType("String");
    rtti->RegisterFunction(dumpRoute);

    auto* routeAhead = Make("BCAI_RouteAhead", &NativeRouteAhead);
    routeAhead->AddParam("handle:IScriptable", "autodrive");
    routeAhead->AddParam("Vector4", "pos");
    routeAhead->AddParam("Float", "ahead");
    routeAhead->SetReturnType("array:Vector4");
    rtti->RegisterFunction(routeAhead);

    auto* routePlan = Make("BCAI_RoutePlan", &NativeRoutePlan);
    routePlan->AddParam("handle:IScriptable", "autodrive");
    routePlan->AddParam("Vector4", "pos");
    routePlan->AddParam("Float", "latAccel");
    routePlan->AddParam("Float", "decel");
    routePlan->SetReturnType("Vector4");
    rtti->RegisterFunction(routePlan);

    auto* repair = Make("BCAI_RepairLanes", &NativeRepairLanes);
    repair->AddParam("handle:IScriptable", "autodrive");
    repair->AddParam("Bool", "apply");
    repair->SetReturnType("String");
    rtti->RegisterFunction(repair);

    auto* lanesNear = Make("BCAI_LanesNear", &NativeLanesNear);
    lanesNear->AddParam("handle:IScriptable", "autodrive");
    lanesNear->AddParam("Vector4", "pos");
    lanesNear->AddParam("Float", "radius");
    lanesNear->AddParam("Bool", "pavements");
    lanesNear->SetReturnType("array:Vector4");
    rtti->RegisterFunction(lanesNear);

    auto* laneGraph = Make("BCAI_DumpLaneGraph", &NativeDumpLaneGraph);
    laneGraph->AddParam("handle:IScriptable", "autodrive");
    laneGraph->SetReturnType("String");
    rtti->RegisterFunction(laneGraph);

    auto* object = Make("BCAI_DumpObject", &NativeDumpObject);
    object->AddParam("handle:IScriptable", "obj");
    object->AddParam("Int32", "depth");
    object->SetReturnType("String");
    rtti->RegisterFunction(object);

    auto* scan = Make("BCAI_ScanForPath", &NativeScanForPath);
    scan->AddParam("handle:IScriptable", "obj");
    scan->AddParam("String", "name");
    scan->AddParam("Vector4", "origin");
    scan->AddParam("Vector4", "heading");
    scan->AddParam("Int32", "depth");
    scan->SetReturnType("String");
    rtti->RegisterFunction(scan);

    auto* probe = Make("BCAI_ControlProbe", &NativeControlProbe);
    probe->AddParam("handle:IScriptable", "vehicle");
    probe->AddParam("Vector4", "inputs");
    probe->SetReturnType("Int32");
    rtti->RegisterFunction(probe);

    auto* probeReset = Make("BCAI_ControlProbeReset", &NativeControlProbeReset);
    probeReset->SetReturnType("Int32");
    rtti->RegisterFunction(probeReset);

    auto* probeReport = Make("BCAI_ControlProbeReport", &NativeControlProbeReport);
    probeReport->SetReturnType("String");
    rtti->RegisterFunction(probeReport);

    auto* fields = Make("BCAI_ControlFields", &NativeControlFields);
    fields->SetReturnType("String");
    rtti->RegisterFunction(fields);

    auto* readControls = Make("BCAI_ReadControls", &NativeReadControls);
    readControls->AddParam("handle:IScriptable", "vehicle");
    readControls->SetReturnType("Vector4");
    rtti->RegisterFunction(readControls);

    auto* setControls = Make("BCAI_SetControls", &NativeSetControls);
    setControls->AddParam("handle:IScriptable", "vehicle");
    setControls->AddParam("Vector4", "controls");
    setControls->AddParam("Float", "seconds");
    setControls->SetReturnType("String");
    rtti->RegisterFunction(setControls);

    auto* controlStatus = Make("BCAI_ControlStatus", &NativeControlStatus);
    controlStatus->SetReturnType("String");
    rtti->RegisterFunction(controlStatus);

    auto* driverStart = Make("BCAI_DriverStart", &NativeDriverStart);
    driverStart->AddParam("handle:IScriptable", "autodrive");
    driverStart->AddParam("Vector4", "pos");
    driverStart->AddParam("Vector4", "forward");
    driverStart->AddParam("Vector4", "goal");
    driverStart->AddParam("Bool", "hasGoal");
    driverStart->SetReturnType("String");
    rtti->RegisterFunction(driverStart);

    auto* driverStep = Make("BCAI_DriverStep", &NativeDriverStep);
    driverStep->AddParam("handle:IScriptable", "vehicle");
    driverStep->AddParam("Vector4", "pos");
    driverStep->AddParam("Vector4", "forward");
    driverStep->AddParam("Vector4", "velocity");
    driverStep->AddParam("Float", "dt");
    driverStep->AddParam("Vector4", "settings");
    driverStep->AddParam("Float", "followTime");
    driverStep->SetReturnType("Vector4");
    rtti->RegisterFunction(driverStep);

    auto* driverAhead = Make("BCAI_DriverAhead", &NativeDriverAhead);
    driverAhead->AddParam("Float", "distance");
    driverAhead->AddParam("Float", "step");
    driverAhead->SetReturnType("array:Vector4");
    rtti->RegisterFunction(driverAhead);

    auto* driverRays = Make("BCAI_DriverRays", &NativeDriverRays);
    driverRays->AddParam("Float", "front");
    driverRays->SetReturnType("array:Vector4");
    rtti->RegisterFunction(driverRays);

    auto* driverSense = Make("BCAI_DriverSense", &NativeDriverSense);
    driverSense->AddParam("array:Vector4", "ends");
    driverSense->SetReturnType("Int32");
    rtti->RegisterFunction(driverSense);

    auto* driverBody = Make("BCAI_DriverBody", &NativeDriverBody);
    driverBody->AddParam("Vector4", "body");
    driverBody->SetReturnType("Bool");
    rtti->RegisterFunction(driverBody);

    auto* driverProfile = Make("BCAI_DriverProfile", &NativeDriverProfile);
    driverProfile->AddParam("handle:IScriptable", "vehicle");
    driverProfile->AddParam("Vector4", "profile");
    driverProfile->SetReturnType("Bool");
    rtti->RegisterFunction(driverProfile);

    auto* driverVehicles = Make("BCAI_DriverVehicles", &NativeDriverVehicles);
    driverVehicles->AddParam("array:Vector4", "list");
    driverVehicles->SetReturnType("Int32");
    rtti->RegisterFunction(driverVehicles);

    auto* driverSeen = Make("BCAI_DriverSeen", &NativeDriverSeen);
    driverSeen->SetReturnType("array:Vector4");
    rtti->RegisterFunction(driverSeen);

    auto* driverInfo = Make("BCAI_DriverInfo", &NativeDriverInfo);
    driverInfo->SetReturnType("Vector4");
    rtti->RegisterFunction(driverInfo);

    auto* driverRoom = Make("BCAI_DriverRoom", &NativeDriverRoom);
    driverRoom->AddParam("Float", "distance");
    driverRoom->AddParam("Float", "step");
    driverRoom->SetReturnType("array:Vector4");
    rtti->RegisterFunction(driverRoom);

    auto* driverStop = Make("BCAI_DriverStop", &NativeDriverStop);
    driverStop->AddParam("String", "why");
    driverStop->SetReturnType("String");
    rtti->RegisterFunction(driverStop);

    auto* driverStatus = Make("BCAI_DriverStatus", &NativeDriverStatus);
    driverStatus->SetReturnType("String");
    rtti->RegisterFunction(driverStatus);

    auto* driverDump = Make("BCAI_DriverDumpPath", &NativeDriverDumpPath);
    driverDump->SetReturnType("String");
    rtti->RegisterFunction(driverDump);

    auto* driverTelemetry = Make("BCAI_DriverTelemetry", &NativeDriverTelemetry);
    driverTelemetry->SetReturnType("String");
    rtti->RegisterFunction(driverTelemetry);

    auto* driverStyle = Make("BCAI_DriverStyle", &NativeDriverStyle);
    driverStyle->AddParam("Int32", "style");
    driverStyle->SetReturnType("Int32");
    rtti->RegisterFunction(driverStyle);

    auto* driverHud = Make("BCAI_DriverHud", &NativeDriverHud);
    driverHud->SetReturnType("String");
    rtti->RegisterFunction(driverHud);

    auto* driverEvents = Make("BCAI_DriverEvents", &NativeDriverEvents);
    driverEvents->SetReturnType("String");
    rtti->RegisterFunction(driverEvents);

    auto* navPlan = Make("BCAI_NavPlan", &NativeNavPlan);
    navPlan->AddParam("handle:IScriptable", "autodrive");
    navPlan->AddParam("Vector4", "pos");
    navPlan->AddParam("Vector4", "forward");
    navPlan->AddParam("Vector4", "goal");
    navPlan->AddParam("Bool", "inCar");
    navPlan->SetReturnType("String");
    rtti->RegisterFunction(navPlan);

    auto* navRandom = Make("BCAI_NavRandom", &NativeNavRandom);
    navRandom->AddParam("handle:IScriptable", "autodrive");
    navRandom->AddParam("Vector4", "pos");
    navRandom->AddParam("Vector4", "forward");
    navRandom->AddParam("Float", "minDist");
    navRandom->AddParam("Float", "maxDist");
    navRandom->AddParam("Int32", "seed");
    navRandom->SetReturnType("Vector4");
    rtti->RegisterFunction(navRandom);

    auto* navAhead = Make("BCAI_NavAhead", &NativeNavAhead);
    navAhead->AddParam("Vector4", "pos");
    navAhead->AddParam("Float", "ahead");
    navAhead->AddParam("Float", "step");
    navAhead->SetReturnType("array:Vector4");
    rtti->RegisterFunction(navAhead);

    auto* navStatus = Make("BCAI_NavStatus", &NativeNavStatus);
    navStatus->AddParam("Vector4", "pos");
    navStatus->SetReturnType("String");
    rtti->RegisterFunction(navStatus);

    auto* navClear = Make("BCAI_NavClear", &NativeNavClear);
    navClear->SetReturnType("Bool");
    rtti->RegisterFunction(navClear);

    auto* scanGps = Make("BCAI_ScanGPS", &NativeScanGPS);
    scanGps->AddParam("handle:IScriptable", "autodrive");
    scanGps->AddParam("Vector4", "origin");
    scanGps->AddParam("Vector4", "heading");
    scanGps->SetReturnType("String");
    rtti->RegisterFunction(scanGps);

    Log("natives registered");
}
} // namespace BCAI
