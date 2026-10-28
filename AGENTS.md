# Briefing for agents working on AirLabs

Read this first. It says what the project is, how to get acquainted with the code, how work gets
shipped, the rules the owner works by, and what's happening right now.

## Handover: where things stand (2026-10-28, end of the last session)

Read this section first; the rest of the file is the background.

### Live spawn preview and viewer-facing goals (2026-10-29)

- Owner approved the full preview plan. Respawn has an optional master Spawn preview toggle
  (off by default), plus Ball spawns / Ball paths / Goal positions layer switches and New example.
  A movable ImGui window stays visible while editing, including before Start and in the main
  menu. Drag its diagram to orbit. In Freeplay, the same geometry also draws in the world.
- Blue latitude-longitude inner/outer shells light the region allowed by cone, height, distance
  and floor. Amber paths originate at the selected example ball and share play's launch checks.
  The bold amber pass drives purple checked goal candidates. The first purple opening previews
  the chosen ring size; other candidates are small marks. No stale goals survive settings edits.
- `LabPreview.h` caches by geometry/settings/nose/example seed and checks one goal example per
  game-thread update. UI reads immutable snapshots under a mutex. All RocketSim work is queued
  through GameWrapper::Execute, not done on ImGui's thread. Preview has separate randomness and
  restores the global car speed cap. It uses the player's nearest hitbox preset when available.
- New single-value Goal spawn distance: 500-3000 uu, default 1250. Maximum redirect angle:
  5-90 degrees, default 40. No random goal distance range or unchecked fallback. The planner finds
  an exact crossing of the chosen distance sphere; actual passes retry up to four times. Invalid
  settings stop the attempt with a HUD explanation until a relevant setting changes.
- Rings face the viewer every physics tick, including both ring-motion modes. The crossing
  calculation follows the rotating/moving opening and requires actual relative ball movement,
  so camera movement alone cannot score. Open rings accept a touched ball from either side.
- Ball spawn cone and Minimum pass distance are the clarified UI labels. Shared spawn sampling
  now handles exact height bands (including level-only) and cone intersections directly. Live
  Respawn rejects impossible spawn/launch combinations rather than silently using a looser fit.
- Local compilation, labworld and legacy boostlab checks passed. Tests cover live changes,
  lit-region/path validity, fixed goal distance, default checked goals, separate speed-cap state,
  viewer-facing crossings, no camera-only goals, both gravity modes and existing physics.
  Off-game ImGui mesh renders of wide/narrow configurations were inspected. Full Release DLL
  linked with PostBuildEventUseInBuild=false; no automatic in-game reload was run. Final visual
  polish was rendered and inspected, and the final Release DLL build passed. Actual in-game
  appearance/feel still needs the owner. Checkpoint branch: codex/boost-lab-normal-gravity.
- Repeatable preview QA now lives in tools/lab_preview_ui.cpp and tools/render_lab_preview.py.
  The harness uses the real ImGui diagram drawing code and dumps a mesh/font atlas; the Python
  script rasterizes it with NumPy/Pillow. Outputs are under build/qa, not public source assets.

### Current Respawn adjustments (2026-10-29)

- Owner confirmed the ring must appear 1000-1500 uu FROM THE CAR WHEN IT APPEARS, not from the
  predicted ball contact. The planner now measures that range in both motion modes, and the
  geometric fallback also respects it. A checked long shot gets enough time to reach its target.
- Respawn now has gravity OFF for both bodies. Constant/Volley keeps normal gravity. The planner
  reads the live arena's gravity instead of assuming -650, so target prediction matches play.
- Ring size is an opening DIAMETER slider, 250-2000 uu, default 700 (`airlabs_lab_ring_size`).
  Rendering and whole-ball crossing checks use the same radius; changes apply to the current ring.
- Removed post-hit speed displays globally: both the TOUCH speed popup and Last touch HUD line.
  Touch counts, goals and flip-reset feedback remain. No ball/car contact physics was changed.
