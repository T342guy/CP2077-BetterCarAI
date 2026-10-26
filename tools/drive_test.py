#!/usr/bin/env python3
"""Compare the drives of a driving test (Debug tools > Driving test, plugin 0.29+).

    tools/drive_test.py <folder or test_<id>_route.txt> [--id ID] [--bends]

The test's files, in the CET mod's folder: test_<id>_route.txt (the route, a
point every 2 m), test_<id>_player.csv (the player's drive) and
test_<id>_ai_<style>.csv (Autodrive+'s). With a folder, the newest test in it.

For each drive: the time, the average and top speed, the hardest braking,
the cornering (the sideways acceleration, from how the velocity turns), the
slip angle (the body against the way it goes) and how long it slid past 17
degrees, and how much of the drive was off the route (a missed turn: the
drives then took different roads). Then bend by bend (bends found in the
route's shape, where the drive was on it): the speed going in, the slowest
point, the hardest cornering and the time through. Then cornering against
slip angle, drive by drive: the game's cars grip best 8-15 degrees into a
bend. Last, the grip the player used without sliding: what the car can
really do, against what Autodrive+ asks.
"""
import argparse
import csv
import glob
import math
import os
import re
import sys


def load_route(path):
    pts = []
    for line in open(path):
        if line.startswith("#"):
            continue
        x, y, z = map(float, line.split())
        pts.append((x, y, z))
    s = [0.0]
    for a, b in zip(pts, pts[1:]):
        s.append(s[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    return pts, s


def load_run(path):
    rows = []
    with open(path) as f:
        lines = [l for l in f if not l.startswith("#")]
    for r in csv.DictReader(lines):
        rows.append({k: float(v) for k, v in r.items()})
    return rows


def project(pts, s, x, y, hint):
    """Distance along the route of the point nearest (x, y), and the offset (left +).

    Searched near hint first; when that is far off (the drive left the route,
    or came back to it somewhere else), along the whole route."""
    def search(lo, hi):
        best, bs, off = 1e18, hint, 0.0
        for i in range(lo, hi):
            ax, ay, _ = pts[i]
            bx, by, _ = pts[i + 1]
            dx, dy = bx - ax, by - ay
            l2 = dx * dx + dy * dy
            t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / l2)) if l2 > 1e-9 else 0.0
            px, py = ax + dx * t, ay + dy * t
            d = (x - px) ** 2 + (y - py) ** 2
            if d < best:
                best, bs = d, s[i] + (s[i + 1] - s[i]) * t
                off = (dx * (y - ay) - dy * (x - ax)) / math.sqrt(l2) if l2 > 1e-9 else 0.0
        return best, bs, off
    i = int(hint / 2) if s[-1] > 0 else 0
    best, bs, off = search(max(0, i - 30), min(len(pts) - 1, i + 120))
    if best > 15.0 ** 2:
        best, bs, off = search(0, len(pts) - 1)
    return bs, off


def derive(rows, pts, s):
    """Adds: along, off, sideways and lengthwise acceleration (from the velocity), slip angle."""
    hint = 0.0
    for r in rows:
        r["along"], r["offset"] = project(pts, s, r["x"], r["y"], hint)
        hint = r["along"]
    for i, r in enumerate(rows):
        # (Over 0.3 s either side, from how the velocity turned and grew: the
        # heading would read the car pointing into the bend as cornering.)
        a, b = rows[max(i - 3, 0)], rows[min(i + 3, len(rows) - 1)]
        dt = b["t"] - a["t"]
        v = math.hypot(r["vx"], r["vy"])
        if dt <= 0 or v < 1:
            r["lat"] = r["long"] = 0.0
        else:
            ax, ay = (b["vx"] - a["vx"]) / dt, (b["vy"] - a["vy"]) / dt
            ux, uy = r["vx"] / v, r["vy"] / v
            r["lat"] = min(abs(-uy * ax + ux * ay), 60.0)
            r["long"] = ux * ax + uy * ay
        if v > 3:
            side = (-r["fy"] * r["vx"] + r["fx"] * r["vy"]) / max(math.hypot(r["fx"], r["fy"]), 1e-6)
            r["slip"] = math.degrees(math.asin(max(-1.0, min(1.0, side / v))))
        else:
            r["slip"] = 0.0


def bends(pts, s, window=10, min_turn=25.0):
    """Stretches of the route that turn more than min_turn degrees within 2*window points."""
    heading = []
    for i in range(len(pts) - 1):
        heading.append(math.atan2(pts[i + 1][1] - pts[i][1], pts[i + 1][0] - pts[i][0]))
    turning = []
    for i in range(len(heading)):
        a, b = heading[max(i - window, 0)], heading[min(i + window, len(heading) - 1)]
        turning.append(abs(math.degrees((b - a + math.pi) % (2 * math.pi) - math.pi)))
    out, i = [], 0
    while i < len(turning):
        if turning[i] >= min_turn:
            j = i
            while j + 1 < len(turning) and turning[j + 1] >= min_turn * 0.6:
                j += 1
            total = math.degrees((heading[min(j + window, len(heading) - 1)] - heading[max(i - window, 0)] + math.pi)
                                 % (2 * math.pi) - math.pi)
            out.append((s[max(i - window, 0)], s[min(j + window, len(s) - 1)], total))
            i = j + window
        i += 1
    return out


SLIDE = 17.0  # slip angle (degrees) past which the car slides: the plugin's kSlideSlip


