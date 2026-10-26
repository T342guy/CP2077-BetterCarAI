#!/bin/bash
# navdrive.sh N SEED [BASEBIN]: drives N random trips (navtrips.py) with
# SIMBIN and with BASEBIN (a build with another route planner), and compares
# how long they took to arrive. Without traffic: this is about the routes.
source "$(dirname "$0")/common.sh"
n=${1:-100}; seed=${2:-1}; base=${3:?a baseline drive_sim}
python3 "$SIMDIR/navtrips.py" "$n" "$seed" | awk -v b="$base" -v s="$SIMBIN" '{printf "%d\t%s\t%s\n%d\t%s\t%s\n", NR, b, $0, NR, s, $0}' |
  xargs -P "$JOBS" -d '\n' -n 1 bash -c '
    IFS=$'"'"'\t'"'"' read -r k bin args <<< "$0"
    cd "$(dirname "$bin")"
    r=$("$bin" "'"$BCAI_LANEGRAPH"'" drive $args 2>&1 | grep -E "^ended after|^  end \(" | tr "\n" " ")
    echo "$k $bin | $r"' > "$OUT/navdrive_$seed.txt"
python3 - "$OUT/navdrive_$seed.txt" "$base" <<'PY'
import re, sys, collections
rows = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    k, rest = line.split(" ", 1)
    bin_, r = rest.split(" | ", 1)
    m = re.search(r"ended after (\d+) s, (\d+) m, state (\d)", r)
    g = re.search(r"([\d.]+) m from the goal", r)
    rows[int(k)]["base" if bin_ == sys.argv[2] else "new"] = (int(m.group(1)), int(m.group(2)), int(m.group(3)), float(g.group(1))) if m and g else None
both = [(v["base"], v["new"]) for v in rows.values() if v.get("base") and v.get("new") and v["base"][2] == 1 and v["new"][2] == 1]
fail = {k: sum(1 for v in rows.values() if not v.get(k) or v[k][2] != 1) for k in ("base", "new")}
print(f"{len(rows)} trips, both arrived on {len(both)}; did not arrive: base {fail['base']}, new {fail['new']}")
for i, name in ((0, "base"), (1, "new ")):
    t = sum(b[i][0] for b in both) / max(len(both), 1)
    d = sum(b[i][1] for b in both) / max(len(both), 1)
    gap = sum(b[i][3] for b in both) / max(len(both), 1)
    print(f"  {name}: {t:.0f} s, {d:.0f} m on average, stops {gap:.1f} m from the goal")
faster = sum(1 for b in both if b[1][0] < b[0][0] - 5); slower = sum(1 for b in both if b[1][0] > b[0][0] + 5)
print(f"  new faster by over 5 s on {faster} trips, slower on {slower}")
worst = sorted(both, key=lambda b: b[0][0] - b[1][0])[:3]
print("  worst for new (base s -> new s):", ", ".join(f"{b[0][0]} -> {b[1][0]}" for b in worst))
PY
