#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoEquipment.generated.h"

/**
 * What the player is wearing: one item id per slot, or NAME_None for an empty
 * one.
 *
 * Three named fields rather than a map, because there are exactly three slots
 * and that is a fact about the design, not a configuration — a map would invite
 * a fourth to appear without anything deciding to add one. SarkoEquip::Slots()
 * is the loop when a loop is wanted.
 *
 * It lives on FSarkoProfile, so it comes from the server on every /v1/profile and
 * survives a level travel the way the stash does. That is the whole of spec §6's
 * "equipment state must survive the trip: it lives with the profile, not the
 * world" — and it is server-side because equipment decides what /v1/raid/start
 * debits, so a client that owned it could equip a pistol it does not have.
 */
USTRUCT()
struct FSarkoEquipment
{
	GENERATED_BODY()

	UPROPERTY()
	FName Weapon;

	UPROPERTY()
	FName Backpack;

	UPROPERTY()
	FName Clothing;
};

/**
 * Everything about equipment that is a rule rather than a picture.
 *
 * Pure: catalogue definitions and structs in, values and strings out. No world,
 * no widget, no HTTP — which is what lets the slot rules, the refusal wording,
 * the equipped→loadout mapping and the no-weapon guard be unit tested under
 * -nullrhi, where there is no Slate application to build the ІНВЕНТАР screen in.
 *
 * It mirrors sarko-api/internal/domain/equipment.go, and the mirror is
 * deliberate rather than incidental: the client computes the loadout it sends and
 * the server debits it, so the two disagreeing about what "equipped" costs would
 * refuse a legitimate raid for insufficient_items.
 */
namespace SarkoEquip
{
	/** Every slot, in the order the character panel draws them top to bottom.
	 *  Pockets is NOT here: it is the carry grid, which belongs to a raid and is
	 *  never worn (spec §2 shows it inline, it does not equip into it). */
	const TArray<ESarkoEquipSlot>& Slots();

	/** The wire name POST /v1/profile/equipment takes — `weapon`, `backpack`,
	 *  `clothing`. Empty for None, which is never a target. */
	const TCHAR* WireName(ESarkoEquipSlot Slot);

	/** The dim caption drawn beside the slot. This is what makes a rectangle read
	 *  as "the weapon slot" rather than as an empty cell, which spec §6 names as
	 *  the one way the crude character drawing is allowed to fail: "acceptable if
	 *  the slots read; not acceptable if the player cannot tell which is which". */
	FString SlotCaption(ESarkoEquipSlot Slot);

	/**
	 * The rectangle an EMPTY slot draws at, in cells.
	 *
	 * The largest thing the slot accepts, not the smallest: the weapon slot is
	 * 3x1 because a rifle is 3x1 (spec §2), so the empty outline shows the room a
	 * weapon has rather than shrinking and then jumping when one goes in. An
	 * OCCUPIED slot draws at the item's own rect, which for the pistol is 2x1 —
	 * the item is the shape, and the slot is the space.
	 */
	FIntPoint EmptyExtent(ESarkoEquipSlot Slot);

	/** The item in a slot, or NAME_None. */
	FName Get(const FSarkoEquipment& Equipment, ESarkoEquipSlot Slot);

	/** Puts an item in a slot, or clears it with NAME_None. Local only — the
	 *  server is still the authority, and the controller sends the change. */
	void Set(FSarkoEquipment& Equipment, ESarkoEquipSlot Slot, FName Item);

	/**
	 * Which slot this item is worn in, or None when it is cargo.
	 *
	 * Reads FSarkoItemDef::EquipSlot, which is authored in items.json. A null
	 * definition — an id the catalog does not know, i.e. drift with the backend —
	 * is None: an unknown item is not equipment, which is the safe direction.
	 */
	ESarkoEquipSlot SlotFor(const FSarkoItemDef* Def);

	/**
	 * Whether this item may go in this slot, and **why not** when it may not.
	 *
	 * The reason is an output rather than something the caller composes, because
	 * a refusal with no reason is exactly the failure mode the container panel's
	 * refusal discipline exists to prevent (spec §2 asks for the same discipline
	 * here: "shake, amber rim, and a named reason"). Every false answer sets
	 * OutReason to something that names the item and the rule.
	 *
	 * bSlotOccupied is separate from the category rule on purpose: "that is a coat,
	 * not a gun" and "you are already holding a gun" are different facts with
	 * different answers, and telling a player the first when the second is true
	 * would send them looking for a different item.
	 */
	bool Accepts(ESarkoEquipSlot Slot, const FSarkoItemDef* Def, bool bSlotOccupied,
		FString& OutReason);

	/**
	 * What the equipment costs to take into a raid: one of each occupied slot.
	 *
	 * This is the whole of spec §4's "what is equipped IS the loadout", and it is
	 * the function whose output /v1/raid/start debits. Sorted by id, because
	 * domain.MergeStacks on the far side sorts by id and a diff between the two is
	 * easier to read when the orders match.
	 *
	 * A slot naming an id the catalog does not know contributes nothing rather
	 * than an unknown stack: the backend rejects the whole request for one bad id,
	 * and losing a raid to catalogue drift would be worse than entering it with
	 * one item fewer.
	 */
	TArray<FSarkoItemStack> Loadout(const FSarkoEquipment& Equipment,
		const FSarkoItemCatalog& Catalog);

	/**
	 * Whether a gun is equipped.
	 *
	 * NOTHING BRANCHES ON THIS TO REFUSE ANYTHING. Entering a raid unarmed is
	 * always allowed (spec §4: "'БЕЗ ЗБРОЇ' is a choice, not an error"), because a
	 * player who died with their only weapon equipped would otherwise have no way
	 * back in. It exists so the raid button can SAY so, and so that "the button
	 * never blocks" is a property a test can hold rather than a promise a comment
	 * makes.
	 */
	bool HasWeapon(const FSarkoEquipment& Equipment);

	/** The equipment as the wire sees it, for the one field /v1/profile carries.
	 *  Exposed so the parser's round trip is testable without HTTP. */
	bool ParseWireName(const FString& Name, ESarkoEquipSlot& OutSlot);
}
