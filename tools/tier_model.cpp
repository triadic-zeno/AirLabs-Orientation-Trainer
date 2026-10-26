// AirLabs tier model.
//
// Build:   g++ -std=c++20 -O2 -I../AirLabs tier_model.cpp -o tier_model
// Use:     ./tier_model table 24 > table.txt     (every preset and range, scores by rank)
//          python3 tier_marks.py                  (turns table.txt into the kTier* arrays)
//          ./tier_model compare 8                 (headline numbers for Bronze, Platinum, Champion, SSL)
//          ./tier_model luck 40                   (run-to-run spread with fixed vs random targets)
//          ./tier_model profile 8 <mode> <preset> <range>
//          ./tier_model mmrgrid 24 > mmrgrid.txt   (2.5.0: every preset/range, scores at 2v2 MMR 100..2650)
//          python3 tier_marks3.py mmrgrid.txt      (turns it into the rank marks; insert_tables2.py
//                                                   writes them into TrainerCore.h)
//          SKILL=<12 values> ./tier_model metrics 16   (a player's numbers on the four measured presets)
//
// Simulates players of each Rocket League rank playing every preset with the plugin's own
// target generator, ball paths and scoring, and the game's air-control physics.
// A player is a closed-loop controller with human limits. Three anchors:
//   Bronze   - can turn the car but slowly and loosely
//   Champion - a practised controller player, from published human-performance figures
//   SSL      - near-perfect: the user's definition of a Supersonic Legend run
// Ranks in between are geometric interpolations of every parameter.
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <deque>
#include <cstdlib>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h" // the plugin's own targets, paths, scoring and physics constants
using namespace airlabs;

struct Skill
{
    float rt;        // s, reaction to a new target
    float offSpeed;  // fraction of the spin cap used while the target is off screen (following the arrow)
    float dirErr;    // deg, error in the turn direction while the target is off screen
    float commit;    // largest stick deflection used once the ball is on screen
    float brake;     // braking judgement: 1 = brakes exactly on time, <1 brakes early
    float brakeSd;   // per-flick spread of that judgement (lognormal sigma)
    float noiseRel;  // motor noise proportional to stick deflection
    float noiseAbs;  // motor noise floor (stick units)
    float delay;     // s, visual feedback delay
    float predict;   // 0-1, share of that delay compensated for your own car
    float remnant;   // tracking: random rate error, as a fraction of the ball's speed (+10 deg/s)
    float velErr;    // tracking: error in judging the ball's speed (fraction, slowly varying)
};
constexpr int kParams = 12;

// Anchors. None of these come from any one player's runs; each value is set from published
// human-performance figures (controller play) and the game's own physics:
//   rt       choice reaction time to a visual target: ~0.35-0.45 s for most people, ~0.30 s
//            for practised gamers.
//   delay    visual feedback loop ~0.10-0.12 s, plus ~0.04 s of game and display lag.
//   brake    aimed human movements spend ~55-60% of their time slowing down (asymmetric
//            velocity profiles), which with the car's equal accel/brake torque means braking
//            at ~0.65 of the latest possible point.
//   noise    signal-dependent motor noise ~10-15% of the command, plus ~2-3% thumb tremor
//            on a stick.
//   tracking speed judgement for moving targets ~10-20% (Weber fraction under game
//            conditions); human-operator remnant ~30% of the target's motion.
// Champion: a practised controller player.
static Skill gChamp  = { 0.30f, 0.42f, 28.f, 0.85f, 0.55f, 0.28f, 0.12f, 0.025f, 0.150f, 0.650f, 0.40f, 0.20f };
// Bronze: the same model further from the limits - general-population reaction time,
// partial stick, early and inconsistent braking, more noise, poorer tracking.
static Skill MakeBronze()
{
    Skill b = gChamp;
    if (const char* e = getenv("BRONZE"))
    {
        // fitting: 12 multipliers on the Champion values, in Skill order
        float f[kParams]; int n = 0; const char* q = e;
        while (n < kParams && *q) { f[n++] = strtof(q, (char**)&q); while (*q == ',' || *q == ' ') ++q; }
        float* x = &b.rt;
        for (int i = 0; i < n; ++i) x[i] *= f[i];
        return b;
    }
    // Set so a Bronze averages about 7 s per target on Flick Mixed (Medium) with no time limit:
    // slow and often wrong off-screen searching, a light hand on the stick, braking far too
    // early and unevenly. (2.3.0; before that Bronze averaged ~2 s, far too quick.)
    b.rt *= 2.0f; b.offSpeed *= 0.2f; b.dirErr *= 3.0f; b.commit *= 0.4f; b.brake *= 0.3f; b.brakeSd *= 2.5f;
    b.noiseRel *= 3.0f; b.noiseAbs *= 3.0f; b.delay *= 1.4f; b.predict *= 0.3f; b.remnant *= 2.5f; b.velErr *= 2.5f;
    return b;
}
static Skill gBronze = MakeBronze();
// Supersonic Legend: a near-perfect run - quick, direct, brakes right on time, barely any error.
static Skill gSSL    = { 0.20f, 0.90f, 5.f, 1.00f, 0.85f, 0.08f, 0.04f, 0.012f, 0.11f, 0.90f, 0.08f, 0.05f };

