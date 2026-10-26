#!/usr/bin/env python3
# randtraffic.py N [seed]: N random city traffic scenes as pool jobs
# (label<TAB>env<TAB>route args). Cars along the route: parked (sometimes half
# in the lane), slow, keeping pace in the other lane, oncoming, and crossing at
# the route's real crossings (crossings_D1.txt, crossings_D2.txt: from
# crossings.sh), setting off when this car comes near.
import os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
n = int(sys.argv[1]); rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
routes = {"D1": (os.environ["ROUTE_D1"], 2400, False),
          "D2": (os.environ["ROUTE_D2"], 3200, True)}
crossings = {r: [float(x) for x in open(os.path.join(here, f"crossings_{r}.txt")).read().split()] for r in routes}
for i in range(n):
    name = rng.choice(list(routes))
    args, length, multilane = routes[name]
    cars = []
    for _ in range(rng.randint(3, 8)):
        at = rng.uniform(60, length - 150)
        kind = rng.random()
        if kind < 0.25:    # parked, maybe sticking into the lane
            cars.append(f"{at:.0f},{rng.choice([0, 0, 1.5, -1.5, 2.5]):.1f},0")
        elif kind < 0.45:  # slow
            cars.append(f"{at:.0f},0,{rng.uniform(2, 7):.1f}")
        elif kind < 0.6:   # same direction, other lane on the wide road, keeping pace-ish
            lat = -3.5 if multilane else 0
            cars.append(f"{at:.0f},{lat},{rng.uniform(8, 15):.1f}")
        elif kind < 0.85:  # oncoming, in its own lane
            lat = 3.5 if not multilane else 5.0
            cars.append(f"{at:.0f},{lat},{-rng.uniform(6, 12):.1f}")
        else:              # crossing at a junction, setting off when the car is near
            js = [j for j in crossings[name] if 60 < j < length - 100]
            at = rng.choice(js) + 5 if js else at
            side = rng.choice([-1, 1])
            cars.append(f"{at:.0f},{side * 22},0,{-side * rng.uniform(5, 9):.1f},{rng.uniform(40, 90):.0f}")
    env = f"BCAI_CARS={';'.join(cars)}"
    if rng.random() < 0.5:
        env += " BCAI_FAST=1"
    print(f"T{i:04d} {name}\t{env}\t{args}")
