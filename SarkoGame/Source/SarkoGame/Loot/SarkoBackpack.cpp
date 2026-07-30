#include "Loot/SarkoBackpack.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/UnrealNetwork.h"

int32 SarkoLoot::AddToBackpack(TArray<FSarkoItemStack>& Slots, const FSarkoItemCatalog& Catalog,
	int32 SlotLimit, FName Item, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return 0;
	}

	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		// Refused whole. The alternative — a guessed stack size — puts an id the
		// backend will reject into the raid result, and the whole haul dies with it.
		return Quantity;
	}

	const int32 StackSize = FMath::Max(1, Def->StackSize);
	int32 Remaining = Quantity;

	// Top up existing partial stacks first.
	for (FSarkoItemStack& Stack : Slots)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Stack.Item != Item || Stack.Quantity >= StackSize)
		{
			continue;
		}
		const int32 Room = StackSize - Stack.Quantity;
		const int32 Moved = FMath::Min(Room, Remaining);
		Stack.Quantity += Moved;
		Remaining -= Moved;
	}

	// Then open new slots, while there are slots to open.
	while (Remaining > 0 && Slots.Num() < FMath::Max(0, SlotLimit))
	{
		const int32 Moved = FMath::Min(StackSize, Remaining);
		Slots.Add(FSarkoItemStack{ Item, Moved });
		Remaining -= Moved;
	}

	return Remaining;
}

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
}

int32 USarkoBackpackComponent::GetSlotLimit() const
{
	return FMath::Max(1, GetDefault<USarkoRaidSettings>()->BackpackSlots);
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
	return SarkoLoot::AddToBackpack(Slots, SarkoLoot::GetItemCatalog(), GetSlotLimit(), Item, Quantity);
}

void USarkoBackpackComponent::ClearOnDeath()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	// Empty, not "marked lost": the server submits the raid result from this
	// array, so the only way loot cannot be credited is for it not to be here.
	Slots.Reset();
}
