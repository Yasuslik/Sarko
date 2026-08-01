#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoBackpack.generated.h"

namespace SarkoLoot
{
	/**
	 * Adds Quantity of Item to Slots, stacking by the catalog's stackSize, and
	 * returns how much did **not** fit.
	 *
	 * Pure: it takes the slots by reference and the limit as an argument rather
	 * than reading a component or the settings, so the whole stacking rule — the
	 * thing that decides whether a haul survives a raid — is unit tested with no
	 * world, no actor and no config.
	 *
	 * Partial stacks are topped up before a new slot is opened, or a 12-slot
	 * backpack fills with half-empty stacks and the limit stops meaning
	 * anything. An unknown item is refused whole: guessing a stack size would
	 * put an id the backend rejects into a raid result.
	 */
	int32 AddToBackpack(TArray<FSarkoItemStack>& Slots, const FSarkoItemCatalog& Catalog,
		int32 SlotLimit, FName Item, int32 Quantity);

	/** The one gear id that grants capacity. Named once, because it is compared
	 *  against on the take path and must never be a loose literal. */
	extern const FName BackpackItemId;

	/**
	 * How many cells a pawn has. Pure, so the single number the whole economy
	 * turns on is unit tested with no world and no settings object.
	 *
	 * Clamped at zero: a negative capacity makes AddToBackpack's
	 * `Slots.Num() < SlotLimit` loop guard behave inconsistently across the two
	 * places capacity is read, which is a haul that half-fits.
	 */
	int32 CapacityFor(bool bBackpackWorn, int32 BaseCells, int32 BonusCells);
}

/**
 * What the player is carrying right now.
 *
 * Replicated **owner-only** (spec §6.1): another player's haul is exactly the
 * information that makes them worth killing, and in a PvP slice it must not be
 * on the wire at all. It is also why this is a component on the pawn rather
 * than state on the game state, which replicates to everyone.
 *
 * Cleared on death by the server, so a dead player's loot is gone before any
 * result is submitted — the loss is real, not cosmetic (spec §4.4).
 */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoBackpackComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FSarkoItemStack>& GetSlots() const { return Slots; }

	int32 GetUsedSlots() const { return Slots.Num(); }

	/** SarkoLoot::CapacityFor over USarkoRaidSettings' two cell dials. */
	int32 GetSlotLimit() const;

	bool IsWearingBackpack() const { return EquippedBackpack != NAME_None; }

	/** Server only. Wearing, not carrying: the bag does not occupy a cell — spec
	 *  §2.3's 4 + 8 = 12 only works if it does not. */
	void EquipBackpack(FName Item);

	/**
	 * What gets submitted to /v1/raid/result: the cells, plus the worn bag as
	 * one stack if there is one. A bag you extracted with is loot, and the one
	 * thing a player could otherwise carry out and never be credited for.
	 */
	TArray<FSarkoItemStack> GetHaulForSubmission() const;

	/**
	 * Server only. Returns the quantity that did not fit, which the caller
	 * leaves in the container (spec §4.3: partial loot is allowed).
	 */
	int32 AddItem(FName Item, int32 Quantity);

	/** Server only. Everything carried is lost. */
	void ClearOnDeath();

	/**
	 * Server only in practice. A test seam — it sets a known state without a
	 * world or a replication cycle — and, since the container panel arrived, also
	 * the server's write-back path after SarkoLoot::TransferOne has moved units
	 * into a local copy of the cells. Named SetSlots rather than SetSlotsForTest
	 * because a name that says "for test" on a production path is a lie.
	 */
	void SetSlots(const TArray<FSarkoItemStack>& NewSlots) { Slots = NewSlots; }

private:
	/**
	 * COND_OwnerOnly. A replicated UPROPERTY that is not registered in
	 * GetLifetimeReplicatedProps silently never replicates, and nothing in a
	 * standalone test would notice, because the server is also the only client.
	 */
	UPROPERTY(Replicated)
	TArray<FSarkoItemStack> Slots;

	/**
	 * The worn backpack's item id, or NAME_None. COND_OwnerOnly for the same
	 * reason Slots is: how much another player can carry is part of how much
	 * they are worth killing.
	 */
	UPROPERTY(Replicated)
	FName EquippedBackpack;
};
