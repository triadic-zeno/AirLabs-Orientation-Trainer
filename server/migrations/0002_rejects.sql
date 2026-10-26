-- Runs the server turned away, with the reason and the replay (no player details), so the
-- checks can be looked at against real runs. Only the latest 300 are kept.
CREATE TABLE rejects (
  id       INTEGER PRIMARY KEY AUTOINCREMENT,
  created  INTEGER NOT NULL,
  board    TEXT NOT NULL,
  score    INTEGER NOT NULL,
  version  TEXT NOT NULL,
  reason   TEXT NOT NULL,
  data     TEXT NOT NULL      -- the replay, packed and deflated like replays.data
);
