#!/usr/bin/env bash
# Builds red4ext/build-linux/drive_sim: the driving simulator (scan_harness's
# drive mode) as a native Linux program, without Wine. Needs g++ (or set CXX).
#   build-linux/drive_sim lanegraph.txt drive x0 y0 hx hy [x1 y1 | cruise] [stopAt]
# OUTBIN: build somewhere else (e.g. a baseline); EXTRA_FLAGS: more compiler
# flags (-ffp-contract=off for builds whose BCAI_DIGEST should match exactly).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
out=${OUTBIN:-build-linux/drive_sim}
mkdir -p "$(dirname "$out")"
"${CXX:-g++}" ${EXTRA_FLAGS:-} -std=c++20 -O2 -march=native -DNDEBUG -DNOMINMAX \
  -DBCAI_VERSION_MAJOR=0 -DBCAI_VERSION_MINOR=0 -DBCAI_VERSION_PATCH=0 \
  -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas \
  -Itests/linux -Itests -Isrc \
  tests/scan_harness.cpp src/Driver*.cpp src/Navigator.cpp src/LaneMap.cpp src/LaneRepair.cpp \
  -o "$out"
echo "Built $(realpath "$out")"
