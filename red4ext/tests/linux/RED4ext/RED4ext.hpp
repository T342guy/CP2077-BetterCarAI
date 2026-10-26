#pragma once
// Stand-ins for the few RED4ext SDK types the driver and lane code mention,
// for the native Linux build of the driving simulator. Nothing here talks to
// the game: logging goes nowhere, arrays are only read (the simulator grows
// them with its own realloc).
#include <cstdint>
#include <cstdlib>

namespace RED4ext
{
struct Vector4
{
    float X, Y, Z, W;
};

struct IScriptable;

template<typename T>
struct Handle
{
    T* instance = nullptr;
    void* refCount = nullptr;
};

template<typename T>
struct DynArray
{
    T* entries = nullptr;
    uint32_t capacity = 0;
    uint32_t count = 0;

    uint32_t size() const { return count; }
    void PushBack(const T&) { std::abort(); } // the simulator sets gArrayAppendOverride
};

namespace v1
{
using PluginHandle = void*;

struct Logger
{
    template<typename... Args>
    void InfoF(PluginHandle, const char*, Args...) const
    {
    }
    template<typename... Args>
    void WarnF(PluginHandle, const char*, Args...) const
    {
    }
};

struct Sdk
{
    Logger* logger = nullptr;
};
} // namespace v1
} // namespace RED4ext
