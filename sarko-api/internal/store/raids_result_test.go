package store_test

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

// sessionState reads a raid session's state and confirmed_at directly, bypassing
// the store, so ConfirmRaid rejection tests can assert a rejected call left the
// row untouched.
func sessionState(t *testing.T, pool *pgxpool.Pool, sessionID string) (state string, confirmedAt *time.Time) {
	t.Helper()
	err := pool.QueryRow(context.Background(),
		`SELECT state::text, confirmed_at FROM raid_sessions WHERE id = $1`, sessionID,
	).Scan(&state, &confirmedAt)
	if err != nil {
		t.Fatalf("query session state: %v", err)
	}
	return state, confirmedAt
}

func TestSubmitResultCreditsExtractedLoot(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-extract", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	res, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 1}},
	})
	if err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}
	if res.AlreadyClosed {
		t.Error("first submit must not report AlreadyClosed")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].ItemID != "turbine" {
		t.Errorf("stash = %v, want one turbine", profile.Stash)
	}
}

func TestSubmitResultIsIdempotent(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-idem", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	params := store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "battery", Quantity: 1}},
	}
	if _, err := s.SubmitResult(ctx, params); err != nil {
		t.Fatalf("first SubmitResult: %v", err)
	}

	second, err := s.SubmitResult(ctx, params)
	if err != nil {
		t.Fatalf("second SubmitResult must succeed, got %v", err)
	}
	if !second.AlreadyClosed {
		t.Error("second submit must report AlreadyClosed")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 1 {
		t.Fatalf("replayed result duplicated loot: %v", profile.Stash)
	}
}

func TestSubmitResultRejectsWrongToken(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-forged", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	_, err = s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: "forged-token",
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 99}},
	})
	if !errors.Is(err, store.ErrBadSessionToken) {
		t.Fatalf("err = %v, want ErrBadSessionToken", err)
	}
}

// TestSubmitResultRejectsAnotherPlayersSession is the same hardening on the
// endpoint that actually moves items. A wrong owner must look exactly like a
// wrong token — ErrBadSessionToken, not a distinct error — so the check adds
// no new way to probe which session ids exist. And crucially: nothing may be
// credited to either player, and the session must stay open for its owner.
func TestSubmitResultRejectsAnotherPlayersSession(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()

	owner := seedPlayer(t, s, "dev-result-owner", []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})
	intruder := seedPlayer(t, s, "dev-result-intruder", nil)

	started, err := s.StartRaid(ctx, startParams(owner, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, owner, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	// Correct session id, correct token, wrong caller.
	_, err = s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     intruder,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 10}},
	})
	if !errors.Is(err, store.ErrBadSessionToken) {
		t.Fatalf("err = %v, want ErrBadSessionToken (indistinguishable from a forged token)", err)
	}

	for _, c := range []struct {
		name     string
		playerID string
	}{{"intruder", intruder}, {"owner", owner}} {
		profile, err := s.Profile(ctx, c.playerID)
		if err != nil {
			t.Fatalf("Profile(%s): %v", c.name, err)
		}
		if len(profile.Stash) != 0 {
			t.Errorf("%s stash = %v, want empty — a refused result must credit nobody", c.name, profile.Stash)
		}
	}

	state, _ := sessionState(t, pool, started.SessionID)
	if state != "active" {
		t.Errorf("state = %q, want active — a refused result must not close the raid", state)
	}

	// The owner's own result still works, and closes the raid normally.
	res, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     owner,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 10}},
	})
	if err != nil {
		t.Fatalf("the owner must still be able to submit: %v", err)
	}
	if res.AlreadyClosed {
		t.Error("the owner's submit must be the first close, not a replay")
	}
}

func TestDeathCreditsOnlyWhatWasSubmitted(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-died", []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})

	started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	// Died carrying one safe-pocket item; the rifle taken into the raid is gone.
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeDied,
		Items:        []domain.ItemStack{{ItemID: "gearbox", Quantity: 1}},
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["gearbox"] != 1 {
		t.Errorf("safe pocket item missing: %v", profile.Stash)
	}
	if _, ok := got["rifle"]; ok {
		t.Error("the rifle carried into the raid must be lost on death")
	}
}

