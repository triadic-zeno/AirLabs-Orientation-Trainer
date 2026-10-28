// Tests for AirLabs/LabWorld.h: the Boost Lab run by RocketSim. Off-game, with a stand-in Vector.
// Build (compiles RocketSim too, about a minute):
//   g++ -std=c++20 -O2 -w -IAirLabs tools/labworld_test.cpp $(find thirdparty/RocketSim -name "*.cpp") -o labworld_test
#include <cstdio>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h"
#include "BoostLab.h"
#include "LabWorld.h"

using namespace airlabs;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double kTick = 1.0 / 120.0;
static const Vector kOctane = { 118.f, 84.2f, 36.16f };

static Basis Facing(Vector aim)
{
    aim = Norm(aim);
    Vector up = std::fabs(aim.Z) < 0.95f ? V(0.f, 0.f, 1.f) : V(1.f, 0.f, 0.f);
    Vector right = Norm(Cross(up, aim));
    return Basis{ aim, right, Cross(aim, right) };
}

// The car flies at `carSpeed` into a still ball placed at `rel`; returns after the touch.
struct HitResult { int touches; float ballSpeed, carSpeed, carSpin; Vector ballVel; };
static HitResult Hit(std::mt19937& rng, Vector rel, float carSpeed)
{
    LabParams p;
    p.motion = kLabConstant;
    p.speedLimit = false;
    p.bubble = 3000.f;
    LabWorld w;
    w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
    w.Place(rel, V(0.f, 0.f, 0.f), V(carSpeed, 0.f, 0.f));
    double t = 0.0;
    for (int i = 0; i < 60; ++i) { w.Step(p, LabControls{}, rng, t); t += kTick; if (w.s.touches) break; }
    for (int i = 0; i < 6; ++i) { w.Step(p, LabControls{}, rng, t); t += kTick; } // let the contact finish
    return { w.s.touches, Len(w.s.ballVel), Len(w.s.carVel), w.GetCarState().angVel.Length(), w.s.ballVel };
}

