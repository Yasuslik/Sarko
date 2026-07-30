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

	/** Reads USarkoRaidSettings::BackpackSlots. */
	int32 GetSlotLimit() const;

	/**
	 * Server only. Returns the quantity that did not fit, which the caller
	 * leaves in the container (spec §4.3: partial loot is allowed).
	 */
	int32 AddItem(FName Item, int32 Quantity);

	/** Server only. Everything carried is lost. */
	void ClearOnDeath();

	/** Test seam: sets a known state without a world or a replication cycle. */
	void SetSlotsForTest(const TArray<FSarkoItemStack>& NewSlots) { Slots = NewSlots; }

private:
	/**
	 * COND_OwnerOnly. A replicated UPROPERTY that is not registered in
	 * GetLifetimeReplicatedProps silently never replicates, and nothing in a
	 * standalone test would notice, because the server is also the only client.
	 */
	UPROPERTY(Replicated)
	TArray<FSarkoItemStack> Slots;
};
