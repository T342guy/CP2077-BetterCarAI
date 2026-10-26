// Autodrive+'s hands and feet. Finds where a car keeps its throttle,
// brake, steering and handbrake inputs by watching its memory while the
// player drives (the probe), then writes them every frame while Autodrive+
// drives (the controls).

#include "Plugin.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

namespace BCAI
{
namespace
{
const char* const kInputNames[kControlCount] = {"throttle", "brake", "steer", "handbrake"};
// The car itself and the first bytes of every object it points to.
constexpr size_t kMaxObject = 0x2000;
constexpr size_t kBlockBytes = 0x200;
constexpr size_t kMaxBlocks = 192;

// Running sums for one float's correlation with each input.
struct Series
{
    double sx = 0.0, sxx = 0.0, sxy[kControlCount]{};
    float lo = INFINITY, hi = -INFINITY;
    bool bad = false;
};

struct Block
{
    int32_t ptrOffset; // -1: the car itself
    uintptr_t ptr;
    size_t bytes;
    bool valid = true;
    std::vector<Series> series;
};

struct Probe
{
    uintptr_t vehicle = 0;
    size_t size = 0;
    std::vector<Block> blocks;
    double sy[kControlCount]{}, syy[kControlCount]{};
    uint32_t n = 0;
};

struct Drive
{
    RED4ext::WeakHandle<RED4ext::IScriptable> vehicle;
    float value[kControlCount]{};
    double until = 0.0;
    uint64_t frames = 0;
    uint64_t overwritten[kControlCount]{};
    float seen[kControlCount]{};
    float written[kControlCount]{};
    bool wrote = false;
};

std::mutex gMutex;
Probe gProbe;
Drive gDrive;
// Found by the control probe in 2.5 (they match the SDK's older layout); the
// probe can still find them again into controls.txt.
ControlField gFields[kControlCount] = {{-1, 0x264}, {-1, 0x268}, {-1, 0x278}, {-1, 0x26C}};
bool gFieldsLoaded = false;

// The car's input reader (vehicle::BaseObject, 2.5 at 0x1401BC03C): clears
// the input fields, then fills them from the player's input actions
// (Accelerate -> 0x264, Decelerate -> 0x268, Handbrake -> 0x26C,
// throttle - brake -> 0x270, TurnX -> 0x278). Cars without player input
// return after clearing. Found by its start, which writes 0x264 and 0x26C.
constexpr uint8_t kInputReaderStart[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x33, 0xFF,
                                         0x0F, 0x29, 0x7C, 0x24, 0x50, 0x0F, 0x28, 0xD9, 0x48, 0x89, 0xB9, 0x64,
                                         0x02, 0x00, 0x00, 0xB2, 0x01, 0x48, 0x89, 0xB9, 0x6C, 0x02, 0x00, 0x00};
// Derived inputs the reader also writes: throttle - brake, at two places.
constexpr int32_t kThrottleMinusBrake[] = {0x270, 0x280};
using InputReader = void (*)(void* aVehicle, float aDelta);
InputReader gReadInputs = nullptr;
bool gHooked = false;

double Now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path FieldsFile()
{
    return PluginDir() / "controls.txt";
}

void LoadFields()
{
    if (gFieldsLoaded)
        return;
    gFieldsLoaded = true;
    std::ifstream in(FieldsFile());
    std::string name;
    int32_t ptrOffset, offset;
    float scale, bias;
    while (in >> name >> ptrOffset >> std::hex >> offset >> std::dec >> scale >> bias)
    {
        for (int k = 0; k < kControlCount; ++k)
            if (name == kInputNames[k])
                gFields[k] = {ptrOffset, offset, scale, bias};
    }
}

void SaveFields()
{
    std::ofstream out(FieldsFile());
    for (int k = 0; k < kControlCount; ++k)
        if (gFields[k].Found())
            out << kInputNames[k] << ' ' << gFields[k].ptrOffset << ' ' << std::hex << gFields[k].offset << std::dec
                << ' ' << gFields[k].scale << ' ' << gFields[k].bias << '\n';
}

std::string Where(int32_t aPtrOffset, int32_t aOffset)
{
    return aPtrOffset < 0 ? Format("car+0x%X", aOffset) : Format("car+0x%X->+0x%X", aPtrOffset, aOffset);
}

// Address of a field of the car at aBase, or 0.
uintptr_t Resolve(uintptr_t aBase, const ControlField& aField)
{
    if (!aField.Found())
        return 0;
    if (aField.ptrOffset < 0)
        return aBase + aField.offset;
    uintptr_t ptr = 0;
    if (!SafeRead(reinterpret_cast<const void*>(aBase + aField.ptrOffset), &ptr, sizeof(ptr)) || ptr < 0x10000)
        return 0;
    return ptr + aField.offset;
}

bool InModule(uintptr_t aPtr)
{
    static uintptr_t start = 0, end = 0;
    if (!start)
    {
        auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        start = reinterpret_cast<uintptr_t>(module);
        end = start + nt->OptionalHeader.SizeOfImage;
    }
    return aPtr >= start && aPtr < end;
}

void StartProbe(uintptr_t aVehicle, size_t aSize)
{
    gProbe = Probe{};
    gProbe.vehicle = aVehicle;
    gProbe.size = std::clamp<size_t>(aSize, 0x100, kMaxObject) & ~size_t(3);
    std::vector<uint8_t> obj(gProbe.size);
    if (!SafeRead(reinterpret_cast<const void*>(aVehicle), obj.data(), obj.size()))
        return;
    gProbe.blocks.push_back({-1, aVehicle, gProbe.size, true, std::vector<Series>(gProbe.size / 4)});
    // Everything the car points to that can be read (skipping the game's
    // code and constant data, e.g. vtables): the inputs may live in a
    // physics or controller object of its own.
    std::vector<uint8_t> probe(kBlockBytes);
    for (size_t off = 0x40; off + 8 <= gProbe.size && gProbe.blocks.size() < kMaxBlocks + 1; off += 8)
    {
        uintptr_t ptr;
        std::memcpy(&ptr, obj.data() + off, sizeof(ptr));
        if (ptr < 0x10000 || ptr > 0x7FFFFFFFFFFF || (ptr & 7) || InModule(ptr))
            continue;
        bool seen = std::any_of(gProbe.blocks.begin(), gProbe.blocks.end(), [&](const Block& b) { return b.ptr == ptr; });
        if (seen || !SafeRead(reinterpret_cast<const void*>(ptr), probe.data(), probe.size()))
            continue;
        gProbe.blocks.push_back(
            {int32_t(off), ptr, kBlockBytes, true, std::vector<Series>(kBlockBytes / 4)});
    }
}

struct Candidate
{
    int32_t ptrOffset;
    int32_t offset;
    double r, slope, bias;
    float lo, hi;
};

// The fields that follow input k most closely, best first.
std::vector<Candidate> Rank(int k, size_t aKeep)
{
    std::vector<Candidate> out;
    const double n = gProbe.n;
    const double varY = n * gProbe.syy[k] - gProbe.sy[k] * gProbe.sy[k];
    if (gProbe.n < 10 || varY <= 1e-6 * n * n)
        return out;
    for (const auto& block : gProbe.blocks)
    {
        if (!block.valid)
            continue;
        for (size_t i = 0; i < block.series.size(); ++i)
        {
            const auto& s = block.series[i];
            if (s.bad)
                continue;
            double varX = n * s.sxx - s.sx * s.sx;
            if (varX <= 1e-9 * n * n)
                continue;
            double cov = n * s.sxy[k] - s.sx * gProbe.sy[k];
            double r = cov / std::sqrt(varX * varY);
            double slope = cov / varY;
            out.push_back({block.ptrOffset, int32_t(i * 4), r, slope, (s.sx - slope * gProbe.sy[k]) / n, s.lo, s.hi});
        }
    }
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return std::abs(a.r) > std::abs(b.r); });
    if (out.size() > aKeep)
        out.resize(aKeep);
    return out;
}

