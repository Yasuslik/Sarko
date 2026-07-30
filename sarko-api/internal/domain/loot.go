package domain

import "fmt"

// ItemStackSizes is every item id the game can legitimately produce, mapped to
// how many units of it fit in one backpack slot. It mirrors
// SarkoGame/Data/Items/items.json, which is the authored source — the values,
// not the file, because domain stays dependency-free and must compile in an
// image that ships only sarko-api/. loot_test.go reads that file and fails if
// the ids or the stack sizes drift in either direction.
//
// Why the backend needs its own copy: stash_items.item_id is free-form TEXT and
// ValidateStacks only bounds its *length*, so before this list existed a client
// could submit any id it liked — including a helicopter turbine out of a starter
// sector — and have it credited. The client is the raid host in this slice
// (spec §4 "зафиксированная иллюзия"), so it is exactly the party that cannot
// be trusted with the set of things that exist — nor with how many of them fit.
var ItemStackSizes = map[string]int{
	// Weapons and ammo.
	"pistol":   1,
	"ammo_9mm": 60,
	// Medical.
	"medkit":      3,
	"bandage":     5,
	"painkillers": 5,
	// Junk and valuables.
	"scrap_metal": 10,
	"copper_wire": 10,
	"duct_tape":   5,
	"canned_food": 5,
	"vodka":       3,
	"cigarettes":  5,
	"toolbox":     1,
	// Vehicle parts. These three are the bicycle recipe in garage.go; the later
	// tiers' parts (engine_small, fuel_tank, engine_large, wheel_medium,
	// wheel_large, gearbox, battery, turbine, rotor_blade, avionics) are
	// deliberately absent — no shipped loot table can produce them yet, so
	// accepting them would only ever be accepting a lie.
	"bike_frame":  1,
	"wheel_small": 2,
	"chain":       1,
}

// KnownItemIDs is the set of legitimate item ids. It is derived from
// ItemStackSizes rather than written out a second time, so the id list and the
// stack sizes cannot drift from each other inside this package.
var KnownItemIDs = knownItemIDs()

func knownItemIDs() map[string]struct{} {
	ids := make(map[string]struct{}, len(ItemStackSizes))
	for id := range ItemStackSizes {
		ids[id] = struct{}{}
	}
	return ids
}

// MaxRaidStacks is the in-raid backpack size (spec §4.4): twelve slots, so a
// result claiming thirteen distinct item stacks describes a raid that could not
// have happened.
const MaxRaidStacks = 12

// MaxRaidUnits is the most units of one item a raid can physically produce:
// every slot of the backpack filled with that item's stack. It is 720 for
// ammo_9mm (12 × 60) but only 12 for bike_frame (12 × 1), which is the whole
// point — a flat 720 would have let one forged result deliver 720 bicycle
// frames, i.e. complete the bicycle recipe about 720 times over.
//
// Unknown ids yield 0; callers reject those before they ever ask for a cap.
func MaxRaidUnits(itemID string) int {
	return MaxRaidStacks * ItemStackSizes[itemID]
}

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
// have carried. The backpack is the physical bound on a raid, so the rules are
// its geometry: every id must exist, every quantity must be positive, the
// merged haul must fit MaxRaidStacks slots, and one item is capped at
// MaxRaidStacks × its stackSize — the slots-times-stack-size product, not a
// flat number. That distinction is the difference between 720 rounds of 9×18
// (a plausible full backpack of ammo) and 720 bicycle frames (which do not
// stack, so twelve is every frame twelve slots can hold).
//
// The quantity check is repeated here rather than left to ValidateStacks:
// nothing pins the order the two are called in, and without it a forged result
// could smuggle an over-cap stack past the per-item cap by merging it with a
// negative entry of the same id.
//
// It is deliberately separate from ValidateStacks, which bounds shape rather
// than content and is called from inside store.StartRaid and
// store.SubmitResult. Keeping content validation at the API edge leaves the
// store's own tests — which use fixture ids like "ammo" and "rifle" — working
// against the layer they actually test.
func ValidateRaidItems(stacks []ItemStack) error {
	for _, s := range stacks {
		if _, ok := ItemStackSizes[s.ItemID]; !ok {
			// The id is not echoed: it is unbounded caller input, and
			// ValidateStacks already refuses to echo it for the same reason.
			return fmt.Errorf("unknown item id")
		}
		if s.Quantity <= 0 {
			return fmt.Errorf("item %s: quantity must be positive, got %d", s.ItemID, s.Quantity)
		}
	}

	merged := MergeStacks(stacks)
	if len(merged) > MaxRaidStacks {
		return fmt.Errorf("at most %d item stacks fit a backpack, got %d", MaxRaidStacks, len(merged))
	}
	for _, s := range merged {
		if limit := MaxRaidUnits(s.ItemID); s.Quantity > limit {
			return fmt.Errorf("item %s: at most %d units fit a backpack (%d slots × a stack of %d), got %d",
				s.ItemID, limit, MaxRaidStacks, ItemStackSizes[s.ItemID], s.Quantity)
		}
	}
	return nil
}
