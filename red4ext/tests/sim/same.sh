#!/bin/bash
# same.sh BIN_A BIN_B [N]: whether two builds drive exactly the same (a
# refactor, logging): N city scenes on seed 7 and N/2 highway scenes, each car,
# each style, compared by the hash of every control output (BCAI_DIGEST).
# Build both with EXTRA_FLAGS=-ffp-contract=off (else the compiler may fuse
# multiply-adds differently in each and the hashes differ for that alone).
source "$(dirname "$0")/common.sh"
export ROUTE_D1 ROUTE_D2 ROUTE_H1 ROUTE_H2 ROUTE_H3
a=$1 b=$2 n=${3:-60}
jobs=$(mktemp)
for car in default hellhound maimai; do
  for style in city aggressive madman; do
    extra="BCAI_DIGEST=1 BCAI_LIST=1 BCAI_CAR=$car BCAI_STYLE=$style"
    { python3 -I "$SIMDIR/randtraffic.py" "$n" 7; python3 -I "$SIMDIR/hwytraffic.py" $((n / 2)) 3; } |
      awk -F'\t' -v OFS='\t' -v e="$extra" -v t="$car.$style." '{$1 = t $1; $2 = $2 " " e; print}' >> "$jobs"
  done
done
for bin in "$a" "$b"; do
  SIMBIN=$(realpath "$bin") "$SIMDIR/pool.sh" < "$jobs" | sed 's/ |.*digest / /' > "$jobs.$(basename "$(dirname "$bin")")"
done
fa="$jobs.$(basename "$(dirname "$a")")" fb="$jobs.$(basename "$(dirname "$b")")"
total=$(wc -l < "$fa")
differ=$(diff "$fa" "$fb" | grep -c '^<')
echo "$total drives, $differ differ"
diff "$fa" "$fb" | head -20
rm -f "$jobs" "$fa" "$fb"
