-- +goose Up
CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE players (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id      TEXT NOT NULL UNIQUE,
    schema_version INT NOT NULL DEFAULT 1,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE stash_items (
    player_id UUID NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    item_id   TEXT NOT NULL,
    quantity  INT  NOT NULL CHECK (quantity > 0),
    PRIMARY KEY (player_id, item_id)
);

CREATE TABLE garage_progress (
    player_id    UUID PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
    vehicle_tier TEXT NOT NULL DEFAULT 'none',
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TYPE raid_state AS ENUM ('pending', 'active', 'closed', 'voided');
CREATE TYPE raid_outcome AS ENUM ('extracted', 'died');

CREATE TABLE raid_sessions (
    id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    player_id          UUID NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    map_id             TEXT NOT NULL,
    seed               BIGINT NOT NULL,
    state              raid_state NOT NULL DEFAULT 'pending',
    outcome            raid_outcome,
    session_token_hash BYTEA NOT NULL,
    loadout            JSONB NOT NULL,
    result_items       JSONB,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    confirmed_at       TIMESTAMPTZ,
    closed_at          TIMESTAMPTZ,
    expires_at         TIMESTAMPTZ NOT NULL
);

-- A player may never have two raids in flight. Enforced by the database.
CREATE UNIQUE INDEX one_open_raid_per_player
    ON raid_sessions (player_id)
    WHERE state IN ('pending', 'active');

-- The sweeper scans by deadline.
CREATE INDEX raid_sessions_open_expiry
    ON raid_sessions (expires_at)
    WHERE state IN ('pending', 'active');

-- +goose Down
DROP TABLE raid_sessions;
DROP TYPE raid_outcome;
DROP TYPE raid_state;
DROP TABLE garage_progress;
DROP TABLE stash_items;
DROP TABLE players;
