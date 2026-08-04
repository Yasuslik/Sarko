package domain

import "fmt"

// ItemDef is as much of one client catalog row as the server has to know: how
// many units share a stack, and the rectangle that stack occupies in the carry
// grid.
//
// Width and Height are whole cells and mirror items.json's `size: [w, h]`. They
// are never rotated, because the client's grid does not rotate either
// (SarkoGrid::FirstFit) — so a 3-wide frame can only ever enter the 4-wide
// backpack page, and that asymmetry is load-bearing rather than incidental.
type ItemDef struct {
	StackSize int
	Width     int
	Height    int
	// Slot is the equipment slot this item is worn in, or "" for cargo. It
	// mirrors items.json's authored `slot` (equipment spec §2), and it is what
	// makes SetEquipment refusable server-side: without it the API would have to
	// take the client's word for what a slot accepts, and the client is the party
	// whose loadout this decides.
	//
	// Authored rather than derived from a category for the reason items.json
	// records: `backpack` and `jacket` are both gear and a bag is not a coat.
	Slot EquipSlot
}

// ItemDefs is every item id the game can legitimately produce, mapped to its
// stack size and its footprint. It mirrors SarkoGame/Data/Items/items.json,
// which is the authored source — the values, not the file, because domain stays
// dependency-free and must compile in an image that ships only sarko-api/.
// loot_test.go reads that file and fails if the ids, the stack sizes or the
// sizes drift in either direction.
//
// Why the backend needs its own copy: stash_items.item_id is free-form TEXT and
// ValidateStacks only bounds its *length*, so before this list existed a client
// could submit any id it liked — including a helicopter turbine out of a starter
// sector — and have it credited. The client is the raid host in this slice
// (spec §4 "зафиксированная иллюзия"), so it is exactly the party that cannot
// be trusted with the set of things that exist — nor with how many of them fit,
// nor with how much room they take.
var ItemDefs = map[string]ItemDef{
	// Weapons and ammo.
	"pistol": {StackSize: 1, Width: 2, Height: 1, Slot: SlotWeapon},
	// Added with their meshes on 2026-08-04 and OBTAINABLE BY NOBODY yet: no loot
	// table rolls them and no ВИЛАЗКА kit grants them, so a client submitting one
	// today is a client that invented it. They are here regardless, because
	// loot_test.go's drift alarm runs in both directions and a client catalog row
	// without a mirror fails HERE. 3 wide because spec §5 makes the rifle's
	// footprint a balance decision, not a decoration.
	"rifle":    {StackSize: 1, Width: 3, Height: 1, Slot: SlotWeapon},
	"shotgun":  {StackSize: 1, Width: 3, Height: 1, Slot: SlotWeapon},
	"ammo_9mm": {StackSize: 60, Width: 1, Height: 1},
	// Medical.
	"medkit":      {StackSize: 3, Width: 1, Height: 1},
	"bandage":     {StackSize: 5, Width: 1, Height: 1},
	"painkillers": {StackSize: 5, Width: 1, Height: 1},
	// Junk and valuables.
	"scrap_metal": {StackSize: 10, Width: 1, Height: 1},
	"copper_wire": {StackSize: 10, Width: 1, Height: 1},
	"duct_tape":   {StackSize: 5, Width: 1, Height: 1},
	"canned_food": {StackSize: 5, Width: 1, Height: 1},
	// Drunk in the raid rather than carried home, but it is still a legal thing
	// to extract with — and an id absent from this map is a whole haul rejected
	// at result time. loot_test.go's drift alarm is a t.Fatalf precisely so that
	// a client-side catalog row without a mirror here fails here and not there.
	"water_bottle": {StackSize: 3, Width: 1, Height: 1},
	"vodka":        {StackSize: 3, Width: 1, Height: 1},
	"cigarettes":   {StackSize: 5, Width: 1, Height: 1},
	"toolbox":      {StackSize: 1, Width: 2, Height: 1},
	// Vehicle parts. These three are the bicycle recipe in garage.go; the later
	// tiers' parts (engine_small, fuel_tank, engine_large, wheel_medium,
	// wheel_large, gearbox, battery, turbine, rotor_blade, avionics) are
	// deliberately absent — no shipped loot table can produce them yet, so
	// accepting them would only ever be accepting a lie.
	"bike_frame":  {StackSize: 1, Width: 3, Height: 2},
	"wheel_small": {StackSize: 2, Width: 2, Height: 2},
	"chain":       {StackSize: 1, Width: 1, Height: 1},
	// Worn equipment. A backpack is submitted as a stack of one when the player
	// extracts wearing it, so the id has to exist here or the whole haul is
	// rejected at result time — which is how the client's catalog and this map
	// are kept honest by loot_test.go's drift alarm.
	"backpack": {StackSize: 1, Width: 2, Height: 2, Slot: SlotBackpack},
	// The clothing slot's only occupant, and OBTAINABLE since 2026-08-03.
	//
	// It used to be in the catalog and in no loot table at all, which made the
	// clothing slot fillable only by a debug exec — a slot that cannot be filled by
	// playing is a slot that does not exist. It now has two honest ways in: the
	// `good` loot tier at weight 3 (SarkoGame/Data/Loot/loot-tables.json), and two
	// of the ВИЛАЗКА kits in sortie.go. Spec §5 still keeps clothing a hook rather
	// than a system — a coat does nothing yet — but a hook you can reach is the
	// difference between a future feature and a dead slot.
	//
	// It has to be here regardless: loot_test.go's drift alarm runs in both
	// directions, so a client catalog row without a mirror fails HERE, which is
	// the whole point of that alarm.
	"jacket": {StackSize: 1, Width: 2, Height: 2, Slot: SlotClothing},
}

