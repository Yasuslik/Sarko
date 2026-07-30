package store_test

// The two tests here pin the service's central accounting invariant: an item
// that entered a raid is either returned to the stash or consumed, never both
// and never neither. Both failures would be silent — the suite would stay
// green and players would quietly gain or lose inventory.

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

// stashOf reads a player's stash as a map, for equality assertions.
func stashOf(t *testing.T, s *store.Store, playerID string) map[string]int {
	t.Helper()
	profile, err := s.Profile(context.Background(), playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := make(map[string]int, len(profile.Stash))
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	return got
}

// TestVoidedSessionCannotAlsoBeCredited is the "loadout returned XOR result
// credited" property.
//
// A voided session has already given its loadout back. If SubmitResult's state
// guard were ever widened to accept voided — an easy-looking change, since
// "the session exists and has a valid token" reads like enough — then every
// abandoned raid would become a free duplication: the player gets the loadout
// back from the void *and* whatever they claim as a result.
//
// Nothing else in the suite would notice. The guard's current behaviour is a
// single `state != pending && state != active` line with no test naming the
// consequence, so this is the test that makes widening it fail loudly.
func TestVoidedSessionCannotAlsoBeCredited(t *testing.T) {
	pool := testutil.Pool(t)
	s := store.New(pool)
	ctx := context.Background()

	loadout := []domain.ItemStack{{ItemID: "ammo", Quantity: 30}, {ItemID: "rifle", Quantity: 1}}
	playerID := seedPlayer(t, s, "dev-void-xor-credit", loadout)

	// Start a raid that is already past its pending deadline, so the sweeper
	// will void it: the player never entered the map.
	p := startParams(playerID, loadout)
	p.PendingTTL = -time.Second
	started, err := s.StartRaid(ctx, p)
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if before := stashOf(t, s, playerID); len(before) != 0 {
		t.Fatalf("the loadout must be debited at start, stash = %v", before)
	}

	voided, died, err := s.SweepExpired(ctx)
	if err != nil {
		t.Fatalf("SweepExpired: %v", err)
	}
	if voided != 1 || died != 0 {
		t.Fatalf("voided=%d died=%d, want 1 and 0", voided, died)
	}

	// The loadout is back — that is the "returned" half of the invariant.
	afterVoid := stashOf(t, s, playerID)
	if afterVoid["ammo"] != 30 || afterVoid["rifle"] != 1 {
		t.Fatalf("loadout was not returned by the void: %v", afterVoid)
	}

	// Now the client, which never learnt about the void, submits its result
	// with the correct session id and the correct token.
	_, err = s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items: []domain.ItemStack{
			{ItemID: "ammo", Quantity: 30},
			{ItemID: "rifle", Quantity: 1},
			{ItemID: "turbine", Quantity: 2},
		},
	})
	if !errors.Is(err, store.ErrSessionNotOpen) {
		t.Fatalf("err = %v, want ErrSessionNotOpen — a voided session must not accept a result", err)
	}

	// The "XOR" half: the stash is exactly what the void left, not one item more.
	afterSubmit := stashOf(t, s, playerID)
	if len(afterSubmit) != len(afterVoid) {
		t.Fatalf("stash changed after a refused result: %v -> %v", afterVoid, afterSubmit)
	}
	for id, qty := range afterVoid {
		if afterSubmit[id] != qty {
			t.Errorf("%s = %d after the refused result, want %d (unchanged)", id, afterSubmit[id], qty)
		}
	}
	if _, ok := afterSubmit["turbine"]; ok {
		t.Error("the refused result credited loot that was never earned")
	}
}

// TestSweepAndStartCreditTheLoadoutExactlyOnce is the only property in this
// service whose failure would be both silent and unrecoverable, and it had no
// test at all.
//
// An expired pending session's loadout is returned by two different code
// paths: the background sweeper (SweepExpired) and the next StartRaid, which
// sweeps this player first so an abandoned raid never blocks them forever.
// When both run at once they contend for the same session row, and the
// invariant rests on PostgreSQL re-qualifying the WHERE clause after a blocked
// FOR UPDATE acquires the lock — the second transaction re-reads the row,
// finds state is now 'voided', and skips it.
//
// Credit it twice and the player duplicates their loadout out of thin air;
// credit it zero times and it is destroyed. Neither shows up in any other
// test, and neither is detectable after the fact.
//
// The loop runs the race repeatedly, each iteration on a fresh player, because
// a single interleaving proves very little.
func TestSweepAndStartCreditTheLoadoutExactlyOnce(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	const rounds = 25
	for round := range rounds {
		loadout := []domain.ItemStack{{ItemID: "ammo", Quantity: 10}}
		playerID := seedPlayer(t, s, fmt.Sprintf("dev-race-%d", round), loadout)

		// An abandoned, already-expired pending raid: the loadout is debited
		// and owed back exactly once.
		p := startParams(playerID, loadout)
		p.PendingTTL = -time.Second
		if _, err := s.StartRaid(ctx, p); err != nil {
			t.Fatalf("round %d: seeding StartRaid: %v", round, err)
		}
		if before := stashOf(t, s, playerID); len(before) != 0 {
			t.Fatalf("round %d: stash must be empty before the race, got %v", round, before)
		}

		var (
			wg       sync.WaitGroup
			sweepErr error
			startErr error
		)
		gate := make(chan struct{})

		wg.Add(2)
		go func() {
			defer wg.Done()
			<-gate
			_, _, sweepErr = s.SweepExpired(ctx)
		}()
		go func() {
			defer wg.Done()
			<-gate
			// A new raid with an empty loadout: it must sweep the abandoned
			// session out of the way before it can open.
			_, startErr = s.StartRaid(ctx, startParams(playerID, nil))
		}()
		close(gate)
		wg.Wait()

		if sweepErr != nil {
			t.Fatalf("round %d: SweepExpired: %v", round, sweepErr)
		}
		if startErr != nil {
			t.Fatalf("round %d: concurrent StartRaid: %v", round, startErr)
		}

		got := stashOf(t, s, playerID)
		if got["ammo"] != 10 {
			t.Fatalf("round %d: ammo = %d, want exactly 10 (0 destroys the loadout, 20 duplicates it); stash = %v",
				round, got["ammo"], got)
		}
		if len(got) != 1 {
			t.Fatalf("round %d: stash = %v, want only the returned loadout", round, got)
		}
	}
}