// ---- 2.5.0: reference players on Rocket League's 2v2 MMR scale ----
// Bronze: the model Bronze above (~7 s per target on Flick Mixed with no time limit), placed
//   at 200 MMR, the middle of Bronze.
// GC1: fitted to a real player's runs (the user, a GC1-level controller player: 10 runs each of
//   Flick Mixed, Track Control, Hybrid Control and Chase Medium, Medium range). The fit matches
//   his score, targets, time per target, reaction, time on target and catch time to ~5%.
//   Placed at 1497 MMR, the middle of GC1.
// Pro: the GC1 player moved towards the near-perfect run until his pointing speed on Flick Mixed
//   is 4.0 bits/s (Fitts throughput: thumbsticks measure 1.5-2.4 bits/s in lab studies, a mouse
//   3.7-4.9; the GC1 player measures ~2.3) and his reaction (time to start turning at 40 deg/s,
//   as the plugin measures it) is 0.25 s (elite gamers' reaction ~150-200 ms, plus stick travel).
//   The rest lands at human limits: ~0.12 s visual delay (0.10 s visuomotor + system latency),
//   ~7% motor noise, ~10% error judging speed (lab Weber fraction ~5.6%).
//   Placed at 2600 MMR, where the best pros peak (the 2v2 record is 2623).
static Skill gGC1Ref = { 0.16051f, 0.2928f, 45.571f, 0.79807f, 0.44982f, 0.40051f, 0.15294f, 0.040896f, 0.13487f, 0.4581f, 0.55638f, 0.31537f };
static Skill gProRef = { 0.17851f, 0.58316f, 11.745f, 0.91653f, 0.66468f, 0.14908f, 0.067166f, 0.019274f, 0.11901f, 0.69327f, 0.16927f, 0.10188f };
constexpr float kBronzeMMR = 200.f, kGC1MMR = 1497.f, kProMMR = 2600.f;
static Skill Geo(const Skill& a, const Skill& b, float t)
{
    const float* x = &a.rt; const float* y = &b.rt; Skill s; float* o = &s.rt;
    for (int i = 0; i < kParams; ++i) o[i] = x[i] * std::pow(y[i] / x[i], t);
    return s;
}
static Skill SkillAtMMR(float mmr)
{
    if (mmr <= kGC1MMR) return Geo(gBronze, gGC1Ref, (mmr - kBronzeMMR) / (kGC1MMR - kBronzeMMR));
    return Geo(gGC1Ref, gProRef, (mmr - kGC1MMR) / (kProMMR - kGC1MMR));
}

static Skill SkillAt(float r)
{
    r = std::clamp(r, 0.f, 7.f);
    const Skill& a = r <= 5.f ? gBronze : gChamp;
    const Skill& b = r <= 5.f ? gChamp : gSSL;
    float t = r <= 5.f ? r / 5.f : (r - 5.f) / 2.f;
    const float* x = &a.rt; const float* y = &b.rt;
    Skill s; float* o = &s.rt;
    for (int i = 0; i < kParams; ++i) o[i] = x[i] * std::pow(y[i] / x[i], t); // geometric
    return s;
}

constexpr float kRollTorque = 36.07956616966136f, kRollDrag = 4.47166302201591f;
constexpr float kViewDeg = 50.f; // a ball further than this from where you aim is off screen

