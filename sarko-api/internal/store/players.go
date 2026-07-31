package store

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// Profile is everything the client needs to render the shelter.
type Profile struct {
	PlayerID      string             `json:"player_id"`
	SchemaVersion int                `json:"schema_version"`
	Stash         []domain.ItemStack `json:"stash"`
	Tier          domain.Tier        `json:"vehicle_tier"`
	UnlockedMaps  []string           `json:"unlocked_maps"`
	// TutorialCompleted is false until the player's first *successful* raid
	// (spec §6.5). While it is false the client uses the map's authored
	// fixedItems instead of a seeded roll, so this field decides what is in
	// every container of the next raid — which is why it is set inside
	// SubmitResult's transaction and never anywhere else.
	TutorialCompleted bool `json:"tutorial_completed"`
}

// UpsertPlayer returns the player id for a device, creating the player and its
// garage row on first sight. Safe to call on every app launch.
func (s *Store) UpsertPlayer(ctx context.Context, deviceID string) (string, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return "", fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var playerID string
	err = tx.QueryRow(ctx,
		`INSERT INTO players (device_id) VALUES ($1)
		 ON CONFLICT (device_id) DO UPDATE SET device_id = EXCLUDED.device_id
		 RETURNING id`, deviceID).Scan(&playerID)
	if err != nil {
		return "", fmt.Errorf("upsert player: %w", err)
	}

	_, err = tx.Exec(ctx,
		`INSERT INTO garage_progress (player_id) VALUES ($1) ON CONFLICT DO NOTHING`, playerID)
	if err != nil {
		return "", fmt.Errorf("ensure garage row: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return "", fmt.Errorf("commit: %w", err)
	}
	return playerID, nil
}

// GrantStarterKit credits domain.StarterKit() to a player exactly once, ever.
// It reports whether this call was the one that granted it.
//
// The flag flip and the credit are one transaction, and the flip is the
// conditional statement: an UPDATE with `AND NOT starter_kit_granted` either
// affects one row or none, so two concurrent logins cannot both credit. Doing
// it the other way round — read the flag, then credit — is a race that hands
// out two pistols to a client that fires two auth calls at once.
func (s *Store) GrantStarterKit(ctx context.Context, playerID string) (bool, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return false, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	tag, err := tx.Exec(ctx,
		`UPDATE players SET starter_kit_granted = true
		 WHERE id = $1 AND NOT starter_kit_granted`, playerID)
	if err != nil {
		return false, fmt.Errorf("claim starter kit: %w", err)
	}
	if tag.RowsAffected() == 0 {
		// Already granted, or no such player. Either way nothing to credit, and
		// "no such player" is not this call's error to report: the caller just
		// created the row.
		return false, nil
	}

	if err := addItemsTx(ctx, tx, playerID, domain.StarterKit()); err != nil {
		return false, err
	}
	if err := tx.Commit(ctx); err != nil {
		return false, fmt.Errorf("commit: %w", err)
	}
	return true, nil
}

// Profile reads stash, garage tier and derived map access.
func (s *Store) Profile(ctx context.Context, playerID string) (Profile, error) {
	p := Profile{PlayerID: playerID, Stash: []domain.ItemStack{}}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{
		IsoLevel:   pgx.RepeatableRead,
		AccessMode: pgx.ReadOnly,
	})
	if err != nil {
		return Profile{}, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	err = tx.QueryRow(ctx,
		`SELECT p.schema_version, COALESCE(g.vehicle_tier, 'none'), p.tutorial_completed
		 FROM players p
		 LEFT JOIN garage_progress g ON g.player_id = p.id
		 WHERE p.id = $1`, playerID).Scan(&p.SchemaVersion, &p.Tier, &p.TutorialCompleted)
	if errors.Is(err, pgx.ErrNoRows) {
		return Profile{}, ErrNotFound
	}
	if err != nil {
		return Profile{}, fmt.Errorf("read player: %w", err)
	}

	rows, err := tx.Query(ctx,
		`SELECT item_id, quantity FROM stash_items WHERE player_id = $1 ORDER BY item_id`, playerID)
	if err != nil {
		return Profile{}, fmt.Errorf("read stash: %w", err)
	}
	defer rows.Close()

	for rows.Next() {
		var item domain.ItemStack
		if err := rows.Scan(&item.ItemID, &item.Quantity); err != nil {
			return Profile{}, fmt.Errorf("scan stash row: %w", err)
		}
		p.Stash = append(p.Stash, item)
	}
	if err := rows.Err(); err != nil {
		return Profile{}, fmt.Errorf("iterate stash: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return Profile{}, fmt.Errorf("commit: %w", err)
	}

	p.UnlockedMaps = domain.UnlockedMaps(p.Tier)
	return p, nil
}

// AddItems credits stacks to a stash, merging with what is already there.
func (s *Store) AddItems(ctx context.Context, playerID string, stacks []domain.ItemStack) error {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := addItemsTx(ctx, tx, playerID, stacks); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit: %w", err)
	}
	return nil
}

// addItemsTx is shared by AddItems and the raid transactions.
func addItemsTx(ctx context.Context, tx pgx.Tx, playerID string, stacks []domain.ItemStack) error {
	for _, item := range domain.MergeStacks(stacks) {
		_, err := tx.Exec(ctx,
			`INSERT INTO stash_items (player_id, item_id, quantity) VALUES ($1, $2, $3)
			 ON CONFLICT (player_id, item_id)
			 DO UPDATE SET quantity = stash_items.quantity + EXCLUDED.quantity`,
			playerID, item.ItemID, item.Quantity)
		if err != nil {
			return fmt.Errorf("credit %s: %w", item.ItemID, err)
		}
	}
	return nil
}