def summary(name, rows):
    moving = [r for r in rows if r["speed"] > 0.5]
    t = rows[-1]["t"] - rows[0]["t"] if rows else 0
    on = [r for r in rows if abs(r["offset"]) < 15]
    dist = (max(r["along"] for r in on) - min(r["along"] for r in on)) if on else 0
    # (The cornering itself: straights would pull the figure down.)
    clean = sorted(r["lat"] for r in rows if r["speed"] > 8 and r["lat"] > 2.0 and abs(r["slip"]) < SLIDE)
    p95 = clean[int(0.95 * (len(clean) - 1))] / 9.81 if clean else 0
    sliding = sum(0.05 for r in rows if r["speed"] > 8 and abs(r["slip"]) >= SLIDE)
    print(f"{name}: {t:.0f} s, {dist:.0f} m of the route driven, average {3.6 * sum(r['speed'] for r in moving) / max(len(moving), 1):.0f} km/h "
          f"moving, top {3.6 * max(r['speed'] for r in rows):.0f} km/h")
    print(f"   cornering: hardest {max(r['lat'] for r in rows) / 9.81:.2f} g, 95% of cornering under {SLIDE:.0f} deg of slip "
          f"under {p95:.2f} g; slip up to {max(abs(r['slip']) for r in rows):.0f} deg, past {SLIDE:.0f} deg for {sliding:.1f} s")
    off = [abs(r["offset"]) for r in moving]
    print(f"   braking: hardest {-min(r['long'] for r in rows):.1f} m/s2; "
          f"{sum(1 for a, b in zip(rows, rows[1:]) if a['long'] > -5 >= b['long'])} times past 5 m/s2; "
          f"off the route (more than 15 m) {100.0 * sum(1 for o in off if o > 15) / max(len(off), 1):.0f}% of the time")
    return p95


def grip_table(runs):
    """Sideways acceleration against slip angle: where the car grips best."""
    print(f"\nCornering against slip angle (g: median / 90%; cornering harder than 0.2 g above 30 km/h):")
    print("   slip      " + "".join(f"| {n[:22]:<22} " for n, _ in runs))
    for lo in range(0, 33, 3):
        line = f"   {lo:2d}-{lo + 3:2d} deg "
        for _, rows in runs:
            g = sorted(r["lat"] / 9.81 for r in rows
                       if r["speed"] > 8 and r["lat"] > 2 and lo <= abs(r["slip"]) < lo + 3)
            line += "| " + (f"{g[len(g) // 2]:.2f} / {g[int(0.9 * (len(g) - 1))]:.2f} g ({len(g):4d})" if g else f"{'-':<22}") + " "
        print(line)


def in_bend(rows, s0, s1):
    near = [r for r in rows if s0 - 40 <= r["along"] <= s1 + 20 and abs(r["offset"]) < 15]
    inside = [r for r in near if s0 <= r["along"] <= s1]
    if not near or not inside:
        return None
    entry = min(near, key=lambda r: abs(r["along"] - (s0 - 30)))["speed"]
    return (3.6 * entry, 3.6 * min(r["speed"] for r in inside), max(r["lat"] for r in inside) / 9.81,
            near[-1]["t"] - near[0]["t"], max(abs(r["slip"]) for r in inside))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("where", help="the mod folder, or a test_<id>_route.txt")
    ap.add_argument("--id", help="which test (default: the newest)")
    ap.add_argument("--bends", action="store_true", help="every bend, not only the 12 sharpest")
    args = ap.parse_args()
    if os.path.isdir(args.where):
        routes = sorted(glob.glob(os.path.join(args.where, "test_*_route.txt")))
        if args.id:
            routes = [r for r in routes if args.id in r]
        if not routes:
            sys.exit("no test_*_route.txt there")
        route_file = routes[-1]
    else:
        route_file = args.where
    test_id = re.search(r"test_(.+)_route\.txt$", route_file).group(1)
    folder = os.path.dirname(route_file) or "."
    with open(route_file) as f:
        print(f.readline().strip("# \n"))
    pts, s = load_route(route_file)
    runs = []
    for path in sorted(glob.glob(os.path.join(folder, f"test_{test_id}_*.csv"))):
        name = re.search(rf"test_{re.escape(test_id)}_(.+)\.csv$", path).group(1).replace("_", " ")
        rows = load_run(path)
        if len(rows) < 10:
            continue
        derive(rows, pts, s)
        runs.append((name, rows))
    if not runs:
        sys.exit("no drives recorded for this test")
    print(f"route: {s[-1]:.0f} m\n")
    grip = {}
    for name, rows in runs:
        grip[name] = summary(name, rows)
    found = bends(pts, s)
    if not args.bends:
        found = sorted(sorted(found, key=lambda b: -abs(b[2]))[:12])
    print(f"\n{len(found)} bends (in km/h: going in, slowest; hardest cornering g; seconds through; most slip deg):")
    header = "   at      turn  " + "".join(f"| {n[:24]:<24} " for n, _ in runs)
    print(header)
    for s0, s1, turn in found:
        line = f"   {s0:5.0f} m {turn:+5.0f}  "
        for _, rows in runs:
            b = in_bend(rows, s0, s1)
            line += "| " + (f"{b[0]:3.0f} {b[1]:3.0f}  {b[2]:.2f} g {b[3]:4.1f} s {b[4]:3.0f}  " if b else f"{'-':<25}") + " "
        print(line)
    grip_table(runs)
    if "player" in grip:
        print(f"\nThe player cornered at up to {grip['player']:.2f} g without sliding (95% of cornering under "
              f"{SLIDE:.0f} deg of slip): about what this car can hold.")


if __name__ == "__main__":
    main()
