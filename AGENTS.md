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
- **Touches are Rocket League's hit** (RLUtilities' measured model): an inelastic collision with
  friction, using how fast the touching point of your car (including its spin) meets the ball, plus
  Psyonix's extra push. The car is treated as not being spun by the hit itself (no car inertia term
  yet): the one known simplification.
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

**Waiting on in-game tests by the owner:**
1. Flips and flip resets: the HUD debug line and the F6 console log every jump press and say whether
   the game or the lab does the flips. (Before this build nothing logged at all, cause unknown.)
2. `airlabs_lab_botprobe`: can a bot car be spawned and held in place? Decides the simulated player.
3. The car's hitbox and wheels as read from the game (logged when the lab starts), the navball's look,
   and whether the car twitches on touches.

**The plan, in order** (agreed with the owner; Phase 1 is done):

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