struct Car
{
    Basis b = BasisFromRotator(0, StartYaw(1), 0);
    float w[3] = { 0, 0, 0 }; // roll, pitch, yaw (rad/s)
    Vector Omega() const { return Add(Add(Scale(b.forward, w[0]), Scale(b.right, -w[1])), Scale(b.up, w[2])); }
    void Step(const float u[3], float dt)
    {
        const float T[3] = { kRollTorque, kPitchTorque, kYawTorque }, D[3] = { kRollDrag, kPitchDrag, kYawDrag };
        for (int i = 0; i < 3; ++i) w[i] += (T[i] * u[i] - D[i] * (1.f - std::fabs(u[i])) * w[i]) * dt;
        float m = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        if (m > kMaxSpin) for (float& x : w) x *= kMaxSpin / m;
        Vector om = Omega();
        float ang = Len(om) * dt;
        if (ang > 1e-9f)
        {
            Vector k = Norm(om);
            b.forward = Norm(RotateAbout(b.forward, k, ang));
            b.up = RotateAbout(b.up, k, ang);
            b.up = Norm(Sub(b.up, Scale(b.forward, Dot(b.up, b.forward))));
            b.right = Cross(b.up, b.forward);
        }
    }
};

struct Snapshot { Basis b; Vector target, targetVel; };

struct Rand
{
    std::mt19937 g;
    std::normal_distribution<float> n{ 0.f, 1.f };
    std::uniform_real_distribution<float> u{ 0.f, 1.f };
    explicit Rand(unsigned s) : g(s) {}
    float N() { return n(g); }
    float U() { return u(g); }
};

// Ornstein-Uhlenbeck noise: slowly wandering error with the given time constant.
struct OU
{
    float x = 0.f;
    void Step(Rand& r, float tau, float dt) { x += -x * dt / tau + std::sqrt(2.f * dt / tau) * r.N(); }
};

struct Player
{
    Skill sk;
    Rand r;
    float stickNoise[3] = { 0, 0, 0 };
    double stickNoiseUntil = 0.0, dirUntil = 0.0;
    float dirOffset = 0.f;
    OU rem1, rem2, vel;
    Player(const Skill& s, unsigned seed) : sk(s), r(seed) {}

    void Command(const Car& car, const Vector& omegaDes, double now, float u[3], float limit)
    {
        float want[3] = { Dot(omegaDes, car.b.forward), -Dot(omegaDes, car.b.right), Dot(omegaDes, car.b.up) };
        const float T[3] = { kRollTorque, kPitchTorque, kYawTorque };
        for (int i = 0; i < 3; ++i) u[i] = std::clamp(10.f * (want[i] - car.w[i]) / T[i], -limit, limit);
        if (now >= stickNoiseUntil) { for (float& x : stickNoise) x = r.N(); stickNoiseUntil = now + 0.06; }
        for (int i = 0; i < 3; ++i) u[i] = std::clamp(u[i] + stickNoise[i] * (sk.noiseRel * std::fabs(u[i]) + sk.noiseAbs), -1.f, 1.f);
    }
};

static float ResidualDelay(const Skill& sk) { return sk.delay * (1.f - sk.predict); }
static float HomingGain(const Skill& sk) { return 1.f / (2.5f * ResidualDelay(sk) + 0.10f); }

// Flick toward a still target.
static Vector FlickOmega(Player& p, const Snapshot& self, const Vector& target, int aim, float brakeK, float targetDeg, double now)
{
    Vector a = AimVec(self.b, aim);
    float th = std::acos(std::clamp(Dot(a, target), -1.f, 1.f));
    Vector axis = Cross(a, target);
    if (Len(axis) < 1e-5f) axis = self.b.right;
    axis = Norm(axis);
    float wDes;
    if (th / kDeg > kViewDeg)
    {
        // Off screen: follow the arrow. The turn direction is judged roughly and re-read
        // every few tenths of a second; speed is held back because you can't see the ball.
        if (now >= p.dirUntil) { p.dirOffset = p.sk.dirErr * kDeg * p.r.N(); p.dirUntil = now + 0.30; }
        axis = Norm(RotateAbout(axis, a, p.dirOffset));
        wDes = p.sk.offSpeed * kMaxSpin;
    }
    else
    {
        float stop = targetDeg * kDeg * 0.2f;
        wDes = std::sqrt(2.f * kPitchTorque * brakeK * std::max(0.f, th - stop));
        wDes = std::min({ wDes, kMaxSpin, HomingGain(p.sk) * th });
    }
    return Scale(axis, wDes);
}

