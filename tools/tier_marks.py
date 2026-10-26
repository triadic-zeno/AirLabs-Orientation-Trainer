# Turns tier_model's table.txt into the tier marks in AirLabs/TrainerCore.h (see tier_model.cpp).
import math, sys, json
rows=[list(map(float,l.split())) for l in open('table.txt')]
NAMES=["Bronze","Silver","Gold","Platinum","Diamond","Champion","Grand Champion","Supersonic Legend"]
def fit_quad(xs, ys):
    # least squares y = a + b x + c x^2
    n=len(xs); S=[sum(x**k for x in xs) for k in range(5)]
    T=[sum(y*x**k for x,y in zip(xs,ys)) for k in range(3)]
    A=[[S[0],S[1],S[2]],[S[1],S[2],S[3]],[S[2],S[3],S[4]]]
    # solve 3x3
    import copy
    M=[A[i]+[T[i]] for i in range(3)]
    for i in range(3):
        p=max(range(i,3),key=lambda r:abs(M[r][i])); M[i],M[p]=M[p],M[i]
        for r in range(3):
            if r!=i:
                f=M[r][i]/M[i][i]
                M[r]=[a-f*b for a,b in zip(M[r],M[i])]
    return [M[i][3]/M[i][i] for i in range(3)]
def nice(v):
    step = 50 if v < 10000 else 100 if v < 40000 else 250
    return int(round(v/step)*step)
out={}
for r in rows:
    mode,preset,rng=map(int,r[:3]); sc=r[3:14]; ssl=r[14]
    xs=[k*0.5 for k in range(11)]
    track = mode in (1, 3)  # Track and Chase are both scored by time on the ball (max 60,000)
    if track:
        tf=lambda s: math.log(max(s,1)/60000/(1-min(s/60000,0.995)))
        inv=lambda y: 60000*(math.exp(y)/(1+math.exp(y)))
    else:
        tf=lambda s: math.log(max(s,1)); inv=lambda y: math.exp(y)
    # Smooth the simulated scores with a quadratic in log score, fitted only where the score
    # isn't stuck at the floor: with the 5 s timeout and the miss penalty, low ranks score
    # (near) 0 on the big-turn presets, and those zeros would bend the whole curve.
    ok=[i for i,s in enumerate(sc) if s >= 300]
    if len(ok) >= 4:
        a,b,c=fit_quad([xs[i] for i in ok],[tf(sc[i]) for i in ok])
    else:
        a,b,c=fit_quad(xs,[tf(s) for s in sc])
    f=lambda x: a+b*x+c*x*x
    lo=xs[ok[0]] if ok else 0.0
    def at(x):                                          # smoothed where valid, raw simulation below that
        if x >= lo: return inv(f(x))
        i=int(round(x*2)); return sc[i]
    th=[0]
    for k in range(1,6): th.append(at(k-0.5))           # Silver..Champion: boundaries between simulated ranks
    champ=inv(f(5.0))
    if track:
        # Track: measure the top from the misses. SSL misses at most 10% of the time, and at
        # most a third of what a Champion misses. GC is a third of the way there (log score).
        # SSL: the best plausible time on target - the near-perfect player's run (2.3.0;
        # before, a rule of thumb on misses).
        sslT = min(ssl, 59900)
        th += [math.exp(math.log(champ)+(math.log(sslT)-math.log(champ))/3), sslT]
    else:
        sslT=ssl                                         # SSL: the fastest plausible run (the near-perfect player)
        gc=math.exp(math.log(champ)+(math.log(sslT)-math.log(champ))/3)  # GC: a third of the way (log scale)
        th += [gc, sslT]
    if track:
        # Easy track presets saturate near 60,000, so near the top space the tiers by misses:
        # above 75% each tier must miss at least 20% less than the one below.
        m=[1-min(v,59990)/60000 for v in th]
        m[0]=1.0
        for i in range(2,8):
            if m[i-1] < 0.25 and m[i] > m[i-1]/1.25: m[i]=m[i-1]/1.25
        th=[60000*(1-x) for x in m]
        def niceT(v):
            step = 10 if v > 58000 else 50 if v < 10000 else 100 if v < 40000 else 250
            return int(round(v/step)*step)
        th=[0]+[niceT(v) for v in th[1:]]
    else:
        th=[nice(v) for v in th]
    # Low ranks that score 0 still need increasing marks: at least 100 points per tier.
    if th[1] < 100: th[1]=100
    for i in range(2,8):
        if th[i] <= th[i-1]: th[i]=th[i-1]+(10 if track and th[i-1] > 58000 else 100)
    assert all(th[i] < th[i+1] for i in range(7)) and (not track or th[7] < 60000), (mode,preset,rng,th)
    out[(mode,preset,rng)]=th
# A longer range is never easier: cap each mark at the same tier's mark one range closer.
for (mode,preset,rng),th in out.items():
    if rng > 0 and (mode,preset,rng-1) in out:
        prev=out[(mode,preset,rng-1)]
        for i in range(1,8): th[i]=min(th[i],prev[i])
        assert all(th[i] < th[i+1] for i in range(7)), (mode,preset,rng,th)
for k,v in out.items(): print(k, v)
json.dump({f"{k[0]} {k[1]} {k[2]}":v for k,v in out.items()}, open('tier_marks.json','w'))
