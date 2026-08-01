package domain_test

import (
	"encoding/json"
	"os"
	"strings"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

func TestStarterKitIsThePistolAmmoAndMedkit(t *testing.T) {
	kit := domain.StarterKit()
	want := map[string]int{"pistol": 1, "ammo_9mm": 60, "medkit": 1}

	if len(kit) != len(want) {
		t.Fatalf("starter kit has %d stacks, want %d: %v", len(kit), len(want), kit)
	}
	for _, s := range kit {
		if want[s.ItemID] != s.Quantity {
			t.Errorf("starter kit %s = %d, want %d", s.ItemID, s.Quantity, want[s.ItemID])
		}
	}
	// Everything granted must be a known item, or the very first raid the new
	// player starts is rejected by the gate below for carrying its own kit.
	if err := domain.ValidateRaidItems(kit); err != nil {
		t.Errorf("the starter kit must pass its own validation: %v", err)
	}
}

func TestValidateRaidItemsRejectsInventedIDs(t *testing.T) {
	// Until now the backend credited any item_id a caller invented: item ids are
	// free-form TEXT in stash_items and nothing checked them. A client that says
	// it found "turbine" — a helicopter part — in a starter sector was believed.
	err := domain.ValidateRaidItems([]domain.ItemStack{{ItemID: "unobtanium", Quantity: 1}})
	if err == nil {
		t.Fatal("an invented item id was accepted")
	}
	if !strings.Contains(err.Error(), "unknown") {
		t.Errorf("error should name the problem, got %q", err)
	}
}

func TestValidateRaidItemsCapsStacksAndQuantities(t *testing.T) {
	tooMany := make([]domain.ItemStack, 0, domain.MaxRaidStacks+1)
	for _, id := range []string{
		"pistol", "ammo_9mm", "medkit", "bandage", "painkillers", "scrap_metal",
		"copper_wire", "duct_tape", "canned_food", "vodka", "cigarettes",
		"toolbox", "chain", "backpack",
	} {
		tooMany = append(tooMany, domain.ItemStack{ItemID: id, Quantity: 1})
	}
	if len(tooMany) <= domain.MaxRaidStacks {
		t.Fatalf("fixture must exceed the cap: %d stacks vs cap %d", len(tooMany), domain.MaxRaidStacks)
	}
	// The backpack holds 12 carried cells plus the worn bag itself, so 14
	// distinct stacks could not have been carried out no matter how the raid went.
	if err := domain.ValidateRaidItems(tooMany); err == nil {
		t.Errorf("%d distinct stacks was accepted, cap is %d", len(tooMany), domain.MaxRaidStacks)
	}

	overCap := domain.MaxRaidUnits("scrap_metal") + 1
	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "scrap_metal", Quantity: overCap},
	}); err == nil {
		t.Errorf("quantity %d was accepted, cap is %d", overCap, domain.MaxRaidUnits("scrap_metal"))
	}

	// The generous end still passes: 13 stacks of the largest stack (ammo, 60)
	// is 780 units, and a legitimate ammo haul must not be rejected.
	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "ammo_9mm", Quantity: domain.MaxRaidUnits("ammo_9mm")},
	}); err != nil {
		t.Errorf("a full backpack of ammo must be accepted: %v", err)
	}
}

// TestValidateRaidItemsCapsPerItemByStackSize pins the fix for a cap that was
// flat: 780 units of anything used to pass, so a forged extraction could claim
// 780 bike_frames — items that do not stack, of which thirteen stacks hold
// thirteen — and complete the bicycle recipe hundreds of times from one raid.
// The cap is now the backpack's geometry: stacks × that item's stackSize.
func TestValidateRaidItemsCapsPerItemByStackSize(t *testing.T) {
	tests := []struct {
		name     string
		stack    domain.ItemStack
		accepted bool
	}{
		// bike_frame has stackSize 1, so thirteen stacks hold exactly thirteen.
		{"fourteen unstackable frames", domain.ItemStack{ItemID: "bike_frame", Quantity: 14}, false},
		{"a full backpack of frames", domain.ItemStack{ItemID: "bike_frame", Quantity: 13}, true},
		// ammo_9mm stacks 60, so the same thirteen stacks hold 780 rounds.
		{"a full backpack of ammo", domain.ItemStack{ItemID: "ammo_9mm", Quantity: 780}, true},
		{"one round past a full backpack", domain.ItemStack{ItemID: "ammo_9mm", Quantity: 781}, false},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := domain.ValidateRaidItems([]domain.ItemStack{tc.stack})
			if tc.accepted && err != nil {
				t.Errorf("%s x%d must be accepted: %v", tc.stack.ItemID, tc.stack.Quantity, err)
			}
			if !tc.accepted && err == nil {
				t.Errorf("%s x%d was accepted, cap is %d",
					tc.stack.ItemID, tc.stack.Quantity, domain.MaxRaidUnits(tc.stack.ItemID))
			}
		})
	}
}