// Follow a moving ball.
static Vector TrackOmega(Player& p, const Snapshot& self, const Snapshot& seen, int aim, float dt)
{
    Vector a = AimVec(self.b, aim);
    float lead = p.sk.delay * p.sk.predict;
    p.vel.Step(p.r, 0.8f, dt);
    Vector vSeen = Scale(seen.targetVel, 1.f + p.sk.velErr * p.vel.x); // misjudged speed
    Vector tgt = Norm(Add(seen.target, Scale(vSeen, lead)));
    float th = std::acos(std::clamp(Dot(a, tgt), -1.f, 1.f));
    Vector axis = Cross(a, tgt);
    Vector corr = Len(axis) > 1e-6f ? Scale(Norm(axis), HomingGain(p.sk) * th) : V(0, 0, 0);
    float cap = std::sqrt(2.f * kPitchTorque * p.sk.brake * th);
    if (th > 0.15f && Len(corr) > cap) corr = Scale(Norm(corr), cap);
    Vector ff = Cross(seen.target, vSeen);
    // remnant: random rate error across the screen, proportional to how fast things move
    p.rem1.Step(p.r, 0.35f, dt); p.rem2.Step(p.r, 0.35f, dt);
    float speed = Len(seen.targetVel) + 20.f * kDeg; // floor: thumb tremor and stick resolution
    Vector e1 = Norm(Cross(a, Len(Cross(a, self.b.up)) > 0.1f ? self.b.up : self.b.forward));
    Vector e2 = Cross(a, e1);
    Vector rem = Scale(Add(Scale(e1, p.rem1.x), Scale(e2, p.rem2.x)), p.sk.remnant * speed);
    return Add(Add(ff, corr), rem);
}

// Targets and ball paths are random every run in the plugin, so each simulated run gets its
// own sequence too (set g_fixedTargets to replay one sequence, for the luck comparison).
static bool g_fixedTargets = false;
static unsigned TargetSeed(int mode, int preset, unsigned runSeed)
{
    return g_fixedTargets ? PresetSeed(mode, preset) : PresetSeed(mode, preset) ^ (runSeed * 2654435761u);
}

struct Result { double score = 0, onPct = 0, avgTime = 0, reaction = 0, targets = 0, misses = 0, avgId = 0; };
// Fitts' index of difficulty of a turn (Shannon form): log2(turn / target width + 1), target width = the ball's diameter.
static double TurnId(float turnDeg, float targetDeg) { return std::log2(turnDeg / (2.f * targetDeg) + 1.f); }

struct Rules { float distance = -1.f; float timeout = -1.f; }; // optional overrides (timeout 0 = none)

// Chase: one ball; catch it when it stops and it dashes off again. Same rules as the plugin.
// Chase reaction: no following for a reaction time after a dash starts, then the speed is
// picked up over g_chaseRamp seconds; after it stops, the hand keeps following for
// g_chaseCarry reaction times. Set so a Champion scores ~20-24k on Chase Medium (Medium range),
// in line with a Champion player's runs (2.3.0; the earlier model followed dashes almost
// perfectly and put Champion near 40k, so real players came out Bronze).
static double g_chaseRamp = 0.5, g_chaseCarry = 2.0;
static Result RunChase(int preset, int range, const Skill& sk, unsigned seed)
{
    const float dt = 1.f / 120.f;
    float target = BallTargetDeg(PresetDistance(kChase, preset, range));
    const MovePreset& m = *MovePresetFor(kChase, preset);
    ChaseParams cp;
    cp.randomSpeed = m.dynamic; cp.speed = m.speed; cp.vMin = m.vMin; cp.vMax = m.vMax;
    cp.moveMin = kChaseMoveMin; cp.moveMax = kChaseMoveMax; cp.band = kBandAny;

    std::mt19937 rng(TargetSeed(kChase, preset, seed));
    Player pl(sk, seed);
    Car car;
    car.b = BasisFromRotator(0, GoalYaw(1), 0); // the run starts once you're on the start ball
    ChaseState c;
    c.path.pos = GoalStartDir(1, kBandAny);
    ChaseLaunch(c, rng, car.b, cp, 0.0);
    std::deque<Snapshot> hist;
    const int delayTicks = std::max(1, (int)std::lround(sk.delay / dt));
    Result res;
    double t = 0.0, sumTime = 0.0, onTime = 0.0, trackTime = 0.0, onSince = -1.0;
    // Humans can't follow a dash the instant it starts: the direction is a surprise, so it takes
    // a reaction time to notice the ball has gone and which way, and a moment more to judge its
    // speed. When it stops, the hand keeps following for a while before braking (overshoot).
    double launchT = 0.0, stopT = -1.0;
    Vector dashVel = V(0, 0, 0);
    bool wasMoving = c.moving;
    while (t < kPresetRunSec)
    {
        Vector prev = c.path.pos;
        ChaseAdvance(c, rng, car.b, cp, t, dt);
        Vector tgt = c.path.pos;
        Vector vel = Scale(Sub(tgt, prev), 1.f / dt);
        if (c.moving && !wasMoving) launchT = t;
        if (!c.moving && wasMoving) stopT = t;
        wasMoving = c.moving;
        if (c.moving) dashVel = vel;
        Vector felt = vel; // the speed the player is acting on
        if (c.moving)
        {
            double since = t - launchT;
            float g = since < sk.rt ? 0.f : (float)std::min(1.0, (since - sk.rt) / g_chaseRamp);
            felt = Scale(vel, g);
        }
        else if (stopT >= 0.0 && t - stopT < g_chaseCarry * sk.rt) felt = dashVel;
        hist.push_back(Snapshot{ car.b, tgt, felt });
        while ((int)hist.size() > delayTicks + 1) hist.pop_front();
        const Snapshot& seen = hist.front();
        int residTicks = std::min((int)hist.size() - 1, (int)std::lround(ResidualDelay(sk) / dt));
        const Snapshot& self = hist[hist.size() - 1 - residTicks];
        float u[3] = { 0, 0, 0 };
        pl.Command(car, TrackOmega(pl, self, seen, 0, dt), t, u, 1.f);
        car.Step(u, dt);
        t += dt;

        float err = AngleDeg(AimVec(car.b, 0), tgt);
        // Scored like Track: 1000 points per second on the ball, partial credit just outside.
        res.score += 1000.0 * TrackCredit(err, target) * dt;
        trackTime += dt;
        if (err <= target) onTime += dt;
        if (err <= target) { if (onSince < 0.0) onSince = t; } else onSince = -1.0;
        if (c.moving) continue;
        if (onSince >= 0.0 && t - std::max(onSince, c.stoppedAt) >= kChaseHoldSec) // caught (held on it for kChaseHoldSec after it stopped): off it goes again
        {
            onSince = -1.0;
            sumTime += t - c.stoppedAt; res.targets += 1;
            ChaseLaunch(c, rng, car.b, cp, t);
        }
        else if (t - c.stoppedAt > kChaseTimeout) { res.misses += 1; ChaseLaunch(c, rng, car.b, cp, t); }
    }
    res.onPct = trackTime > 0 ? 100.0 * onTime / trackTime : -1;
    res.avgTime = res.targets ? sumTime / res.targets : -1; // average time to catch it after it stops
    res.reaction = -1;
    return res;
}

