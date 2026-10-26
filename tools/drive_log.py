#!/usr/bin/env python3
"""Summarise Autodrive+ drives and NPC traffic reactions from BetterCarAI.log.

    tools/drive_log.py BetterCarAI.log [--last N] [--events] [--traffic] [--routes]

For each drive (newest last): when, the style, the route, how it ended, and
counts of what it decided (lane changes by reason, passes, changes of mind,
hard braking, leaving the road, backing up, what slowed it down). --events
prints every decision of those drives as well; --traffic lists what NPC
drivers did when bumped; --routes lists the routes planned to the map
destination (plugin 0.28+), with where the pin was and where the game put it.

The log lines it reads (plugin 0.27+, CET mod with "Autodrive+ |" events):
  [time] [pid] [BetterCarAI] Autodrive+: ON, City (route: ...)
  [time] [pid] [BetterCarAI] Autodrive+ | 12.3 s  54 km/h  345 m  move (keeping right): ...
  [time] [pid] [BetterCarAI] Autodrive+ raw: v 23.9 target ...    (twice a second)
  [time] [pid] [BetterCarAI] Autodrive+ stopped: arrived; ...
  [time] [pid] [BetterCarAI] Traffic | tap 2 on Thorton Galena (9 km/h): honk and flash
  [time] [pid] [BetterCarAI] Route | new destination: 2311 m, about 3 min 05 s, ... (pin (x, y, z), ...)
Older logs (before the events) only have the raw lines; for those it reports
the drives and how they ended.
"""
import argparse
import collections
import re
import sys

LINE = re.compile(r"^\[(?P<when>[^\]]+)\] \[\d+\] \[BetterCarAI\] (?P<msg>.*)$")
EVENT = re.compile(r"^Autodrive\+ \| +(?P<t>[\d.]+) s +(?P<v>-?\d+) km/h +(?P<s>-?\d+) m +(?P<what>.*)$")


class Drive:
    def __init__(self, when, msg):
        self.when = when
        self.start = msg
        self.end = None
        self.events = []
        self.raw = 0

    def style(self):
        for _, _, _, what in self.events:
            if what.startswith("style: "):
                return what[len("style: "):].split(";")[0]
        m = re.search(r"ON, (\w+)", self.start)
        return m.group(1).lower() if m else "?"


def read(path):
    drives, traffic, routes, current = [], [], [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE.match(line.rstrip("\n"))
            if not m:
                continue
            when, msg = m.group("when")[:19], m.group("msg")
            if msg.startswith("Autodrive+: ON"):
                current = Drive(when, msg)
                drives.append(current)
            elif msg.startswith("Autodrive+ stopped:") or msg.startswith("Autodrive+ did not start:"):
                if msg.startswith("Autodrive+ did not start:"):
                    drives.append(Drive(when, msg))
                    drives[-1].end = msg
                elif current:
                    current.end = msg[len("Autodrive+ stopped: "):]
                current = None
            elif msg.startswith("Autodrive+ | "):
                e = EVENT.match(msg)
                if e and current:
                    current.events.append((float(e.group("t")), int(e.group("v")), int(e.group("s")), e.group("what")))
            elif msg.startswith("Autodrive+ raw:") or msg.startswith("Autodrive+: v "):
                if current:
                    current.raw += 1
            elif msg.startswith("Traffic | "):
                traffic.append((when, msg[len("Traffic | "):]))
            elif msg.startswith("Route | "):
                routes.append((when, msg[len("Route | "):]))
    return drives, traffic, routes


def kind(what):
    """The decision's kind and, where it has one, its reason."""
    m = re.match(r"move \(([^)]*)\)", what)
    if m:
        return "lane change / swerve", m.group(1)
    m = re.match(r"speed: ([^,(]*)", what)
    if m:
        return ("slowed for", m.group(1).strip()) if not what.startswith("speed: free again") else (None, None)
    if what.startswith("pass: "):
        return "passing", re.split(r",| \(| a | an | something", what[len("pass: "):])[0]
    for prefix, name in (("changed its mind", "changed its mind"), ("braking hard", "braking hard"),
                         ("off the mapped road", "left the road"), ("backing up", "backed up"),
                         ("losing grip", "lost grip"), ("off its line", "replanned"),
                         ("lost its place", "replanned")):
        if what.startswith(prefix):
            return name, ""
    return None, None


def summary(d):
    last_t = d.events[-1][0] if d.events else 0.0
    route = re.search(r"\((route: [^)]*)\)", d.start)
    print(f"\n=== {d.when}  {d.style()}  {route.group(1) if route else d.start}")
    print(f"    ended: {d.end or '(still driving when the log ends)'}"
          + (f"  after {last_t:.0f} s" if d.events else ""))
    if not d.events:
        print(f"    no decision events (older log): {d.raw} raw lines")
        return
    counts = collections.Counter()
    reasons = collections.defaultdict(collections.Counter)
    for _, _, _, what in d.events:
        k, r = kind(what)
        if k:
            counts[k] += 1
            if r:
                reasons[k][r] += 1
    for k in ("lane change / swerve", "passing", "changed its mind", "slowed for", "braking hard", "lost grip",
              "left the road", "backed up", "replanned"):
        if counts[k]:
            detail = ", ".join(f"{r} {n}" for r, n in reasons[k].most_common())
            print(f"    {k:22s} {counts[k]:3d}" + (f"   ({detail})" if detail else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--last", type=int, default=0, help="only the last N drives")
    ap.add_argument("--events", action="store_true", help="every decision of those drives")
    ap.add_argument("--traffic", action="store_true", help="what NPC drivers did when bumped")
    ap.add_argument("--routes", action="store_true", help="the routes planned to the map destination")
    args = ap.parse_args()
    drives, traffic, routes = read(args.log)
    if args.last:
        drives = drives[-args.last:]
    print(f"{len(drives)} Autodrive+ drives")
    for d in drives:
        summary(d)
        if args.events:
            for t, v, s, what in d.events:
                print(f"    {t:7.1f} s {v:4d} km/h {s:6d} m  {what}")
    if traffic:
        c = collections.Counter()
        for _, what in traffic:
            c["taps" if what.startswith("tap ") else "crashes" if what.startswith("crash") else
              "calmed" if what.startswith("calm") else "other"] += 1
        lost = sum(1 for _, w in traffic if "lost patience" in w)
        print(f"\nNPC traffic: {c['taps']} taps ({lost} lost patience), {c['crashes']} crashes, "
              f"{c['calmed']} times people nearby stayed calm")
        if args.traffic:
            for when, what in traffic:
                print(f"    {when}  {what}")
    if routes:
        c = collections.Counter(what.split(":")[0] for _, what in routes)
        failed = sum(1 for _, w in routes if "no route" in w)
        print(f"\nRoutes: {len(routes)} planned ({', '.join(f'{k} {n}' for k, n in c.most_common())}), {failed} without a route")
        if args.routes:
            for when, what in routes:
                print(f"    {when}  {what}")


if __name__ == "__main__":
    sys.exit(main())