- Changes are local; Release/x64 compilation and the updated labworld suite pass. All 24 sampled
  passes produced checked 15-40 degree redirects with rings 1000-1500 uu from the car at spawn,
  and all 24 replayed shots scored in both motion settings. Ring creation was reduced from up to
  18.3 ms to 2.7 ms in the sampled checks by projecting empty-arena flight after a RocketSim touch.
  Tests also cover live ring resizing, whole-ball clearance, moving-ring toggle continuity,
  zero gravity in Respawn and normal gravity in Constant, plus existing physics regressions.
  No in-game test yet. Earlier gravity/ring-distance descriptions below are historical.

### Latest: Respawn redirect ring and settings (2026-10-29)

- The owner requested no curve in Respawn, default spawn distance 1500-2000 uu, markers off and
  wait before launch 1.25 s. These are applied locally. Respawn has Nearest/Farthest spawn sliders;
  its boundary expands to fit the range. Constant keeps its bubble and Curve controls. Spawn
  floor fallback preserves the chosen radial distance rather than shortening it by clipping Z.
- The owner approved a new ring on every Respawn ball, roughly 800-1200 uu past an estimated
  reachable contact, with a modest 15-40 degree redirect accounting for speed/height/gravity.
  Both the ring itself AND its motion must have separate toggles (explicit owner clarification).
- `airlabs_lab_redirect_ring` enables the exercise. `airlabs_lab_ring_motion` on: fixed target
  in hidden-world coordinates, rendered relative to the car exactly like the ball. Off: fixed
  offset from the car. Switching motion preserves the displayed position; crossing detection
  uses the same selected coordinates, including movement of the car during the tick.
- `RedirectRing.h` predicts a meeting point, estimates approach reachability, probes glancing
  contacts through RocketSim and forecasts the outgoing ball to place the ring. The radius is
  350 uu; the whole ball must pass through in the planned direction after a touch. A goal turns
  the ring green, flashes GOAL and increments Redirects. The shot has up to 3.5 s after the last
  touch; bubble/floor replacement is suspended during that shot, then the next ball/ring appears.
  Restrictive settings may get a geometric fallback; approach reachability is an estimate, not
  proof of a steerable controller trajectory. Standard-field visual clamping still applies;
  large-range exercises are intended for the space map.
- Local Release/x64 compilation and labworld/legacy boostlab suites pass. Across 24 sampled
  Slow/Medium/Fast passes and both motion choices, all 24 plans used checked RocketSim touches,
  all had 15-40 degree turns, and all 24 replayed touches reached their rings. Setup took up to
  5.3 ms in that check. Motion/toggle continuity, scoring in both coordinate systems, whole-ball
  clearance, spawn range/floor, no Respawn curve and existing physics regressions passed.
  No in-game visual or player-reachability check yet; do not release. Use Back to the defaults
  for saved old settings. Checkpoint branch remains codex/boost-lab-normal-gravity.

### Latest experiment: keep vertical drift through Volley flips (2026-10-29)

- The owner approved trying flip braking/push in a frame moving with the car's vertical speed
  at flip start. Constant/Volley only: `airlabs_lab_keep_flip_drift` defaults to 1, with a
  **Keep vertical drift: ON/OFF** chip under Flips and speed. Off, and all Respawn flips, use
  normal RocketSim flip braking. The invisible-floor Respawn idea was discussed, not built.
- RocketSim's car latches its vertical velocity on an accepted flip. Its flip push speed scaling
  subtracts that reference's component along the nose; horizontal velocity remains relevant.
  Vertical damping and its sign check use `vZ - reference`, then add the reference back. The
  reference is fixed during the flip, never follows the ball, and is cleared on momentum resets,
  including a boundary bounce during Infinite flips. No velocity adjustment is made to the ball.
- This is deliberately different from field-relative match flips. Normal gravity, absolute caps
  and ball drag remain enabled as before. Caps/drag can still make fast and slow encounters differ;
  do not promise full equivalence or exact Rocket League behaviour for this experiment.
