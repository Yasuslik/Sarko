package domain_test

import (
	"strings"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

// TestSlotAcceptsHoldsTheCategoryRule is the server's half of the rule the
// ІНВЕНТАР screen draws. It is here as well as on the client because what is
// equipped is what /v1/raid/start debits: a client that could equip a bike frame
// as a weapon could debit a bike frame and lose it on death.
func TestSlotAcceptsHoldsTheCategoryRule(t *testing.T) {
	tests := []struct {
		name     string
		slot     domain.EquipSlot
		itemID   string
		accepted bool
		// reason is a substring the refusal must name, so a refusal can never be
		// "no" with nothing said.
		reason string
	}{
		{"the pistol is a weapon", domain.SlotWeapon, "pistol", true, ""},
		{"the backpack is a bag", domain.SlotBackpack, "backpack", true, ""},
		{"the jacket is clothing", domain.SlotClothing, "jacket", true, ""},
		// The pair that makes the `slot` field authored rather than derived: both
		// are `gear`, and a bag is not a coat.
		{"a bag is not clothing", domain.SlotClothing, "backpack", false, "backpack slot"},
		{"a coat is not a bag", domain.SlotBackpack, "jacket", false, "clothing slot"},
		{"a medkit is not a weapon", domain.SlotWeapon, "medkit", false, "not equipment"},
		{"a bike frame is not a weapon", domain.SlotWeapon, "bike_frame", false, "not equipment"},
		{"an invented id is refused", domain.SlotWeapon, "unobtanium", false, "unknown item"},
		{"an invented slot is refused", domain.EquipSlot("hat"), "pistol", false, "unknown equipment slot"},
		{"the empty slot is not a target", domain.SlotNone, "pistol", false, "unknown equipment slot"},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := domain.SlotAccepts(tc.slot, tc.itemID)
			if tc.accepted {
				if err != nil {
					t.Fatalf("%s in %s must be accepted: %v", tc.itemID, tc.slot, err)
				}
				return
			}
			if err == nil {
				t.Fatalf("%s in %s was accepted", tc.itemID, tc.slot)
			}
			if !strings.Contains(err.Error(), tc.reason) {
				t.Errorf("refusal must name the reason %q, got %q", tc.reason, err)
			}
		})
	}

	// The id is never echoed back: it is unbounded caller input, the same rule
	// ValidateRaidItems follows.
	err := domain.SlotAccepts(domain.SlotWeapon, strings.Repeat("z", 40))
	if err == nil || strings.Contains(err.Error(), "z") {
		t.Errorf("an unknown id must be refused without being echoed, got %v", err)
	}
}

// TestAuthoredSlotsAgreeWithCategories pins the invariant the two authored
// fields have to keep between them: the weapon slot only ever holds a weapon,
// and the two worn-gear slots only ever hold gear. Without it the `slot` field
// could quietly disagree with the colour the cell is painted, which is the one
// signal a player reads before they read anything else.
//
// It is expressed over ItemDefs rather than over a literal list so that a new
// equippable item is covered the moment it is added.
func TestAuthoredSlotsAgreeWithCategories(t *testing.T) {
	// The client's items.json is the authored source of the category; this list is
	// the server's knowledge of which ids are equipment at all. Together they say
	// that the three slots have exactly one occupant each today.
	want := map[string]domain.EquipSlot{
		"pistol":   domain.SlotWeapon,
		"backpack": domain.SlotBackpack,
		"jacket":   domain.SlotClothing,
	}

	for id, def := range domain.ItemDefs {
		if def.Slot == domain.SlotNone {
			if _, equippable := want[id]; equippable {
				t.Errorf("%s should be equippable in %s and has no slot", id, want[id])
			}
			continue
		}
		if want[id] != def.Slot {
			t.Errorf("%s is authored for slot %q, expected %q", id, def.Slot, want[id])
		}
		if !domain.IsValidEquipSlot(string(def.Slot)) {
			t.Errorf("%s names slot %q, which is not a slot", id, def.Slot)
		}
	}
	for id, slot := range want {
		if domain.ItemDefs[id].Slot != slot {
			t.Errorf("%s must be authored for slot %s", id, slot)
		}
	}
}