// An input's own field: follows it almost exactly, on a similar scale.
bool InputLike(const Candidate& c)
{
    return std::abs(c.r) >= 0.85 && std::abs(c.slope) >= 0.3 && std::abs(c.slope) <= 3.0 && c.hi - c.lo <= 6.0;
}
// Our inputs into the car at aBase (with gMutex held).
void WriteControls(uintptr_t aBase)
{
    for (int k = 0; k < kControlCount; ++k)
    {
        uintptr_t addr = Resolve(aBase, gFields[k]);
        float current;
        if (!addr || !SafeRead(reinterpret_cast<const void*>(addr), &current, sizeof(current)))
            continue;
        // Something else (the player's input handling) wrote it since our
        // last write: then our value only counts if we write after it.
        if (!gHooked && gDrive.wrote && current != gDrive.written[k])
        {
            ++gDrive.overwritten[k];
            gDrive.seen[k] = current;
        }
        float raw = gFields[k].bias + gFields[k].scale * gDrive.value[k];
        *reinterpret_cast<volatile float*>(addr) = raw;
        gDrive.written[k] = raw;
    }
    // The fields the reader derives, where the probe found the inputs the
    // reader writes.
    if (gHooked && gFields[0].ptrOffset < 0 && gFields[0].offset == 0x264 && gFields[1].offset == 0x268)
        for (int32_t off : kThrottleMinusBrake)
            *reinterpret_cast<volatile float*>(aBase + off) = gDrive.value[0] - gDrive.value[1];
    gDrive.wrote = true;
    ++gDrive.frames;
}

