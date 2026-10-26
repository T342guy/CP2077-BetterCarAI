#!/bin/bash
# sweep.sh: an obstacle every ${STEP:-25} m along both city routes (ENVS: env
# for every run). Lines "D1 300 state 1 0c 4.2", or with SUMMARY=1 a summary.
source "$(dirname "$0")/common.sh"
step=${STEP:-25}
tmp=$(mktemp)
{ for d in $(seq 100 $step 2350); do printf 'D1 %s\t%s\t%s\n' $d "${ENVS:-X=1}" "$ROUTE_D1 $d"; done
  for d in $(seq 100 $step 3200); do printf 'D2 %s\t%s\t%s\n' $d "${ENVS:-X=1}" "$ROUTE_D2 $d"; done; } |
  "$SIMDIR/pool.sh" | sed -E 's/^(D[12] [0-9]+) \| (state [0-9]).*\| [0-9]+ other cars: ([0-9]+) contacts, closest centre-to-centre ([0-9.]+).*/\1 \2 \3c \4/' > "$tmp"
if [[ -n "$SUMMARY" ]]; then
  n=$(wc -l < "$tmp"); clean=$(grep -c 'state 1 0c' "$tmp")
  hits=$(grep -c ' [1-9][0-9]*c ' "$tmp"); stuck=$(grep -vc 'state 1' "$tmp")
  echo "${ENVS:-X=1}: $clean/$n clean, $hits with contact, $stuck not arrived"
else
  cat "$tmp"
fi
rm -f "$tmp"
