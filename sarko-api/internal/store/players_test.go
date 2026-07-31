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

func TestNewPlayerStartsAtTierNoneWithBridge(t *testing.T) {
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
	if len(profile.UnlockedMaps) != 1 || profile.UnlockedMaps[0] != "bridge" {
		t.Errorf("UnlockedMaps = %v, want [bridge]", profile.UnlockedMaps)
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

func TestGrantStarterKitIsOneTime(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-starter")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	granted, err := s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("first GrantStarterKit: %v", err)
	}
	if !granted {
		t.Fatal("first GrantStarterKit reported nothing granted")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := make(map[string]int, len(profile.Stash))
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	for _, want := range domain.StarterKit() {
		if got[want.ItemID] != want.Quantity {
			t.Errorf("stash %s = %d, want %d", want.ItemID, got[want.ItemID], want.Quantity)
		}
	}

	// Idempotent: every app launch calls /v1/auth/anonymous, so a second grant
	// would be a free pistol per launch.
	granted, err = s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("second GrantStarterKit: %v", err)
	}
	if granted {
		t.Error("second GrantStarterKit granted the kit again")
	}

	profile, err = s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile after second grant: %v", err)
	}
	if len(profile.Stash) != len(domain.StarterKit()) {
		t.Errorf("stash grew on the second grant: %v", profile.Stash)
	}
}

func TestGrantStarterKitDoesNotComeBackAfterTheKitIsSpent(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-spender")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}
	if _, err := s.GrantStarterKit(ctx, playerID); err != nil {
		t.Fatalf("GrantStarterKit: %v", err)
	}

	// Take the raid: the loadout is debited at start, so the pistol leaves the
	// stash. ON CONFLICT DO NOTHING would silently re-grant it on the next
	// launch — which is why the flag lives on the player row, not on the item.
	started, err := s.StartRaid(ctx, store.StartRaidParams{
		PlayerID:   playerID,
		MapID:      "bridge",
		Loadout:    []domain.ItemStack{{ItemID: "pistol", Quantity: 1}},
		PendingTTL: time.Minute,
	})
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	_ = started

	granted, err := s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("GrantStarterKit after spending: %v", err)
	}
	if granted {
		t.Error("the starter kit came back after being spent")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	for _, item := range profile.Stash {
		if item.ItemID == "pistol" {
			t.Errorf("pistol is back in the stash: %v", profile.Stash)
		}
	}
}

func TestProfileUnknownPlayerReturnsErrNotFound(t *testing.T) {
	s := store.New(testutil.Pool(t))

	_, err := s.Profile(context.Background(), "00000000-0000-0000-0000-000000000000")
	if !errors.Is(err, store.ErrNotFound) {
		t.Errorf("err = %v, want ErrNotFound", err)
	}
}

func TestProfileReportsTutorialNotCompletedForANewPlayer(t *testing.T) {
	// Read through the same path the client reads: /v1/profile serialises this
	// struct verbatim, so a field that is never populated by Profile() is a field
	// the client receives as false no matter what the column says.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "device-tutorial-fresh", nil)

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Error("a new player must start with tutorial_completed = false")
	}
}
