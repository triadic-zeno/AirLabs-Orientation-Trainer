#pragma once
// Respawn's redirect target. Planning is approximate for the player's approach, but proposed
// contacts are checked with RocketSim. Remaining empty-arena flight is projected using its
// drag/gravity/caps and verified against RocketSim in tests. No live physics is edited.
#include <memory>

namespace airlabs
{
    struct LabRedirectPlan
    {
        bool checked = false;
        Vector goal = V(0.f, 0.f, 0.f), normal = V(1.f, 0.f, 0.f); // hidden-world coordinates
        Vector ballPos{}, ballVel{}, carPos{}, carVel{};
        Basis carBasis{};
        float turn = 0.f, flightSec = 1.f, distance = 1250.f; // ring distance from car at spawn
    };

    inline Basis LabFacing(const Vector& f)
    {
        const Vector forward = Norm(f);
        const Vector up = std::fabs(forward.Z) < 0.95f ? V(0.f, 0.f, 1.f) : V(1.f, 0.f, 0.f);
        const Vector right = Norm(Cross(up, forward));
        return Basis{ forward, right, Cross(forward, right) };
    }

    // A centre crossing counts only when the WHOLE ball fits inside the opening, forward.
    inline bool LabRingCrossed(const Vector& before, const Vector& after, const Vector& centre,
                               const Vector& normal, float radius)
    {
        const float a = Dot(Sub(before, centre), normal), b = Dot(Sub(after, centre), normal);
        if (a > 0.f || b < 0.f || b - a < 1e-5f || radius <= kLabBallRadius) return false;
        const Vector crossing = Add(before, Scale(Sub(after, before), -a / (b - a)));
        return Len(Sub(crossing, centre)) <= radius - kLabBallRadius;
    }

    inline bool LabSphereCrossing(const Vector& a, const Vector& b, const Vector& centre, float radius, float& t)
    {
        const Vector x = Sub(a, centre), v = Sub(b, a);
        const float aa = Dot(v, v), bb = 2.f * Dot(x, v), cc = Dot(x, x) - radius * radius;
        const float disc = bb * bb - 4.f * aa * cc;
        if (aa < 1e-8f || disc < 0.f) return false;
        for (float root : { (-bb - std::sqrt(disc)) / (2.f * aa), (-bb + std::sqrt(disc)) / (2.f * aa) })
            if (root >= 0.f && root <= 1.f) { t = root; return true; }
        return false;
    }

    // A ring that faces the viewer can rotate or translate between ticks. Require actual
    // ball motion THROUGH the opening: changing only the camera cannot award a goal.
    inline bool LabFacingRingCrossed(const Vector& before, const Vector& after,
        const Vector& centreBefore, const Vector& centreAfter, const Vector& viewerBefore,
        const Vector& viewerAfter, float radius)
    {
        const Vector n0 = Norm(Sub(centreBefore, viewerBefore)), n1 = Norm(Sub(centreAfter, viewerAfter));
        const Vector x0 = Sub(before, centreBefore), x1 = Sub(after, centreAfter);
        const float a = Dot(x0, n0), b = Dot(x1, n1);
        if (a * b > 0.f || std::fabs(b - a) < 1e-5f || radius <= kLabBallRadius) return false;
        if (std::fabs(Dot(Sub(x1, x0), Norm(Add(n0, n1)))) < 1e-4f) return false;
        float lo = 0.f, hi = 1.f;
        for (int i = 0; i < 12; ++i)
        {
            const float t = (lo + hi) * 0.5f;
            const Vector n = Norm(Add(Scale(n0, 1.f - t), Scale(n1, t)));
            const float d = Dot(Add(Scale(x0, 1.f - t), Scale(x1, t)), n);
            if ((a <= 0.f) == (d <= 0.f)) lo = t; else hi = t;
        }
        const float t = (lo + hi) * 0.5f;
        return Len(Add(Scale(x0, 1.f - t), Scale(x1, t))) <= radius - kLabBallRadius;
    }

