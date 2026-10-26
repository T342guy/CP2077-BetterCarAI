#!/usr/bin/env python3
"""navtrips.py N SEED: N random trips for drive_sim (one per line: x0 y0 hx hy x1 y1):
a start on a road (not a junction, not a highway ramp end), heading along it,
and a destination 300-2500 m away, up to 12 m off a road that isn't a highway."""
import math, random, sys
path = sys.argv[3] if len(sys.argv) > 3 else "/run/media/system/NVME256/SteamLibrary/steamapps/common/Cyberpunk 2077/red4ext/plugins/BetterCarAI/dumps/lanegraph.txt"
n, seed = int(sys.argv[1]), int(sys.argv[2])
roads = []
for line in open(path):
    if line.startswith('#'):
        continue
    parts = line.split('|')
    f = parts[0].split()
    flags = int(f[1], 16)
    if not (flags & 16) or flags & (8 | 2048 | 32 | 128 | 8192 | 32768):
        continue
    pts = [tuple(map(float, p.split(','))) for p in parts[2].split()]
    if len(pts) < 2:
        continue
    length = sum(math.dist(pts[i][:2], pts[i + 1][:2]) for i in range(len(pts) - 1))
    if length > 40:
        roads.append((flags, pts))
rng = random.Random(seed)
def spot(pts):
    i = rng.randrange(len(pts) - 1)
    a, b = pts[i], pts[i + 1]
    t = rng.random()
    d = math.dist(a[:2], b[:2]) or 1
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t), ((b[0] - a[0]) / d, (b[1] - a[1]) / d)
for _ in range(n):
    (p, h) = spot(rng.choice(roads)[1])
    while True:
        flags, pts = rng.choice(roads)
        if flags & 16384:
            continue
        q, hq = spot(pts)
        side = rng.uniform(-12, 12)
        q = (q[0] - hq[1] * side, q[1] + hq[0] * side)
        if 300 < math.dist(p, q) < 2500:
            break
    print(f"{p[0]:.1f} {p[1]:.1f} {h[0]:.3f} {h[1]:.3f} {q[0]:.1f} {q[1]:.1f}")
