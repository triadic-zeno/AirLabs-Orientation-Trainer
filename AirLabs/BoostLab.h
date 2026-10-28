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
// The ball lives inside a bubble around the car. A new ball appears somewhere inside it,
// waits a moment (its launch direction already chosen, so it can be shown), then launches.
// Respawn: touch it and a new ball appears shortly after (one that reaches the edge untouched
// got away). Constant: the ball never stops; at the edge it bounces back inwards, and each bounce
// is a fresh start like a new ball: it comes back at the set speed and your own speed resets (so
// you're never stuck at the speed cap chasing a ball you can't catch, and a bounce never comes
// at you faster than the set speed). Launches and bounces never aim the ball straight at you
// (its path passes at least `pass` uu from the car), always go at a speed and in a direction you
// can actually reach from where you are (slower than the set speed if it has to), never travel steeper than `steep` degrees
// from level, and never take it below the floor (`floor` uu under you), which it also bounces off.
//
// Speed limit (an option): the car's 2300 cap, the ball's 6000 and the ball's air drag (about 3% of
// its speed a second). All three are measured against a field; in open space there isn't one, so
// with the limit off only how you move relative to the ball counts (an air dribble can go on for
// ever, as gravity cancels between car and ball). Cars have no drag.
//
// Flips: the game does the flip itself when it can (the plugin adds its push to your momentum);
// LabFlipImpulse is Rocket League's flip push for when it can't. Real flips: one for each new ball
// (and each bounce), and one for each flip reset (the ball on your wheels).
//
// Touches use Rocket League's ball-car hit as the community has measured it (RLUtilities):
// a collision impulse from how fast the car's contact point (including its spin) moves into
// the ball, with friction, plus Psyonix's extra push along car -> ball.
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
    constexpr float kLabBallMass      = 30.f;      // Rocket League's masses (only the ratio matters)
    constexpr float kLabCarMass       = 180.f;
    constexpr float kLabFriction      = 2.f;       // ball-car friction coefficient
    constexpr float kLabBallDrag      = 0.0305f;   // 1/s, the ball's air drag: it loses about 3% of its speed each second
    constexpr float kLabTouchGap      = 0.05f;     // s, a second touch can't count sooner than this
    constexpr float kLabBounceGap     = 0.25f;     // s, Constant: the ball can't bounce again sooner than this
    constexpr float kLabTouchedSec    = 1.5f;      // s, Respawn: a new ball appears this long after your last touch (or when it reaches the edge)
    // Flips (RocketSim's constants, which match Rocket League's)
    constexpr float kLabJumpImpulse   = 291.667f;  // uu/s along the car's roof: a double jump
    constexpr float kLabFlipImpulse   = 500.f;     // uu/s: a flip's push
    constexpr float kLabFlipSideScale = 1.9f;      // the sideways push grows with your forward speed, up to this at top speed
    constexpr float kLabFlipBackScale = 2.5f;      // the backwards push, likewise
    constexpr float kLabFlipDeadzone  = 0.5f;      // |pitch| + |yaw| + |roll| below this: a double jump instead of a flip
    constexpr float kLabFlipTime      = 0.65f;     // s, how long a flip turns the car
    constexpr float kLabCurveAxisSec  = 1.f;       // s, a curving ball picks a new way to curve this often
    constexpr float kLabReactSec      = 0.4f;      // s, reachable check: time to react and turn before you boost (Constant)
    constexpr float kLabReactSecWait  = 0.2f;      // s, the same when the ball waited with its arrow first (Respawn)
    constexpr float kLabReachMargin   = 140.f;     // uu, reachable check: close enough to touch (ball radius + about half a car)

    enum LabMotion { kLabRespawn = 0, kLabConstant = 1 };
    enum LabPhase  { kLabHang = 0, kLabFlying = 1, kLabTouched = 2, kLabDone = 3 };
    enum LabFlips  { kLabFlipsOff = 0, kLabFlipsReal = 1, kLabFlipsInfinite = 2 };

    struct LabParams
    {
        int motion = kLabRespawn;
        float bubble = 800.f;     // uu, radius of the bubble around the car
        float pass = 250.f;       // uu, how close a launched or bounced ball's path may come to the car
        float speed = 800.f;      // uu/s, the ball's speed after a launch or bounce
        bool dynamic = false;     // a random speed between speedMin and speedMax for each launch or bounce
        float speedMin = 400.f, speedMax = 1400.f;
        float curve = 0.f;        // deg/s, how fast a free ball curves (0 = straight lines)
        float hangSec = 0.75f;    // s, Respawn: how long a new ball waits before it launches
        float spawnMin = 350.f;   // uu, the nearest a new ball appears
        float spawnCone = 180.f;  // deg, new balls appear within this angle of your nose (180 = anywhere)
        float heightMin = -90.f, heightMax = 90.f; // deg above the horizon a new ball can appear at
        float steep = 90.f;       // deg, the steepest a launched or bounced ball travels, up or down from level
        float floor = 5000.f;     // uu, how far below your car the ball can go (it bounces off this floor)
        float runSec = 0.f;       // s, run length (0 = endless)
        bool speedLimit = true;   // the car's 2300 cap, the ball's 6000 cap and its air drag (all measured against a field;
                                  // off: open space, where only how you move relative to the ball counts)
        int flips = kLabFlipsReal; // Real: one flip for each new ball, and one for each flip reset (wheels on the ball)
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
        Vector carVel = V(0.f, 0.f, 0.f);    // uu/s, your car in the hidden world
        Vector ballPos = V(600.f, 0.f, 0.f); // uu, the ball relative to the car's origin (what you see)
        Vector ballVel = V(0.f, 0.f, 0.f);   // uu/s, the ball in the hidden world
        Vector launchDir = V(1.f, 0.f, 0.f); // where a waiting ball will go
        float launchSpeed = 800.f;
        bool free = false;                   // flying on its own (launched or bounced, not touched since): it may curve
        Vector curveAxis = V(0.f, 0.f, 1.f);
        double curveAxisUntil = 0.0;
        int phase = kLabHang;
        double phaseEnd = 0.0;               // Hang: when the ball launches; Touched: when the next ball appears
        double runStart = 0.0, runEnd = 1e18;
        double lastTouch = -10.0, lastBounce = -10.0;
        int touches = 0, misses = 0, balls = 0, bounces = 0;
        float lastClosing = 0.f;             // uu/s, how fast your car met the ball at the last touch
        float lastBallSpeed = 0.f;           // uu/s, how fast the ball left you (relative to your car)
        bool touchedNow = false, missedNow = false; // set for the tick it happened
        bool flipReady = true;               // Real flips: you have a flip (or double jump) to use
        int resets = 0, flipsUsed = 0;
        bool resetNow = false;               // a flip reset this tick
    };

    inline Vector LabRelVel(const LabState& s) { return Sub(s.ballVel, s.carVel); }

    inline void CapSpeed(Vector& v, float cap)
    {
        float l = Len(v);
        if (l > cap) v = Scale(v, cap / l);
    }

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

    // How far it travels along d before leaving the bubble or going through the floor.
    inline float LabRunDistance(const Vector& p, const Vector& d, float radius, float floor)
    {
        float run = LabExitDistance(p, d, radius);
        if (d.Z < -1e-4f) run = std::min(run, std::max(0.f, (p.Z + floor) / -d.Z));
        return run;
    }

    // The closest a ball at p moving along d comes to the car (from now on).
    inline float LabPassDistance(const Vector& p, const Vector& d)
    {
        float t = std::max(0.f, -Dot(p, d));
        return Len(Add(p, Scale(d, t)));
    }

    // A direction for a ball at p: its path passes at least `pass` from the car, travels no
    // steeper than `steep` degrees from level, and crosses at least `minRun` of the bubble before
    // the edge or the floor (from the edge, that's a cone pointing inwards: minRun = the radius
    // gives 60 degrees around the inward direction). If nothing random fits (settings that can't
    // all be met), the closest fit is used.
    inline Vector LabPickDirection(std::mt19937& rng, const Vector& p, const LabParams& prm, float minRun)
    {
        Vector best = Scale(Norm(V(p.X, p.Y, 0.f)), -1.f);
        float bestFit = -1.f;
        const float steepSin = std::sin(std::clamp(prm.steep, 0.f, 90.f) * kDeg);
        for (int i = 0; i < 600; ++i)
        {
            Vector d = LabRandomDir(rng);
            if (std::fabs(d.Z) > steepSin) // too steep: flatten it to the limit
            {
                float h = std::sqrt(std::max(1e-6f, d.X * d.X + d.Y * d.Y));
                float c = std::sqrt(std::max(0.f, 1.f - steepSin * steepSin));
                d = V(d.X / h * c, d.Y / h * c, d.Z > 0.f ? steepSin : -steepSin);
            }
            float run = LabRunDistance(p, d, prm.bubble, prm.floor), miss = LabPassDistance(p, d);
            if (run >= minRun && miss >= prm.pass) return d;
            float fit = std::min(minRun > 0.f ? run / minRun : 1.f, prm.pass > 0.f ? miss / prm.pass : 1.f);
            if (fit > bestFit) { bestFit = fit; best = d; }
        }
        return best;
    }

    // How far a car starting from rest gets in t seconds, boosting flat out (capped at 2300).
    inline float LabBoostDistance(float t, bool speedLimit = true)
    {
        if (t <= 0.f) return 0.f;
        if (!speedLimit) return 0.5f * kBoostAccel * t * t;
        const float tCap = kCarTopSpeed / kBoostAccel;
        if (t <= tCap) return 0.5f * kBoostAccel * t * t;
        return 0.5f * kBoostAccel * tCap * tCap + kCarTopSpeed * (t - tCap);
    }

    // Could you reach a ball at p moving at w (your car at rest), after `react` seconds to react
    // and turn, before it leaves the bubble or goes through the floor?
    inline bool LabReachable(const Vector& p, const Vector& w, const LabParams& prm, float react)
    {
        const float sp = Len(w);
        float tMax = 4.f;
        if (sp > 1.f) tMax = std::min(tMax, LabRunDistance(p, Scale(w, 1.f / sp), prm.bubble, prm.floor) / sp);
        for (float t = 0.f; t <= tMax + 1e-4f; t += 0.02f)
            if (Len(Add(p, Scale(w, t))) - kLabReachMargin <= LabBoostDistance(t - react, prm.speedLimit)) return true;
        return false;
    }

    // A launch or bounce for a ball at p (your car at rest): a direction that keeps the pass
    // distance, the steepness and the run across the bubble, at a speed you can actually reach
    // it at. Starts at `speed`, and goes slower (by 15% at a time, down to 150) until a direction
    // is reachable.
    inline Vector LabPickLaunch(std::mt19937& rng, const Vector& p, const LabParams& prm, float speed, float react)
    {
        float sp = speed;
        for (int round = 0; round < 14; ++round)
        {
            for (int i = 0; i < 40; ++i)
            {
                Vector d = LabPickDirection(rng, p, prm, prm.bubble);
                if (LabReachable(p, Scale(d, sp), prm, react)) return Scale(d, sp);
            }
            if (sp <= 150.f) break;
            sp = std::max(150.f, sp * 0.85f);
        }
        return Scale(LabPickDirection(rng, p, prm, prm.bubble), sp);
    }

    inline float LabNextSpeed(std::mt19937& rng, const LabParams& p)
    {
        if (!p.dynamic) return p.speed;
        float lo = std::min(p.speedMin, p.speedMax), hi = std::max(p.speedMin, p.speedMax);
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }

    // A spot for a new ball, between spawnMin and a little inside the edge, within spawnCone
    // of your nose and the height band (both loosened if they can't be met).
    inline Vector LabSpawnPoint(std::mt19937& rng, const LabParams& p, const Vector& nose)
    {
        float hi = std::max(150.f, p.bubble - 60.f);
        float lo = std::clamp(p.spawnMin, 150.f, hi);
        float r = std::uniform_real_distribution<float>(lo, hi)(rng);
        const float cone = std::clamp(p.spawnCone, 10.f, 180.f);
        const float hMin = std::min(p.heightMin, p.heightMax), hMax = std::max(p.heightMin, p.heightMax);
        const float lowest = -(p.floor - kLabBallRadius); // keep it above the floor
        Vector d = LabRandomDir(rng);
        for (int i = 0; i < 2000; ++i)
        {
            float e = ElevationDeg(d);
            if (AngleDeg(d, nose) <= cone && e >= hMin && e <= hMax && d.Z * r >= lowest) break;
            d = LabRandomDir(rng);
        }
        Vector out = Scale(d, r);
        if (out.Z < lowest) out.Z = lowest;
        return out;
    }

    // The point of the car's hitbox nearest p, relative to the car's origin.
    inline Vector LabClosestOnBox(const Vector& p, const Basis& car, const LabHitbox& hb)
    {
        Vector c = Add(Add(Scale(car.forward, hb.offset.X), Scale(car.right, hb.offset.Y)), Scale(car.up, hb.offset.Z));
        Vector d = Sub(p, c);
        float lx = std::clamp(Dot(d, car.forward), -hb.half.X, hb.half.X);
        float ly = std::clamp(Dot(d, car.right), -hb.half.Y, hb.half.Y);
        float lz = std::clamp(Dot(d, car.up), -hb.half.Z, hb.half.Z);
        return Add(c, Add(Add(Scale(car.forward, lx), Scale(car.right, ly)), Scale(car.up, lz)));
    }

    // Distance from the ball's centre at p to the car's hitbox (0 inside it), and the box's centre.
    inline float LabBoxDistance(const Vector& p, const Basis& car, const LabHitbox& hb, Vector* centreOut = nullptr)
    {
        if (centreOut)
            *centreOut = Add(Add(Scale(car.forward, hb.offset.X), Scale(car.right, hb.offset.Y)), Scale(car.up, hb.offset.Z));
        return Len(Sub(p, LabClosestOnBox(p, car, hb)));
    }

    // Psyonix's extra push: its size relative to the speed between car and ball.
    inline float LabPushScale(float dv)
    {
        if (dv <= 500.f) return 0.65f;
        if (dv <= 2300.f) return 0.65f - 0.10f * (dv - 500.f) / 1800.f;
        return 0.55f - 0.25f * (std::min(dv, 4600.f) - 2300.f) / 2300.f;
    }

    // Rocket League's ball-car hit. The ball is at ballPos (relative to the car's origin) and
    // touches the box at contact; carSpin is how fast that point of the car moves because the
    // car is turning. Changes both velocities; returns how fast the car met the ball (<= 0: no hit).
    inline float LabHit(Vector& ballVel, Vector& carVel, const Vector& ballPos, const Vector& contact, const Vector& carSpin, const Basis& car,
                        bool speedLimit = true)
    {
        const Vector n1 = Norm(Sub(ballPos, contact));               // out of the car, into the ball
        const Vector dV = Sub(Add(carVel, carSpin), ballVel);        // car's contact point relative to the ball
        const float closing = Dot(dV, n1);
        if (closing <= 0.f) return closing;
        // The impulse that stops the contact point sliding and closing (a perfectly inelastic hit):
        // straight on, the ball and car act as their two masses; sideways the ball can also
        // roll (a solid sphere: 2.5x easier to move at its surface). The car is treated as not turning.
        const float a = 1.f / kLabCarMass + 1.f / kLabBallMass, b = 2.5f / kLabBallMass;
        const Vector dVn = Scale(n1, closing), dVt = Sub(dV, dVn);
        Vector Jn = Scale(dVn, 1.f / a), Jt = Scale(dVt, 1.f / (a + b));
        const float jn = Len(Jn), jt = Len(Jt);
        if (jt > kLabFriction * jn) Jt = Scale(Jt, kLabFriction * jn / jt); // friction can only grip so hard
        const Vector J1 = Add(Jn, Jt);
        // Psyonix's push: along car -> ball, with the part along the car's nose reduced.
        Vector n2 = ballPos;
        n2 = Norm(Sub(n2, Scale(car.forward, 0.35f * Dot(n2, car.forward))));
        const float dv = std::min(Len(Sub(ballVel, carVel)), 4600.f);
        const Vector J2 = Scale(n2, kLabBallMass * dv * LabPushScale(dv));
        ballVel = Add(ballVel, Scale(Add(J1, J2), 1.f / kLabBallMass));
        carVel = Sub(carVel, Scale(J1, 1.f / kLabCarMass));
        if (speedLimit)
        {
            CapSpeed(ballVel, kLabBallTopSpeed);
            CapSpeed(carVel, kCarTopSpeed);
        }
        return closing;
    }

    // A flip or double jump as Rocket League does it (RocketSim), for when the game itself
    // doesn't: the stick decides. Little stick: a double jump (a push along the roof). Otherwise
    // a flip, pushing the way the stick points (forward/back along the nose, sideways along the
    // right), flattened to level like the game's, and stronger sideways and backwards the faster
    // you're going forwards. Returns the flip direction (x forward, y right), or 0,0 for a jump.
    inline Vector LabFlipImpulse(Vector& carVel, const Basis& car, float pitch, float yaw, float roll, bool speedLimit)
    {
        if (std::fabs(pitch) + std::fabs(yaw) + std::fabs(roll) < kLabFlipDeadzone)
        {
            carVel = Add(carVel, Scale(car.up, kLabJumpImpulse));
            if (speedLimit) CapSpeed(carVel, kCarTopSpeed);
            return V(0.f, 0.f, 0.f);
        }
        float dx = -pitch, dy = yaw + roll; // stick forward (pitch -1) = flip forward
        const float l = std::sqrt(dx * dx + dy * dy);
        dx /= l; dy /= l;
        if (std::fabs(dx) < 0.1f) dx = 0.f;
        if (std::fabs(dy) < 0.1f) dy = 0.f;
        const float forwardSpeed = Dot(carVel, car.forward);
        const float ratio = std::min(1.f, std::fabs(forwardSpeed) / kCarTopSpeed);
        const bool backwards = dx < 0.f;
        float ix = dx * kLabFlipImpulse * (((backwards ? kLabFlipBackScale : 1.f) - 1.f) * ratio + 1.f);
        float iy = dy * kLabFlipImpulse * ((kLabFlipSideScale - 1.f) * ratio + 1.f);
        if (backwards) ix *= 16.f / 15.f;
        Vector f2 = V(car.forward.X, car.forward.Y, 0.f);
        if (Len(f2) < 0.2f) f2 = V(-car.up.X, -car.up.Y, 0.f) ; // nose straight up or down: use where the roof faces
        f2 = Norm(f2);
        const Vector r2 = V(-f2.Y, f2.X, 0.f);
        carVel = Add(carVel, Add(Scale(f2, ix), Scale(r2, iy)));
        if (speedLimit) CapSpeed(carVel, kCarTopSpeed);
        return V(dx, dy, 0.f);
    }

    // The car's axes back to an Unreal rotator (the reverse of BasisFromRotator).
    inline void RotatorFromBasis(const Basis& b, int& pitchU, int& yawU, int& rollU)
    {
        const float k = 32768.f / kPi;
        pitchU = static_cast<int>(std::lround(std::asin(std::clamp(b.forward.Z, -1.f, 1.f)) * k));
        yawU = static_cast<int>(std::lround(std::atan2(b.forward.Y, b.forward.X) * k));
        rollU = static_cast<int>(std::lround(std::atan2(-b.right.Z, b.up.Z) * k));
    }

    // A new ball: your momentum is reset (each ball starts clean), and the ball waits, with
    // where it's going already chosen.
    inline void LabRespawn(LabState& s, std::mt19937& rng, const LabParams& p, const Vector& nose, double now)
    {
        s.carVel = V(0.f, 0.f, 0.f);
        s.ballVel = V(0.f, 0.f, 0.f);
        s.ballPos = LabSpawnPoint(rng, p, nose);
        const Vector w = LabPickLaunch(rng, s.ballPos, p, LabNextSpeed(rng, p), kLabReactSecWait);
        s.launchSpeed = Len(w);
        s.launchDir = Scale(w, 1.f / std::max(1.f, s.launchSpeed));
        s.free = false;
        s.phase = kLabHang;
        s.phaseEnd = now + p.hangSec;
        s.flipReady = true; // a flip for each new ball
        ++s.balls;
    }

    inline void LabStart(LabState& s, std::mt19937& rng, const LabParams& p, const Vector& nose, double now)
    {
        s = LabState{};
        LabRespawn(s, rng, p, nose, now);
        if (p.motion == kLabConstant) s.phaseEnd = now + std::max(p.hangSec, 1.f); // a moment to find it first
        s.runStart = now;
        s.runEnd = p.runSec > 0.f ? now + std::max(p.hangSec, 1.f) + p.runSec : 1e18; // the clock starts at the first launch
    }

    // One physics tick. car / prevCar: the car's axes now and a tick ago (its spin moves the
    // contact point); boost: holding boost; throttle: -1..1.
    inline void LabStep(LabState& s, std::mt19937& rng, const LabParams& p, const Basis& car, const Basis& prevCar,
                        const LabHitbox& hb, bool boost, float throttle, double now, float dt)
    {
        s.touchedNow = s.missedNow = s.resetNow = false;
        if (dt <= 0.f || s.phase == kLabDone) return;
        if (now >= s.runEnd)
        {
            s.phase = kLabDone;
            s.carVel = s.ballVel = V(0.f, 0.f, 0.f);
            return;
        }

        // ---- your car: boost (or air throttle) along the nose, capped at 2300 ----
        if (boost) s.carVel = Add(s.carVel, Scale(car.forward, kBoostAccel * dt));
        else if (std::fabs(throttle) > 0.01f) s.carVel = Add(s.carVel, Scale(car.forward, kAirThrottleAccel * std::clamp(throttle, -1.f, 1.f) * dt));
        if (p.speedLimit) CapSpeed(s.carVel, kCarTopSpeed);

        // ---- a waiting ball launches, the way its arrow showed ----
        if (s.phase == kLabHang && now >= s.phaseEnd)
        {
            s.ballVel = Scale(s.launchDir, s.launchSpeed);
            s.phase = kLabFlying;
            s.free = true;
            s.curveAxisUntil = 0.0;
        }

        // ---- air drag: the ball slowly loses speed (so a ball pushed just past your top speed
        // doesn't stay out of reach for ever, as in a match) ----
        if (s.phase != kLabHang && p.speedLimit) s.ballVel = Scale(s.ballVel, std::max(0.f, 1.f - kLabBallDrag * dt));

        // ---- a free ball curves: its direction turns steadily about an axis that changes every second ----
        if (s.free && p.curve > 0.f && s.phase == kLabFlying)
        {
            const float sp = Len(s.ballVel);
            if (sp > 1.f)
            {
                const Vector dir = Scale(s.ballVel, 1.f / sp);
                if (now >= s.curveAxisUntil || std::fabs(Dot(s.curveAxis, dir)) > 0.2f)
                {
                    if (p.steep < 89.f)
                    {
                        // Limited steepness: curve left or right only (turning about the vertical
                        // keeps how steep it travels).
                        s.curveAxis = std::uniform_int_distribution<int>(0, 1)(rng) ? V(0.f, 0.f, 1.f) : V(0.f, 0.f, -1.f);
                    }
                    else
                    {
                        Vector a = LabRandomDir(rng);
                        s.curveAxis = Norm(Sub(a, Scale(dir, Dot(a, dir)))); // at right angles to its path
                    }
                    s.curveAxisUntil = now + kLabCurveAxisSec;
                }
                if (std::fabs(Dot(s.curveAxis, dir)) < 0.999f) s.ballVel = Scale(RotateAbout(dir, s.curveAxis, p.curve * kDeg * dt), sp);
            }
        }

        // ---- move, and touch ----
        const Vector rel = LabRelVel(s);
        const Vector next = Add(s.ballPos, Scale(rel, dt));
        const bool contact = LabBoxDistance(next, car, hb) <= kLabBallRadius + 2.f;
        bool hit = false;
        if (contact && now - s.lastTouch >= kLabTouchGap)
        {
            const Vector cp = LabClosestOnBox(s.ballPos, car, hb);
            // How fast that point of the car is moving because the car is turning: where it
            // is now minus where the same point of the car was a tick ago.
            const Vector local = V(Dot(cp, car.forward), Dot(cp, car.right), Dot(cp, car.up));
            const Vector before = Add(Add(Scale(prevCar.forward, local.X), Scale(prevCar.right, local.Y)), Scale(prevCar.up, local.Z));
            const Vector spin = Scale(Sub(cp, before), 1.f / dt);
            const float closing = LabHit(s.ballVel, s.carVel, s.ballPos, cp, spin, car, p.speedLimit);
            if (closing > 0.f)
            {
                hit = true;
                s.lastClosing = closing;
                s.lastBallSpeed = Len(LabRelVel(s));
                s.lastTouch = now;
                ++s.touches;
                s.touchedNow = true;
                s.free = false;
                if (s.phase == kLabHang) s.phase = kLabFlying;
                if (p.motion == kLabRespawn) { s.phase = kLabTouched; s.phaseEnd = now + kLabTouchedSec; } // keep touching it to keep it
                // A flip reset: the ball on the underside, where the wheels are (all four touching).
                const Vector rel0 = Sub(cp, Add(Add(Scale(car.forward, hb.offset.X), Scale(car.right, hb.offset.Y)), Scale(car.up, hb.offset.Z)));
                const float lx = Dot(rel0, car.forward), ly = Dot(rel0, car.right), lz = Dot(rel0, car.up);
                if (lz <= -hb.half.Z + 0.5f && std::fabs(lx) <= hb.half.X * 0.8f && std::fabs(ly) <= hb.half.Y * 0.9f)
                {
                    ++s.resets;
                    s.resetNow = true;
                    s.flipReady = true;
                }
            }
        }
        if (!hit) s.ballPos = next;
        // Never inside the car (turning the car into the ball, or a touch that can't count
        // yet): push it out to the surface, so the game's own car never hits it.
        for (int i = 0; i < 3; ++i)
        {
            const Vector cp = LabClosestOnBox(s.ballPos, car, hb);
            const float bd = Len(Sub(s.ballPos, cp));
            if (bd >= kLabBallRadius + 2.f) break;
            Vector out = bd > 0.5f ? Scale(Sub(s.ballPos, cp), 1.f / bd) : Norm(s.ballPos);
            s.ballPos = Add(cp, Scale(out, kLabBallRadius + 2.f));
        }

        // ---- Respawn: a touched ball is replaced shortly after ----
        if (s.phase == kLabTouched && now >= s.phaseEnd)
        {
            LabRespawn(s, rng, p, car.forward, now);
            return;
        }

        // ---- the edge of the bubble, and the floor ----
        const Vector relNow = LabRelVel(s);
        const float r = Len(s.ballPos);
        const bool outEdge = r >= p.bubble && Dot(s.ballPos, relNow) > 0.f;
        const bool outFloor = s.ballPos.Z <= -p.floor && relNow.Z < 0.f;
        if (outEdge || outFloor)
        {
            if (p.motion == kLabRespawn)
            {
                if (s.phase == kLabFlying) { ++s.misses; s.missedNow = true; } // it got away untouched
                LabRespawn(s, rng, p, car.forward, now);
            }
            else if (now - s.lastBounce >= kLabBounceGap)
            {
                // A fresh start, like a new ball that doesn't stop: it comes back at the set speed
                // and your own speed resets (how you were moving relative to it is replaced either
                // way; this keeps you from being stuck at the speed cap). It can't be lost.
                if (outEdge) s.ballPos = Scale(s.ballPos, p.bubble / r);
                if (s.ballPos.Z < -p.floor) s.ballPos.Z = -p.floor;
                s.carVel = V(0.f, 0.f, 0.f);
                s.ballVel = LabPickLaunch(rng, s.ballPos, p, LabNextSpeed(rng, p), kLabReactSec);
                s.flipReady = true;
                if (s.phase == kLabHang) s.phase = kLabFlying;
                s.free = true;
                s.curveAxisUntil = 0.0;
                s.lastBounce = now;
                ++s.bounces;
            }
            else
            {
                // just bounced: it slides along the edge or the floor
                if (outEdge) s.ballPos = Scale(s.ballPos, p.bubble / r);
                if (s.ballPos.Z < -p.floor) s.ballPos.Z = -p.floor;
            }
        }
    }
}
