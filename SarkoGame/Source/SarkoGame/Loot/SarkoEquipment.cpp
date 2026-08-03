#include "Loot/SarkoEquipment.h"

#include "UI/SarkoInventoryStyle.h"

const TArray<ESarkoEquipSlot>& SarkoEquip::Slots()
{
	// Weapon first because it is what the raid button's label depends on, then the
	// two 2x2 slots that sit side by side on the body.
	static const TArray<ESarkoEquipSlot> Order = {
		ESarkoEquipSlot::Weapon, ESarkoEquipSlot::Clothing, ESarkoEquipSlot::Backpack
	};
	return Order;
}

const TCHAR* SarkoEquip::WireName(ESarkoEquipSlot Slot)
{
	switch (Slot)
	{
	case ESarkoEquipSlot::Weapon:   return TEXT("weapon");
	case ESarkoEquipSlot::Backpack: return TEXT("backpack");
	case ESarkoEquipSlot::Clothing: return TEXT("clothing");
	default:                        return TEXT("");
	}
}

bool SarkoEquip::ParseWireName(const FString& Name, ESarkoEquipSlot& OutSlot)
{
	for (ESarkoEquipSlot Slot : Slots())
	{
		if (Name == WireName(Slot))
		{
			OutSlot = Slot;
			return true;
		}
	}
	return false;
}

FString SarkoEquip::SlotCaption(ESarkoEquipSlot Slot)
{
	switch (Slot)
	{
	case ESarkoEquipSlot::Weapon:   return TEXT("ЗБРОЯ");
	case ESarkoEquipSlot::Backpack: return TEXT("РЮКЗАК");
	case ESarkoEquipSlot::Clothing: return TEXT("ОДЯГ");
	default:                        return FString();
	}
}

FIntPoint SarkoEquip::EmptyExtent(ESarkoEquipSlot Slot)
{
	switch (Slot)
	{
	// 3x1: a rifle's rect (spec §2's table). The pistol that goes in it is 2x1 and
	// draws at its own size, so the outline is the room and the item is the shape.
	case ESarkoEquipSlot::Weapon:   return FIntPoint(3, 1);
	case ESarkoEquipSlot::Backpack: return FIntPoint(2, 2);
	case ESarkoEquipSlot::Clothing: return FIntPoint(2, 2);
	default:                        return FIntPoint(1, 1);
	}
}

FName SarkoEquip::Get(const FSarkoEquipment& Equipment, ESarkoEquipSlot Slot)
{
	switch (Slot)
	{
	case ESarkoEquipSlot::Weapon:   return Equipment.Weapon;
	case ESarkoEquipSlot::Backpack: return Equipment.Backpack;
	case ESarkoEquipSlot::Clothing: return Equipment.Clothing;
	default:                        return NAME_None;
	}
}

void SarkoEquip::Set(FSarkoEquipment& Equipment, ESarkoEquipSlot Slot, FName Item)
{
	switch (Slot)
	{
	case ESarkoEquipSlot::Weapon:   Equipment.Weapon = Item;   break;
	case ESarkoEquipSlot::Backpack: Equipment.Backpack = Item; break;
	case ESarkoEquipSlot::Clothing: Equipment.Clothing = Item; break;
	default: break;
	}
}

ESarkoEquipSlot SarkoEquip::SlotFor(const FSarkoItemDef* Def)
{
	// A null definition is an id the catalog does not know — drift with the
	// backend, which stays visible in the stash grid as a raw id — and an unknown
	// item is not equipment. Refusing to guess is the safe direction: guessing
	// "weapon" would let drift put an arbitrary id into the loadout.
	return Def ? Def->EquipSlot : ESarkoEquipSlot::None;
}

bool SarkoEquip::Accepts(ESarkoEquipSlot Slot, const FSarkoItemDef* Def, bool bSlotOccupied,
	FString& OutReason)
{
	OutReason.Reset();

	if (!Def)
	{
		OutReason = TEXT("НЕВІДОМИЙ ПРЕДМЕТ");
		return false;
	}

	// The label the reason names the item by is the SHORT one a cell draws, so the
	// sentence and the cell the player just tapped say the same word. A display
	// name of five Cyrillic syllables in a refusal note beside a cell that reads
	// ЛАНЦ is two names for one thing.
	const FString ItemLabel = SarkoUI::CellLabelFor(Def, Def->Id);

	// THE ITEM IS ASKED ABOUT BEFORE THE SLOT IS, and that order is what a frame
	// corrected. Tapping a stash cell routes by the item's own slot, so a medkit
	// arrives here with Slot == None — and answering "НЕМА ТАКОГО СЛОТА" told the
	// player about the *screen* when the fact they needed was about the *item*. What
	// is wrong with tapping a medkit is that a medkit is not equipment; that it
	// therefore has no slot is a consequence, not the reason.
	const ESarkoEquipSlot Home = SlotFor(Def);
	if (Home == ESarkoEquipSlot::None)
	{
		// The commonest refusal by far: most of what is in a stash is cargo.
		OutReason = FString::Printf(TEXT("%s — НЕ СНАРЯЖЕННЯ"), *ItemLabel);
		return false;
	}

	// Only reachable for an item that IS equipment named against no slot at all,
	// which is a caller bug rather than a player action. It still says something.
	if (Slot == ESarkoEquipSlot::None)
	{
		OutReason = TEXT("НЕМА ТАКОГО СЛОТА");
		return false;
	}

	if (Home != Slot)
	{
		// Named both ways round, because "not here" alone leaves the player
		// hunting: it says where the thing DOES go, which turns a refusal into an
		// instruction. This is the case that makes the `slot` field authored rather
		// than derived — a backpack and a jacket are both `gear`.
		OutReason = FString::Printf(TEXT("%s — ЦЕ %s, НЕ %s"),
			*ItemLabel, *SlotCaption(Home), *SlotCaption(Slot));
		return false;
	}

	// Checked LAST and separately, because a full slot is a different fact from a
	// wrong category and the two must not share a sentence: this one is answered by
	// tapping what is already worn, and that one is answered by finding another
	// item.
	if (bSlotOccupied)
	{
		OutReason = FString::Printf(TEXT("СЛОТ ЗАЙНЯТО: %s"), *SlotCaption(Slot));
		return false;
	}

	return true;
}

TArray<FSarkoItemStack> SarkoEquip::Loadout(const FSarkoEquipment& Equipment,
	const FSarkoItemCatalog& Catalog)
{
	TArray<FSarkoItemStack> Loadout;
	for (ESarkoEquipSlot Slot : Slots())
	{
		const FName Item = Get(Equipment, Slot);
		if (Item.IsNone())
		{
			continue;
		}
		// An id the catalog does not know contributes nothing: the backend rejects
		// the whole /v1/raid/start body for one unknown id, and losing the raid to
		// catalogue drift is worse than entering it one item short.
		if (Catalog.Find(Item) == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SarkoEquip: '%s' is equipped in slot %d and is not in the catalog — it is left out of the loadout"),
				*Item.ToString(), static_cast<int32>(Slot));
			continue;
		}
		Loadout.Add(FSarkoItemStack{ Item, 1 });
	}

	// By id, matching domain.MergeStacks on the far side. Equal equipment then
	// produces an identical body, which is what makes a diff between the two ends
	// readable at all.
	Loadout.Sort([](const FSarkoItemStack& A, const FSarkoItemStack& B)
	{
		return A.Item.LexicalLess(B.Item);
	});
	return Loadout;
}

bool SarkoEquip::HasWeapon(const FSarkoEquipment& Equipment)
{
	return !Equipment.Weapon.IsNone();
}
