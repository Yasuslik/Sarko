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
	if err := s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, time.Minute); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	res, err := s.SubmitResult(ctx, store.SubmitResultParams{
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
	if err := s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, time.Minute); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	params := store.SubmitResultParams{
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
		SessionID:    started.SessionID,
		SessionToken: "forged-token",
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "turbine", Quantity: 99}},
	})
	if !errors.Is(err, store.ErrBadSessionToken) {
		t.Fatalf("err = %v, want ErrBadSessionToken", err)
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
	if err := s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, time.Minute); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	// Died carrying one safe-pocket item; the rifle taken into the raid is gone.
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
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

// TestConfirmRaidRejectsWrongToken, TestConfirmRaidRejectsUnknownSessionID and
// TestConfirmRaidRejectsAlreadyActiveSession together prove ConfirmRaid's
// anti-oracle property: a wrong token, an unknown session id, and a
// non-pending session are indistinguishable to the caller — all three return
// ErrSessionNotOpen and leave the session's state and confirmed_at untouched.
// This is deliberately different from SubmitResult, which does report
// ErrBadSessionToken / ErrNotFound / ErrSessionNotOpen separately.

func TestConfirmRaidRejectsWrongToken(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-wrong-token", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	err = s.ConfirmRaid(ctx, started.SessionID, "forged-token", time.Minute)
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

	err = s.ConfirmRaid(ctx, "00000000-0000-0000-0000-000000000000", started.SessionToken, time.Minute)
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

func TestConfirmRaidRejectsAlreadyActiveSession(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-confirm-double", nil)

	started, err := s.StartRaid(ctx, startParams(playerID, nil))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if err := s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, time.Minute); err != nil {
		t.Fatalf("first ConfirmRaid: %v", err)
	}
	_, firstConfirmedAt := sessionState(t, pool, started.SessionID)
	if firstConfirmedAt == nil {
		t.Fatal("confirmed_at must be set after the first confirm")
	}

	err = s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, time.Minute)
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
