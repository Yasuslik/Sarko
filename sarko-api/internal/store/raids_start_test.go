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

func seedPlayer(t *testing.T, s *store.Store, device string, stash []domain.ItemStack) string {
	t.Helper()
	ctx := context.Background()
	playerID, err := s.UpsertPlayer(ctx, device)
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}
	if len(stash) > 0 {
		if err := s.AddItems(ctx, playerID, stash); err != nil {
			t.Fatalf("AddItems: %v", err)
		}
	}
	return playerID
}

func startParams(playerID string, loadout []domain.ItemStack) store.StartRaidParams {
	return store.StartRaidParams{
		PlayerID:   playerID,
		MapID:      "bridge",
		Loadout:    loadout,
		PendingTTL: time.Minute,
	}
}

func TestStartRaidDebitsLoadout(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-start", []domain.ItemStack{
		{ItemID: "rifle", Quantity: 1},
		{ItemID: "ammo", Quantity: 60},
	})

	started, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{
		{ItemID: "rifle", Quantity: 1},
		{ItemID: "ammo", Quantity: 30},
	}))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if started.SessionToken == "" || started.SessionID == "" {
		t.Fatal("StartRaid must return a session id and token")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if _, still := got["rifle"]; still {
		t.Error("rifle must leave the stash when the raid starts")
	}
	if got["ammo"] != 30 {
		t.Errorf("ammo left = %d, want 30", got["ammo"])
	}
}

func TestStartRaidRejectsItemsPlayerDoesNotHave(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-greedy", []domain.ItemStack{{ItemID: "ammo", Quantity: 5}})

	_, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 6}}))
	if !errors.Is(err, store.ErrInsufficientItems) {
		t.Fatalf("err = %v, want ErrInsufficientItems", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 5 {
		t.Errorf("stash must be untouched after a rejected start, got %v", profile.Stash)
	}
}

func TestStartRaidRejectsSecondConcurrentRaid(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-double", nil)

	if _, err := s.StartRaid(ctx, startParams(playerID, nil)); err != nil {
		t.Fatalf("first StartRaid: %v", err)
	}
	_, err := s.StartRaid(ctx, startParams(playerID, nil))
	if !errors.Is(err, store.ErrRaidInProgress) {
		t.Fatalf("err = %v, want ErrRaidInProgress", err)
	}
}

func TestStartRaidRejectsLockedMap(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-locked", nil)

	p := startParams(playerID, nil)
	p.MapID = "airbase" // needs a helicopter
	if _, err := s.StartRaid(ctx, p); !errors.Is(err, store.ErrMapLocked) {
		t.Fatalf("err = %v, want ErrMapLocked", err)
	}
}

// TestStartRaidRejectsNegativeQuantity proves that a negative Quantity is
// rejected before any mutation. Against the unfixed code, debitItemsTx's
// `quantity > $3` guard is trivially true for a negative $3, and
// `quantity - $3` *adds* to the stash — so this test's stash assertion is
// the load-bearing part: it catches a regression that lets the value
// through even if some other error happened to still be returned.
func TestStartRaidRejectsNegativeQuantity(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-negative", []domain.ItemStack{{ItemID: "ammo", Quantity: 5}})

	_, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: -1}}))
	if err == nil {
		t.Error("StartRaid must reject a negative quantity")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 5 {
		t.Errorf("stash must be unchanged after a rejected start, got %v (want ammo x5)", profile.Stash)
	}
}

// TestStartRaidRejectsZeroQuantity proves a zero Quantity is rejected too.
// Unfixed, the UPDATE succeeds as a silent no-op and a spurious
// zero-quantity stack gets persisted into the session's loadout JSON.
func TestStartRaidRejectsZeroQuantity(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-zero", []domain.ItemStack{{ItemID: "ammo", Quantity: 5}})

	_, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{{ItemID: "ammo", Quantity: 0}}))
	if err == nil {
		t.Error("StartRaid must reject a zero quantity")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].Quantity != 5 {
		t.Errorf("stash must be unchanged after a rejected start, got %v (want ammo x5)", profile.Stash)
	}
}

// TestStartRaidRollsBackFirstItemOnSecondItemShortfall exercises
// debitItemsTx's central invariant: it loops per stack inside one open
// transaction, so if the loadout's second item is short, the first item's
// debit has already executed and correctness depends entirely on the
// deferred rollback in StartRaid undoing it.
func TestStartRaidRollsBackFirstItemOnSecondItemShortfall(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-partial", []domain.ItemStack{
		{ItemID: "rifle", Quantity: 1},
		{ItemID: "ammo", Quantity: 5},
	})

	_, err := s.StartRaid(ctx, startParams(playerID, []domain.ItemStack{
		{ItemID: "rifle", Quantity: 1}, // sufficient — debited first
		{ItemID: "ammo", Quantity: 6},  // short — should fail the whole raid
	}))
	if !errors.Is(err, store.ErrInsufficientItems) {
		t.Fatalf("err = %v, want ErrInsufficientItems", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["rifle"] != 1 {
		t.Errorf("rifle left = %d, want 1 (debit must roll back)", got["rifle"])
	}
	if got["ammo"] != 5 {
		t.Errorf("ammo left = %d, want 5 (unchanged)", got["ammo"])
	}
}
