"""Rank marks (2.5.0): Bronze I .. Grand Champion III, Supersonic Legend, Pro.

Input: mmrgrid.txt from `tier_model mmrgrid` - for every preset and range, the model's mean
score at Rocket League 2v2 MMR 100, 150, ..., 2650. The model player at each MMR is a blend of
three reference players (see tier_model.cpp, "reference players"):
    Bronze  200 MMR   ~7 s per target on Flick Mixed with no time limit
    GC1    1497 MMR   fitted to a real GC1-level player's runs
    Pro    2600 MMR   the GC1 player at human limits (where the best pros peak; record 2623)

Each division's mark is the score of a player at the MMR where that division starts in
Rocket League 2v2 (so divisions are spaced the way RL spaces them: ~60 MMR per division in
Bronze to Platinum, 80 in Diamond, 120 in Champion, 140 in Grand Champion), and Pro is the
pro reference player's score: the highest plausible score.

Per mode, the MMR axis is nudged so the measured GC1 player's own average (in log score,
or log-odds of time on the ball for Track and Chase) sits exactly at 1497 MMR, the middle of
Grand Champion I. Bronze (200) and Pro (2600) stay fixed.
"""
import json, math, sys

GRID = sys.argv[1] if len(sys.argv) > 1 else 'mmrgrid.txt'
MMRS = list(range(100, 2651, 50))
# Rocket League 2v2: MMR at the start of each division (rocketleague.tracker.network), Bronze II .. SSL, then Pro.
DIV = [170, 229, 294, 355, 415, 475, 535, 594, 655, 715, 773, 835, 915, 995,
       1075, 1195, 1315, 1435, 1575, 1715, 1863, 2600]
NAMES = ["B2", "B3", "S1", "S2", "S3", "G1", "G2", "G3", "P1", "P2", "P3", "D1", "D2", "D3",
         "C1", "C2", "C3", "GC1", "GC2", "GC3", "SSL", "PRO"]
BRONZE, GC1, PRO = 200.0, 1497.0, 2600.0

# The measured GC1 player's runs (Medium range), in the order played.
ANCHORS = {
    0: (4, 1, [24463, 29862, 0, 27388, 28196, 30554, 25390, 32606, 23830, 26019, 23086]),   # Flick Mixed
    1: (1, 1, [47960, 47678, 48473, 46952, 46304, 43308, 46250, 47209, 47356, 48204, 46374]),  # Track Control
    2: (0, 1, [39033, 36882, 45862, 45621, 42758, 38019, 42961, 44192, 43046, 45808]),  # Hybrid Control
    3: (1, 1, [28578, 24930, 24106, 26903, 26127, 27562, 24706, 29629, 28827, 24753]),  # Chase Medium
}

def tracky(mode): return mode in (1, 3)  # scored by time on the ball, max 60,000

def tf(mode, s):
    if tracky(mode):
        f = min(max(s, 1.0) / 60000.0, 0.9995)
        return math.log(f / (1 - f))
    return math.log(max(s, 1.0))

def inv(mode, y):
    if tracky(mode): return 60000.0 * (math.exp(y) / (1 + math.exp(y)))
    return math.exp(y)

def pava(y):
    """Monotone non-decreasing fit (pool adjacent violators)."""
    out = []
    for v in y:
        out.append([v, 1])
        while len(out) > 1 and out[-2][0] > out[-1][0]:
            v2, n2 = out.pop(); v1, n1 = out.pop()
            out.append([(v1 * n1 + v2 * n2) / (n1 + n2), n1 + n2])
    res = []
    for v, n in out: res += [v] * n
    return res

def smooth3(y):
    """1-2-1 smoothing (the ends stay put): takes the simulation noise out of the curves."""
    return [y[0]] + [(y[i - 1] + 2 * y[i] + y[i + 1]) / 4 for i in range(1, len(y) - 1)] + [y[-1]]

def interp(xs, ys, x):
    if x <= xs[0]: return ys[0] + (x - xs[0]) * (ys[1] - ys[0]) / (xs[1] - xs[0])
    if x >= xs[-1]: return ys[-1]
    for i in range(len(xs) - 1):
        if xs[i] <= x <= xs[i + 1]:
            t = (x - xs[i]) / (xs[i + 1] - xs[i])
            return ys[i] + t * (ys[i + 1] - ys[i])

