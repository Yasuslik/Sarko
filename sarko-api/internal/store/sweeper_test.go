package store_test

import (
	"context"
	"testing"
	"time"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

func TestSweepExpiredVoidsPendingAndReturnsLoadout(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sweep-pending", []domain.ItemStack{{ItemID: "ammo", Quantity: 4}})

	p := startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 4}})
	p.PendingTTL = -time.Second
	if _, err := s.StartRaid(ctx, p); err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 1 || died != 0 {
		t.Errorf("voided=%d died=%d, want 1 and 0", voided, died)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 4 {
		t.Errorf("loadout not returned: %v", profile.Stash)
	}
}

func TestSweepExpiredClosesActiveRaidAsDied(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sweep-active", []domain.ItemStack{{ItemID: "rifle", Quantity: 1}})

	started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "rifle", Quantity: 1}}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	// Confirm with a deadline already in the past: the player entered and vanished.
	if err := s.ConfirmRaid(ctx, started.SessionID, started.SessionToken, -time.Second); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 0 || died != 1 {
		t.Errorf("voided=%d died=%d, want 0 and 1", voided, died)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("quitting mid-raid must lose the loadout, got %v", profile.Stash)
	}
}

func TestSweepExpiredLeavesLiveRaidsAlone(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sweep-live", nil)

	if _, err := s.StartRaid(ctx, startParams(playerID, nil)); err != nil {
		t.Fatalf("StartRaid: %v", err)
	}

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 0 || died != 0 {
		t.Errorf("voided=%d died=%d, want 0 and 0 for a live raid", voided, died)
	}
}
