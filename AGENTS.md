# Briefing for agents working on AirLabs

Read this first. It says what the project is, how to get acquainted with the code, how work gets
shipped, the rules the owner works by, and what's happening right now.

## What AirLabs is

A BakkesMod plugin for Rocket League that trains aerial car control. The car is pinned in mid-air
(on a custom space map, or the standard field) and you aim it at a ball: **Flick** (snap onto
targets), **Track** (hold your nose on a moving ball: the mode we recommend most), **Hybrid** (flick,
then track) and **Chase**. Presets give everyone the same one-minute run, so scores compare, and a
public leaderboard (a Cloudflare Worker + D1 database, with a web page) keeps every player's best run
and its replay. It's published on bakkesplugins.com as plugin **904**.

There's also the **Boost Lab** (a test, not released yet): flying at the ball with real momentum.
See "Right now" below.

## Getting acquainted with the code

Read in this order:

1. **`README.md`**: the player's view of everything (modes, presets, scoring, leaderboard, Boost Lab,
   console commands). It's kept in sync with the code, so it's the quickest full picture.
2. **`AirLabs/TrainerCore.h`**: the game-free core. Geometry, Rocket League's air-control physics
   (community-measured), presets, ball paths, target spawning, scoring. Everything here is testable
   off-game; it needs a `Vector` type with `X, Y, Z` defined first.
3. **`AirLabs/BoostLab.h`**: the Boost Lab's physics, also game-free (relative motion, the bubble,
   reachable launches, Rocket League's ball-car hit, flips, flip resets). The comment at the top
   explains the whole model.
4. **`AirLabs/AirLabs.h`**: the plugin class; its member comments map out all the state.
5. **`AirLabs/AirLabs.cpp`**: the plugin. `onLoad` registers cvars and commands; `OnPhysicsTick`
   (hooked to `Car_TA.SetVehicleInput`, once per 120 Hz physics tick) pins the car and runs the
   drills; `LabTick` runs the Boost Lab; `Render(CanvasWrapper)` draws the in-game HUD.
6. **`AirLabs/TrainerWindow.cpp`**: the ImGui window (Play, Boost Lab, Stats, Leaderboard, Assists,
   Help pages). **`AirLabs/Leaderboard.cpp`**: uploads, boards, replays, updates, names.
