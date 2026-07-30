package store

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// ErrMaxTier means the player already owns the last vehicle.
var ErrMaxTier = errors.New("already at the highest tier")

// CraftNextVehicle spends the recipe parts and advances the garage one tier.
// Either the parts are consumed and the tier advances, or nothing changes.
func (s *Store) CraftNextVehicle(ctx context.Context, playerID string) (domain.Tier, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return "", fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var current domain.Tier
	err = tx.QueryRow(ctx,
		`SELECT vehicle_tier FROM garage_progress WHERE player_id = $1 FOR UPDATE`,
		playerID).Scan(&current)
	if errors.Is(err, pgx.ErrNoRows) {
		return "", ErrNotFound
	}
	if err != nil {
		return "", fmt.Errorf("lock garage row: %w", err)
	}

	next, ok := domain.NextTier(current)
	if !ok {
		return "", ErrMaxTier
	}
	parts, ok := domain.Recipe(next)
	if !ok {
		return "", fmt.Errorf("no recipe for tier %s", next)
	}

	if err := debitItemsTx(ctx, tx, playerID, domain.MergeStacks(parts)); err != nil {
		return "", err
	}

	_, err = tx.Exec(ctx,
		`UPDATE garage_progress SET vehicle_tier = $2, updated_at = now() WHERE player_id = $1`,
		playerID, string(next))
	if err != nil {
		return "", fmt.Errorf("advance tier: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return "", fmt.Errorf("commit: %w", err)
	}
	return next, nil
}