// TestEquipmentLoadoutIsOneOfEachOccupiedSlot is the equipped -> loadout
// mapping, which is the whole of "what is equipped IS the loadout" (spec §4).
func TestEquipmentLoadoutIsOneOfEachOccupiedSlot(t *testing.T) {
	tests := []struct {
		name      string
		equipment map[string]string
		want      []domain.ItemStack
	}{
		{"nothing equipped is an empty loadout", nil, []domain.ItemStack{}},
		{"an empty map is an empty loadout", map[string]string{}, []domain.ItemStack{}},
		{
			"a full kit is three stacks of one",
			map[string]string{"weapon": "pistol", "backpack": "backpack", "clothing": "jacket"},
			[]domain.ItemStack{
				// MergeStacks sorts by id, so this order is the wire order.
				{ItemID: "backpack", Quantity: 1},
				{ItemID: "jacket", Quantity: 1},
				{ItemID: "pistol", Quantity: 1},
			},
		},
		{
			"a weapon alone",
			map[string]string{"weapon": "pistol"},
			[]domain.ItemStack{{ItemID: "pistol", Quantity: 1}},
		},
		{
			// An empty string is an unequipped slot, not an item.
			"an empty item id contributes nothing",
			map[string]string{"weapon": "", "backpack": "backpack"},
			[]domain.ItemStack{{ItemID: "backpack", Quantity: 1}},
		},
		{
			// The safe direction for a row that somehow named an id the game does
			// not define: debit nothing, rather than refuse the whole raid.
			"an unknown id contributes nothing",
			map[string]string{"weapon": "unobtanium"},
			[]domain.ItemStack{},
		},
		{
			// A slot this build does not know about is not a fourth stack.
			"an unknown slot is ignored",
			map[string]string{"hat": "pistol"},
			[]domain.ItemStack{},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := domain.EquipmentLoadout(tc.equipment)
			if len(got) != len(tc.want) {
				t.Fatalf("loadout = %v, want %v", got, tc.want)
			}
			for i := range got {
				if got[i] != tc.want[i] {
					t.Fatalf("loadout = %v, want %v", got, tc.want)
				}
			}
		})
	}
}

// TestTheFullEquipmentLoadoutPassesThePlausibilityGate is the re-check spec §4
// asks for by name: /v1/raid/start has only ever seen an EMPTY loadout, and the
// gate in front of it now reasons in cells and stacks. A gate that refused a
// legitimate full kit would refuse the raid outright.
//
// The arithmetic it depends on: the worn backpack costs no cell (domain.
// WornBagID), the jacket is 2×2 and fills the pocket page, and the pistol is 2×1
// on the backpack page. If a future slot's item cannot be placed alongside the
// others this test is what says so before a player finds out.
func TestTheFullEquipmentLoadoutPassesThePlausibilityGate(t *testing.T) {
	full := domain.EquipmentLoadout(map[string]string{
		"weapon": "pistol", "backpack": "backpack", "clothing": "jacket",
	})
	if err := domain.ValidateRaidItems(full); err != nil {
		t.Fatalf("a full equipment loadout must pass the gate: %v (%v)", err, full)
	}
	if err := domain.ValidateStacks(full); err != nil {
		t.Fatalf("a full equipment loadout must pass the shape check: %v", err)
	}

	// And the floor: an empty loadout stays legal, because entering a raid unarmed
	// is always allowed (spec §4) and it is what a client with nothing equipped
	// sends.
	if err := domain.ValidateRaidItems(domain.EquipmentLoadout(nil)); err != nil {
		t.Fatalf("an empty loadout must pass the gate: %v", err)
	}
}

// TestHasWeaponIsTheNoWeaponGuardsInput pins the fact the raid button's label is
// built from. It never blocks anything: unarmed is a choice.
func TestHasWeaponIsTheNoWeaponGuardsInput(t *testing.T) {
	if domain.HasWeapon(nil) {
		t.Error("an empty equipment set must report no weapon")
	}
	if domain.HasWeapon(map[string]string{"backpack": "backpack", "clothing": "jacket"}) {
		t.Error("a bag and a coat are not a weapon")
	}
	if !domain.HasWeapon(map[string]string{"weapon": "pistol"}) {
		t.Error("an equipped pistol is a weapon")
	}
	if domain.HasWeapon(map[string]string{"weapon": ""}) {
		t.Error("an empty weapon slot is not a weapon")
	}
}
