-- The name each player's game reports (players.name is the name shown on the boards, which can be
-- one the player picked). Kept for moderation; never shown.
ALTER TABLE players ADD COLUMN game_name TEXT;
