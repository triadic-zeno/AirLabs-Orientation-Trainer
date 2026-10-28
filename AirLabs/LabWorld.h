#pragma once
// The Boost Lab's hidden world, run by RocketSim (thirdparty/RocketSim: an MIT-licensed
// reimplementation of Rocket League's physics; see thirdparty/RocketSim/AIRLABS.md).
//
// RocketSim simulates your car and the ball in an empty arena (THE_VOID) with normal gravity: boost,
// air control, flips, jumps, the ball-car hit (where it lands on the car decides how the ball
// leaves and how your car spins and slows), the wheels' suspension against the ball, and flip
// resets (three or more wheels on the ball). The game car becomes a puppet: it shows RocketSim's
// car's rotation, held in place, and the real ball is drawn where RocketSim's ball is relative to
// the car.
//
// The lab's rules sit on top (BoostLab.h): the bubble around the car, new balls and launches,
// bounces off the edge and the floor, reachable speeds, curving. They act through RocketSim's state
// setters, and LabState mirrors RocketSim each tick so the HUD needn't know about it.
//
// Needs TrainerCore.h and BoostLab.h first. Nothing here talks to the game.

#include "../thirdparty/RocketSim/src/RocketSim.h"

namespace airlabs
{
    struct LabControls
    {
        float throttle = 0.f, steer = 0.f, pitch = 0.f, yaw = 0.f, roll = 0.f;
        bool boost = false, jump = false, handbrake = false;
    };

    inline RocketSim::Vec ToRs(const Vector& v) { return RocketSim::Vec(v.X, v.Y, v.Z); }
    inline Vector FromRs(const RocketSim::Vec& v) { return V(v.x, v.y, v.z); }

    class LabWorld
    {
    public:
        LabState s;                  // mirrors RocketSim each tick (positions relative to the car)
        Vector ballSpin = V(0.f, 0.f, 0.f); // rad/s, RocketSim's
        int wheelsOnBall = 0;
        bool carHasFlip = true;      // you can flip (or double jump) right now
        int flipResets = 0;
        bool resetNow = false;
        const char* carName = "Octane";

        ~LabWorld() { Clear(); }

        void Clear()
        {
            if (arena) { delete arena; arena = nullptr; car = nullptr; }
        }

        // Your car's hitbox (full sizes) picks the closest of RocketSim's car presets.
        static const RocketSim::CarConfig& ConfigFor(const Vector& fullSize, const char** name)
        {
            using namespace RocketSim;
            struct P { const CarConfig* c; const char* n; };
            const P presets[] = { { &CAR_CONFIG_OCTANE, "Octane" }, { &CAR_CONFIG_DOMINUS, "Dominus" }, { &CAR_CONFIG_PLANK, "Plank" },
                                  { &CAR_CONFIG_BREAKOUT, "Breakout" }, { &CAR_CONFIG_HYBRID, "Hybrid" }, { &CAR_CONFIG_MERC, "Merc" } };
            const P* best = &presets[0];
            float bestD = 1e9f;
            for (const P& p : presets)
            {
                const Vec& h = p.c->hitboxSize;
                float d = std::fabs(h.x - fullSize.X) + std::fabs(h.y - fullSize.Y) + std::fabs(h.z - fullSize.Z);
                if (d < bestD) { bestD = d; best = &p; }
            }
            *name = best->n;
            return *best->c;
        }

