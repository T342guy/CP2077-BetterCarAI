#!/usr/bin/env python3
# hwytraffic.py N [seed]: N random highway scenes as pool jobs. Cars in the
# lanes either side and in the route's own lane (5 m apart on these roads),
# slower than this car or about its speed, so it changes lanes to get past.
import os, random, sys

n = int(sys.argv[1]); rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
routes = {"H1": (os.environ["ROUTE_H1"], 4300, [-5, 0, 5]),
          "H2": (os.environ["ROUTE_H2"], 4200, [-5, 0, 5]),
          "H3": (os.environ["ROUTE_H3"], 3600, [-5, 0])}
for i in range(n):
    name = rng.choice(list(routes))
    args, length, lanes = routes[name]
    cars = []
    for _ in range(rng.randint(5, 12)):
        at = rng.uniform(80, length - 200)
        lat = rng.choice(lanes)
        u = rng.uniform(10, 20) if rng.random() < 0.6 else rng.uniform(20, 30)
        cars.append(f"{at:.0f},{lat},{u:.1f}")
    env = f"BCAI_CARS={';'.join(cars)}"
    if rng.random() < 0.5:
        env += " BCAI_FAST=1"
    print(f"W{i:04d} {name}\t{env}\t{args}")