- Horizontal push direction, rotation, cancels and contacts still run through RocketSim. Its
  push uses the normalized horizontal nose direction; near-vertical cars can still get horizontal
  push. The flip log now includes the captured vertical drift or says normal flip braking.
- Source is in the local build folder. Release/x64 MSVC compilation and expanded labworld tests
  passed. With caps/drag off to isolate the change, +/-6000 uu/s vertical drift matched the slow
  encounter within 0.02 uu/s and 0.004 uu over 0.7 s (front/back flips, level/tilted cars, cancels).
  A roof flick registered one touch in both frames, with relative velocity error below 0.02 uu/s.
  Disabled-option/Respawn parity, default caps during flips, Infinite-flip bounce reset and existing
  gravity/hit/reset/bounce tests passed. No in-game result yet; the musty report remains open.
  Checkpoint branch: codex/boost-lab-normal-gravity. No release/server change.

### Latest change: normal gravity and default speed limits (2026-10-28)

- The owner approved normal gravity for BOTH simulated bodies and speed limits ON by default.
  `LabWorld::ApplySettings` now uses RocketSim's -650 uu/s² gravity. Car cap 2300 uu/s, ball cap
  6000 uu/s and normal ball drag are on by default in the plugin. The existing toggle can still
  disable caps and ball drag; gravity remains on. Older zero-gravity descriptions below are history.
- The visible car is still pinned. A waiting ball now keeps its falling velocity instead of
  having it zeroed each tick. Launches, bounces and new-ball speed resets retain the lab's rules.
- A second vendored RocketSim change keeps a stationary ball active in THE_VOID; its upstream
  zero-velocity sleep shortcut otherwise prevents gravity from acting on a ball placed at rest.
  Other game modes retain the upstream sleep rule. Documented in thirdparty/RocketSim/AIRLABS.md.
- Local Release/x64 MSVC compilation (`/t:ClCompile`) passed. The RocketSim-backed labworld_test
  suite passed, including new free-fall/countdown, both caps and no-contact backflip checks, and
  existing hit, boost, flip reset and bounce checks. After 1 s without drag, both bodies have
  vertical velocity -650; their relative displacement is below 0.01 uu. In a rising no-contact
  backflip, the ball's velocity matches the coasting control every tick while car vertical speed
  changes. This confirms the mechanism off-game, not the exact cause of the owner's last flip.
- Changes are in the owner's local build folder. No release or server-version change. Awaiting
  an in-game rebuild/test with normal gravity and Speed limit: ON; the musty bug is not yet closed.

### State of the code

- `main` at commit 8523b66 is what the owner last built and played. It builds in Visual Studio,
  with RocketSim compiled into the plugin.
- **The Boost Lab runs on RocketSim** (`AirLabs/LabWorld.h`, `thirdparty/RocketSim`). The game car is
  a puppet: RocketSim flies the car and the ball in an empty arena with normal gravity. Each tick the game
  car gets RocketSim's rotation and is held in place, and the game ball is put where RocketSim's ball
  is relative to the car.
- **Flips are RocketSim's.** The owner's `bakkesmod.log` confirmed that Rocket League itself will NOT
  flip a car held in mid-air ("the game said the car had a flip but didn't flip it"), so don't go
  back to the game's own flips.
- While the lab runs, the game's ball is frozen (`SetbFrozen(1)`) with `bCollideActors` 0, and the
  game car is told its flip is used up. Both are restored when the lab stops or a normal run starts.
- Diagnostics are on (`airlabs_lab_debug` 1, two debug lines on the HUD). They log to the F6 console
  and `bakkesmod.log`:
  - "the GAME moved its ball";
  - "the GAME turned the car";
  - "RocketSim's ball jumped" (relative speed above 6000 uu/s within one tick).

### Open bug #1 (top priority): the ball "shoots upwards really fast" on a musty or backflip

**The owner's report:** on a musty or backflip, at any distance, without touching the ball, it
shoots upwards very fast. Earlier, about every flip: "the ball goes flying".

**The owner's rule for what should happen:** "it should only fling with normal flick mechanics when
the roof of my car is touching as I rotate from the flip." That's real Rocket League: a musty is a
backflip whose rotation sweeps the roof or back of the car into the ball.

