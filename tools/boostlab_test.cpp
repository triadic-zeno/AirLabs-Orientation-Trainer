// Tests for AirLabs/BoostLab.h, run outside the game with a stand-in Vector type.
// Build: g++ -std=c++20 -O2 -IAirLabs tools/boostlab_test.cpp -o boostlab_test
#include <cstdio>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h"
#include "BoostLab.h"

using namespace airlabs;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const float kDt = 1.f / 120.f;

static Basis Facing(Vector aim)
{
    aim = Norm(aim);
    Vector up = std::fabs(aim.Z) < 0.95f ? V(0.f, 0.f, 1.f) : V(1.f, 0.f, 0.f);
    Vector right = Norm(Cross(up, aim));
    return Basis{ aim, right, Cross(aim, right) };
}

// A pilot that turns instantly (this tests the physics, not the steering): it points where
// the ball will be and boosts, and if it's closing too fast it turns round and brakes.
struct Pilot
{
    Basis car = Facing(V(1.f, 0.f, 0.f));
    bool Drive(const LabState& s)
    {
        Vector rel = LabRelVel(s);
        float dist = Len(s.ballPos);
        float t = std::clamp(dist / 1000.f, 0.05f, 1.0f);
        Vector aim = Add(s.ballPos, Scale(rel, t));
        // sideways drift relative to the ball: cancel it too
        Vector toBall = Norm(s.ballPos);
        Vector side = Sub(Scale(rel, -1.f), Scale(toBall, Dot(Scale(rel, -1.f), toBall)));
        aim = Sub(Norm(aim), Scale(side, 1.f / 800.f));
        car = Facing(aim);
        return true;
    }
};

