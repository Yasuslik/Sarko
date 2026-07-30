package store_test

import (
	"context"
	"errors"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

func TestCraftBicycleConsumesPartsAndUnlocksMap(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	parts, ok := domain.Recipe(domain.TierBicycle)
	if !ok {
		t.Fatal("bicycle recipe missing")
	}
	playerID := seedPlayer(t, s, "dev-craft", parts)

	tier, err := s.CraftNextVehicle(ctx, playerID)
	if err != nil {
		t.Fatalf("CraftNextVehicle: %v", err)
	}
	if tier != domain.TierBicycle {
		t.Errorf("tier = %s, want bicycle", tier)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("parts must be consumed, stash = %v", profile.Stash)
	}
	if len(profile.UnlockedMaps) != 2 {
		t.Errorf("UnlockedMaps = %v, want two maps after the bicycle", profile.UnlockedMaps)
	}
}

func TestCraftWithoutPartsFailsAndChangesNothing(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-poor", []domain.ItemStack{{ItemID: "chain", Quantity: 1}})

	if _, err := s.CraftNextVehicle(ctx, playerID); !errors.Is(err, store.ErrInsufficientItems) {
		t.Fatalf("err = %v, want ErrInsufficientItems", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.Tier != domain.TierNone {
		t.Errorf("tier = %s, want none", profile.Tier)
	}
	if len(profile.Stash) != 1 {
		t.Errorf("stash must be untouched, got %v", profile.Stash)
	}
}
