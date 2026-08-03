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

// sortieParams is startParams with the mode set and a cooldown long enough that a
// second sortie inside one test is refused unless the test says otherwise.
func sortieParams(playerID string, cooldown time.Duration) store.StartRaidParams {
	p := startParams(playerID, nil)
	p.Mode = domain.ModeSortie
	p.SortieCooldown = cooldown
	return p
}

// isAnAuthoredKit reports whether these stacks are one of domain.SortieKits,
// exactly. Byte-equality against the table is the assertion that matters: it is
// what "the server grants the kit, never the client" means once the request has
// been through the wire.
func isAnAuthoredKit(stacks []domain.ItemStack) bool {
	for _, kit := range domain.SortieKits {
		want := domain.SortieKitStacks(kit)
		if len(want) != len(stacks) {
			continue
		}
		same := true
		for i := range want {
			if want[i] != stacks[i] {
				same = false
				break
			}
		}
		if same {
			return true
		}
	}
	return false
}

// A ВИЛАЗКА is FREE and the kit is the SERVER's. Both halves are asserted against
// the most hostile input the endpoint can be given: an empty stash (so any debit
// at all would fail) and a loadout naming gear the player does not own (so a
// client that could name its kit would be handed a bicycle).
func TestSortieIsFreeAndTheServerChoosesTheKit(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	// Broke. This is the player the mechanic exists for.
	playerID := seedPlayer(t, s, "dev-sortie-free", nil)

	p := sortieParams(playerID, time.Hour)
	// The forged ask: three bicycle frames and a pistol, from a stash that has
	// nothing in it. A raid would be refused for insufficient_items; a sortie must
	// discard this list unread.
	p.Loadout = []domain.ItemStack{
		{ItemID: "bike_frame", Quantity: 3},
		{ItemID: "pistol", Quantity: 1},
	}

	started, err := s.StartRaid(ctx, p)
	if err != nil {
		t.Fatalf("a sortie must be free, so an empty stash cannot refuse it: %v", err)
	}
	if started.Mode != domain.ModeSortie {
		t.Errorf("mode = %q, want sortie", started.Mode)
	}
	if len(started.GrantedKit) == 0 {
		t.Fatal("a sortie must come back with a granted kit")
	}
	if !isAnAuthoredKit(started.GrantedKit) {
		t.Errorf("granted kit %v is not one of the authored tables — the server did not choose it", started.GrantedKit)
	}
	for _, stack := range started.GrantedKit {
		if stack.ItemID == "bike_frame" {
			t.Error("the client asked for a bike frame and was given one: the kit is not server-chosen")
		}
	}

	// Nothing debited. The stash was empty and it still is.
	if got := stashOf(t, s, playerID); len(got) != 0 {
		t.Errorf("a sortie must debit nothing, stash = %v", got)
	}
}

