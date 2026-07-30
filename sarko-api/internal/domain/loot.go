package domain

import "fmt"

// KnownItemIDs is every item id the game can legitimately produce. It mirrors
// SarkoGame/Data/Items/items.json, which is the authored source; loot_test.go
// reads that file and fails if the two lists drift.
//
// Why the backend needs its own copy: stash_items.item_id is free-form TEXT and
// ValidateStacks only bounds its *length*, so before this list existed a client
// could submit any id it liked — including a helicopter turbine out of a starter
// sector — and have it credited. The client is the raid host in this slice
// (spec §4 "зафиксированная иллюзия"), so it is exactly the party that cannot
// be trusted with the set of things that exist.
var KnownItemIDs = map[string]struct{}{
	// Weapons and ammo.
	"pistol":   {},
	"ammo_9mm": {},
	// Medical.
	"medkit":      {},
	"bandage":     {},
	"painkillers": {},
	// Junk and valuables.
	"scrap_metal": {},
	"copper_wire": {},
	"duct_tape":   {},
	"canned_food": {},
	"vodka":       {},
	"cigarettes":  {},
	"toolbox":     {},
	// Vehicle parts. These three are the bicycle recipe in garage.go; the later
	// tiers' parts (engine_small, fuel_tank, engine_large, wheel_medium,
	// wheel_large, gearbox, battery, turbine, rotor_blade, avionics) are
	// deliberately absent — no shipped loot table can produce them yet, so
	// accepting them would only ever be accepting a lie.
	"bike_frame":  {},
	"wheel_small": {},
	"chain":       {},
}

const (
	// MaxRaidStacks is the in-raid backpack size (spec §4.4): twelve slots, so
	// a result claiming thirteen distinct item stacks describes a raid that
	// could not have happened.
	MaxRaidStacks = 12
	// MaxRaidUnitsPerItem is twelve slots of the largest stack in the catalog
	// (ammo_9mm, 60), i.e. the most units of one item a full backpack can hold.
	// Deliberately generous: this is the ceiling on the physically possible,
	// not a balance number, and the balance ceiling is the loot tables.
	MaxRaidUnitsPerItem = 720
)

// StarterKit is what a brand-new player is given once, at anonymous
// registration (spec §4.6). Without it a new player has an empty stash, cannot
// field a loadout, and — since the loadout is debited at /v1/raid/start —
// enters the first raid unarmed, which the spec's decision journal (§2.2)
// explicitly rejects as breaking the loot economy.
func StarterKit() []ItemStack {
	return []ItemStack{
		{ItemID: "pistol", Quantity: 1},
		{ItemID: "ammo_9mm", Quantity: 60},
		{ItemID: "medkit", Quantity: 1},
	}
}

// ValidateRaidItems is the plausibility gate on anything a client claims to
// have carried: every id must exist, the haul must fit a backpack, and no
// single stack may exceed what a backpack could physically hold.
//
// It is deliberately separate from ValidateStacks, which bounds shape rather
// than content and is called from inside store.StartRaid and
// store.SubmitResult. Keeping content validation at the API edge leaves the
// store's own tests — which use fixture ids like "ammo" and "rifle" — working
// against the layer they actually test.
func ValidateRaidItems(stacks []ItemStack) error {
	for _, s := range stacks {
		if _, ok := KnownItemIDs[s.ItemID]; !ok {
			// The id is not echoed: it is unbounded caller input, and
			// ValidateStacks already refuses to echo it for the same reason.
			return fmt.Errorf("unknown item id")
		}
	}

	merged := MergeStacks(stacks)
	if len(merged) > MaxRaidStacks {
		return fmt.Errorf("at most %d item stacks fit a backpack, got %d", MaxRaidStacks, len(merged))
	}
	for _, s := range merged {
		if s.Quantity > MaxRaidUnitsPerItem {
			return fmt.Errorf("item %s: at most %d units fit a backpack, got %d",
				s.ItemID, MaxRaidUnitsPerItem, s.Quantity)
		}
	}
	return nil
}
