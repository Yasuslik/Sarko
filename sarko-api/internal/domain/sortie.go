package domain

import "fmt"

// RaidMode is which kind of run a session is (spec §4.5).
//
// A string for the same reason Tier and EquipSlot are: it is stored, it is on the
// wire, and a renumbering must never be able to turn an ordinary raid into a free
// one — which in this case would mean a raid that debits nothing and is never
// refused for a cooldown.
type RaidMode string

const (
	// ModeRaid is an ordinary raid: the loadout is debited, extraction returns it,
	// death loses it, and an extraction latches tutorial_completed.
	ModeRaid RaidMode = "raid"
	// ModeSortie is a ВИЛАЗКА: free entry, a server-granted kit, a server-timed
	// cooldown, and it never latches the tutorial.
	ModeSortie RaidMode = "sortie"
)

// IsValidRaidMode guards client input. An EMPTY mode is valid and means
// ModeRaid: every client built before this field existed omits it, and a raid is
// the mode that costs the player something — degrading to the free one would be
// the wrong direction in every sense.
func IsValidRaidMode(s string) bool {
	return s == "" || RaidMode(s) == ModeRaid || RaidMode(s) == ModeSortie
}

// NormaliseRaidMode turns validated client input into the stored value.
func NormaliseRaidMode(s string) RaidMode {
	if RaidMode(s) == ModeSortie {
		return ModeSortie
	}
	return ModeRaid
}

// SortieKit is one authored borrowed loadout, and its share of the roll.
//
// Named as well as weighted because the name is what a log line says when a
// player asks what they were given, and because a test that asserts "every kit
// has a gun" is unreadable if the failure only names an index.
type SortieKit struct {
	Name string
	// Weight is this kit's share of the total, in arbitrary units. Not a
	// probability: probabilities have to be kept summing to one by hand, and the
	// first edit that forgets to is a table that silently never rolls its last row.
	Weight int
	Items  []ItemStack
}

// SortieKits is every kit a ВИЛАЗКА can hand out, and the whole of "the variance
// is the appeal" (spec §4.5).
//
// THE SHAPE OF THE TABLE, stated once so an edit can be judged against it:
//
//   - Every kit carries a gun and rounds for it. That is not decoration — the
//     sortie exists as the LADDER out of a hole ("broke → sortie → walk out with a
//     pistol → you own a pistol"), and a gunless kit is the floor the game already
//     ships (entering unarmed is always allowed). A rung you can be handed nothing
//     on is not a rung.
//   - The variance is the BAG and the extras, never the gun. Sometimes a bag,
//     sometimes not; occasionally a coat; rarely both plus a medkit.
//   - It is MEDIOCRE on purpose. Ten to twenty rounds against a raid's own loot
//     tables, which hand out ammo in twenties from military crates. The trade-off a
//     sortie makes is quality and a shorter clock (SortieTTL), never safety —
//     nothing here or anywhere else makes a sortie less dangerous than a raid.
//
// The weights are read as a share of the total (SumSortieWeights), so adding a
// row does not require touching the others.
//
// Everything in here has to be an item the CLIENT can draw and the stash can
// hold, because extraction credits it: SortieKitsAreLegalLoadouts pins every kit
// against ValidateRaidItems, which is the same gate a raid result passes.
var SortieKits = []SortieKit{
	{
		// The floor, and the common case. A worn pistol and a few rounds, which is
		// the kit spec §4.5 names first.
		Name:   "worn",
		Weight: 50,
		Items: []ItemStack{
			{ItemID: "pistol", Quantity: 1},
			{ItemID: "ammo_9mm", Quantity: 10},
		},
	},
	{
		// "Sometimes a bag, sometimes not." The bag is the biggest single upgrade a
		// sortie can hand out, because it is what turns four pocket cells into
		// twelve — so it is the coin flip rather than the rarity.
		Name:   "bagged",
		Weight: 30,
		Items: []ItemStack{
			{ItemID: "pistol", Quantity: 1},
			{ItemID: "ammo_9mm", Quantity: 14},
			{ItemID: "backpack", Quantity: 1},
		},
	},
	{
		// The coat, and one of the two honest ways into the clothing slot (the other
		// is the `good` loot tier — see SarkoGame/Data/Loot/loot-tables.json). Spec
		// §5 keeps clothing a hook rather than a system, so a jacket is worth no more
		// than the bandages beside it here; what it is worth is that the slot can be
		// filled by PLAYING.
		Name:   "patched",
		Weight: 15,
		Items: []ItemStack{
			{ItemID: "pistol", Quantity: 1},
			{ItemID: "ammo_9mm", Quantity: 12},
			{ItemID: "jacket", Quantity: 1},
			{ItemID: "bandage", Quantity: 2},
		},
	},
	{
		// "Occasionally something better." One in twenty, and still nothing a raid
		// could not have produced — the appeal is that the roll can be kind, not that
		// the free run out-earns the paid one.
		Name:   "provisioned",
		Weight: 5,
		Items: []ItemStack{
			{ItemID: "pistol", Quantity: 1},
			{ItemID: "ammo_9mm", Quantity: 20},
			{ItemID: "backpack", Quantity: 1},
			{ItemID: "jacket", Quantity: 1},
			{ItemID: "medkit", Quantity: 1},
		},
	},
}