void ReadInputsHook(void* aVehicle, float aDelta)
{
    gReadInputs(aVehicle, aDelta);
    std::lock_guard lock(gMutex);
    if (gDrive.until == 0.0 || aVehicle != gDrive.vehicle.instance || Now() > gDrive.until)
        return;
    WriteControls(reinterpret_cast<uintptr_t>(aVehicle));
}

const uint8_t* FindInText(const uint8_t* aPattern, size_t aSize)
{
    auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if (std::memcmp(section->Name, ".text", 6) != 0)
            continue;
        const uint8_t* begin = module + section->VirtualAddress;
        const uint8_t* end = begin + section->Misc.VirtualSize;
        auto it = std::search(begin, end, aPattern, aPattern + aSize);
        if (it != end && std::search(it + 1, end, aPattern, aPattern + aSize) == end)
            return it;
        return nullptr; // not found, or not unique
    }
    return nullptr;
}
} // namespace

bool InstallControlHook()
{
    const uint8_t* target = FindInText(kInputReaderStart, sizeof(kInputReaderStart));
    if (!target)
    {
        Warn("car input reader not found: Autodrive+ falls back to writing once per frame");
        return false;
    }
    gHooked = gSdk->hooking->Attach(gHandle, const_cast<uint8_t*>(target), reinterpret_cast<void*>(&ReadInputsHook),
                                    reinterpret_cast<void**>(&gReadInputs));
    if (gHooked)
        Log("hooked the car input reader at exe+0x%llX",
            (unsigned long long)(target - reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr))));
    else
        Warn("could not hook the car input reader");
    return gHooked;
}