// "Extraction credits everything — granted gear and haul. Death loses all of it."
// This is the whole promise of the mechanic, and it is one test because the two
// halves are the same code path with one flag different.
func TestSortieExtractionKeepsTheKitAndDeathLosesIt(t *testing.T) {
	run := func(t *testing.T, outcome domain.RaidOutcome, haul []domain.ItemStack) (granted []domain.ItemStack, stash map[string]int, result store.RaidResult) {
		t.Helper()
		s := store.New(testutil.Pool(t))
		ctx := context.Background()
		playerID := seedPlayer(t, s, "dev-sortie-"+string(outcome), nil)

		started, err := s.StartRaid(ctx, sortieParams(playerID, time.Hour))
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
			time.Hour, time.Hour); err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}
		result, err = s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      outcome,
			Items:        haul,
		})
		if err != nil {
			t.Fatalf("SubmitResult: %v", err)
		}
		return started.GrantedKit, stashOf(t, s, playerID), result
	}

	t.Run("extraction: the borrowed gear becomes yours", func(t *testing.T) {
		granted, stash, result := run(t, domain.OutcomeExtracted,
			[]domain.ItemStack{{ItemID: "scrap_metal", Quantity: 4}})

		if result.Mode != domain.ModeSortie {
			t.Errorf("result mode = %q, want sortie", result.Mode)
		}
		// The returned loadout is the granted kit, item for item. Not "a pistol" —
		// exactly what was lent, because the credit reads the session row that the
		// grant wrote.
		if len(result.ReturnedLoadout) != len(granted) {
			t.Fatalf("returned %v, granted %v", result.ReturnedLoadout, granted)
		}
		for i := range granted {
			if result.ReturnedLoadout[i] != granted[i] {
				t.Errorf("returned[%d] = %v, granted %v", i, result.ReturnedLoadout[i], granted[i])
			}
		}
		// And it is in the stash, which is the sentence that matters: walk out with a
		// pistol, own a pistol.
		for _, stack := range granted {
			if stash[stack.ItemID] < stack.Quantity {
				t.Errorf("stash holds %d %s, the sortie lent %d — extraction must keep the granted gear",
					stash[stack.ItemID], stack.ItemID, stack.Quantity)
			}
		}
		if stash["scrap_metal"] != 4 {
			t.Errorf("the haul must credit too, stash holds %d scrap", stash["scrap_metal"])
		}
	})

	t.Run("death: nothing was earned", func(t *testing.T) {
		granted, stash, result := run(t, domain.OutcomeDied, nil)

		if len(result.ReturnedLoadout) != 0 {
			t.Errorf("death must return nothing, got %v", result.ReturnedLoadout)
		}
		for _, stack := range granted {
			if stash[stack.ItemID] != 0 {
				t.Errorf("death credited %d %s — a sortie's kit is lost with the run",
					stash[stack.ItemID], stack.ItemID)
			}
		}
		if len(stash) != 0 {
			t.Errorf("a died sortie must leave the stash exactly as it was, got %v", stash)
		}
	})
}

// Dying on a free run must not cost the player something they still own.
//
// The death branch's DELETE FROM player_equipment exists so a shelter cannot show
// gear the stash no longer holds — and after a sortie the stash holds everything it
// did before, because nothing was debited. Clearing the slots would be the free run
// confiscating the player's own pistol.
func TestSortieDeathDoesNotUnequipThePlayer(t *testing.T) {
	for _, c := range []struct {
		name  string
		byAPI bool
	}{
		// Both paths that end an active raid: the client submitting a death, and the
		// sweeper closing an abandoned one. They must agree — a sortie must not cost
		// the equipment only when the client forgot to submit.
		{"the client submits a death", true},
		{"the sweeper closes it as MIA", false},
	} {
		t.Run(c.name, func(t *testing.T) {
			s := store.New(testutil.Pool(t))
			ctx := context.Background()
			playerID := seedPlayer(t, s, "dev-sortie-equip-"+c.name, []domain.ItemStack{
				{ItemID: "pistol", Quantity: 1},
			})
			if _, err := s.SetEquipment(ctx, playerID, domain.SlotWeapon, "pistol"); err != nil {
				t.Fatalf("equip: %v", err)
			}

			started, err := s.StartRaid(ctx, sortieParams(playerID, time.Hour))
			if err != nil {
				t.Fatalf("StartRaid: %v", err)
			}
			// A deadline already in the past for the sweeper case, so the session is
			// active and expired at once.
			deadline := time.Hour
			if !c.byAPI {
				deadline = -time.Second
			}
			if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
				deadline, deadline); err != nil {
				t.Fatalf("ConfirmRaid: %v", err)
			}

			if c.byAPI {
				if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
					PlayerID:     playerID,
					SessionID:    started.SessionID,
					SessionToken: started.SessionToken,
					Outcome:      domain.OutcomeDied,
				}); err != nil {
					t.Fatalf("SubmitResult: %v", err)
				}
			} else if _, _, err := s.SweepExpired(ctx); err != nil {
				t.Fatalf("SweepExpired: %v", err)
			}

			profile, err := s.Profile(ctx, playerID)
			if err != nil {
				t.Fatalf("Profile: %v", err)
			}
			if profile.Equipment["weapon"] != "pistol" {
				t.Errorf("equipment = %v, want the pistol still worn: a sortie debited nothing, so a sortie death takes nothing",
					profile.Equipment)
			}
			if stashOf(t, s, playerID)["pistol"] != 1 {
				t.Error("the player's own pistol must still be in the stash after a sortie death")
			}
		})
	}
}

