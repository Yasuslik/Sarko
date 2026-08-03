-- +goose Up
-- Equipment lives with the PLAYER, not with the world (equipment spec §6): a
-- level travel destroys every actor, and what the player is wearing has to
-- survive the trip into the raid and back out of it.
--
-- Server-side and not on the client's game instance, which was the other option
-- on the table. The deciding argument is that /v1/raid/start now DEBITS what is
-- equipped: a client that owned the equipment state could equip a pistol it does
-- not have and either debit an item out of nowhere or be refused for
-- insufficient_items with no way to see why. Equipment that decides what the
-- loadout debits has to be server truth. The cost of the alternative was also
-- concrete — a reinstall would have lost it silently.
--
-- A TABLE rather than a jsonb column on players, for two reasons. The slot set
-- is small and fixed, so one row per worn thing is the natural shape and an
-- upsert on (player_id, slot) is the whole write path; and item_id then sits in
-- the same kind of column stash_items already uses, so the two are compared
-- without unwrapping JSON inside SQL.
--
-- No foreign key on item_id: stash_items.item_id has none either (ids are
-- free-form TEXT and domain.ItemDefs is the authority), and adding one here
-- alone would make the two disagree about what an item is.
CREATE TABLE player_equipment (
    player_id UUID NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    slot      TEXT NOT NULL,
    item_id   TEXT NOT NULL,
    -- One thing per slot. The primary key is the rule, not a convention: two
    -- rows for `weapon` would make EquipmentLoadout debit two guns.
    PRIMARY KEY (player_id, slot)
);

-- +goose Down
DROP TABLE player_equipment;