// TestStartThenResultWithoutConfirmBanksNothing is the exploit loop itself.
//
// start -> result, as fast as the network allows, with no confirm in between
// and therefore no gameplay at all: the session is still `pending`, which means
// "loadout debited, the client never entered the map". SubmitResult used to
// accept that and credit the full haul, so the loop banked a backpack per round
// trip. Nothing deterred it — the client's loadout is empty, so a start costs
// nothing to lose.
//
// Every other SubmitResult test in this file calls ConfirmRaid first, which is
// exactly why nothing covered this.
func TestStartThenResultWithoutConfirmBanksNothing(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-no-confirm", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	// No ConfirmRaid. Straight to the payday.
	_, err = s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 13}},
	})
	if !errors.Is(err, store.ErrSessionNotConfirmed) {
		t.Fatalf("err = %v, want ErrSessionNotConfirmed — a raid nobody entered cannot produce a haul", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("stash = %v, want empty — the loop banked loot", profile.Stash)
	}
	// The same call also latched the tutorial flag, permanently skipping the
	// tutorial for an account that had never played a raid.
	if profile.TutorialCompleted {
		t.Error("an unconfirmed result completed the tutorial")
	}

	// The session is untouched, deliberately: a stray or replayed result must
	// not destroy a raid the player is about to confirm and play.
	state, confirmedAt := sessionState(t, pool, started.SessionID)
	if state != "pending" {
		t.Errorf("state = %q, want pending — a refused result must not close the session", state)
	}
	if confirmedAt != nil {
		t.Errorf("confirmed_at = %v, want nil", confirmedAt)
	}

	// And the honest path still works from exactly here: confirm, then submit.
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid after a refused result: %v", err)
	}
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 1}},
	}); err != nil {
		t.Fatalf("a confirmed raid must still submit: %v", err)
	}
}

// TestResultLoopWithoutConfirmNeverPays runs the loop rather than one round of
// it, because "free money per round trip" is the actual finding: one refusal
// proves the check exists, ten prove the loop cannot be ground.
func TestResultLoopWithoutConfirmNeverPays(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-loop", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	for i := 0; i < 10; i++ {
		_, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeExtracted,
			Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 13}},
		})
		if !errors.Is(err, store.ErrSessionNotConfirmed) {
			t.Fatalf("round %d: err = %v, want ErrSessionNotConfirmed", i, err)
		}
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("ten rounds of the loop banked %v", profile.Stash)
	}
}

// TestConfirmRaidRejectsAnExpiredPendingSession pins the other half of the same
// invariant. ConfirmRaid never looked at expires_at, so a pending session the
// sweeper had not reached yet was confirmed anyway and handed a brand-new
// deadline of now() + raid duration + grace buffer. PENDING_TTL therefore meant
// nothing whenever the sweeper was behind — a nondeterministic window at best,
// and unbounded if the sweeper goroutine had stopped.
//
// The sweeper is deliberately never called here: that is the whole point.
func TestConfirmRaidRejectsAnExpiredPendingSession(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-expired", []domain.ItemStack{{ItemID: "ammo", Quantity: 30}})

	p := startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 30}})
	p.PendingTTL = -time.Second // the client never entered, and the deadline passed
	started, err := s.StartRaid(ctx, p)
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	_, err = s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour, 0)
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen (the uniform refusal)", err)
	}

	// Closed the way the sweeper would close it: voided, loadout back. Not
	// "active with an hour on the clock", which is what it used to be.
	state, confirmedAt := sessionState(t, pool, started.SessionID)
	if state != "voided" {
		t.Errorf("state = %q, want voided", state)
	}
	if confirmedAt != nil {
		t.Errorf("confirmed_at = %v, want nil — an expired session was never entered", confirmedAt)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["ammo"] != 30 {
		t.Errorf("loadout was not returned: %v", profile.Stash)
	}

	// The refused confirm freed the player, so a new raid starts immediately
	// rather than waiting for the sweeper.
	if _, err := s.StartRaid(ctx, startParams(playerID, nil)); err != nil {
		t.Errorf("a new raid must start after an expired session is refused: %v", err)
	}
}

