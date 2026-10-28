# AirLabs (BakkesMod plugin)

Pins your car in mid-air at the exact centre of the arena and trains aerial car
control. You can rotate freely but can't move. There are four modes. Presets give
every player the same one-minute run, so scores can be compared.

| Mode | What happens |
|---|---|
| **Flick** | A ball appears somewhere around you. Turn onto it as fast as you can. |
| **Track** | The ball moves around you and keeps changing direction. Keep your aim on it. |
| **Hybrid** | A ball appears, you flick onto it, then it takes off in a straight line and you track it for 4 seconds. |
| **Chase** | One ball. Point at it and it dashes off in a random direction, then stops. Catch it again, and again. |

**New? Start with Track.** Aerials are about holding your nose on a moving ball while you
fly, and Track trains exactly that. AirLabs opens on Track Control, Medium range: move up
to Standard once you're on the ball about 75% of the time.

## Install

**Players:** install AirLabs from the BakkesMod plugin manager (or
[bakkesplugins.com](https://bakkesplugins.com)), then set a key to open its window. The easy
way: press F2, go to Plugins > AirLabs and click **Set O to open AirLabs**. Or press F6 to open
the BakkesMod console and type:

```
bind O AL_menu
```

Press O and hit START: it takes you to the space map and the run begins. Everything
AirLabs needs, including its window font and the space map, comes with the plugin.

## Build from source (Windows)

You need Visual Studio 2022 (or newer) with the **Desktop development with C++**
workload, and BakkesMod installed (it brings the SDK the build uses).

1. Open `AirLabs.sln` in Visual Studio.
2. Pick **Release** and **x64**, then **Build > Build Solution**.

That's it. The project finds the SDK in BakkesMod's own folder (the same way as the
official plugin template), and after each build BakkesMod's `bakkesmod-patch.exe`
copies `AirLabs.dll` into `bakkesmod\plugins` and reloads it if the game is running.
To load it yourself: `plugin load airlabs` in the F6 console; add that line to
`bakkesmod\cfg\plugins.cfg` to load it on every launch.

From a command line instead: in the **x64 Native Tools Command Prompt for VS**,
`msbuild AirLabs.sln /p:Configuration=Release /p:Platform=x64`.
A `CMakeLists.txt` is included as an alternative build.

The space map and the rank icons are in `data/AirLabs/`. When you build it yourself, copy that
folder into `%APPDATA%\bakkesmod\bakkesmod\data\` (the bakkesplugins install does this for you).

**Coming from Orientation Trainer (1.x)?** AirLabs 2.0 copies your runs, saved presets
and direction grid from `data\OrientationTrainer` the first time it loads (the old
folder is left alone). Unload the old plugin, remove `plugin load orientationtrainer`
from `plugins.cfg`, and delete `plugins\OrientationTrainer.dll`. Commands are now
`airlabs_...` instead of `ot_...`, so redo your key binds.

## The trainer window

Everything lives in the plugin's own window. Open and close it with the `AL_menu`
command; the easiest way is to bind a key once in the F6 console:

```
bind O AL_menu
```

(Or press **Open the AirLabs window** in F2 > Plugins > AirLabs.)

- **Sidebar:** Play, Stats, Leaderboard, Assists and Help, plus the run status and the key hint.
- **Play:** an "Up next" card shows exactly what Start will play (mode, preset, range,
  target size, speed or turns, run length, your best) with a big **START** button, and a
  training tip under it (a new one each time the window opens; **Hide tips** turns them
  off, Help turns them back on). Below that: mode tiles, preset chips (Dynamic opens its
  speed ranges), the Short / Medium / Long range switch, every setting on the same page,
  and a **Save as preset** row at the bottom. Your saved presets sit in a **My presets**
  row under the main preset chips. Pressing Start closes the window so you go straight
  into the run.
- The window opens at 1340 x 940 the first time; drag the corner to resize it and it
  remembers the size.
- **Stats:** Overview, Records, Directions and History.
- The F2 settings tab keeps the old layout under **Classic settings view** as a fallback.

The window font is Inter (SIL Open Font License, see `AirLabs/fonts/OFL.txt`). It's
compiled into the DLL and written to `bakkesmod\data\fonts` when the plugin loads.

## How a run works

1. Open the trainer window (`AL_menu`), pick a mode, a preset and a range on the
   **Play** page, and press **START** (or `bind F5 airlabs_toggle`). START takes you to the
   space map, from the main menu too (see "The space map" below).
2. Your car resets to the centre of the arena with a ball between you and the goal you
   attack. The car is level but turned 45 deg away from the ball, so the ball is on
   screen to the side (Flip Reset's ball sits above, on the goal side). Turn onto it and
   point at it for **1.5 seconds** and the run begins. Looking away resets the countdown.
3. When the minute is up you get your results: score, comparison with your
   personal best, and a breakdown. The car and ball reset straight away, with the
   results still on screen. After a 1-second pause you can turn onto the ball to go
   again. No menu needed. Press Stop (or your `airlabs_toggle` key) to finish.

The ring around the target is the real hit zone: what you see is exactly what counts.

**Best-score marker:** on a preset, the HUD shows your best score while you play, and
whether you're ahead of or behind the pace needed to beat it. The bar under it shows
your score so far (blue), your best (gold line), and where your best would be by now
(white line). Stay right of the white line and you're on course for a new best. Before
the run starts, the start-ball screen shows your best too.

## The space map

AirLabs comes with its own map: open space under a real star sky (NASA's Deep Star Maps), with
no floor, walls or goals to distract you. It's the default: **START** takes you there from
anywhere, the car is held in the air from the moment you spawn, and the start ball appears
straight away. Rocket League's ball-location line (the vertical line under the ball) is hidden
there, since there's no ground for it to point at.

- **Space | Standard**, the switch next to START, picks where START takes you. Space is the
  default; Standard means Freeplay on DFH Stadium (or the Freeplay you're already in).
- The choice is remembered between sessions (`airlabs_map`: 0 = space map, 1 = standard field).
  `airlabs_space` and `airlabs_standard` switch and travel in one go (handy to bind to keys).

## Presets

### Flick (targets don't move, so presets set the turn size)

| Preset | Turns | Fastest possible | Peak spin reached |
|---|---|---|---|
| Small | 15-45 deg | 0.29-0.59 s | 88-177 deg/s |
| Medium | 45-90 deg | 0.51-0.84 s | 152-250 deg/s |
| Big | 90-135 deg | 0.72-1.03 s | 214-307 deg/s |
| Turnaround | 135-180 deg | 0.88-1.19 s | hits the 315 deg/s cap |
| Mixed | 30-180 deg | | |
| Flip Reset | 30-180 deg, **wheels** aim, ball above you, **350 uu away** (target 15.6 deg) | | |

Small flicks never get past about half of max spin, so they're all start-and-stop.
Big flicks hit the spin cap, so they're about committing to full stick and braking early.

### Track

Steady-speed presets:

| Preset | Ball speed | % of max spin | Stick needed | New direction every | Biggest change |
|---|---|---|---|---|---|
| Precision | 15 deg/s | 5% | ~5% | 0.7 s | 90 deg |
| Control | 30 deg/s | 10% | ~10% | 1.0 s | 90 deg |
| Standard | 45 deg/s | 14% | ~15% | 1.3 s | 90 deg |
| Fast | 60 deg/s | 19% | ~19% | 1.6 s | 90 deg |
| Insane | 90 deg/s | 29% | ~26% | 2.0 s | 60 deg |

**Dynamic** (a button in the same row, with its own sub-presets): the ball keeps
speeding up and slowing down, so you have to change how far you push the stick, not
just which way.

| Dynamic | Speed range |
|---|---|
| Low | 15-45 deg/s |
| Medium | 30-60 deg/s |
| High | 45-90 deg/s |
| Full | 15-90 deg/s |

Dynamic direction changes follow the steady ladder for the current speed (more often
when slow), up to 90 deg.

The whole ladder lives in the first quarter of your stick: half stick already spins
the car at about 250 deg/s. If your controller deadzone is near 5%, Precision will
feel on/off rather than smooth.

### Hybrid

Every Hybrid preset flicks 30-180 deg, then tracks for 4 s. Once it takes off, the ball holds that direction for the whole track (Dynamic presets still speed up and slow down).

| Preset | Tracking |
|---|---|
| Control | 30 deg/s |
| Fast | 60 deg/s |
| Insane | 90 deg/s, direction changes up to 60 deg |
| Dynamic Low / Medium / High / Full | same speed ranges as Track Dynamic |

### Chase

One ball for the whole run. Once it's stopped, hold your aim on it for 150 ms and it dashes off
in a random direction, in a straight line, for a random 0.5-1.5 s, then stops and waits for you.
Scored like Track (1000 points per second on the ball), so you lose points when it
leaves you behind at a launch, while you lag during the dash, and while it sits still
without you on it. If you're already on it when it stops, it still waits the 150 ms before leaving.
A ball left waiting 7.5 s counts as a miss and moves on anyway (no points are taken off in Chase).

| Preset | Dash speed |
|---|---|
| Slow | 30 deg/s |
| Medium | 60 deg/s |
| Fast | 90 deg/s |
| Random | a new speed between 20 and 90 deg/s for every dash |

The launch is the hard part: when the ball goes from still to 90 deg/s, even a perfect
player needs about a tenth of a second to see it move, by which time it's more than a
ball-width away.

No mode ever moves the ball faster than **90 deg/s**. All presets use a one-minute run,
0.2 s hold to confirm, 20 ms between targets and 5 s to reach each Flick or Hybrid target
(Chase gives 7.5 s to catch the stopped ball).

## No air roll

A variant of every mode: switch **No air roll** on (Play page, under the modes) and air roll
does nothing during runs, so you turn with pitch and yaw only. Air roll left, air roll right
and free air roll are all blocked; jump and boost stay blocked as usual.

- **Own records.** No air roll runs are saved with their own best scores, beside the normal
  ones. Stats > Records has a **Show No air roll records** button to switch between them.
- **Nose aim only.** Aiming with your wheels needs air roll, so No air roll isn't used when a
  run aims with the wheels.

## Ranges

Every preset comes in three ranges, picked with the Range buttons under the preset row:

| Range | Ball distance | Target |
|---|---|---|
| Short | 400 uu | 13.6 deg |
| Medium | 600 uu | 9.0 deg |
| Long | 900 uu | 6.0 deg |

**Each range is its own competition**, with its own personal bests, leaderboard, progress
graph and skill-profile data, because a closer ball is a bigger target. Stats > Records and
Stats > Overview have Range buttons too. **Flip Reset is the exception:** it always puts
the ball 350 uu away (15.6 deg) and has one set of records.

## Target size = the ball

There's no target size setting. You're on target when your aim is on the ball itself,
at any distance. The target is the ball's angular radius, asin(radius / distance):

| Distance | Target |
|---|---|
| 350 uu (Flip Reset) | 15.6 deg |
| 400 uu (Short) | 13.6 deg |
| 600 uu (Medium) | 9.0 deg |
| 700 uu | 7.7 deg |
| 900 uu (Long) | 6.0 deg |
| 920 uu (max) | 5.9 deg |

It uses a 94 uu radius (the ball is 92.75 uu), so the ring sits just outside the
ball's edge. A closer ball is a bigger target and easier to hit.

## Presets, Custom and records

- **Presets** have fixed settings, so every player plays the same test. The targets and
  ball paths are **random every run**, in every mode, so nothing can be learnt by heart;
  over a one-minute run the luck evens out to a few percent. Preset runs show
  **"Counts toward your records"**.
- **Change any setting and the run becomes Custom.** The settings under the preset
  row always show what the selected preset uses; edit one and the preset becomes
  **"Custom (based on <preset>)"**, starting from the preset's values. **Back to
  preset** returns to it. Plain Custom runs are marked **"Custom: practice only"** and
  don't count toward records.
- **Save a Custom setup as your own preset.** The Save as preset button at the bottom of
  the Play page stays dark until you change a setting; then type a name and press
  it (or Enter). The preset appears in the **My presets** row and gets its own records:
  personal best, run count, last-10 average and a progress graph (they're not on the
  leaderboard, because nobody else has your settings). Like everything else, its targets are random
  every run.
- **Saved presets are locked.** Change a setting while one is selected and the run
  becomes Custom based on it, which you can save as a new preset. That way a preset's
  records always come from the same settings. Delete one with **Delete this preset**
  (click twice to confirm); its runs stay in History.
- A preset run also doesn't count if you skip a target. Ball cam doesn't matter.
- Preset runs saved before 1.6.0 (when ranges arrived) used different targets, timing
  or distances. They stay in History marked "older rules" but don't count toward any
  range's records, because they can't be compared with new ones.
- The laser and aim marker are allowed (they're the equivalent of a crosshair). The
  stick guide only appears in Custom runs.
- Scores are honour-system: the code is public, so a determined person could fake a
  result line. That's true of any offline trainer.

## Settings (on the Play page)

Every setting is on the Play page, with its name and a plain-language explanation on the
left and the control on the right, most important first:

1. **Round:** round length, time per target (time to catch the ball in Chase) and the
   delay between targets.
2. **Ball spawn:** ball spawn height, ball spawn direction (relative to your car) and
   ball distance.
3. **The mode's own settings**, listed below.

Changing any of them makes the run Custom.

**Flick:** smallest/biggest turn, hold to confirm, time limit, delay
between targets, aim with (nose or wheels), must stop on target (and how slow counts
as stopped), weak-spot mode, directions, ball height.

**Track:** ball speed (steady speed, or changing between a slowest and
fastest speed), new direction every, biggest direction change, directions, ball height.

**Hybrid:** the Flick settings (Flick part), then track time after lock-on, speed and
direction changes (Track part), directions and ball height.

**Chase:** dash speed (steady, or random per dash between a slowest and fastest),
shortest and longest dash, how long a stopped ball waits for you, ball height.

**For every mode:** run length (0 = until you stop) and ball distance (300-920 uu).
The target is always the ball, so distance does change difficulty: closer is easier.
920 uu is the most that fits straight up or down from the centre
pin (929 uu to the roof or floor), so the ball is always exactly the distance you set.

**Ball height** is relative to the ground, not your car: Any, Above, Level, Below, or
your own range in degrees above the horizon.

## Scoring

- **Flick:** each target is worth 1000 points at par time, up to 2000 for twice as
  fast; a target you can't reach in 5 s is a miss and costs 500 points (a run never goes below 0). Par comes from physics: the fastest
  possible turn for that angle, plus a human allowance, so big turns aren't punished.
- **Track:** 1000 points per second on target, with partial credit just outside the
  target (fading to zero at twice its size). Maximum 60,000.
  Results also show **Near**: the share of time your aim was within 25 degrees of the ball,
  where the laser and ring turn yellow. It's for feedback only: no points for yellow, since
  25 degrees is several ball-widths away and paying for it would reward roughly pointing the
  right way. (Track, Chase and Hybrid's tracking part.)
- **Hybrid:** flick points plus tracking points for each rep; a missed flick costs 500 points, as in Flick.
- **How good is a score?** The leaderboard says: the Up next card shows your place on the
  preset's board ("#3 of 10"), and the Leaderboard page shows everyone's. (Until 2.8.1 AirLabs
  also gave every score a rank from Bronze to Pro, worked out from a model of simulated
  players. With real players on the boards that guess wasn't needed any more, so it's gone.)

## Stats page

Four sub-tabs:

- **Overview:** your last run with a **Copy result** button, which puts a line like
  this on your clipboard:
  `AirLabs 2.8.1 | Track: Control (Medium) | 41,250 | 69% on target | PRESET`.
  Below it is your **skill profile**: a six-axis radar built from your last 10 counted
  runs on each built-in preset at the chosen range. Every axis shows the real number
  behind it:
  - **Flick speed:** your flick times against par (1.00x = par, higher is faster).
  - **Reaction:** how long before you start turning.
  - **Accuracy:** how far you swing past the edge of the target after reaching it.
  - **Precision tracking:** % on target in Track Precision and Control.
  - **Speed tracking:** % on target in Track Standard, Fast and Insane.
  - **Consistency:** how much your score varies run to run on the same preset.

  Next to it, **Strongest** and **Work on** cards (with a preset to practise) and a
  **Trend** card comparing your last 5 runs with the 5 before. Then your **tracking ceiling** (the
  fastest steady Track preset where you've held 75% on target) and your **speed
  curve** (from your last Track Dynamic run, how well you stayed on at each speed).
- **Records:** pick a mode to see a table of every preset with your best score, number of
  runs and last-10 average, with your saved presets for that mode in a **My presets** table
  underneath. Click a row for its progress graph.
- **Directions:** your recent flick time in each direction from the driver's seat,
  for nose aim or wheels aim. Green = faster than your average, red = a weak spot.
  Bigger turns are scaled so every direction compares fairly.
- **History:** your last 25 runs, the session counter and where the file is saved.

Everything is saved in `%APPDATA%\bakkesmod\bakkesmod\data\AirLabs\`:
`runs.csv` (every run, opens in a spreadsheet; the `range` and `user_preset` columns
say which range and which saved preset a run used), `presets.txt` (your saved presets,
one per line) and `direction_stats_v2.txt` (the grid).
Runs saved by 1.2.0 are carried over. Old Insane scores are kept in History but don't
count as records, because Insane used to allow 180 deg direction changes.

## Leaderboard

The **Leaderboard** page shows everyone's best run on every preset and range, with No air roll
on boards of its own. The same boards are on the web at
<https://leaderboard.airlabs.workers.dev>.

- **Post my runs** is on by default: your preset runs post under your Rocket League name.
  Whenever a run beats your best on that board, it goes up with its replay. Custom runs, saved
  presets and runs with a skipped target never post. Turn it off with **Stop posting** on the
  Leaderboard page (or `airlabs_lb_post 0`). While it's off, your best run on each board is kept
  (in `data/AirLabs/pending`), and those go up, one every 25 seconds or so, if you turn it back on.
- **No internet when a run ends?** It waits in `data/AirLabs/pending` and AirLabs tries again by
  itself (and soon after the game starts), or straight away with **Retry now** on the
  Leaderboard page. The Leaderboard page shows how many runs are waiting.
- **Watch** plays any run on the board in the game: your car and the ball follow the replay
  tick by tick, with the score building as it did in the run. Stop (or your `airlabs_toggle`
  key) ends it. Outside Freeplay, Watch loads the map first.
- **Your saved bests go up too.** Each time AirLabs loads (with posting on), it sends your best
  score on every preset board from your own saved runs (`runs.csv`), from 2.5.0 on (presets and
  scoring have been the same since then). Runs from before replays were recorded show
  **No replay** instead of a Watch button. Only scores higher than what the board already has
  for you change anything.
- **Rocket League ranks.** Next to each name are that player's ranks in 1s, 2s and 3s, as their
  game reported them to AirLabs (and kept from the last time it did). A dash means AirLabs
  hasn't seen that rank; players without a rank are on the boards like everyone else.
  `airlabs_rank` prints what the game reports.
- **Your name** on the boards is your Rocket League name, or one you pick with **Change name** on
  the Leaderboard page. A name another player already uses can't be taken.
- **Input:** each run shows whether it was played on a controller or a keyboard (AirLabs tells
  from the stick: a keyboard only ever pushes all the way or not at all).
- **Max 60,000.** Track and Chase pay 1000 points a second on the ball, so a one-minute run
  tops out at 60,000; the Up next card and those boards say so. Flick and Hybrid have no fixed
  maximum.
- **Every run posted from the game comes with its replay.** The plugin records the whole run, one sample per physics
  tick: the car's rotation and where the ball was, which is everything a score depends on. Anyone
  can watch any run, so a run that doesn't add up is plain to see. The server also plays each run
  back through the plugin's own rules and keeps that score with the run for reference.
- **What's stored:** your Rocket League name, platform, your platform account id (used only to
  tell players apart; never shown), and your best run per board with its replay. The first time
  you post, the server gives this install a key (`data/AirLabs/leaderboard.txt`), so nobody else
  can post as you. Turn posting off any time with **Stop posting**.

**Updates.** When AirLabs loads, and whenever you open its window (at most every 5 minutes), it
asks the leaderboard server which version is on bakkesplugins. The sidebar shows the result:
*Checking for updates...*, *No update available* (with **Check again**), *Couldn't check for
updates* (with **Try again**), or, when a newer version is out, an **Update** button. That has
BakkesMod install the new version (`bpm_install 904`, as the website's install button does) and
reload it.

The server is a Cloudflare Worker with a D1 database, in `server/` (see `server/README.md`).

## Boost Lab (test)

Fly at the ball with real momentum. It's on its own page in the window (**Boost Lab**), and is a
test: there are no scores or leaderboard yet.

**The physics is RocketSim** (`thirdparty/RocketSim`, MIT licence): a reimplementation of Rocket
League's physics, close enough that machine-learning bots train on it. It runs your car and the ball
in an empty arena: Respawn has no gravity; Constant has normal gravity. It handles boost, air
control, flips and jumps, the ball-car hit (where the
ball lands on your car decides how it leaves, how much speed your car loses and how your car spins),
the wheels' suspension against the ball, and flip resets (three or more wheels on the ball). The
game car is a puppet that shows RocketSim's car, held in place; the real ball is drawn where
RocketSim's ball is relative to it. The lab's rules sit on top (`AirLabs/LabWorld.h`, using
`AirLabs/BoostLab.h`). Tests: `tools/labworld_test.cpp` (with RocketSim) and `tools/boostlab_test.cpp`
(the rules). Head-on at 1500 into a still ball, the ball leaves at 2129, your car drops to 1294 and
pitches at 2.4 rad/s; off the front corner the ball leaves at 1802, 1076 of it sideways.

- **How it moves.** Your car stays pinned on screen, but underneath it moves in a hidden world:
  holding boost pushes it along its nose at 991.7 uu/s² (air throttle: 66.7 uu/s²), up to 2300 uu/s,
  and letting go doesn't stop you. Turning doesn't change where you're going; only boost does. The
  ball has its own velocity, and is drawn where it is relative to you. Gravity pulls the car and
  the ball down at 650 uu/s² in Constant. Respawn has gravity turned off for both. Your visible
  car stays pinned in either mode.
- **The bubble.** In Constant, pick Short 500, Medium 800, Long 1200 uu or a radius up to 2000.
  Respawn's boundary expands to fit its spawn range. On the standard field the displayed ball is
  also limited by the arena floor, walls and roof; the large spawn range is best used on the space map.
- **Respawn:** new balls appear **1500–2000 uu away** by default (Nearest/Farthest spawn change
  the range), within your nose cone and height band. They wait **1.25 s**, then launch with no curve.
  Your speed resets with each new ball. Without a ring, a new ball comes 1.5 s after the last touch.
- **Redirect ring (Respawn, on by default):** a target appears with each ball. It is planned from
  the pass's position, speed and direction, with an estimated reachable meeting point. RocketSim
  checks a glancing touch. The planner then projects the outgoing flight with Respawn's zero
  gravity and ball drag,
  looking for a 15–40 degree redirect. The ring appears **1000–1500 uu from your car**, measured
  when it spawns. **Ring size** changes the full opening width from 250 to 2000 uu (700 by default);
  the whole ball must fit through after a touch to score. The ring turns green and the HUD counts
  redirects. A touched ball has at least 3.5 s after the latest touch to reach it (longer when the
  checked shot needs more time), with boundary/floor
  replacement suspended during the shot. Restrictive settings may use a simpler fallback target;
  the approach is an estimate, not a guarantee that every flight path is reachable.
- **Ring motion (separate toggle):** ON keeps the ring fixed in the hidden world and shows it
  relative to your moving car, like the ball. OFF keeps the ring at the same offset from your car.
  Scoring follows the selected motion. Switching preserves its displayed position. Both the ring
  and its motion have their own switches, so either behaviour can be tested.
- **Constant:** the ball never stops. At the edge of the bubble (or the floor) it bounces back
  inwards, and each bounce is a fresh start like a new ball: it comes back at the set speed and
  your own speed resets. (Earlier, your hidden speed stayed at the 2300 cap, so boosting at the
  ball could do nothing, and a bounce could come at you at the set speed plus 2300.) It can't be lost.
- **Always reachable:** every launch and bounce is checked: a car starting from rest, after 0.4 s to
  react and turn (0.2 s in Respawn, where the arrow shows the way first), boosting flat out up to
  2300, must be able to reach the ball before it leaves the bubble. If no direction works at the set
  speed, the ball goes slower (15% at a time) until one does. So fast balls need a bigger bubble:
  set to 1400, they typically go about 620 in a 500 bubble, 860 in 800, 1000 in 1200, 1400 in 2000.
- **Floor and steepness:** the ball never goes more than the floor distance below your car (300 uu
  by default; it bounces off it), and launches and bounces travel no steeper than 45 degrees from
  level by default (curving balls then curve left and right only).
- **Speed limit (on by default):** with it on, the car tops out at 2300, the ball at 6000, and the
  ball has Rocket League's air drag (about 3% of its speed a second), so a ball pushed just past your
  top speed loses speed in the air (the HUD says when it's faster than you). Turning the limit off
  removes both caps and ball drag. Gravity remains off in Respawn and on in Constant.
- **Flips** (Off / Real / Infinite; Real by default): jump with the stick flips, jump without it is a
  double jump, all by RocketSim's rules (Rocket League's). Real gives one flip with each new ball and
  each bounce (it lasts until you use it, like a flip reset), and one for every **flip reset**: three
  or more wheels on the ball, decided by RocketSim's own wheel suspension. The game itself never flips the
  car (it's always told its flip is used up), and the game's own ball is frozen with its collisions
  off while the lab runs (as before a kickoff: visible, its own physics stopped), so only RocketSim
  moves things. That also means the game's own flip reset sound and effect don't play yet. Infinite: as many flips and double jumps as you like.
- **Keep vertical drift (Volley/Constant only, on by default):** a test option for long rallies.
  A flip keeps the upward or downward speed your car had when it started, and applies flip braking
  to changes around that speed. Its push uses that same vertical reference; horizontal travel still
  affects the push normally. Rotation, cancels and ball contacts are handled by RocketSim. This
  deliberately changes match flip braking. Speed limits and ball drag still apply, so fast encounters
  can still differ when those intervene. Switch it off to compare normal RocketSim flips. Respawn
  always uses normal flip braking. A new ball or bounce clears the captured drift.
- **Debug line (on for now, `airlabs_lab_debug`):** jump presses, flip resets, whether you have a
  flip, the car RocketSim is using (the preset closest to your car's hitbox: Octane, Dominus, Plank,
  Breakout, Hybrid or Merc), wheels on the ball and your car's spin.
- **A Respawn ball you keep touching stays:** a new one comes 1.5 s after your last touch (or when it
  reaches the edge), so you can dribble it.
- **Never straight at you.** Launches and bounces pick a direction whose path (as you see it) passes
  at least the pass distance (250 uu) from your car's centre and crosses at least a bubble radius
  of the bubble. From the edge that's 18-60 degrees off straight in.
- **Ball speed:** Slow 400, Medium 800, Fast 1400 uu/s, any speed, or Dynamic (a random speed for
  each launch or bounce). **Curve (Constant only):** a ball flying on its own turns at this many degrees a second,
  picking a new way to curve every second. **Run length:** a run ends after this long (boost to go again).
- **On screen:** green markers show where you're heading relative to the ball (on the ball = you'll
  hit it) and the opposite way (boost there to slow down). A waiting ball has a ring (yellow and
  pulsing; a flying ball has none), and there's an arrow at the screen's edge when it's off screen. Optional: a **navball**
  (every direction on one disc: the middle is your nose, the rim behind you; with the horizon, the
  ball, the markers and your own drift as a blue dot), and **dust** that streaks past as you move.
- Boost reaches the game, so you see and hear it (it never runs out); jump is blocked.
- `airlabs_lab_botprobe` (a test for a later simulated player): spawns a bot car and holds it next
  to you for 20 s, logging where it is.

## Assists page

None of these affect whether a run counts.

- **Laser** out of the nose (or the wheels in wheels aim). Off by default, and best left off:
  there's no laser in a real match. It's on the Play page too (**Laser: OFF**, under No air
  roll). **Stop at the ball** (on by default) ends it at the ball's distance, so its tip touches
  the ball when you're on it; turn that off to set your own **Laser length**. Colour: either
  **Changes with your aim** (red off target, yellow close, green locked on) or **One colour**
  you pick. Toggle: `bind L airlabs_laser_toggle`.
- **Aim marker** (white cross). Off by default.
- **Stick guide** (Custom runs only): shows the input that gets you onto the ball
  fastest, including when to brake. Options: inverted pitch, brake cue timing.
- **Readout:** live stick %, car spin speed, ball speed (both as % of max spin),
  measured physics tick rate. Works in Freeplay even without a run.
- **Direction grid on screen** during Flick and Hybrid.
- **Block jump and boost** in Custom runs (preset runs always block them).

## Fair timing

Rocket League's physics runs at 120 ticks per second on every PC. Runs are timed by
counting ticks, so frame rate doesn't affect scores and pausing the game pauses the
run. The plugin measures the tick rate during the 1.5-second start and falls back to the
PC clock if it ever looks wrong; the Readout shows which is in use.

## Console commands

| Command | What it does |
|---|---|
| `AL_menu` (or `airlabs_menu`) | Open or close the trainer window: `bind <any key> AL_menu` |
| `airlabs_space` | Go to the AirLabs space map (no floor; the car is held in the air from spawn), and make it where START takes you |
| `airlabs_standard` | Go back to the standard field (DFH Stadium Freeplay), and make it where START takes you |
| `airlabs_map 0` / `airlabs_map 1` | Where START takes you: the space map (default) / the standard field |
| `airlabs_toggle` | Start, or stop the run in progress |
| `airlabs_start` / `airlabs_stop` | Go to the start ball / stop |
| `airlabs_mode_toggle` | Cycle Flick, Track, Hybrid |
| `airlabs_preset_next` | Next preset in the current mode (including Custom) |
| `airlabs_next` | Skip a target (the run then won't count) |
| `airlabs_laser_toggle` | Laser on/off |
| `airlabs_noroll 1` / `airlabs_noroll 0` | No air roll on / off |
| `airlabs_aim_toggle` | Nose/wheels in Flick (makes the run Custom) |
| `airlabs_reset_session` | Reset this session's run counter |
| `airlabs_reset_directions` | Clear the direction grid |
| `airlabs_lb_post 1` / `airlabs_lb_post 0` | Post your preset runs to the leaderboard / stop |
| `airlabs_lab` | Start or stop the Boost Lab (test) |
| `airlabs_lab_motion 0` / `1` | Boost Lab: Respawn / Constant |
| `airlabs_lab_bubble`, `_pass`, `_speed`, `_dynamic`, `_speed_min`, `_speed_max`, `_curve`, `_cone`, `_height_min`, `_height_max`, `_steep`, `_floor`, `_run`, `_hang` | Boost Lab settings (see "Boost Lab") |
| `airlabs_lab_flips 0` / `1` / `2` | Boost Lab flips: off / real (default) / infinite |
| `airlabs_lab_speed_limit 1` / `0` | Boost Lab speed limits and ball drag on (default) / off; Respawn has no gravity |
| `airlabs_lab_keep_flip_drift 1` / `0` | Volley test: keep vertical drift through flips (default) / normal flip braking; ignored in Respawn |
| `airlabs_lab_markers`, `airlabs_lab_navball`, `airlabs_lab_dust` (1 / 0) | Boost Lab: relative markers, navball, dust (all off by default) |
| `airlabs_lab_spawn_min`, `airlabs_lab_spawn_max` | Respawn spawn range in uu (1500–2000 by default) |
| `airlabs_lab_redirect_ring 1` / `0` | Respawn target ring on (default) / off |
| `airlabs_lab_ring_motion 1` / `0` | Ring fixed in hidden world (default) / fixed offset from car |
| `airlabs_lab_ring_size` | Ring opening width, 250–2000 uu (700 by default) |
| `airlabs_lab_botprobe` | Test: spawn a bot car and hold it next to you for 20 s |
| `airlabs_watch <run>` | Watch a leaderboard run's replay (the number is the run's id on the server) |
| `airlabs_lb_url <url>` | The leaderboard server (only change this for a server of your own) |

## Tuning presets

All presets and par times are in `AirLabs/TrainerCore.h` (`kFlickPresets`,
`kTrackPresets`, `kHybridPresets`, `kChasePresets`, `kParReaction`, `kParFactor`). Changing a
preset changes what its scores mean, so its leaderboard should start again (a new board).

## How the pin works

Every physics tick the plugin puts the car back at the pin point (arena centre, 1022 uu
up) and zeroes its linear velocity. During a run it never touches rotation or spin, so
Rocket League's own air control and drag act exactly as in a match (it only resets
the car between runs). What you train here transfers directly.

Air physics constants: community measurements from
[smish.dev](https://www.smish.dev/rocket_league/aerial_control/).

## Credits and legal

- **Star sky (space map):** NASA/Goddard Space Flight Center Scientific Visualization Studio,
  Deep Star Maps 2020. Gaia DR2: ESA/Gaia/DPAC.
- **Font:** Inter by Rasmus Andersson, SIL Open Font License 1.1 (`AirLabs/fonts/OFL.txt`).
- **UI library:** Dear ImGui by Omar Cornut, MIT License (`AirLabs/imgui/LICENSE.txt`).
- **Air physics constants:** community measurements from smish.dev.
- **Boost Lab physics:** RocketSim by ZealanL, MIT License (`thirdparty/RocketSim/LICENSE`; one change
  for AirLabs, listed in `thirdparty/RocketSim/AIRLABS.md`). RocketSim contains no code from Rocket
  League.
- **Rank icons and rank names:** Rocket League's, by Psyonix / Epic Games.

AirLabs is a free fan-made training tool. Portions of the materials used are trademarks and/or
copyrighted works of Epic Games, Inc. All rights reserved by Epic. This material is not official
and is not endorsed by Epic.
