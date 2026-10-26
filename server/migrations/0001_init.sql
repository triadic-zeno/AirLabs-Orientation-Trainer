-- AirLabs leaderboard: players, their runs, the replay of each player's best run per board.

CREATE TABLE players (
  id          TEXT PRIMARY KEY,          -- "Steam:7656..." / "Epic:abcd..." (never sent out by the API)
  pid         TEXT NOT NULL UNIQUE,      -- public id: a hash of id
  name        TEXT NOT NULL,             -- Rocket League name, as of the last upload
  platform    TEXT NOT NULL,
  key_hash    TEXT,                      -- SHA-256 of the install key handed out on the first upload
  created     INTEGER NOT NULL,          -- unix seconds
  last_upload INTEGER NOT NULL DEFAULT 0,
  banned      INTEGER NOT NULL DEFAULT 0
);

-- Every personal best that was posted (not every run: only runs that beat your best are stored).
CREATE TABLE runs (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  player    TEXT NOT NULL REFERENCES players(id),
  board     TEXT NOT NULL,                -- "mode-preset-range-noroll"
  score     INTEGER NOT NULL,
  created   INTEGER NOT NULL,
  version   TEXT NOT NULL,
  targets   INTEGER NOT NULL DEFAULT 0,
  misses    INTEGER NOT NULL DEFAULT 0,
  on_pct    REAL,
  avg_time  REAL,
  flags     TEXT NOT NULL DEFAULT '',
  hidden    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX runs_player ON runs(player, board);

-- Replays (deflate-compressed ALR1, base64). Only each player's current best per board keeps one.
CREATE TABLE replays (
  run   INTEGER PRIMARY KEY REFERENCES runs(id),
  data  TEXT NOT NULL
);

-- Each player's best per board: the leaderboards.
CREATE TABLE best (
  board    TEXT NOT NULL,
  player   TEXT NOT NULL REFERENCES players(id),
  run      INTEGER NOT NULL REFERENCES runs(id),
  score    INTEGER NOT NULL,
  created  INTEGER NOT NULL,
  plays    INTEGER NOT NULL DEFAULT 1,   -- runs uploaded on this board (bests or not)
  PRIMARY KEY (board, player)
);
CREATE INDEX best_board_score ON best(board, score DESC, created ASC, run ASC);

-- New players per address per day (stops one machine minting thousands of fake players).
CREATE TABLE signups (
  ip    TEXT NOT NULL,
  day   INTEGER NOT NULL,
  n     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (ip, day)
);