// TestConfirmRaidRejectsWrongToken, TestConfirmRaidRejectsUnknownSessionID,
// TestConfirmRaidRejectsAlreadyActiveSession and
// TestConfirmRaidRejectsAnotherPlayersSession together prove ConfirmRaid's
// anti-oracle property: a wrong token, an unknown session id, a non-pending
// session and somebody else's session are indistinguishable to the caller —
// all four return ErrSessionNotOpen and leave the session's state and
// confirmed_at untouched. This is deliberately different from SubmitResult,
// which does report ErrBadSessionToken / ErrNotFound / ErrSessionNotOpen
// separately.

func TestConfirmRaidRejectsWrongToken(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-wrong-token", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	_, err = s.ConfirmRaid(ctx, playerID, started.SessionID, "forged-token", time.Minute, 0)
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen", err)
	}

	state, confirmedAt := sessionState(t, pool, started.SessionID)
	if state != "pending" {
		t.Errorf("state = %q, want pending (rejected confirm must not change it)", state)
	}
	if confirmedAt != nil {
		t.Errorf("confirmed_at = %v, want nil", confirmedAt)
	}
}

func TestConfirmRaidRejectsUnknownSessionID(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-unknown-id", nil)

	// A real session and its real token exist so the rejection below is
	// provably about the (wrong) session id, not a coincidentally bad token.
	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	_, err = s.ConfirmRaid(ctx, playerID, "00000000-0000-0000-0000-000000000000", started.SessionToken, time.Minute, 0)
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen", err)
	}

	state, confirmedAt := sessionState(t, pool, started.SessionID)
	if state != "pending" {
		t.Errorf("real session state = %q, want pending (untouched)", state)
	}
	if confirmedAt != nil {
		t.Errorf("real session confirmed_at = %v, want nil", confirmedAt)
	}
}

// TestConfirmRaidRejectsAnotherPlayersSession: a session token is not by
// itself authorisation. Before this check, anyone holding a leaked or
// intercepted token could drive another player's raid — start its clock, and
// (via SubmitResult) decide whether they lived or died. The rejection must be
// the same uniform ErrSessionNotOpen as every other refusal, so it does not
// become an oracle for "this session id exists".
func TestConfirmRaidRejectsAnotherPlayersSession(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()

	owner := seedPlayer(t, s, "dev-confirm-owner", nil)
	intruder := seedPlayer(t, s, "dev-confirm-intruder", nil)

	started, err := s.StartRaid(ctx, startParams(owner, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	// The correct session id and the correct token — only the caller is wrong.
	_, err = s.ConfirmRaid(ctx, intruder, started.SessionID, started.SessionToken, time.Minute, 0)
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen (the same error every other refusal gives)", err)
	}

	state, confirmedAt := sessionState(t, pool, started.SessionID)
	if state != "pending" {
		t.Errorf("state = %q, want pending — an intruder must not start the raid clock", state)
	}
	if confirmedAt != nil {
		t.Errorf("confirmed_at = %v, want nil", confirmedAt)
	}

	// The owner is unaffected and can still confirm.
	if _, err := s.ConfirmRaid(ctx, owner, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("the owner must still be able to confirm: %v", err)
	}
}

func TestConfirmRaidRejectsAlreadyActiveSession(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-double", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("first ConfirmRaid: %v", err)
	}
	_, firstConfirmedAt := sessionState(t, pool, started.SessionID)
	if firstConfirmedAt == nil {
		t.Fatal("confirmed_at must be set after the first confirm")
	}

	_, err = s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0)
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen", err)
	}

	state, secondConfirmedAt := sessionState(t, pool, started.SessionID)
	if state != "active" {
		t.Errorf("state = %q, want active (unchanged)", state)
	}
	if secondConfirmedAt == nil || !secondConfirmedAt.Equal(*firstConfirmedAt) {
		t.Errorf("confirmed_at changed on rejected double-confirm: %v -> %v", firstConfirmedAt, secondConfirmedAt)
	}
}

