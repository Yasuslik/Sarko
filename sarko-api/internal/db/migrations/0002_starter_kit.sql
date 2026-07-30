-- +goose Up
-- One-time flag rather than "does the stash already contain a pistol": the kit
-- is spent in the first raid, and a stash-contents check would hand out a fresh
-- one on the next app launch. /v1/auth/anonymous runs on every launch, so
-- "already granted" has to be recorded, not inferred.
ALTER TABLE players ADD COLUMN starter_kit_granted BOOLEAN NOT NULL DEFAULT false;

-- Players that predate this migration keep their stash untouched and are
-- treated as not yet granted, so existing testers get the kit on next launch.

-- +goose Down
ALTER TABLE players DROP COLUMN starter_kit_granted;