static Result RunPreset(int mode, int preset, int range, const Skill& sk, unsigned seed, Rules rules = {})
{
    if (mode == kChase) return RunChase(preset, range, sk, seed);
    const float dt = 1.f / 120.f;
    float dist = rules.distance > 0 ? rules.distance : PresetDistance(mode, preset, range);
    float target = BallTargetDeg(dist);
    int aim = 0; float lo = 0, hi = 0; Band band = kBandAny; PathParams pp;
    if (mode == kFlick) { const auto& f = kFlickPresets[preset]; lo = f.minTurn; hi = f.maxTurn; aim = f.aim; band = f.band; }
    else
    {
        const MovePreset& m = *MovePresetFor(mode, preset);
        pp.dynamic = m.dynamic; pp.speed = m.dynamic ? (m.vMin + m.vMax) * 0.5f : m.speed;
        pp.vMin = m.vMin; pp.vMax = m.vMax;
        pp.changeEvery = m.dynamic ? ChangeIntervalForSpeed(pp.speed) : m.changeEvery;
        pp.maxTurn = m.maxTurn;
        if (mode == kHybrid) { lo = kHybridFlickMin; hi = kHybridFlickMax; }
    }
    pp.band = band;

    std::mt19937 rng(TargetSeed(mode, preset, seed));
    Player pl(sk, seed);
    Car car;
    Vector startDir = GoalStartDir(1, band);
    std::deque<Snapshot> hist;
    const int delayTicks = std::max(1, (int)std::lround(sk.delay / dt));

    Result res;
    double t = 0.0;
    bool hasTarget = false, tracking = false, reached = false, locking = false, reacted = false;
    double spawnT = 0, lockStart = 0, firstReach = 0, nextSpawn = 0, reactAt = 0, hybEnd = 0;
    float targetAngle = 0, brakeK = 1.f, prevErr = -1.f;
    Vector tgt = startDir, prevTgt = startDir, oldTgt = startDir;
    PathState path;
    double onTime = 0, trackTime = 0, sumTime = 0, sumReact = 0, sumId = 0; int nReact = 0;
    if (mode == kTrack) { PathStart(path, rng, startDir, car.b, pp, 0.0); tracking = true; hasTarget = true; }

    while (t < kPresetRunSec)
    {
        if (mode != kTrack && !hasTarget && t >= nextSpawn)
        {
            std::array<double, kRegionCount> w; w.fill(1.0);
            SpawnResult s = SpawnDirection(rng, car.b, aim, lo, hi, band, 0u, w);
            oldTgt = tgt; tgt = s.dir;
            targetAngle = AngleDeg(AimVec(car.b, aim), s.dir);
            hasTarget = true; reached = false; locking = false; reacted = false; spawnT = t; prevErr = -1.f;
            reactAt = t + sk.rt * std::exp(0.25f * pl.r.N() - 0.03f);
            brakeK = sk.brake * std::exp(sk.brakeSd * pl.r.N());
        }
        if (tracking) { prevTgt = tgt; PathAdvance(path, rng, car.b, 0, pp, t, dt); tgt = path.pos; }

        hist.push_back(Snapshot{ car.b, tgt, tracking ? Scale(Sub(tgt, prevTgt), 1.f / dt) : V(0, 0, 0) });
        while ((int)hist.size() > delayTicks + 1) hist.pop_front();
        const Snapshot& seen = hist.front();
        int residTicks = std::min((int)hist.size() - 1, (int)std::lround(ResidualDelay(sk) / dt));
        const Snapshot& self = hist[hist.size() - 1 - residTicks];

        float u[3] = { 0, 0, 0 };
        Vector omegaDes;
        if (tracking) omegaDes = TrackOmega(pl, self, seen, aim, dt);
        else if (hasTarget && t >= reactAt) omegaDes = FlickOmega(pl, self, tgt, aim, brakeK, target, t);
        else omegaDes = FlickOmega(pl, self, hasTarget ? oldTgt : tgt, aim, 0.5f, target, t);
        float limit = tracking ? 1.f : sk.commit;
        pl.Command(car, omegaDes, t, u, limit);
        car.Step(u, dt);
        t += dt;

        float err = AngleDeg(AimVec(car.b, aim), tgt);
        if (tracking)
        {
            res.score += 1000.0 * TrackCredit(err, target) * dt;
            trackTime += dt;
            if (err <= target) onTime += dt;
            if (mode == kHybrid && t >= hybEnd) { tracking = false; hasTarget = false; nextSpawn = t + kPresetDelaySec; }
            continue;
        }
        if (!hasTarget) continue;
        // the plugin's reaction measure: closing faster than 40 deg/s
        if (!reacted) { if (prevErr >= 0.f && (prevErr - err) / dt > 40.f) { reacted = true; sumReact += t - spawnT; ++nReact; } prevErr = err; }
        bool in = err <= target;
        if (in && !reached) { reached = true; firstReach = t; }
        if (in)
        {
            if (!locking) { locking = true; lockStart = t; }
            if (t - lockStart >= kPresetHoldSec)
            {
                double tt = firstReach - spawnT;
                res.score += FlickPoints(tt, targetAngle);
                sumTime += tt; res.targets += 1; sumId += TurnId(targetAngle, target);
                hasTarget = false; nextSpawn = t + kPresetDelaySec;
                if (mode == kHybrid) { PathStart(path, rng, tgt, car.b, HybridPath(pp), t); tracking = true; hasTarget = true; hybEnd = t + kHybridTrackSec; }
            }
        }
        else locking = false;
        const float timeout = rules.timeout >= 0.f ? rules.timeout : kPresetTimeout;
        if (timeout > 0.f && hasTarget && !tracking && t - spawnT > timeout) { res.misses += 1; res.score = std::max(0.0, res.score - kMissPenalty); hasTarget = false; nextSpawn = t + kPresetDelaySec; }
    }
    res.onPct = trackTime > 0 ? 100.0 * onTime / trackTime : -1;
    res.avgTime = res.targets ? sumTime / res.targets : -1;
    res.reaction = nReact ? sumReact / nReact : -1;
    res.avgId = res.targets ? sumId / res.targets : 0;
    return res;
}

