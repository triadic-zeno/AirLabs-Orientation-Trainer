#pragma once
// Shared, game-free preview geometry. It uses the same constraints and contact planner as play.
// Update on the game thread; the UI receives immutable snapshots, never a live RocketSim arena.
#include <vector>

namespace airlabs
{
    struct LabPreviewLine { Vector a, b; int layer = 0; bool lit = false; };
    struct LabPreviewData
    {
        std::vector<LabPreviewLine> lines;
        std::vector<Vector> goals;
        std::vector<Vector> velocities;
        Vector example{}, nose = V(1.f, 0.f, 0.f);
        LabParams params;
        bool spawnValid = false, checking = false;
        int checks = 0;
        float scaleRadius = 2000.f;
        std::string status;
    };

    class LabPreview
    {
    public:
        LabPreviewData data;

        bool Update(const LabParams& p, const Basis& basis, unsigned seed, bool wantGoals = true,
                    const Vector& hitbox = V(118.f, 84.2f, 36.16f))
        {
            const Vector nose = basis.forward;
            const auto key = LabGeometryKey(p, nose);
            const bool changed = !haveKey || key != lastKey || seed != lastSeed || Len(Sub(hitbox, lastHitbox)) > 0.01f;
            if (changed)
            {
                const Vector previous = data.example;
                const bool keep = haveKey && seed == lastSeed && LabSpawnAllowed(previous, p, nose);
                lastKey = key; lastSeed = seed; haveKey = true;
                lastHitbox = hitbox;
                data = LabPreviewData{}; data.params = p; data.nose = nose;
                data.scaleRadius = std::max({ p.spawnMax > 0.f ? p.spawnMax : p.bubble, p.goalDistance, 500.f });
                std::mt19937 rng(seed);
                data.example = keep ? previous : LabSpawnPoint(rng, p, nose);
                data.spawnValid = LabSpawnAllowed(data.example, p, nose);
                BuildGrid(p, nose);
                if (!data.spawnValid) { data.status = "No ball spawn fits this cone, height and floor."; return true; }
                const Vector first = LabPickLaunch(rng, data.example, p, LabNextSpeed(rng, p), kLabReactSecWait);
                if (LabLaunchAllowed(data.example, first, p)) data.velocities.push_back(first);
                for (int i = 0; i < 160 && data.velocities.size() < 28; ++i)
                {
                    const Vector d = LabPickDirection(rng, data.example, p, p.bubble);
                    float speed = p.dynamic ? p.speedMin + (p.speedMax - p.speedMin) * (i % 3) / 2.f : p.speed;
                    const Vector v = Scale(d, speed);
                    if (LabLaunchAllowed(data.example, v, p)) data.velocities.push_back(v);
                }
                for (size_t i = 0; i < data.velocities.size(); ++i)
                {
                    Vector at = data.example, v = data.velocities[i];
                    const float drag = p.speedLimit ? std::pow(1.f - RocketSim::RLConst::BALL_DRAG, 0.08f) : 1.f;
                    for (int j = 0; j < 22; ++j)
                    {
                        const Vector next = Add(at, Scale(v, 0.08f));
                        if (Len(next) > p.bubble || next.Z < -p.floor) break;
                        data.lines.push_back({ at, next, 1, i == 0 });
                        at = next; v = Scale(v, drag);
                    }
                }
                data.checking = p.redirectRing && p.motion == kLabRespawn && !data.velocities.empty();
                data.status = data.velocities.empty() ? "No launch fits these settings. Reduce pass distance or widen steepest path." :
                    data.checking ? "Checking goal examples for the bold amber pass..." : "Blue: ball spawns. Amber: launch paths.";
            }
            if (wantGoals && data.checking) { CheckGoal(basis, seed); return true; }
            return changed;
        }

    private:
        decltype(LabGeometryKey(LabParams{}, V(1.f, 0.f, 0.f))) lastKey{};
        bool haveKey = false;
        unsigned lastSeed = 0;
        Vector lastHitbox = V(118.f, 84.2f, 36.16f);

