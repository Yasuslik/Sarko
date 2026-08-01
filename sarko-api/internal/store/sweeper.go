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
		if err := closeExpiredSessionTx(ctx, tx, e); err != nil {
			return voided, died, err
		}
		if e.state == domain.StatePending {
			voided++
		} else {
			died++
		}
	}

	return voided, died, nil
}

// closeExpiredSessionTx applies the expiry rule to one session row that the
// caller has already loaded and locked FOR UPDATE.
//
// This is the whole of "what expiry means", and it is deliberately the only
// copy: a pending session never entered the map, so its loadout goes back and
// it becomes voided; an active session ran out of time, which counts as death
// (§11), so it closes with outcome died and nothing is credited. Two callers
// share it — the sweeper, which finds expired rows by scanning, and
// SubmitResult, which finds one because it happens to be holding it. If they
// drifted apart, whether an abandoned loadout came back would depend on which
// code path noticed the expiry first.
//
// The caller owns the transaction and must hold the row lock; nothing here
// re-checks expires_at.
func closeExpiredSessionTx(ctx context.Context, tx pgx.Tx, e expiredSession) error {
	if e.state == domain.StatePending {
		if err := addItemsTx(ctx, tx, e.playerID, e.loadout); err != nil {
			return err
		}
		if _, err := tx.Exec(ctx,
			`UPDATE raid_sessions SET state = 'voided', closed_at = now() WHERE id = $1`,
			e.id); err != nil {
			return fmt.Errorf("void session %s: %w", e.id, err)
		}
		return nil
	}

	if _, err := tx.Exec(ctx,
		`UPDATE raid_sessions
		 SET state = 'closed', outcome = 'died', closed_at = now(), result_items = '[]'::jsonb
		 WHERE id = $1`, e.id); err != nil {
		return fmt.Errorf("close session %s: %w", e.id, err)
	}
	return nil
}

// SweepBatchSize is how many expired sessions one sweep transaction closes.
//
// The sweep used to be a single unbounded transaction, which is fine at a
// steady state (a handful of rows) and pathological after one: closing a
// pending session issues one statement per loadout stack plus the state update,
// so a thousand-row backlog was ~14 000 sequential statements holding a
// thousand row locks for seconds — while sweepPlayerTx, which every
// /v1/raid/start runs with a plain FOR UPDATE and no SKIP LOCKED, queued behind
// it holding each player's garage_progress lock. One failing row also rolled
// back the whole batch, and a deterministic batch means one poison row stalls
// expiry cleanup forever.
//
// 200 keeps a pass at a few thousand statements and, more to the point, keeps
// the locks a pass holds bounded by a constant instead of by how bad the outage
// was. Committed passes also survive a later failure, so a pass that dies takes
// its own batch down and not the work already done.
//
// What this does NOT fix: a genuinely poisonous row still stalls the passes
// behind it, because the oldest expired rows are swept first and a poison row
// is by definition old. That is the audit's separate item (a last-success
// timestamp and a way past a bad row), and it was no better before.
const SweepBatchSize = 200

// SweepExpired closes every raid past its deadline, across all players.
// Pending sessions are voided and their loadout returned; active sessions
// are closed as died (§11: leaving the app counts as death).
//
// It runs bounded passes until one comes back short, each in its own
// transaction. Termination is not a matter of trust: every row a pass commits
// leaves the 'pending'/'active' predicate, so the candidate set strictly
// shrinks, and a pass that returns fewer rows than it asked for is the last
// one. Counts accumulated before an error are still returned — those passes
// committed.
func (s *Store) SweepExpired(ctx context.Context) (voided int, died int, err error) {
	for {
		v, d, err := s.SweepExpiredBatch(ctx, SweepBatchSize)
		voided += v
		died += d
		if err != nil {
			return voided, died, err
		}
		if v+d < SweepBatchSize {
			return voided, died, nil
		}
		if err := ctx.Err(); err != nil {
			return voided, died, err
		}
	}
}

// SweepExpiredBatch closes at most limit expired sessions in one transaction.
// It is one pass of SweepExpired, exported so a test can pin the bound itself
// rather than only the end state — which is identical either way.
func (s *Store) SweepExpiredBatch(ctx context.Context, limit int) (voided int, died int, err error) {
	if limit <= 0 {
		return 0, 0, nil
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return 0, 0, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// SKIP LOCKED lets a second instance sweep concurrently without blocking;
	// with the LIMIT it also means a pass can come back short because another
	// replica holds the rows, which correctly ends this instance's loop.
	// state is read as text for the same reason as everywhere else in this package.
	voided, died, err = closeExpiredTx(ctx, tx,
		`SELECT id, player_id, state::text, loadout FROM raid_sessions
		 WHERE state IN ('pending', 'active') AND expires_at <= now()
		 ORDER BY expires_at
		 LIMIT $1
		 FOR UPDATE SKIP LOCKED`, limit)
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