// TestConfirmRaidReturnsStoredDeadline proves the timestamp ConfirmRaid hands
// back is the row's own expires_at, not a value recomputed in Go. The client
// aligns its in-raid timer to it, so a drift between the two would put the
// client's idea of the deadline ahead of the server's — exactly the failure
// mode the grace buffer exists to prevent.
func TestConfirmRaidReturnsStoredDeadline(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-deadline", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	const deadline = 14 * time.Minute
	before := time.Now()
	expiresAt, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, deadline, 0)
	if err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}
	if expiresAt.IsZero() {
		t.Fatal("ConfirmRaid must return the confirmed deadline")
	}

	var stored time.Time
	if err := pool.QueryRow(ctx,
		`SELECT expires_at FROM raid_sessions WHERE id = $1`, started.SessionID).Scan(&stored); err != nil {
		t.Fatalf("read expires_at: %v", err)
	}
	if !expiresAt.Equal(stored) {
		t.Errorf("returned expires_at = %v, stored = %v — they must be the same instant", expiresAt, stored)
	}

	want := before.Add(deadline)
	if diff := expiresAt.Sub(want); diff < -5*time.Second || diff > 5*time.Second {
		t.Errorf("expires_at = %v, want ~%v (now + deadline)", expiresAt, want)
	}
	// The pending deadline must have been replaced, not kept.
	if !expiresAt.After(started.ExpiresAt) {
		t.Errorf("confirmed expires_at %v is not past the pending one %v", expiresAt, started.ExpiresAt)
	}
}

// The four tests below pin the boundary that decides whether a player keeps a
// raid's worth of loot. They deliberately never run the sweeper: expiry must
// be enforced by SubmitResult itself, so that a stalled sweeper goroutine
// costs storage rather than correctness.

// TestSubmitResultCreditsAResultInsideTheDeadline is the on-time side of the
// boundary — and the side that matters most, because this is where the grace
// buffer does its job. A result arriving before expires_at (which already
// includes the buffer) must be credited in full.
func TestSubmitResultCreditsAResultInsideTheDeadline(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-inside-deadline", []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})

	started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	// A deadline a comfortable distance in the future stands in for "the raid
	// duration plus the grace buffer, and the result got here in time".
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	res, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 3}},
	})
	if err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}
	if res.Outcome != domain.OutcomeExtracted {
		t.Errorf("outcome = %q, want extracted", res.Outcome)
	}
	if res.AlreadyClosed {
		t.Error("an on-time result must not be reported as already closed")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["turbine"] != 3 {
		t.Errorf("extracted loot was not credited: %v", profile.Stash)
	}
}

// TestSubmitResultOnExpiredActiveSessionRecordsDeath is the late side. The
// sweeper is never called, so if SubmitResult did not check expires_at itself
// the player would be credited for a raid they had already lost.
func TestSubmitResultOnExpiredActiveSessionRecordsDeath(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-late-result", []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})

	started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	// Confirm with a deadline already in the past: past the grace buffer too.
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, -time.Second, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	res, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 99}},
	})
	if err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}
	if res.Outcome != domain.OutcomeDied {
		t.Errorf("outcome = %q, want died — an expired active raid is a death", res.Outcome)
	}
	if len(res.CreditedItems) != 0 {
		t.Errorf("credited %v, want nothing", res.CreditedItems)
	}

	state, _ := sessionState(t, pool, started.SessionID)
	if state != "closed" {
		t.Errorf("state = %q, want closed", state)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("nothing may be credited for an expired raid, got %v", profile.Stash)
	}
}

// TestSubmitResultOnExpiredPendingSessionReturnsLoadout covers the other
// expiry outcome: a session that never entered the map gives the loadout back,
// exactly as the sweeper would, and the result is refused.
func TestSubmitResultOnExpiredPendingSessionReturnsLoadout(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-late-pending", []domain.ItemStack{{ItemID: "ammo", Quantity: 30}})

	p := startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 30}})
	p.PendingTTL = -time.Second // never confirmed, already past the deadline
	started, err := s.StartRaid(ctx, p)
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	_, err = s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 5}},
	})
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen", err)
	}

	state, _ := sessionState(t, pool, started.SessionID)
	if state != "voided" {
		t.Errorf("state = %q, want voided", state)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["ammo"] != 30 {
		t.Errorf("loadout was not returned: %v", profile.Stash)
	}
	if _, ok := got["turbine"]; ok {
		t.Error("a refused result must credit nothing")
	}
}

