-- +goose Up
-- ВИЛАЗКА, the free run (spec §4.5). Two facts have to become storable: which
-- kind of raid a session is, and when this player's last sortie ended.
--
-- ONE COLUMN, not two. `mode` on the session row is the only thing added,
-- because the cooldown is DERIVED from it rather than stored a second time:
-- "available again some minutes after the last one ends" is exactly
-- `max(closed_at) WHERE mode = 'sortie' AND state = 'closed'`, and every close
-- path in this service already writes closed_at (store.SubmitResult's close, and
-- closeExpiredSessionTx for both the voided and the died branch). A
-- players.sortie_available_at column would have needed a matching write in all
-- three of them, and the failure mode of forgetting one is a cooldown that
-- silently does not apply — i.e. the farm this exists to prevent.
--
-- TEXT and not an enum, unlike raid_state and raid_outcome. Those two are read
-- and written as ::text everywhere already (pgx has no mapping for our enum
-- OIDs), so an enum buys nothing here but a migration to add the next mode; and
-- domain.IsValidRaidMode is the gate, on the way in, where client input is.
--
-- DEFAULT 'raid' backfills every existing session as an ordinary raid, which is
-- what all of them are: nothing before this migration could have been a sortie.
-- NOT NULL so no code path has to decide what a null mode means.
ALTER TABLE raid_sessions ADD COLUMN mode TEXT NOT NULL DEFAULT 'raid';

-- The cooldown lookup: the newest closed sortie for one player. Partial, because
-- sorties are a small minority of sessions and this index only ever answers that
-- one question — asked once per /v1/raid/start with mode=sortie.
CREATE INDEX raid_sessions_last_sortie
    ON raid_sessions (player_id, closed_at DESC)
    WHERE mode = 'sortie' AND state = 'closed';

-- +goose Down
DROP INDEX raid_sessions_last_sortie;
ALTER TABLE raid_sessions DROP COLUMN mode;
