#!/usr/bin/env python3
"""Explain a Better Car AI drive recording (rec_*.csv).

    tools/analyze_recording.py REC.csv [--png OUT.png] [--at SECONDS]

Prints, in plain words, what the car did around the wipe-out that saved the
recording (or the time given with --at), lists every steering snap in the
recording, and a second-by-second table. With --png it also draws a map of
the incident and graphs of speed, turn rate against the tyre grip limit, and
slip angle, with snaps and wipe-outs marked. --png needs Pillow.

The game doesn't let scripts read the AI driver's steering wheel or where it
wants to steer, so steering is estimated from how fast the car turns:
  turn rate   how fast the car's heading changes (deg/s); + is left, - is right
  cornering   turn rate x speed, in g: what the turn asks of the tyres.
              Road tyres hold roughly 1 g; far above that the car can't
              follow the turn and slides or spins.
  est. steer  front-wheel angle that would give that turn without sliding
              (2.8 m wheelbase); meaningless once the car is sliding.
  slip angle  angle between where the car points and where it is going.
A "snap" is the turn rate jumping by more than 40 deg/s within 0.3 s above
50 km/h, asking for more than 1.5 g: the driver yanked the wheel.
"""
import argparse
import csv
import math
import re
import sys

G = 9.81
WHEELBASE = 2.8
GRIP_G = 1.0


def num(row, key, default=float("nan")):
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return default


def load(path):
    meta, lines = {}, []
    with open(path, newline="") as f:
        for line in f:
            if line.startswith("#"):
                key, _, value = line[1:].partition(":")
                meta[key.strip()] = value.strip()
            else:
                lines.append(line)
    rows, last_t = [], None
    for row in csv.DictReader(lines):
        t = num(row, "t")
        if t == last_t:
            continue  # game paused: the same sample repeats
        last_t = t
        derive(row)
        row["_hit"] = row.get("collision") == "1" and bool(rows) and rows[-1].get("collision") != "1"
        rows.append(row)
    return meta, rows


def derive(r):
    v = num(r, "speed_kmh", 0) / 3.6
    yaw = num(r, "yaw_dps", 0)
    r["_v"] = v
    r["_yaw"] = yaw
    r["_g"] = math.radians(abs(yaw)) * v / G
    r["_steer"] = math.degrees(math.atan(WHEELBASE * math.radians(yaw) / v)) if v > 3 else 0.0
    fx, fy = num(r, "fwd_x", 0), num(r, "fwd_y", 0)
    vx, vy = num(r, "vel_x", 0), num(r, "vel_y", 0)
    if v > 3 and (fx or fy):
        # signed: + = travelling to the left of where the car points
        r["_slip"] = math.degrees(math.atan2(fx * vy - fy * vx, fx * vx + fy * vy))
    else:
        r["_slip"] = 0.0


def side(deg):
    return "left" if deg > 0 else "right"


def describe_rays(rays):
    # "kind:dist|..." for rays at -20..+20 deg; + is left of the heading.
    names = {"1": "car", "2": "wall", "3": "road rising"}
    angles = [-20, -10, -4, 0, 4, 10, 20]
    hits = {}
    for i, ray in enumerate(rays.split("|")):
        kind, _, dist = ray.partition(":")
        if kind in names and i < len(angles):
            d = float(dist)
            name = names[kind]
            if name not in hits or d < hits[name][0]:
                hits[name] = (d, angles[i])
    out = []
    for name, (d, a) in sorted(hits.items(), key=lambda kv: kv[1][0]):
        where = "straight ahead" if abs(a) <= 4 else f"{abs(a)} deg {side(a)}"
        out.append(f"{name} {d:.0f} m {where}")
    return ", ".join(out)


def find_snaps(rows):
    snaps, i = [], 3
    while i < len(rows):
        r = rows[i]
        jump = r["_yaw"] - rows[i - 3]["_yaw"]
        if r["_v"] * 3.6 > 50 and abs(jump) > 40 and r["_g"] > 1.5:
            # onset: last calm sample before; peak: largest turn rate after
            start = i
            while start > 0 and abs(rows[start - 1]["_yaw"]) > 10 and i - start < 10:
                start -= 1
            start = max(start - 1, 0)
            end = min(i + 6, len(rows))
            peak = max(range(i, end), key=lambda k: abs(rows[k]["_yaw"]))
            snaps.append((start, peak))
            i = peak + 10
        else:
            i += 1
    return snaps