// Spec §4.5: "it does not count as the tutorial". The latch decides what is inside
// every container of the next raid, so a free run with borrowed gear must not be
// able to skip the teaching layout — and an ordinary raid still must.
func TestSortieDoesNotLatchTheTutorial(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sortie-tutorial", nil)

	extract := func(p store.StartRaidParams) {
		t.Helper()
		started, err := s.StartRaid(ctx, p)
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
			time.Hour, time.Hour); err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}
		if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeExtracted,
		}); err != nil {
			t.Fatalf("SubmitResult: %v", err)
		}
	}

	extract(sortieParams(playerID, 0))
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Fatal("a sortie extraction must not latch tutorial_completed")
	}

	// The control: the same call on an ordinary raid still latches, so the exemption
	// is the mode and not a broken latch.
	extract(startParams(playerID, nil))
	profile, err = s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if !profile.TutorialCompleted {
		t.Error("a normal raid's extraction must still latch tutorial_completed")
	}
}

// The cooldown, which is the whole of "so it cannot be farmed" — and the request
// made during it is refused BY NAME.
func TestSortieCooldownRefusesASecondFreeRun(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sortie-cooldown", nil)

	first, err := s.StartRaid(ctx, sortieParams(playerID, time.Hour))
	if err != nil {
		t.Fatalf("first sortie: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, first.SessionID, first.SessionToken,
		time.Hour, time.Hour); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    first.SessionID,
		SessionToken: first.SessionToken,
		Outcome:      domain.OutcomeExtracted,
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	// The request during the cooldown. This is the farm, attempted.
	_, err = s.StartRaid(ctx, sortieParams(playerID, time.Hour))
	if !errors.Is(err, store.ErrSortieCooldown) {
		t.Fatalf("a second sortie inside the cooldown must be refused as ErrSortieCooldown, got %v", err)
	}
	// The refusal says how long, because the client shows it verbatim.
	if err != nil && !containsDigit(err.Error()) {
		t.Errorf("the refusal must name the remaining time, got %q", err)
	}

	// A NORMAL raid is unaffected: the cooldown is on the free run, not on playing.
	// Getting this wrong would lock a player out of the game for having taken a
	// sortie, which is the opposite of what the mechanic is for.
	if _, err := s.StartRaid(ctx, startParams(playerID, nil)); err != nil {
		t.Fatalf("the sortie cooldown must not block an ordinary raid: %v", err)
	}
}

// The cooldown is measured from when the last sortie ENDED, so a zero cooldown
// permits the next one immediately — which is also how a test proves the refusal
// above came from the cooldown and not from something else about a second sortie.
func TestSortieCooldownOfZeroAllowsTheNextOne(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sortie-nocooldown", nil)

	for i := 0; i < 2; i++ {
		started, err := s.StartRaid(ctx, sortieParams(playerID, 0))
		if err != nil {
			t.Fatalf("sortie %d: %v", i, err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
			time.Hour, time.Hour); err != nil {
			t.Fatalf("ConfirmRaid %d: %v", i, err)
		}
		if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      domain.OutcomeDied,
		}); err != nil {
			t.Fatalf("SubmitResult %d: %v", i, err)
		}
	}
}