**Owner's clarification (2026-10-28, takeover):** ball cam was ON. The ball came back because it
hit the bubble boundary. That return is not evidence of a camera-only swing. Check the ball's
hidden-world velocity separately from the car's flip impulse and the boundary bounce before
choosing a fix. The owner rebuilt and reproduced a no-contact backflip; the relevant final flip
is at 16:36:27 in the local BakkesMod log (see evidence below).

**Evidence so far:**
- **New in-game reproduction, 16:36:27 (owner identifies the LAST flip as no-contact):** at flip
  detection the ball is 393 uu away, moving at 522 uu/s relative to the car. At +0.7 s it is
  1659 uu away, moving at 2417 uu/s relative to the car, including +2362 uu/s vertically. Car
  speed 1355, ball speed 2728; counted touches since detection 0; cumulative bounces 9, balls 1.
  This establishes a real change in simulated relative motion, so a camera-only explanation is
  insufficient. It does NOT establish a ball impulse: the starting individual velocities and
  bounce count were not logged. Touch counting also has a cooldown and starts after the flip's
  first step. Next isolate ball velocity changes from car velocity changes and lab rule events.
  In particular, RocketSim Car.cpp lines 786-791 damps the CAR's world-Z velocity during a flip;
  a rising car losing upward speed would leave an untouched ball moving upward relative to it.
  This is a source-backed hypothesis, not yet the confirmed cause of this reproduction.
  The rebuilt log still contains 5-20 uu GAME-moved warnings; the earlier bookkeeping fix alone
  has not eliminated them. Do not treat these warnings as proof of an extra ball impulse.
- The owner's log from the diagnostics build (16:11-16:16 that day) shows no "turned the car" and no
  "RocketSim's ball jumped".
- **"The GAME moved its ball" fired hundreds of times, but it's a false alarm.** The amounts match one
  tick of the ball's own motion (7, 13 or 20 uu at 800, 1600 or 2400 uu/s), because
  `ball.GetLocation()` read straight after `SetLocation()` still returns the previous tick's
  location. **Fixed in commit after d178748 (compiles; not yet run in game):** `LabTick` now stores
  `labPlacedBall = Add(pinLocation, rel)` (the location set) instead of
  `labPlacedBall = ball.GetLocation()`.
- **Off-game:** 400 runs of 20 s of random realistic inputs (flips, mustys, boost, throttle, air roll,
  handbrake) never made the ball jump. In 600 runs, the ball's speed never changed in a tick without a
  hitbox hit, apart from scheduled launches. A backflip gives a 533 uu/s push backwards (correct) and
  leaves the ball alone. **So RocketSim isn't flinging the ball.**

**Hypotheses, most likely first:**
1. **The camera.** The game car always has zero velocity. Rocket League's air car-camera follows your
   direction of travel; with zero speed it may follow the car's rotation instead. A backflip turns the
   car through a full rotation in about 0.6 s, so the view whips round and the ball appears to shoot
   off, then comes back. The owner has since confirmed ball cam ON and a return caused by a
   boundary bounce, so this explanation is unconfirmed and the return does not support it.
   - Possible fixes: give the game car a velocity equal to RocketSim's car velocity, so the camera
     behaves as in a match, and place the car at pin - velocity x tick before each physics step so it
     still ends up at the pin. Watch for jitter. Or read and steer the camera (`CameraWrapper`).
2. **A lab rule.** The backflip pushes you back, the ball reaches the bubble's edge, and a bounce
   (Constant) or a new ball (Respawn) relaunches it. Check the bounce and ball counts around a flip.
3. **Physics below the 6000 threshold.** The per-flip log is now in (compiles; not yet run in game): when RocketSim's
   `CarState.isFlipping` goes true, log the ball's distance and its velocity relative to the car, and
   again 0.7 s later, with touches, bounces and balls since. Fields to add to `AirLabs.h`:
   `double labFlipLogAt; Vector labFlipRelBefore; int labFlipTouchesBefore;`.

