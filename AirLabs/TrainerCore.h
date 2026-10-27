#pragma once
// AirLabs core: geometry, Rocket League air physics, presets, ball paths,
// target spawning and scoring. Nothing in this file talks to the game, so it can be
// tested on its own.
//
// Before including this file a `Vector` type with float members X, Y, Z must exist
// (the BakkesMod SDK's Vector, or a small stand-in for tests).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <string_view>

namespace airlabs
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kDeg = kPi / 180.f;

    // ------------------------------------------------------------------------------------
    // Rocket League air control, community-measured (smish.dev/rocket_league/aerial_control).
    // Holding stick deflection u on pitch or yaw:  spin acceleration = T*u - D*(1-|u|)*spin.
    // The drag term fades out as you push the stick further, and is gone at full stick.
    // ------------------------------------------------------------------------------------
    constexpr float kPitchTorque = 12.14599781908070f; // rad/s^2 at full stick
    constexpr float kYawTorque   = 8.91962804287785f;
    constexpr float kPitchDrag   = 2.798194258050845f; // 1/s
    constexpr float kYawDrag     = 1.886491900437232f;
    constexpr float kMaxSpin     = 5.5f;               // rad/s, the game's cap on how fast a car spins
    constexpr float kMaxSpinDeg  = kMaxSpin / kDeg;    // ~315 deg/s

    constexpr float kMaxBallSpeed = 90.f;              // deg/s, the fastest any mode moves the ball

    // Par time for a flick = human allowance + factor x the physically fastest possible turn.
    // Par sets the points scale: 1000 points at par, up to 2000 for twice as fast.
    constexpr float kParReaction = 0.25f;
    constexpr float kParFactor   = 1.25f;

    // ------------------------------------------------------------------------------------
    // Vector helpers
    // ------------------------------------------------------------------------------------
    inline Vector V(float x, float y, float z) { return Vector{ x, y, z }; }
    inline Vector Add(const Vector& a, const Vector& b) { return V(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
    inline Vector Sub(const Vector& a, const Vector& b) { return V(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
    inline Vector Scale(const Vector& a, float s) { return V(a.X * s, a.Y * s, a.Z * s); }
    inline float Dot(const Vector& a, const Vector& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
    inline float Len(const Vector& a) { return std::sqrt(Dot(a, a)); }
    inline Vector Cross(const Vector& a, const Vector& b)
    {
        return V(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
    }
    inline Vector Norm(const Vector& a)
    {
        float l = Len(a);
        return l > 1e-6f ? Scale(a, 1.f / l) : V(1.f, 0.f, 0.f);
    }
    inline float AngleDeg(const Vector& a, const Vector& b)
    {
        float d = std::clamp(Dot(Norm(a), Norm(b)), -1.f, 1.f);
        return std::acos(d) / kDeg;
    }
    // Rotate v about the unit axis k by `rad` radians (Rodrigues' formula).
    inline Vector RotateAbout(const Vector& v, const Vector& k, float rad)
    {
        float c = std::cos(rad), s = std::sin(rad);
        return Add(Add(Scale(v, c), Scale(Cross(k, v), s)), Scale(k, Dot(k, v) * (1.f - c)));
    }
    // Signed angle (deg) that turns a onto b around the axis k, in RotateAbout's direction.
    inline float SignedAngleDeg(const Vector& a, const Vector& b, const Vector& k)
    {
        return std::atan2(Dot(k, Cross(a, b)), Dot(a, b)) / kDeg;
    }
    // How high above the horizon a direction from the car points, in degrees.
    inline float ElevationDeg(const Vector& d) { return std::asin(std::clamp(d.Z, -1.f, 1.f)) / kDeg; }

    struct Basis { Vector forward, right, up; };

    // Unreal rotator (65536 units per turn) to the car's axes. X forward, Y right, Z up.
    inline Basis BasisFromRotator(int pitchU, int yawU, int rollU)
    {
        const float k = kPi / 32768.f;
        float p = pitchU * k, y = yawU * k, r = rollU * k;
        float cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y), cr = std::cos(r), sr = std::sin(r);
        Basis b;
        b.forward = V(cp * cy, cp * sy, sp);
        b.right   = V(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp);
        b.up      = V(-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp);
        return b;
    }

    // Aim 0 = the nose, aim 1 = the wheels (the underside).
    inline Vector AimVec(const Basis& car, int aim) { return aim == 1 ? Scale(car.up, -1.f) : car.forward; }

    // ------------------------------------------------------------------------------------
    // Physics helpers
    // ------------------------------------------------------------------------------------

    // Fastest possible turn of `deg` degrees: full stick (which switches the drag off),
    // accelerate, then brake at the same rate, cruising at the spin cap if the turn is big
    // enough to reach it. Uses pitch, the quickest axis that moves the nose.
    inline float FastestTurnSec(float deg)
    {
        float th = std::max(0.f, deg) * kDeg;
        float peak = std::sqrt(kPitchTorque * th);
        if (peak <= kMaxSpin) return 2.f * std::sqrt(th / kPitchTorque);
        return th / kMaxSpin + kMaxSpin / kPitchTorque;
    }
    inline float ParSec(float deg) { return kParReaction + kParFactor * FastestTurnSec(deg); }

    // Stick deflection (0..1) that holds a steady spin:  u = D*w / (T + D*w)
    inline float StickForSpin(float degPerSec, bool pitch)
    {
        float w = std::fabs(degPerSec) * kDeg;
        float T = pitch ? kPitchTorque : kYawTorque, D = pitch ? kPitchDrag : kYawDrag;
        return D * w / (T + D * w);
    }
    inline float PctOfMaxSpin(float degPerSec) { return 100.f * degPerSec / kMaxSpinDeg; }

    // ------------------------------------------------------------------------------------
    // Ball height range, relative to the ground (degrees above/below the horizon).
    // ------------------------------------------------------------------------------------
    struct Band
    {
        float minDeg = -90.f, maxDeg = 90.f;
        bool Full() const { return minDeg <= -89.9f && maxDeg >= 89.9f; }
        bool Contains(const Vector& d) const
        {
            float e = ElevationDeg(d);
            return e >= minDeg - 0.05f && e <= maxDeg + 0.05f;
        }
    };
    constexpr Band kBandAny{ -90.f, 90.f };
    constexpr Band kBandAbove{ 20.f, 90.f };
    constexpr Band kBandLevel{ -15.f, 15.f };
    constexpr Band kBandBelow{ -90.f, -20.f };

    // The same compass direction as d, at a different height.
    inline Vector WithElevation(const Vector& d, float elevDeg, const Vector& fallback)
    {
        Vector hor = V(d.X, d.Y, 0.f);
        if (Len(hor) < 1e-4f) hor = V(fallback.X, fallback.Y, 0.f);
        if (Len(hor) < 1e-4f) hor = V(1.f, 0.f, 0.f);
        hor = Norm(hor);
        float e = elevDeg * kDeg;
        return Add(Scale(hor, std::cos(e)), V(0.f, 0.f, std::sin(e)));
    }
    inline Vector ClampToBand(const Vector& d, const Band& b, const Vector& fallback)
    {
        float e = ElevationDeg(d);
        if (e > b.maxDeg) return WithElevation(d, b.maxDeg, fallback);
        if (e < b.minDeg) return WithElevation(d, b.minDeg, fallback);
        return d;
    }

    // Every round starts the same way: the start ball sits between the car and the goal it
    // attacks (+Y for blue, -Y for orange), and the car is level but turned 45 degrees away
    // from it, so you make a deliberate turn onto the ball to start (it's still on screen).
    // Height ranges still apply (Flip Reset's ball is above you), so the ball is lifted or
    // lowered into the range but stays on the goal side.
    constexpr int kYawQuarter = 16384;      // 90 degrees in Unreal rotation units
    constexpr int kStartYawOffset = 8192;   // 45 degrees: how far the car starts turned away from the ball
    inline int GoalYaw(int goalSign) { return goalSign >= 0 ? kYawQuarter : -kYawQuarter; }
    inline int StartYaw(int goalSign) { return GoalYaw(goalSign) + kStartYawOffset; }
    inline Vector GoalStartDir(int goalSign, const Band& band)
    {
        Vector g = V(0.f, goalSign >= 0 ? 1.f : -1.f, 0.f);
        return Norm(ClampToBand(g, band, g));
    }

    // A start ball a little above any given aim, inside the height range (used by the tests'
    // path simulations, which start from random orientations).
    constexpr float kStartOffsetDeg = 20.f;
    inline Vector StartBallDir(const Basis& car, int aim, const Band& band)
    {
        Vector a = AimVec(car, aim);
        Vector up = V(0.f, 0.f, 1.f);
        if (std::fabs(Dot(a, up)) > 0.97f) up = aim == 1 ? car.forward : car.up; // aiming straight up/down
        Vector t = Norm(Sub(up, Scale(a, Dot(up, a))));
        float r = kStartOffsetDeg * kDeg;
        Vector d = Norm(Add(Scale(a, std::cos(r)), Scale(t, std::sin(r))));
        return Norm(ClampToBand(d, band, car.forward));
    }

    // ------------------------------------------------------------------------------------
    // Screen directions (driver's view) used by the direction picker and the grid.
    // Eight 45-degree slices around the nose, plus "behind" (more than 135 degrees away).
    // ------------------------------------------------------------------------------------
    enum Region { kRight = 0, kUpRight, kUp, kUpLeft, kLeft, kDownLeft, kDown, kDownRight, kBehind, kRegionCount };
    constexpr unsigned kAllRegions = (1u << kRegionCount) - 1u;

    inline int ClassifyRegion(const Basis& car, const Vector& dir)
    {
        float f = std::clamp(Dot(dir, car.forward), -1.f, 1.f);
        if (std::acos(f) / kDeg > 135.f) return kBehind;
        float phi = std::atan2(Dot(dir, car.up), Dot(dir, car.right)) / kDeg; // 0 = right, 90 = up
        if (phi < 0.f) phi += 360.f;
        return static_cast<int>(std::floor((phi + 22.5f) / 45.f)) % 8;
    }

    // How much of the sky a region covers inside the allowed turn range. Picking regions in
    // proportion to this spreads targets evenly instead of bunching them at a region edge.
    inline float RegionArea(int region, float lo, float hi, bool noseAim)
    {
        float tLo = region == kBehind ? 135.f : 0.f, tHi = region == kBehind ? 180.f : 135.f;
        if (noseAim) { tLo = std::max(tLo, lo); tHi = std::min(tHi, hi); }
        if (tHi <= tLo) return 0.f;
        float area = std::cos(tLo * kDeg) - std::cos(tHi * kDeg);
        return region == kBehind ? area : area / 8.f;
    }

    // A random direction inside a region that respects the turn range and height range.
    inline bool SampleInRegion(std::mt19937& rng, int region, const Basis& car, int aim, float lo, float hi,
                               const Band& band, Vector& out)
    {
        float tLo = region == kBehind ? 135.f : 0.f, tHi = region == kBehind ? 180.f : 135.f;
        const bool noseAim = aim == 0;
        if (noseAim)
        {
            // With nose aim, the turn needed is exactly the angle from the nose.
            tLo = std::max(tLo, lo);
            tHi = std::min(tHi, hi);
            if (tLo > tHi) return false;
        }
        // cos(angle) uniform = directions spread evenly over the sphere.
        std::uniform_real_distribution<float> uc(std::cos(tHi * kDeg), std::cos(tLo * kDeg));
        float phiC = region == kBehind ? 0.f : region * 45.f;
        float spread = region == kBehind ? 180.f : 22.5f;
        std::uniform_real_distribution<float> up((phiC - spread) * kDeg, (phiC + spread) * kDeg);
        Vector a = AimVec(car, aim);
        for (int i = 0; i < 400; ++i)
        {
            float c = uc(rng);
            float s = std::sqrt(std::max(0.f, 1.f - c * c));
            float ph = up(rng);
            Vector d = Norm(Add(Scale(car.forward, c),
                                Scale(Add(Scale(car.right, std::cos(ph)), Scale(car.up, std::sin(ph))), s)));
            if (!band.Contains(d)) continue;
            if (noseAim) { out = d; return true; }
            float t = AngleDeg(a, d);
            if (t >= lo && t <= hi) { out = d; return true; }
        }
        return false;
    }

    // Fallback: anywhere inside the height range, preferring the turn range.
    inline Vector SampleInBand(std::mt19937& rng, const Band& band, const Vector& aim, float lo, float hi, bool& turnOk)
    {
        std::uniform_real_distribution<float> uz(std::sin(band.minDeg * kDeg), std::sin(band.maxDeg * kDeg));
        std::uniform_real_distribution<float> ut(0.f, 2.f * kPi);
        Vector d = V(1.f, 0.f, 0.f);
        turnOk = false;
        for (int i = 0; i < 400; ++i)
        {
            float z = uz(rng), t = ut(rng);
            float r = std::sqrt(std::max(0.f, 1.f - z * z));
            d = V(r * std::cos(t), r * std::sin(t), z);
            float a = AngleDeg(aim, d);
            if (a >= lo && a <= hi) { turnOk = true; break; }
        }
        return d;
    }

    template <std::size_t N>
    inline int PickWeighted(std::mt19937& rng, const std::array<double, N>& w)
    {
        double total = 0.0;
        for (double x : w) total += std::max(0.0, x);
        if (total <= 0.0) return -1;
        std::uniform_real_distribution<double> u(0.0, total);
        double pick = u(rng);
        int last = -1;
        for (std::size_t i = 0; i < N; ++i)
        {
            if (w[i] <= 0.0) continue;
            last = static_cast<int>(i);
            pick -= w[i];
            if (pick <= 0.0) return last;
        }
        return last;
    }

    // Pick a flick target: a region (weighted by its share of the sky, times any extra weight
    // such as weak-spot mode), then a point inside it. Falls back to "anywhere in the height
    // range" when nothing selected fits the turn range from where you're facing.
    struct SpawnResult { Vector dir; int region; bool fallback; };
    inline SpawnResult SpawnDirection(std::mt19937& rng, const Basis& car, int aim, float minTurn, float maxTurn,
                                      const Band& band, unsigned dirMask, const std::array<double, kRegionCount>& extraWeight)
    {
        float lo = std::min(minTurn, maxTurn), hi = std::max(minTurn, maxTurn);
        if ((dirMask & kAllRegions) == 0u) dirMask = kAllRegions;
        std::array<double, kRegionCount> w{};
        for (int r = 0; r < kRegionCount; ++r)
            w[r] = ((dirMask >> r) & 1u) ? RegionArea(r, lo, hi, aim == 0) * extraWeight[r] : 0.0;

        SpawnResult res{ AimVec(car, aim), -1, false };
        for (int attempt = 0; attempt < kRegionCount; ++attempt)
        {
            int r = PickWeighted(rng, w);
            if (r < 0) break;
            if (SampleInRegion(rng, r, car, aim, lo, hi, band, res.dir)) { res.region = r; return res; }
            w[r] = 0.0;
        }
        bool turnOk = false;
        res.dir = SampleInBand(rng, band, AimVec(car, aim), lo, hi, turnOk);
        res.region = ClassifyRegion(car, res.dir);
        res.fallback = true;
        return res;
    }

    // ------------------------------------------------------------------------------------
    // Ball paths for Track, Dynamic and Hybrid. The ball lives on a sphere around the car
    // with a position and a heading (which way it's moving across the sky). Direction
    // changes rotate the heading by a random angle of up to maxTurn, relative to the ball's
    // own motion. The plugin seeds every run randomly; a fixed seed replays the same path.
    // ------------------------------------------------------------------------------------
    struct PathParams
    {
        bool  dynamic = false;
        float speed = 30.f;             // constant-speed modes, deg/s
        float vMin = 15.f, vMax = 45.f; // dynamic mode range, deg/s
        float changeEvery = 1.f;        // seconds between direction changes (constant speed)
        float maxTurn = 90.f;           // largest heading change at a direction change, deg
        Band  band;                     // keep the ball inside this height range
        unsigned dirMask = 0;           // Custom only: screen directions to head toward (bits 0-7)
    };

    struct PathState
    {
        Vector pos{ 1.f, 0.f, 0.f };     // direction from the car to the ball
        Vector heading{ 0.f, 0.f, 1.f }; // direction the ball is moving across the sky
        float  speed = 30.f, targetSpeed = 30.f;
        double nextChange = 0.0, nextSpeedChange = 0.0;
        float  turnLeft = 0.f, turnRate = 0.f;
    };

    constexpr float kTurnDuration  = 0.35f; // seconds to carry out a heading change
    constexpr float kEdgeSteerRate = 240.f; // deg/s heading correction at a height-range edge
    constexpr float kDynamicAccel  = 60.f;  // deg/s^2, how quickly dynamic speed changes

    // Dynamic mode links the change interval to the current speed, using the same ladder as
    // the Track presets: 0.7 s at 15 deg/s up to 2.0 s at 90 deg/s.
    inline float ChangeIntervalForSpeed(float v)
    {
        v = std::clamp(v, 15.f, 90.f);
        return 0.7f + (v - 15.f) / 75.f * 1.3f;
    }
    inline double ChangeInterval(const PathParams& p, float speed)
    {
        return p.dynamic ? ChangeIntervalForSpeed(speed) : std::max(0.1f, p.changeEvery);
    }

    inline float PickTurn(std::mt19937& rng, const PathState& s, const Basis& car, int aim, const PathParams& p)
    {
        float maxT = std::clamp(p.maxTurn, 0.f, 180.f);
        std::uniform_real_distribution<float> u(-maxT, maxT);
        float turn = u(rng); // always drawn, so the random sequence stays the same with or without a filter
        unsigned m = p.dirMask & 0xFFu;
        if (m == 0u || m == 0xFFu) return turn;

        // Custom direction filter: head toward one of the chosen screen directions.
        int sectors[8], n = 0;
        for (int r = 0; r < 8; ++r)
            if (m & (1u << r)) sectors[n++] = r;
        std::uniform_int_distribution<int> pick(0, n - 1);
        std::uniform_real_distribution<float> jitter(-22.5f, 22.5f);
        float phi = (sectors[pick(rng)] * 45.f + jitter(rng)) * kDeg;
        Vector screenUp = aim == 0 ? car.up : car.forward;
        Vector want = Add(Scale(car.right, std::cos(phi)), Scale(screenUp, std::sin(phi)));
        want = Sub(want, Scale(s.pos, Dot(want, s.pos)));
        if (Len(want) < 0.1f) return turn;
        return std::clamp(SignedAngleDeg(s.heading, Norm(want), s.pos), -maxT, maxT);
    }

    inline void PathStart(PathState& s, std::mt19937& rng, const Vector& startDir, const Basis& car, const PathParams& p, double now)
    {
        s.pos = Norm(startDir);
        // Initial heading: a random direction across the sky, measured from "screen up".
        Vector ref = Sub(car.up, Scale(s.pos, Dot(car.up, s.pos)));
        if (Len(ref) < 0.1f) ref = Sub(car.forward, Scale(s.pos, Dot(car.forward, s.pos)));
        std::uniform_real_distribution<float> angle(-180.f, 180.f);
        s.heading = Norm(RotateAbout(Norm(ref), s.pos, angle(rng) * kDeg));

        std::uniform_real_distribution<float> spd(std::min(p.vMin, p.vMax), std::max(p.vMin, p.vMax));
        float first = spd(rng); // drawn in every mode so the sequence stays aligned
        s.speed = s.targetSpeed = std::min(kMaxBallSpeed, p.dynamic ? first : p.speed);

        std::uniform_real_distribution<double> gap(1.0, 3.0), jitter(0.5, 1.5);
        s.nextSpeedChange = now + gap(rng);
        s.nextChange = now + ChangeInterval(p, s.speed) * jitter(rng);
        s.turnLeft = 0.f;
        s.turnRate = 0.f;
    }

    // Near an edge of the height range and heading out of it: curve back along the edge.
    inline void SteerIntoBand(PathState& s, const Band& b, float dt)
    {
        if (b.Full() || std::fabs(s.pos.Z) > 0.999f) return;
        float e = ElevationDeg(s.pos);
        Vector upT = Norm(Sub(V(0.f, 0.f, 1.f), Scale(s.pos, s.pos.Z))); // "higher" along the sphere
        float climb = Dot(s.heading, upT);
        float margin = std::min(10.f, (b.maxDeg - b.minDeg) * 0.25f);
        bool top = b.maxDeg < 89.9f && e > b.maxDeg - margin && climb > 0.f;
        bool bottom = b.minDeg > -89.9f && e < b.minDeg + margin && climb < 0.f;
        if (!top && !bottom) return;

        Vector flat = Sub(s.heading, Scale(upT, climb));
        if (Len(flat) < 1e-3f) flat = Cross(s.pos, upT);
        Vector want = Norm(Add(Norm(flat), Scale(upT, top ? -0.35f : 0.35f))); // slightly back inside
        float ang = SignedAngleDeg(s.heading, want, s.pos);
        float step = std::clamp(ang, -kEdgeSteerRate * dt, kEdgeSteerRate * dt);
        s.heading = Norm(RotateAbout(s.heading, s.pos, step * kDeg));
        s.turnLeft = 0.f; // the edge overrides the current random turn
    }

    // Hard limit: if the ball ends up outside the height range, put it back on the edge.
    inline void ClampIntoBand(PathState& s, const Band& b)
    {
        if (b.Full()) return;
        float e = ElevationDeg(s.pos);
        if (e <= b.maxDeg && e >= b.minDeg) return;
        s.pos = WithElevation(s.pos, std::clamp(e, b.minDeg, b.maxDeg), s.heading);
        Vector h = Sub(s.heading, Scale(s.pos, Dot(s.heading, s.pos)));
        if (std::fabs(s.pos.Z) < 0.999f)
        {
            Vector upT = Norm(Sub(V(0.f, 0.f, 1.f), Scale(s.pos, s.pos.Z)));
            float climb = Dot(h, upT);
            if ((e > b.maxDeg && climb > 0.f) || (e < b.minDeg && climb < 0.f)) h = Sub(h, Scale(upT, climb));
        }
        if (Len(h) < 1e-3f) h = Cross(s.pos, V(0.f, 0.f, 1.f));
        s.heading = Norm(h);
    }

    inline void PathAdvance(PathState& s, std::mt19937& rng, const Basis& car, int aim, const PathParams& p, double now, float dt)
    {
        // 1. Speed: constant, or drifting smoothly toward a new random target (dynamic).
        if (p.dynamic)
        {
            if (now >= s.nextSpeedChange)
            {
                std::uniform_real_distribution<float> spd(std::min(p.vMin, p.vMax), std::max(p.vMin, p.vMax));
                std::uniform_real_distribution<double> gap(1.0, 3.0);
                s.targetSpeed = spd(rng);
                s.nextSpeedChange = now + gap(rng);
            }
            float step = kDynamicAccel * dt;
            s.speed += std::clamp(s.targetSpeed - s.speed, -step, step);
        }
        else
        {
            s.speed = p.speed;
        }
        s.speed = std::clamp(s.speed, 0.f, kMaxBallSpeed);

        // 2. Direction changes: up to maxTurn degrees, carried out over kTurnDuration.
        if (now >= s.nextChange)
        {
            std::uniform_real_distribution<double> jitter(0.5, 1.5);
            s.nextChange = now + ChangeInterval(p, s.speed) * jitter(rng);
            s.turnLeft = PickTurn(rng, s, car, aim, p);
            s.turnRate = std::max(std::fabs(s.turnLeft) / kTurnDuration, 1.f);
        }
        if (s.turnLeft != 0.f)
        {
            float step = std::min(std::fabs(s.turnLeft), s.turnRate * dt);
            if (s.turnLeft < 0.f) step = -step;
            s.heading = Norm(RotateAbout(s.heading, s.pos, step * kDeg));
            s.turnLeft -= step;
            if (std::fabs(s.turnLeft) < 1e-3f) s.turnLeft = 0.f;
        }

        // 3. Stay inside the height range.
        SteerIntoBand(s, p.band, dt);

        // 4. Move along the sphere (a great circle) at the current speed.
        float a = s.speed * dt * kDeg;
        Vector pos = Add(Scale(s.pos, std::cos(a)), Scale(s.heading, std::sin(a)));
        Vector head = Sub(Scale(s.heading, std::cos(a)), Scale(s.pos, std::sin(a)));
        s.pos = Norm(pos);
        s.heading = Norm(Sub(head, Scale(s.pos, Dot(head, s.pos))));
        ClampIntoBand(s, p.band);
    }

    // Speed bands for the Dynamic speed curve: 15-30, 30-45, 45-60, 60-75, 75-90 deg/s.
    constexpr int kSpeedBands = 5;
    inline int SpeedBand(float v) { return std::clamp(static_cast<int>((v - 15.f) / 15.f), 0, kSpeedBands - 1); }

    // ------------------------------------------------------------------------------------
    // Chase: one ball for the whole run. It waits until you point at it, then dashes off in a
    // random direction for a random time (straight across the sky, at a fixed speed or a
    // random one per dash), stops, and waits for you again.
    // ------------------------------------------------------------------------------------
    struct ChaseParams
    {
        bool  randomSpeed = false;
        float speed = 60.f, vMin = 20.f, vMax = 90.f;                // deg/s
        float moveMin = 0.5f, moveMax = 1.5f;                        // s per dash
        Band  band;
    };
    struct ChaseState
    {
        PathState path;
        bool   moving = false;
        double stopAt = 0.0, stoppedAt = 0.0;
        float  dashSpeed = 0.f;
        int    dashes = 0;
    };
    inline PathParams ChasePath(const ChaseParams& p, float speed)
    {
        PathParams pp;
        pp.dynamic = false;
        pp.speed = speed;
        pp.changeEvery = 1e9f; // straight lines: no direction changes during a dash
        pp.maxTurn = 0.f;
        pp.band = p.band;
        return pp;
    }
    // Hybrid: after the flick the ball takes off and holds that direction for the whole track
    // (a straight line round the sky). Dynamic presets still speed up and slow down.
    inline PathParams HybridPath(PathParams p)
    {
        p.maxTurn = 0.f;
        if (!p.dynamic) p.changeEvery = 1e9f;
        return p;
    }
    // Start a dash from wherever the ball is now.
    inline void ChaseLaunch(ChaseState& c, std::mt19937& rng, const Basis& car, const ChaseParams& p, double now)
    {
        std::uniform_real_distribution<float> spd(std::min(p.vMin, p.vMax), std::max(p.vMin, p.vMax));
        std::uniform_real_distribution<float> dur(std::min(p.moveMin, p.moveMax), std::max(p.moveMin, p.moveMax));
        float v = spd(rng); // always drawn, so the random sequence is the same in every preset
        float d = dur(rng);
        c.dashSpeed = std::min(kMaxBallSpeed, p.randomSpeed ? v : p.speed);
        PathStart(c.path, rng, c.path.pos, car, ChasePath(p, c.dashSpeed), now); // random heading
        c.path.speed = c.path.targetSpeed = c.dashSpeed;
        c.moving = true;
        c.stopAt = now + d;
        ++c.dashes;
    }
    inline void ChaseAdvance(ChaseState& c, std::mt19937& rng, const Basis& car, const ChaseParams& p, double now, float dt)
    {
        if (!c.moving) return;
        PathAdvance(c.path, rng, car, 0, ChasePath(p, c.dashSpeed), now, dt);
        if (now >= c.stopAt) { c.moving = false; c.stoppedAt = now; }
    }

    // ------------------------------------------------------------------------------------
    // Scoring
    // ------------------------------------------------------------------------------------

    // Tracking: full points inside the target, fading to zero at twice its size.
    inline float TrackCredit(float errDeg, float targetDeg)
    {
        if (errDeg <= targetDeg) return 1.f;
        if (errDeg >= 2.f * targetDeg) return 0.f;
        return (2.f * targetDeg - errDeg) / targetDeg;
    }

    // Flick: 1000 points at par time, up to 2000 for twice as fast.
    inline float FlickPoints(double seconds, float turnDeg)
    {
        double t = std::max(0.05, seconds);
        return 1000.f * static_cast<float>(std::clamp(ParSec(turnDeg) / t, 0.0, 2.0));
    }

    // Time scaled to what it would be for a 90-degree turn (for the direction grid).
    inline float NormalisedFlickTime(double seconds, float turnDeg)
    {
        return static_cast<float>(seconds * ParSec(90.f) / ParSec(std::max(5.f, turnDeg)));
    }

    // Average par over a turn range, weighted the way targets are spread (by sky area).
    inline float AvgPar(float lo, float hi)
    {
        double num = 0.0, den = 0.0;
        for (int i = 0; i < 64; ++i)
        {
            float th = lo + (hi - lo) * (i + 0.5f) / 64.f;
            double w = std::sin(th * kDeg);
            num += w * ParSec(th);
            den += w;
        }
        return den > 0.0 ? static_cast<float>(num / den) : ParSec(lo);
    }

    // ------------------------------------------------------------------------------------
    // Modes and presets
    // ------------------------------------------------------------------------------------
    enum Mode { kFlick = 0, kTrack = 1, kHybrid = 2, kChase = 3, kModeCount = 4 };

    // Shared by every preset run, so scores can be compared between players.
    // (The target is always the ball itself: see BallTargetDeg.)
    constexpr float kPresetHoldSec   = 0.2f;
    constexpr float kPresetTimeout   = 5.f;     // s, Flick and Hybrid: a target you can't reach in time is a miss
    constexpr float kChaseTimeout    = 7.5f;    // s, Chase: catch the stopped ball within this or it dashes off
    constexpr float kNearDeg         = 25.f;    // deg, "near the ball": the laser and ring turn yellow inside this
    constexpr float kChaseHoldSec    = 0.15f;   // s, Chase: keep your aim on the stopped ball this long to catch it
    constexpr float kMissPenalty     = 500.f;   // points lost for each missed Flick / Hybrid target (score never drops below 0)
    constexpr float kPresetDelaySec  = 0.02f;   // s between one target and the next
    constexpr float kPresetRunSec    = 60.f;
    constexpr float kHybridTrackSec  = 4.f;     // s of tracking after each Hybrid flick (3 s before 2.4.0)
    constexpr float kHybridFlickMin  = 30.f, kHybridFlickMax = 180.f;
    constexpr float kDynamicMaxTurn  = 90.f;
    constexpr float kChaseMoveMin    = 0.5f, kChaseMoveMax = 1.5f; // s, how long each Chase dash lasts
    constexpr float kReadyHoldSec    = 1.5f;    // point at the start ball this long to begin a run
    constexpr float kReadyLockSec    = 1.0f;    // after a run ends, the next start ball can't be locked for this long
    constexpr unsigned kSeedVersion  = 2;       // bump when a change alters preset target sequences

    // The car is pinned at the exact centre of the arena's height. From there the ball can be
    // 929 uu straight up before touching the roof (2044 / 2 - ball radius), and the same down
    // to the floor, so 920 uu keeps every direction clear.
    constexpr float kArenaCentreZ    = 1022.f;
    constexpr float kMaxBallDistance = 920.f;

    // Target size: the ball itself. Your aim is on the ball when the angle between your aim and
    // the ball's centre is at most the ball's angular radius, asin(radius / distance), so the
    // target is exactly the ball at any distance. The radius used is 94 uu (the ball is 92.75)
    // so the ring sits just outside the ball's edge: 7.7 deg at 700 uu, 7.2 at 750, 5.9 at 920.
    constexpr float kTargetBallRadius = 94.f;
    inline float BallTargetDeg(float distance)
    {
        float d = std::max(distance, kTargetBallRadius + 1.f);
        return std::asin(kTargetBallRadius / d) / kDeg;
    }

    // Range: how far away the ball is in a preset run. Each range is its own competition with
    // its own records, because a closer ball is a bigger target. (Flip Reset always uses its
    // own close distance.)
    enum Range { kShort = 0, kMedium = 1, kLong = 2, kRangeCount = 3 };
    constexpr float kRangeDistance[kRangeCount] = { 400.f, 600.f, 900.f };
    inline const char* RangeName(int r)
    {
        static const char* names[kRangeCount] = { "Short", "Medium", "Long" };
        return names[std::clamp(r, 0, kRangeCount - 1)];
    }
    inline float RangeDistance(int r) { return kRangeDistance[std::clamp(r, 0, kRangeCount - 1)]; }
    constexpr float kDefaultCustomDistance = 600.f; // uu, Custom's starting distance (Medium)

    struct FlickPreset
    {
        const char* name; const char* blurb; float minTurn, maxTurn; int aim; Band band;
        float fixedDistance = 0.f; // uu; 0 = the chosen range sets the distance
    };
    // Track and Hybrid presets: steady speed, or "Dynamic" (speed drifts between vMin and vMax).
    struct MovePreset
    {
        const char* name; const char* group; const char* blurb;
        bool dynamic; float speed, vMin, vMax, changeEvery, maxTurn;
    };

    inline constexpr FlickPreset kFlickPresets[] = {
        { "Small",      "Small corrections. You never get past about half of max spin, so it's all starting and stopping cleanly.", 15.f,  45.f, 0, kBandAny },
        { "Medium",     "Everyday flicks: a quick burst of spin and a well-timed brake.",                                         45.f,  90.f, 0, kBandAny },
        { "Big",        "Big turns close to full spin speed. Commit to full stick and brake early.",                              90.f, 135.f, 0, kBandAny },
        { "Turnaround", "Targets behind you. You'll hit the spin speed cap, so start braking well before you arrive.",           135.f, 180.f, 0, kBandAny },
        { "Mixed",      "Any size, never the same twice. The all-round flick test.",                                              30.f, 180.f, 0, kBandAny },
        { "Flip Reset", "Point your wheels at a ball above you: the setup for a flip reset. The ball is close, as it would be.",   30.f, 180.f, 1, kBandAbove, 350.f },
    };
    inline constexpr MovePreset kTrackPresets[] = {
        { "Precision", "", "About 5% stick. Tiny steady inputs and constant small corrections.",    false, 15.f, 0.f, 0.f, 0.7f, 90.f },
        { "Control",   "", "About 10% stick. Smooth, controlled tracking.",                         false, 30.f, 0.f, 0.f, 1.0f, 90.f },
        { "Standard",  "", "About 15% stick. The everyday tracking speed.",                          false, 45.f, 0.f, 0.f, 1.3f, 90.f },
        { "Fast",      "", "About 19% stick. Bigger, quicker adjustments.",                          false, 60.f, 0.f, 0.f, 1.6f, 90.f },
        { "Insane",    "", "About 26% stick. The fastest ball in the trainer.",                      false, 90.f, 0.f, 0.f, 2.0f, 60.f },
        { "Low",    "Dynamic", "The ball speeds up and slows down between 15 and 45 deg/s. Change how far you push the stick, not just which way.", true, 0.f, 15.f, 45.f, 0.f, kDynamicMaxTurn },
        { "Medium", "Dynamic", "Speed drifts between 30 and 60 deg/s.",                              true, 0.f, 30.f, 60.f, 0.f, kDynamicMaxTurn },
        { "High",   "Dynamic", "Speed drifts between 45 and 90 deg/s.",                              true, 0.f, 45.f, 90.f, 0.f, kDynamicMaxTurn },
        { "Full",   "Dynamic", "Speed drifts across the whole range, 15 to 90 deg/s.",               true, 0.f, 15.f, 90.f, 0.f, kDynamicMaxTurn },
    };
    inline constexpr MovePreset kHybridPresets[] = {
        { "Control", "", "Flick onto the ball, then it takes off in a straight line: track it for 4 seconds at 30 deg/s.",           false, 30.f, 0.f, 0.f, 1.0f, 90.f },
        { "Fast",    "", "Flick, then track at 60 deg/s.",                                          false, 60.f, 0.f, 0.f, 1.6f, 90.f },
        { "Insane",  "", "Flick, then track at 90 deg/s.",                                          false, 90.f, 0.f, 0.f, 2.0f, 60.f },
        { "Low",    "Dynamic", "Flick, then track a ball whose speed drifts between 15 and 45 deg/s.", true, 0.f, 15.f, 45.f, 0.f, kDynamicMaxTurn },
        { "Medium", "Dynamic", "Flick, then track at 30 to 60 deg/s.",                              true, 0.f, 30.f, 60.f, 0.f, kDynamicMaxTurn },
        { "High",   "Dynamic", "Flick, then track at 45 to 90 deg/s.",                              true, 0.f, 45.f, 90.f, 0.f, kDynamicMaxTurn },
        { "Full",   "Dynamic", "Flick, then track at anything from 15 to 90 deg/s.",                true, 0.f, 15.f, 90.f, 0.f, kDynamicMaxTurn },
    };
    // Chase: "dynamic" here means a new random speed for every dash.
    inline constexpr MovePreset kChasePresets[] = {
        { "Slow",   "", "Each dash goes at 30 deg/s. Follow it and be on it when it stops.",            false, 30.f, 0.f, 0.f, 0.f, 0.f },
        { "Medium", "", "Each dash goes at 60 deg/s.",                                                    false, 60.f, 0.f, 0.f, 0.f, 0.f },
        { "Fast",   "", "Each dash goes at 90 deg/s, the fastest ball in the trainer.",                   false, 90.f, 0.f, 0.f, 0.f, 0.f },
        { "Random", "", "Every dash picks a new speed between 20 and 90 deg/s: read it and commit.",      true,  0.f, 20.f, 90.f, 0.f, 0.f },
    };

    inline int PresetCount(int mode)
    {
        switch (mode)
        {
        case kFlick:  return static_cast<int>(std::size(kFlickPresets));
        case kTrack:  return static_cast<int>(std::size(kTrackPresets));
        case kHybrid: return static_cast<int>(std::size(kHybridPresets));
        case kChase:  return static_cast<int>(std::size(kChasePresets));
        default:      return 0;
        }
    }
    inline const char* ModeName(int mode)
    {
        static const char* names[kModeCount] = { "Flick", "Track", "Hybrid", "Chase" };
        return names[std::clamp(mode, 0, kModeCount - 1)];
    }
    inline const MovePreset* MovePresetFor(int mode, int preset)
    {
        if (preset < 0 || preset >= PresetCount(mode)) return nullptr;
        if (mode == kTrack) return &kTrackPresets[preset];
        if (mode == kHybrid) return &kHybridPresets[preset];
        if (mode == kChase) return &kChasePresets[preset];
        return nullptr;
    }
    // The preset index equal to PresetCount(mode) is "Custom".
    inline const char* PresetName(int mode, int preset)
    {
        if (preset < 0 || preset >= PresetCount(mode)) return "Custom";
        if (mode == kFlick) return kFlickPresets[preset].name;
        return MovePresetFor(mode, preset)->name;
    }
    // "Dynamic" for the Dynamic sub-presets, "" otherwise.
    inline const char* PresetGroup(int mode, int preset)
    {
        const MovePreset* p = MovePresetFor(mode, preset);
        return p ? p->group : "";
    }
    inline bool PresetIsDynamic(int mode, int preset)
    {
        const MovePreset* p = MovePresetFor(mode, preset);
        return p && p->dynamic;
    }
    // Whether a preset sits under the "Dynamic" button in the preset row (Track and Hybrid).
    inline bool PresetInDynamicGroup(int mode, int preset)
    {
        const MovePreset* p = MovePresetFor(mode, preset);
        return p && std::string_view(p->group) == "Dynamic";
    }
    inline const char* PresetBlurb(int mode, int preset)
    {
        if (preset < 0 || preset >= PresetCount(mode)) return "Your own settings. Custom runs don't count toward records until you save them as a preset.";
        if (mode == kFlick) return kFlickPresets[preset].blurb;
        return MovePresetFor(mode, preset)->blurb;
    }
    // Whether a preset's distance comes from the chosen range (every preset but Flip Reset).
    inline bool PresetUsesRange(int mode, int preset)
    {
        if (mode == kFlick && preset >= 0 && preset < PresetCount(kFlick)) return kFlickPresets[preset].fixedDistance <= 0.f;
        return true;
    }
    inline float PresetDistance(int mode, int preset, int range)
    {
        if (!PresetUsesRange(mode, preset)) return kFlickPresets[preset].fixedDistance;
        return RangeDistance(range);
    }
    // ------------------------------------------------------------------------------------
    // Rocket League's rank names, for players' real ranks on the leaderboard (AirLabs has no
    // ranks of its own any more: the leaderboard says how you compare). Index i is Rocket
    // League's tier i + 1: 0 = Bronze I .. 20 = Grand Champion III, 21 = Supersonic Legend.
    // (22 = Pro is left from the old AirLabs ranks; nothing uses it.)
    // ------------------------------------------------------------------------------------
    constexpr int kTierCount = 23;
    constexpr int kSSLTier = 21;
    inline const char* TierName(int i)
    {
        static const char* names[kTierCount] = {
            "Bronze I", "Bronze II", "Bronze III", "Silver I", "Silver II", "Silver III", "Gold I", "Gold II", "Gold III",
            "Platinum I", "Platinum II", "Platinum III", "Diamond I", "Diamond II", "Diamond III", "Champion I", "Champion II",
            "Champion III", "Grand Champion I", "Grand Champion II", "Grand Champion III", "Supersonic Legend", "Pro" };
        return names[std::clamp(i, 0, kTierCount - 1)];
    }
    // Short form for badges: B1 .. GC3, SSL.
    inline const char* TierShort(int i)
    {
        static const char* names[kTierCount] = { "B1", "B2", "B3", "S1", "S2", "S3", "G1", "G2", "G3", "P1", "P2", "P3",
                                                 "D1", "D2", "D3", "C1", "C2", "C3", "GC1", "GC2", "GC3", "SSL", "PRO" };
        return names[std::clamp(i, 0, kTierCount - 1)];
    }
    // Which of the ranks a division belongs to: 0 Bronze .. 6 Grand Champion, 7 SSL, 8 Pro.
    inline int TierRank(int i)
    {
        i = std::clamp(i, 0, kTierCount - 1);
        return i < kSSLTier ? i / 3 : i == kSSLTier ? 7 : 8;
    }

    // A repeatable seed per preset, for the tests. (The plugin itself seeds
    // every run randomly.)
    inline unsigned PresetSeed(int mode, int preset)
    {
        return 0x07AB0000u + static_cast<unsigned>(mode) * 64u + static_cast<unsigned>(preset) + kSeedVersion * 4096u;
    }

    // ------------------------------------------------------------------------------------
    // Key binds, for on-screen prompts ("PRESS O FOR THE MENU"). BakkesMod keeps them in
    // bakkesmod/cfg/binds.cfg, one per line:  bind O "airlabs_menu"  or  bind F6 "a; b"
    // Returns the first key bound to the command, as a player reads it ("O", "F5", "4"), or
    // "" if none is.
    // ------------------------------------------------------------------------------------
    inline std::string BoundKey(const std::string& cfg, const std::string& command)
    {
        auto trim = [](const std::string& s) {
            const char* junk = " \t\r\n\"";
            const std::size_t a = s.find_first_not_of(junk), b = s.find_last_not_of(junk);
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        static const char* kDigits[10] = { "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine" };
        std::size_t pos = 0;
        while (pos < cfg.size())
        {
            std::size_t end = cfg.find('\n', pos);
            if (end == std::string::npos) end = cfg.size();
            const std::string line = trim(cfg.substr(pos, end - pos));
            pos = end + 1;
            if (line.rfind("bind ", 0) != 0) continue;
            const std::size_t k0 = line.find_first_not_of(" \t", 5);
            const std::size_t k1 = k0 == std::string::npos ? k0 : line.find_first_of(" \t", k0);
            if (k1 == std::string::npos) continue;
            std::string key = line.substr(k0, k1 - k0);
            const std::string cmds = line.substr(k1);
            bool match = false;
            for (std::size_t c = 0; c <= cmds.size() && !match;)
            {
                std::size_t e = cmds.find(';', c);
                if (e == std::string::npos) e = cmds.size();
                match = trim(cmds.substr(c, e - c)) == command;
                c = e + 1;
            }
            if (!match) continue;
            for (int d = 0; d < 10; ++d)
                if (key == kDigits[d]) return std::string(1, static_cast<char>('0' + d));
            if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') key[0] = static_cast<char>(key[0] - 'a' + 'A');
            if (key == "SpaceBar") return "Space";
            return key;
        }
        return "";
    }
}
