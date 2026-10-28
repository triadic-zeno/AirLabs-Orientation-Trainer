#pragma once
// Boost Lab (test): fly at a ball in open space with real Rocket League momentum.
//
// The car stays pinned on screen and everything is worked out relative to it. Underneath,
// there's a hidden world where the car really moves: boost pushes it along its nose (and it
// keeps drifting when you let go: there are no brakes in the air), the ball has its own
// velocity, and what you see is the ball's position relative to your car. Gravity pulls the
// car and the ball down equally, so it doesn't change how they move relative to each other,
// and there isn't any here.
//
// The ball lives inside a bubble around the car. A new ball appears somewhere random inside
// it, waits a moment, then launches in a random direction. Respawn: touch it and it flies off,
// and a new ball appears shortly after (one that reaches the edge untouched is a miss).
// Constant: the ball never stops; at the edge it bounces back inwards. Launches and bounces never
// aim the ball straight at you: its path always passes at least `pass` uu from the car.
//
// Touches are simple for now: any contact sends the ball off at a set speed along the line
// from the car's centre through the ball.
//
// Needs TrainerCore.h first (Vector helpers, Basis). Nothing here talks to the game.

#include <random>

namespace airlabs
{
    constexpr float kBoostAccel       = 991.667f;  // uu/s^2, boost in the air (community-measured)
    constexpr float kAirThrottleAccel = 66.667f;   // uu/s^2, throttle in the air (without boost)
    constexpr float kCarTopSpeed      = 2300.f;    // uu/s, the car's speed cap
    constexpr float kLabBallTopSpeed  = 6000.f;    // uu/s, the ball's speed cap
    constexpr float kLabBallRadius    = 92.75f;
    constexpr float kLabTouchGap      = 0.1f;      // s, a second touch can't count sooner than this
    constexpr float kLabBounceGap     = 0.25f;     // s, Constant: the ball can't bounce again sooner than this
    constexpr float kLabTouchedSec    = 0.6f;      // s, Respawn: a touched ball flies off this long, then a new one appears

    enum LabMotion { kLabRespawn = 0, kLabConstant = 1 };
    enum LabPhase  { kLabHang = 0, kLabFlying = 1, kLabTouched = 2 };

    struct LabParams
    {
        int motion = kLabRespawn;
        float bubble = 800.f;    // uu, radius of the bubble around the car
        float pass = 250.f;      // uu, how close a launched or bounced ball's path may come to the car
        float speed = 800.f;     // uu/s, the ball's speed (as you see it) after a launch or bounce
        float hitSpeed = 1500.f; // uu/s, simple touch: how fast the ball leaves your car
        float hangSec = 0.75f;   // s, Respawn: how long a new ball waits before it launches
        float spawnMin = 350.f;  // uu, Respawn: the nearest a new ball appears
    };

    // The car's hitbox in its own axes (X forward, Y right, Z up): half sizes and the offset of
    // its centre from the car's origin. Default: the Octane.
    struct LabHitbox
    {
        Vector half = V(59.f, 42.1f, 18.1f);
        Vector offset = V(13.88f, 0.f, 20.75f);
    };

    struct LabState
    {
        Vector carVel = V(0.f, 0.f, 0.f);  // uu/s, your car in the hidden world
        Vector ballPos = V(600.f, 0.f, 0.f); // uu, the ball relative to the car's origin (what you see)
        Vector ballVel = V(0.f, 0.f, 0.f); // uu/s, the ball in the hidden world
        int phase = kLabHang;
        double phaseEnd = 0.0;              // Hang: when the ball launches
        double lastTouch = -10.0, lastBounce = -10.0;
        int touches = 0, misses = 0, balls = 0, bounces = 0;
        float lastClosing = 0.f;            // uu/s, how fast you were closing on the ball at the last touch
        bool touchedNow = false, missedNow = false; // set for the tick it happened
    };

    inline Vector LabRelVel(const LabState& s) { return Sub(s.ballVel, s.carVel); }

