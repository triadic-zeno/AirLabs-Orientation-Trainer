"""Rank marks with divisions (2.5.0): Bronze I .. Grand Champion III, plus Supersonic Legend.

Input: grid.txt from `tier_model grid`: for every preset and range, the model's mean score at
skill ranks 0, 0.25, ..., 7 (0 = Bronze anchor, 5 = Champion anchor, 7 = near-perfect run).

1. Smooth each curve: monotone (pool-adjacent-violators) in a transformed score space
   (log score for Flick/Hybrid, logit of time-on-ball for Track/Chase), ignoring points stuck
   at the floor (scores under 300, where the 5 s limit and miss penalty pin low ranks to 0).
2. Recalibrate per mode with a measured anchor: a GC1-level player's runs on one preset per
   mode. Their average (in log / logit score, the space ranks are roughly even in) lands in
   the middle of Grand Champion I (rank value 6.17). The model's rank axis is stretched by one
   factor per mode so that point maps there; Bronze stays at 0.
3. Marks for each division: rank value i/3 (Bronze II = 1/3 ... Grand Champion III = 20/3),
   following the model's own curve through the stretch, so every division is one even step
   of skill. Supersonic Legend is the best plausible run (the model's near-perfect player,
   capped for Track and Chase).
"""
import json, math, sys

GRID = sys.argv[1] if len(sys.argv) > 1 else 'grid.txt'
RANKS = [k * 0.25 for k in range(29)]

# Measured anchors: a GC1-level controller player's runs (Medium range), in the order played.
# The anchor is the average in the same space the rank is averaged in (log score, or logit of
# time on the ball), so the weighted-average rank of these runs comes out at GC1, not just the
# rank of the average score. (Ranks are roughly linear in log score, so the plain average score
# would sit a little high.) Runs under 300 (a 0 from a broken run) are left out.
ANCHORS = {
    0: (4, 1, [24463, 29862, 0, 27388, 28196, 30554, 25390, 32606, 23830, 26019, 23086]),   # Flick Mixed
    1: (1, 1, [47960, 47678, 48473, 46952, 46304, 43308, 46250, 47209, 47356, 48204, 46374]),  # Track Control
    2: (0, 1, [39033, 36882, 45862, 45621, 42758, 38019, 42961, 44192, 43046, 45808]),  # Hybrid Control
    3: (1, 1, [28578, 24930, 24106, 26903, 26127, 27562, 24706, 29629, 28827, 24753]),  # Chase Medium
}
# Where in Grand Champion I the anchor lands (6.0 = bottom, 6.33 = top): the middle.
GC1_MID = 6.0 + 1.0 / 6.0

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
    blocks = [[v, 1] for v in y]
    out = []
    for b in blocks:
        out.append(b)
        while len(out) > 1 and out[-2][0] > out[-1][0]:
            v2, n2 = out.pop(); v1, n1 = out.pop()
            out.append([(v1 * n1 + v2 * n2) / (n1 + n2), n1 + n2])
    res = []
    for v, n in out: res += [v] * n
    return res

def smooth3(y):
    return [y[0]] + [(y[i - 1] + 2 * y[i] + y[i + 1]) / 4 for i in range(1, len(y) - 1)] + [y[-1]]

def interp(xs, ys, x):
    if x <= xs[0]: return ys[0]
    if x >= xs[-1]: return ys[-1]
    for i in range(len(xs) - 1):
        if xs[i] <= x <= xs[i + 1]:
            t = (x - xs[i]) / (xs[i + 1] - xs[i])
            return ys[i] + t * (ys[i + 1] - ys[i])

def inv_interp(xs, ys, y):
    """x where the (non-decreasing) curve reaches y."""
    if y <= ys[0]: return xs[0]
    if y >= ys[-1]: return xs[-1]
    for i in range(len(xs) - 1):
        if ys[i] <= y <= ys[i + 1]:
            if ys[i + 1] == ys[i]: return xs[i]
            return xs[i] + (y - ys[i]) / (ys[i + 1] - ys[i]) * (xs[i + 1] - xs[i])

