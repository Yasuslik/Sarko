package store_test

import (
	"context"
	"fmt"
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
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, -time.Second, 0); err != nil {
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

// TestSweepExpiredIsBatched pins the bound rather than the end state, because
// the end state is identical either way — which is exactly why an unbounded
// sweep survived. Closing a pending session issues one statement per loadout
// stack plus the state update, so a backlog of a thousand rows was thousands of
// sequential statements in ONE transaction holding a thousand row locks, while
// every affected player's /v1/raid/start queued behind it (sweepPlayerTx takes
// a plain FOR UPDATE, no SKIP LOCKED, holding their garage_progress lock).
//
// So: one pass must close at most what it was asked for, and the loop must
// still finish the backlog.
func TestSweepExpiredIsBatched(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	const backlog = 5
	players := make([]string, backlog)
	for i := range players {
		players[i] = seedPlayer(t, s, fmt.Sprintf("dev-backlog-%d", i),
			[]domain.ItemStack{{ItemID: "ammo", Quantity: 7}})
		p := startParams(players[i], []domain.ItemStack{{ItemID: "ammo", Quantity: 7}})
		p.PendingTTL = -time.Second
		if _, err := s.StartRaid(ctx, p); err != nil {
			t.Fatalf("StartRaid %d: %v", i, err)
		}
	}

	const batch = 2
	voided, died, err := s.SweepExpiredBatch(ctx, batch)
	if err != nil {
		t.Fatalf("SweepExpiredBatch: %v", err)
	}
	if voided+died != batch {
		t.Fatalf("one pass closed %d sessions (voided=%d died=%d), want at most %d — the sweep is unbounded",
			voided+died, voided, died, batch)
	}

	// The rest still get swept: a bounded pass must not mean a stranded backlog.
	voided, died, err = s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != backlog-batch || died != 0 {
		t.Errorf("voided=%d died=%d, want %d and 0", voided, died, backlog-batch)
	}

	// And the loop is done: a further sweep finds nothing.
	voided, died, err = s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("third SweepExpired: %v", err)
	}
	if voided != 0 || died != 0 {
		t.Errorf("voided=%d died=%d after the backlog was cleared, want 0 and 0", voided, died)
	}

	for i, playerID := range players {
		profile, err := s.Profile(ctx, playerID)
		if err != nil {
			t.Fatalf("Profile %d: %v", i, err)
		}
		if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 7 {
			t.Errorf("player %d loadout not returned: %v", i, profile.Stash)
		}
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
