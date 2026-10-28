#pragma once
// Respawn's redirect target. Planning is approximate for the player's approach, but proposed
// touches and the outgoing ball's flight are checked with RocketSim. No live physics is edited.
#include <memory>

namespace airlabs
{
    struct LabRedirectPlan
    {
        bool checked = false;
        Vector goal = V(0.f, 0.f, 0.f), normal = V(1.f, 0.f, 0.f); // hidden-world coordinates
        Vector ballPos{}, ballVel{}, carPos{}, carVel{};
        Basis carBasis{};
        float turn = 0.f, flightSec = 1.f, distance = 1000.f;
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

    inline LabRedirectPlan LabPlanRedirect(RocketSim::Arena* live, RocketSim::Car* liveCar,
                                           const LabParams& p, const LabState& s, std::mt19937& rng)
    {
        using namespace RocketSim;
        constexpr float dt = 1.f / 120.f;
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
            cv.Z += RLConst::GRAVITY_Z * dt;
            if (p.speedLimit) CapSpeed(cv, RLConst::CAR_MAX_SPEED);
            cp = Add(cp, Scale(cv, dt));
            bv = Scale(bv, drag);
            bv.Z += RLConst::GRAVITY_Z * dt;
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
        float bestPenalty = 1e9f;
        const float nose = liveCar->config.hitboxPosOffset.x + liveCar->config.hitboxSize.x * 0.5f;
        const float ballRadius = live->GetMutatorConfig().ballRadius;
        for (int attempt = 0; attempt < 128; ++attempt)
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
            const Vector afterCoast = Add(coast, V(0.f, 0.f, RLConst::GRAVITY_Z * dt * hitTicks));
            const Vector outRel = Sub(FromRs(probe->ball->GetState().vel), afterCoast);
            if (Len(outRel) < 200.f) continue;
            const float turn = AngleDeg(travel, outRel);
            if (turn < 8.f || turn > 65.f) continue;
            const float penalty = turn < 15.f ? 15.f - turn : turn > 40.f ? turn - 40.f : 0.f;
            if (penalty > bestPenalty) continue;
            cs = pc->GetState();
            Vector goalCarPos = FromRs(cs.pos), goalCarVel = FromRs(cs.vel);
            cs.pos = Vec(100000, 0, 0); cs.vel = Vec(0, 0, 0);
            pc->SetState(cs); // only forecast the ball from here, with normal gravity/drag/caps
            for (int i = 0; i < 240; ++i)
            {
                const auto ball = probe->ball->GetState();
                const float distance = ball.pos.Length();
                if (distance >= 1000.f)
                {
                    if (distance <= 1200.f)
                    {
                        best.checked = true;
                        best.goal = p.ringRelativeMotion ? Add(contact, FromRs(ball.pos)) :
                            Add(FromRs(initialCar.pos), Sub(FromRs(ball.pos), goalCarPos));
                        best.normal = Norm(p.ringRelativeMotion ? FromRs(ball.vel) : Sub(FromRs(ball.vel), goalCarVel));
                        best.ballPos = contact; best.ballVel = incoming;
                        best.carPos = carAt; best.carVel = approach; best.carBasis = facing;
                        best.turn = turn; best.flightSec = (hitTicks + i) * dt;
                        best.distance = distance;
                        bestPenalty = penalty;
                    }
                    break;
                }
                probe->Step(1);
                goalCarVel.Z += RLConst::GRAVITY_Z * dt;
                if (p.speedLimit) CapSpeed(goalCarVel, RLConst::CAR_MAX_SPEED);
                goalCarPos = Add(goalCarPos, Scale(goalCarVel, dt));
            }
            if (best.checked && bestPenalty == 0.f) return best;
        }
        if (best.checked) return best;
        // Unusual restrictive settings: offer a shallow target rather than no ring at all.
        // This fallback is geometric, not a solver-checked promise of a reachable shot.
        const Vector out = RotateAbout(travel, around.up, 20.f * kDeg);
        const float seconds = std::clamp(1000.f / std::max(400.f, Len(inRel)), 0.5f, 1.5f);
        best.goal = Add(contact, Add(Scale(Add(coast, Scale(out, std::max(400.f, Len(inRel)))), seconds),
                                     V(0.f, 0.f, 0.5f * RLConst::GRAVITY_Z * seconds * seconds)));
        best.normal = out; best.turn = 20.f; best.flightSec = seconds;
        if (!p.ringRelativeMotion)
            best.goal = Add(FromRs(initialCar.pos), Add(Sub(contact, cp), Scale(out, std::max(400.f, Len(inRel)) * seconds)));
        return best;
    }
}
