# RocketSim in AirLabs

RocketSim (https://github.com/ZealanL/RocketSim, MIT licence, version 2.2.1, commit c2baacb) is
the Boost Lab's physics: car, ball, hits, wheels, flips and flip resets, run in its empty-space
arena (`THE_VOID`). It contains no code from Rocket League.

One change from upstream, marked "AirLabs change":

- `src/Sim/Car/Car.h` / `Car.cpp`: the car's 2300 top speed is `g_carMaxSpeedUU` instead of the
  constant, so the Boost Lab can turn it off (0 = no cap).