        void BuildGrid(const LabParams& p, const Vector& nose)
        {
            const float outer = p.spawnMax > 0.f ? std::min(p.spawnMax, p.bubble - 60.f) : p.bubble - 60.f;
            const float inner = std::min(p.spawnMin, outer);
            auto dir = [](float lat, float lon) { return V(std::cos(lat * kDeg) * std::cos(lon * kDeg),
                std::cos(lat * kDeg) * std::sin(lon * kDeg), std::sin(lat * kDeg)); };
            auto line = [&](Vector a, Vector b, float radius, int layer) {
                const Vector mid = Scale(Norm(Add(a, b)), radius);
                const bool lit = layer == 0 && LabSpawnAllowed(a, p, nose) && LabSpawnAllowed(b, p, nose) && LabSpawnAllowed(mid, p, nose);
                data.lines.push_back({ a, b, layer, lit });
            };
            for (float r : { inner, outer })
            {
                for (int lat = -75; lat <= 75; lat += 15)
                    for (int lon = 0; lon < 360; lon += 10)
                        line(Scale(dir((float)lat, (float)lon), r), Scale(dir((float)lat, (float)lon + 10.f), r), r, 0);
                for (int lon = 0; lon < 360; lon += 30)
                    for (int lat = -90; lat < 90; lat += 10)
                        line(Scale(dir((float)lat, (float)lon), r), Scale(dir((float)lat + 10.f, (float)lon), r), r, 0);
                // Exact height and cone boundaries remain visible even for narrow/level bands.
                for (float lat : { p.heightMin, p.heightMax })
                    for (int lon = 0; lon < 360; lon += 5)
                        line(Scale(dir(lat, (float)lon), r), Scale(dir(lat, (float)lon + 5.f), r), r, 0);
                const Basis b = LabFacing(nose);
                for (int i = 0; i < 72; ++i)
                {
                    auto point = [&](int j) { const float a = j * 5.f * kDeg; return Scale(Add(Scale(b.forward, std::cos(p.spawnCone * kDeg)),
                        Scale(Add(Scale(b.right, std::cos(a)), Scale(b.up, std::sin(a))), std::sin(p.spawnCone * kDeg))), r); };
                    line(point(i), point(i + 1), r, 0);
                }
            }
            // Faint purple shell: one exact goal distance. Bright dots below are checked targets.
            for (int lat = -60; lat <= 60; lat += 30)
                for (int lon = 0; lon < 360; lon += 10)
                    line(Scale(dir((float)lat, (float)lon), p.goalDistance),
                         Scale(dir((float)lat, (float)lon + 10.f), p.goalDistance), p.goalDistance, 2);
        }

        void CheckGoal(const Basis& basis, unsigned seed)
        {
            using namespace RocketSim;
            if (RocketSim::GetStage() == RocketSimStage::UNINITIALIZED) RocketSim::InitFromMem({}, true);
            struct RestoreCap { float saved; ~RestoreCap() { g_carMaxSpeedUU = saved; } } restore{ g_carMaxSpeedUU };
            g_carMaxSpeedUU = data.params.speedLimit ? RLConst::CAR_MAX_SPEED : 0.f;
            std::unique_ptr<Arena> arena(Arena::Create(GameMode::THE_VOID));
            auto m = arena->GetMutatorConfig(); m.gravity = Vec(0, 0, 0);
            m.ballDrag = data.params.speedLimit ? RLConst::BALL_DRAG : 0.f;
            m.ballMaxSpeed = data.params.speedLimit ? RLConst::BALL_MAX_SPEED : 1e6f;
            arena->SetMutatorConfig(m);
            const char* name = nullptr;
            Car* car = arena->AddCar(Team::BLUE, LabWorld::ConfigFor(lastHitbox, &name));
            CarState cs; cs.pos = Vec(0, 0, 0); cs.isOnGround = false;
            cs.rotMat = RotMat(ToRs(basis.forward), ToRs(basis.right), ToRs(basis.up)); car->SetState(cs);
            BallState bs; bs.pos = ToRs(data.example); bs.vel = Vec(0, 0, 0); arena->ball->SetState(bs);
            LabState s; s.ballPos = data.example;
            s.launchSpeed = Len(data.velocities.front()); s.launchDir = Norm(data.velocities.front());
            std::mt19937 rng(seed + 7919u * static_cast<unsigned>(data.checks + 1));
            const auto plan = LabPlanRedirect(arena.get(), car, data.params, s, rng, 48);
            if (plan.checked)
            {
                bool unique = true;
                for (const Vector& goal : data.goals) if (AngleDeg(goal, plan.goal) < 3.f) unique = false;
                if (unique) data.goals.push_back(plan.goal);
            }
            ++data.checks;
            data.checking = data.checks < 16 && data.goals.size() < 8;
            data.status = data.checking ? "Checking goal examples for the bold amber pass..." : data.goals.empty() ?
                "No checked goal for this example. Change goal distance/angle or choose New example." :
                "Bright purple: checked goal examples for the bold amber pass.";
        }
    };
}
