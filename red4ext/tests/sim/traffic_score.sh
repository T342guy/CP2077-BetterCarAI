#!/bin/bash
# traffic_score.sh label [N]: N random city traffic scenes (SEED, default 7),
# summed; touches by kind follow. Results kept in $OUT/traffic_<label>.txt.
source "$(dirname "$0")/common.sh"
# The scenes are laid out along the routes the 0.27 planner took (its
# crossings, its multi-lane road): keep those.
export BCAI_ROUTES=${BCAI_ROUTES-0.27}
export ROUTE_D1 ROUTE_D2
f="$OUT/traffic_${1// /_}.txt"
python3 -I "$SIMDIR/randtraffic.py" ${2:-400} ${SEED:-7} | "$SIMDIR/pool.sh" > "$f"
n=$(wc -l < "$f"); hit=$(grep -c ': [1-9][0-9]* contacts' "$f"); arr=$(grep -c 'state 1: arrived' "$f")
avg=$(grep -o 'average [0-9]*' "$f" | awk '{s+=$2} END {printf "%.0f", s/NR}')
mind=$(grep -o '[0-9]* changes of mind' "$f" | awk '{s+=$1} END {printf "%.2f", NR ? s/NR : 0}')
ped=$("$SIMDIR/pedals.sh" "$f")
off=$(grep -o 'off the road: [0-9]*' "$f" | awk '{s+=$4} END {printf "%d", s}')
echo "$1: $n scenes, $hit with contact, $((n-arr)) not arrived, average $avg km/h, $off m off the road, $mind changes of mind per drive; $ped"
"$SIMDIR/classify.sh" "$f"