        // A fresh run: the car at the origin, turned as `rot`, not moving; the first ball.
        void Start(const LabParams& p, const Basis& rot, const Vector& hitboxFull, std::mt19937& rng, double now)
        {
            using namespace RocketSim;
            if (RocketSim::GetStage() == RocketSimStage::UNINITIALIZED) RocketSim::InitFromMem({}, true);
            Clear();
            arena = Arena::Create(GameMode::THE_VOID);
            car = arena->AddCar(Team::BLUE, ConfigFor(hitboxFull, &carName));
            ApplySettings(p);
            CarState cs;
            cs.pos = Vec(0, 0, 0);
            cs.rotMat = RotMat(ToRs(rot.forward), ToRs(rot.right), ToRs(rot.up));
            cs.isOnGround = false;
            car->SetState(cs);
            GiveFlip();
            s = LabState{};
            LabRespawn(s, rng, p, rot.forward, now);
            if (p.motion == kLabConstant) s.phaseEnd = now + std::max(p.hangSec, 1.f);
            s.runStart = now;
            s.runEnd = p.runSec > 0.f ? now + std::max(p.hangSec, 1.f) + p.runSec : 1e18;
            PlaceBall(s.ballPos, V(0.f, 0.f, 0.f));
            lastHitTick = car->GetState().ballHitInfo.tickCountWhenHit;
            wheelsOnBall = 0;
            flipResets = 0;
            Mirror();
        }

        bool Ready() const { return arena != nullptr; }

        Basis CarBasis() const
        {
            const RocketSim::RotMat& m = car->GetState().rotMat;
            return Basis{ FromRs(m.forward), FromRs(m.right), FromRs(m.up) };
        }
        RocketSim::CarState GetCarState() const { return car->GetState(); }

        // One 120 Hz tick.
        void Step(const LabParams& p, const LabControls& in, std::mt19937& rng, double now)
        {
            using namespace RocketSim;
            s.touchedNow = s.missedNow = false;
            resetNow = false;
            if (!arena || s.phase == kLabDone) return;
            if (now >= s.runEnd)
            {
                s.phase = kLabDone;
                SetCarVel(Vec(0, 0, 0));
                PlaceBall(s.ballPos, V(0.f, 0.f, 0.f));
                Mirror();
                return;
            }
            ApplySettings(p);

            // ---- the lab's rules before the tick: a waiting ball launches, a free ball curves ----
            if (s.phase == kLabHang && now >= s.phaseEnd)
            {
                SetBallVel(Add(FromRs(car->GetState().vel), Scale(s.launchDir, s.launchSpeed)) /* as you see it */);
                s.phase = kLabFlying;
                s.free = true;
                s.curveAxisUntil = 0.0;
            }
            // While waiting, both bodies keep falling under gravity. Clearing the ball's
            // velocity each tick would hold it up while the car accelerates downward.
            if (s.free && p.curve > 0.f && s.phase == kLabFlying) Curve(p, rng, now);

            // ---- the tick itself ----
            const Vector relPosBefore = s.ballPos;
            const Vector relVelBefore = Sub(FromRs(arena->ball->GetState().vel), FromRs(car->GetState().vel));
            CarControls c;
            c.throttle = in.throttle; c.steer = in.steer;
            c.pitch = in.pitch; c.yaw = in.yaw; c.roll = in.roll;
            if (in.handbrake && std::fabs(in.roll) < 0.01f) { c.roll = in.yaw; c.yaw = 0.f; } // free air roll
            c.boost = in.boost;
            c.jump = in.jump && p.flips != kLabFlipsOff;
            c.handbrake = in.handbrake;
            c.ClampFix();
            car->controls = c;
            const bool onBallBefore = car->GetState().isOnGround;
            arena->Step(1);

            // ---- what happened ----
            const RocketSim::CarState cs = car->GetState();
            const bool hit = cs.ballHitInfo.isValid && cs.ballHitInfo.tickCountWhenHit != lastHitTick;
            lastHitTick = cs.ballHitInfo.tickCountWhenHit;
            int wheels = 0;
            for (bool w : cs.wheelsWithContact) wheels += w ? 1 : 0;
            const bool wheelTouch = wheels > 0 && wheelsOnBall == 0;
            wheelsOnBall = wheels;
            if (cs.isOnGround && !onBallBefore) { ++flipResets; resetNow = true; } // 3+ wheels on the ball: the game's rule
            if ((hit || wheelTouch) && now - s.lastTouch >= kLabTouchGap)
            {
                const Vector toBall = Norm(relPosBefore);
                s.lastClosing = std::max(0.f, -Dot(relVelBefore, toBall));
                ++s.touches;
                s.touchedNow = true;
            }
            if (hit || wheelTouch || wheels > 0)
            {
                s.lastTouch = now;
                s.free = false;
                if (s.phase == kLabHang) s.phase = kLabFlying;
                if (p.motion == kLabRespawn) { s.phase = kLabTouched; s.phaseEnd = now + kLabTouchedSec; }
            }
            Mirror();
            if (s.touchedNow) s.lastBallSpeed = Len(LabRelVel(s));

            // ---- Respawn: a touched ball is replaced 1.5 s after the last touch ----
            if (s.phase == kLabTouched && now >= s.phaseEnd) { NewBall(p, rng, now); return; }

            // ---- the edge of the bubble, and the floor ----
            const Vector relNow = LabRelVel(s);
            const float r = Len(s.ballPos);
            const bool outEdge = r >= p.bubble && Dot(s.ballPos, relNow) > 0.f;
            const bool outFloor = s.ballPos.Z <= -p.floor && relNow.Z < 0.f;
            if (outEdge || outFloor)
            {
                if (p.motion == kLabRespawn)
                {
                    if (s.phase == kLabFlying) { ++s.misses; s.missedNow = true; }
                    NewBall(p, rng, now);
                }
                else if (now - s.lastBounce >= kLabBounceGap)
                {
                    // A fresh start: the ball comes back at the set speed, your speed resets, and
                    // (Real flips) you get a flip. It can't be lost.
                    Vector pos = s.ballPos;
                    if (outEdge) pos = Scale(pos, p.bubble / r);
                    if (pos.Z < -p.floor) pos.Z = -p.floor;
                    SetCarVel(RocketSim::Vec(0, 0, 0));
                    PlaceBall(pos, LabPickLaunch(rng, pos, p, LabNextSpeed(rng, p), kLabReactSec));
                    if (p.flips == kLabFlipsReal) GiveFlip();
                    if (s.phase == kLabHang) s.phase = kLabFlying;
                    s.free = true;
                    s.curveAxisUntil = 0.0;
                    s.lastBounce = now;
                    ++s.bounces;
                    Mirror();
                }
                else
                {
                    Vector pos = s.ballPos;
                    if (outEdge) pos = Scale(pos, p.bubble / r);
                    if (pos.Z < -p.floor) pos.Z = -p.floor;
                    PlaceBall(pos, Add(FromRs(car->GetState().vel), relNow));
                    Mirror();
                }
            }

            // Keep the numbers small: now and then, move both back to the origin (nothing changes
            // between them, so nothing you can see changes).
            const RocketSim::Vec cp = car->GetState().pos;
            if (cp.Length() > 20000.f) Recentre();
        }

