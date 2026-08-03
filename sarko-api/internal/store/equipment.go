package store

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// ErrNotEquippable means the item may not be worn in the requested slot.
var ErrNotEquippable = errors.New("item is not equippable in that slot")

// Equipment reads what a player is wearing, as slot -> item id.
//
// Never nil, so a caller can index it without checking: an empty map is a player
// wearing nothing, which is a legal and common state (a brand-new player, and
// every player who has just died).
func (s *Store) Equipment(ctx context.Context, playerID string) (map[string]string, error) {
	return equipmentTx(ctx, s.pool, playerID)
}

// querier is the little of pgx both a pool and a transaction offer. Equipment is
// read both on its own (Equipment) and inside the profile's read-only
// transaction, and reading it outside that transaction would let the stash and
// the equipment come from two different snapshots — i.e. a screen showing a
// pistol equipped beside a stash that was read before it was equipped.
type querier interface {
	Query(ctx context.Context, sql string, args ...any) (pgx.Rows, error)
}

func equipmentTx(ctx context.Context, q querier, playerID string) (map[string]string, error) {
	out := make(map[string]string, len(domain.EquipSlots))

	rows, err := q.Query(ctx,
		`SELECT slot, item_id FROM player_equipment WHERE player_id = $1`, playerID)
	if err != nil {
		return nil, fmt.Errorf("read equipment: %w", err)
	}
	defer rows.Close()

	for rows.Next() {
		var slot, itemID string
		if err := rows.Scan(&slot, &itemID); err != nil {
			return nil, fmt.Errorf("scan equipment row: %w", err)
		}
		out[slot] = itemID
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate equipment: %w", err)
	}
	return out, nil
}

// SetEquipment puts itemID in slot, or clears the slot when itemID is empty, and
// returns the equipment as it stands afterwards.
//
// Three things are checked, and all three are checked HERE rather than only at
// the API edge or only on the client, because this write decides what
// /v1/raid/start debits:
//
//   - the slot is a real slot, and the item is one that slot accepts
//     (domain.SlotAccepts). Without it a client could equip a bike frame as a
//     weapon and then debit it as a loadout.
//   - the player actually HOLDS the item. This is the one the spec names: "a
//     client can equip what it does not own" otherwise, and the shelter would
//     then show a pistol the stash has never contained and send it as a loadout
//     that /v1/raid/start refuses with insufficient_items and no explanation.
//   - the same item is not worn in two slots at once, which the primary key
//     cannot express (it bounds slots per player, not items per player). Two
//     slots holding one pistol would make EquipmentLoadout debit two.
//
// Equipping does NOT debit the stash. The item stays in the stash and is debited
// at raid start, so a player who equips and then closes the game has lost
// nothing — and so the stash grid keeps showing what the player owns rather than
// splitting it into two places the player has to add up.
func (s *Store) SetEquipment(ctx context.Context, playerID string, slot domain.EquipSlot, itemID string) (map[string]string, error) {
	if itemID != "" {
		if err := domain.SlotAccepts(slot, itemID); err != nil {
			return nil, fmt.Errorf("%w: %s", ErrNotEquippable, err)
		}
	} else if !domain.IsValidEquipSlot(string(slot)) {
		return nil, fmt.Errorf("%w: unknown equipment slot", ErrNotEquippable)
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return nil, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if itemID == "" {
		if _, err := tx.Exec(ctx,
			`DELETE FROM player_equipment WHERE player_id = $1 AND slot = $2`,
			playerID, string(slot)); err != nil {
			return nil, fmt.Errorf("clear slot: %w", err)
		}
	} else {
		// The player row is locked first, and in the same order every other write
		// in this package takes its locks (players before stash_items) — see the
		// note in SubmitResult about the deadlock cycle the other order closes.
		var exists bool
		err = tx.QueryRow(ctx, `SELECT true FROM players WHERE id = $1 FOR UPDATE`, playerID).Scan(&exists)
		if errors.Is(err, pgx.ErrNoRows) {
			return nil, ErrNotFound
		}
		if err != nil {
			return nil, fmt.Errorf("lock player: %w", err)
		}

		// Ownership, read inside the transaction that writes. A read outside it is
		// a race with a raid start that debits the same stack.
		var held int
		err = tx.QueryRow(ctx,
			`SELECT COALESCE(SUM(quantity), 0) FROM stash_items
			 WHERE player_id = $1 AND item_id = $2`, playerID, itemID).Scan(&held)
		if err != nil {
			return nil, fmt.Errorf("read stash for equip: %w", err)
		}
		if held < 1 {
			return nil, fmt.Errorf("%w: %s x1", ErrInsufficientItems, itemID)
		}

		// One item, one slot. The delete is scoped to the OTHER slots so that
		// re-equipping the same thing in the same slot is a no-op rather than a
		// delete-then-insert that could momentarily leave the slot empty.
		if _, err := tx.Exec(ctx,
			`DELETE FROM player_equipment
			 WHERE player_id = $1 AND item_id = $2 AND slot <> $3`,
			playerID, itemID, string(slot)); err != nil {
			return nil, fmt.Errorf("clear duplicate slot: %w", err)
		}

		if _, err := tx.Exec(ctx,
			`INSERT INTO player_equipment (player_id, slot, item_id) VALUES ($1, $2, $3)
			 ON CONFLICT (player_id, slot) DO UPDATE SET item_id = EXCLUDED.item_id`,
			playerID, string(slot), itemID); err != nil {
			return nil, fmt.Errorf("equip %s: %w", itemID, err)
		}
	}

	out, err := equipmentTx(ctx, tx, playerID)
	if err != nil {
		return nil, err
	}
	if err := tx.Commit(ctx); err != nil {
		return nil, fmt.Errorf("commit: %w", err)
	}
	return out, nil
}

// clearEquipmentTx strips everything a player is wearing, inside an existing
// transaction.
//
// This is what "death loses it" means in the database (spec §4). The equipment
// was already debited from the stash at raid start and a death credits nothing
// back, so leaving the rows behind would make the shelter show a pistol the
// player no longer owns — and the next raid would then be refused for
// insufficient_items with the reason hidden two screens away. Clearing them is
// what keeps the equipment screen and the stash telling the same story.
func clearEquipmentTx(ctx context.Context, tx pgx.Tx, playerID string) error {
	if _, err := tx.Exec(ctx,
		`DELETE FROM player_equipment WHERE player_id = $1`, playerID); err != nil {
		return fmt.Errorf("clear equipment: %w", err)
	}
	return nil
}
