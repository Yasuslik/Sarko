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
		"toolbox", "chain",
	} {
		tooMany = append(tooMany, domain.ItemStack{ItemID: id, Quantity: 1})
	}
	if len(tooMany) <= domain.MaxRaidStacks {
		t.Fatalf("fixture must exceed the cap: %d stacks vs cap %d", len(tooMany), domain.MaxRaidStacks)
	}
	// The backpack holds 12 slots (spec §4.4), so 13 distinct stacks could not
	// have been carried out no matter how the raid went.
	if err := domain.ValidateRaidItems(tooMany); err == nil {
		t.Errorf("%d distinct stacks was accepted, cap is %d", len(tooMany), domain.MaxRaidStacks)
	}

	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "scrap_metal", Quantity: domain.MaxRaidUnitsPerItem + 1},
	}); err == nil {
		t.Errorf("quantity %d was accepted, cap is %d", domain.MaxRaidUnitsPerItem+1, domain.MaxRaidUnitsPerItem)
	}

	// The generous end still passes: 12 slots of the largest stack (ammo, 60)
	// is 720 units, and a legitimate ammo haul must not be rejected.
	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "ammo_9mm", Quantity: domain.MaxRaidUnitsPerItem},
	}); err != nil {
		t.Errorf("a full backpack of ammo must be accepted: %v", err)
	}
}

func TestValidateRaidItemsAcceptsSplitEntriesThatMergeUnderTheCap(t *testing.T) {
	// endpoints_test.go already relies on this: 50 separate one-unit entries of
	// the same id merge to a single stack, which is one stack, not fifty.
	stacks := make([]domain.ItemStack, 50)
	for i := range stacks {
		stacks[i] = domain.ItemStack{ItemID: "chain", Quantity: 1}
	}
	if err := domain.ValidateRaidItems(stacks); err != nil {
		t.Errorf("50 split entries of one id must merge to one stack: %v", err)
	}
}

// TestKnownItemsMatchTheClientCatalog is the drift alarm. The client's
// items.json is the authored source; this list is a mirror, and a mirror that
// is never compared is a lie waiting to happen. The test skips rather than
// fails when the file is absent, because the deployed container image contains
// only sarko-api/ and this must not break a build that has no game directory.
func TestKnownItemsMatchTheClientCatalog(t *testing.T) {
	const path = "../../../SarkoGame/Data/Items/items.json"
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("client catalog not present at %s: %v", path, err)
	}

	var catalog struct {
		Items []struct {
			ID string `json:"id"`
		} `json:"items"`
	}
	if err := json.Unmarshal(raw, &catalog); err != nil {
		t.Fatalf("client catalog is not valid JSON: %v", err)
	}
	if len(catalog.Items) == 0 {
		t.Fatal("client catalog parsed to zero items")
	}

	client := make(map[string]struct{}, len(catalog.Items))
	for _, item := range catalog.Items {
		client[item.ID] = struct{}{}
		if _, ok := domain.KnownItemIDs[item.ID]; !ok {
			t.Errorf("client item %q is missing from domain.KnownItemIDs — a raid carrying it would be rejected", item.ID)
		}
	}
	for id := range domain.KnownItemIDs {
		if _, ok := client[id]; !ok {
			t.Errorf("domain.KnownItemIDs has %q, which the client catalog does not define", id)
		}
	}
}