    inline Vector LabRandomDir(std::mt19937& rng)
    {
        // Even over the whole sphere: an even height, then an even angle around it. (Even
        // angles up and down would bunch the directions up at the top and bottom.)
        std::uniform_real_distribution<float> uz(-1.f, 1.f), ua(0.f, 2.f * kPi);
        float z = uz(rng), a = ua(rng), r = std::sqrt(std::max(0.f, 1.f - z * z));
        return V(r * std::cos(a), r * std::sin(a), z);
    }

    // How far a ball at p (inside the bubble, or on its edge) travels along d before leaving it.
    inline float LabExitDistance(const Vector& p, const Vector& d, float radius)
    {
        float b = Dot(p, d), c = Dot(p, p) - radius * radius;
        float disc = b * b - c;
        if (disc <= 0.f) return 0.f;
        return std::max(0.f, -b + std::sqrt(disc));
    }

    // The closest a ball at p moving along d comes to the car (from now on).
    inline float LabPassDistance(const Vector& p, const Vector& d)
    {
        float t = std::max(0.f, -Dot(p, d));
        return Len(Add(p, Scale(d, t)));
    }

    // A direction for a launch or bounce from p: its path passes at least `pass` from the car
    // and crosses at least `minRun` of the bubble (from the edge, that's a cone pointing inwards:
    // minRun = the radius gives 60 degrees around the inward direction). If nothing random
    // fits (settings that can't all be met), the closest fit is used.
    inline Vector LabPickDirection(std::mt19937& rng, const Vector& p, float radius, float pass, float minRun)
    {
        Vector best = Scale(Norm(p), -1.f);
        float bestFit = -1.f;
        for (int i = 0; i < 400; ++i)
        {
            Vector d = LabRandomDir(rng);
            float run = LabExitDistance(p, d, radius), miss = LabPassDistance(p, d);
            if (run >= minRun && miss >= pass) return d;
            float fit = std::min(minRun > 0.f ? run / minRun : 1.f, pass > 0.f ? miss / pass : 1.f);
            if (fit > bestFit) { bestFit = fit; best = d; }
        }
        return best;
    }

    // A spot for a new ball: any direction, between spawnMin and a little inside the edge.
    inline Vector LabSpawnPoint(std::mt19937& rng, const LabParams& p)
    {
        float hi = std::max(150.f, p.bubble - 60.f);
        float lo = std::clamp(p.spawnMin, 150.f, hi);
        std::uniform_real_distribution<float> ur(lo, hi);
        return Scale(LabRandomDir(rng), ur(rng));
    }

    // Distance from the ball's centre at p to the car's hitbox (0 inside it), and the box's
    // centre in world axes.
    inline float LabBoxDistance(const Vector& p, const Basis& car, const LabHitbox& hb, Vector* centreOut = nullptr)
    {
        Vector c = Add(Add(Scale(car.forward, hb.offset.X), Scale(car.right, hb.offset.Y)), Scale(car.up, hb.offset.Z));
        if (centreOut) *centreOut = c;
        Vector d = Sub(p, c);
        float lx = Dot(d, car.forward), ly = Dot(d, car.right), lz = Dot(d, car.up);
        float ex = std::max(0.f, std::fabs(lx) - hb.half.X);
        float ey = std::max(0.f, std::fabs(ly) - hb.half.Y);
        float ez = std::max(0.f, std::fabs(lz) - hb.half.Z);
        return std::sqrt(ex * ex + ey * ey + ez * ez);
    }

    // A new ball: your momentum is reset (each ball starts clean), and the ball waits.
    inline void LabRespawn(LabState& s, std::mt19937& rng, const LabParams& p, double now)
    {
        s.carVel = V(0.f, 0.f, 0.f);
        s.ballVel = V(0.f, 0.f, 0.f);
        s.ballPos = LabSpawnPoint(rng, p);
        s.phase = kLabHang;
        s.phaseEnd = now + p.hangSec;
        ++s.balls;
    }

    inline void LabStart(LabState& s, std::mt19937& rng, const LabParams& p, double now)
    {
        s = LabState{};
        LabRespawn(s, rng, p, now);
        if (p.motion == kLabConstant) s.phaseEnd = now + std::max(p.hangSec, 1.f); // a moment to find it first
    }

    inline void CapSpeed(Vector& v, float cap)
    {
        float l = Len(v);
        if (l > cap) v = Scale(v, cap / l);
    }

