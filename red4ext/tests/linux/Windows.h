#pragma once
// The little of <Windows.h> the lane code uses, for the native Linux build of
// the driving simulator (tests/build_linux_sim.sh).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

using SIZE_T = size_t;

// The simulator's lane table is its own memory: writing to it can't fault.
inline void* GetCurrentProcess()
{
    return nullptr;
}

inline int WriteProcessMemory(void*, void* aDst, const void* aSrc, SIZE_T aSize, SIZE_T* aWritten)
{
    std::memcpy(aDst, aSrc, aSize);
    if (aWritten)
        *aWritten = aSize;
    return 1;
}

union LARGE_INTEGER
{
    long long QuadPart;
};

inline int QueryPerformanceFrequency(LARGE_INTEGER* aOut)
{
    aOut->QuadPart = 1000000000LL;
    return 1;
}

inline int QueryPerformanceCounter(LARGE_INTEGER* aOut)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    aOut->QuadPart = ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}

struct SYSTEMTIME
{
    uint16_t wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

inline void GetLocalTime(SYSTEMTIME* aOut)
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    *aOut = {uint16_t(tm.tm_year + 1900), uint16_t(tm.tm_mon + 1), uint16_t(tm.tm_wday), uint16_t(tm.tm_mday),
             uint16_t(tm.tm_hour), uint16_t(tm.tm_min), uint16_t(tm.tm_sec), 0};
}
