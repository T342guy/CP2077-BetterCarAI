#!/usr/bin/env python3
# randpeople.py N [seed]: N random city scenes with people on foot as pool jobs
# (label<TAB>env<TAB>route args), on the two city routes as the navigator
# drives them (road_D1.txt, road_D2.txt: from crosswalks.sh). Each has 2-6
# people: crossing at a crosswalk or mid-block (stepping off the pavement when
# this car is 25-90 m away), walking along the pavement by the kerb, or
# standing at the kerb by a crosswalk. Styles city, aggressive and madman in
# turn.
import os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
n = int(sys.argv[1]); rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
routes = {}
for r in ("D1", "D2"):
    road = [tuple(float(x) for x in line.split()) for line in open(os.path.join(here, f"road_{r}.txt"))]
    routes[r] = (os.environ[f"ROUTE_{r}"], road)
styles = ["city", "aggressive", "madman"]
for i in range(n):
    name = rng.choice(list(routes))
    args, road = routes[name]
    usable = [p for p in road if 60 < p[0] < road[-1][0] - 100]
    walks = [p for p in usable if p[3] == 1]
    people = []
    for _ in range(rng.randint(2, 6)):
        kind = rng.random()
        side = rng.choice([-1, 1])
        if kind < 0.6:      # crossing: at a crosswalk (most), or anywhere
            at, hi, lo, _ = rng.choice(walks) if walks and rng.random() < 0.6 else rng.choice(usable)
            edge, far = (hi + 1.0, lo - 1.0) if side > 0 else (lo - 1.0, hi + 1.0)
            people.append(f"{at + rng.uniform(0, 4):.0f},{edge + side * 1.0:.1f},0,{-side * rng.uniform(1.2, 1.8):.1f},"
                          f"{rng.uniform(25, 90):.0f},{far - side * 1.0:.1f}")
        elif kind < 0.85:   # along the pavement, either way
            at, hi, lo, _ = rng.choice(usable)
            edge = hi + 1.0 if side > 0 else lo - 1.0
            people.append(f"{at:.0f},{edge + side * 1.5:.1f},{rng.choice([-1.4, 1.4]):.1f},0")
        else:               # standing at the kerb, by a crosswalk if there is one
            at, hi, lo, _ = rng.choice(walks or usable)
            edge = hi + 1.0 if side > 0 else lo - 1.0
            people.append(f"{at + rng.uniform(-3, 3):.0f},{edge + side * 0.8:.1f},0,0")
    env = f"BCAI_PEOPLE={';'.join(people)} BCAI_STYLE={styles[i % 3]}"
    print(f"P{i:04d} {name}\t{env}\t{args}")