int main()
{
    std::mt19937 rng(11);

    // ---- normal gravity reaches both bodies, including the launch countdown ----
    for (int waiting = 0; waiting < 2; ++waiting)
    {
        LabParams p;
        CHECK(p.speedLimit, "normal speed limits should be on by default");
        p.speedLimit = false; // isolate gravity from ball drag and speed caps
        p.hangSec = 2.f;
        p.bubble = 50000.f;
        p.floor = 50000.f;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        if (!waiting) w.Place(V(1000.f, 0.f, 300.f), V(0.f, 0.f, 0.f), V(0.f, 0.f, 0.f));
        const Vector initialRel = w.s.ballPos;
        for (int i = 0; i < 120; ++i) w.Step(p, LabControls{}, rng, i * kTick);
        std::printf("1 s gravity, %s: car Z %.1f, ball Z %.1f, relative displacement %.3f\n",
                    waiting ? "waiting" : "flying", w.s.carVel.Z, w.s.ballVel.Z, Len(Sub(w.s.ballPos, initialRel)));
        CHECK(std::fabs(w.s.carVel.Z + 650.f) < 1.f, "gravity on car: %.1f", w.s.carVel.Z);
        CHECK(std::fabs(w.s.ballVel.Z + 650.f) < 1.f, "gravity on ball: %.1f", w.s.ballVel.Z);
        CHECK(Len(Sub(w.s.ballPos, initialRel)) < 0.1f, "equal free fall should preserve separation");
        CHECK(w.s.touches == 0 && w.s.bounces == 0 && w.s.balls == 1, "free fall changed the drill");
        CHECK(w.s.phase == (waiting ? kLabHang : kLabFlying), "unexpected phase during free fall");
    }

    // ---- the default caps constrain BOTH bodies, including vertical speed ----
    {
        LabParams p;
        p.bubble = p.floor = 50000.f;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        w.Place(V(10000.f, 0.f, 10000.f), V(0.f, 0.f, 7000.f), V(0.f, 0.f, 6000.f));
        for (int i = 0; i < 120; ++i)
        {
            w.Step(p, LabControls{}, rng, i * kTick);
            CHECK(Len(w.s.carVel) <= 2300.1f, "car exceeded its cap: %.1f", Len(w.s.carVel));
            CHECK(Len(w.s.ballVel) <= 6000.1f, "ball exceeded its cap: %.1f", Len(w.s.ballVel));
        }
        CHECK(w.s.touches == 0 && w.s.bounces == 0, "speed cap test had contact or a bounce");
    }

    // ---- a backflip can cut car vertical speed without imparting any impulse to the ball ----
    {
        LabParams p;
        p.bubble = p.floor = 50000.f;
        LabWorld flip, coast;
        flip.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        coast.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        for (LabWorld* w : { &flip, &coast })
            w->Place(V(0.f, 1000.f, 300.f), V(0.f, 0.f, 1800.f), V(0.f, 0.f, 1800.f));
        LabControls c;
        c.jump = true;
        c.pitch = 1.f;
        for (int i = 0; i < 84; ++i)
        {
            flip.Step(p, c, rng, i * kTick);
            coast.Step(p, LabControls{}, rng, i * kTick);
            c = LabControls{};
            CHECK(Len(Sub(flip.s.ballVel, coast.s.ballVel)) < 0.1f, "no-contact flip changed ball velocity");
            CHECK(Len(flip.s.carVel) <= 2300.1f, "backflip exceeded the car speed cap");
        }
        std::printf("no-contact backflip: ball Z %.0f (coasting %.0f), car Z %.0f (coasting %.0f), relative up %.0f\n",
                    flip.s.ballVel.Z, coast.s.ballVel.Z, flip.s.carVel.Z, coast.s.carVel.Z, LabRelVel(flip.s).Z);
        CHECK(coast.s.carVel.Z - flip.s.carVel.Z > 1000.f, "flip should reduce the car's upward velocity");
        CHECK(flip.s.touches == 0 && flip.s.bounces == 0 && flip.s.balls == 1, "no-contact flip changed the drill");
    }

    // ---- Volley: shared vertical drift gives the same flip response as a slow encounter ----
    // Isolate flips: absolute caps/ball drag deliberately remain field-relative options.
    for (float drift : { -6000.f, 6000.f })
    for (float pitch : { -1.f, 1.f })
    for (Vector aim : { V(1.f, 0.f, 0.f), V(0.5f, 0.f, 0.8660254f) })
    {
        LabParams p;
        p.motion = kLabConstant;
        p.keepFlipDrift = true;
        p.speedLimit = false;
        p.bubble = p.floor = 50000.f;
        LabWorld slow, fast;
        for (LabWorld* w : { &slow, &fast }) w->Start(p, Facing(aim), kOctane, rng, 0.0);
        slow.Place(V(0.f, 1000.f, 300.f), V(400.f, 0.f, -100.f), V(400.f, 0.f, 0.f));
        fast.Place(V(0.f, 1000.f, 300.f), V(400.f, 0.f, drift - 100.f), V(400.f, 0.f, drift));
        float worstRel = 0.f, worstPos = 0.f;
        for (int i = 0; i < 84; ++i)
        {
            LabControls c;
            c.jump = i == 0;
            c.pitch = i < 20 ? pitch : -pitch; // include flip cancelling
            slow.Step(p, c, rng, i * kTick);
            fast.Step(p, c, rng, i * kTick);
            worstRel = std::max(worstRel, Len(Sub(LabRelVel(fast.s), LabRelVel(slow.s))));
            worstPos = std::max(worstPos, Len(Sub(fast.s.ballPos, slow.s.ballPos)));
            CHECK(std::fabs((fast.s.ballVel.Z - slow.s.ballVel.Z) - drift) < 0.2f, "flip changed untouched ball drift");
            CHECK(Len(Sub(FromRs(fast.GetCarState().angVel), FromRs(slow.GetCarState().angVel))) < 0.01f, "shared drift changed flip rotation");
        }
        std::printf("Volley flip: drift %+.0f pitch %+.0f nose up %.2f, relative velocity error %.3f, position error %.3f\n",
                    drift, pitch, aim.Z, worstRel, worstPos);
        CHECK(worstRel < 0.3f && worstPos < 0.3f, "shared drift changed the flip response");
        CHECK(fast.s.touches == 0 && fast.s.bounces == 0, "flip equivalence test had a touch or bounce");
        CHECK(std::fabs(fast.FlipDriftZ() - drift) < 0.1f, "flip reference should stay fixed until a reset");
        fast.GiveFlip();
        CHECK(fast.FlipDriftZ() == 0.f, "giving a fresh flip should clear its reference");
    }

    // ---- disabled Volley experiment must match Respawn's normal flip physics ----
    {
        LabParams normal;
        normal.bubble = normal.floor = 50000.f;
        normal.speedLimit = false;
        LabParams volley = normal;
        volley.motion = kLabConstant;
        volley.keepFlipDrift = false;
        LabWorld a, b;
        a.Start(normal, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        b.Start(volley, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        for (LabWorld* w : { &a, &b }) w->Place(V(0.f, 1000.f, 300.f), V(0.f, 0.f, 1800.f), V(0.f, 0.f, 1800.f));
        for (int i = 0; i < 84; ++i)
        {
            LabControls c;
            c.jump = i == 0;
            c.pitch = i == 0 ? 1.f : 0.f;
            a.Step(normal, c, rng, i * kTick);
            b.Step(volley, c, rng, i * kTick);
            CHECK(Len(Sub(a.s.carVel, b.s.carVel)) < 0.01f && Len(Sub(a.s.ballPos, b.s.ballPos)) < 0.01f,
                  "disabled Volley drift experiment changed normal physics");
        }
        CHECK(a.FlipDriftZ() == 0.f && b.FlipDriftZ() == 0.f, "normal flips captured a drift reference");
    }

    // ---- an actual roof flick still hits through RocketSim's contact solver ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.speedLimit = false;
        p.bubble = p.floor = 50000.f;
        LabWorld slow, fast;
        for (LabWorld* w : { &slow, &fast }) w->Start(p, Facing(V(0.8660254f, 0.f, -0.5f)), kOctane, rng, 0.0);
        slow.Place(V(-50.f, 0.f, 140.f), V(0.f, 0.f, -150.f), V(0.f, 0.f, 0.f));
        fast.Place(V(-50.f, 0.f, 140.f), V(0.f, 0.f, 5850.f), V(0.f, 0.f, 6000.f));
        float worstRel = 0.f;
        for (int i = 0; i < 84; ++i)
        {
            LabControls c;
            c.jump = i == 0;
            c.pitch = i == 0 ? 1.f : 0.f;
            slow.Step(p, c, rng, i * kTick);
            fast.Step(p, c, rng, i * kTick);
            worstRel = std::max(worstRel, Len(Sub(LabRelVel(fast.s), LabRelVel(slow.s))));
        }
        std::printf("roof flick: slow touches %d, fast touches %d, relative velocity error %.3f\n", slow.s.touches, fast.s.touches, worstRel);
        CHECK(slow.s.touches > 0 && fast.s.touches == slow.s.touches, "roof flick should contact the ball in both frames");
        CHECK(worstRel < 5.f, "shared drift changed the roof collision response (%.3f)", worstRel);
    }

    // ---- default speed caps still apply during the experimental Volley flip ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.bubble = p.floor = 50000.f;
        LabWorld w;
        w.Start(p, Facing(V(0.5f, 0.f, 0.8660254f)), kOctane, rng, 0.0);
        w.Place(V(0.f, 1000.f, 300.f), V(0.f, 0.f, 2200.f), V(0.f, 0.f, 2200.f));
        for (int i = 0; i < 84; ++i)
        {
            LabControls c;
            c.jump = i == 0;
            c.pitch = i == 0 ? 1.f : 0.f;
            w.Step(p, c, rng, i * kTick);
            CHECK(Len(w.s.carVel) <= 2300.1f && Len(w.s.ballVel) <= 6000.1f, "Volley flip bypassed a speed cap");
        }
    }

    // ---- a bounce during an Infinite flip must discard the old drift reference ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.flips = kLabFlipsInfinite;
        p.speedLimit = false;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        w.Place(V(p.bubble - 1.f, 0.f, 0.f), V(1000.f, 0.f, 1000.f), V(0.f, 0.f, 1000.f));
        LabControls c;
        c.jump = true;
        c.pitch = 1.f;
        w.Step(p, c, rng, 0.0);
        CHECK(w.s.bounces == 1 && w.FlipDriftZ() == 0.f, "bounce retained a flip's old drift reference");
        for (int i = 1; i < 40; ++i)
        {
            w.Step(p, LabControls{}, rng, i * kTick);
            CHECK(w.s.carVel.Z < 1.f, "old flip reference reintroduced upward speed after a bounce");
        }
    }

    // ---- hits: the car slows, spins, and where it lands changes how the ball leaves ----
    {
        HitResult nose = Hit(rng, V(13.88f + 59.f + 91.25f + 15.f, 0.f, 20.75f), 1500.f);
        HitResult corner = Hit(rng, V(13.88f + 59.f + 55.f, -(42.1f + 50.f), 20.75f), 1500.f);
        HitResult roof = Hit(rng, V(13.88f + 59.f + 70.f, 0.f, 20.75f + 18.1f + 60.f), 1500.f);
        std::printf("1500 into a still ball: nose -> ball %.0f, car %.0f, car spin %.2f | corner -> ball %.0f (sideways %.0f), car %.0f, spin %.2f | top edge -> ball %.0f (up %.0f), spin %.2f\n",
                    nose.ballSpeed, nose.carSpeed, nose.carSpin, corner.ballSpeed, corner.ballVel.Y, corner.carSpeed, corner.carSpin,
                    roof.ballSpeed, roof.ballVel.Z, roof.carSpin);
        CHECK(nose.touches == 1 && nose.ballSpeed > 1800.f, "nose hit: ball %.0f", nose.ballSpeed);
        CHECK(nose.carSpeed < 1450.f, "the car should lose speed on a hit (%.0f)", nose.carSpeed);
        CHECK(corner.carSpin > 0.5f, "an off-centre hit should spin the car (%.2f rad/s)", corner.carSpin);
        CHECK(corner.ballVel.Y < -300.f, "a corner hit should send it sideways (%.0f)", corner.ballVel.Y);
        CHECK(std::fabs(nose.ballSpeed - corner.ballSpeed) > 100.f, "different parts of the car should send the ball differently");
        CHECK(roof.ballVel.Z > 100.f, "a hit on the top edge should send it up (%.0f)", roof.ballVel.Z);
    }

    // ---- boosting into the ball touches it ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.speedLimit = false;
        p.bubble = 3000.f;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        w.Place(V(400.f, 0.f, 20.f), V(0.f, 0.f, 0.f), V(0.f, 0.f, 0.f));
        double t = 0.0;
        LabControls c;
        c.boost = true;
        for (int i = 0; i < 240 && !w.s.touches; ++i) { w.Step(p, c, rng, t); t += kTick; }
        CHECK(w.s.touches == 1 && t < 1.0, "boosting at a ball 400 uu ahead: %d touches after %.2f s", w.s.touches, t);
    }

    // ---- flips and flip resets ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.speedLimit = false;
        p.bubble = 3000.f;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        CHECK(w.carHasFlip, "a new ball should come with a flip");
        double t = 0.0;
        LabControls c;
        c.jump = true;
        c.pitch = -1.f;
        w.Step(p, c, rng, t); t += kTick;
        const float flipPush = Len(w.s.carVel);
        c = LabControls{};
        for (int i = 0; i < 150; ++i) { w.Step(p, c, rng, t); t += kTick; }
        CHECK(flipPush > 400.f, "a front flip should push about 500 (%.0f)", flipPush);
        CHECK(!w.carHasFlip, "after a flip you shouldn't have another");
        w.Place(V(9.f, 0.f, -125.f), V(0.f, 0.f, 150.f), V(0.f, 0.f, 0.f)); // the ball rising into the wheels
        int most = 0;
        for (int i = 0; i < 120; ++i) { w.Step(p, c, rng, t); t += kTick; most = std::max(most, w.wheelsOnBall); }
        std::printf("ball into the wheels: %d wheels, %d flip resets, flip back: %s\n", most, w.flipResets, w.carHasFlip ? "yes" : "no");
        CHECK(most >= 3 && w.flipResets == 1 && w.carHasFlip, "flip reset: wheels %d resets %d has flip %d", most, w.flipResets, (int)w.carHasFlip);
        // the nose doesn't give a reset
        LabWorld n;
        n.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        n.Place(V(13.88f + 59.f + 91.25f + 10.f, 0.f, 20.75f), V(-300.f, 0.f, 0.f), V(0.f, 0.f, 0.f));
        for (int i = 0; i < 60; ++i) { n.Step(p, c, rng, t); t += kTick; }
        CHECK(n.flipResets == 0, "a nose touch gave a flip reset");
    }

    // ---- the speed limit ----
    for (int limit = 0; limit < 2; ++limit)
    {
        LabParams p;
        p.motion = kLabConstant;
        p.speedLimit = limit == 1;
        p.bubble = 50000.f;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        w.Place(V(0.f, 40000.f, 0.f), V(0.f, 0.f, 0.f), V(0.f, 0.f, 0.f));
        LabControls c;
        c.boost = true;
        double t = 0.0;
        for (int i = 0; i < 120 * 4; ++i) { w.Step(p, c, rng, t); t += kTick; }
        const float sp = Len(w.s.carVel);
        std::printf("4 s of boost, speed limit %s: %.0f uu/s\n", limit ? "on " : "off", sp);
        if (limit) CHECK(std::fabs(sp - 2300.f) < 5.f, "with the limit the car should stop at 2300 (%.0f)", sp);
        else CHECK(sp > 3000.f, "with no limit the car should keep speeding up (%.0f)", sp);
    }

    // ---- Constant: bounces come back at the set speed, reset your speed, and give a flip ----
    {
        LabParams p;
        p.motion = kLabConstant;
        p.speedLimit = false;
        LabWorld w;
        w.Start(p, Facing(V(1.f, 0.f, 0.f)), kOctane, rng, 0.0);
        double t = 0.0;
        float worst = 0.f, maxR = 0.f;
        int b0 = 0;
        LabControls c;
        for (int i = 0; i < 120 * 60; ++i)
        {
            c.boost = (i / 240) % 2 == 0;
            c.yaw = (i / 300) % 2 ? 0.4f : -0.3f;
            w.Step(p, c, rng, t);
            t += kTick;
            maxR = std::max(maxR, Len(w.s.ballPos));
            if (w.s.bounces > b0)
            {
                b0 = w.s.bounces;
                worst = std::max(worst, Len(LabRelVel(w.s)));
                CHECK(Len(w.s.carVel) < 0.5f && w.carHasFlip, "a bounce should reset your speed and give a flip");
            }
        }
        std::printf("constant, 60 s: %d bounces, fastest bounce %.0f uu/s (set %.0f), furthest %.0f uu\n", w.s.bounces, worst, p.speed, maxR);
        CHECK(w.s.bounces > 10 && worst <= p.speed + 1.f, "bounces %d, fastest %.0f", w.s.bounces, worst);
        CHECK(maxR <= p.bubble + 40.f, "the ball got %.0f away (bubble %.0f)", maxR, p.bubble);
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