    // One physics tick. boost: holding boost; throttle: -1..1.
    inline void LabStep(LabState& s, std::mt19937& rng, const LabParams& p, const Basis& car, const LabHitbox& hb,
                        bool boost, float throttle, double now, float dt)
    {
        s.touchedNow = s.missedNow = false;
        if (dt <= 0.f) return;

        // ---- your car: boost (or air throttle) along the nose, capped at 2300 ----
        if (boost) s.carVel = Add(s.carVel, Scale(car.forward, kBoostAccel * dt));
        else if (std::fabs(throttle) > 0.01f) s.carVel = Add(s.carVel, Scale(car.forward, kAirThrottleAccel * std::clamp(throttle, -1.f, 1.f) * dt));
        CapSpeed(s.carVel, kCarTopSpeed);

        // ---- a waiting ball launches ----
        if (s.phase == kLabHang && now >= s.phaseEnd)
        {
            Vector d = LabPickDirection(rng, s.ballPos, p.bubble, p.pass, p.bubble);
            s.ballVel = Add(s.carVel, Scale(d, p.speed)); // the launch speed is as you see it
            CapSpeed(s.ballVel, kLabBallTopSpeed);
            s.phase = kLabFlying;
        }

        // ---- move, and touch: the ball stops at your car this tick and leaves the next ----
        // (Respawn: a ball you've touched is on its way out and can't be touched again.)
        const Vector rel = LabRelVel(s);
        const Vector next = Add(s.ballPos, Scale(rel, dt));
        Vector boxCentre;
        const bool contact = LabBoxDistance(next, car, hb, &boxCentre) <= kLabBallRadius + 2.f;
        if (contact && now - s.lastTouch >= kLabTouchGap && s.phase != kLabTouched)
        {
            Vector n = Norm(Sub(s.ballPos, boxCentre));
            s.lastClosing = std::max(0.f, -Dot(rel, n));
            s.ballVel = Add(s.carVel, Scale(n, p.hitSpeed));
            CapSpeed(s.ballVel, kLabBallTopSpeed);
            s.lastTouch = now;
            ++s.touches;
            s.touchedNow = true;
            s.phase = p.motion == kLabRespawn ? kLabTouched : kLabFlying;
            if (p.motion == kLabRespawn) s.phaseEnd = now + kLabTouchedSec;
        }
        else
        {
            s.ballPos = next;
        }
        // Never inside the car (turning the car into the ball, or a touch that can't count
        // yet): push it out to the surface, so the game's own car never hits it.
        for (int i = 0; i < 3; ++i)
        {
            float bd = LabBoxDistance(s.ballPos, car, hb, &boxCentre);
            if (bd >= kLabBallRadius + 2.f) break;
            s.ballPos = Add(s.ballPos, Scale(Norm(Sub(s.ballPos, boxCentre)), kLabBallRadius + 2.f - bd));
        }

        // ---- Respawn: a touched ball is replaced shortly after ----
        if (s.phase == kLabTouched && now >= s.phaseEnd)
        {
            LabRespawn(s, rng, p, now);
            return;
        }

        // ---- the edge of the bubble ----
        const float r = Len(s.ballPos);
        if (r >= p.bubble && Dot(s.ballPos, LabRelVel(s)) > 0.f)
        {
            if (p.motion == kLabRespawn)
            {
                if (s.phase == kLabFlying) { ++s.misses; s.missedNow = true; } // it got away untouched
                LabRespawn(s, rng, p, now);
            }
            else if (now - s.lastBounce >= kLabBounceGap)
            {
                // Bounce back inwards at the set speed, as you see it, whatever you're doing:
                // the ball can't be lost.
                s.ballPos = Scale(s.ballPos, p.bubble / r);
                Vector d = LabPickDirection(rng, s.ballPos, p.bubble, p.pass, p.bubble);
                s.ballVel = Add(s.carVel, Scale(d, p.speed));
                CapSpeed(s.ballVel, kLabBallTopSpeed);
                if (s.phase == kLabHang) s.phase = kLabFlying;
                s.lastBounce = now;
                ++s.bounces;
            }
            else
            {
                s.ballPos = Scale(s.ballPos, p.bubble / r); // just bounced: it slides along the edge
            }
        }
    }
}
