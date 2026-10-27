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
bind O airlabs_menu
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

Everything lives in the plugin's own window. Open and close it with the `airlabs_menu`
command; the easiest way is to bind a key once in the F6 console:

```
bind O airlabs_menu
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

1. Open the trainer window (`airlabs_menu`), pick a mode, a preset and a range on the
   **Play** page, and press **START** (or `bind F5 airlabs_toggle`). START takes you to the
   space map, from the main menu too (see "The space map" below).
2. Your car resets to the centre of the arena with a ball between you and the goal you
   attack. The car is level but turned 45 deg away from the ball, so the ball is on
   screen to the side (Flip Reset's ball sits above, on the goal side). Turn onto it and
   point at it for **1.5 seconds** and the run begins. Looking away resets the countdown.
3. When the minute is up you get your results: score, tier, your rank, comparison with your
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
- **Same tiers.** The tier marks are the same as for normal runs. The player model the marks
  come from (see "How the ranks were worked out") already turns the nose with pitch and yaw
  alone, so blocking air roll doesn't change what each tier can score.
- **Nose aim only.** Aiming with your wheels needs air roll, so No air roll isn't used when a
  run aims with the wheels.

## Ranges

Every preset comes in three ranges, picked with the Range buttons under the preset row:

| Range | Ball distance | Target |
|---|---|---|
| Short | 400 uu | 13.6 deg |
| Medium | 600 uu | 9.0 deg |
| Long | 900 uu | 6.0 deg |

**Each range is its own competition**, with its own personal bests, tiers, progress
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
- **Save a Custom setup as your own preset.** The Save as preset button above the
  Customise drawer stays dark until you change a setting; then type a name and press
  it (or Enter). The preset appears in the **My presets** row and gets its own records:
  personal best, run count, last-10 average and a progress graph (no tiers, because
  there's no reference for your settings). Like everything else, its targets are random
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
- **Tiers** are named after Rocket League's ranks: Bronze, Silver, Gold, Platinum,
  Diamond, Champion and Grand Champion, each with divisions I, II and III, then Supersonic
  Legend, then **Pro**: the highest score a human can plausibly get. A run's tier is what
  that one score is worth. See "Ranks" and "How the ranks were worked out" below.

## Ranks

Every preset has **your rank** for each range (and for No air roll): one rank built from all
your runs on it, so one great run moves it a little instead of jumping it. It shows on the
preset buttons ("Control  GC1"), on the Up Next card, after each run ("Your rank: Champion
III (+1 division)") and in Stats > Records.

- **Placements.** Your first 10 runs on a preset are placements: the rank is the plain
  average of those runs, and the button shows "3/10" until you've done them.
- **After that, a weighted average.** Each run counts for 10% and everything before it for
  90%, so your last ~10 runs carry most of the weight and older runs fade out. Your rank
  follows how you play now, not your best day.
- **Flukes are capped.** One run can pull your rank at most one full rank (three divisions)
  towards itself before the 10% weighting, so a fluke Grand Champion III run on a Champion II
  rank moves you up about a third of a division, and a disaster run (a 0 after a disconnect,
  say) costs about the same.
- **How a run is scored on the way in:** where the score sits between the division marks,
  so a score halfway between Champion II and Champion III counts as halfway, not as
  Champion II.
- Saved presets and Custom runs have no rank (no marks to compare against).
- **Pro** sits above Supersonic Legend: the score of a player at human limits, where the best
  pros would be. It's meant to be the ceiling, not a rank most players reach.

**Rank icons.** AirLabs draws a coloured badge ("GC1") by default. To use your own icons, put
PNG files (transparent background, about 128x128) in
`%APPDATA%\bakkesmod\bakkesmod\data\AirLabs\ranks\`, named one per division:
`bronze_1.png`, `bronze_2.png`, `bronze_3.png`, `silver_1.png` ... `grand_champion_3.png`,
then `supersonic_legend.png` and `pro.png`; or one per rank (`bronze.png` ...
`grand_champion.png`), in which case the division is drawn as 1-3 dots under the icon.
`unranked.png` is shown next to "3/10" while you're placing. Restart Rocket League (or reload
the plugin) after adding them. Ranks: `bronze`, `silver`, `gold`, `platinum`, `diamond`,
`champion`, `grand_champion`, `supersonic_legend`, `pro`.

## How the ranks were worked out

Every preset and every range has its own marks, because a 900 uu Insane track and a 400 uu
Small flick are completely different tests. The marks come from a simulation
(`tools/tier_model.cpp`, which compiles against the plugin's own `TrainerCore.h`):

1. **Simulated players.** A player is a control loop with human limits: reaction time,
   visual delay (and how much of it they predict away), how hard they commit to the
   stick, how early they brake, stick noise, how well they judge a moving ball, and how
   they find a ball that starts off screen by following the arrow.
2. **Real game.** Each simulated player plays every preset through the plugin's own
   target generator, ball paths and scoring, with Rocket League's air-control physics
   (torque, drag, the 5.5 rad/s spin cap), 24 one-minute runs per point.
3. **Three reference players, placed on Rocket League's 2v2 MMR scale:**

   | Reference | MMR | Where it comes from |
   |---|---|---|
   | Bronze | 200 (middle of Bronze) | ~7-8 s per target on Flick Mixed with no time limit |
   | Grand Champion I | 1497 (middle of GC1) | Fitted to a real GC1-level controller player's runs: 10 each of Flick Mixed, Track Control, Hybrid Control and Chase Medium. The fit matches his score, targets, time per target, reaction, time on target and catch time to within ~5%. |
   | Pro | 2600 (where the best pros peak; the 2v2 record is 2623) | The GC1 player at human limits (below) |

   A player at any other MMR is a blend of the two reference players either side of it.
4. **Marks.** Each division's mark is what a player at the MMR where that division starts in
   Rocket League 2v2 scores (Bronze II 170, Silver I 294, Gold I 475, Platinum I 655, Diamond I
   835, Champion I 1075, Grand Champion I 1435, GC II 1575, GC III 1715, Supersonic Legend 1863).
   So divisions are spaced the way Rocket League spaces them: about 60 MMR per division from
   Bronze to Platinum, 80 in Diamond, 120 in Champion and 140 in Grand Champion. **Pro** is the
   Pro reference player's score. Per mode, the scale is nudged so the measured GC1 player's own
   average sits exactly in the middle of Grand Champion I.

**The Pro player: human limits.** Moving the GC1 player all the way to a perfect controller
gives scores no human gets (Flick Mixed ~85,000 at 0.8 s per target, which is close to what the
car's physics allow). So Pro stops at measured human limits:

- **Pointing speed.** Fitts' law measures how fast you can land on a target as bits per second
  (harder = further or smaller). Thumbsticks measure 1.5-2.4 bits/s in lab studies; a mouse,
  the fastest common pointer, 3.7-4.9. The GC1 player measures ~2.3 on Flick Mixed. Pro is set
  to 4.0: a stick player pointing as well as a typical mouse user.
- **Reaction.** Elite gamers react in ~150-200 ms; with stick travel that's 0.25 s to the
  plugin's reaction measure (the GC1 player: 0.34 s).
- What follows from those lands at human limits too: ~0.12 s visual delay (0.10 s visuomotor
  plus system latency), ~7% stick noise, ~10% error judging the ball's speed (the lab best is
  ~5.6%).

Where that puts the top of the ladder (Medium range):

| Preset | GC1 | GC2 | GC3 | SSL | Pro | Pro player |
|---|---|---|---|---|---|---|
| Flick Mixed | 24,200 | 30,500 | 36,800 | 42,750 | 71,000 | 0.89 s per target |
| Track Control | 45,200 | 49,600 | 54,200 | 57,400 | 59,630 | 99% on the ball |
| Hybrid Control | 39,900 | 45,500 | 50,250 | 53,500 | 62,000 | |
| Chase Medium | 25,800 | 27,500 | 28,900 | 30,900 | 37,200 | catches in 0.27 s |

For comparison, the measured GC1 player averages 1.44 s per target on Flick Mixed and is on the
ball 55% of the time in Track Control; a Supersonic Legend in the model takes 1.2 s per target.

Notes:
- **Flick Small** tops out: at Grand Champion most targets already earn the 2,000-point maximum,
  so GC II to Supersonic Legend are only a few thousand points apart there.
- **Big and Turnaround:** with the 5 s limit and the 500-point miss penalty, weaker players
  score close to 0, so the low marks are only a few hundred points: any targets at all lift you
  out of Bronze. The marks are fitted only to the part of the curve that isn't stuck at 0.
- **Chase** has its own reaction model: real players can't follow a dash the moment it starts
  (the direction is a surprise), take a moment to judge its speed, and keep turning for a while
  after it stops.

The middle ranks are the least certain part: they're a blend between the Bronze estimate and the
measured GC1 player. The next step is real **controller** runs from players of known ranks:
AirLabs marks keyboard runs in `runs.csv` (the `flags` column says `keyboard`) so they can be
kept out of that. Older runs are re-graded with the current marks when the plugin loads.

**Flick Small (2.7.2).** The first leaderboard runs showed the model rating Small far too hard
for low and middle ranks: a player who is Platinum on Flick Mixed and Gold on Track came out
Bronze I on Small. The model treats small, on-screen turns as easy, and misses the time real
players lose settling onto the ball for the hold. Small's marks are now set from Flick Medium's,
scaled up by how much higher Small's Pro mark is (about 1.4-1.6x, because small turns are
quicker), rising to 1.37 times that at Bronze. The Pro mark is unchanged.

## Stats page

Four sub-tabs:

- **Overview:** your last run with a **Copy result** button, which puts a line like
  this on your clipboard:
  `AirLabs 2.0.0 | Track: Control (Medium) | 41,250 | Champion | 69% on target | PRESET`.
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
- **Records:** pick a mode to see a table of every preset with your rank, best score
  and the best's tier, number of runs and last-10 average, with your saved presets for
  that mode in a **My presets** table underneath. Click a row for its progress graph,
  the points to the next tier and every tier mark.
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
- **Every run comes with its replay.** The plugin records the whole run, one sample per physics
  tick: the car's rotation and where the ball was, which is everything a score depends on. Anyone
  can watch any run, so a run that doesn't add up is plain to see. The server also plays each run
  back through the plugin's own rules and keeps that score with the run for reference.
- **What's stored:** your Rocket League name, platform, your platform account id (used only to
  tell players apart; never shown), and your best run per board with its replay. The first time
  you post, the server gives this install a key (`data/AirLabs/leaderboard.txt`), so nobody else
  can post as you. Turn posting off any time with **Stop posting**.

The server is a Cloudflare Worker with a D1 database, in `server/` (see `server/README.md`).

## Assists page

None of these affect whether a run counts.

- **Laser** out of the nose (or the wheels in wheels aim). Red off target, yellow
  close, green locked on. Off by default. Toggle: `bind L airlabs_laser_toggle`.
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
| `airlabs_menu` | Open or close the trainer window |
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
| `airlabs_watch <run>` | Watch a leaderboard run's replay (the number is the run's id on the server) |
| `airlabs_lb_url <url>` | The leaderboard server (only change this for a server of your own) |

## Tuning presets

All presets, par times and tier marks are in `AirLabs/TrainerCore.h` (`kFlickPresets`,
`kTrackPresets`, `kHybridPresets`, `kChasePresets`, `kParReaction`, `kParFactor`,
`kTierFlick`, `kTierTrack`, `kTierHybrid`, `kTierChase`). After changing a preset,
regenerate its tier marks with `tools/tier_model.cpp` (instructions at the top of that
file).

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
- **Rank icons and rank names:** Rocket League's, by Psyonix / Epic Games.

AirLabs is a free fan-made training tool. Portions of the materials used are trademarks and/or
copyrighted works of Epic Games, Inc. All rights reserved by Epic. This material is not official
and is not endorsed by Epic.