7. **`AirLabs/Replay.h`**: the replay format (`ALR1`) shared with the server.
8. **`server/README.md`**, then `server/src/index.js` (API), `server/src/verify.js` (replays a run
   through the plugin's own rules), `server/src/page.js` (the web page).

Build and test:

```
# the plugin (Windows, Visual Studio 2022, BakkesMod installed), in the x64 Native Tools prompt:
msbuild AirLabs.sln /p:Configuration=Release /p:Platform=x64

# off-game tests (any OS with g++):
g++ -std=c++20 -O2 -IAirLabs tools/core_test.cpp -o core_test && ./core_test
g++ -std=c++20 -O2 -IAirLabs tools/boostlab_test.cpp -o boostlab_test && ./boostlab_test

# the server (Node 22):
cd server && node --test test/*.test.mjs
node test/localserver.mjs local.db 8787   # the Worker locally, SQLite standing in for D1

# after changing presets or rules in TrainerCore.h, regenerate the server's copy (the deploy fails if stale):
g++ -std=c++20 -O1 -IAirLabs tools/dump_core.cpp -o dump_core && ./dump_core > server/src/core-data.js
```

Earlier sessions also used an off-game simulation (the plugin's real `.cpp` files compiled against
stubbed SDK calls, driving a simulated car) and an ImGui screenshot harness for the window. Those
live outside the repo; rebuild something similar if you need end-to-end checks without the game.

## How work ships

- **Plugin releases:** the owner builds the DLL on their PC, a release ZIP is made (the DLL plus
  `data/AirLabs/`), and the owner uploads it to bakkesplugins. Players update in-game: the plugin
  asks the server for the newest version and shows an Update button that runs `bpm_install 904`.
- **Bump `LATEST_VERSION` in `server/src/index.js` only after bakkesplugins shows the new version.**
  Updating installs whatever bakkesplugins has.
- **The server deploys on every push to `main`** (`.github/workflows/deploy-leaderboard.yml`: tests,
  D1 migrations, `wrangler deploy`). The Cloudflare token lives in GitHub secrets only.
- Commit messages explain what changed and why, in plain words.

## The owner's rules

- No personal names in public material: the repo, URLs, published text. (This repo is public.)
- Secrets stay in GitHub secrets; never in files or logs.
- Never force-push without the owner's explicit approval.
- On the owner's PC, don't delete files without asking.
- Plain language everywhere players read: short sentences, no jargon. Explain ideas conceptually;
  the owner likes understanding *why*, not just *what*.
- Be direct, test before shipping, and say plainly what couldn't be checked off-game.
- Talk it through before building anything big: the owner often wants the design discussed first.

## Right now

**Released:** 2.8.1 is live on bakkesplugins. Real Rocket League ranks (1s, 2s, 3s) on the boards,
saved bests uploaded, an update check and button, the laser options, and your own spot pinned on
the leaderboard. AirLabs' own rank system was removed: the leaderboard is the measure now.

**In the source, not released (2.8.2):** an Input column (controller or keyboard) on the boards,
Change name (with the server stopping anyone taking another player's name), a "play upside down and
sideways" tip, the pinned leaderboard row as a full row with Watch, and the Boost Lab (test).

**Boost Lab: what it is.** Your car stays pinned on screen, but in a hidden world it really moves:
boost pushes it along its nose (991.7 uu/s², community-measured), letting go keeps you drifting, and
the ball is drawn where it is *relative to you*. Gravity is left out because it pulls car and ball
equally, so it doesn't change their relative motion. Key choices, each made after testing:

- **Respawn** (a ball appears, shows an arrow for where it'll go, launches; touch it to keep it)
  and **Constant** (it never stops; at the bubble's edge it bounces back in).
- **A bounce is a fresh start:** the ball comes back at the set speed and your own speed resets.
  (Before, your hidden speed sat at the 2300 cap, boosting did nothing, and bounces came at you at
  3100.)
- **Every launch and bounce is reachable:** from rest, after a reaction allowance, boosting flat out,
  you could get there before it leaves the bubble; otherwise it goes slower. So fast balls need a
  bigger bubble.
- **Touches approximate Rocket League's hit** (RLUtilities' model): an inelastic collision with
  friction, using how fast the touching point of your car (including its spin) meets the ball, plus
  Psyonix's extra push. Not 1:1 yet: see "Top priority" below.
- **Speed limit option, off by default:** the 2300 cap and the ball's air drag are measured against a
  field, and open space has none, so with it off only relative motion counts and air dribbles can
  last for ever.
- **Flips: the game's own.** Jump reaches the game, which does the flip (real rotation, cancels,
  stalls); the plugin adds the game's push to your momentum. The game tracks your flip; it's only told
  you have one at a new ball or bounce (Real), or after each flip (Infinite). **Flip resets are the
  game's own too:** the real wheels are contact points in the lab, and the real ball sits against
  them, so the game decides the reset and plays its v2.66 flip reset sound and effect. If the game
  won't flip a car held in the air, the lab does flips itself (RocketSim's numbers).
- Floor (the ball can't go far below you), steepest path, curve, Dynamic speed, ranges, run length,
  nose cone, height band; relative prograde and retrograde markers (on the ball = you'll hit it); an
  optional navball; optional dust.

### Top priority: make the Boost Lab 1:1 with Rocket League

The owner's requirement, in their words: "everything about this should be 1:1": a hit that lands on
a certain part of your car rotates it, a touch costs you speed, different parts of the car bounce the
ball at different speeds, and flip resets and musty flicks must be exact. The owner says the current
flip reset "feels janky". **This comes before Phase 2.**

**Where the old hand-written model fell short** (`AirLabs/BoostLab.h`, before RocketSim): the car gets no rotation from a hit
(no car inertia term), the ball has no spin, the contact is a single analytic impulse rather than
Bullet's solver, the wheels are spheres at guessed rest positions with an 8 uu "suspension reach"
(so flip resets are approximate), and the lab's air dribble pinning is our own. The Psyonix push
now matches RocketSim (world-vertical scaled to 35%, then the nose part cut to 65%).

**The plan: run RocketSim as the hidden world.** RocketSim (github.com/ZealanL/RocketSim, **MIT**:
keep its licence notice, credit it in the README) reimplements Rocket League's physics on Bullet,
closely enough that RLGym bots train on it. It has an arena with no geometry, `GameMode::THE_VOID`,
and `RocketSim::InitFromMem({})` sets it up with no mesh files. Each physics tick:

1. Feed the player's `ControllerInput` (throttle, steer, pitch, yaw, roll, boost, jump, handbrake)
   into the RocketSim car's controls and step the arena one tick (120 Hz).
2. Make the game car a puppet: pin its location, set its rotation from RocketSim's car (`rotMat`), so
   air control, flips, cancels and hit-induced spin are all RocketSim's.
3. Put the real ball at pin + (RocketSim ball pos - car pos), and mirror its spin for looks.
4. Lab rules sit on top through RocketSim's state setters: the bubble, launches, bounces (which reset
   the car's speed), reachability, the floor, the steepness.

Settings map onto RocketSim's `MutatorConfig`: gravity 0 (open space; gravity cancels between car
and ball anyway, but the car's speed cap doesn't), `ballDrag` (0 with the speed limit off),
`unlimitedFlips` (Infinite). The car's 2300 cap is hard-coded (`Car.cpp`, around line 195): patch the
vendored copy so the speed limit can be turned off.

Flip resets then come out exactly as in the game: RocketSim's wheel suspension rays hit the ball, and
**3 or more wheels in contact counts as being on the ground**, which restores your jump and flip
(`Car.cpp`, `numWheelsInContact >= 3`). For the game's own flip reset sound and burst (v2.66), mirror
RocketSim's jump and flip state onto the game car (`SetbJumped`, `SetbDoubleJumped`), so when the
game's own wheels touch the real ball it sees the dodge come back. Test this in game.

Building: add RocketSim's `src/` and `libsrc/` (a Bullet subset, about 3 MB of source) to the Visual
Studio project, or as a static library project, and check MSVC compiles it into the plugin DLL. The
owner has approved building and running RocketSim in the cloud session (2026-10-28).

**Proven (2026-10-28, `tools/rocketsim_proof.cpp`):** RocketSim built with CMake and ran in
`THE_VOID` with gravity set to 0 and `ballDrag` 0, after `RocketSim::InitFromMem({}, true)`:
- head-on at 1500 into a still ball (middle of the nose): ball 2129 uu/s, car slowed to 1294, car
  pitching at 2.2 rad/s (the ball sits above the car's centre of mass);
- the same off the front-right corner: ball 1831 going sideways, car 1355, car yawing and rolling;
- a front flip 0.3 s after a jump: +500 uu/s, spin 5.5 rad/s;
- a car that had already flipped, with the ball rising at 150 uu/s into its wheels: 4 wheels in
  contact, flip reset YES, the ball resting on the suspension.

**Done (2026-10-28):** RocketSim is vendored in `thirdparty/RocketSim` (MIT, version 2.2.1,
commit c2baacb; one change for the speed limit, listed in `thirdparty/RocketSim/AIRLABS.md`) and
added to `AirLabs.vcxproj` (warnings off, conformance mode off for its files) and `CMakeLists.txt`.
`AirLabs/LabWorld.h` runs the lab on it:
- controls go in and `Step(1)` runs; the game car shows RocketSim's car's rotation, held in place;
- the lab's rules act through `SetState`, and `LabState` is mirrored each tick, so the HUD, markers
  and navball still work;
- the car preset is the one closest to the game car's hitbox;
- the game car's `bJumped` / `bDoubleJumped` follow RocketSim's;
- a new ball or bounce gives a flip that lasts until used (`hasJumped = false`, which is RocketSim's
  flip-reset state).

RocketSim's axes match the plugin's exactly: `Angle(yaw, pitch, roll)` in radians from the game's
rotator gives the same forward, right and up vectors as `BasisFromRotator`. Its controls also match:
+pitch raises the nose, +yaw turns right, +roll tips right.

`tools/labworld_test.cpp` passes:
- nose hit at 1500: ball 2129, car drops to 1294, car spins at 2.4 rad/s;
- corner hit: ball 1802, 1076 of it sideways, car spin 2.5;
- top-edge hit: the ball goes up;
- boost into the ball: it's touched;
- after a flip you have none; a ball into the wheels: 4 wheels, a flip reset, flip back; the nose
  gives no reset;
- speed limit on: 2300, off: still speeding up (4233 after 4 s);
- Constant: bounces come back at the set speed, reset your speed and give a flip.

The off-game sim ran the whole plugin with RocketSim linked in.

**Still to do:**
1. **The owner builds it in Visual Studio.** RocketSim hasn't been compiled with MSVC inside this
   project yet. If it fails, the usual suspects are `/permissive-` (already off for its files) and
   `NOMINMAX` interplay. bakkesplugins builds from this project too, so it must build cleanly.
2. **In-game checks:**
   - the sticks drive the car the right way (they should; the conventions match);
   - air roll, including free air roll (handbrake plus yaw becomes roll when the game doesn't
     already give roll);
   - whether the game's own flip reset effect plays;
   - how the hits feel.
3. **The ball's spin isn't shown yet** (the game ball's angular velocity is set to 0). Mirroring
   RocketSim's needs the game's angular-velocity sign: learn it by comparing the ball's rotation
   from one tick to the next.
4. The old hand-written physics in `BoostLab.h` (`LabStep`, `LabHit`, `LabFlipImpulse`) is no longer
   used by the plugin. Keep it or remove it once RocketSim is confirmed in game.

**If RocketSim can't be built in (fallback):** port its pieces into `BoostLab.h`:
- the car's inertia: Bullet's box formula from the hitbox, about the car's origin,
  I = m/12 * (ly²+lz², lx²+lz², lx²+ly²), with m = 180 and full sizes l;
- ball spin (inertia 0.4 m r², capped at 6 rad/s);
- the full 3x3 effective-mass contact impulse with friction 2.0 and restitution 0;
- the ball's collision radius of 91.25 (it's drawn at 92.75);
- the extra push at most every other tick;
- real wheel suspension raycasts (`WheelWrapper` gives `GetLocalSuspensionRayStart`,
  `GetSuspensionTravel` and `GetWheelRadius`).

Applying a hit's spin to the game car needs the sign convention between our maths and the game's
angular velocity. Learn it at runtime: compare `GetAngularVelocity()` with the rotation measured
from one tick to the next.

**RocketSim facts already confirmed from its source** (`src/RLConst.h`, `src/Sim/Ball/Ball.cpp`):
- masses: car 180, ball 30;
- car-ball contact: friction 2.0, restitution 0;
- ball: radius 91.25, top spin 6 rad/s, drag 0.03;
- jumps: immediate push 291.67, then 1458.33/s held for up to 0.2 s; a double jump or flip is
  allowed up to 1.25 s after the jump;
- flips: push 500 (up to 1.9x sideways and 2.5x backwards at top speed, backwards times 16/15),
  torque 260 (left/right) and 224 (forward/back) for 0.65 s, vertical speed damped to 35% between
  0.15 and 0.21 s, pitch lock 1 s (+0.3 s);
- extra hit push: the speed between car and ball (capped at 4600) times a curve (0.65 up to 500,
  0.55 at 2300, 0.30 at 4600), along car -> ball with its world-vertical part at 35% and its nose
  part at 65%, at most every other tick.

**Waiting on in-game tests by the owner:**
1. Flips and flip resets: the HUD debug line and the F6 console log every jump press and say whether
   the game or the lab does the flips. (Before this build nothing logged at all, cause unknown.)
2. `airlabs_lab_botprobe`: can a bot car be spawned and held in place? Decides the simulated player.
3. The car's hitbox and wheels as read from the game (logged when the lab starts), the navball's look,
   and whether the car twitches on touches.

**The plan, in order** (agreed with the owner; Phase 1 is done; 1:1 physics comes first):

- **Phase 2, Redirect:** a pass comes in, a target ring hangs in space; touch the ball so it leaves
  towards the ring. Scored on angle error.
- **Phase 3:** Intercept scoring (Respawn, time against a physics par), **Shadow** (stay 300-500 uu
  behind a curving ball: Track for momentum), **Dock** (arrive with almost no closing speed).
- **Phase 4, simulated player:** a bot car that flies by the same rules and really hits the ball, so
  you read its touches (Shadow), and it passes to you (Redirect). Depends on the bot probe.
- **Phase 5:** Dribble, Pinball scoring, a ring course.
- **Later:** field scenarios with gravity, a floor and a goal; scores and leaderboards for the lab
  once the modes feel right.

**Also open:** the bakkesplugins description still advertises the removed rank system and needs
rewriting; 2.8.2 needs a build and release; ideas raised but not picked up yet: an "aim at a spot on
the ball" mode, a clean recording mode for videos.

## Sources the physics relies on

- Air control: smish.dev/rocket_league/aerial_control (torques and drag).
- Ball-car hit: RLUtilities (Sam Mish), Psyonix impulse scale 0.65 / 0.55 / 0.30 at 0 / 2300 / 4600.
- Flips, jumps, car limits: RocketSim (an open-source reimplementation of Rocket League's physics).
- Flip reset indicator: Rocket League patch notes v2.66 (three or more wheels on the ball).
