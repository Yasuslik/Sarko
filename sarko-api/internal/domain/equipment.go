package domain

import "fmt"

// EquipSlot is one place on the character something is worn. It mirrors
// ESarkoEquipSlot in SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h and
// items.json's authored `slot` field.
//
// A string and not an integer, for the same reason Tier is: it is stored, it is
// on the wire, and a renumbering must never be able to move a player's pistol
// into their coat.
type EquipSlot string

const (
	// SlotNone is the zero value and means "this item is cargo, not equipment".
	SlotNone     EquipSlot = ""
	SlotWeapon   EquipSlot = "weapon"
	SlotBackpack EquipSlot = "backpack"
	SlotClothing EquipSlot = "clothing"
)

// EquipSlots is every slot a player has, in the order the client draws them.
// Pockets is deliberately NOT here: it is the carry grid, which belongs to a
// raid and is never stored on the profile (equipment spec §2).
var EquipSlots = []EquipSlot{SlotWeapon, SlotBackpack, SlotClothing}

// IsValidEquipSlot guards client input. The empty slot is not valid as a
// *target*: clearing a slot names the slot and sends an empty item id.
func IsValidEquipSlot(s string) bool {
	for _, slot := range EquipSlots {
		if EquipSlot(s) == slot {
			return true
		}
	}
	return false
}

// SlotAccepts reports whether itemID may be worn in slot, and says why not when
// it may not.
//
// This is the server's copy of the rule the ІНВЕНТАР screen enforces, and it is
// here rather than only there because the equipment decides what /v1/raid/start
// debits: a client that could equip a bike frame in the weapon slot could debit
// a bike frame as a loadout and lose it on death, which is a way to destroy your
// own stash that no player asked for. The client is the party this rule is
// about, so the client cannot be the only party that holds it.
//
// The reason is returned as an error rather than a bool because a refused equip
// with no reason is the failure mode the container panel's refusal discipline
// exists to prevent, and the API's message is what the client shows.
func SlotAccepts(slot EquipSlot, itemID string) error {
	if !IsValidEquipSlot(string(slot)) {
		return fmt.Errorf("unknown equipment slot")
	}
	def, ok := ItemDefs[itemID]
	if !ok {
		// The id is not echoed: it is unbounded caller input, the same reason
		// ValidateRaidItems refuses to echo one.
		return fmt.Errorf("unknown item id")
	}
	if def.Slot == SlotNone {
		return fmt.Errorf("that item is not equipment")
	}
	if def.Slot != slot {
		return fmt.Errorf("that item is worn in the %s slot, not %s", def.Slot, slot)
	}
	return nil
}

// EquipmentLoadout is what a set of equipment costs to take into a raid: one
// stack of one per occupied slot, merged and sorted so equal equipment always
// produces byte-identical output.
//
// This is the whole of "what is equipped IS the loadout" (spec §4), and it lives
// in domain so both ends compute it the same way: the client sends it as the
// loadout and the server debits it, and the two agreeing by construction is what
// keeps a legitimate raid from being refused for insufficient_items over a
// disagreement about what "equipped" costs.
//
// An empty or unknown id in a slot contributes nothing rather than an invalid
// stack: the stored equipment is validated on the way in, so this cannot
// normally see one, and the safe direction for an id that slipped through is to
// debit nothing rather than to reject the whole raid.
func EquipmentLoadout(equipment map[string]string) []ItemStack {
	stacks := make([]ItemStack, 0, len(EquipSlots))
	for _, slot := range EquipSlots {
		itemID := equipment[string(slot)]
		if itemID == "" {
			continue
		}
		if _, ok := ItemDefs[itemID]; !ok {
			continue
		}
		stacks = append(stacks, ItemStack{ItemID: itemID, Quantity: 1})
	}
	return MergeStacks(stacks)
}

// HasWeapon reports whether this equipment carries a gun.
//
// The no-weapon case is a LEGAL choice and never a refusal (spec §4: "entering a
// raid unarmed is always allowed ... 'БЕЗ ЗБРОЇ' is a choice, not an error"), so
// nothing on the server branches on this — it exists so the rule can be stated
// once and tested, and so the client's raid-button label and the server agree on
// what "armed" means.
func HasWeapon(equipment map[string]string) bool {
	return equipment[string(SlotWeapon)] != ""
}
