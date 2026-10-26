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
    // Par only sets the points scale (1000 points at par); the tier marks are calibrated
    // separately, per preset and range (see Tiers).
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
    // Ranks, named after Rocket League's: Bronze I .. Grand Champion III (three divisions each),
    // Supersonic Legend, then Pro. Each preset and range has its own marks (see README, "How the
    // ranks were worked out", tools/tier_model.cpp and tools/tier_marks3.py). Each mark is the
    // score of a simulated player at the MMR where that division starts in Rocket League 2v2,
    // built from three reference players:
    //  - Bronze (200 MMR): ~7-8 s per target on Flick Mixed with no time limit.
    //  - GC1 (1497 MMR): fitted to a real GC1-level player's runs.
    //  - Pro (2600 MMR, where the best pros peak): that player at human limits. Its score is
    //    the Pro mark, the highest plausible score.
    // A rank value runs from 0 (Bronze I) in steps of 1/3 per division: GC1 = 6.0, SSL = 7.0,
    // Pro = 7.33.
    // ------------------------------------------------------------------------------------
    constexpr int kTierCount = 23; // Bronze I..III, Silver, Gold, Platinum, Diamond, Champion, Grand Champion I..III, Supersonic Legend, Pro
    constexpr int kSSLTier = 21;
    constexpr int kProTier = 22;   // the highest plausible human score: where the best pros would be
    inline const char* TierName(int i)
    {
        static const char* names[kTierCount] = {
            "Bronze I", "Bronze II", "Bronze III", "Silver I", "Silver II", "Silver III", "Gold I", "Gold II", "Gold III",
            "Platinum I", "Platinum II", "Platinum III", "Diamond I", "Diamond II", "Diamond III", "Champion I", "Champion II",
            "Champion III", "Grand Champion I", "Grand Champion II", "Grand Champion III", "Supersonic Legend", "Pro" };
        return names[std::clamp(i, 0, kTierCount - 1)];
    }
    // Short form for buttons: B1 .. GC3, SSL, PRO.
    inline const char* TierShort(int i)
    {
        static const char* names[kTierCount] = { "B1", "B2", "B3", "S1", "S2", "S3", "G1", "G2", "G3", "P1", "P2", "P3",
                                                 "D1", "D2", "D3", "C1", "C2", "C3", "GC1", "GC2", "GC3", "SSL", "PRO" };
        return names[std::clamp(i, 0, kTierCount - 1)];
    }
    // Which of the nine ranks a division belongs to: 0 Bronze .. 6 Grand Champion, 7 SSL, 8 Pro.
    inline int TierRank(int i)
    {
        i = std::clamp(i, 0, kTierCount - 1);
        return i < kSSLTier ? i / 3 : i == kSSLTier ? 7 : 8;
    }
    // Marks for Bronze II..Pro, per preset and range (Short, Medium, Long).
    inline constexpr int kTierFlick[6][kRangeCount][kTierCount - 1] = {
        { // Small
            { 30100, 32500, 35400, 38300, 41250, 44750, 48250, 52250, 56250, 60250, 64500, 69250, 76000, 82500, 90250, 102750, 116500, 131750, 143500, 145750, 148000, 158250 },
            { 23000, 25000, 27200, 29300, 31800, 34500, 37000, 39700, 42750, 45750, 49000, 52750, 58000, 63250, 69000, 79000, 89000, 101250, 111250, 114250, 117500, 132250 },
            { 19200, 20800, 22800, 24600, 26200, 28100, 30200, 32500, 35000, 37500, 40250, 43250, 47500, 51750, 56500, 64750, 73500, 83750, 92250, 95250, 98000, 113750 },
        },
        { // Medium
            { 1350, 1800, 2450, 3350, 4450, 5900, 7450, 8850, 10300, 11700, 13200, 15100, 17700, 20700, 24400, 30100, 36400, 44000, 53500, 61500, 69000, 100250 },
            { 1200, 1700, 2400, 3200, 4200, 5300, 6550, 7900, 9300, 10600, 12000, 13600, 15900, 18400, 21500, 26500, 32500, 39400, 47500, 54500, 61250, 90500 },
            { 950, 1350, 2000, 2850, 3750, 4750, 5950, 7250, 8500, 9650, 11000, 12600, 14700, 16900, 19500, 24400, 29400, 35600, 43250, 50000, 56000, 82500 },
        },
        { // Big
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1100, 1950, 3250, 4950, 6500, 8200, 10900, 14100, 18000, 23600, 29400, 35600, 66250 },
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1650, 2750, 4400, 6000, 7800, 10400, 13500, 17000, 21900, 27400, 33100, 61500 },
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1350, 2350, 4150, 5700, 7300, 9800, 12800, 16200, 20800, 25800, 31100, 57750 },
        },
        { // Turnaround
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1450, 3100, 5800, 8350, 10900, 14600, 18700, 23300, 49000 },
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1450, 2950, 5600, 7950, 10400, 13900, 17700, 22100, 46250 },
            { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 2500, 5250, 7650, 10000, 13300, 17000, 21000, 43750 },
        },
        { // Mixed
            { 600, 750, 900, 1150, 1450, 1750, 2100, 2600, 3300, 4200, 5150, 6500, 8600, 10700, 13000, 16700, 21400, 26600, 34000, 40750, 47500, 78250 },
            { 450, 550, 750, 950, 1150, 1450, 1750, 2150, 2750, 3650, 4600, 5700, 7500, 9600, 11600, 15000, 19200, 24200, 30500, 36800, 42750, 71000 },
            { 400, 500, 600, 750, 950, 1150, 1400, 1800, 2450, 3150, 3950, 5000, 6600, 8700, 11000, 14100, 17900, 22500, 28300, 34100, 39500, 65750 },
        },
        { // Flip Reset
            { 1700, 2250, 2650, 3100, 3750, 4550, 5600, 6850, 8400, 9950, 11400, 12800, 14800, 17600, 21200, 27300, 33200, 40250, 48750, 57500, 65750, 100250 },
            { 1700, 2250, 2650, 3100, 3750, 4550, 5600, 6850, 8400, 9950, 11400, 12800, 14800, 17600, 21200, 27300, 33200, 40250, 48750, 57500, 65750, 100250 },
            { 1700, 2250, 2650, 3100, 3750, 4550, 5600, 6850, 8400, 9950, 11400, 12800, 14800, 17600, 21200, 27300, 33200, 40250, 48750, 57500, 65750, 100250 },
        },
    };
    inline constexpr int kTierTrack[9][kRangeCount][kTierCount - 1] = {
        { // Precision
            { 39900, 41500, 43300, 44900, 46400, 47900, 49300, 50600, 51900, 53000, 54000, 54900, 56000, 56900, 57600, 58440, 59000, 59350, 59570, 59650, 59660, 59680 },
            { 23500, 25000, 26700, 28300, 30000, 31700, 33500, 35300, 37100, 38900, 40600, 42300, 44600, 46800, 48800, 51600, 53900, 55800, 57600, 58930, 59470, 59630 },
            { 11800, 12700, 13800, 14900, 16000, 17200, 18500, 19800, 21200, 22700, 24200, 25800, 28100, 30500, 33100, 36800, 40700, 44400, 49000, 53900, 57200, 59580 },
        },
        { // Control
            { 25500, 26900, 28600, 30200, 31900, 33500, 35200, 36900, 38600, 40300, 41900, 43600, 45700, 47700, 49700, 52200, 54400, 56200, 57900, 59130, 59580, 59690 },
            { 12900, 13900, 15000, 16100, 17300, 18500, 19800, 21200, 22700, 24200, 25700, 27300, 29600, 31900, 34300, 38000, 41600, 45200, 49600, 54200, 57400, 59630 },
            { 6100, 6550, 7150, 7750, 8400, 9050, 9800, 10600, 11400, 12300, 13300, 14300, 15800, 17500, 19300, 22200, 25500, 29100, 34300, 41400, 48100, 59520 },
        },
        { // Standard
            { 16900, 17900, 19200, 20400, 21700, 23100, 24500, 26000, 27600, 29200, 30700, 32500, 34800, 37200, 39600, 43100, 46600, 49700, 53300, 56700, 58630, 59680 },
            { 8000, 8650, 9400, 10100, 10900, 11700, 12600, 13600, 14600, 15700, 16800, 18000, 19700, 21600, 23600, 26800, 30400, 34100, 39400, 46200, 52100, 59590 },
            { 3600, 3900, 4300, 4650, 5050, 5450, 5900, 6350, 6900, 7500, 8100, 8800, 9750, 10900, 12100, 14100, 16500, 19300, 23600, 30100, 37600, 58670 },
        },
        { // Fast
            { 11900, 12700, 13600, 14600, 15600, 16700, 17900, 19100, 20400, 21800, 23200, 24800, 26900, 29200, 31600, 35200, 39000, 42800, 47500, 52800, 56400, 59670 },
            { 5500, 5850, 6250, 6750, 7250, 7850, 8500, 9200, 10000, 10800, 11600, 12600, 14000, 15500, 17100, 19900, 23000, 26400, 31400, 38600, 45900, 59340 },
            { 2450, 2600, 2800, 3000, 3250, 3500, 3850, 4200, 4550, 4950, 5400, 5850, 6600, 7450, 8350, 9900, 11700, 13800, 17200, 22700, 29700, 56800 },
        },
        { // Insane
            { 7400, 7800, 8250, 8750, 9350, 10000, 10800, 11500, 12400, 13400, 14300, 15400, 16800, 18500, 20200, 22900, 26000, 29500, 34600, 41700, 48600, 59640 },
            { 3400, 3600, 3800, 4000, 4300, 4650, 5000, 5350, 5800, 6300, 6800, 7400, 8250, 9200, 10200, 11800, 13700, 16100, 19700, 25800, 33100, 58580 },
            { 1500, 1600, 1700, 1800, 1950, 2100, 2250, 2400, 2600, 2850, 3100, 3450, 3850, 4300, 4800, 5650, 6650, 7950, 10000, 13700, 18800, 52100 },
        },
        { // Dynamic Low
            { 26200, 27600, 29200, 30700, 32300, 33900, 35500, 37100, 38700, 40300, 41800, 43400, 45400, 47300, 49200, 51600, 53800, 55500, 57300, 58740, 59380, 59660 },
            { 13800, 14800, 15900, 17000, 18200, 19400, 20700, 22000, 23400, 24900, 26300, 27800, 30000, 32300, 34600, 38000, 41500, 44900, 49100, 53500, 56700, 59600 },
            { 6700, 7250, 7850, 8500, 9150, 9850, 10600, 11400, 12300, 13200, 14100, 15200, 16700, 18300, 20100, 22900, 26100, 29600, 34600, 41200, 47600, 59340 },
        },
        { // Dynamic Medium
            { 17800, 18900, 20200, 21500, 22900, 24300, 25700, 27200, 28800, 30300, 31800, 33500, 35700, 38000, 40300, 43600, 46800, 49700, 53100, 56400, 58320, 59670 },
            { 8600, 9200, 9950, 10700, 11500, 12300, 13200, 14200, 15200, 16300, 17300, 18600, 20300, 22300, 24300, 27500, 31100, 34800, 39900, 46300, 51800, 59560 },
            { 3950, 4300, 4650, 5000, 5400, 5800, 6250, 6750, 7300, 7900, 8500, 9150, 10200, 11200, 12500, 14500, 16900, 19700, 24100, 30600, 37900, 58370 },
        },
        { // Dynamic High
            { 11100, 11900, 12800, 13600, 14400, 15300, 16200, 17300, 18500, 19700, 20900, 22300, 24200, 26200, 28300, 31400, 34800, 38300, 43100, 49000, 53900, 59600 },
            { 5200, 5650, 6150, 6600, 7000, 7500, 8000, 8550, 9200, 9950, 10700, 11600, 12800, 14100, 15400, 17600, 20100, 23000, 27400, 34000, 41100, 58860 },
            { 2350, 2550, 2800, 3000, 3200, 3450, 3750, 4000, 4300, 4700, 5100, 5600, 6250, 6950, 7700, 8900, 10300, 12000, 14800, 19500, 25600, 54600 },
        },
        { // Dynamic Full
            { 16500, 17600, 18800, 20000, 21100, 22400, 23700, 25000, 26500, 27900, 29300, 30800, 32800, 34800, 36900, 39900, 42900, 45700, 49400, 53400, 56400, 59680 },
            { 8250, 8900, 9600, 10300, 11000, 11800, 12600, 13400, 14400, 15400, 16400, 17600, 19200, 20900, 22800, 25600, 28700, 31900, 36400, 42300, 47900, 59230 },
            { 3850, 4150, 4500, 4850, 5200, 5600, 6050, 6500, 7000, 7550, 8100, 8750, 9700, 10800, 12000, 14000, 16300, 18800, 22600, 28200, 34600, 56600 },
        },
    };
    inline constexpr int kTierHybrid[7][kRangeCount][kTierCount - 1] = {
        { // Control
            { 6000, 6650, 7650, 8800, 10500, 12800, 15100, 17000, 18700, 20900, 23400, 26100, 29300, 32400, 35600, 39600, 43250, 46750, 50500, 53500, 55500, 63250 },
            { 3000, 3600, 4200, 4950, 6000, 7450, 9100, 10600, 12100, 13600, 15400, 17900, 21200, 23800, 26700, 31400, 35600, 39900, 45500, 50250, 53500, 62000 },
            { 1750, 2050, 2450, 2900, 3500, 4350, 5400, 6400, 7450, 8850, 10300, 11700, 13800, 16200, 18200, 21400, 25400, 29100, 35000, 41500, 47750, 60750 },
        },
        { // Fast
            { 3250, 3700, 4450, 5400, 6400, 7650, 9100, 10800, 12700, 14400, 15700, 17600, 20600, 23200, 25900, 29800, 34300, 39100, 44750, 50000, 54000, 63500 },
            { 1850, 2100, 2450, 2950, 3600, 4350, 5000, 5800, 6800, 8150, 9450, 10800, 12600, 14500, 16500, 20000, 23700, 27400, 33500, 39900, 46500, 62000 },
            { 950, 1050, 1250, 1450, 1850, 2400, 3000, 3600, 4400, 5100, 5900, 6850, 8000, 9200, 10600, 12700, 15200, 18200, 22200, 27900, 34000, 58750 },
        },
        { // Insane
            { 1850, 2250, 2900, 3350, 3800, 4700, 5900, 6950, 7900, 9150, 10400, 11900, 14200, 16900, 19300, 22800, 26500, 30500, 36500, 43500, 49750, 63250 },
            { 950, 1150, 1400, 1650, 2000, 2600, 3300, 4050, 4900, 5800, 6700, 7650, 8800, 10400, 12200, 14300, 16900, 19800, 24400, 30200, 37000, 59750 },
            { 700, 800, 900, 1050, 1350, 1650, 2000, 2450, 3000, 3450, 3950, 4750, 5900, 6850, 7900, 9650, 11400, 13500, 16600, 20200, 25000, 53250 },
        },
        { // Dynamic Low
            { 5100, 6050, 7200, 8600, 10300, 12300, 14400, 16100, 18300, 21100, 23900, 26200, 29100, 32800, 35700, 39300, 43250, 46750, 50750, 54000, 56250, 63750 },
            { 2900, 3750, 4400, 5050, 6250, 7650, 8950, 10300, 12100, 14500, 16700, 18900, 21500, 24300, 27400, 31400, 35500, 39900, 45250, 50250, 53750, 62500 },
            { 1550, 1950, 2450, 3050, 3700, 4350, 5100, 6100, 7500, 8850, 10100, 11700, 14200, 16500, 18700, 21900, 25800, 29800, 35300, 41250, 47250, 61250 },
        },
        { // Dynamic Medium
            { 3750, 4650, 5800, 7000, 8450, 10200, 12000, 13700, 15600, 17700, 19400, 21600, 25300, 28500, 31800, 36400, 40500, 44750, 49750, 53750, 56500, 64250 },
            { 2200, 2750, 3450, 4250, 5200, 6050, 7000, 8300, 10000, 11600, 13000, 14500, 16600, 19400, 22300, 26000, 29900, 34600, 40250, 46250, 51250, 62750 },
            { 1400, 1600, 1900, 2300, 2900, 3650, 4400, 5050, 5650, 6350, 7450, 8900, 10600, 12500, 14300, 17200, 20500, 23900, 28500, 34400, 40750, 60250 },
        },
        { // Dynamic High
            { 2800, 3500, 4250, 5100, 6350, 7650, 8900, 10100, 11400, 13000, 14900, 17000, 19500, 21700, 24100, 28400, 32600, 37100, 43000, 48750, 53250, 64000 },
            { 1700, 1850, 2150, 2700, 3400, 4050, 4800, 5700, 6750, 7850, 8800, 9850, 11600, 13700, 15900, 18900, 22300, 26100, 31300, 37500, 44000, 61750 },
            { 850, 950, 1100, 1400, 1850, 2300, 2800, 3650, 4550, 5150, 5650, 6350, 7650, 8800, 10200, 12300, 14500, 17100, 21100, 26000, 31400, 56250 },
        },
        { // Dynamic Full
            { 4250, 4950, 6000, 7200, 8350, 9750, 11500, 13300, 15100, 16800, 18600, 20500, 23200, 26000, 29000, 33100, 37100, 41250, 46250, 50750, 54500, 63750 },
            { 2350, 2950, 3550, 4300, 5100, 6000, 6900, 8050, 9450, 10700, 11800, 13200, 15600, 18100, 20500, 23900, 27400, 31600, 36900, 42000, 47500, 61750 },
            { 1300, 1650, 2100, 2400, 2750, 3250, 3900, 4600, 5450, 6200, 7100, 8400, 10300, 11900, 13500, 16300, 19100, 22400, 26500, 31700, 37300, 58000 },
        },
    };
    inline constexpr int kTierChase[4][kRangeCount][kTierCount - 1] = {
        { // Slow
            { 32800, 34400, 35900, 37100, 38400, 39900, 41400, 42900, 44500, 45800, 47100, 48500, 50100, 51500, 52900, 55000, 56600, 57800, 58610, 59110, 59510, 59950 },
            { 21400, 22000, 22800, 23800, 24800, 26000, 27200, 28200, 29300, 30500, 31800, 33200, 35300, 37300, 39400, 42500, 45800, 48600, 50900, 52700, 54400, 58100 },
            { 13600, 14100, 14700, 15600, 16500, 17500, 18200, 18700, 19100, 19800, 20500, 21400, 22900, 24300, 25700, 28100, 30800, 33100, 35600, 38000, 40300, 47900 },
        },
        { // Medium
            { 17300, 17800, 18300, 18900, 19700, 20700, 21600, 22400, 23300, 24300, 25400, 26600, 28000, 29600, 31600, 35000, 38100, 40700, 42900, 44500, 46100, 51300 },
            { 12300, 12700, 13000, 13400, 13800, 14300, 14700, 15300, 15800, 16300, 16700, 17200, 18000, 18800, 19700, 21800, 24100, 25800, 27500, 28900, 30900, 37200 },
            { 9100, 9600, 10200, 10500, 10800, 11100, 11500, 11800, 12000, 12300, 12700, 13000, 13300, 13800, 14400, 15300, 16100, 17100, 18000, 19100, 20500, 25600 },
        },
        { // Fast
            { 12300, 12600, 12900, 13200, 13500, 13900, 14400, 14800, 15400, 15800, 16200, 16800, 17800, 18700, 19700, 21500, 23600, 25800, 27600, 29300, 31000, 36800 },
            { 9900, 10000, 10200, 10400, 10600, 10900, 11100, 11400, 11600, 11800, 11900, 12200, 12700, 13200, 13600, 14600, 15700, 16800, 18100, 19300, 20700, 25600 },
            { 7700, 8100, 8500, 8800, 9050, 9300, 9450, 9650, 9850, 10100, 10300, 10500, 10600, 10700, 10800, 11200, 11600, 12000, 12600, 13400, 14300, 18900 },
        },
        { // Random
            { 20000, 20900, 21600, 22100, 22700, 23600, 24600, 25500, 26400, 27400, 28400, 29500, 31300, 32900, 34400, 37200, 39800, 41900, 43600, 45100, 46700, 51400 },
            { 13900, 14500, 14900, 15600, 16200, 16800, 17300, 17800, 18500, 19000, 19700, 20400, 21500, 22700, 24000, 25700, 27900, 29900, 31700, 33100, 34800, 39900 },
            { 10100, 10600, 11100, 11600, 12000, 12400, 12900, 13400, 13800, 14200, 14600, 15000, 15500, 16400, 17100, 18000, 19100, 20600, 22000, 22900, 24400, 29800 },
        },
    };

    // The score needed for a tier on a preset (Bronze needs 0). Flip Reset has one distance,
    // so every range uses the same marks.
    inline float TierThreshold(int mode, int preset, int range, int tier)
    {
        if (tier <= 0 || preset < 0 || preset >= PresetCount(mode)) return 0.f;
        tier = std::min(tier, kTierCount - 1);
        range = PresetUsesRange(mode, preset) ? std::clamp(range, 0, kRangeCount - 1) : 0;
        const int* marks = mode == kFlick ? kTierFlick[preset][range] : mode == kTrack ? kTierTrack[preset][range]
                         : mode == kHybrid ? kTierHybrid[preset][range] : kTierChase[preset][range];
        return static_cast<float>(marks[tier - 1]);
    }
    inline int TierIndex(float score, int mode, int preset, int range)
    {
        int t = 0;
        for (int i = 1; i < kTierCount; ++i)
            if (score >= TierThreshold(mode, preset, range, i)) t = i;
        return t;
    }
    // ------------------------------------------------------------------------------------
    // Rank value and the per-preset rating
    // ------------------------------------------------------------------------------------
    // A score as a continuous rank value: 0 = the start of Bronze I, +1/3 per division (each
    // division's mark is i/3), 7 = Supersonic Legend, 7.33 = Pro. Linear in score within a division.
    inline float RankValue(float score, int mode, int preset, int range)
    {
        float lo = 0.f;
        for (int i = 1; i < kTierCount; ++i)
        {
            float hi = TierThreshold(mode, preset, range, i);
            if (score < hi) return ((i - 1) + std::clamp((score - lo) / std::max(1.f, hi - lo), 0.f, 1.f)) / 3.f;
            lo = hi;
        }
        return static_cast<float>(kTierCount - 1) / 3.f; // Pro
    }
    inline int TierFromValue(float v) { return std::clamp(static_cast<int>(std::floor(v * 3.f + 1e-4f)), 0, kTierCount - 1); }

    // Your current rank on a preset: a weighted average of your runs' rank values.
    //  - Placements: for the first kPlacementRuns runs it's the plain average (weight 1/n).
    //  - After that each run moves it kRankRate of the way toward that run (a moving average
    //    where older runs fade: weights 0.1, 0.09, 0.081, ...). A one-off great or awful run
    //    nudges it; a new level, held for ~10 runs, carries it most of the way.
    //  - A run counts as at most kRankStepLimit (one rank = three divisions) away from your
    //    current rank, so an AFK or alt-tab run can't sink it.
    constexpr int kPlacementRuns = 10;
    constexpr float kRankRate = 0.1f;
    constexpr float kRankStepLimit = 1.f;
    struct RankState
    {
        float value = 0.f;     // current rank value
        int runs = 0;          // runs counted
        float lastRun = -1.f;  // the last run's own rank value
        float lastDelta = 0.f; // how much the last run moved the rank
    };
    inline void RankUpdate(RankState& st, float runValue)
    {
        const float before = st.value;
        float v = runValue;
        if (st.runs > 0) v = std::clamp(v, st.value - kRankStepLimit, st.value + kRankStepLimit);
        const int n = st.runs + 1;
        const float k = n <= kPlacementRuns ? 1.f / static_cast<float>(n) : kRankRate;
        st.value = st.runs == 0 ? v : st.value + k * (v - st.value);
        st.runs = n;
        st.lastRun = runValue;
        st.lastDelta = n > 1 ? st.value - before : 0.f;
    }
    inline bool RankPlacing(const RankState& st) { return st.runs < kPlacementRuns; }

    static_assert(std::size(kTierFlick) == std::size(kFlickPresets) && std::size(kTierTrack) == std::size(kTrackPresets) &&
                  std::size(kTierHybrid) == std::size(kHybridPresets) && std::size(kTierChase) == std::size(kChasePresets),
                  "every preset needs tier marks");

    // A repeatable seed per preset, for the tests and the tier model. (The plugin itself seeds
    // every run randomly.)
    inline unsigned PresetSeed(int mode, int preset)
    {
        return 0x07AB0000u + static_cast<unsigned>(mode) * 64u + static_cast<unsigned>(preset) + kSeedVersion * 4096u;
    }
}
