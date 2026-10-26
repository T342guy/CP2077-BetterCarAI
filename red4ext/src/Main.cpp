// Better Car AI - RED4ext plugin.
// Step 1 ("see inside"): script-callable tools to look at the native vehicle
// AI - type dumps, live object dumps and a memory scan for the planned path.
// The mod works without this plugin; its scripts only use the natives when
// BetterCarAINative.reds (shipped next to the DLL) has been compiled in.

#include "Plugin.hpp"

#include <Windows.h>

namespace BCAI
{
std::filesystem::path PluginDir()
{
    static std::filesystem::path dir = []
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&PluginDir), &module);
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }();
    return dir;
}

namespace
{
bool StateNoop(RED4ext::CGameApplication*)
{
    return true;
}

// Every frame while the game runs.
bool StateRunningUpdate(RED4ext::CGameApplication*)
{
    ControlTick();
    return false;
}
} // namespace

std::filesystem::path DumpDir()
{
    auto dir = PluginDir() / "dumps";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
} // namespace BCAI

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        BCAI::gHandle = aHandle;
        BCAI::gSdk = aSdk;
        RED4ext::CRTTISystem::Get()->AddPostRegisterCallback(BCAI::RegisterNatives);
        // Autodrive+ writes the car's inputs every frame.
        static RED4ext::v1::GameState running{&BCAI::StateNoop, &BCAI::StateRunningUpdate, &BCAI::StateNoop};
        if (!aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &running))
            BCAI::Warn("could not add the per-frame update");
        BCAI::InstallControlHook();
        // Declarations of our natives, compiled into the game's scripts only
        // while this plugin is loaded.
        auto reds = BCAI::PluginDir() / "BetterCarAINative.reds";
        if (!aSdk->scripts->Add(aHandle, reds.c_str()))
            BCAI::Warn("could not add %ls to the script compilation", reds.c_str());
        BCAI::Log("Better Car AI plugin %d.%d.%d loaded from %ls", BCAI_VERSION_MAJOR, BCAI_VERSION_MINOR,
                  BCAI_VERSION_PATCH, BCAI::PluginDir().c_str());
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
        break;
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"BetterCarAI";
    aInfo->author = L"T342";
    aInfo->version = RED4EXT_V1_SEMVER(BCAI_VERSION_MAJOR, BCAI_VERSION_MINOR, BCAI_VERSION_PATCH);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
