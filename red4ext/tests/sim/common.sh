# Shared settings for the simulator scripts (sourced).
# BCAI_LANEGRAPH: the lane map dumped from the game; SIMBIN: the simulator
# (default: the native Linux build); JOBS: runs at a time; OUT: where result
# files go.
SIMDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RED4EXT=$(cd "$SIMDIR/../.." && pwd)
: "${BCAI_LANEGRAPH:=/run/media/system/NVME256/SteamLibrary/steamapps/common/Cyberpunk 2077/red4ext/plugins/BetterCarAI/dumps/lanegraph.txt}"
: "${SIMBIN:=$RED4EXT/build-linux/drive_sim}"
: "${JOBS:=24}"
: "${OUT:=$RED4EXT/build-linux/sim-results}"
mkdir -p "$OUT"
# The two city test routes and three highway routes: start x y, heading x y, goal x y.
ROUTE_D1="-540 3100 -0.18 1 -891 2412"
ROUTE_D2="-891 2412 -1 1 -1881 1464"
ROUTE_H1="-1940 1160 0.47 0.88 34 -214"
ROUTE_H2="-559 1934 -1.00 -0.01 -2018 -1212"
ROUTE_H3="-700 -1046 -0.81 0.58 -315 -2167"
