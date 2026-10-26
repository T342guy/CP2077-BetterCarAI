#!/bin/bash
# hwy_score.sh label [N]: N random highway scenes (SEED, default 3): touches,
# metres off the road, sideways moves of 2 m or more per drive.
source "$(dirname "$0")/common.sh"
export ROUTE_H1 ROUTE_H2 ROUTE_H3
f="$OUT/hwy_${1// /_}.txt"
python3 -I "$SIMDIR/hwytraffic.py" ${2:-200} ${SEED:-3} | "$SIMDIR/pool.sh" > "$f"
n=$(wc -l < "$f"); hit=$(grep -c ': [1-9][0-9]* contacts' "$f"); arr=$(grep -c 'state 1: arrived' "$f")
off=$(grep -o 'off the road: [0-9]*' "$f" | awk '{s+=$4; if ($4>20) k++} END {printf "%d m in all, %d scenes over 20 m", s, k}')
mv=$(grep -o '[0-9]* moves of 2 m' "$f" | awk '{s+=$1; if ($1>m) m=$1} END {printf "%.1f (most %d)", s/NR, m}')
avg=$(grep -o 'average [0-9]*' "$f" | awk '{s+=$2} END {printf "%.0f", s/NR}')
mind=$(grep -o '[0-9]* changes of mind' "$f" | awk '{s+=$1} END {printf "%.2f", NR ? s/NR : 0}')
ped=$("$SIMDIR/pedals.sh" "$f")
echo "$1: $n scenes, $hit with contact, $((n-arr)) not arrived, off road $off, $mv big moves per drive, average $avg km/h, $mind changes of mind per drive; $ped"
