#include "Loot/SarkoBackpack.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/UnrealNetwork.h"

const FName SarkoLoot::BackpackItemId(TEXT("backpack"));

USarkoBackpackComponent::USarkoBackpackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Without this the component itself is never considered for replication and
	// the Slots registration below has nothing to run on.
	SetIsReplicatedByDefault(true);
}

void USarkoBackpackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Owner-only: a haul on the wire is a target list for everyone else.
	DOREPLIFETIME_CONDITION(USarkoBackpackComponent, Slots, COND_OwnerOnly);
	// Registered, or it silently never replicates and the owning client's panel
	// draws four cells for a pawn the server thinks has twelve.
	DOREPLIFETIME_CONDITION(USarkoBackpackComponent, EquippedBackpack, COND_OwnerOnly);
}

TArray<FSarkoGridPage> USarkoBackpackComponent::GetCarryPages() const
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	return SarkoGrid::CarryPages(IsWearingBackpack(), Settings.PocketGrid, Settings.BackpackGrid);
}

int32 USarkoBackpackComponent::GetUsedCells() const
{
	return SarkoGrid::UsedCells(Slots, SarkoLoot::GetItemCatalog());
}

int32 USarkoBackpackComponent::GetCellCount() const
{
	return SarkoGrid::TotalCells(GetCarryPages());
}

void USarkoBackpackComponent::EquipBackpack(FName Item)
{
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoBackpack: EquipBackpack called without authority; ignored"));
		return;
	}
	EquippedBackpack = Item;
}

TArray<FSarkoItemStack> USarkoBackpackComponent::GetHaulForSubmission() const
{
	TArray<FSarkoItemStack> Haul = Slots;
	if (IsWearingBackpack())
	{
		Haul.Add(FSarkoItemStack{ EquippedBackpack, 1 });
	}
	return Haul;
}

int32 USarkoBackpackComponent::AddItem(FName Item, int32 Quantity)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// A client calling this would change nothing on the server and then be
		// corrected by the next replication — a confusing flicker rather than a
		// cheat, but still a bug worth refusing loudly.
		UE_LOG(LogTemp, Warning, TEXT("SarkoBackpack: AddItem called without authority; ignored"));
		return Quantity;
	}
	return SarkoGrid::AddToGrid(Slots, SarkoLoot::GetItemCatalog(), GetCarryPages(), Item, Quantity);
}

void USarkoBackpackComponent::ClearOnDeath()
{
	// Only the server may empty a backpack. The null-owner case passes through
	// rather than refusing, which is the same authority-guard shape
	// USarkoHealthComponent::ApplyDamage uses and for the same reason: a NewObject
	// component has no owner, and this is the effect that Sarko.Extract.
	// LosingOutcomesLoseTheHaul has to be able to observe. Nothing in a running
	// game reaches a backpack with no owner — the component is a default subobject
	// of the pawn.
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		return;
	}
	// Empty, not "marked lost": the server submits the raid result from this
	// array, so the only way loot cannot be credited is for it not to be here.
	Slots.Reset();
	// The bag goes with the pockets. Spec §5's default, made explicit: everything
	// carried is lost, and a bag that survived death would be the one piece of
	// gear the core rule did not apply to.
	EquippedBackpack = NAME_None;
}
