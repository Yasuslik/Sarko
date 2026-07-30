package store_test

import (
	"context"
	"errors"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

func TestUpsertPlayerIsIdempotent(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	first, err := s.UpsertPlayer(ctx, "device-abc")
	if err != nil {
		t.Fatalf("first UpsertPlayer: %v", err)
	}
	second, err := s.UpsertPlayer(ctx, "device-abc")
	if err != nil {
		t.Fatalf("second UpsertPlayer: %v", err)
	}
	if first != second {
		t.Errorf("same device produced two players: %s and %s", first, second)
	}
}

func TestNewPlayerStartsAtTierNoneWithForest(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-new")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}

	if profile.Tier != domain.TierNone {
		t.Errorf("Tier = %s, want none", profile.Tier)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("new stash = %v, want empty", profile.Stash)
	}
	if len(profile.UnlockedMaps) != 1 || profile.UnlockedMaps[0] != "forest" {
		t.Errorf("UnlockedMaps = %v, want [forest]", profile.UnlockedMaps)
	}
	if profile.SchemaVersion != 1 {
		t.Errorf("SchemaVersion = %d, want 1", profile.SchemaVersion)
	}
}

func TestAddItemsMergesIntoExistingStacks(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-stash")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	if err := s.AddItems(ctx, playerID, []domain.ItemStack{{ItemID: "bolt", Quantity: 2}}); err != nil {
		t.Fatalf("first AddItems: %v", err)
	}
	if err := s.AddItems(ctx, playerID, []domain.ItemStack{
		{ItemID: "bolt", Quantity: 3},
		{ItemID: "chain", Quantity: 1},
	}); err != nil {
		t.Fatalf("second AddItems: %v", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := map[string]int{}
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	if got["bolt"] != 5 {
		t.Errorf("bolt = %d, want 5", got["bolt"])
	}
	if got["chain"] != 1 {
		t.Errorf("chain = %d, want 1", got["chain"])
	}
}

func TestProfileUnknownPlayerReturnsErrNotFound(t *testing.T) {
	s := store.New(testutil.Pool(t))

	_, err := s.Profile(context.Background(), "00000000-0000-0000-0000-000000000000")
	if !errors.Is(err, store.ErrNotFound) {
		t.Errorf("err = %v, want ErrNotFound", err)
	}
}