// A sortie that was never entered has not "ended": it granted nothing and cost
// nothing, so it must not burn the cooldown. A network hiccup between start and
// confirm is the client's likeliest failure, and punishing it would make the
// recovery path unreliable exactly when it is needed.
//
// It cannot be farmed either — a voided session credits nothing, because the kit is
// only ever credited by an extraction, which needs a confirm.
func TestAVoidedSortieDoesNotBurnTheCooldown(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sortie-voided", nil)

	p := sortieParams(playerID, time.Hour)
	p.PendingTTL = -time.Second // born already expired: never confirmed, never entered
	if _, err := s.StartRaid(ctx, p); err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if voided, _, err := s.SweepExpired(ctx); err != nil {
		t.Fatalf("SweepExpired: %v", err)
	} else if voided != 1 {
		t.Fatalf("voided = %d, want 1", voided)
	}

	if _, err := s.StartRaid(ctx, sortieParams(playerID, time.Hour)); err != nil {
		t.Errorf("a sortie that was never entered must not burn the cooldown: %v", err)
	}
}

// SortieRemaining is what /v1/profile reports so the button can show a number. It
// must be zero before any sortie and positive right after one — a client that was
// told zero forever would offer a button the server then refuses on every press.
func TestSortieRemainingReportsTheCountdown(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()
	playerID := seedPlayer(t, s, "dev-sortie-remaining", nil)

	if remaining, err := s.SortieRemaining(ctx, playerID, time.Hour); err != nil {
		t.Fatalf("SortieRemaining: %v", err)
	} else if remaining != 0 {
		t.Errorf("a player who has never taken a sortie must be able to take one now, got %s", remaining)
	}

	started, err := s.StartRaid(ctx, sortieParams(playerID, time.Hour))
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	// Still zero while the sortie is IN FLIGHT: the one-open-raid rule is what stops
	// a second one, and reporting a cooldown here would be reporting the wrong reason.
	if remaining, err := s.SortieRemaining(ctx, playerID, time.Hour); err != nil {
		t.Fatalf("SortieRemaining: %v", err)
	} else if remaining != 0 {
		t.Errorf("an in-flight sortie is not a cooldown, got %s", remaining)
	}

	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
		time.Hour, time.Hour); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeDied,
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	remaining, err := s.SortieRemaining(ctx, playerID, time.Hour)
	if err != nil {
		t.Fatalf("SortieRemaining: %v", err)
	}
	if remaining <= 0 || remaining > time.Hour {
		t.Errorf("remaining = %s, want something inside the hour just started", remaining)
	}
}

// The shorter clock (spec §4.5: the trade-off is quality, not danger). The server's
// deadline is what the client's in-raid timer is capped by, so this is the one place
// the shorter run is decided — and the session's own stored mode decides it, not the
// caller.
func TestSortieRunsAShorterClockThanARaid(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	confirm := func(playerID string, p store.StartRaidParams) time.Time {
		t.Helper()
		started, err := s.StartRaid(ctx, p)
		if err != nil {
			t.Fatalf("StartRaid: %v", err)
		}
		expires, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken,
			time.Hour, 2*time.Minute)
		if err != nil {
			t.Fatalf("ConfirmRaid: %v", err)
		}
		return expires
	}

	raider := seedPlayer(t, s, "dev-clock-raid", nil)
	sortier := seedPlayer(t, s, "dev-clock-sortie", nil)

	raidDeadline := confirm(raider, startParams(raider, nil))
	sortieDeadline := confirm(sortier, sortieParams(sortier, time.Hour))

	if !sortieDeadline.Before(raidDeadline) {
		t.Errorf("the sortie's deadline (%s) must be earlier than the raid's (%s): a free run is worse, not longer",
			sortieDeadline, raidDeadline)
	}
	// The raid got its hour and the sortie got its two minutes, so the pick is by
	// mode and not by "the second one is always shorter".
	if left := time.Until(raidDeadline); left < 50*time.Minute {
		t.Errorf("a raid must still get the raid deadline, %s left", left)
	}
	if left := time.Until(sortieDeadline); left > 5*time.Minute {
		t.Errorf("a sortie must get the sortie deadline, %s left", left)
	}
}

func containsDigit(s string) bool {
	for _, r := range s {
		if r >= '0' && r <= '9' {
			return true
		}
	}
	return false
}
