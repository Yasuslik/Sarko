package store

import (
	"context"
	"crypto/subtle"
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
	if err := domain.ValidateStacks(p.Loadout); err != nil {
		return StartedRaid{}, err
	}
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

// sweepPlayerTx closes this player's expired raids inside an existing
// transaction. Pending sessions never entered the map, so their loadout goes
// back; active sessions ran out of time, which counts as death (§11). The
// scan-then-close logic is shared with SweepExpired via closeExpiredTx
// (sweeper.go); this just scopes the query to one player and one already-open
// transaction, so no SKIP LOCKED is needed here — the caller already holds
// the player's garage_progress row lock (see StartRaid).
func sweepPlayerTx(ctx context.Context, tx pgx.Tx, playerID string) error {
	// state is cast to text: pgx has no type mapping for our custom enum OIDs,
	// so every enum column is read and written as text throughout this package.
	_, _, err := closeExpiredTx(ctx, tx,
		`SELECT id, player_id, state::text, loadout FROM raid_sessions
		 WHERE player_id = $1 AND state IN ('pending', 'active') AND expires_at <= now()
		 FOR UPDATE`, playerID)
	return err
}

var (
	// ErrBadSessionToken means the token does not match the session.
	ErrBadSessionToken = errors.New("bad session token")
	// ErrSessionNotOpen means the session is voided or never existed.
	ErrSessionNotOpen = errors.New("session not open")
	// ErrSessionNotConfirmed means a result arrived for a session that never
	// confirmed it entered the map. It is deliberately its own error rather
	// than a shade of ErrSessionNotOpen: the session IS open, and the caller's
	// fix is to confirm it, which is worth saying out loud on an endpoint that
	// already distinguishes its refusals (unlike ConfirmRaid, which collapses
	// all of its own).
	ErrSessionNotConfirmed = errors.New("session not confirmed")
)

// SubmitResultParams is the input to SubmitResult.
type SubmitResultParams struct {
	// PlayerID is the authenticated caller. The session's owner must match it:
	// holding a session token is not by itself authorisation to close
	// somebody else's raid.
	PlayerID     string
	SessionID    string
	SessionToken string
	Outcome      domain.RaidOutcome
	Items        []domain.ItemStack
}

// RaidResult reports what the server recorded.
type RaidResult struct {
	SessionID     string             `json:"session_id"`
	Outcome       domain.RaidOutcome `json:"outcome"`
	CreditedItems []domain.ItemStack `json:"credited_items"`
	// AlreadyClosed is true when this call replayed an earlier result.
	AlreadyClosed bool `json:"already_closed"`
}

// ConfirmRaid marks a pending raid as actually entered and extends its deadline
// by deadline. Without this the sweeper returns the loadout (§11).
//
// deadline is one already-computed duration, not a policy: the caller decides
// what raid duration plus grace buffer means, and the store only records
// now() + deadline. The confirmed expires_at is returned so the client can
// align its own in-raid timer to the server's authoritative deadline instead
// of guessing it from its own configuration.
//
// playerID is the authenticated caller. A session token alone is not
// authorisation: possession of one must not let a caller drive somebody else's
// raid, so the session's owner has to match.
func (s *Store) ConfirmRaid(ctx context.Context, playerID, sessionID, sessionToken string, deadline time.Duration) (time.Time, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return time.Time{}, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var (
		stateText  string
		storedHash []byte
		ownerID    string
		loadoutRaw []byte
		expired    bool
	)
	// Enum columns are read as text — see the note in sweepPlayerTx.
	//
	// expires_at <= now() is evaluated by Postgres, not compared against the Go
	// clock, for the same reason SubmitResult does it that way: this call, that
	// one and the sweeper must all agree on what "late" means.
	err = tx.QueryRow(ctx,
		`SELECT state::text, session_token_hash, player_id, loadout, expires_at <= now()
		 FROM raid_sessions WHERE id = $1 FOR UPDATE`,
		sessionID,
	).Scan(&stateText, &storedHash, &ownerID, &loadoutRaw, &expired)
	notFound := errors.Is(err, pgx.ErrNoRows)
	if err != nil && !notFound {
		return time.Time{}, fmt.Errorf("load session: %w", err)
	}

	// Anti-oracle: unlike SubmitResult (which reports ErrNotFound,
	// ErrBadSessionToken and ErrSessionNotOpen as distinct errors), a caller
	// here must not be able to tell an unknown session id, a wrong token, a
	// non-pending session, somebody else's session and an expired one apart —
	// all five collapse to ErrSessionNotOpen. Wrong-owner deliberately gets no
	// error of its own: a distinct one would confirm that a guessed session id
	// exists.
	badToken := !notFound && subtle.ConstantTimeCompare(storedHash, auth.HashToken(sessionToken)) != 1
	wrongOwner := !notFound && ownerID != playerID
	if notFound || badToken || wrongOwner || stateText != string(domain.StatePending) {
		return time.Time{}, ErrSessionNotOpen
	}

	// Expiry is enforced here, not only by the sweeper.
	//
	// Without this the UPDATE below handed an already-dead pending session a
	// brand-new expires_at of now() + raid duration + grace buffer: PENDING_TTL
	// meant nothing whenever the sweeper had not reached the row yet, which is
	// a nondeterministic window at best and unbounded if the sweeper goroutine
	// stops (it has no restart and no health signal). SubmitResult already
	// claimed the invariant this restores — "the sweeper is a garbage collector
	// whose failure costs storage, not correctness" — and confirm was the hole
	// in it.
	//
	// The session is closed the way the sweeper would close it rather than just
	// refused, so the abandoned loadout comes back now instead of whenever the
	// sweeper next runs, and so the two paths cannot answer differently. A
	// pending session never entered the map, so this always voids and refunds.
	// The caller still sees the same uniform ErrSessionNotOpen: the refund is a
	// side effect on the player's own row, not information about it.
	if expired {
		var loadout []domain.ItemStack
		if err := json.Unmarshal(loadoutRaw, &loadout); err != nil {
			return time.Time{}, fmt.Errorf("unmarshal loadout: %w", err)
		}
		if err := closeExpiredSessionTx(ctx, tx, expiredSession{
			id:       sessionID,
			playerID: ownerID,
			state:    domain.StatePending,
			loadout:  loadout,
		}); err != nil {
			return time.Time{}, err
		}
		// Committed even though an error is returned: the void is the point of
		// this block, and the deferred rollback would throw it away.
		if err := tx.Commit(ctx); err != nil {
			return time.Time{}, fmt.Errorf("commit: %w", err)
		}
		return time.Time{}, ErrSessionNotOpen
	}

	var expiresAt time.Time
	err = tx.QueryRow(ctx,
		`UPDATE raid_sessions
		 SET state = 'active', confirmed_at = now(), expires_at = now() + $2::interval
		 WHERE id = $1
		 RETURNING expires_at`,
		sessionID, fmt.Sprintf("%d seconds", int(deadline.Seconds())),
	).Scan(&expiresAt)
	if err != nil {
		return time.Time{}, fmt.Errorf("confirm raid: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return time.Time{}, fmt.Errorf("commit: %w", err)
	}
	return expiresAt, nil
}

// SubmitResult closes a raid and credits surviving items, exactly once.
// Replays return the stored result instead of crediting again.
func (s *Store) SubmitResult(ctx context.Context, p SubmitResultParams) (RaidResult, error) {
	// Same exposure as StartRaid's loadout: a negative quantity would make
	// addItemsTx's arithmetic debit rather than credit, so validate before
	// any mutation (and before the transaction even opens).
	if err := domain.ValidateStacks(p.Items); err != nil {
		return RaidResult{}, err
	}
	items := domain.MergeStacks(p.Items)

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return RaidResult{}, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var (
		playerID    string
		stateText   string
		storedHash  []byte
		storedRaw   []byte
		outcomeText *string
		loadoutRaw  []byte
		expired     bool
	)
	// Enum columns are read as text — see the note in sweepPlayerTx.
	//
	// Expiry is evaluated by Postgres (expires_at <= now()) rather than
	// compared against the Go clock, so this call and the sweeper agree on
	// what "late" means even if the app server's clock has drifted. now() is
	// the transaction's start time, which is also what the sweeper uses.
	err = tx.QueryRow(ctx,
		`SELECT player_id, state::text, session_token_hash, result_items, outcome::text,
		        loadout, expires_at <= now()
		 FROM raid_sessions WHERE id = $1 FOR UPDATE`, p.SessionID,
	).Scan(&playerID, &stateText, &storedHash, &storedRaw, &outcomeText, &loadoutRaw, &expired)
	if errors.Is(err, pgx.ErrNoRows) {
		return RaidResult{}, ErrNotFound
	}
	if err != nil {
		return RaidResult{}, fmt.Errorf("load session: %w", err)
	}
	state := domain.RaidState(stateText)

	// The token is checked before anything else, and for every state.
	//
	// A caller who is not the session's owner is treated exactly like a caller
	// with a bad token. That is the point: it leaks nothing this endpoint did
	// not already leak, so someone who guesses a session id learns only what
	// they would have learnt by guessing a token too. Both branches are folded
	// into one return so the two cases stay indistinguishable.
	tokenOK := subtle.ConstantTimeCompare(storedHash, auth.HashToken(p.SessionToken)) == 1
	if !tokenOK || playerID != p.PlayerID {
		return RaidResult{}, ErrBadSessionToken
	}

	// Idempotency: a closed session replays its stored answer.
	if state == domain.StateClosed {
		out := RaidResult{SessionID: p.SessionID, AlreadyClosed: true, CreditedItems: []domain.ItemStack{}}
		if outcomeText != nil {
			out.Outcome = domain.RaidOutcome(*outcomeText)
		}
		if len(storedRaw) > 0 {
			if err := json.Unmarshal(storedRaw, &out.CreditedItems); err != nil {
				return RaidResult{}, fmt.Errorf("unmarshal stored result: %w", err)
			}
		}
		if err := tx.Commit(ctx); err != nil {
			return RaidResult{}, fmt.Errorf("commit: %w", err)
		}
		return out, nil
	}

	if state != domain.StatePending && state != domain.StateActive {
		return RaidResult{}, ErrSessionNotOpen
	}

	// Expiry is enforced here, not only by the sweeper.
	//
	// Before this, "leaving the app counts as death" (§11) held only while the
	// sweeper goroutine was making progress: if it stalled, an expired session
	// stayed extractable indefinitely and abandoned loadouts were never
	// returned. The sweeper is now a garbage collector whose failure costs
	// storage, not correctness.
	//
	// Note the interaction with the grace buffer: expires_at already includes
	// it, so a result that arrives inside the buffer is *not* expired and is
	// credited normally. Only a result past the buffer lands here.
	if expired {
		var loadout []domain.ItemStack
		if err := json.Unmarshal(loadoutRaw, &loadout); err != nil {
			return RaidResult{}, fmt.Errorf("unmarshal loadout: %w", err)
		}
		e := expiredSession{id: p.SessionID, playerID: playerID, state: state, loadout: loadout}
		if err := closeExpiredSessionTx(ctx, tx, e); err != nil {
			return RaidResult{}, err
		}

		// The transaction is committed on both branches below, including the
		// one that returns an error: the close is the point of this block, and
		// the deferred rollback would otherwise throw it away and leave the
		// session open for the next attempt.
		if err := tx.Commit(ctx); err != nil {
			return RaidResult{}, fmt.Errorf("commit: %w", err)
		}

		if state == domain.StatePending {
			// Voided, loadout returned. Same answer the caller would get had
			// the sweeper voided it a moment earlier.
			return RaidResult{}, ErrSessionNotOpen
		}
		// Closed as died, nothing credited. Deliberately byte-identical to the
		// replay a caller gets when the sweeper closed the session first, so
		// the outcome does not depend on which side won the race.
		return RaidResult{
			SessionID:     p.SessionID,
			Outcome:       domain.OutcomeDied,
			CreditedItems: []domain.ItemStack{},
			AlreadyClosed: true,
		}, nil
	}

	// Only a confirmed raid can produce a result.
	//
	// `pending` means the loadout was debited and the client never entered the
	// map — that is the entire distinction ConfirmRaid exists to draw, and the
	// one closeExpiredSessionTx honours by giving the loadout back rather than
	// crediting anything. This function used to accept `pending` anyway and
	// credit a full haul, so
	//
	//     POST /v1/raid/start  ->  POST /v1/raid/result
	//
	// in a loop banked a backpack per round trip with zero gameplay, and the
	// tutorial latch below fired on the first of them — permanently skipping
	// the tutorial for an account that had never played a raid. The loadout
	// debit deterred nothing, because the client sends an empty loadout.
	//
	// Placed after the expiry block on purpose: an expired pending session is
	// the sweeper's case, not this one, and it keeps answering ErrSessionNotOpen
	// with the loadout returned. A live pending session is a caller that skipped
	// a step, and it is told exactly that.
	//
	// The session is deliberately left alone — not voided, not closed. A stray
	// or replayed result must not be able to destroy a raid the player is about
	// to confirm and play; it expires on its own if they never do, and the
	// sweeper refunds it then.
	if state == domain.StatePending {
		return RaidResult{}, ErrSessionNotConfirmed
	}

	// Ordered players-before-stash_items deliberately. GrantStarterKit locks
	// players then stash_items; if this function locked stash_items first it
	// would close a cycle with it, and Postgres would abort one of the two.
	// A deadlock here costs a player their whole extracted haul: the client does
	// not retry a failed result submission, so the session would expire and the
	// sweeper would close it as `died`.
	// Spec §6.5: the tutorial completes on the first *successful* raid. Set in
	// this transaction, so "the haul was credited" and "the tutorial is over"
	// can never disagree — a separate call could be interrupted between them and
	// leave a player who has banked tutorial loot still reading fixed lists.
	//
	// `AND NOT tutorial_completed` makes it a one-way latch and makes the
	// statement a no-op for every raid after the first, so this costs one indexed
	// row lookup per extraction and nothing else.
	//
	// Deliberately not reached on the two early-return paths above: a replay of
	// an already-closed session must not re-latch anything (it credits nothing),
	// and an expired session is closed as `died` regardless of what the client
	// claimed, so it is not a successful raid.
	//
	// playerID is the value loaded from the session row, not p.PlayerID: the two
	// are already proven equal by the token check above, and using the loaded one
	// keeps every write in this function keyed off the row it locked.
	if p.Outcome == domain.OutcomeExtracted {
		if _, err := tx.Exec(ctx,
			`UPDATE players SET tutorial_completed = true
			 WHERE id = $1 AND NOT tutorial_completed`, playerID); err != nil {
			return RaidResult{}, fmt.Errorf("complete tutorial: %w", err)
		}
	}

	if err := addItemsTx(ctx, tx, playerID, items); err != nil {
		return RaidResult{}, err
	}

	itemsJSON, err := json.Marshal(items)
	if err != nil {
		return RaidResult{}, fmt.Errorf("marshal result items: %w", err)
	}
	_, err = tx.Exec(ctx,
		`UPDATE raid_sessions
		 SET state = 'closed', outcome = $2::raid_outcome, result_items = $3, closed_at = now()
		 WHERE id = $1`, p.SessionID, string(p.Outcome), itemsJSON)
	if err != nil {
		return RaidResult{}, fmt.Errorf("close session: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return RaidResult{}, fmt.Errorf("commit: %w", err)
	}
	return RaidResult{SessionID: p.SessionID, Outcome: p.Outcome, CreditedItems: items}, nil
}