uint32_t ControlProbe(void* aVehicle, size_t aSize, const float aInputs[kControlCount])
{
    std::lock_guard lock(gMutex);
    auto base = reinterpret_cast<uintptr_t>(aVehicle);
    if (!base)
        return gProbe.n;
    if (base != gProbe.vehicle)
        StartProbe(base, aSize);
    if (gProbe.blocks.empty())
        return 0;
    std::vector<uint8_t> obj(gProbe.size), buf(kBlockBytes);
    if (!SafeRead(aVehicle, obj.data(), obj.size()))
        return gProbe.n;
    for (auto& block : gProbe.blocks)
    {
        if (!block.valid)
            continue;
        const uint8_t* data = obj.data();
        if (block.ptrOffset >= 0)
        {
            uintptr_t ptr;
            std::memcpy(&ptr, obj.data() + block.ptrOffset, sizeof(ptr));
            // Repointed or freed: its sums would mix two objects.
            if (ptr != block.ptr || !SafeRead(reinterpret_cast<const void*>(ptr), buf.data(), block.bytes))
            {
                block.valid = false;
                continue;
            }
            data = buf.data();
        }
        for (size_t i = 0; i < block.series.size(); ++i)
        {
            auto& s = block.series[i];
            if (s.bad)
                continue;
            float v;
            std::memcpy(&v, data + i * 4, sizeof(v));
            if (!std::isfinite(v) || std::abs(v) > 1e7f)
            {
                s.bad = true;
                continue;
            }
            s.sx += v;
            s.sxx += double(v) * v;
            for (int k = 0; k < kControlCount; ++k)
                s.sxy[k] += double(v) * aInputs[k];
            s.lo = std::min(s.lo, v);
            s.hi = std::max(s.hi, v);
        }
    }
    for (int k = 0; k < kControlCount; ++k)
    {
        gProbe.sy[k] += aInputs[k];
        gProbe.syy[k] += double(aInputs[k]) * aInputs[k];
    }
    return ++gProbe.n;
}

uint32_t ControlProbeReset()
{
    std::lock_guard lock(gMutex);
    uint32_t n = gProbe.n;
    gProbe = Probe{};
    return n;
}

std::string ControlProbeReport(std::filesystem::path& aOutPath)
{
    std::lock_guard lock(gMutex);
    LoadFields();
    aOutPath = DumpDir() / "controls_probe.txt";
    std::ofstream out(aOutPath);
    size_t valid = std::count_if(gProbe.blocks.begin(), gProbe.blocks.end(), [](const Block& b) { return b.valid; });
    out << "# Control probe: " << gProbe.n << " samples, car " << Format("%llX", (unsigned long long)gProbe.vehicle)
        << ", " << Format("0x%zX", gProbe.size) << " bytes, " << valid << "/" << gProbe.blocks.size()
        << " objects still pointed to at the end\n";
    out << "# field ~= slope * input + bias; r = correlation with the input\n";
    std::string summary = Format("%u samples:", gProbe.n);
    int found = 0;
    for (int k = 0; k < kControlCount; ++k)
    {
        auto ranked = Rank(k, 15);
        const double n = std::max<double>(gProbe.n, 1);
        out << "\n## " << kInputNames[k] << Format(" (input mean %.2f)", gProbe.sy[k] / n) << "\n";
        if (ranked.empty())
        {
            out << "never used while probing\n";
            summary += Format(" %s unused,", kInputNames[k]);
            continue;
        }
        for (const auto& c : ranked)
            out << Format("%-22s r %+.3f  slope %+.3f  bias %+.3f  range %.3f..%.3f%s\n",
                          Where(c.ptrOffset, c.offset).c_str(), c.r, c.slope, c.bias, c.lo, c.hi,
                          InputLike(c) ? "  input-like" : "");
        // The best input-like field; the car's own over one it points to
        // when they follow the input about as closely.
        const Candidate* best = nullptr;
        for (const auto& c : ranked)
        {
            if (!InputLike(c))
                continue;
            if (!best || (best->ptrOffset >= 0 && c.ptrOffset < 0 && std::abs(c.r) > std::abs(best->r) - 0.02))
                best = &c;
        }
        if (best)
        {
            gFields[k] = {best->ptrOffset, best->offset, float(best->slope), float(best->bias)};
            ++found;
            summary += Format(" %s %s (r %.2f),", kInputNames[k], Where(best->ptrOffset, best->offset).c_str(), best->r);
        }
        else
            summary += Format(" %s not found (best r %.2f),", kInputNames[k], ranked[0].r);
    }
    if (found)
        SaveFields();
    summary.pop_back();
    out << "\n# Chosen: " << summary << "\n";
    return summary;
}