static Result Mean(int mode, int preset, int range, const Skill& sk, int runs, Rules rules = {})
{
    Result m;
    for (int i = 0; i < runs; ++i)
    {
        Result r = RunPreset(mode, preset, range, sk, 1000u + i * 7919u, rules);
        m.score += r.score / runs; m.onPct += r.onPct / runs; m.avgTime += r.avgTime / runs;
        m.reaction += r.reaction / runs; m.targets += r.targets / runs; m.misses += r.misses / runs; m.avgId += r.avgId / runs;
    }
    return m;
}

// A few headline numbers for a profile.
static void Compare(const Skill& sk, int runs)
{
    Result mixed = Mean(kFlick, 4, kMedium, sk, runs);
    Result small = Mean(kFlick, 0, kMedium, sk, runs);
    Result stdT  = Mean(kTrack, 2, kMedium, sk, runs);
    std::printf("  Flick Mixed/Medium : score %6.0f  avg time %.2f s  reaction %.2f s  targets %4.1f\n", mixed.score, mixed.avgTime, mixed.reaction, mixed.targets);
    std::printf("  Flick Small/Medium : score %6.0f  avg time %.2f s  reaction %.2f s  targets %4.1f\n", small.score, small.avgTime, small.reaction, small.targets);
    std::printf("  Track Standard/Med : score %6.0f  on target %4.1f%%\n", stdT.score, stdT.onPct);
}

