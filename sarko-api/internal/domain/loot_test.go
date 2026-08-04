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

	// The generous end still passes: twelve 1×1 cells of ammo at 60 a stack is
	// 720 rounds, and a legitimate ammo haul must not be rejected.
	//
	// 720 and not MaxRaidUnits' 780: the thirteenth stack MaxRaidStacks counts
	// is the worn bag itself, which occupies no cell, so twelve cells hold
	// twelve stacks of ammo and the thirteenth would have nowhere to go. The
	// unit cap is the outer wall and is deliberately looser than the grid.
	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "ammo_9mm", Quantity: 720},
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
		// bike_frame has stackSize 1, so thirteen stacks would hold thirteen —
		// which is what this cap says, and why it is not the last word. The bag
		// holds one 3×2 frame (see the geometry tests below).
		{"fourteen unstackable frames", domain.ItemStack{ItemID: "bike_frame", Quantity: 14}, false},
		{"one frame", domain.ItemStack{ItemID: "bike_frame", Quantity: 1}, true},
		// ammo_9mm stacks 60 and is 1×1, so the twelve cells hold 720 rounds.
		{"a full backpack of ammo", domain.ItemStack{ItemID: "ammo_9mm", Quantity: 720}, true},
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

// TestValidateRaidItemsRefusesAHaulThatCannotBePacked is the fix for the hole
// the grid feature opened: items got rectangles and the server was never told.
// The per-item cap counts stacks and knows nothing about shape, so it accepted
// thirteen 3×2 bicycle frames into a bag whose only 3-wide page holds exactly
// one — thirteen bicycles' worth of tier-1 parts from a raid that can carry one
// frame, and the recipe in garage.go would have consumed them happily.
//
// The bag is a 2×2 pocket page plus a 4×2 backpack page: 12 cells, and *shape*
// decides what enters them, not area.
func TestValidateRaidItemsRefusesAHaulThatCannotBePacked(t *testing.T) {
	tests := []struct {
		name     string
		items    []domain.ItemStack
		accepted bool
	}{
		// The finding, exactly: 13 passes every count-based check (one merged
		// stack, and 13 is its own per-item cap) and is 13× what fits.
		{"thirteen bicycle frames", []domain.ItemStack{{ItemID: "bike_frame", Quantity: 13}}, false},
		// Two is already impossible: 3 wide cannot enter the 2-wide pocket page,
		// and the 4-wide page fits one 3×2 with a 1-wide strip left over.
		{"two bicycle frames", []domain.ItemStack{{ItemID: "bike_frame", Quantity: 2}}, false},
		{"one bicycle frame", []domain.ItemStack{{ItemID: "bike_frame", Quantity: 1}}, true},
		// A whole bicycle recipe is a legitimate raid and must still pass:
		// 3×2 frame + one 2×2 wheel stack + a 1×1 chain = 11 of the 12 cells.
		{"one bicycle's worth of parts", []domain.ItemStack{
			{ItemID: "bike_frame", Quantity: 1},
			{ItemID: "wheel_small", Quantity: 2},
			{ItemID: "chain", Quantity: 1},
		}, true},
		{"two bicycles' worth of parts", []domain.ItemStack{
			{ItemID: "bike_frame", Quantity: 2},
			{ItemID: "wheel_small", Quantity: 4},
			{ItemID: "chain", Quantity: 2},
		}, false},
		// wheel_small is 2×2 and stacks 2, so three rectangles fit (pocket page
		// + two on the backpack page) and hold six wheels. The old cap said 26.
		{"six small wheels", []domain.ItemStack{{ItemID: "wheel_small", Quantity: 6}}, true},
		{"eight small wheels", []domain.ItemStack{{ItemID: "wheel_small", Quantity: 8}}, false},
		{"twenty-six small wheels", []domain.ItemStack{{ItemID: "wheel_small", Quantity: 26}}, false},
		// pistol is 2×1: two lie in the pocket page, four on the backpack page.
		{"six pistols", []domain.ItemStack{{ItemID: "pistol", Quantity: 6}}, true},
		{"seven pistols", []domain.ItemStack{{ItemID: "pistol", Quantity: 7}}, false},
		// The aggregate hole: three ids, each at its own per-item cap, three
		// merged stacks — every count-based check passes, and it is 39 stacks
		// against a twelve-cell bag.
		{"three ids each at their own cap", []domain.ItemStack{
			{ItemID: "ammo_9mm", Quantity: 780},
			{ItemID: "scrap_metal", Quantity: 130},
			{ItemID: "copper_wire", Quantity: 130},
		}, false},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := domain.ValidateRaidItems(tc.items)
			if tc.accepted && err != nil {
				t.Errorf("%v must be accepted: %v", tc.items, err)
			}
			if !tc.accepted && err == nil {
				t.Errorf("%v was accepted; it does not fit a %d-cell bag", tc.items, domain.TotalCarryCells())
			}
		})
	}
}

