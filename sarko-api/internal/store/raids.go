package store

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"math/rand/v2"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/domain"
)

var (
	// ErrRaidInProgress means the player already has an open raid.
	ErrRaidInProgress = errors.New("raid already in progress")
	// ErrInsufficientItems means the loadout asks for more than the stash holds.
	ErrInsufficientItems = errors.New("insufficient items")
	// ErrMapLocked means the player's vehicle tier does not unlock the map.
	ErrMapLocked = errors.New("map locked")
)

// StartRaidParams is the input to StartRaid.
type StartRaidParams struct {
	PlayerID   string
	MapID      string
	Loadout    []domain.ItemStack
	PendingTTL time.Duration
}

// StartedRaid is returned once, and carries the only copy of the plaintext token.
type StartedRaid struct {
	SessionID    string    `json:"session_id"`
	SessionToken string    `json:"session_token"`
	Seed         int64     `json:"seed"`
	ExpiresAt    time.Time `json:"expires_at"`
}

// StartRaid debits the loadout and opens a pending session, atomically.
// Either the player loses the items and gets a session, or nothing happens.
func (s *Store) StartRaid(ctx context.Context, p StartRaidParams) (StartedRaid, error) {
	loadout := domain.MergeStacks(p.Loadout)

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return StartedRaid{}, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// Lock the player row so two concurrent starts serialise here.
	var tier domain.Tier
	err = tx.QueryRow(ctx,
		`SELECT vehicle_tier FROM garage_progress WHERE player_id = $1 FOR UPDATE`,
		p.PlayerID).Scan(&tier)
	if errors.Is(err, pgx.ErrNoRows) {
		return StartedRaid{}, ErrNotFound
	}
	if err != nil {
		return StartedRaid{}, fmt.Errorf("lock garage row: %w", err)
	}

	if !mapUnlocked(tier, p.MapID) {
		return StartedRaid{}, ErrMapLocked
	}

	// Close anything already expired so an abandoned raid never blocks forever.
	if err := sweepPlayerTx(ctx, tx, p.PlayerID); err != nil {
		return StartedRaid{}, err
	}

	var open int
	err = tx.QueryRow(ctx,
		`SELECT count(*) FROM raid_sessions
		 WHERE player_id = $1 AND state IN ('pending', 'active')`, p.PlayerID).Scan(&open)
	if err != nil {
		return StartedRaid{}, fmt.Errorf("count open raids: %w", err)
	}
	if open > 0 {
		return StartedRaid{}, ErrRaidInProgress
	}

	if err := debitItemsTx(ctx, tx, p.PlayerID, loadout); err != nil {
		return StartedRaid{}, err
	}

	plain, hash, err := auth.NewSessionToken()
	if err != nil {
		return StartedRaid{}, err
	}
	loadoutJSON, err := json.Marshal(loadout)
	if err != nil {
		return StartedRaid{}, fmt.Errorf("marshal loadout: %w", err)
	}

	out := StartedRaid{SessionToken: plain, Seed: int64(rand.Uint32())}
	err = tx.QueryRow(ctx,
		`INSERT INTO raid_sessions
		     (player_id, map_id, seed, session_token_hash, loadout, expires_at)
		 VALUES ($1, $2, $3, $4, $5, now() + $6::interval)
		 RETURNING id, expires_at`,
		p.PlayerID, p.MapID, out.Seed, hash, loadoutJSON,
		fmt.Sprintf("%d seconds", int(p.PendingTTL.Seconds())),
	).Scan(&out.SessionID, &out.ExpiresAt)
	if err != nil {
		return StartedRaid{}, fmt.Errorf("insert session: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return StartedRaid{}, fmt.Errorf("commit: %w", err)
	}
	return out, nil
}

func mapUnlocked(tier domain.Tier, mapID string) bool {
	for _, m := range domain.UnlockedMaps(tier) {
		if m == mapID {
			return true
		}
	}
	return false
}

// debitItemsTx removes stacks from a stash, failing if any is short.
//
// The schema's stash_items has CHECK (quantity > 0), so an UPDATE that would
// leave a stack at exactly zero must never run — it would fail the check
// constraint instead of behaving like "empty the stack". So a debit that
// exactly exhausts a stack is a DELETE, and a debit that leaves a remainder
// is an UPDATE; each is still a single conditional statement (quantity
// compared in the WHERE clause), so a short stash is still detected
// atomically by the statement itself rather than by a read-then-write race.
func debitItemsTx(ctx context.Context, tx pgx.Tx, playerID string, stacks []domain.ItemStack) error {
	for _, item := range stacks {
		tag, err := tx.Exec(ctx,
			`UPDATE stash_items SET quantity = quantity - $3
			 WHERE player_id = $1 AND item_id = $2 AND quantity > $3`,
			playerID, item.ItemID, item.Quantity)
		if err != nil {
			return fmt.Errorf("debit %s: %w", item.ItemID, err)
		}
		if tag.RowsAffected() == 1 {
			continue
		}

		tag, err = tx.Exec(ctx,
			`DELETE FROM stash_items WHERE player_id = $1 AND item_id = $2 AND quantity = $3`,
			playerID, item.ItemID, item.Quantity)
		if err != nil {
			return fmt.Errorf("debit %s: %w", item.ItemID, err)
		}
		if tag.RowsAffected() != 1 {
			return fmt.Errorf("%w: %s x%d", ErrInsufficientItems, item.ItemID, item.Quantity)
		}
	}
	return nil
}

// sweepPlayerTx closes this player's expired raids inside an existing transaction.
// Pending sessions never entered the map, so their loadout goes back; active
// sessions ran out of time, which counts as death (§11).
func sweepPlayerTx(ctx context.Context, tx pgx.Tx, playerID string) error {
	// state is cast to text: pgx has no type mapping for our custom enum OIDs,
	// so every enum column is read and written as text throughout this package.
	rows, err := tx.Query(ctx,
		`SELECT id, state::text, loadout FROM raid_sessions
		 WHERE player_id = $1 AND state IN ('pending', 'active') AND expires_at <= now()
		 FOR UPDATE`, playerID)
	if err != nil {
		return fmt.Errorf("select expired: %w", err)
	}

	type expired struct {
		id      string
		state   domain.RaidState
		loadout []domain.ItemStack
	}
	var batch []expired

	for rows.Next() {
		var e expired
		var raw []byte
		var state string
		if err := rows.Scan(&e.id, &state, &raw); err != nil {
			rows.Close()
			return fmt.Errorf("scan expired: %w", err)
		}
		e.state = domain.RaidState(state)
		if err := json.Unmarshal(raw, &e.loadout); err != nil {
			rows.Close()
			return fmt.Errorf("unmarshal loadout: %w", err)
		}
		batch = append(batch, e)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return fmt.Errorf("iterate expired: %w", err)
	}

	for _, e := range batch {
		if e.state == domain.StatePending {
			if err := addItemsTx(ctx, tx, playerID, e.loadout); err != nil {
				return err
			}
			_, err = tx.Exec(ctx,
				`UPDATE raid_sessions SET state = 'voided', closed_at = now() WHERE id = $1`, e.id)
		} else {
			_, err = tx.Exec(ctx,
				`UPDATE raid_sessions
				 SET state = 'closed', outcome = 'died', closed_at = now(), result_items = '[]'::jsonb
				 WHERE id = $1`, e.id)
		}
		if err != nil {
			return fmt.Errorf("close expired session %s: %w", e.id, err)
		}
	}
	return nil
}