def wipeouts(rows):
    return [i for i, r in enumerate(rows) if r.get("event") and r.get("mode") == "ai"]


def outcome(rows, peak):
    t = num(rows[peak], "t")
    for r in rows[peak:]:
        if num(r, "t") > t + 2.5:
            break
        if r.get("event") in ("spin", "impact", "collision", "hard slowdown"):
            return f"led to a {r['event']}"
        if abs(r["_slip"]) > 30:
            return "led to a spin"
    return "recovered"


def governor_limiting(r):
    # Only worth mentioning when the cap is at or below the speed it's driving.
    cap = num(r, "governor_cap_kmh")
    return bool(r.get("governor_reason")) and cap <= r["_v"] * 3.6 + 10


def pedal(r):
    gear = r.get("gear", "")
    if gear == "-1" and r["_v"] > 2:
        return "braking"
    return f"gear {gear}" if gear not in ("", "0") else "-"


def story(rows, focus, snaps):
    """Plain-language account of the few seconds around rows[focus]."""
    t0 = num(rows[focus], "t")
    out = []
    before = [r for r in rows if t0 - 3.0 <= num(r, "t") < t0 - 0.5]
    snap = next(((s, p) for s, p in snaps if t0 - 3 <= num(rows[p], "t") <= t0 + 1), None)
    calm = rows[snap[0]] if snap else (before[-1] if before else rows[focus])
    rays = describe_rays(calm.get("rays", ""))
    out.append(f"t={num(calm, 't'):6.1f}  {'Driving' if abs(calm['_yaw']) < 10 else 'Turning'} at "
               f"{calm['_v'] * 3.6:.0f} km/h ({pedal(calm)}), cornering {calm['_g']:.1f} g; "
               + (f"ahead: {rays}." if rays else "nothing ahead in the look-ahead rays."))
    if governor_limiting(calm):
        out.append(f"         Governor was limiting speed to {calm.get('governor_cap_kmh')} km/h ({calm['governor_reason']}).")
    if snap:
        s, p = snap
        rp = rows[p]
        dt = num(rp, "t") - num(rows[s], "t")
        out.append(f"t={num(rows[min(s + 1, p)], 't'):6.1f}  SNAP {side(rp['_yaw']).upper()}: turn rate {rows[s]['_yaw']:.0f} -> "
                   f"{rp['_yaw']:.0f} deg/s in {dt:.1f} s. At {rp['_v'] * 3.6:.0f} km/h that asks for "
                   f"{rp['_g']:.1f} g of cornering; tyres hold about {GRIP_G:.0f} g.")
    first_slide = next((r for r in rows if num(r, "t") >= t0 - 1 and abs(r["_slip"]) > 10), None)
    if first_slide and num(first_slide, "t") <= t0 + 3:
        out.append(f"t={num(first_slide, 't'):6.1f}  Lost grip: car pointing {abs(first_slide['_slip']):.0f} deg away "
                   f"from where it is going, sliding {abs(num(first_slide, 'sideways_ms')):.0f} m/s sideways.")
    near = [r for r in rows if t0 - 1 <= num(r, "t") <= t0 + 4]
    worst = max(near, key=lambda r: abs(r["_slip"])) if near else None
    if worst and abs(worst["_slip"]) > 30:
        out.append(f"t={num(worst, 't'):6.1f}  Worst slide: {abs(worst['_slip']):.0f} deg sideways at {worst['_v'] * 3.6:.0f} km/h.")
    air = [r for r in rows if t0 - 1 <= num(r, "t") <= t0 + 6 and r.get("airborne") == "1"]
    if air:
        out.append(f"t={num(air[0], 't'):6.1f}  Airborne (or over a drop with no ground below) for {num(air[-1], 't') - num(air[0], 't') + 0.1:.1f} s.")
    # The game's collision flag can stay on for good after a crash, so only
    # count it switching on.
    hit = next((r for p, r in zip(rows, rows[1:]) if t0 - 1 <= num(r, "t") <= t0 + 6
                and r.get("collision") == "1" and p.get("collision") != "1"), None)
    if hit:
        out.append(f"t={num(hit, 't'):6.1f}  Hit something at {hit['_v'] * 3.6:.0f} km/h.")
    slow = next((r for r in rows if t0 < num(r, "t") <= t0 + 8 and r["_v"] * 3.6 < 30), None)
    if slow:
        out.append(f"t={num(slow, 't'):6.1f}  Down to {slow['_v'] * 3.6:.0f} km/h.")

    # A gradual over-fast turn (no snap): the hardest turn just before the event.
    lead = [r for r in rows if t0 - 1.5 <= num(r, "t") <= t0 + 0.3]
    bend = max(lead, key=lambda r: r["_g"]) if lead else None
    if not snap and bend and bend["_g"] > 1.3:
        out.insert(1, f"t={num(bend, 't'):6.1f}  Turning {side(bend['_yaw'])} at {abs(bend['_yaw']):.0f} deg/s at "
                      f"{bend['_v'] * 3.6:.0f} km/h: {bend['_g']:.1f} g, more than the tyres hold.")
    if snap:
        rays = describe_rays(rows[snap[0]].get("rays", ""))
        if rays.startswith("car"):
            cause = (f"a late dodge: the driver swerved hard around something ahead ({rays}) "
                     f"at {rows[snap[1]]['_v'] * 3.6:.0f} km/h.")
        else:
            cause = ("a steering snap with no car in the look-ahead rays: the game's driver steered hard "
                     "on its own (a kink in its lane path at a junction, or something its own vision saw).")
    elif bend and bend["_g"] > 1.3:
        braked = any(pedal(r) == "braking" for r in rows if num(bend, "t") - 3 <= num(r, "t") <= num(bend, "t"))
        cause = (f"too fast for the bend: it turned at {abs(bend['_yaw']):.0f} deg/s at {bend['_v'] * 3.6:.0f} km/h "
                 f"({bend['_g']:.1f} g, tyres hold ~1 g) "
                 + ("after braking too little." if braked else "without braking for it first."))
    elif air:
        cause = "it left the ground (crest, kerb or ramp) and landed out of control."
    else:
        cause = "unclear from the measurements; look at the table and the graphs."
    out.append(f"Likely cause: {cause}")
    return out