// TestTheWornBackpackCostsNoCell pins the one exemption in the placer, which is
// also the arithmetic MaxRaidStacks is built on: twelve cells plus the bag on
// the player's back is thirteen stacks. A second backpack is cargo and takes
// its 2×2 like anything else.
func TestTheWornBackpackCostsNoCell(t *testing.T) {
	full := []domain.ItemStack{
		{ItemID: "backpack", Quantity: 1},
		{ItemID: "ammo_9mm", Quantity: 720}, // twelve 1×1 stacks, i.e. every cell
	}
	if err := domain.ValidateRaidItems(full); err != nil {
		t.Errorf("a full bag plus the bag itself must be accepted: %v", err)
	}
	if len(domain.MergeStacks(full)) != 2 {
		t.Fatal("fixture drifted")
	}

	second := []domain.ItemStack{
		{ItemID: "backpack", Quantity: 2},
		{ItemID: "ammo_9mm", Quantity: 720},
	}
	if err := domain.ValidateRaidItems(second); err == nil {
		t.Error("a full bag plus a spare bag was accepted; the spare needs 2x2 cells that are taken")
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

// TestKnownItemsMatchTheClientCatalog is the drift alarm, over ids, stack sizes
// *and* footprints: the per-item cap is slots × stackSize and the placement gate
// is the footprint, so either one disagreeing with the client is a bound that
// disagrees with what a player can actually carry — too small rejects a
// legitimate full backpack (a raid's haul deleted), too large re-opens the hole
// the shape-blind cap left. The client's items.json is the authored source; the
// domain map is a mirror, and a mirror that is never compared is a lie waiting
// to happen.
//
// A missing file is a FAILURE, not a skip. It used to be a skip "because the
// deployed image contains only sarko-api/" — but that image does not run tests,
// and a repo split or a sparse checkout would have turned the only alarm
// standing between the client and every server-side bound into a silent pass.
// Disarming it now takes a deliberate, greppable environment variable, and even
// that shouts on the way past.
func TestKnownItemsMatchTheClientCatalog(t *testing.T) {
	const path = "../../../SarkoGame/Content/Data/Items/items.json"
	// The escape hatch for a build that genuinely has no game directory. It is
	// opt-IN: absence of the file must never be self-certifying.
	const optOutEnv = "SARKO_CLIENT_CATALOG_OPTIONAL"

	raw, err := os.ReadFile(path)
	if err != nil {
		if os.Getenv(optOutEnv) == "" {
			t.Fatalf("client catalog not present at %s: %v\n"+
				"This test is the ONLY check that the server's item ids, stack sizes and cell "+
				"sizes still match the client's. Run it from a full checkout. If this build "+
				"deliberately has no SarkoGame/ tree, set %s=1 — and know that every bound in "+
				"domain.ItemDefs is then unverified.", path, err, optOutEnv)
		}
		t.Logf("!!! DRIFT ALARM DISARMED via %s: domain.ItemDefs is unverified against %s (%v)",
			optOutEnv, path, err)
		t.Skip("client catalog absent and explicitly declared optional")
	}

	var catalog struct {
		Items []struct {
			ID        string `json:"id"`
			StackSize int    `json:"stackSize"`
			Size      []int  `json:"size"`
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
		if item.StackSize <= 0 {
			t.Errorf("client item %q has stackSize %d; the catalog must state a positive stack size", item.ID, item.StackSize)
			continue
		}
		// size is REQUIRED in items.json (its own _readme says so) and the
		// server's placement gate is built on it, so a missing or malformed one
		// is not something to paper over with a 1×1 default.
		if len(item.Size) != 2 || item.Size[0] <= 0 || item.Size[1] <= 0 {
			t.Errorf("client item %q has size %v; the catalog must state [width, height] in whole cells", item.ID, item.Size)
			continue
		}
		client[item.ID] = struct{}{}

		def, ok := domain.ItemDefs[item.ID]
		if !ok {
			t.Errorf("client item %q is missing from domain.ItemDefs — a raid carrying it would be rejected", item.ID)
			continue
		}
		if def.StackSize != item.StackSize {
			t.Errorf("stackSize drift on %q: client says %d, domain says %d — the per-item cap would be %d instead of %d units",
				item.ID, item.StackSize, def.StackSize,
				domain.MaxRaidStacks*def.StackSize, domain.MaxRaidStacks*item.StackSize)
		}
		if def.Width != item.Size[0] || def.Height != item.Size[1] {
			t.Errorf("size drift on %q: client says %dx%d, domain says %dx%d — the server would accept or refuse hauls the player's own grid does not",
				item.ID, item.Size[0], item.Size[1], def.Width, def.Height)
		}
	}
	// The other direction only needs the ids: the shared ones were compared
	// above, and reporting each mismatch twice only obscures it.
	for id := range domain.ItemDefs {
		if _, ok := client[id]; !ok {
			t.Errorf("domain.ItemDefs has %q, which the client catalog does not define", id)
		}
	}
	// KnownItemIDs is derived, so this only guards the derivation itself.
	if len(domain.KnownItemIDs) != len(domain.ItemDefs) {
		t.Errorf("KnownItemIDs has %d ids, ItemDefs has %d", len(domain.KnownItemIDs), len(domain.ItemDefs))
	}
}

// TestCarryGridMatchesTheClientSettings pins the other half of the mirror: the
// pages themselves. The item sizes above are compared against the client's file;
// the grid they are placed into is a copy of USarkoRaidSettings::PocketGrid and
// ::BackpackGrid, which no file exports. If those move without this moving, the
// server starts refusing hauls the client can hold (a deleted raid) or accepting
// ones it cannot (the hole reopens), and MaxRaidStacks stops describing anything.
func TestCarryGridMatchesTheClientSettings(t *testing.T) {
	want := []domain.GridPage{{Columns: 2, Rows: 2}, {Columns: 4, Rows: 2}}
	if len(domain.CarryPages) != len(want) {
		t.Fatalf("CarryPages = %v, want %v (pocket page + worn backpack page)", domain.CarryPages, want)
	}
	for i, p := range want {
		if domain.CarryPages[i] != p {
			t.Errorf("CarryPages[%d] = %v, want %v", i, domain.CarryPages[i], p)
		}
	}
	// 12 cells plus the worn bag is what MaxRaidStacks counts. The two bounds
	// have to keep agreeing or the looser one stops being reachable.
	if got := domain.TotalCarryCells(); got != domain.MaxRaidStacks-1 {
		t.Errorf("the grid holds %d cells but MaxRaidStacks is %d; they must be cells+1 (the worn bag)",
			got, domain.MaxRaidStacks)
	}
}
