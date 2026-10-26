#!/bin/bash
# crossings.sh: where roads really cross on the two city routes (from the
# plan dump's crossing flag), for randtraffic.py's crossing cars.
source "$(dirname "$0")/common.sh"
for r in D1 D2; do
  route=ROUTE_$r
  "$SIMDIR/sim.sh" ${!route} > /dev/null
  awk '!/^#/ && $15==1 {if ($1-last>15) printf "%d ", $1; last=$1}' "$RED4EXT/build-linux/dumps/driver_path.txt" > "$SIMDIR/crossings_$r.txt"
  echo "$r: $(cat "$SIMDIR/crossings_$r.txt")"
done