    inline LabRedirectPlan LabPlanRedirect(RocketSim::Arena* live, RocketSim::Car* liveCar,
                                           const LabParams& p, const LabState& s, std::mt19937& rng,
                                           int attempts = 128)
    {
        using namespace RocketSim;
        constexpr float dt = 1.f / 120.f;
        const Vector gravity = FromRs(live->GetMutatorConfig().gravity);
        const auto initialCar = liveCar->GetState();
        const auto initialBall = live->ball->GetState();
        Vector cp = FromRs(initialCar.pos), cv = FromRs(initialCar.vel);
        Vector bp = FromRs(initialBall.pos), bv = FromRs(initialBall.vel);
        Vector contact = bp, incoming = bv, coast = cv;
        Vector meetCar = cp;
        float bestRange = 1e9f;
        float meetTime = p.hangSec + 1.f;
        const float drag = p.speedLimit ? std::pow(1.f - RLConst::BALL_DRAG, dt) : 1.f;
        const float wait = std::max(0.f, p.hangSec);
        bool launched = false;
        // Predict the pass and a meeting point with room to turn. The approach check is an
        // estimate, not a claim that every controller trajectory reaches this exact state.
        for (int i = 0; i < 720; ++i)
        {
            const float t = i * dt;
            if (!launched && t >= wait)
            {
                bv = Add(cv, Scale(s.launchDir, s.launchSpeed));
                launched = true;
            }
            cv = Add(cv, Scale(gravity, dt));
            if (p.speedLimit) CapSpeed(cv, RLConst::CAR_MAX_SPEED);
            cp = Add(cp, Scale(cv, dt));
            bv = Scale(bv, drag);
            bv = Add(bv, Scale(gravity, dt));
            if (p.speedLimit) CapSpeed(bv, RLConst::BALL_MAX_SPEED);
            bp = Add(bp, Scale(bv, dt));
            const Vector rel = Sub(bp, cp);
            // Avoid waiting for a slow pass until both bodies are already near terminal
            // falling speeds: the car would have no spare speed for a gentle redirect.
            const float cost = Len(rel) + 5.f * std::max(0.f, Len(bv) - 1800.f);
            if (launched && Len(rel) + 200.f < LabBoostDistance(std::max(0.f, t - 0.4f), p.speedLimit) &&
                rel.Z > -p.floor + kLabBallRadius && Len(rel) < p.bubble - 30.f && cost < bestRange)
            {
                contact = bp; incoming = bv; coast = cv; meetTime = t + dt; meetCar = cp;
                bestRange = cost;
            }
        }
        cp = meetCar;
        const Vector inRel = Sub(incoming, coast);
        const Vector travel = Len(inRel) > 1.f ? Norm(inRel) : s.launchDir;
        const Basis around = LabFacing(travel);
        std::unique_ptr<Arena> probe(Arena::Create(GameMode::THE_VOID));
        probe->SetMutatorConfig(live->GetMutatorConfig());
        Car* pc = probe->AddCar(Team::BLUE, liveCar->config);
        LabRedirectPlan best;
        const float nose = liveCar->config.hitboxPosOffset.x + liveCar->config.hitboxSize.x * 0.5f;
        const float ballRadius = live->GetMutatorConfig().ballRadius;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            const float a = std::uniform_real_distribution<float>(0.f, 2.f * kPi)(rng);
            const Vector side = Add(Scale(around.right, std::cos(a)), Scale(around.up, std::sin(a)));
            const float back = std::uniform_real_distribution<float>(0.01f, 0.65f)(rng);
            const Basis facing = LabFacing(Norm(Sub(side, Scale(travel, back))));
            const Vector carAt = Sub(Sub(contact, Scale(facing.forward, nose + ballRadius + 2.f)),
                                     Scale(facing.up, liveCar->config.hitboxPosOffset.z));
            const Vector displacement = Sub(carAt, cp);
            const float moveTime = std::max(0.1f, meetTime - 0.4f);
            if (Len(displacement) > LabBoostDistance(moveTime, p.speedLimit)) continue;
            // Meet the pass moving along with it, then nudge across its path. A head-on
            // approach would force a reversal instead of the requested shallow redirect.
            const float closing = std::max(80.f, Len(inRel)) * std::uniform_real_distribution<float>(0.02f, 1.3f)(rng);
            Vector approach = Add(incoming, Scale(facing.forward, closing));
            if (p.speedLimit) CapSpeed(approach, RLConst::CAR_MAX_SPEED);
            if (Len(Sub(approach, coast)) > kBoostAccel * moveTime || Dot(Sub(approach, incoming), facing.forward) < 20.f) continue;
            CarState cs;
            cs.pos = ToRs(Sub(carAt, contact));
            cs.vel = ToRs(approach);
            cs.rotMat = RotMat(ToRs(facing.forward), ToRs(facing.right), ToRs(facing.up));
            cs.isOnGround = false;
            pc->SetState(cs);
            pc->controls = CarControls{};
            BallState bs;
            bs.pos = Vec(0, 0, 0); bs.vel = ToRs(incoming);
            probe->ball->SetState(bs);
            bool hit = false;
            int hitTicks = 0;
            for (; hitTicks < 18; ++hitTicks)
            {
                probe->Step(1);
                if (pc->GetState().ballHitInfo.isValid) { hit = true; ++hitTicks; break; }
            }
            if (!hit) continue;
            // Let the solver finish the contact before measuring the outgoing direction.
            for (int i = 0; i < 5; ++i) { probe->Step(1); ++hitTicks; }
            const Vector afterCoast = Add(coast, Scale(gravity, dt * hitTicks));
            const Vector outRel = Sub(FromRs(probe->ball->GetState().vel), afterCoast);
            if (Len(outRel) < 200.f) continue;
            const float turn = AngleDeg(travel, outRel);
            if (turn < std::min(15.f, p.maxRedirectAngle * 0.5f) || turn > p.maxRedirectAngle) continue;
            cs = pc->GetState();
            Vector goalCarPos = FromRs(cs.pos), goalCarVel = FromRs(cs.vel);
            Vector flightPos = FromRs(probe->ball->GetState().pos), flightVel = FromRs(probe->ball->GetState().vel);
            // The checked touch is over. In this empty arena the remaining flight is just
            // drag, gravity and the speed cap; do not run hundreds of extra Bullet ticks.
            Vector previousGoal = p.ringRelativeMotion ? Add(contact, flightPos) :
                Add(FromRs(initialCar.pos), Sub(flightPos, goalCarPos));
            for (int i = 0; i < 960; ++i)
            {
                const Vector goal = p.ringRelativeMotion ? Add(contact, flightPos) :
                    Add(FromRs(initialCar.pos), Sub(flightPos, goalCarPos));
                float t = 0.f;
                if (i >= 48 && LabSphereCrossing(previousGoal, goal, FromRs(initialCar.pos), p.goalDistance, t))
                {
                        best.checked = true;
                        best.goal = Add(previousGoal, Scale(Sub(goal, previousGoal), t));
                        const Vector viewer = p.ringRelativeMotion ? Add(contact, goalCarPos) : FromRs(initialCar.pos);
                        best.normal = Norm(Sub(best.goal, viewer));
                        const Vector through = p.ringRelativeMotion ? flightVel : Sub(flightVel, goalCarVel);
                        if (std::fabs(Dot(Norm(through), best.normal)) < 0.15f) { best.checked = false; break; }
                        best.ballPos = contact; best.ballVel = incoming;
                        best.carPos = carAt; best.carVel = approach; best.carBasis = facing;
                        best.turn = turn; best.flightSec = (hitTicks + i) * dt;
                        best.distance = Len(Sub(best.goal, FromRs(initialCar.pos)));
                        return best;
                }
                previousGoal = goal;
                flightVel = Add(Scale(flightVel, drag), Scale(gravity, dt));
                if (p.speedLimit) CapSpeed(flightVel, RLConst::BALL_MAX_SPEED);
                flightPos = Add(flightPos, Scale(flightVel, dt));
                goalCarVel = Add(goalCarVel, Scale(gravity, dt));
                if (p.speedLimit) CapSpeed(goalCarVel, RLConst::CAR_MAX_SPEED);
                goalCarPos = Add(goalCarPos, Scale(goalCarVel, dt));
            }
        }
        // Do not invent an unchecked goal or silently change the chosen distance/angle.
        return best;
    }
}
