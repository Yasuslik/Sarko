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

func TestSetEquipmentRequiresOwnership(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	// A stash with a bag in it and NO pistol: the case the spec names — "a client
	// can equip what it does not own" — and the reason equipment is server state.
	playerID := seedPlayer(t, s, "dev-equip-own", []domain.ItemStack{
		{ItemID: "backpack", Quantity: 1},
	})

	if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); !errors.Is(err, store.ErrInsufficientItems) {
		t.Fatalf("equipping an unowned pistol: err = %v, want ErrInsufficientItems", err)
	}

	equipment, err := s.SetEquipment(ctx, playerID, domain.SlotBackpack, "backpack")
	if err != nil {
		t.Fatalf("equipping an owned backpack: %v", err)
	}
	if equipment["backpack"] != "backpack" {
		t.Fatalf("equipment = %v, want the backpack worn", equipment)
	}
	// Equipping does not debit: the item stays in the stash until a raid starts,
	// so a player who equips and quits has lost nothing.
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].ItemID != "backpack" || profile.Stash[0].Quantity != 1 {
		t.Errorf("equipping must not debit the stash, got %v", profile.Stash)
	}
	if profile.Equipment["backpack"] != "backpack" {
		t.Errorf("the profile must carry the equipment, got %v", profile.Equipment)
	}
}

func TestSetEquipmentRefusesTheWrongSlot(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-equip-slot", []domain.ItemStack{
		{ItemID: "backpack", Quantity: 1},
		{ItemID: "medkit", Quantity: 1},
	})

	// Both are refused for the rule, not for ownership — the player holds both.
	for _, tc := range []struct {
		slot   domain.EquipSlot
		itemID string
	}{
		{domain.SlotClothing, "backpack"},
		{domain.SlotWeapon, "medkit"},
		{domain.SlotBackpack, "medkit"},
	} {
		if _, err := s.SetEquipment(ctx, playerID, tc.slot, tc.itemID); !errors.Is(err, store.ErrNotEquippable) {
			t.Errorf("%s in %s: err = %v, want ErrNotEquippable", tc.itemID, tc.slot, err)
		}
	}
}

func TestSetEquipmentClearsASlotAndNeverTwoSlotsAtOnce(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-equip-clear", []domain.ItemStack{
		{ItemID: "pistol", Quantity: 1},
		{ItemID: "backpack", Quantity: 1},
	})

	if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
		t.Fatalf("equip pistol: %v", err)
	}
	if _, err := s.SetEquipment(ctx, playerID, domain.SlotBackpack, "backpack"); err != nil {
		t.Fatalf("equip backpack: %v", err)
	}

	// An empty item id is the unequip. Only the named slot moves.
	equipment, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "")
	if err != nil {
		t.Fatalf("unequip weapon: %v", err)
	}
	if _, worn := equipment["weapon"]; worn {
		t.Errorf("the weapon slot must be empty, got %v", equipment)
	}
	if equipment["backpack"] != "backpack" {
		t.Errorf("unequipping one slot must not touch another, got %v", equipment)
	}

	// One item cannot be worn in two slots: the primary key bounds slots per
	// player, not items per player, so this is the store's rule and not the
	// schema's. Two slots holding one pistol would debit two at raid start.
	if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
		t.Fatalf("re-equip pistol: %v", err)
	}
	equipment, err = s.SetEquipment(ctx, playerID, domain.SlotBackpack, "backpack")
	if err != nil {
		t.Fatalf("re-equip backpack: %v", err)
	}
	if len(domain.EquipmentLoadout(equipment)) != 2 {
		t.Errorf("loadout = %v, want two stacks", domain.EquipmentLoadout(equipment))
	}
}

