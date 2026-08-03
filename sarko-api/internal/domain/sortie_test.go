package domain_test

import (
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// The kit is credited to a stash on extraction, so an authored typo in the table
// is a way to mint an item that does not exist — or a hundred of one that does.
// This is the gate that makes the table safe to edit.
func TestSortieKitsAreLegalLoadouts(t *testing.T) {
	if len(domain.SortieKits) == 0 {
		t.Fatal("there must be at least one sortie kit, or ВИЛАЗКА grants nothing")
	}
	seen := map[string]bool{}
	for _, kit := range domain.SortieKits {
		if kit.Name == "" {
			t.Error("every kit needs a name: it is what a log line says when a player asks what they were lent")
		}
		if seen[kit.Name] {
			t.Errorf("two kits are both called %q", kit.Name)
		}
		seen[kit.Name] = true
		if kit.Weight <= 0 {
			t.Errorf("kit %q has weight %d, so it can never be rolled", kit.Name, kit.Weight)
		}
		if err := domain.ValidateSortieKit(kit); err != nil {
			t.Errorf("kit %q is not a loadout a raid could have produced: %v", kit.Name, err)
		}
		// Spec §4.5: the sortie is the LADDER out of a hole — "broke → sortie → walk
		// out with a pistol → you own a pistol". A kit with no gun is the floor the
		// game already ships (entering unarmed is always allowed), not a rung.
		equipment := map[string]string{}
		for _, stack := range domain.SortieKitStacks(kit) {
			if def, ok := domain.ItemDefs[stack.ItemID]; ok && def.Slot != domain.SlotNone {
				equipment[string(def.Slot)] = stack.ItemID
			}
		}
		if !domain.HasWeapon(equipment) {
			t.Errorf("kit %q carries no weapon — a sortie you can be handed nothing on is not a way out of a hole", kit.Name)
		}
	}
}

// The kit is chosen from a roll the server draws, and this is the property that
// makes "several authored tables" true rather than decorative: every row of the
// table is reachable, and the shares are the weights.
func TestPickSortieKitWalksTheWholeTable(t *testing.T) {
	total := domain.SumSortieWeights()
	if total <= 0 {
		t.Fatal("the table's total weight must be positive")
	}

	counts := map[string]int{}
	for roll := 0; roll < total; roll++ {
		counts[domain.PickSortieKit(roll).Name]++
	}
	for _, kit := range domain.SortieKits {
		if counts[kit.Name] != kit.Weight {
			t.Errorf("kit %q claims %d of %d rolls, got %d", kit.Name, kit.Weight, total, counts[kit.Name])
		}
	}

	// Out of range clamps rather than panicking: this sits on the raid-start path,
	// and the worst outcome of a bad roll must be a boring kit.
	if domain.PickSortieKit(-1).Name != domain.SortieKits[0].Name {
		t.Error("a negative roll must clamp to the first kit")
	}
	if domain.PickSortieKit(total).Name != domain.SortieKits[0].Name {
		t.Error("a roll past the total must clamp to the first kit")
	}
}

// The mediocrity is a design rule, not a taste: the trade-off a sortie makes is
// quality (spec §4.5). A kit that out-earns a raid's own military crate would make
// the free run the main loop.
func TestSortieKitsStayMediocre(t *testing.T) {
	for _, kit := range domain.SortieKits {
		for _, stack := range domain.SortieKitStacks(kit) {
			if stack.ItemID == "ammo_9mm" && stack.Quantity > 20 {
				t.Errorf("kit %q hands out %d rounds — a military crate gives 10..20, so this is not a mediocre kit",
					kit.Name, stack.Quantity)
			}
			// No vehicle parts, ever. The garage ladder is what raids are for, and a
			// free run that could roll a bike frame would make the only stated goal
			// reachable without ever risking anything.
			if def, ok := domain.ItemDefs[stack.ItemID]; ok && def.Width*def.Height >= 6 {
				t.Errorf("kit %q carries %q, which is a %dx%d — too big to be borrowed gear",
					kit.Name, stack.ItemID, def.Width, def.Height)
			}
			if stack.ItemID == "bike_frame" || stack.ItemID == "wheel_small" || stack.ItemID == "chain" {
				t.Errorf("kit %q carries the vehicle part %q — the garage is what raids are for", kit.Name, stack.ItemID)
			}
		}
	}
}

func TestRaidModeGuardsClientInput(t *testing.T) {
	for _, c := range []struct {
		in    string
		valid bool
		want  domain.RaidMode
	}{
		// Absent means raid: every client built before the field existed omits it,
		// and defaulting to the mode that COSTS the player something is the only
		// safe direction.
		{"", true, domain.ModeRaid},
		{"raid", true, domain.ModeRaid},
		{"sortie", true, domain.ModeSortie},
		{"SORTIE", false, domain.ModeRaid},
		{"free", false, domain.ModeRaid},
		{"raid ", false, domain.ModeRaid},
	} {
		if got := domain.IsValidRaidMode(c.in); got != c.valid {
			t.Errorf("IsValidRaidMode(%q) = %v, want %v", c.in, got, c.valid)
		}
		if got := domain.NormaliseRaidMode(c.in); got != c.want {
			t.Errorf("NormaliseRaidMode(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}
