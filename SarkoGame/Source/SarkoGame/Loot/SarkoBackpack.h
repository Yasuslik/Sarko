#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"

#include "SarkoBackpack.generated.h"

namespace SarkoLoot
{
	/** The one gear id that grants capacity. Named once, because it is compared
	 *  against on the take path and must never be a loose literal. */
	extern const FName BackpackItemId;

	/**
	 * The rounds the pistol fires, and now the only thing that puts them in it
	 * (spec §1). Named here for BackpackItemId's reason and one more: the reload
	 * path, the HUD's reserve readout and the extraction fold-back all compare
	 * against it, and three loose literals is three places for a typo to become an
	 * ammo supply that silently never depletes.
	 */
	extern const FName AmmoItemId;
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

	/** The pages this pawn carries right now: pockets, plus the bag if one is
	 *  worn. USarkoRaidSettings' two grid dials, through SarkoGrid::CarryPages. */
	TArray<FSarkoGridPage> GetCarryPages() const;

	/** Cells occupied, by area. What the HUD's n/m now counts. */
	int32 GetUsedCells() const;

	/** Cells available, by area: 4 without a bag, 12 with one. */
	int32 GetCellCount() const;

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

	/**
	 * How many units of one id are carried right now. Const and authority-free on
	 * purpose: the owning client reads it every frame to draw the ammo reserve, and
	 * Slots is COND_OwnerOnly, so the number it sees is its own and nobody else's.
	 */
	int32 CountItem(FName Item) const;

	/**
	 * Server only. Takes up to Quantity units out and returns how many actually
	 * left — the reload path's half of the transfer (spec §1), and the only way
	 * anything ever leaves the grid other than consuming or dying.
	 *
	 * Fewer than asked for is a normal answer, not a failure: three rounds in the
	 * bag load three rounds into the magazine.
	 */
	int32 RemoveItem(FName Item, int32 Quantity);

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