// A skill from the environment: SKILL="rt,offSpeed,dirErr,commit,brake,brakeSd,noiseRel,noiseAbs,delay,predict,remnant,velErr"
// (absolute values), or SKILL_RANK=<model rank> for SkillAt(rank).
static bool EnvSkill(Skill& out)
{
    if (const char* e = getenv("SKILL"))
    {
        float* x = &out.rt; int n = 0; const char* q = e;
        while (n < kParams && *q) { x[n++] = strtof(q, (char**)&q); while (*q == ',' || *q == ' ') ++q; }
        return n == kParams;
    }
    if (const char* e = getenv("SKILL_RANK")) { out = SkillAt((float)atof(e)); return true; }
    return false;
}

// The four presets measured from a real player's runs (all Medium range), with the same
// numbers the plugin logs: Flick Mixed, Track Control, Hybrid Control, Chase Medium.
static void Metrics(const Skill& sk, int runs)
{
    Result f = Mean(kFlick, 4, kMedium, sk, runs);
    Result t = Mean(kTrack, 1, kMedium, sk, runs);
    Result h = Mean(kHybrid, 0, kMedium, sk, runs);
    Result c = Mean(kChase, 1, kMedium, sk, runs);
    std::printf("TP %.2f bits/s (ID %.2f) | ", f.avgId / std::max(0.05, f.avgTime - f.reaction), f.avgId);
    std::printf("flick %6.0f targets %4.1f time %.3f react %.3f | track %6.0f on %4.1f | hybrid %6.0f reps %4.1f time %.3f on %4.1f | chase %6.0f catches %4.1f time %.3f on %4.1f\n",
                f.score, f.targets, f.avgTime, f.reaction, t.score, t.onPct, h.score, h.targets, h.avgTime, h.onPct, c.score, c.targets, c.avgTime, c.onPct);
}

