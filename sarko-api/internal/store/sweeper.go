package store

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// expiredSession is one raid past its deadline, staged for closing.
type expiredSession struct {
	id       string
	playerID string
	state    domain.RaidState
	loadout  []domain.ItemStack
}

// closeExpiredTx runs query (which must select id, player_id, state::text and
// loadout for every raid session past its deadline) against tx, and closes
// each row it finds: a pending session never entered the map, so its loadout
// goes back to the stash and it becomes voided; an active session ran out of
// time, which counts as death (§11 — leaving the app counts as death), so it
// closes with outcome died and nothing is credited.
//
// query/args is the only thing that differs between the two callers:
// sweepPlayerTx scopes to one player inside an already-open transaction,
// SweepExpired scans every player with FOR UPDATE SKIP LOCKED in its own
// transaction. Everything else — draining, closing, and applying the two
// outcomes — lives here once.
//
// Rows are drained into a slice and closed before any write is issued:
// iterating a pgx.Rows while writing on the same transaction is unsafe.
func closeExpiredTx(ctx context.Context, tx pgx.Tx, query string, args ...any) (voided int, died int, err error) {
	rows, err := tx.Query(ctx, query, args...)
	if err != nil {
		return 0, 0, fmt.Errorf("select expired: %w", err)
	}

	var batch []expiredSession
	for rows.Next() {
		var e expiredSession
		var raw []byte
		var state string
		if err := rows.Scan(&e.id, &e.playerID, &state, &raw); err != nil {
			rows.Close()
			return 0, 0, fmt.Errorf("scan expired: %w", err)
		}
		e.state = domain.RaidState(state)
		if err := json.Unmarshal(raw, &e.loadout); err != nil {
			rows.Close()
			return 0, 0, fmt.Errorf("unmarshal loadout: %w", err)
		}
		batch = append(batch, e)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, 0, fmt.Errorf("iterate expired: %w", err)
	}

	for _, e := range batch {
		if e.state == domain.StatePending {
			if err := addItemsTx(ctx, tx, e.playerID, e.loadout); err != nil {
				return voided, died, err
			}
			if _, err := tx.Exec(ctx,
				`UPDATE raid_sessions SET state = 'voided', closed_at = now() WHERE id = $1`,
				e.id); err != nil {
				return voided, died, fmt.Errorf("void session %s: %w", e.id, err)
			}
			voided++
			continue
		}

		if _, err := tx.Exec(ctx,
			`UPDATE raid_sessions
			 SET state = 'closed', outcome = 'died', closed_at = now(), result_items = '[]'::jsonb
			 WHERE id = $1`, e.id); err != nil {
			return voided, died, fmt.Errorf("close session %s: %w", e.id, err)
		}
		died++
	}

	return voided, died, nil
}

// SweepExpired closes every raid past its deadline, across all players.
// Pending sessions are voided and their loadout returned; active sessions
// are closed as died (§11: leaving the app counts as death).
func (s *Store) SweepExpired(ctx context.Context) (voided int, died int, err error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return 0, 0, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// SKIP LOCKED lets a second instance sweep concurrently without blocking.
	// state is read as text for the same reason as everywhere else in this package.
	voided, died, err = closeExpiredTx(ctx, tx,
		`SELECT id, player_id, state::text, loadout FROM raid_sessions
		 WHERE state IN ('pending', 'active') AND expires_at <= now()
		 FOR UPDATE SKIP LOCKED`)
	if err != nil {
		return 0, 0, err
	}

	if err := tx.Commit(ctx); err != nil {
		return 0, 0, fmt.Errorf("commit: %w", err)
	}
	return voided, died, nil
}

// RunSweeper calls SweepExpired on a ticker until ctx is cancelled.
func RunSweeper(ctx context.Context, s *Store, every time.Duration) {
	ticker := time.NewTicker(every)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			voided, died, err := s.SweepExpired(ctx)
			if err != nil {
				slog.Error("sweep expired raids", "err", err)
				continue
			}
			if voided > 0 || died > 0 {
				slog.Info("swept raids", "voided", voided, "died", died)
			}
		}
	}
}