// TestSubmitResultExpiryMatchesTheSweeper checks the two paths cannot drift:
// whether the sweeper or the request itself notices the expiry, the caller
// sees the same answer and the stash ends up the same. Without this, closing
// an expired session in SubmitResult could quietly diverge from §11.
func TestSubmitResultExpiryMatchesTheSweeper(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	submitLate := func(device string, sweepFirst bool) (store.RaidResult, error, []domain.ItemStack) {
		t.Helper()
		playerID := seedPlayer(t, s, device, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})
		started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, -time.Second, 0); err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}
		if sweepFirst {
			if _, _, err := s.SweepExpired(ctx); err != nil {
				t.Fatalf("SweepExpired: %v", err)
			}
		}
		res, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeExtracted,
			Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 7}},
		})
		profile, perr := s.Profile(ctx, playerID)
		if perr != nil {
			t.Fatalf("Profile: %v", perr)
		}
		return res, err, profile.Stash
	}

	swept, sweptErr, sweptStash := submitLate("dev-race-sweeper", true)
	self, selfErr, selfStash := submitLate("dev-race-self", false)

	if sweptErr != nil || selfErr != nil {
		t.Fatalf("errors differ or are unexpected: sweeper-first=%v, self=%v", sweptErr, selfErr)
	}
	if swept.Outcome != self.Outcome {
		t.Errorf("outcome differs: sweeper-first=%q, self=%q", swept.Outcome, self.Outcome)
	}
	if swept.AlreadyClosed != self.AlreadyClosed {
		t.Errorf("AlreadyClosed differs: sweeper-first=%v, self=%v", swept.AlreadyClosed, self.AlreadyClosed)
	}
	if len(swept.CreditedItems) != 0 || len(self.CreditedItems) != 0 {
		t.Errorf("credited items must be empty on both paths: %v / %v", swept.CreditedItems, self.CreditedItems)
	}
	if len(sweptStash) != 0 || len(selfStash) != 0 {
		t.Errorf("stash must be empty on both paths: %v / %v", sweptStash, selfStash)
	}
}

func TestPendingRaidExpiryReturnsLoadout(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-noshow", []domain.ItemStack{{ItemID: "ammo", Quantity: 10}})

	p := startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 10}})
	p.PendingTTL = -time.Second // already expired: the client never entered
	if _, err := s.StartRaid(ctx, p); err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	// A new start sweeps the abandoned session first.
	if _, err := s.StartRaid(ctx, startParams(playerID, nil)); err != nil {
		t.Fatalf("second StartRaid: %v", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 10 {
		t.Errorf("loadout was not returned: %v", profile.Stash)
	}
}

func TestExtractedResultSetsTheTutorialFlag(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "device-tutorial-extract", nil)

	// Before the first raid the flag must be false, because false is what puts a
	// client into tutorial mode. A default of true would silently skip the
	// tutorial for every player who ever existed.
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Fatal("a brand-new player is already marked as having completed the tutorial")
	}

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "scrap_metal", Quantity: 2}},
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	profile, err = s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile after extraction: %v", err)
	}
	if !profile.TutorialCompleted {
		t.Error("a successful extraction did not complete the tutorial")
	}
}

func TestDiedResultLeavesTheTutorialFlagAlone(t *testing.T) {
	// Spec §6.5: "dying replays the tutorial with the same static layout". So
	// death must not set the flag — that is the whole difference between the two
	// outcomes as far as the tutorial is concerned.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "device-tutorial-death", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeDied,
		Items:        nil,
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Error("dying completed the tutorial; it must replay instead")
	}
}

func TestTutorialFlagNeverUnsetsAndSurvivesALaterDeath(t *testing.T) {
	// One-way latch. The flag decides whether containers roll or read a fixed
	// list, so a flag that could go back to false would put a veteran back into
	// the tutorial's static loot — and, worse, make the loot a player sees depend
	// on the order of their last two raids.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "device-tutorial-latch", nil)

	raid := func(outcome domain.RaidOutcome) {
		t.Helper()
		started, err := s.StartRaid(ctx, startParams(playerID, nil))
		if err != nil {
			t.Fatalf("StartRaid(%s): %v", outcome, err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour, 0); err != nil {
			t.Fatalf("ConfirmRaid(%s): %v", outcome, err)
		}
		if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      outcome,
		}); err != nil {
			t.Fatalf("SubmitResult(%s): %v", outcome, err)
		}
	}

	raid(domain.OutcomeExtracted)
	raid(domain.OutcomeDied)
	raid(domain.OutcomeExtracted)

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if !profile.TutorialCompleted {
		t.Error("the tutorial flag came back off after a death following a successful raid")
	}
}
