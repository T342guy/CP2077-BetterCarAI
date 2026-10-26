#!/bin/bash
# sim.sh <route args> [obstacle distance]: one simulated drive (env passes through:
# BCAI_CARS, BCAI_FAST, BCAI_LEAD, BCAI_OBST_L, BCAI_TELE, BCAI_TRACE ...).
source "$(dirname "$0")/common.sh"
cd "$(dirname "$SIMBIN")" && "$SIMBIN" "$BCAI_LANEGRAPH" drive "$@" 2>&1