// TestValidateRaidItemsRejectsNonPositiveQuantitiesOnItsOwn calls the gate
// alone. Both handlers happen to run ValidateStacks first, which is what caught
// negatives before — but nothing pins that order, and a negative entry is how a
// caller would hide an over-cap stack: 1_000_000 and -999_300 of the same id
// merge to 700, which is under ammo's 780 cap, while the million is what the
// arithmetic downstream would have to survive.
func TestValidateRaidItemsRejectsNonPositiveQuantitiesOnItsOwn(t *testing.T) {
	err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "ammo_9mm", Quantity: 1_000_000},
		{ItemID: "ammo_9mm", Quantity: -999_300},
	})
	if err == nil {
		t.Fatal("a negative quantity was accepted; the gate relies on ValidateStacks running first")
	}
	if !strings.Contains(err.Error(), "positive") {
		t.Errorf("error should name the problem, got %q", err)
	}
}

func TestValidateRaidItemsAcceptsSplitEntriesThatMergeUnderTheCap(t *testing.T) {
	// endpoints_test.go already relies on this: 50 separate one-unit entries of
	// the same id merge to a single stack, which is one stack, not fifty. The id
	// is scrap_metal because 50 units must also stay under its per-item cap
	// (13 stacks × a stack of 10 = 130).
	stacks := make([]domain.ItemStack, 50)
	for i := range stacks {
		stacks[i] = domain.ItemStack{ItemID: "scrap_metal", Quantity: 1}
	}
	if err := domain.ValidateRaidItems(stacks); err != nil {
		t.Errorf("50 split entries of one id must merge to one stack: %v", err)
	}
}

// TestKnownItemsMatchTheClientCatalog is the drift alarm, over ids *and* stack
// sizes: the per-item cap is slots × stackSize, so a stack size that disagrees
// with the client is a cap that disagrees with what a player can actually carry
// — too small rejects a legitimate full backpack, too large re-opens the hole
// the flat cap left. The client's items.json is the authored source; the domain
// map is a mirror, and a mirror that is never compared is a lie waiting to
// happen. The test skips rather than fails when the file is absent, because the
// deployed container image contains only sarko-api/ and this must not break a
// build that has no game directory.
func TestKnownItemsMatchTheClientCatalog(t *testing.T) {
	const path = "../../../SarkoGame/Data/Items/items.json"
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("client catalog not present at %s: %v", path, err)
	}

	var catalog struct {
		Items []struct {
			ID        string `json:"id"`
			StackSize int    `json:"stackSize"`
		} `json:"items"`
	}
	if err := json.Unmarshal(raw, &catalog); err != nil {
		t.Fatalf("client catalog is not valid JSON: %v", err)
	}
	if len(catalog.Items) == 0 {
		t.Fatal("client catalog parsed to zero items")
	}

	client := make(map[string]int, len(catalog.Items))
	for _, item := range catalog.Items {
		if item.StackSize <= 0 {
			t.Errorf("client item %q has stackSize %d; the catalog must state a positive stack size", item.ID, item.StackSize)
			continue
		}
		client[item.ID] = item.StackSize

		size, ok := domain.ItemStackSizes[item.ID]
		if !ok {
			t.Errorf("client item %q is missing from domain.ItemStackSizes — a raid carrying it would be rejected", item.ID)
			continue
		}
		if size != item.StackSize {
			t.Errorf("stackSize drift on %q: client says %d, domain says %d — the per-item cap would be %d instead of %d units",
				item.ID, item.StackSize, size, domain.MaxRaidStacks*size, domain.MaxRaidStacks*item.StackSize)
		}
	}
	// The other direction only needs the ids: the sizes of the shared ones were
	// compared above, and reporting each mismatch twice only obscures it.
	for id := range domain.ItemStackSizes {
		if _, ok := client[id]; !ok {
			t.Errorf("domain.ItemStackSizes has %q, which the client catalog does not define", id)
		}
	}
	// KnownItemIDs is derived, so this only guards the derivation itself.
	if len(domain.KnownItemIDs) != len(domain.ItemStackSizes) {
		t.Errorf("KnownItemIDs has %d ids, ItemStackSizes has %d", len(domain.KnownItemIDs), len(domain.ItemStackSizes))
	}
}