// KnownItemIDs is the set of legitimate item ids. It is derived from ItemDefs
// rather than written out a second time, so the id list and the definitions
// cannot drift from each other inside this package.
var KnownItemIDs = knownItemIDs()

func knownItemIDs() map[string]struct{} {
	ids := make(map[string]struct{}, len(ItemDefs))
	for id := range ItemDefs {
		ids[id] = struct{}{}
	}
	return ids
}

// StackSizeOf is how many units of an item share one stack — one rectangle in
// the grid. Unknown ids yield 0; callers reject those before they ever ask.
func StackSizeOf(itemID string) int { return ItemDefs[itemID].StackSize }

// MaxRaidStacks is the most item stacks one raid can deliver: twelve carried
// cells (4 pockets + 8 from a worn backpack, container-inventory spec §2.3) plus
// the worn backpack itself, which rides home as a thirteenth stack without ever
// occupying a cell. A result claiming fourteen describes a raid that could not
// have happened.
const MaxRaidStacks = 13

// MaxRaidUnits is the most units of one item a raid can physically produce:
// every slot of the backpack filled with that item's stack. It is 780 for
// ammo_9mm (13 × 60) but only 13 for bike_frame (13 × 1), which is the whole
// point — a flat 780 would have let one forged result deliver 780 bicycle
// frames, i.e. complete the bicycle recipe about 780 times over.
//
// Unknown ids yield 0; callers reject those before they ever ask for a cap.
//
// This is the outer wall, not the wall. It counts stacks and ignores the shape
// of them, so it still says 13 for a 3×2 bike_frame of which the bag holds
// exactly one. FitsCarryGrid is what closes that gap; this stays because it is
// O(1), it bounds the numbers *before* the placer turns them into rectangles,
// and a cheap check that cannot be tricked into allocating is worth keeping in
// front of one that can.
func MaxRaidUnits(itemID string) int {
	return MaxRaidStacks * ItemDefs[itemID].StackSize
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
// merged haul must fit MaxRaidStacks slots, one item is capped at
// MaxRaidStacks × its stackSize — and, finally, the whole haul must place into
// the carry grid (FitsCarryGrid).
//
// The last check is the one that binds. The three before it count; only the
// placer measures. Counting alone let thirteen 3×2 bicycle frames through a bag
// that holds one, and let thirteen ids each at their own cap through a bag of
// twelve cells. They stay in front of it because they are O(1) and they bound
// the quantities before the placer allocates a rectangle per stack.
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
		if _, ok := ItemDefs[s.ItemID]; !ok {
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
				s.ItemID, limit, MaxRaidStacks, ItemDefs[s.ItemID].StackSize, s.Quantity)
		}
	}

	// The geometry, last: the checks above are the outer wall (they bound the
	// numbers so this one can safely turn them into rectangles), and this is the
	// wall — the haul has to actually place into the bag the client carries.
	return FitsCarryGrid(merged)
}
