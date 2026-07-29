package store_test

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

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
