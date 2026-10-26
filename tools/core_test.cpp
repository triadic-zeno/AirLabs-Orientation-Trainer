// Unit tests for AirLabs/TrainerCore.h, run outside the game with a stand-in Vector type.
// Build: g++ -std=c++20 -O2 -I../AirLabs core_test.cpp -o core_test
#include <cstdio>
#include <vector>
#include <string>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h"

using namespace airlabs;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Basis RandomBasis(std::mt19937& r)
{
    std::uniform_int_distribution<int> u(-32768, 32767);
    return BasisFromRotator(u(r), u(r), u(r));
}

// Motion direction of the ball between two positions, as a tangent at `from`.
static Vector Motion(const Vector& from, const Vector& to)
{
    Vector d = Sub(to, from);
    return Norm(Sub(d, Scale(from, Dot(d, from))));
}

struct PathRunStats { float maxSpeed = 0, minSpeed = 1e9f, maxEpisodeTurn = 0, minElev = 90, maxElev = -90; std::vector<float> turnSeq, speedSeq; };

static PathRunStats RunPath(const PathParams& p, unsigned seed, std::mt19937& world, float seconds = 60.f, bool record = false)
{
    PathRunStats st;
    Basis car = RandomBasis(world);
    Vector start = StartBallDir(car, 0, p.band);
    std::mt19937 rng(seed);
    PathState s;
    double t = 0.0; const float dt = 1.f / 120.f;
    PathStart(s, rng, start, car, p, t);
    Vector prevPos = s.pos, prevMotion{ 0, 0, 0 }; bool haveMotion = false;
    float episode = 0.f; double lastChange = s.nextChange;
    for (int i = 0; i < int(seconds * 120); ++i)
    {
        t += dt;
        double before = s.nextChange;
        PathAdvance(s, rng, car, 0, p, t, dt);
        if (s.nextChange != before) { st.maxEpisodeTurn = std::max(st.maxEpisodeTurn, std::fabs(episode)); episode = 0.f; lastChange = s.nextChange; }
        Vector m = Motion(prevPos, s.pos);
        if (haveMotion)
        {
            // transport the previous motion direction to the new position, then measure the turn
            Vector pm = Norm(Sub(prevMotion, Scale(s.pos, Dot(prevMotion, s.pos))));
            float turn = SignedAngleDeg(pm, m, s.pos);
            episode += turn;
            if (record) st.turnSeq.push_back(turn);
        }
        prevMotion = m; haveMotion = true; prevPos = s.pos;
        st.maxSpeed = std::max(st.maxSpeed, s.speed);
        st.minSpeed = std::min(st.minSpeed, s.speed);
        if (record) st.speedSeq.push_back(s.speed);
        float e = ElevationDeg(s.pos);
        st.minElev = std::min(st.minElev, e); st.maxElev = std::max(st.maxElev, e);
    }
    (void)lastChange;
    return st;
}