        // Set up an exact situation (tests): the ball at `rel` from the car, both velocities.
        void Place(const Vector& rel, const Vector& ballVel, const Vector& carVel)
        {
            SetCarVel(ToRs(carVel));
            PlaceBall(rel, ballVel);
            s.phase = kLabFlying;
            s.free = false;
            Mirror();
        }

        // You can flip: like a flip reset, it lasts until you use it.
        void GiveFlip()
        {
            RocketSim::CarState cs = car->GetState();
            cs.hasJumped = false;
            cs.hasDoubleJumped = false;
            cs.hasFlipped = false;
            cs.isJumping = false;
            cs.isFlipping = false;
            cs.isOnGround = false;
            car->SetState(cs);
        }

    private:
        RocketSim::Arena* arena = nullptr;
        RocketSim::Car* car = nullptr;
        uint64_t lastHitTick = ~0ULL;

        void ApplySettings(const LabParams& p)
        {
            RocketSim::MutatorConfig m = arena->GetMutatorConfig();
            m.gravity = RocketSim::Vec(0, 0, RocketSim::RLConst::GRAVITY_Z); // both bodies: normal match gravity
            m.ballDrag = p.speedLimit ? RocketSim::RLConst::BALL_DRAG : 0.f;
            m.ballMaxSpeed = p.speedLimit ? RocketSim::RLConst::BALL_MAX_SPEED : 1e6f;
            m.unlimitedFlips = p.flips == kLabFlipsInfinite;
            m.unlimitedDoubleJumps = p.flips == kLabFlipsInfinite;
            m.boostUsedPerSecond = 0.f;                   // boost never runs out
            arena->SetMutatorConfig(m);
            p_flipsInfinite = p.flips == kLabFlipsInfinite;
            RocketSim::g_carMaxSpeedUU = p.speedLimit ? RocketSim::RLConst::CAR_MAX_SPEED : 0.f;
        }