int main(int argc, char** argv)
{
    std::string cmd = argc > 1 ? argv[1] : "table";
    if (cmd == "metrics") // SKILL or SKILL_RANK from the environment
    {
        Skill sk = SkillAt(5.f); EnvSkill(sk);
        Metrics(sk, argc > 2 ? atoi(argv[2]) : 8);
        return 0;
    }
    if (cmd == "mmrgrid") // every preset/range: mode preset range, then scores at MMR 100, 150, ..., 2650
    {
        const int runs = argc > 2 ? atoi(argv[2]) : 8;
        const char* only = getenv("TIER_MODES");
        for (int mode = 0; mode < kModeCount; ++mode)
        {
            if (only && !strchr(only, '0' + mode)) continue;
            for (int p = 0; p < PresetCount(mode); ++p)
                for (int rg = 0; rg < kRangeCount; ++rg)
                {
                    if (!PresetUsesRange(mode, p) && rg != 0) continue;
                    std::printf("%d %d %d", mode, p, rg);
                    for (int m = 100; m <= 2650; m += 50) std::printf(" %.0f", Mean(mode, p, rg, SkillAtMMR((float)m), runs).score);
                    std::printf("\n");
                    std::fflush(stdout);
                }
        }
        return 0;
    }
    if (cmd == "mmr") // metrics at an MMR
    {
        Metrics(SkillAtMMR(argc > 3 ? (float)atof(argv[3]) : 1497.f), argc > 2 ? atoi(argv[2]) : 8);
        return 0;
    }
    if (cmd == "skill") // print SkillAt(rank)
    {
        Skill sk = SkillAt(argc > 2 ? (float)atof(argv[2]) : 5.f); const float* x = &sk.rt;
        for (int i = 0; i < kParams; ++i) std::printf("%s%.4g", i ? "," : "", x[i]);
        std::printf("\n");
        return 0;
    }
    if (const char* e = getenv("CHASE_RAMP")) g_chaseRamp = atof(e);
    if (const char* e = getenv("CHASE_CARRY")) g_chaseCarry = atof(e);
    int runs = argc > 2 ? atoi(argv[2]) : 8;
    if (cmd == "luck") // how much the random sequence adds to run-to-run spread, for a Champion
    {
        struct P { int m, p, r; const char* name; } list[] = { { 0, 4, 1, "Flick Mixed" }, { 0, 0, 1, "Flick Small" }, { 1, 2, 1, "Track Standard" },
                                                               { 2, 0, 1, "Hybrid Control" }, { 3, 1, 1, "Chase Medium" } };
        for (const auto& e : list)
        {
            double sd[2], mean[2];
            for (int fixed = 1; fixed >= 0; --fixed)
            {
                g_fixedTargets = fixed;
                std::vector<double> v;
                for (int i = 0; i < runs; ++i) v.push_back(RunPreset(e.m, e.p, e.r, SkillAt(5.f), 1000u + i * 7919u).score);
                double mu = 0; for (double x : v) mu += x; mu /= v.size();
                double q = 0; for (double x : v) q += (x - mu) * (x - mu);
                sd[fixed] = std::sqrt(q / (v.size() - 1)); mean[fixed] = mu;
            }
            std::printf("  %-15s mean %6.0f   spread (1 sd): same targets %4.1f%%, random targets %4.1f%%\n", e.name, mean[0], 100 * sd[1] / mean[1], 100 * sd[0] / mean[0]);
        }
        return 0;
    }
    if (cmd == "natural") // average time per target with no timeout: mode preset range rank
    {
        int m = atoi(argv[3]), p = atoi(argv[4]), rg = atoi(argv[5]); float rk = argc > 6 ? (float)atof(argv[6]) : 0.f;
        Rules rl; rl.timeout = 0.f;
        Result r = Mean(m, p, rg, SkillAt(rk), runs, rl);
        Result r5 = Mean(m, p, rg, SkillAt(rk), runs);
        std::printf("rank %.1f  no timeout: avgT %.2f s targets %.1f score %.0f | 5 s timeout: avgT %.2f targets %.1f misses %.1f score %.0f\n",
                    rk, r.avgTime, r.targets, r.score, r5.avgTime, r5.targets, r5.misses, r5.score);
        return 0;
    }
    if (cmd == "compare")
    {
        for (float r : { 0.f, 2.5f, 5.f, 7.f }) { std::printf("rank %.1f\n", r); Compare(SkillAt(r), runs); }
        return 0;
    }
    if (cmd == "profile") // print a preset's score at each half rank
    {
        int m = atoi(argv[3]), p = atoi(argv[4]), rg = atoi(argv[5]);
        for (int k = 0; k <= 14; ++k)
        {
            Result r = Mean(m, p, rg, SkillAt(k * 0.5f), runs);
            std::printf("rank %.1f score %.0f on %.1f avgT %.2f targets %.1f miss %.1f\n", k * 0.5f, r.score, r.onPct, r.avgTime, r.targets, r.misses);
        }
        return 0;
    }
    if (cmd == "grid") // every preset/range: mode preset range, then scores at ranks 0, 0.25, ..., 7
    {
        const char* only = getenv("TIER_MODES");
        for (int mode = 0; mode < kModeCount; ++mode)
        {
            if (only && !strchr(only, '0' + mode)) continue;
            for (int p = 0; p < PresetCount(mode); ++p)
                for (int rg = 0; rg < kRangeCount; ++rg)
                {
                    if (!PresetUsesRange(mode, p) && rg != 0) continue;
                    std::printf("%d %d %d", mode, p, rg);
                    for (int k = 0; k <= 28; ++k) std::printf(" %.0f", Mean(mode, p, rg, SkillAt(k * 0.25f), runs).score);
                    std::printf("\n");
                    std::fflush(stdout);
                }
        }
        return 0;
    }
    // table: every preset/range, scores at half ranks 0..7
    const char* only = getenv("TIER_MODES"); // e.g. "02" = Flick and Hybrid only
    for (int mode = 0; mode < kModeCount; ++mode)
        if (!only || strchr(only, '0' + mode))
        for (int p = 0; p < PresetCount(mode); ++p)
            for (int rg = 0; rg < kRangeCount; ++rg)
            {
                if (!PresetUsesRange(mode, p) && rg != 0) continue;
                std::printf("%d %d %d", mode, p, rg);
                for (int k = 0; k <= 10; ++k) std::printf(" %.0f", Mean(mode, p, rg, SkillAt(k * 0.5f), runs).score);
                std::printf(" %.0f", Mean(mode, p, rg, SkillAt(7.f), runs).score);
                std::printf("\n");
                std::fflush(stdout);
            }
}
