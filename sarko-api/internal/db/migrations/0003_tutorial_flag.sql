-- +goose Up
-- The tutorial raid happens once (spec §6.5). A flag on the player row rather
-- than "has this player ever closed a raid as extracted": the raid_sessions
-- table is swept and closed rows are the historical record, so inferring the
-- answer would mean a scan per profile read on the hottest endpoint the client
-- has. It is also a one-way latch, which a derived answer would not be.
--
-- DEFAULT false, so every player who predates this migration is put back into
-- the tutorial exactly once. That is the deliberate choice: the alternative
-- (DEFAULT true) would mean nobody ever sees the tutorial, and the only players
-- that exist today are the developer's own test devices.
--
-- Safe on the populated production table: a NOT NULL column with a non-volatile
-- DEFAULT is metadata-only since Postgres 11, so no row rewrite and no long
-- ACCESS EXCLUSIVE hold. Same shape as 0002_starter_kit.sql, which is already
-- deployed.
ALTER TABLE players ADD COLUMN tutorial_completed BOOLEAN NOT NULL DEFAULT false;

-- +goose Down
ALTER TABLE players DROP COLUMN tutorial_completed;
