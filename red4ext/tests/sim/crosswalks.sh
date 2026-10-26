#!/bin/bash
# crosswalks.sh: the road along the two city routes as the navigator drives
# them (from the plan dump), for randpeople.py: every 5 m the room either side
# of the line (hi, lo) and whether a crosswalk crosses it there (any of the 5 m).
source "$(dirname "$0")/common.sh"
for r in D1 D2; do
  route=ROUTE_$r
  "$SIMDIR/sim.sh" ${!route} > /dev/null
  awk '!/^#/ {k=int($1/5); if (!(k in hi)) {hi[k]=$10; lo[k]=$9; n=k}; if ($16==1) cw[k]=1}
       END {for (k=0; k<=n; k++) printf "%d %.1f %.1f %d\n", k*5, hi[k], lo[k], (k in cw)}' \
    "$RED4EXT/build-linux/dumps/driver_path.txt" > "$SIMDIR/road_$r.txt"
  echo "$r: $(wc -l < "$SIMDIR/road_$r.txt") points, $(awk '$4==1' "$SIMDIR/road_$r.txt" | wc -l) with a crosswalk"
done
