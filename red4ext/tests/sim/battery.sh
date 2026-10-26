#!/bin/bash
# battery.sh label [cars]: the whole set for each car (default: default
# hellhound maimai), with the game's vehicle list (LIST=0: rays only):
# single obstacles, city traffic on seeds 7 and 11, highway traffic.
# One summary line each, with touches by kind; kept in $OUT/battery_<label>.txt.
source "$(dirname "$0")/common.sh"
label=$1; shift
f="$OUT/battery_$label.txt"
: > "$f"
for car in ${@:-default hellhound maimai}; do
  export BCAI_CAR=$car
  if [[ "${LIST:-1}" == 1 ]]; then export BCAI_LIST=1; else unset BCAI_LIST; fi
  echo "== $car" | tee -a "$f"
  EXTRA="BCAI_CAR=$car ${BCAI_LIST:+BCAI_LIST=1}" "$SIMDIR/score.sh" "${label}_$car" | tee -a "$f"
  for s in 7 11; do SEED=$s "$SIMDIR/traffic_score.sh" "${label}_${car}_s$s" | tee -a "$f"; done
  "$SIMDIR/hwy_score.sh" "${label}_${car}_h" | tee -a "$f"
done
