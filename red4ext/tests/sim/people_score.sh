#!/bin/bash
# people_score.sh label [N]: N random city scenes with people on foot (SEED,
# default 7; randpeople.py), each driven again without the people: people hit
# (with the first hit's speed; how many it could have stopped for: from
# where they set off, half a second to react and 6 m/s^2 braking, less if the
# car can't), how close it came to them, and the time the
# people cost. Results kept in $OUT/people_<label>.txt and people_<label>_none.txt.
source "$(dirname "$0")/common.sh"
export ROUTE_D1 ROUTE_D2
f="$OUT/people_${1// /_}.txt"
python3 -I "$SIMDIR/randpeople.py" ${2:-300} ${SEED:-7} > "$f.jobs"
"$SIMDIR/pool.sh" < "$f.jobs" > "$f"
sed 's/BCAI_PEOPLE=[^ \t]*//' "$f.jobs" | "$SIMDIR/pool.sh" > "${f%.txt}_none.txt"
rm -f "$f.jobs"
n=$(wc -l < "$f"); hit=$(grep -c ': [1-9][0-9]* hit' "$f"); arr=$(grep -c 'state 1: arrived' "$f")
hits=$(grep -o '[0-9]* hit,' "$f" | awk '{s+=$1} END {print s+0}')
could=$(grep -o 'hits it could have stopped for' "$f" | wc -l); could=$(grep -o '[0-9]* hits it could have stopped for' "$f" | awk '{s+=$1} END {print s+0}')
near=$(grep -o 'hit, closest [0-9.]*' "$f" | awk '$3<1.0 {c++} END {print c+0}')
speeds=$(grep -o 'hit a person, speed [0-9.]*' "$f" | awk '{printf "%s%.0f", (NR>1 ? " " : ""), $5}')
lost=$(paste -d'\n' <(grep -o 'ended after [0-9]*' "$f" | awk '{print $3}') <(grep -o 'ended after [0-9]*' "${f%.txt}_none.txt" | awk '{print $3}') |
  paste - - | awk '{a+=$1; b+=$2} END {printf "%.1f%%", b ? 100*(a-b)/b : 0}')
echo "$1: $n scenes, $hit with a person hit ($hits hits, $could it could have stopped for; at km/h: ${speeds:--}), $near passed within 1 m, $((n-arr)) not arrived, the people cost $lost time"
i=0
for s in city aggressive madman; do
  echo "  $s: $(awk -v i=$i '(NR-1)%3==i' "$f" | grep -c ': [1-9][0-9]* hit') scenes with a hit, $(awk -v i=$i '(NR-1)%3==i' "$f" | grep -o 'average [0-9]*' | awk '{s+=$2} END {printf "%.0f", s/NR}') km/h on average"
  i=$((i+1))
done