int main()
{
    std::mt19937 world(12345);

    std::printf("== Track and Hybrid presets: speed, max direction change, determinism ==\n");
    for (int mode : { kTrack, kHybrid })
        for (int i = 0; i < PresetCount(mode); ++i)
        {
            const MovePreset& mp = *MovePresetFor(mode, i);
            PathParams p; p.dynamic = mp.dynamic; p.speed = mp.speed; p.vMin = mp.vMin; p.vMax = mp.vMax;
            p.changeEvery = mp.changeEvery; p.maxTurn = mp.maxTurn;
            float worstTurn = 0, hi = 0, lo = 1e9f;
            for (int run = 0; run < 30; ++run)
            {
                auto st = RunPath(p, 1000 + run, world);
                worstTurn = std::max(worstTurn, st.maxEpisodeTurn); hi = std::max(hi, st.maxSpeed); lo = std::min(lo, st.minSpeed);
            }
            auto a = RunPath(p, 777, world, 20.f, true), b = RunPath(p, 777, world, 20.f, true);
            float maxDiff = 0; for (size_t k = 0; k < a.turnSeq.size() && k < b.turnSeq.size(); ++k) maxDiff = std::max(maxDiff, std::fabs(a.turnSeq[k] - b.turnSeq[k]));
            std::printf("  %-6s %-8s %-9s speeds %5.1f-%5.1f, largest direction change %5.1f deg (limit %.0f), determinism diff %.4f\n",
                        ModeName(mode), mp.group, mp.name, lo, hi, worstTurn, mp.maxTurn, maxDiff);
            float cap = mp.dynamic ? mp.vMax : mp.speed;
            CHECK(hi <= cap + 1e-3f && hi <= kMaxBallSpeed + 1e-3f, "%s speed exceeded", mp.name);
            if (mp.dynamic) CHECK(lo >= mp.vMin - 1e-3f && hi - lo > 0.6f * (mp.vMax - mp.vMin), "%s dynamic range wrong", mp.name);
            CHECK(worstTurn <= mp.maxTurn + 3.f, "%s turned %.1f > %.0f", mp.name, worstTurn, mp.maxTurn);
            CHECK(maxDiff < 0.05f, "%s not deterministic", mp.name);
        }
    CHECK(kTrackPresets[4].maxTurn == 60.f && kHybridPresets[2].maxTurn == 60.f, "Insane max turn should be 60");

    std::printf("== Height ranges: the ball stays inside ==\n");
    struct NamedBand { const char* name; Band b; } bands[] = { { "Above", kBandAbove }, { "Level", kBandLevel }, { "Below", kBandBelow }, { "Narrow 40-50", { 40.f, 50.f } } };
    for (const auto& nb : bands)
        for (const auto& tp : kTrackPresets)
        {
            if (tp.dynamic) continue;
            PathParams p; p.speed = tp.speed; p.changeEvery = tp.changeEvery; p.maxTurn = tp.maxTurn; p.band = nb.b;
            float lo = 90, hi = -90;
            for (int run = 0; run < 25; ++run) { auto st = RunPath(p, 3000 + run, world); lo = std::min(lo, st.minElev); hi = std::max(hi, st.maxElev); }
            if (&tp == &kTrackPresets[0] || &tp == &kTrackPresets[4])
                std::printf("  %-12s %-9s: height seen %6.1f to %5.1f (range %.0f to %.0f)\n", nb.name, tp.name, lo, hi, nb.b.minDeg, nb.b.maxDeg);
            CHECK(lo >= nb.b.minDeg - 0.5f && hi <= nb.b.maxDeg + 0.5f, "%s/%s left the band (%.1f..%.1f)", nb.name, tp.name, lo, hi);
        }

    std::printf("== Flick presets: turn range, height, spread ==\n");
    std::array<double, kRegionCount> ones; ones.fill(1.0);
    for (const auto& fp : kFlickPresets)
    {
        int n = 20000, outOfRange = 0, outOfBand = 0, fallbacks = 0, near135 = 0;
        double sum = 0, expectNum = 0, expectDen = 0;
        std::mt19937 rng(42);
        for (int i = 0; i < n; ++i)
        {
            Basis car = RandomBasis(world);
            auto r = SpawnDirection(rng, car, fp.aim, fp.minTurn, fp.maxTurn, fp.band, kAllRegions, ones);
            float turn = AngleDeg(AimVec(car, fp.aim), r.dir);
            if (turn < fp.minTurn - 0.01f || turn > fp.maxTurn + 0.01f) ++outOfRange;
            if (!fp.band.Contains(r.dir)) ++outOfBand;
            if (r.fallback) ++fallbacks;
            if (std::fabs(turn - 135.f) < 0.5f) ++near135;
            sum += turn;
        }
        for (int i = 0; i < 1000; ++i) { float th = fp.minTurn + (fp.maxTurn - fp.minTurn) * (i + 0.5f) / 1000.f; expectNum += th * std::sin(th * kDeg); expectDen += std::sin(th * kDeg); }
        std::printf("  %-10s %3.0f-%3.0f: avg turn %5.1f (even spread would be %5.1f), out of turn range %d, out of height range %d, fallbacks %d, within 0.5 deg of 135: %.1f%%\n",
                    fp.name, fp.minTurn, fp.maxTurn, sum / n, expectNum / expectDen, outOfRange, outOfBand, fallbacks, 100.0 * near135 / n);
        CHECK(outOfBand == 0, "%s spawned outside height range", fp.name);
        if (fp.band.Full()) CHECK(outOfRange == 0, "%s spawned outside turn range", fp.name);
        if (fp.band.Full()) CHECK(std::fabs(sum / n - expectNum / expectDen) < 2.0, "%s spread is uneven", fp.name);
    }

    std::printf("== Start ball: inside the height range, a little above your aim ==\n");
    {
        int bad = 0; float avgOffset = 0; int cnt = 0;
        for (int i = 0; i < 5000; ++i)
        {
            Basis car = RandomBasis(world);
            for (const auto& nb : bands) { Vector d = StartBallDir(car, 0, nb.b); if (!nb.b.Contains(d)) ++bad; }
            Vector d = StartBallDir(car, 0, kBandAny); avgOffset += AngleDeg(car.forward, d); ++cnt;
        }
        Basis upright = BasisFromRotator(0, 0, 0);
        Vector d = StartBallDir(upright, 0, kBandAny);
        std::printf("  outside range: %d, average offset from aim %.1f deg, upright car: %.1f deg above the horizon\n", bad, avgOffset / cnt, ElevationDeg(d));
        CHECK(bad == 0, "start ball outside band");
    }

    std::printf("== Round start: ball between car and goal, car turned 45 deg away from it ==\n");
    for (int g : { 1, -1 })
    {
        Basis goalFacing = BasisFromRotator(0, GoalYaw(g), 0);
        CHECK(std::fabs(goalFacing.forward.Y - g) < 1e-4f, "goal yaw doesn't face the goal (sign %d)", g);
        Basis start = BasisFromRotator(0, StartYaw(g), 0);
        CHECK(std::fabs(start.up.Z - 1.f) < 1e-4f, "start isn't level");
        float off = AngleDeg(start.forward, GoalStartDir(g, kBandAny));
        CHECK(std::fabs(off - 45.f) < 0.1f, "car should start 45 deg off the ball, got %.1f", off);
        for (int r = 0; r < kRangeCount; ++r) CHECK(off > BallTargetDeg(RangeDistance(r)) + 20.f, "car starts on (or near) the ball");
        for (const auto& nb : bands)
        {
            Vector d = GoalStartDir(g, nb.b);
            CHECK(nb.b.Contains(d), "goal start ball outside band");
            CHECK(d.Y * g > 0.2f && std::fabs(d.X) < 1e-4f, "goal start ball not on the goal side");
        }
        Vector d = GoalStartDir(g, kBandAny);
        std::printf("  goal %+d: forward (%.2f, %.2f, %.2f), level ball %.1f deg off the nose, Above-range ball %.1f deg up\n",
                    g, start.forward.X, start.forward.Y, start.forward.Z, AngleDeg(start.forward, d), ElevationDeg(GoalStartDir(g, kBandAbove)));
    }

    std::printf("== Chase: dash, stop, wait, dash again ==\n");
    {
        Basis car = BasisFromRotator(0, GoalYaw(1), 0);
        for (int p = 0; p < PresetCount(kChase); ++p)
        {
            const MovePreset& m = *MovePresetFor(kChase, p);
            ChaseParams cp; cp.randomSpeed = m.dynamic; cp.speed = m.speed; cp.vMin = m.vMin; cp.vMax = m.vMax;
            cp.moveMin = kChaseMoveMin; cp.moveMax = kChaseMoveMax;
            std::mt19937 r1(PresetSeed(kChase, p)), r2(PresetSeed(kChase, p));
            ChaseState a, b; a.path.pos = b.path.pos = GoalStartDir(1, kBandAny);
            const float dt = 1.f / 120.f;
            double t = 0.0; int dashes = 0; float minV = 1e9f, maxV = 0.f, worstSpeedErr = 0.f, maxStillDrift = 0.f;
            bool same = true;
            for (int k = 0; k < 20; ++k)
            {
                ChaseLaunch(a, r1, car, cp, t); ChaseLaunch(b, r2, car, cp, t); ++dashes;
                minV = std::min(minV, a.dashSpeed); maxV = std::max(maxV, a.dashSpeed);
                Vector start = a.path.pos; double t0 = t;
                CHECK(a.moving && a.stopAt - t >= kChaseMoveMin - 1e-6 && a.stopAt - t <= kChaseMoveMax + 1e-6, "dash length out of range");
                while (a.moving) { ChaseAdvance(a, r1, car, cp, t, dt); ChaseAdvance(b, r2, car, cp, t, dt); t += dt; }
                float travelled = AngleDeg(start, a.path.pos), expected = a.dashSpeed * static_cast<float>(t - t0);
                // straight across the sky: the angle travelled matches speed x time (less for long dashes past 180 deg)
                if (expected < 170.f) worstSpeedErr = std::max(worstSpeedErr, std::fabs(travelled - expected));
                Vector stopped = a.path.pos;
                for (int i = 0; i < 60; ++i) { ChaseAdvance(a, r1, car, cp, t, dt); t += dt; }
                maxStillDrift = std::max(maxStillDrift, AngleDeg(stopped, a.path.pos));
                for (int i = 0; i < 60; ++i) { ChaseAdvance(b, r2, car, cp, t - 60 * dt + i * dt, dt); }
                same &= AngleDeg(a.path.pos, b.path.pos) < 0.05f; // (acos of a dot product near 1 has ~0.02 deg of float noise)
            }
            CHECK(maxStillDrift < 0.05f, "%s: the ball moved while stopped", m.name);
            CHECK(worstSpeedErr < 2.f, "%s: dash distance doesn't match speed x time (%.2f deg off)", m.name, worstSpeedErr);
            CHECK(same, "%s: same seed gave different dashes", m.name);
            if (m.dynamic) CHECK(minV >= m.vMin - 1e-3f && maxV <= m.vMax + 1e-3f && maxV - minV > 20.f, "%s: random speeds out of range", m.name);
            else CHECK(minV == m.speed && maxV == m.speed, "%s: fixed speed changed", m.name);
            std::printf("  %-7s %d dashes, speed %.0f-%.0f deg/s, distance error %.2f deg\n", m.name, dashes, minV, maxV, worstSpeedErr);
        }
        // Height range is respected.
        ChaseParams cp; cp.speed = 90.f; cp.band = kBandAbove;
        std::mt19937 r(7); ChaseState c; c.path.pos = GoalStartDir(1, kBandAbove);
        float lowest = 90.f; double t = 0.0;
        for (int k = 0; k < 200; ++k)
        {
            ChaseLaunch(c, r, car, cp, t);
            while (c.moving) { ChaseAdvance(c, r, car, cp, t, 1.f / 120.f); t += 1.0 / 120.0; lowest = std::min(lowest, ElevationDeg(c.path.pos)); }
        }
        CHECK(lowest >= kBandAbove.minDeg - 0.1f, "Chase left the height range (%.1f deg)", lowest);
    }

    std::printf("== Par times ==\n");
    for (float a : { 15.f, 45.f, 90.f, 135.f, 180.f }) std::printf("  %3.0f deg: fastest possible %.2f s, par %.2f s\n", a, FastestTurnSec(a), ParSec(a));

    std::printf("== Rank marks: 22 divisions, rising, reachable, harder at longer range ==\n");
    CHECK(kTierCount == 23 && std::string(TierName(0)) == "Bronze I" && std::string(TierName(17)) == "Champion III" &&
          std::string(TierName(18)) == "Grand Champion I" && std::string(TierName(kSSLTier)) == "Supersonic Legend" &&
          std::string(TierName(kProTier)) == "Pro" && std::string(TierShort(kProTier)) == "PRO" &&
          std::string(TierShort(18)) == "GC1" && TierRank(20) == 6 && TierRank(kSSLTier) == 7 && TierRank(kProTier) == 8, "rank names");
    for (int m = 0; m < kModeCount; ++m)
        for (int p = 0; p < PresetCount(m); ++p)
            for (int r = 0; r < kRangeCount; ++r)
            {
                CHECK(TierThreshold(m, p, r, 0) == 0.f, "Bronze needs 0");
                for (int t = 1; t < kTierCount; ++t)
                {
                    CHECK(TierThreshold(m, p, r, t) > TierThreshold(m, p, r, t - 1), "%s %s %s: tier %d not above %d", ModeName(m), PresetName(m, p), RangeName(r), t, t - 1);
                    if (m == kTrack) CHECK(TierThreshold(m, p, r, t) < 60000.f, "track tier above the maximum");
                    if (r > 0 && PresetUsesRange(m, p)) CHECK(TierThreshold(m, p, r, t) <= TierThreshold(m, p, r - 1, t), "%s %s: %s easier than shorter range (tier %d)", ModeName(m), PresetName(m, p), RangeName(r), t);
                    float th = TierThreshold(m, p, r, t);
                    CHECK(TierIndex(th, m, p, r) == t && TierIndex(th - 1.f, m, p, r) == t - 1, "TierIndex boundary");
                }
            }
    for (int m = 0; m < kModeCount; ++m)
        for (int p = 0; p < PresetCount(m); ++p)
            std::printf("  %-6s %-10s Medium: Gold I %6.0f  Champion I %6.0f  GC I %6.0f  SSL %6.0f\n", ModeName(m), PresetName(m, p),
                        TierThreshold(m, p, kMedium, 6), TierThreshold(m, p, kMedium, 15), TierThreshold(m, p, kMedium, 18), TierThreshold(m, p, kMedium, 21));

    std::printf("== Rank value and rating ==\n");
    {
        // Rank value: each division's mark is exactly i/3, and it never goes down as the score rises.
        for (int m = 0; m < kModeCount; ++m)
            for (int p = 0; p < PresetCount(m); ++p)
            {
                float prev = -1.f;
                for (int t = 0; t < kTierCount; ++t)
                {
                    float v = RankValue(TierThreshold(m, p, kMedium, t), m, p, kMedium);
                    CHECK(std::fabs(v - t / 3.f) < 1e-3f && TierFromValue(v) == t, "%s %s: rank value at division %d is %.3f", ModeName(m), PresetName(m, p), t, v);
                    CHECK(v > prev, "rank value not rising");
                    prev = v;
                }
            }
        // The calibration anchors: a GC1 player's averages sit in Grand Champion I.
        struct A { int m, p; float s; } anchors[] = { { kFlick, 4, 27140.f }, { kTrack, 1, 46915.f }, { kHybrid, 0, 42418.f }, { kChase, 1, 26612.f } };
        for (const A& a : anchors) CHECK(TierFromValue(RankValue(a.s, a.m, a.p, kMedium)) == 18, "%s %s: anchor not GC1", ModeName(a.m), PresetName(a.m, a.p));
        // ...and the rank built from those runs, in the order played (weighted average with
        // placements and the step limit), is Grand Champion I too.
        struct Runs { int m, p; std::vector<float> s; } played[] = {
            { kFlick, 4, { 24463, 29862, 0, 27388, 28196, 30554, 25390, 32606, 23830, 26019, 23086 } },
            { kTrack, 1, { 47960, 47678, 48473, 46952, 46304, 43308, 46250, 47209, 47356, 48204, 46374 } },
            { kHybrid, 0, { 39033, 36882, 45862, 45621, 42758, 38019, 42961, 44192, 43046, 45808 } },
            { kChase, 1, { 28578, 24930, 24106, 26903, 26127, 27562, 24706, 29629, 28827, 24753 } } };
        for (const Runs& r : played)
        {
            RankState st;
            for (float v : r.s) RankUpdate(st, RankValue(v, r.m, r.p, kMedium));
            CHECK(TierFromValue(st.value) == 18, "%s %s: rank from the anchor runs is %s, not GC1", ModeName(r.m), PresetName(r.m, r.p), TierName(TierFromValue(st.value)));
        }
        // Placements are the plain average; then a fluke only nudges; a disaster is limited.
        RankState st;
        for (int i = 0; i < kPlacementRuns; ++i) RankUpdate(st, i % 2 ? 5.5f : 5.3f);
        CHECK(std::fabs(st.value - 5.4f) < 1e-4f && !RankPlacing(st), "placement average");
        RankState fluke = st; RankUpdate(fluke, 6.8f);   // C2 player, GC3 run
        CHECK(fluke.value > st.value && fluke.value - st.value < 1.f / 3.f, "a fluke moves less than a division");
        RankState afk = st; RankUpdate(afk, 0.f);        // a 0-score run
        CHECK(std::fabs((st.value - afk.value) - kRankRate * kRankStepLimit) < 1e-4f, "a disaster is capped");
        RankState up = st; for (int i = 0; i < 10; ++i) RankUpdate(up, 6.4f);
        CHECK(up.value > 6.0f, "ten runs at a new level carry most of the way");
        std::printf("  placements -> %.2f; GC3 fluke -> %.3f; 0 run -> %.3f; ten GC2 runs -> %.2f\n", st.value, fluke.value, afk.value, up.value);
    }

    std::printf("== Stick needed for each Track speed ==\n");
    for (const auto& tp : kTrackPresets) if (!tp.dynamic) std::printf("  %-10s %2.0f deg/s: %4.1f%% of max spin, stick %.1f%% pitch / %.1f%% yaw\n", tp.name, tp.speed, PctOfMaxSpin(tp.speed), 100 * StickForSpin(tp.speed, true), 100 * StickForSpin(tp.speed, false));

    {
        float above = 2044.f - 92.75f - kArenaCentreZ, below = kArenaCentreZ - 92.75f;
        std::printf("== Arena: room for the ball from the centre pin: %.0f uu up, %.0f uu down (max distance %.0f) ==\n", above, below, kMaxBallDistance);
        CHECK(kMaxBallDistance <= above && kMaxBallDistance <= below, "ball can touch roof/floor");
    }
    std::printf("== Target size (the ball itself) ==\n");
    {
        for (float d : { 300.f, 700.f, 750.f, 800.f, 920.f })
            std::printf("  %4.0f uu: %.2f deg\n", d, BallTargetDeg(d));
        CHECK(std::fabs(BallTargetDeg(700.f) - 7.7f) < 0.1f, "700 uu should be ~7.7 deg (got %.2f)", BallTargetDeg(700.f));
        CHECK(std::fabs(BallTargetDeg(920.f) - 5.9f) < 0.1f, "920 uu should be ~5.9 deg (got %.2f)", BallTargetDeg(920.f));
        // Ring radius on screen = distance * tan(target): just outside the real ball (92.75 uu).
        for (float d : { 300.f, 750.f, 920.f })
        {
            float ringR = d * std::tan(BallTargetDeg(d) * kDeg);
            CHECK(ringR > 92.75f && ringR < 100.f, "ring radius at %.0f uu is %.1f", d, ringR);
        }
        CHECK(kPresetDelaySec == 0.02f && kPresetTimeout == 5.f && kChaseTimeout == 7.5f && kMissPenalty == 500.f, "preset rules");
        CHECK(RangeDistance(kShort) == 400.f && RangeDistance(kMedium) == 600.f && RangeDistance(kLong) == 900.f, "ranges 400/600/900");
        CHECK(RangeDistance(kLong) <= kMaxBallDistance, "Long range fits in the arena");
        for (int r = 0; r < kRangeCount; ++r)
            std::printf("  %-6s %3.0f uu: target %.1f deg\n", RangeName(r), RangeDistance(r), BallTargetDeg(RangeDistance(r)));
        CHECK(kModeCount == 4 && std::string(ModeName(kHybrid)) == "Hybrid" && std::string(ModeName(kChase)) == "Chase", "four modes");
        CHECK(!PresetUsesRange(kFlick, 5) && PresetDistance(kFlick, 5, kLong) == 350.f, "Flip Reset fixed at 350 uu");
        CHECK(PresetUsesRange(kFlick, 0) && PresetDistance(kFlick, 0, kShort) == 400.f && PresetDistance(kTrack, 2, kLong) == 900.f, "other presets follow the range");
        std::printf("  Flip Reset (fixed) %3.0f uu: target %.1f deg\n", PresetDistance(kFlick, 5, 0), BallTargetDeg(PresetDistance(kFlick, 5, 0)));
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