// TestExtractionReturnsTheLoadoutAndDeathLosesIt is the whole of spec §4's
// consequence, end to end through the store: equip -> start (debited) ->
// die/extract.
func TestExtractionReturnsTheLoadoutAndDeathLosesIt(t *testing.T) {
	held := func(t *testing.T, s *store.Store, playerID, itemID string) int {
		t.Helper()
		profile, err := s.Profile(context.Background(), playerID)
		if err != nil {
			t.Fatalf("Profile: %v", err)
		}
		for _, stack := range profile.Stash {
			if stack.ItemID == itemID {
				return stack.Quantity
			}
		}
		return 0
	}

	t.Run("extraction gives it back", func(t *testing.T) {
		s := store.New(testutil.Pool(t))
		ctx := context.Background()
		playerID := seedPlayer(t, s, "dev-loadout-out", []domain.ItemStack{
			{ItemID: "pistol", Quantity: 1},
			{ItemID: "backpack", Quantity: 1},
		})
		if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
			t.Fatalf("equip: %v", err)
		}
		loadout := domain.EquipmentLoadout(map[string]string{"weapon": "pistol"})

		started, err := s.StartRaid(ctx, startParams(playerID, loadout))
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		if got := held(t, s, playerID, "pistol"); got != 0 {
			t.Fatalf("the pistol must be debited at start, stash holds %d", got)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}

		result, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeExtracted,
			Items:        []domain.ItemStack{{ItemID: "scrap_metal", Quantity: 3}},
		})
		if err != nil {
			t.Fatalf("SubmitResult: %v", err)
		}
		if len(result.ReturnedLoadout) != 1 || result.ReturnedLoadout[0].ItemID != "pistol" {
			t.Errorf("returned loadout = %v, want the pistol", result.ReturnedLoadout)
		}
		if got := held(t, s, playerID, "pistol"); got != 1 {
			t.Errorf("extraction must return the pistol, stash holds %d", got)
		}
		if got := held(t, s, playerID, "scrap_metal"); got != 3 {
			t.Errorf("the haul must still be credited, stash holds %d scrap", got)
		}
		// The equipment survives, because what it names is back in the stash.
		profile, err := s.Profile(ctx, playerID)
		if err != nil {
			t.Fatalf("Profile: %v", err)
		}
		if profile.Equipment["weapon"] != "pistol" {
			t.Errorf("extraction must keep the equipment, got %v", profile.Equipment)
		}

		// A replay credits nothing a second time — the loadout came back once.
		replay, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeExtracted,
			Items:        []domain.ItemStack{{ItemID: "scrap_metal", Quantity: 3}},
		})
		if err != nil {
			t.Fatalf("replay: %v", err)
		}
		if !replay.AlreadyClosed || len(replay.ReturnedLoadout) != 0 {
			t.Errorf("a replay must return nothing, got %+v", replay)
		}
		if got := held(t, s, playerID, "pistol"); got != 1 {
			t.Errorf("a replay must not credit a second pistol, stash holds %d", got)
		}
	})

	t.Run("death loses it and clears the slots", func(t *testing.T) {
		s := store.New(testutil.Pool(t))
		ctx := context.Background()
		playerID := seedPlayer(t, s, "dev-loadout-dead", []domain.ItemStack{
			{ItemID: "pistol", Quantity: 1},
		})
		if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
			t.Fatalf("equip: %v", err)
		}
		loadout := domain.EquipmentLoadout(map[string]string{"weapon": "pistol"})

		started, err := s.StartRaid(ctx, startParams(playerID, loadout))
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Minute, 0); err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}
		result, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeDied,
			Items:        []domain.ItemStack{},
		})
		if err != nil {
			t.Fatalf("SubmitResult: %v", err)
		}
		if len(result.ReturnedLoadout) != 0 {
			t.Errorf("death must return nothing, got %v", result.ReturnedLoadout)
		}
		if got := held(t, s, playerID, "pistol"); got != 0 {
			t.Errorf("death must lose the pistol, stash holds %d", got)
		}
		// And the slots are cleared, or the shelter would show a pistol the stash
		// does not contain and the next raid would be refused with the reason
		// hidden two screens away.
		profile, err := s.Profile(ctx, playerID)
		if err != nil {
			t.Fatalf("Profile: %v", err)
		}
		if len(profile.Equipment) != 0 {
			t.Errorf("death must clear the equipment, got %v", profile.Equipment)
		}
	})
}

// TestAVoidedRaidRefundsTheLoadoutAndKeepsTheEquipment covers the path spec §6
// says "now matters for real": a raid that was started and never entered. The
// loadout comes back, so what the slots name is owned again and the slots stay.
func TestAVoidedRaidRefundsTheLoadoutAndKeepsTheEquipment(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-loadout-void", []domain.ItemStack{
		{ItemID: "pistol", Quantity: 1},
	})
	if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
		t.Fatalf("equip: %v", err)
	}

	// PendingTTL of zero: the session is born already past its deadline, which is
	// what the sweeper's "never entered the map" branch is for.
	started, err := s.StartRaid(ctx, store.StartRaidParams{
		PlayerID: playerID, MapID: "bridge",
		Loadout:    domain.EquipmentLoadout(map[string]string{"weapon": "pistol"}),
		PendingTTL: 0,
	})
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	_ = started

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 1 || died != 0 {
		t.Fatalf("sweep voided %d / died %d, want 1 / 0", voided, died)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].ItemID != "pistol" {
		t.Errorf("a voided raid must refund the loadout, stash = %v", profile.Stash)
	}
	if profile.Equipment["weapon"] != "pistol" {
		t.Errorf("a voided raid must keep the equipment, got %v", profile.Equipment)
	}
}

// TestAnAbandonedActiveRaidLosesTheEquipment is the MIA case: an active session
// past its deadline is a death (§11), so the equipment goes the same way it does
// on a submitted death. Without this, walking away from a raid would keep the
// slots pointing at gear the stash no longer holds.
func TestAnAbandonedActiveRaidLosesTheEquipment(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-loadout-mia", []domain.ItemStack{
		{ItemID: "pistol", Quantity: 1},
	})
	if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
		t.Fatalf("equip: %v", err)
	}

	started, err := s.StartRaid(ctx, startParams(playerID,
		domain.EquipmentLoadout(map[string]string{"weapon": "pistol"})))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	// Confirmed with a zero deadline: active, and already late.
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, 0, 0); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 0 || died != 1 {
		t.Fatalf("sweep voided %d / died %d, want 0 / 1", voided, died)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if len(profile.Stash) != 0 {
		t.Errorf("an abandoned active raid keeps nothing, stash = %v", profile.Stash)
	}
	if len(profile.Equipment) != 0 {
		t.Errorf("an abandoned active raid must clear the equipment, got %v", profile.Equipment)
	}
}