**How to read the owner's log yourself:** when the session is linked to the owner's PC, the
BakkesMod folder is mounted (`~/mnt/bakkesmod/bakkesmod.log`; grep for `[AirLabs]`). Ask the owner
to reproduce the bug, then read it directly, so they don't have to copy anything.

### Other open items for the Boost Lab

- **The ball's spin isn't shown:** the game ball's angular velocity is set to 0. Mirroring
  RocketSim's needs the game's angular-velocity sign: learn it by comparing the ball's rotation from
  one tick to the next.
- **The game's own flip reset sound and burst (v2.66) can't play:** they need the game's wheels to
  touch the game's ball, and its collisions are off. Find another trigger, or add our own cue. "FLIP
  RESET" text shows on the HUD.
- **Free air roll:** if the game gives handbrake plus yaw instead of roll, `LabWorld::Step` turns yaw
  into roll. Not confirmed in game.
- **Before any release:** turn the debug lines off by default, quieten the logging, and decide
  whether to remove the old hand-written physics in `BoostLab.h` (`LabStep`, `LabHit`,
  `LabFlipImpulse`, kept for reference and their tests).
- **The Boost Lab hasn't been released.** The source's version string says 2.8.2; 2.8.1 is live. Don't
  release without the owner's go, and bump `LATEST_VERSION` on the server only after bakkesplugins
  shows the new version.
- After the 1:1 work: Redirect, then Intercept, Shadow and Dock, the simulated player, Dribble, and
  the rest (see "The plan" below). The bot probe (`airlabs_lab_botprobe`) is still waiting for an
  in-game test.

### Working with the owner (learned this session)

- The owner builds on Windows in the x64 Native Tools prompt, from the project folder in their
  Downloads folder:
  `msbuild AirLabs.sln /p:Configuration=Release /p:Platform=x64`.
  That folder is a plain copy, not a git checkout. Changed files were sent there as a `.tgz` and
  written over the old ones with `tar xzf file.tgz -O path > path`, because deleting files on the
  owner's PC needs their permission, and a plain `tar x` deletes before it writes. New files and
  folders extract normally.
- The owner tests in game and reports what they feel; they want Rocket League behaviour **1:1**. Say
  plainly what's simulated versus genuine, and what couldn't be checked off-game.
- For anything big, the owner wants the design discussed before building. Explain ideas
  conceptually, in plain words, with the numbers alongside.
- The owner may run low on usage credits: checkpoint often (commit, update this file), and keep
  reports short.
- **The off-game test rigs from the last session are gone** (they lived in that session's temporary
  workspace):
  - a simulation that compiled `AirLabs.cpp`, `Leaderboard.cpp` and `TrainerWindow.cpp` against the
    BakkesMod SDK headers, with stubbed SDK functions and a simulated car;
  - an ImGui screenshot harness for the window.

  To rebuild the simulation: get the SDK headers (BakkesModSDK on GitHub), stub what the linker says
  is missing, and link RocketSim, which compiles with g++:
  `g++ -std=c++20 -O2 -w -c` each `.cpp` under `thirdparty/RocketSim`.
  `tools/labworld_test.cpp`, `tools/boostlab_test.cpp` and `tools/core_test.cpp` need no SDK.

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
- the game car is always told its flip is used up (`bJumped` and `bDoubleJumped` 1,
  `MaxTimeForDodge` 0) so it never flips on its own, and the game's ball is frozen (`SetbFrozen(1)`)
  with `bCollideActors` 0 while the lab runs. Both are restored when the lab stops. This came from the
  owner's first test, where "the ball goes flying" on every flip. Off-game RocketSim flips were
  normal, so the likely cause was the game's own car and ball colliding (RocketSim lets them overlap
  by a fraction of a uu during a touch), or the game doing its own flip;
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
   - whether flipping is fixed (the frozen, non-colliding game ball and the game never flipping on
     its own);
   - the game's own flip reset sound and effect can't play with the game ball not colliding. Find
     another way to trigger it, or play a cue of our own;
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