def inv_interp(xs, ys, y):
    if y <= ys[0]: return xs[0]
    if y >= ys[-1]: return xs[-1]
    for i in range(len(xs) - 1):
        if ys[i] <= y <= ys[i + 1]:
            if ys[i + 1] == ys[i]: return xs[i]
            return xs[i] + (y - ys[i]) / (ys[i + 1] - ys[i]) * (xs[i + 1] - xs[i])

rows = {}
for line in open(GRID):
    v = line.split()
    if len(v) < 3 + len(MMRS): continue
    m, p, r = map(int, v[:3])
    rows[(m, p, r)] = list(map(float, v[3:3 + len(MMRS)]))

# Smooth each curve (monotone, then 1-2-1 twice) in transformed score, ignoring the floor (scores under 300, where
# the 5 s limit and the miss penalty pin weak players near 0).
curves = {}
for key, sc in rows.items():
    m = key[0]
    raw = pava(sc)
    ok = [i for i, s in enumerate(raw) if s >= 300]
    xs = [MMRS[i] for i in ok] if len(ok) >= 4 else MMRS[:]
    ys = pava(smooth3(smooth3(pava([tf(m, raw[MMRS.index(x)]) for x in xs]))))
    curves[key] = (xs, ys, raw)

# Per-mode nudge so the measured GC1 player's average lands at 1497 MMR.
his = {}
for m, (p, r, runs) in ANCHORS.items():
    xs, ys, _ = curves[(m, p, r)]
    ok = [tf(m, v) for v in runs if v >= 300]
    ya = sum(ok) / len(ok)
    his[m] = inv_interp(xs, ys, ya)
    print(f"mode {m}: measured GC1 average {inv(m, ya):.0f} = model MMR {his[m]:.0f} -> placed at {GC1:.0f}", file=sys.stderr)

def warp(m, x):
    """Rank MMR -> model MMR for this mode."""
    h = his[m]
    if x <= GC1: return BRONZE + (x - BRONZE) * (h - BRONZE) / (GC1 - BRONZE)
    return h + (x - GC1) * (PRO - h) / (PRO - GC1)

def nice(m, v):
    if tracky(m):
        step = 10 if v > 58000 else 50 if v < 10000 else 100
    else:
        step = 50 if v < 10000 else 100 if v < 40000 else 250
    return int(round(v / step) * step)

out = {}
for (m, p, r), (xs, ys, raw) in curves.items():
    th = [0]
    for x in DIV:
        mx = warp(m, x)
        v = interp(MMRS, raw, mx) if mx < xs[0] else inv(m, interp(xs, ys, mx))
        th.append(v)
    if tracky(m): th[-1] = min(th[-1], 59950)
    th = [0] + [nice(m, v) for v in th[1:]]
    if th[1] < 100: th[1] = 100
    for i in range(2, len(th)):
        gap = 10 if tracky(m) and th[i - 1] > 58000 else 100
        if th[i] < th[i - 1] + gap: th[i] = th[i - 1] + gap
    out[(m, p, r)] = th

# A longer range is never easier: cap each mark at the same mark one range closer.
for (m, p, r), th in sorted(out.items()):
    if r > 0 and (m, p, r - 1) in out:
        prev = out[(m, p, r - 1)]
        for i in range(1, len(th)): th[i] = min(th[i], prev[i])
        for i in range(2, len(th)):
            if th[i] <= th[i - 1]: th[i] = th[i - 1] + 10
for (m, p, r), th in out.items():
    assert len(th) == 23 and all(th[i] < th[i + 1] for i in range(22)), (m, p, r, th)
    if tracky(m): assert th[-1] < 60000, (m, p, r, th)

json.dump({f"{k[0]} {k[1]} {k[2]}": v for k, v in out.items()}, open('tier_marks3.json', 'w'))
print("      " + " ".join(f"{n:>6}" for n in NAMES))
for k in sorted(out): print(k, " ".join(f"{v:6d}" for v in out[k][1:]))
