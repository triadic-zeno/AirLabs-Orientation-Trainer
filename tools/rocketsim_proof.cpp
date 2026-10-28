// Proof that RocketSim (github.com/ZealanL/RocketSim, MIT) runs the Boost Lab's hidden world: an
// empty-space arena (THE_VOID) with no gravity; hits, spin from hits, a flip, a flip reset.
// Build: clone RocketSim next to this repo, build it with CMake (Release), then
//   g++ -std=c++20 -O2 -I../RocketSim/src tools/rocketsim_proof.cpp ../RocketSim/build/libRocketSim.a -o rocketsim_proof
// Result on 2026-10-28: head-on 1500 -> ball 2129, car 1294, car pitches 2.2 rad/s; corner -> ball 1831
// sideways, car 1355, car yaws and rolls; front flip +500; ball into the wheels -> 4 wheels, flip reset YES.
#include "RocketSim.h"
#include <cstdio>
using namespace RocketSim;
static Arena* MakeArena(bool speedLimitDrag = false) {
    Arena* a = Arena::Create(GameMode::THE_VOID);
    MutatorConfig m = a->GetMutatorConfig();
    m.gravity = Vec(0, 0, 0);
    if (!speedLimitDrag) m.ballDrag = 0;
    a->SetMutatorConfig(m);
    return a;
}
static void Place(Arena* a, Car* car, Vec carVel, Vec ballPos, Vec ballVel, Vec carAng = Vec()) {
    CarState cs; cs.pos = Vec(0, 0, 0); cs.vel = carVel; cs.angVel = carAng; cs.isOnGround = false; cs.hasJumped = true; cs.airTime = 1; cs.airTimeSinceJump = 1;
    car->SetState(cs);
    BallState bs; bs.pos = ballPos; bs.vel = ballVel; a->ball->SetState(bs);
}
int main() {
    RocketSim::InitFromMem({}, true);
    {   // head-on at 1500 into a still ball, middle of the nose
        Arena* a = MakeArena(); Car* car = a->AddCar(Team::BLUE);
        Place(a, car, Vec(1500, 0, 0), Vec(13.88f + 59.f + 91.25f + 20.f, 0, 20.75f), Vec(0, 0, 0));
        for (int i = 0; i < 12; i++) a->Step(1);
        auto cs = car->GetState(); auto bs = a->ball->GetState();
        printf("head-on 1500: ball %.0f uu/s (%.0f %.0f %.0f), car %.0f, car spin %.2f %.2f %.2f rad/s\n", bs.vel.Length(), bs.vel.x, bs.vel.y, bs.vel.z, cs.vel.Length(), cs.angVel.x, cs.angVel.y, cs.angVel.z);
    }
    {   // same, but the ball meets the front-right corner
        Arena* a = MakeArena(); Car* car = a->AddCar(Team::BLUE);
        Place(a, car, Vec(1500, 0, 0), Vec(13.88f + 59.f + 60.f, -(42.1f + 50.f), 20.75f), Vec(0, 0, 0));
        for (int i = 0; i < 12; i++) a->Step(1);
        auto cs = car->GetState(); auto bs = a->ball->GetState();
        printf("corner 1500:  ball %.0f uu/s (%.0f %.0f %.0f), car %.0f, car spin %.2f %.2f %.2f rad/s\n", bs.vel.Length(), bs.vel.x, bs.vel.y, bs.vel.z, cs.vel.Length(), cs.angVel.x, cs.angVel.y, cs.angVel.z);
    }
    {   // a front flip from rest (has jumped long ago: in RL you'd have no flip; check what RocketSim says)
        Arena* a = MakeArena(); Car* car = a->AddCar(Team::BLUE);
        CarState cs; cs.pos = Vec(0, 0, 0); cs.isOnGround = false; cs.hasJumped = true; cs.isJumping = false; cs.airTime = 0.3f; cs.airTimeSinceJump = 0.3f; car->SetState(cs);
        BallState bs; bs.pos = Vec(0, 3000, 0); a->ball->SetState(bs);
        car->controls.jump = true; car->controls.pitch = -1; a->Step(1);
        car->controls.jump = false; car->controls.pitch = 0;
        for (int i = 0; i < 30; i++) a->Step(1);
        auto s = car->GetState();
        printf("front flip 0.3 s after a jump: vel %.0f %.0f %.0f, flipped %d, spin %.2f %.2f %.2f\n", s.vel.x, s.vel.y, s.vel.z, (int)s.hasFlipped, s.angVel.x, s.angVel.y, s.angVel.z);
    }
    {   // flip reset: car has flipped already; ball rising gently into the wheels
        Arena* a = MakeArena(); Car* car = a->AddCar(Team::BLUE);
        CarState cs; cs.pos = Vec(0, 0, 0); cs.isOnGround = false; cs.hasJumped = true; cs.hasFlipped = true; cs.airTime = 3; cs.airTimeSinceJump = 3; car->SetState(cs);
        BallState bs; bs.pos = Vec(9, 0, -125); bs.vel = Vec(0, 0, 150); a->ball->SetState(bs);
        int maxWheels = 0; bool reset = false;
        for (int i = 0; i < 120; i++) {
            a->Step(1);
            auto s = car->GetState();
            int w = s.wheelsWithContact[0] + s.wheelsWithContact[1] + s.wheelsWithContact[2] + s.wheelsWithContact[3];
            if (w > maxWheels) maxWheels = w;
            if (s.HasFlipReset() || s.GotFlipReset()) reset = true;
        }
        auto s = car->GetState(); auto b = a->ball->GetState();
        printf("ball into the wheels: most wheels touching %d, flip reset %s; car vel %.0f %.0f %.0f, ball vel %.0f %.0f %.0f\n", maxWheels, reset ? "YES" : "no", s.vel.x, s.vel.y, s.vel.z, b.vel.x, b.vel.y, b.vel.z);
    }
}