        void Mirror()
        {
            const RocketSim::CarState cs = car->GetState();
            const RocketSim::BallState bs = arena->ball->GetState();
            s.carVel = FromRs(cs.vel);
            s.ballVel = FromRs(bs.vel);
            s.ballPos = Sub(FromRs(bs.pos), FromRs(cs.pos));
            ballSpin = FromRs(bs.angVel);
            carHasFlip = p_flipsInfinite || cs.HasFlipOrJump() || cs.HasFlipReset();
            s.flipReady = carHasFlip;
            s.resets = flipResets;
            s.wheelsTouching = wheelsOnBall;
        }
        bool p_flipsInfinite = false;

        void SetCarVel(const RocketSim::Vec& v)
        {
            RocketSim::CarState cs = car->GetState();
            cs.vel = v;
            car->SetState(cs);
        }
        void SetBallVel(const Vector& v)
        {
            RocketSim::BallState bs = arena->ball->GetState();
            bs.vel = ToRs(v);
            arena->ball->SetState(bs);
        }
        // The ball at `rel` from the car, moving at `vel` (in the hidden world).
        void PlaceBall(const Vector& rel, const Vector& vel)
        {
            RocketSim::BallState bs = arena->ball->GetState();
            bs.pos = car->GetState().pos + ToRs(rel);
            bs.vel = ToRs(vel);
            bs.angVel = RocketSim::Vec(0, 0, 0);
            arena->ball->SetState(bs);
        }
        void NewBall(const LabParams& p, std::mt19937& rng, double now)
        {
            const Basis b = CarBasis();
            LabRespawn(s, rng, p, b.forward, now); // picks the spot, the launch, resets the hang
            SetCarVel(RocketSim::Vec(0, 0, 0));
            PlaceBall(s.ballPos, V(0.f, 0.f, 0.f));
            if (p.flips == kLabFlipsReal) GiveFlip();
            Mirror();
        }
        void Curve(const LabParams& p, std::mt19937& rng, double now)
        {
            const float dt = 1.f / 120.f;
            const Vector carV = FromRs(car->GetState().vel);
            Vector v = Sub(FromRs(arena->ball->GetState().vel), carV); // curve the path you see
            const float sp = Len(v);
            if (sp < 1.f) return;
            const Vector dir = Scale(v, 1.f / sp);
            if (now >= s.curveAxisUntil || std::fabs(Dot(s.curveAxis, dir)) > 0.2f)
            {
                if (p.steep < 89.f) s.curveAxis = std::uniform_int_distribution<int>(0, 1)(rng) ? V(0.f, 0.f, 1.f) : V(0.f, 0.f, -1.f);
                else
                {
                    Vector a = LabRandomDir(rng);
                    s.curveAxis = Norm(Sub(a, Scale(dir, Dot(a, dir))));
                }
                s.curveAxisUntil = now + kLabCurveAxisSec;
            }
            if (std::fabs(Dot(s.curveAxis, dir)) < 0.999f)
                SetBallVel(Add(carV, Scale(RotateAbout(dir, s.curveAxis, p.curve * kDeg * dt), sp)));
        }
        void Recentre()
        {
            RocketSim::CarState cs = car->GetState();
            RocketSim::BallState bs = arena->ball->GetState();
            bs.pos = bs.pos - cs.pos;
            cs.pos = RocketSim::Vec(0, 0, 0);
            car->SetState(cs);
            arena->ball->SetState(bs);
        }
    };
}
