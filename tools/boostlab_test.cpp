// Tests for AirLabs/BoostLab.h, run outside the game with a stand-in Vector type.
// Build: g++ -std=c++20 -O2 -IAirLabs tools/boostlab_test.cpp -o boostlab_test
#include <cstdio>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h"
#include "BoostLab.h"

using namespace airlabs;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// A car that turns its nose onto where the ball will be and boosts while it's roughly lined up.
struct Bot
{
    Basis car = BasisFromRotator(0, 0, 0);
    bool Drive(const LabState& s)
    {
        Vector rel = LabRelVel(s);
        float dist = Len(s.ballPos);
        float t = std::clamp(dist / 1200.f, 0.05f, 1.0f);
        Vector aim = Norm(Add(s.ballPos, Scale(rel, t)));
        // turn instantly (this tests the physics, not the steering)
        Vector up = std::fabs(aim.Z) < 0.95f ? V(0.f, 0.f, 1.f) : V(1.f, 0.f, 0.f);
        Vector right = Norm(Cross(up, aim));
        car = Basis{ aim, right, Cross(aim, right) };
        return true;
    }
};

int main()
{
    std::mt19937 rng(7);

    // Directions: even over the sphere (no bunching at the top and bottom).
    {
        int top = 0, band = 0;
        const int n = 200000;
        for (int i = 0; i < n; ++i)
        {
            Vector d = LabRandomDir(rng);
            if (d.Z > 0.9f) ++top;
            if (std::fabs(d.Z) < 0.1f) ++band;
        }
        // Equal-height slices of a sphere have equal area: z > 0.9 and |z| < 0.1 are both 5%.
        CHECK(std::abs(top - n / 20) < n / 200, "top cap %d, expected about %d", top, n / 20);
        CHECK(std::abs(band - n / 10) < n / 100, "equator band %d, expected about %d", band, n / 10);
    }

    // Launch and bounce directions keep the pass distance and cross the bubble.
    {
        LabParams p;
        int bad = 0;
        for (int i = 0; i < 20000; ++i)
        {
            Vector pos = (i % 2) ? Scale(LabRandomDir(rng), p.bubble) : LabSpawnPoint(rng, p);
            Vector d = LabPickDirection(rng, pos, p.bubble, p.pass, p.bubble);
            if (LabPassDistance(pos, d) < p.pass - 0.5f || LabExitDistance(pos, d, p.bubble) < p.bubble - 0.5f) ++bad;
        }
        CHECK(bad == 0, "%d directions broke the pass distance or crossed too little", bad);
        // From the edge that's a band 18-60 degrees off the inward direction.
        Vector edge = V(p.bubble, 0.f, 0.f);
        float lo = 180.f, hi = 0.f;
        for (int i = 0; i < 5000; ++i)
        {
            float a = AngleDeg(LabPickDirection(rng, edge, p.bubble, p.pass, p.bubble), V(-1.f, 0.f, 0.f));
            lo = std::min(lo, a); hi = std::max(hi, a);
        }
        CHECK(lo > 17.5f && lo < 20.f && hi < 60.5f && hi > 57.f, "edge bounce angles %.1f-%.1f deg (want about 18-60)", lo, hi);
        std::printf("edge bounce: %.1f-%.1f deg off straight in\n", lo, hi);
    }

    // Spawns are inside the shell.
    {
        LabParams p;
        p.bubble = 2000.f;
        for (int i = 0; i < 1000; ++i)
        {
            float r = Len(LabSpawnPoint(rng, p));
            CHECK(r >= p.spawnMin - 0.1f && r <= p.bubble - 59.f, "spawn at %.0f", r);
            if (r < p.spawnMin - 0.1f || r > p.bubble - 59.f) break;
        }
    }

    // Boost: about 1 s from standing to ~990 uu/s, capped at 2300, and you keep drifting.
    {
        LabParams p;
        p.bubble = 50000.f; // nothing respawns during the test
        LabState s;
        LabStart(s, rng, p, 0.0);
        s.ballPos = V(0.f, 900.f, 0.f); // out of the way
        Basis car = BasisFromRotator(0, 0, 0);
        double t = 0.0;
        for (int i = 0; i < 120; ++i) { LabStep(s, rng, p, car, LabHitbox{}, true, 0.f, t, 1.f / 120.f); t += 1.0 / 120.0; }
        float v1 = Len(s.carVel);
        CHECK(std::fabs(v1 - 991.7f) < 2.f, "after 1 s of boost: %.1f uu/s", v1);
        for (int i = 0; i < 600; ++i) { LabStep(s, rng, p, car, LabHitbox{}, true, 0.f, t, 1.f / 120.f); t += 1.0 / 120.0; }
        CHECK(std::fabs(Len(s.carVel) - 2300.f) < 0.5f, "capped speed %.1f", Len(s.carVel));
        Vector before = s.carVel;
        Basis turned = BasisFromRotator(0, 16384, 0); // turn 90 degrees without boosting
        LabStep(s, rng, p, turned, LabHitbox{}, false, 0.f, t, 1.f / 120.f);
        CHECK(Len(Sub(s.carVel, before)) < 0.01f, "turning without boost changed your velocity");
    }

    // Touch: the ball leaves at the touch speed along centre -> ball, and never ends up inside the car.
    {
        LabParams p;
        LabState s;
        LabStart(s, rng, p, 0.0);
        s.phase = kLabFlying;
        s.ballPos = V(400.f, 0.f, 20.f);
        s.ballVel = V(-1200.f, 0.f, 0.f); // coming straight at the nose (test only)
        Basis car = BasisFromRotator(0, 0, 0);
        double t = 0.0;
        float closest = 1e9f;
        for (int i = 0; i < 120 && s.touches == 0; ++i)
        {
            LabStep(s, rng, p, car, LabHitbox{}, false, 0.f, t, 1.f / 120.f);
            closest = std::min(closest, LabBoxDistance(s.ballPos, car, LabHitbox{}));
            t += 1.0 / 120.0;
        }
        CHECK(s.touches == 1, "no touch");
        CHECK(closest >= kLabBallRadius - 0.01f, "the ball went into the car (%.1f from the box)", closest);
        CHECK(std::fabs(s.lastClosing - 1200.f) < 1.f, "closing speed %.0f", s.lastClosing);
        CHECK(std::fabs(Len(LabRelVel(s)) - p.hitSpeed) < 1.f && LabRelVel(s).X > 1400.f, "after the touch: %.0f uu/s", Len(LabRelVel(s)));
        CHECK(s.phase == kLabTouched, "phase after a Respawn touch");
    }

    // Constant: the ball never leaves the bubble, even if you boost away from it flat out.
    {
        LabParams p;
        p.motion = kLabConstant;
        LabState s;
        LabStart(s, rng, p, 0.0);
        double t = 0.0;
        float maxR = 0.f;
        std::uniform_int_distribution<int> u(-32768, 32767);
        Basis car = BasisFromRotator(0, 0, 0);
        for (int i = 0; i < 120 * 120; ++i)
        {
            if (i % 240 == 0) car = BasisFromRotator(u(rng), u(rng), u(rng)); // a new random heading every 2 s
            LabStep(s, rng, p, car, LabHitbox{}, true, 0.f, t, 1.f / 120.f);
            maxR = std::max(maxR, Len(s.ballPos));
            t += 1.0 / 120.0;
        }
        CHECK(maxR <= p.bubble + 1.f, "ball reached %.0f uu (bubble %.0f)", maxR, p.bubble);
        CHECK(s.bounces > 20, "only %d bounces in 2 minutes", s.bounces);
        std::printf("constant, boosting at random for 2 min: %d bounces, %d touches, furthest %.0f uu\n", s.bounces, s.touches, maxR);
    }

    // A bot that chases the ball scores touches in both motions; idle, it touches nothing (pass distance).
    for (int motion = 0; motion < 2; ++motion)
    {
        LabParams p;
        p.motion = motion;
        for (int chase = 0; chase < 2; ++chase)
        {
            LabState s;
            LabStart(s, rng, p, 0.0);
            Bot bot;
            double t = 0.0;
            for (int i = 0; i < 120 * 60; ++i)
            {
                bool boost = chase && bot.Drive(s);
                LabStep(s, rng, p, bot.car, LabHitbox{}, boost && Len(s.ballPos) > 250.f, 0.f, t, 1.f / 120.f);
                t += 1.0 / 120.0;
            }
            std::printf("%s, %s: %d touches, %d misses, %d balls, %d bounces in 60 s\n", motion ? "constant" : "respawn",
                        chase ? "chasing bot" : "idle", s.touches, s.misses, s.balls, s.bounces);
            if (!chase) CHECK(s.touches == 0, "%d touches without moving", s.touches);
            else CHECK(s.touches >= 10, "chasing bot only touched %d times", s.touches);
        }
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