std::string ControlFields()
{
    std::lock_guard lock(gMutex);
    LoadFields();
    std::string s;
    for (int k = 0; k < kControlCount; ++k)
    {
        const auto& f = gFields[k];
        s += f.Found() ? Format("%s %s x%.2f%+.2f", kInputNames[k], Where(f.ptrOffset, f.offset).c_str(), f.scale, f.bias)
                       : Format("%s ?", kInputNames[k]);
        if (k + 1 < kControlCount)
            s += ", ";
    }
    return s;
}

bool ReadControls(void* aVehicle, float aOut[kControlCount])
{
    std::lock_guard lock(gMutex);
    LoadFields();
    bool any = false;
    for (int k = 0; k < kControlCount; ++k)
    {
        aOut[k] = NAN;
        uintptr_t addr = Resolve(reinterpret_cast<uintptr_t>(aVehicle), gFields[k]);
        float raw;
        if (addr && SafeRead(reinterpret_cast<const void*>(addr), &raw, sizeof(raw)))
        {
            aOut[k] = gFields[k].scale != 0.0f ? (raw - gFields[k].bias) / gFields[k].scale : raw;
            any = true;
        }
    }
    return any;
}

std::string SetControls(const RED4ext::Handle<RED4ext::IScriptable>& aVehicle, const float aValues[kControlCount],
                        float aSeconds)
{
    std::lock_guard lock(gMutex);
    LoadFields();
    if (aSeconds <= 0.0f || !aVehicle)
    {
        gDrive.until = 0.0;
        return "released";
    }
    if (std::none_of(std::begin(gFields), std::end(gFields), [](const ControlField& f) { return f.Found(); }))
        return "controls not found yet: run the control probe";
    if (gDrive.until == 0.0 || gDrive.vehicle.instance != aVehicle.instance)
    {
        gDrive = Drive{};
        gDrive.vehicle = aVehicle;
    }
    std::copy(aValues, aValues + kControlCount, gDrive.value);
    gDrive.until = Now() + std::min(aSeconds, 5.0f);
    return "driving";
}

std::string ControlStatus()
{
    std::lock_guard lock(gMutex);
    std::string s = Format("%s, %s, %llu frames written", gHooked ? "hooked" : "not hooked",
                           gDrive.until > 0.0 ? "driving" : "idle", (unsigned long long)gDrive.frames);
    for (int k = 0; k < kControlCount && !gHooked; ++k)
        if (gFields[k].Found())
            s += Format("; %s changed by the game in %llu (last %.2f)", kInputNames[k],
                        (unsigned long long)gDrive.overwritten[k], gDrive.seen[k]);
    return s;
}

// Every frame (game state Running): Autodrive+'s inputs into the car.
void ControlTick()
{
    std::lock_guard lock(gMutex);
    if (gDrive.until == 0.0)
        return;
    auto vehicle = gDrive.vehicle.Lock();
    bool expired = Now() > gDrive.until;
    if (!vehicle || expired)
    {
        // Let go of the pedals and the wheel; the player's input takes over.
        if (vehicle && gDrive.wrote)
        {
            for (int k = 0; k < kControlCount; ++k)
            {
                uintptr_t addr = Resolve(reinterpret_cast<uintptr_t>(vehicle.instance), gFields[k]);
                float current;
                if (addr && SafeRead(reinterpret_cast<const void*>(addr), &current, sizeof(current)))
                    *reinterpret_cast<volatile float*>(addr) = gFields[k].bias;
            }
        }
        gDrive.until = 0.0;
        return;
    }
    // With the hook, the input reader writes our inputs at the right moment.
    if (!gHooked)
        WriteControls(reinterpret_cast<uintptr_t>(vehicle.instance));
}
} // namespace BCAI