// SumSortieWeights is the table's total weight. Zero is impossible with the table
// above and is handled by PickSortieKit anyway, because a table edited down to
// nothing must not divide by zero on the raid path.
func SumSortieWeights() int {
	total := 0
	for _, kit := range SortieKits {
		if kit.Weight > 0 {
			total += kit.Weight
		}
	}
	return total
}

// PickSortieKit chooses a kit from a roll in [0, SumSortieWeights()).
//
// The ROLL IS A PARAMETER and the randomness is the caller's. That is what makes
// this testable at all — "the table is walked in order and every kit is
// reachable" is a property of a pure function — and it is also the trust boundary
// stated in code: store.StartRaid draws the roll from the server's own
// math/rand/v2, so nothing a client sends can reach this argument. A client
// cannot ask for a better kit because it cannot ask for a kit.
//
// Out-of-range rolls clamp to the first kit rather than panicking: this is on the
// raid-start path, and the worst outcome of a bad roll must be a boring kit.
func PickSortieKit(roll int) SortieKit {
	if len(SortieKits) == 0 {
		return SortieKit{}
	}
	if roll < 0 {
		return SortieKits[0]
	}
	acc := 0
	for _, kit := range SortieKits {
		if kit.Weight <= 0 {
			continue
		}
		acc += kit.Weight
		if roll < acc {
			return kit
		}
	}
	return SortieKits[0]
}

// SortieKitStacks is the granted kit as a merged, sorted stack list — the shape
// raid_sessions.loadout stores and /v1/raid/start returns.
//
// Merged through MergeStacks so it is byte-identical to what the extraction path
// credits back, which is the property that makes "extraction returns exactly what
// the sortie granted" checkable rather than believable.
func SortieKitStacks(kit SortieKit) []ItemStack {
	return MergeStacks(kit.Items)
}

// ValidateSortieKit is the assertion that an authored kit is a thing this service
// could legitimately have produced.
//
// It exists because the granted kit is CREDITED on extraction, so an authored
// typo is a way to mint an item that does not exist, or a hundred of one that
// does. Called by the kit test rather than on the raid path — the table is a
// compile-time constant, so a runtime check would be checking the build.
func ValidateSortieKit(kit SortieKit) error {
	if len(kit.Items) == 0 {
		return fmt.Errorf("kit %q is empty", kit.Name)
	}
	stacks := SortieKitStacks(kit)
	if err := ValidateStacks(stacks); err != nil {
		return fmt.Errorf("kit %q: %w", kit.Name, err)
	}
	// The same gate a raid result passes, and for the same reason: the kit lands in
	// a stash, so it has to be a haul a raid could have carried.
	if err := ValidateRaidItems(stacks); err != nil {
		return fmt.Errorf("kit %q: %w", kit.Name, err)
	}
	return nil
}