int main()
{
    std::mt19937 rng(7);

    // ---- directions: even over the sphere ----
    {
        int top = 0, band = 0;
        const int n = 200000;
        for (int i = 0; i < n; ++i)
        {
            Vector d = LabRandomDir(rng);
            if (d.Z > 0.9f) ++top;
            if (std::fabs(d.Z) < 0.1f) ++band;
        }
        CHECK(std::abs(top - n / 20) < n / 200, "top cap %d, expected about %d", top, n / 20);
        CHECK(std::abs(band - n / 10) < n / 100, "equator band %d, expected about %d", band, n / 10);
    }

    // ---- launch directions keep the pass distance and cross the bubble ----
    {
        LabParams p;
        int bad = 0;
        for (int i = 0; i < 20000; ++i)
        {
            Vector pos = (i % 2) ? Scale(LabRandomDir(rng), p.bubble) : LabSpawnPoint(rng, p, V(1.f, 0.f, 0.f));
            Vector d = LabPickDirection(rng, pos, p, p.bubble);
            if (LabPassDistance(pos, d) < p.pass - 0.5f || LabExitDistance(pos, d, p.bubble) < p.bubble - 0.5f) ++bad;
        }
        CHECK(bad == 0, "%d directions broke the pass distance or crossed too little", bad);
    }

    // ---- spawns: inside the shell, in the cone and the height band ----
    {
        LabParams p;
        p.spawnCone = 60.f; p.heightMin = 0.f; p.heightMax = 45.f;
        Vector nose = V(0.f, 1.f, 0.f);
        int bad = 0;
        for (int i = 0; i < 2000; ++i)
        {
            Vector s = LabSpawnPoint(rng, p, nose);
            float r = Len(s), e = ElevationDeg(Norm(s));
            if (r < p.spawnMin - 0.1f || r > p.bubble - 59.f || AngleDeg(s, nose) > 60.1f || e < -0.1f || e > 45.1f) ++bad;
        }
        CHECK(bad == 0, "%d spawns outside the shell, cone or height band", bad);
    }

    // ---- boost: ~991 uu/s after 1 s, capped at 2300, and turning doesn't change your velocity ----
    {
        LabParams p;
        p.bubble = 50000.f; // nothing respawns during the test
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.ballPos = V(0.f, 30000.f, 0.f);
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0;
        for (int i = 0; i < 120; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, true, 0.f, t, kDt); t += kDt; }
        CHECK(std::fabs(Len(s.carVel) - 991.7f) < 2.f, "after 1 s of boost: %.1f uu/s", Len(s.carVel));
        for (int i = 0; i < 600; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, true, 0.f, t, kDt); t += kDt; }
        CHECK(std::fabs(Len(s.carVel) - 2300.f) < 0.5f, "capped speed %.1f", Len(s.carVel));
        Vector before = s.carVel;
        Basis turned = Facing(V(0.f, 1.f, 0.f));
        LabStep(s, rng, p, turned, turned, LabHitbox{}, false, 0.f, t, kDt);
        CHECK(Len(Sub(s.carVel, before)) < 0.01f, "turning without boost changed your velocity");
    }

    // ---- the hit: head-on into a still ball at 200 and 1500 uu/s ----
    for (float u : { 200.f, 1500.f })
    {
        Basis car = Facing(V(1.f, 0.f, 0.f));
        Vector ballVel = V(0.f, 0.f, 0.f), carVel = V(u, 0.f, 0.f);
        LabHitbox hb;
        hb.offset = V(13.88f, 0.f, 0.f); // box centred on the car's height, so the push is straight ahead
        Vector ballPos = V(13.88f + 59.f + 92.75f, 0.f, 0.f); // touching the middle of the nose
        Vector cp = LabClosestOnBox(ballPos, car, hb);
        float closing = LabHit(ballVel, carVel, ballPos, cp, V(0.f, 0.f, 0.f), car);
        // Inelastic part: ball 6/7 u, car loses 1/7 u. Psyonix: 0.65 u (u <= 500), 0.594 u at 1500.
        float wantBall = u * 6.f / 7.f + u * LabPushScale(u), wantCar = u * 6.f / 7.f;
        CHECK(std::fabs(closing - u) < 0.5f, "closing %.1f", closing);
        CHECK(std::fabs(ballVel.X - wantBall) < 1.f && std::fabs(ballVel.Y) < 0.5f, "ball after a %.0f hit: %.1f (want %.1f)", u, ballVel.X, wantBall);
        CHECK(std::fabs(carVel.X - wantCar) < 1.f, "car after a %.0f hit: %.1f (want %.1f)", u, carVel.X, wantCar);
        std::printf("head-on at %4.0f: ball %4.0f uu/s, car %4.0f, ball leaves you at %4.0f\n", u, ballVel.X, carVel.X, ballVel.X - carVel.X);
    }

    // ---- a corner hit sends it sideways; a spinning car hits a ball it's not moving into ----
    {
        Basis car = Facing(V(1.f, 0.f, 0.f));
        LabHitbox hb;
        Vector ballPos = V(13.88f + 59.f + 60.f, 42.1f + 60.f, 20.75f); // off the front right corner
        Vector ballVel = V(0.f, 0.f, 0.f), carVel = V(1000.f, 0.f, 0.f);
        LabHit(ballVel, carVel, ballPos, LabClosestOnBox(ballPos, car, hb), V(0.f, 0.f, 0.f), car);
        CHECK(ballVel.Y > 300.f, "corner hit: ball went %.0f sideways", ballVel.Y);
        // spin: car still, its nose sweeping up into a ball above the nose at 5.5 rad/s
        Basis now = Facing(V(1.f, 0.f, 0.f));
        Basis prev = Facing(Vector{ std::cos(5.5f * kDt), 0.f, -std::sin(5.5f * kDt) }); // a tick ago, the nose was lower
        LabParams p;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.phase = kLabFlying;
        s.ballPos = V(60.f, 0.f, 20.75f + 18.1f + 92.75f + 1.f); // resting on the roof near the front
        s.ballVel = V(0.f, 0.f, 0.f);
        LabStep(s, rng, p, now, prev, hb, false, 0.f, 0.1, kDt);
        CHECK(s.touches == 1 && s.ballVel.Z > 200.f, "flick: touches %d, ball up %.0f", s.touches, s.ballVel.Z);
    }

    // ---- through the whole step: the ball never ends up inside the car ----
    {
        LabParams p;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.phase = kLabFlying;
        s.ballPos = V(400.f, 0.f, 20.f);
        s.ballVel = V(-1200.f, 0.f, 0.f);
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0;
        float closest = 1e9f;
        for (int i = 0; i < 120 && s.touches == 0; ++i)
        {
            LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt);
            closest = std::min(closest, LabBoxDistance(s.ballPos, car, LabHitbox{}));
            t += kDt;
        }
        CHECK(s.touches == 1, "no touch");
        CHECK(closest >= kLabBallRadius - 0.01f, "the ball went into the car (%.1f from the box)", closest);
        CHECK(LabRelVel(s).X > 500.f, "after the touch it should leave forwards (%.0f)", LabRelVel(s).X);
    }

    // ---- the launch goes the way the arrow showed ----
    {
        LabParams p;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        Vector shown = s.launchDir;
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0;
        while (s.phase == kLabHang) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; }
        CHECK(AngleDeg(s.ballVel, shown) < 0.05f && std::fabs(Len(s.ballVel) - s.launchSpeed) < 0.5f && s.launchSpeed <= p.speed + 0.5f, "launch didn't follow its arrow (%.3f deg, %.1f uu/s, phase %d)", AngleDeg(s.ballVel, shown), Len(s.ballVel), s.phase);
    }

    // ---- the bounce fix: drifting flat out, then boosting at the ball, reaches it ----
    {
        int reached = 0, tries = 40;
        float worldSpeedErr = 0.f;
        for (int k = 0; k < tries; ++k)
        {
            LabParams p;
            p.motion = kLabConstant;
            LabState s;
            LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
            double t = 0.0;
            Basis side = Facing(LabRandomDir(rng));
            for (int i = 0; i < 120 * 4; ++i) { LabStep(s, rng, p, side, side, LabHitbox{}, true, 0.f, t, kDt); t += kDt; } // 4 s flat out one way
            int b0 = s.bounces;
            Pilot pilot;
            Basis prev = pilot.car;
            for (int i = 0; i < 120 * 6 && s.touches == 0; ++i)
            {
                pilot.Drive(s);
                LabStep(s, rng, p, pilot.car, prev, LabHitbox{}, true, 0.f, t, kDt);
                prev = pilot.car;
                t += kDt;
                if (s.bounces > b0) { b0 = s.bounces; worldSpeedErr = std::max(worldSpeedErr, std::fabs(Len(s.ballVel) - p.speed)); }
            }
            if (s.touches > 0) ++reached;
        }
        std::printf("after 4 s drifting flat out, a pilot boosting at the ball reached it %d/%d times within 6 s\n", reached, tries);
        CHECK(reached >= tries * 3 / 4, "only reached the ball %d/%d times", reached, tries);
    }
    // ---- the three Constant bugs: super-speed bounces, boost doing nothing, the ball going under you ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.floor = 300.f;
        p.steep = 45.f;
        float worstRel = 0.f, deepest = 0.f, steepest = 0.f;
        int stuck = 0;
        const int runs = 30;
        for (int run = 0; run < runs; ++run)
        {
            LabState s;
            LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
            double t = 0.0;
            Basis prev = Facing(V(1.f, 0.f, 0.f));
            int b0 = 0;
            // Stuck: for 3 s the ball keeps getting away (closing < 0), with no touch or bounce,
            // and boosting at it hasn't improved the closing speed by even 50 uu/s.
            float windowStart = 0.f;
            double windowT = 0.0;
            bool inWindow = false;
            for (int i = 0; i < 120 * 60; ++i)
            {
                Basis car = Facing(s.ballPos); // always points at the ball and boosts
                LabStep(s, rng, p, car, prev, LabHitbox{}, true, 0.f, t, kDt);
                prev = car;
                t += kDt;
                float closing = -Dot(LabRelVel(s), Norm(s.ballPos));
                if (s.touchedNow || s.bounces != b0 || closing >= 0.f) inWindow = false;
                else if (!inWindow) { inWindow = true; windowStart = closing; windowT = t; }
                else if (t - windowT >= 3.0)
                {
                    if (closing - windowStart < 50.f) { ++stuck; break; }
                    windowStart = closing; windowT = t;
                }
                if (s.bounces > b0)
                {
                    b0 = s.bounces;
                    worstRel = std::max(worstRel, Len(LabRelVel(s)));
                    steepest = std::max(steepest, std::fabs(ElevationDeg(Norm(s.ballVel))));
                    CHECK(Len(s.carVel) < 0.01f, "your speed wasn't reset by the bounce");
                }
                deepest = std::min(deepest, s.ballPos.Z);
            }
        }
        std::printf("constant with a floor at 300 and paths up to 45 deg: bounces came at you at up to %.0f uu/s, the ball went %.0f uu below you, steepest bounce %.1f deg, stuck boosting for 3 s in %d/%d runs\n",
                    worstRel, -deepest, steepest, stuck, runs);
        CHECK(worstRel <= p.speed + 0.5f, "a bounce came at you at %.0f (set speed %.0f)", worstRel, p.speed);
        CHECK(stuck == 0, "boosting at the ball gained nothing for 3 s in %d runs", stuck);
        CHECK(deepest >= -p.floor - 25.f, "the ball went %.0f below you (floor %.0f)", -deepest, p.floor);
        CHECK(steepest <= 45.1f, "a bounce went %.1f deg steep", steepest);
    }
    // Every launch is reachable; a fast ball is only slowed when it has to be.
    {
        int unreachable = 0, slowed = 0, n = 2000;
        float slowest = 1e9f;
        LabParams p;
        p.speed = 2000.f;
        for (int i = 0; i < n; ++i)
        {
            LabState s;
            LabRespawn(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
            if (!LabReachable(s.ballPos, Scale(s.launchDir, s.launchSpeed), p, kLabReactSecWait)) ++unreachable;
            if (s.launchSpeed < p.speed - 0.5f) ++slowed;
            slowest = std::min(slowest, s.launchSpeed);
        }
        std::printf("set speed 2000: %d/%d launches slowed to be reachable (slowest %.0f), %d unreachable\n", slowed, n, slowest, unreachable);
        CHECK(unreachable == 0, "%d launches you couldn't reach", unreachable);
        p.speed = 400.f;
        int slowedSlow = 0;
        for (int i = 0; i < 500; ++i) { LabState s; LabRespawn(s, rng, p, V(1.f, 0.f, 0.f), 0.0); if (s.launchSpeed < 399.5f) ++slowedSlow; }
        CHECK(slowedSlow < 25, "%d/500 slow launches were slowed further", slowedSlow);
    }

    // Respawn launches keep to the floor and the steepness too.
    {
        LabParams p;
        p.floor = 200.f;
        p.steep = 30.f;
        int bad = 0;
        for (int i = 0; i < 3000; ++i)
        {
            LabState s;
            LabRespawn(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
            if (s.ballPos.Z < -p.floor + kLabBallRadius - 0.5f || std::fabs(ElevationDeg(s.launchDir)) > 30.1f ||
                LabRunDistance(s.ballPos, s.launchDir, p.bubble, p.floor) < p.bubble * 0.5f) ++bad;
        }
        CHECK(bad < 30, "%d respawns broke the floor or the steepness, or had nowhere to go", bad);
    }

    // ---- Constant: the ball never leaves the bubble, even boosting at random flat out ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.curve = 60.f;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        double t = 0.0;
        float maxR = 0.f;
        Basis car = Facing(V(1.f, 0.f, 0.f));
        for (int i = 0; i < 120 * 120; ++i)
        {
            if (i % 240 == 0) car = Facing(LabRandomDir(rng));
            LabStep(s, rng, p, car, car, LabHitbox{}, true, 0.f, t, kDt);
            maxR = std::max(maxR, Len(s.ballPos));
            t += kDt;
        }
        CHECK(maxR <= p.bubble + 1.f, "ball reached %.0f uu (bubble %.0f)", maxR, p.bubble);
        CHECK(s.bounces > 20, "only %d bounces in 2 minutes", s.bounces);
    }

    // ---- curving keeps the speed and turns at the set rate ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.curve = 90.f;
        p.bubble = 50000.f;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0;
        while (s.phase == kLabHang) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; }
        s.ballPos = V(0.f, 0.f, 20000.f); // far away: no touches
        Vector v0 = s.ballVel;
        for (int i = 0; i < 60; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; } // 0.5 s
        float turned = AngleDeg(v0, s.ballVel);
        CHECK(std::fabs(Len(s.ballVel) - Len(v0) * (1.f - 0.5f * kLabBallDrag)) < 2.f, "curving changed the speed (beyond air drag)");
        CHECK(turned > 40.f && turned <= 45.5f, "turned %.1f deg in 0.5 s at 90 deg/s", turned);
    }

    // ---- dynamic speeds stay in range; run length ends the run ----
    {
        LabParams p;
        p.dynamic = true; p.speedMin = 500.f; p.speedMax = 900.f;
        float lo = 1e9f, hi = 0.f;
        for (int i = 0; i < 1000; ++i) { float v = LabNextSpeed(rng, p); lo = std::min(lo, v); hi = std::max(hi, v); }
        CHECK(lo >= 500.f && hi <= 900.f && hi - lo > 350.f, "dynamic speeds %.0f-%.0f", lo, hi);
        p.runSec = 5.f;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0;
        for (int i = 0; i < 120 * 8; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; }
        CHECK(s.phase == kLabDone, "run didn't end");
    }

    // ---- flips: Rocket League's pushes (RocketSim) ----
    {
        Basis car = Facing(V(1.f, 0.f, 0.f));
        Vector v = V(0.f, 0.f, 0.f);
        Vector d = LabFlipImpulse(v, car, -1.f, 0.f, 0.f, true); // stick forward, standing still: front flip
        CHECK(d.X > 0.99f && std::fabs(v.X - 500.f) < 0.5f && std::fabs(v.Y) < 0.01f, "front flip from rest: %.1f %.1f", v.X, v.Y);
        v = V(0.f, 0.f, 0.f);
        d = LabFlipImpulse(v, car, 0.1f, 0.f, 0.1f, true);       // barely any stick: a double jump
        CHECK(std::fabs(v.Z - kLabJumpImpulse) < 0.5f && std::fabs(d.X) + std::fabs(d.Y) < 0.01f, "double jump: %.1f up", v.Z);
        v = V(2300.f, 0.f, 0.f);
        LabFlipImpulse(v, car, 0.f, 1.f, 0.f, false);            // side flip at top speed: 1.9x sideways
        CHECK(std::fabs(v.Y - 950.f) < 0.5f && std::fabs(v.X - 2300.f) < 0.5f, "side flip at speed: %.1f %.1f", v.X, v.Y);
        v = V(2300.f, 0.f, 0.f);
        LabFlipImpulse(v, car, 1.f, 0.f, 0.f, false);            // backflip at top speed: 2.5 x 16/15
        CHECK(std::fabs(v.X - (2300.f - 500.f * 2.5f * 16.f / 15.f)) < 0.5f, "backflip at speed: %.1f", v.X);
        // a nose-down car still flips level, like the game
        Basis down = Facing(Norm(V(1.f, 0.f, -1.f)));
        v = V(0.f, 0.f, 0.f);
        LabFlipImpulse(v, down, -1.f, 0.f, 0.f, true);
        CHECK(std::fabs(v.Z) < 0.01f && std::fabs(v.X - 500.f) < 0.5f, "flips push level: %.1f %.1f %.1f", v.X, v.Y, v.Z);
    }
    // ---- the car's rotation round-trips ----
    {
        int bad = 0;
        std::uniform_int_distribution<int> u(-16000, 16000), uy(-32767, 32767);
        for (int i = 0; i < 2000; ++i)
        {
            int p0 = u(rng), y0 = uy(rng), r0 = uy(rng);
            Basis b = BasisFromRotator(p0, y0, r0);
            int p1, y1, r1;
            RotatorFromBasis(b, p1, y1, r1);
            Basis c = BasisFromRotator(p1, y1, r1);
            if (AngleDeg(b.forward, c.forward) > 0.05f || AngleDeg(b.up, c.up) > 0.05f) ++bad;
        }
        CHECK(bad == 0, "%d rotations didn't round-trip", bad);
    }
    // ---- flip reset: the ball meeting your wheels gives you a flip back ----
    {
        LabParams p;
        p.motion = kLabConstant;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.phase = kLabFlying;
        s.flipReady = false;
        Basis car = Facing(V(1.f, 0.f, 0.f));
        LabHitbox hb;
        s.ballPos = V(hb.offset.X, 0.f, hb.offset.Z - hb.half.Z - kLabBallRadius - 6.f); // under the car
        s.ballVel = V(0.f, 0.f, 300.f);                                                     // rising into the wheels
        double t = 0.0;
        for (int i = 0; i < 10 && s.touches == 0; ++i) { LabStep(s, rng, p, car, car, hb, false, 0.f, t, kDt); t += kDt; }
        CHECK(s.touches == 1 && s.resets == 1 && s.flipReady, "flip reset: touches %d resets %d ready %d", s.touches, s.resets, (int)s.flipReady);
        // the nose doesn't count
        LabState n;
        LabStart(n, rng, p, V(1.f, 0.f, 0.f), 0.0);
        n.phase = kLabFlying;
        n.flipReady = false;
        n.ballPos = V(hb.offset.X + hb.half.X + kLabBallRadius + 6.f, 0.f, hb.offset.Z);
        n.ballVel = V(-300.f, 0.f, 0.f);
        for (int i = 0; i < 10 && n.touches == 0; ++i) { LabStep(n, rng, p, car, car, hb, false, 0.f, t, kDt); t += kDt; }
        CHECK(n.touches == 1 && n.resets == 0 && !n.flipReady, "a nose touch gave a flip reset");
    }
    // ---- the speed limit: you at 2300 behind a ball doing 2350 the same way, boosting at it ----
    for (int limit = 0; limit < 2; ++limit)
    {
        LabParams p;
        p.motion = kLabConstant;
        p.bubble = 2000.f;
        p.speedLimit = limit == 1;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.phase = kLabFlying;
        s.free = false;
        s.ballPos = V(500.f, 0.f, 20.f);
        s.carVel = V(2300.f, 0.f, 0.f);
        s.ballVel = V(2350.f, 0.f, 0.f);
        Basis car = Facing(V(1.f, 0.f, 0.f));
        double t = 0.0, reached = -1.0;
        for (int i = 0; i < 120 * 3 && reached < 0.0; ++i)
        {
            LabStep(s, rng, p, car, car, LabHitbox{}, true, 0.f, t, kDt);
            t += kDt;
            if (s.touches > 0) reached = t;
        }
        std::printf("you at 2300 chasing a ball at 2350, speed limit %s: %s\n", limit ? "on " : "off",
                    reached > 0.0 ? ("caught it in " + std::to_string(reached).substr(0, 4) + " s").c_str() : "couldn't catch it in 3 s");
        if (!limit) CHECK(reached > 0.0 && reached < 1.5, "with no speed limit you should catch it quickly");
        else CHECK(reached < 0.0 || reached > 1.5, "with the limit you shouldn't catch it quickly");
    }

    // ---- Respawn: a ball you keep touching stays; a new one comes 1.5 s after the last touch ----
    {
        LabParams p;
        LabState s;
        LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
        s.phase = kLabTouched;
        s.lastTouch = 0.0;
        s.phaseEnd = kLabTouchedSec;
        s.ballPos = V(400.f, 0.f, 0.f);
        s.ballVel = V(0.f, 0.f, 0.f);
        int balls = s.balls;
        Basis car = Facing(V(0.f, 1.f, 0.f));
        double t = 0.0;
        for (int i = 0; i < 170; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; }
        CHECK(s.balls == balls, "replaced before 1.5 s");
        for (int i = 0; i < 20; ++i) { LabStep(s, rng, p, car, car, LabHitbox{}, false, 0.f, t, kDt); t += kDt; }
        CHECK(s.balls == balls + 1, "not replaced after 1.5 s");
    }

    // ---- a pilot that chases scores touches in both motions; idle, it touches nothing ----
    for (int motion = 0; motion < 2; ++motion)
    {
        LabParams p;
        p.motion = motion;
        for (int chase = 0; chase < 2; ++chase)
        {
            LabState s;
            LabStart(s, rng, p, V(1.f, 0.f, 0.f), 0.0);
            Pilot pilot;
            Basis prev = pilot.car;
            double t = 0.0;
            for (int i = 0; i < 120 * 60; ++i)
            {
                if (chase) pilot.Drive(s);
                LabStep(s, rng, p, pilot.car, prev, LabHitbox{}, chase && Len(s.ballPos) > 220.f, 0.f, t, kDt);
                prev = pilot.car;
                t += kDt;
            }
            std::printf("%s, %s: %d touches, %d got away, %d balls, %d bounces in 60 s\n", motion ? "constant" : "respawn",
                        chase ? "chasing pilot" : "idle", s.touches, s.misses, s.balls, s.bounces);
            if (!chase) CHECK(s.touches == 0, "%d touches without moving", s.touches);
            else CHECK(s.touches >= 10, "chasing pilot only touched %d times", s.touches);
        }
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
