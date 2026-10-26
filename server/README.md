# AirLabs leaderboard server

A Cloudflare Worker (`src/`) with a D1 database (`migrations/`). The plugin posts finished
preset runs here and reads the boards and replays; `/` is the leaderboard as a web page.

Live at <https://leaderboard.airlabs.workers.dev>.

## How a run is checked

A posted run is the plugin's replay (`AirLabs/Replay.h`, format `ALR1`): every physics tick of
the run, with the car's rotation and the direction to the ball, plus events (target appeared,
hit, miss, ...). The score depends on nothing else, so `src/verify.js` plays it back through
the plugin's own rules and scores it again, then checks it against the preset (ball distance,
ball speed, turn sizes, dash lengths, hold and time limits) and against Rocket League's physics
(the 5.5 rad/s spin cap; no roll on No air roll runs). Scores past the human limit (the Pro
mark; on Track and Chase, less than half the Pro's time off the ball) are turned away. Only a
run that beats the player's best on its board is checked and stored. Each player's best run
per board keeps its replay (stored delta-packed and deflated, about a third of its raw size).

The presets, run rules and rank marks come from `AirLabs/TrainerCore.h` through
`tools/dump_core.cpp`, which writes `src/core-data.js`. The deploy fails if that file is stale:

```
g++ -std=c++20 -O1 -IAirLabs tools/dump_core.cpp -o dump_core && ./dump_core > server/src/core-data.js
```

## API

| | |
|---|---|
| `POST /api/runs` | `{"player": {"platform", "id", "name"}, "key", "replay": "<base64 ALR1>"}`. The first post for an account returns a `key`; later posts must send it. |
| `GET /api/boards` | Every board with its player count and top score |
| `GET /api/boards/:board` | One board, best first (`?limit=` up to 200, `?me=<public id>` adds your place). A board is `mode-preset-range-noroll`, e.g. `0-4-1-0` = Flick Mixed, Medium. |
| `GET /api/replays/:run` | The replay of a run that is someone's current best |

Add `?format=text` for the plugin's format: one `key value` per line, table rows as
`row` lines with tab-separated fields. Everything else is JSON.

## Deploying

Pushing to `main` deploys it (`.github/workflows/deploy-leaderboard.yml`): tests, then
find or create the D1 database `airlabs-leaderboard`, apply `migrations/`, `wrangler deploy`.
It needs two repository secrets: `CLOUDFLARE_API_TOKEN` (permissions: Account > Workers
Scripts: Edit and Account > D1: Edit) and `CLOUDFLARE_ACCOUNT_ID`.

## Testing locally

Node 22 runs the Worker as is, with SQLite standing in for D1:

```
node --test test/*.test.mjs                  # verifier and API tests
node test/localserver.mjs local.db 8787      # the server on http://localhost:8787
```

The fixtures in `test/fixtures/` are real replays written by the plugin's own code
(AirLabs.cpp and Leaderboard.cpp compiled off-game, driving a simulated car).

## Moderating

In the Cloudflare dashboard, Workers & Pages > D1 > airlabs-leaderboard > Console:

```sql
-- who's on a board
SELECT r.id, p.name, r.score, r.flags FROM best b JOIN runs r ON r.id = b.run JOIN players p ON p.id = b.player
  WHERE b.board = '0-4-1-0' ORDER BY r.score DESC;
UPDATE runs SET hidden = 1 WHERE id = 123;                     -- take one run off the boards
UPDATE players SET banned = 1 WHERE name = 'Someone';          -- take a player off every board
UPDATE players SET key_hash = NULL WHERE name = 'Someone';     -- a player lost their key: the next post links a new one
```