def table(rows, focus, before, after):
    t0 = num(rows[focus], "t")
    print(f"{'t':>6} {'km/h':>5} {'pedal':>7} {'turn':>7} {'corner':>6} {'steer':>6} {'slip':>6} "
          f"{'ground':>6}  notes")
    print(f"{'s':>6} {'':>5} {'':>7} {'deg/s':>7} {'g':>6} {'deg':>6} {'deg':>6} {'m':>6}")
    for r in rows:
        t = num(r, "t")
        if t < t0 - before or t > t0 + after:
            continue
        notes = []
        if r.get("_snap_start"):
            notes.append("<< SNAP " + side(r["_yaw"]).upper() + " STARTS")
        if r.get("_snap_peak"):
            notes.append("<< snap peak")
        if r.get("event"):
            notes.append("<< " + r["event"].upper())
        if r.get("airborne") == "1":
            notes.append("airborne")
        if r.get("_hit"):
            notes.append("collision")
        if r.get("mode") != "ai":
            notes.append(f"[{r.get('mode')}]")
        if governor_limiting(r):
            notes.append(f"governor {r.get('governor_cap_kmh')} km/h: {r['governor_reason']}")
        rays = describe_rays(r.get("rays", ""))
        if rays:
            notes.append(rays)
        ground = num(r, "ground_m")
        gtxt = "air" if ground < 0 else f"{ground:.2f}"
        steer = f"{r['_steer']:+.1f}" if abs(r["_slip"]) < 8 else "slide"
        warn = "!" if r["_g"] > 1.2 else " "
        print(f"{t:6.1f} {r['_v'] * 3.6:5.0f} {pedal(r):>7} {r['_yaw']:+7.0f} {r['_g']:5.1f}{warn} {steer:>6} "
              f"{r['_slip']:+6.0f} {gtxt:>6}  {'; '.join(notes)}")


def speed_colour(kmh, top):
    x = max(0.0, min(1.0, kmh / max(top, 1.0)))
    stops = [(0.0, (60, 120, 255)), (0.4, (60, 210, 90)), (0.7, (250, 210, 40)), (1.0, (240, 50, 50))]
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if x <= b:
            f = (x - a) / (b - a)
            return tuple(int(ca[k] + (cb[k] - ca[k]) * f) for k in range(3))
    return stops[-1][1]


