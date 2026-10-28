# RocketSim in AirLabs

RocketSim (https://github.com/ZealanL/RocketSim, MIT licence, version 2.2.1, commit c2baacb) is
the Boost Lab's physics: car, ball, hits, wheels, flips and flip resets, run in its empty-space
arena (`THE_VOID`). It contains no code from Rocket League.

Changes from upstream, marked "AirLabs change":

- `src/Sim/Car/Car.h` / `Car.cpp`: the car's 2300 top speed is `g_carMaxSpeedUU` instead of the
  constant, so the Boost Lab can turn it off (0 = no cap).
- `src/Sim/Arena/Arena.cpp`: a still ball in `THE_VOID` stays active instead of sleeping, so
  normal gravity can accelerate it from rest. Other arena modes keep the upstream sleep rule.
- `src/Sim/Car/Car.h` / `Car.cpp`: opt-in `airlabsKeepVerticalFlipDrift` captures the car's
  vertical velocity when a flip is accepted. Flip push speed scaling and vertical damping
  (including its sign condition) use velocity relative to that fixed vertical reference.
  Horizontal direction, flip torque/cancels and contact solving retain RocketSim's code. The
  ball's velocity is not edited. The reference clears on a non-flipping SetState (new ball,
  bounce or reset); LabWorld also clears it whenever it resets car velocity, including Infinite
  flips. The plugin enables this experiment only for Constant/Volley, with a toggle.
  This changes match flip physics; absolute speed caps and ball drag remain unchanged, so it
  does not guarantee identical encounters under shared drift when those also intervene.