rows = {}
for line in open(GRID):
    v = line.split()
    if not v: continue
    m, p, r = map(int, v[:3])
    rows[(m, p, r)] = list(map(float, v[3:]))

curves = {}
for (m, p, r), sc in rows.items():
    ok = [i for i, s in enumerate(sc) if s >= 300]
    xs = [RANKS[i] for i in ok] if len(ok) >= 4 else RANKS[:]
    ys = smooth3(pava([tf(m, sc[RANKS.index(x)]) for x in xs]))
    ys = pava(ys)
    curves[(m, p, r)] = (xs, ys, sc)

# Per-mode stretch of the model's rank axis, from the anchors.
warp = {}
for m, (p, r, runs) in ANCHORS.items():
    xs, ys, _ = curves[(m, p, r)]
    ok = [tf(m, v) for v in runs if v >= 300]
    ya_his = sum(ok) / len(ok)
    score = inv(m, ya_his)
    r_his = inv_interp(xs, ys, ya_his)
    warp[m] = r_his
    print(f"mode {m}: anchor {score:.0f} sits at model rank {r_his:.2f} -> rank value {GC1_MID:.2f} (GC1)", file=sys.stderr)

def model_rank(m, R):
    """Inverse of the stretch: true rank value R -> model rank. One straight stretch through
    the anchor, so the divisions keep the model's own spacing on both sides of it (Grand
    Champion II and III continue Champion's step instead of jumping)."""
    return R * warp[m] / GC1_MID

def nice(m, v):
    if tracky(m):
        step = 10 if v > 58000 else 50 if v < 10000 else 100
    else:
        step = 50 if v < 10000 else 100 if v < 40000 else 250
    return int(round(v / step) * step)

out = {}
for (m, p, r), (xs, ys, sc) in curves.items():
    th = [0]
    for i in range(1, 22):
        R = 7.0 if i == 21 else i / 3.0
        if i == 21:
            # Supersonic Legend: the best plausible run (the near-perfect player), not a
            # continuation of the division steps.
            ys7 = interp(xs, ys, 7.0)
            if tracky(m):
                # The near-perfect model tracks ~99% even at 90 deg/s, which no human does. Cap
                # Supersonic Legend at a believable step above GC1: 2.5 in logit of time on the
                # ball (e.g. 78% -> 98%, 58% -> 94%, 14% -> 66%).
                ys7 = min(ys7, interp(xs, ys, warp[m]) + 2.5)
            v = inv(m, ys7)
        else:
            x = model_rank(m, R)
            if x < xs[0]:
                v = interp(RANKS, sc, x)      # below the smoothed part: the raw (near-0) scores
            else:
                v = inv(m, interp(xs, ys, x))
        th.append(v)
    if tracky(m): th[21] = min(th[21], 59900)
    th = [0] + [nice(m, v) for v in th[1:]]
    if th[1] < 100: th[1] = 100
    for i in range(2, 22):
        gap = 10 if tracky(m) and th[i - 1] > 58000 else 100
        if th[i] < th[i - 1] + gap: th[i] = th[i - 1] + gap
    out[(m, p, r)] = th

# A longer range is never easier: cap each mark at the same mark one range closer.
for (m, p, r), th in sorted(out.items()):
    if r > 0 and (m, p, r - 1) in out:
        prev = out[(m, p, r - 1)]
        for i in range(1, 22): th[i] = min(th[i], prev[i])
        for i in range(2, 22):
            if th[i] <= th[i - 1]: th[i] = th[i - 1] + 10
for (m, p, r), th in out.items():
    assert all(th[i] < th[i + 1] for i in range(21)), (m, p, r, th)
    if tracky(m): assert th[21] < 60000, (m, p, r, th)

json.dump({f"{k[0]} {k[1]} {k[2]}": v for k, v in out.items()}, open('tier_marks2.json', 'w'))
for k in sorted(out): print(k, out[k])
