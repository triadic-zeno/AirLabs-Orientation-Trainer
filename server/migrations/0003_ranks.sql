-- Real Rocket League ranks, as each player's game last reported them (NULL = not known).
-- Each is tier * 4 + division: tier 0 = Unranked, 1 = Bronze I .. 22 = Supersonic Legend;
-- division 0-3 = Division I-IV.
ALTER TABLE players ADD COLUMN rank1 INTEGER;          -- 1v1 (Ranked Duel)
ALTER TABLE players ADD COLUMN rank2 INTEGER;          -- 2v2 (Ranked Doubles)
ALTER TABLE players ADD COLUMN rank3 INTEGER;          -- 3v3 (Ranked Standard)
ALTER TABLE players ADD COLUMN ranks_updated INTEGER;  -- unix seconds
