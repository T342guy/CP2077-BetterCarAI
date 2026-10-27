cmake_minimum_required(VERSION 3.21)

project(BetterCarAI LANGUAGES CXX VERSION 0.31.2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_subdirectory(deps/RED4ext.SDK)
# Correctly-cased include names for the SDK (made by build.sh).
target_include_directories(RED4ext.SDK BEFORE PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/casefix)

add_library(BetterCarAI SHARED
  src/Main.cpp
  src/Natives.cpp
  src/RttiDump.cpp
  src/MemScan.cpp
  src/LaneMap.cpp
  src/Route.cpp
  src/LaneRepair.cpp
  src/Controls.cpp
  src/Driver.cpp
  src/DriverRoute.cpp
  src/DriverSense.cpp
  src/DriverLanes.cpp
  src/DriverReport.cpp
  src/Navigator.cpp
  src/NavGuide.cpp
)
target_link_libraries(BetterCarAI PRIVATE RED4ext.SDK)
target_compile_definitions(BetterCarAI PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
  BCAI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} BCAI_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  BCAI_VERSION_PATCH=${PROJECT_VERSION_PATCH})
# The SDK's headers trip a lot of clang warnings that MSVC doesn't have.
# /EHa: hardware exceptions (access violations) reach __try/__except and
# catch(...). With /EHsc clang-cl silently drops __except around calls it
# thinks can't fault (tested under Wine).
target_compile_options(BetterCarAI PRIVATE /EHa -Wno-microsoft-cast -Wno-invalid-offsetof -Wno-unused-parameter
  -Wno-pragma-pack -Wno-nonportable-include-path)

# Scanner crash test (run under Wine): build/scan_harness.exe
add_executable(scan_harness EXCLUDE_FROM_ALL tests/scan_harness.cpp src/MemScan.cpp src/LaneMap.cpp src/Route.cpp src/LaneRepair.cpp src/Controls.cpp src/Driver.cpp src/DriverRoute.cpp src/DriverSense.cpp src/DriverLanes.cpp src/DriverReport.cpp src/Navigator.cpp src/NavGuide.cpp src/Main.cpp)
target_link_libraries(scan_harness PRIVATE RED4ext.SDK)
target_compile_definitions(scan_harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX BCAI_VERSION_MAJOR=0 BCAI_VERSION_MINOR=0
  BCAI_VERSION_PATCH=0)
target_compile_options(scan_harness PRIVATE /EHa -Wno-microsoft-cast -Wno-invalid-offsetof -Wno-unused-parameter)