def render(rows, focus, snaps, out, title):
    from PIL import Image, ImageDraw

    t0 = num(rows[focus], "t")
    win = [r for r in rows if t0 - 12 <= num(r, "t") <= t0 + 6] or rows
    W, H, MAP_H = 1400, 1500, 760
    img = Image.new("RGB", (W, H), (18, 18, 22))
    d = ImageDraw.Draw(img)
    WHITE, GREY, SNAP, EVENT = (235, 235, 235), (140, 140, 150), (255, 80, 200), (255, 255, 255)

    # --- map ---
    xs = [num(r, "x") for r in win]
    ys = [num(r, "y") for r in win]
    top = max(r["_v"] * 3.6 for r in win)
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    span = max(maxx - minx, maxy - miny, 20.0)
    scale = (min(W, MAP_H) - 120) / span
    ox, oy = (W - (maxx - minx) * scale) / 2, (MAP_H - (maxy - miny) * scale) / 2

    def p(x, y):
        return ox + (x - minx) * scale, MAP_H - (oy + (y - miny) * scale)

    step = 10 if span < 300 else 50
    gx = math.floor(minx / step) * step
    while gx <= maxx:
        d.line([p(gx, miny), p(gx, maxy)], fill=(35, 35, 42))
        gx += step
    gy = math.floor(miny / step) * step
    while gy <= maxy:
        d.line([p(minx, gy), p(maxx, gy)], fill=(35, 35, 42))
        gy += step
    d.text((10, 8), title, fill=WHITE)
    d.text((10, 26), f"Map, seen from above (north up), t={num(win[0], 't'):.0f}..{num(win[-1], 't'):.0f} s, grid {step} m. "
           f"Line colour = speed (blue slow .. red {top:.0f} km/h). White ticks = where the car points, every 0.5 s.",
           fill=GREY)
    d.text((10, 44), "Pink = steering snap, magenta dots = airborne, white ring = wipe-out.", fill=GREY)

    for a, b in zip(win, win[1:]):
        d.line([p(num(a, "x"), num(a, "y")), p(num(b, "x"), num(b, "y"))],
               fill=speed_colour(b["_v"] * 3.6, top), width=5)
    for i, r in enumerate(win):
        x, y = p(num(r, "x"), num(r, "y"))
        if i % 5 == 0:
            fx, fy = num(r, "fwd_x", 0), num(r, "fwd_y", 0)
            d.line([(x, y), (x + fx * 16, y - fy * 16)], fill=WHITE, width=1)
        if i % 10 == 0:
            d.text((x + 8, y + 4), f"{num(r, 't'):.0f}s", fill=GREY)
        if r.get("airborne") == "1":
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(240, 70, 240))
    win_ids = {id(r) for r in win}
    for s, pk in snaps:
        r, rs = rows[pk], rows[min(s + 1, pk)]
        if id(rs) in win_ids:
            x, y = p(num(rs, "x"), num(rs, "y"))
            d.ellipse([x - 8, y - 8, x + 8, y + 8], outline=SNAP, width=3)
            d.text((x + 12, y - 22), f"SNAP {side(r['_yaw'])} starts: up to {abs(r['_yaw']):.0f} deg/s, {r['_g']:.1f} g",
                   fill=SNAP)
    fr = rows[focus]
    x, y = p(num(fr, "x"), num(fr, "y"))
    d.ellipse([x - 14, y - 14, x + 14, y + 14], outline=EVENT, width=2)
    d.text((x + 16, y + 6), f"{fr.get('event') or 'here'} t={t0:.1f}", fill=EVENT)

    # --- graphs over the same window ---
    ta, tb = num(win[0], "t"), max(num(win[-1], "t"), num(win[0], "t") + 1)
    graphs = [
        ("Speed (km/h). White = speed, orange = AI speed limit we send, blue = governor cap",
         [(lambda r: r["_v"] * 3.6, WHITE), (lambda r: num(r, "sent_cap_kmh"), (240, 160, 40)),
          (lambda r: num(r, "governor_cap_kmh"), (80, 200, 255))], False),
        ("Turn rate (deg/s, + left / - right). Grey band = what the tyres can hold at this speed (~1 g)",
         [(lambda r: r["_yaw"], (120, 230, 120))], True),
        ("Slip angle (deg): how far the car points away from where it is going. Beyond ~10 = sliding",
         [(lambda r: r["_slip"], (255, 90, 90))], False),
    ]
    gh = (H - MAP_H - 30) // len(graphs)
    for gi, (gtitle, series, band) in enumerate(graphs):
        top_y = MAP_H + 20 + gi * gh
        bottom = top_y + gh - 24
        vals = [f(r) for f, _ in series for r in win if not math.isnan(f(r))]
        lo, hi = (min(vals), max(vals)) if vals else (0, 1)
        lo, hi = min(lo, 0), max(hi, 0)
        if band:
            lim = [math.degrees(GRIP_G * G / max(r["_v"], 1)) for r in win]
            hi, lo = max(hi, 30), min(lo, -30)
        pad = (hi - lo) * 0.05 + 1
        lo, hi = lo - pad, hi + pad

        def g(t, v):
            return 70 + (t - ta) / (tb - ta) * (W - 90), bottom - (v - lo) / (hi - lo) * (bottom - top_y)

        d.rectangle([70, top_y, W - 20, bottom], outline=(60, 60, 70))
        if band:
            pts_hi = [g(num(r, "t"), min(l, hi)) for r, l in zip(win, lim)]
            pts_lo = [g(num(r, "t"), max(-l, lo)) for r, l in zip(win, lim)]
            d.polygon(pts_hi + pts_lo[::-1], fill=(48, 48, 58))
        d.line([g(ta, 0), g(tb, 0)], fill=(70, 70, 80))
        d.text((74, top_y + 3), gtitle, fill=(200, 200, 200))
        d.text((8, top_y), f"{hi:.0f}", fill=GREY)
        d.text((8, bottom - 12), f"{lo:.0f}", fill=GREY)
        t = math.ceil(ta)
        while t <= tb:
            x, _ = g(t, 0)
            d.text((x - 8, bottom + 4), f"{t:.0f}", fill=GREY)
            t += 1
        for f, col in series:
            pts = [g(num(r, "t"), f(r)) for r in win if not math.isnan(f(r))]
            if len(pts) > 1:
                d.line(pts, fill=col, width=2)
        for s, pk in snaps:
            t = num(rows[min(s + 1, pk)], "t")
            if ta <= t <= tb:
                x, _ = g(t, 0)
                d.line([(x, top_y), (x, bottom)], fill=SNAP, width=2)
                d.text((x + 3, bottom - 14), "snap", fill=SNAP)
        x, _ = g(t0, 0)
        d.line([(x, top_y), (x, bottom)], fill=EVENT)
    img.save(out)
    print(f"wrote {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recording")
    ap.add_argument("--png")
    ap.add_argument("--at", type=float, help="explain this time instead of the save's trigger")
    args = ap.parse_args()
    meta, rows = load(args.recording)
    if not rows:
        sys.exit("no samples")
    snaps = find_snaps(rows)
    for s, pk in snaps:
        rows[min(s + 1, pk)]["_snap_start"] = True
        rows[pk]["_snap_peak"] = True

    # What to explain: --at, else the event that triggered the save, else the
    # first AI-drive wipe-out, else the first snap.
    reason = meta.get("reason", "")
    m = re.search(r"at t=([\d.]+)", reason)
    want = args.at if args.at is not None else (float(m.group(1)) if m else None)
    if want is not None:
        focus = min(range(len(rows)), key=lambda i: abs(num(rows[i], "t") - want))
    elif wipeouts(rows):
        focus = wipeouts(rows)[0]
    elif snaps:
        focus = snaps[0][1]
    else:
        focus = len(rows) - 1

    name = args.recording.replace("\\", "/").split("/")[-1]
    title = f"{name}   car: {meta.get('vehicle', '?')}   saved because: {reason or '?'}"
    print(title)
    print()
    print(f"WHAT HAPPENED around t={num(rows[focus], 't'):.1f} s")
    for line in story(rows, focus, snaps):
        print("  " + line)
    print()
    print("STEERING SNAPS in this recording (turn rate jumping > 40 deg/s within 0.3 s above 50 km/h):")
    if not snaps:
        print("  none")
    for s, pk in snaps:
        r = rows[pk]
        rays = describe_rays(rows[s].get("rays", ""))
        print(f"  t={num(rows[min(s + 1, pk)], 't'):6.1f}  {side(r['_yaw']):5} {abs(r['_yaw']):4.0f} deg/s at {r['_v'] * 3.6:3.0f} km/h "
              f"= {r['_g']:.1f} g, {outcome(rows, pk)}; ahead: {rays or 'clear'}; "
              f"at ({num(r, 'x'):.0f}, {num(r, 'y'):.0f})")
    print()
    print("SECOND BY SECOND (3 s before to 3 s after). 'corner' over ~1 g (marked !) is more than the tyres hold;")
    print("'steer' is estimated from the turn rate, 'slide' when the car is sliding. Turn/steer/slip: + left, - right.")
    table(rows, focus, 3.0, 3.0)
    if args.png:
        render(rows, focus, snaps, args.png, title)


if __name__ == "__main__":
    main()
