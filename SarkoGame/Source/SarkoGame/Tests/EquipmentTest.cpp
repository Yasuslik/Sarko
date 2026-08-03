#include "Misc/AutomationTest.h"

#include "Loot/SarkoEquipment.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"
#include "Net/SarkoBackendClient.h"
#include "Shelter/SarkoShelterView.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** The shipped catalog. Every rule below is asserted against the real items.json
	 *  rather than a fixture, because the whole question these rules answer is which
	 *  of the game's actual items go where. */
	const FSarkoItemCatalog& EquipCatalog()
	{
		return SarkoLoot::GetItemCatalog();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEquipSlotsAreAuthoredAndAgreeWithCategories,
	"Sarko.Equip.SlotsAreAuthoredAndAgreeWithCategories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEquipSlotsAreAuthoredAndAgreeWithCategories::RunTest(const FString& Parameters)
{
	// `slot` is AUTHORED in items.json and not derived from `category`, and this is
	// the pair that makes that necessary: backpack and jacket are both `gear`, so no
	// derivation can put a bag on the back and a coat on the chest. This test holds
	// the invariant the two fields still owe each other — a weapon slot only ever
	// holds a Weapon, and the two worn-gear slots only ever hold Gear — because the
	// category is what paints the cell, and a cell whose colour disagrees with the
	// slot it is allowed in is the first signal a player reads going wrong.
	const FSarkoItemCatalog& Catalog = EquipCatalog();
	if (!TestTrue(TEXT("the shipped catalog loaded"), Catalog.Items.Num() > 0))
	{
		return false;
	}

	struct FRow { const TCHAR* Id; ESarkoEquipSlot Slot; ESarkoItemCategory Category; };
	static const FRow Table[] = {
		{ TEXT("pistol"),   ESarkoEquipSlot::Weapon,   ESarkoItemCategory::Weapon },
		{ TEXT("backpack"), ESarkoEquipSlot::Backpack, ESarkoItemCategory::Gear },
		{ TEXT("jacket"),   ESarkoEquipSlot::Clothing, ESarkoItemCategory::Gear },
	};

	for (const FRow& Row : Table)
	{
		const FSarkoItemDef* Def = Catalog.Find(FName(Row.Id));
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is in the catalog"), Row.Id), Def))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("'%s' is worn in the right slot"), Row.Id),
			static_cast<int32>(Def->EquipSlot), static_cast<int32>(Row.Slot));
		TestEqual(*FString::Printf(TEXT("'%s' has the category its slot implies"), Row.Id),
			static_cast<int32>(Def->Category), static_cast<int32>(Row.Category));
	}

	// And in the other direction, over the WHOLE catalog: nothing else is equipment,
	// so a new item cannot quietly become equippable by being added — and no
	// equippable item can carry a category that contradicts its slot.
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		const bool bInTable = Def.Id == FName(TEXT("pistol"))
			|| Def.Id == FName(TEXT("backpack"))
			|| Def.Id == FName(TEXT("jacket"));
		if (!bInTable)
		{
			TestEqual(*FString::Printf(TEXT("'%s' is cargo, not equipment"), *Def.Id.ToString()),
				static_cast<int32>(Def.EquipSlot), static_cast<int32>(ESarkoEquipSlot::None));
			continue;
		}
		if (Def.EquipSlot == ESarkoEquipSlot::Weapon)
		{
			TestEqual(TEXT("only a weapon goes in the weapon slot"),
				static_cast<int32>(Def.Category), static_cast<int32>(ESarkoItemCategory::Weapon));
		}
		else
		{
			TestEqual(TEXT("the worn slots take gear"),
				static_cast<int32>(Def.Category), static_cast<int32>(ESarkoItemCategory::Gear));
		}
	}

	// The wire names are the contract with POST /v1/profile/equipment: a rename here
	// is a 400 the client cannot see coming.
	for (ESarkoEquipSlot Slot : SarkoEquip::Slots())
	{
		ESarkoEquipSlot RoundTripped = ESarkoEquipSlot::None;
		TestTrue(*FString::Printf(TEXT("'%s' round-trips"), SarkoEquip::WireName(Slot)),
			SarkoEquip::ParseWireName(SarkoEquip::WireName(Slot), RoundTripped));
		TestEqual(TEXT("and comes back as itself"),
			static_cast<int32>(RoundTripped), static_cast<int32>(Slot));
		// Every slot has a caption, because a rectangle without one is exactly the
		// failure spec §6 names: "not acceptable if the player cannot tell which slot
		// is which".
		TestFalse(*FString::Printf(TEXT("slot '%s' has a caption"), SarkoEquip::WireName(Slot)),
			SarkoEquip::SlotCaption(Slot).IsEmpty());
	}
	ESarkoEquipSlot Unknown = ESarkoEquipSlot::Weapon;
	TestFalse(TEXT("an invented slot name is refused"), SarkoEquip::ParseWireName(TEXT("hat"), Unknown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEquipRefusalsNameTheirReason,
	"Sarko.Equip.RefusalsNameTheirReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEquipRefusalsNameTheirReason::RunTest(const FString& Parameters)
{
	// The refusal discipline, which spec §2 asks to be the container panel's: shake,
	// amber rim, and A NAMED REASON. The first two are motion; this is the third, and
	// it is the one that can be tested without a Slate application.
	const FSarkoItemCatalog& Catalog = EquipCatalog();
	const FSarkoItemDef* Pistol = Catalog.Find(FName(TEXT("pistol")));
	const FSarkoItemDef* Bag = Catalog.Find(FName(TEXT("backpack")));
	const FSarkoItemDef* Coat = Catalog.Find(FName(TEXT("jacket")));
	const FSarkoItemDef* Medkit = Catalog.Find(FName(TEXT("medkit")));
	if (!TestTrue(TEXT("the fixtures are in the catalog"), Pistol && Bag && Coat && Medkit))
	{
		return false;
	}

	FString Reason;

	// Accepted, and an accepted answer says nothing — an empty reason beside a
	// `true` is what stops a success from drawing a refusal note.
	TestTrue(TEXT("a pistol goes in the weapon slot"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Weapon, Pistol, /*bSlotOccupied*/ false, Reason));
	TestTrue(TEXT("and says nothing about it"), Reason.IsEmpty());
	TestTrue(TEXT("a bag goes on the back"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Backpack, Bag, false, Reason));
	TestTrue(TEXT("a coat goes on the chest"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Clothing, Coat, false, Reason));

	// THE WRONG-CATEGORY REFUSAL, and the commonest one: most of a stash is cargo.
	TestFalse(TEXT("a medkit is not equipment"),
		SarkoEquip::Accepts(ESarkoEquipSlot::None, Medkit, false, Reason));
	TestTrue(TEXT("and the refusal says which item"), Reason.Contains(TEXT("АПТ")));
	TestTrue(TEXT("and says why"), Reason.Contains(TEXT("НЕ СНАРЯЖЕННЯ")));

	// The pair the authored `slot` field exists for. Both name where the item DOES
	// go, which turns a refusal into an instruction instead of a hunt.
	TestFalse(TEXT("a bag is not clothing"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Clothing, Bag, false, Reason));
	TestTrue(TEXT("and it says the bag is a bag"), Reason.Contains(TEXT("РЮКЗАК")));
	TestTrue(TEXT("and that the slot is the coat's"), Reason.Contains(TEXT("ОДЯГ")));

	TestFalse(TEXT("a coat is not a bag"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Backpack, Coat, false, Reason));
	TestTrue(TEXT("and it names both"),
		Reason.Contains(TEXT("ОДЯГ")) && Reason.Contains(TEXT("РЮКЗАК")));

	// A FULL slot is a different fact from a wrong category and must not share a
	// sentence: this one is answered by taking off what is worn, and that one by
	// finding another item.
	TestFalse(TEXT("an occupied slot refuses"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Weapon, Pistol, /*bSlotOccupied*/ true, Reason));
	TestTrue(TEXT("and says the slot is taken"), Reason.Contains(TEXT("ЗАЙНЯТО")));
	TestFalse(TEXT("and does not claim the item is wrong"), Reason.Contains(TEXT("НЕ СНАРЯЖЕННЯ")));

	// An id the catalog does not know is drift with the backend, and it is refused
	// by name rather than guessed into a slot — guessing would put an arbitrary id
	// into a loadout the server debits.
	TestFalse(TEXT("an unknown item is refused"),
		SarkoEquip::Accepts(ESarkoEquipSlot::Weapon, nullptr, false, Reason));
	TestFalse(TEXT("and the refusal is not silent"), Reason.IsEmpty());
	TestEqual(TEXT("an unknown item has no home slot"),
		static_cast<int32>(SarkoEquip::SlotFor(nullptr)), static_cast<int32>(ESarkoEquipSlot::None));

	// Nothing above may EVER return a false with an empty reason. That is the whole
	// discipline in one assertion, and it is what a bare bool could not have held.
	for (ESarkoEquipSlot Slot : SarkoEquip::Slots())
	{
		for (const FSarkoItemDef& Def : Catalog.Items)
		{
			for (bool bOccupied : { false, true })
			{
				FString Each;
				if (!SarkoEquip::Accepts(Slot, &Def, bOccupied, Each))
				{
					TestFalse(*FString::Printf(TEXT("refusing '%s' for slot '%s' states a reason"),
						*Def.Id.ToString(), SarkoEquip::WireName(Slot)), Each.IsEmpty());
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEquippedItemsAreTheLoadout,
	"Sarko.Equip.EquippedItemsAreTheLoadout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEquippedItemsAreTheLoadout::RunTest(const FString& Parameters)
{
	// The mapping /v1/raid/start debits (spec §4). It has to agree with
	// sarko-api's domain.EquipmentLoadout stack for stack, or a legitimate raid is
	// refused for insufficient_items over a disagreement about what "equipped" costs.
	const FSarkoItemCatalog& Catalog = EquipCatalog();

	FSarkoEquipment Nothing;
	TestEqual(TEXT("nothing equipped is an empty loadout"),
		SarkoEquip::Loadout(Nothing, Catalog).Num(), 0);

	FSarkoEquipment WeaponOnly;
	WeaponOnly.Weapon = FName(TEXT("pistol"));
	const TArray<FSarkoItemStack> One = SarkoEquip::Loadout(WeaponOnly, Catalog);
	if (TestEqual(TEXT("one slot is one stack"), One.Num(), 1))
	{
		TestEqual(TEXT("of the equipped item"), One[0].Item, FName(TEXT("pistol")));
		// ONE, never the stash's quantity: equipment is worn, and a player with two
		// pistols wears one of them.
		TestEqual(TEXT("and of exactly one"), One[0].Quantity, 1);
	}

	FSarkoEquipment Full;
	Full.Weapon = FName(TEXT("pistol"));
	Full.Backpack = FName(TEXT("backpack"));
	Full.Clothing = FName(TEXT("jacket"));
	const TArray<FSarkoItemStack> All = SarkoEquip::Loadout(Full, Catalog);
	if (TestEqual(TEXT("three slots are three stacks"), All.Num(), 3))
	{
		// Sorted by id, matching domain.MergeStacks on the far side.
		TestEqual(TEXT("sorted by id, as the server merges them"), All[0].Item, FName(TEXT("backpack")));
		TestEqual(TEXT("sorted by id, as the server merges them"), All[1].Item, FName(TEXT("jacket")));
		TestEqual(TEXT("sorted by id, as the server merges them"), All[2].Item, FName(TEXT("pistol")));
	}

	// THE PLAUSIBILITY GATE'S SHAPE, checked from this side too. The backend's
	// domain.FitsCarryGrid places the loadout into the same 2x2 + 4x2 pages the
	// client's own placer uses, and it has only ever seen an empty list — so the one
	// thing that must be true is that a full kit still FITS. The worn backpack is
	// exempt server-side (domain.WornBagID), so what has to place is the jacket's 2x2
	// and the pistol's 2x1: four cells and two, into twelve.
	const TArray<FSarkoGridPage> Pages = SarkoGrid::CarryPages(/*bBackpackWorn*/ true,
		FIntPoint(2, 2), FIntPoint(4, 2));
	const TArray<FSarkoGridSlot> Placed = SarkoGrid::Place(All, Catalog, Pages);
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("loadout stack '%s' places into the carry grid"),
			*All[Index].Item.ToString()), Placed[Index].IsPlaced());
	}

	// Catalogue drift is left out rather than sent: the backend rejects a whole body
	// for one unknown id, and losing the raid to drift is worse than one item short.
	FSarkoEquipment Drifted;
	Drifted.Weapon = FName(TEXT("plasma_rifle"));
	Drifted.Backpack = FName(TEXT("backpack"));
	const TArray<FSarkoItemStack> Survivors = SarkoEquip::Loadout(Drifted, Catalog);
	if (TestEqual(TEXT("an unknown id is dropped and the rest survives"), Survivors.Num(), 1))
	{
		TestEqual(TEXT("the known item is still taken in"), Survivors[0].Item, FName(TEXT("backpack")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheRaidButtonNeverBlocksAnUnarmedPlayer,
	"Sarko.Equip.TheRaidButtonNeverBlocksAnUnarmedPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheRaidButtonNeverBlocksAnUnarmedPlayer::RunTest(const FString& Parameters)
{
	// THE DEAD-END GUARD (spec §4), and it is the reason this task shipped the guard
	// rather than the next one: the loadout is real now, so a new player who dies
	// with their only weapon equipped has nothing to equip — and a raid button that
	// refused them would be the end of the save, not a hard moment in it.
	//
	// The rule is: entering unarmed is ALWAYS allowed, and the button SAYS SO
	// instead of going grey.
	FSarkoEquipment Nothing;
	const FSarkoRaidButtonView Unarmed =
		SarkoShelter::BuildRaidButton(Nothing, /*bProfileLoaded*/ true, FString(), /*SortieCooldownSeconds*/ 0);
	TestTrue(TEXT("an unarmed player can still raid"), Unarmed.bEnabled);
	TestTrue(TEXT("and the button knows it is unarmed"), Unarmed.bUnarmed);
	TestEqual(TEXT("the verb does not change"), Unarmed.Label, FString(TEXT("В РЕЙД")));
	TestEqual(TEXT("and the second line says БЕЗ ЗБРОЇ, plainly"),
		Unarmed.SubLabel, FString(TEXT("БЕЗ ЗБРОЇ")));

	// Armed: the same verb, and nothing extra said.
	FSarkoEquipment Armed;
	Armed.Weapon = FName(TEXT("pistol"));
	const FSarkoRaidButtonView WithGun = SarkoShelter::BuildRaidButton(Armed, true, FString(), 0);
	TestTrue(TEXT("an armed player can raid"), WithGun.bEnabled);
	TestFalse(TEXT("and is not flagged unarmed"), WithGun.bUnarmed);
	TestEqual(TEXT("the verb is unchanged"), WithGun.Label, FString(TEXT("В РЕЙД")));
	TestTrue(TEXT("and there is no second line"), WithGun.SubLabel.IsEmpty());

	// A bag and a coat are not a weapon. This is the case a "do they own anything"
	// test would have passed by accident.
	FSarkoEquipment Dressed;
	Dressed.Backpack = FName(TEXT("backpack"));
	Dressed.Clothing = FName(TEXT("jacket"));
	const FSarkoRaidButtonView Kitted = SarkoShelter::BuildRaidButton(Dressed, true, FString(), 0);
	TestTrue(TEXT("a bag and a coat still let you raid"), Kitted.bEnabled);
	TestTrue(TEXT("and are still БЕЗ ЗБРОЇ"), Kitted.bUnarmed);

	// The ONLY thing that disables the button is a first fetch still in flight, and
	// even a FAILED fetch re-enables it: an offline shelter still raids (spec §4.6).
	// Checked for both equipment states, so "unarmed" cannot creep into the condition.
	for (const FSarkoEquipment& Equipment : { Nothing, Armed })
	{
		TestFalse(TEXT("only an in-flight first fetch disables the button"),
			SarkoShelter::BuildRaidButton(Equipment, /*bProfileLoaded*/ false, FString(), 0).bEnabled);
		TestTrue(TEXT("a failed fetch still allows a raid"),
			SarkoShelter::BuildRaidButton(Equipment, /*bProfileLoaded*/ false,
				TEXT("/v1/profile: HTTP 500"), 0).bEnabled);
	}

	// And through the whole view, which is what the widget actually reads.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	const FSarkoShelterView View = SarkoShelter::BuildView(FSarkoLastRaid(), Profile,
		/*bProfileLoaded*/ true, FString(), FString(), EquipCatalog(),
		ESarkoShelterScreen::Inventory);
	TestTrue(TEXT("the view's raid button is live for an unarmed player"), View.bRaidEnabled);
	TestTrue(TEXT("and the two copies of that fact agree"), View.Raid.bEnabled == View.bRaidEnabled);
	TestEqual(TEXT("and it says БЕЗ ЗБРОЇ"), View.Raid.SubLabel, FString(TEXT("БЕЗ ЗБРОЇ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheHubHasThreeDestinationsAndAKnownCurrentOne,
	"Sarko.Equip.TheHubHasThreeDestinationsAndAKnownCurrentOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheHubHasThreeDestinationsAndAKnownCurrentOne::RunTest(const FString& Parameters)
{
	// Spec §1: three destinations, the current one marked, the shop a disabled PEER
	// rather than an absent one. The order is fixed — a nav column whose entries move
	// is a nav column that has to be re-read every time.
	for (ESarkoShelterScreen Screen : { ESarkoShelterScreen::Inventory,
		ESarkoShelterScreen::Garage, ESarkoShelterScreen::Shop })
	{
		const TArray<FSarkoShelterDestination> Destinations = SarkoShelter::BuildDestinations(Screen);
		if (!TestEqual(TEXT("there are three destinations"), Destinations.Num(), 3))
		{
			continue;
		}
		TestEqual(TEXT("ІНВЕНТАР is first"), Destinations[0].Label, FString(TEXT("ІНВЕНТАР")));
		TestEqual(TEXT("ГАРАЖ is second"), Destinations[1].Label, FString(TEXT("ГАРАЖ")));
		TestEqual(TEXT("МАГАЗИН is third"), Destinations[2].Label, FString(TEXT("МАГАЗИН")));
		TestFalse(TEXT("the shop is still a stub"), Destinations[2].bEnabled);
		TestTrue(TEXT("ІНВЕНТАР is reachable"), Destinations[0].bEnabled);
		TestTrue(TEXT("ГАРАЖ is reachable"), Destinations[1].bEnabled);

		int32 Current = 0;
		for (const FSarkoShelterDestination& Destination : Destinations)
		{
			if (Destination.bCurrent)
			{
				++Current;
				TestEqual(TEXT("the marked one is the one asked for"),
					static_cast<int32>(Destination.Screen), static_cast<int32>(Screen));
			}
		}
		TestEqual(TEXT("exactly one destination is current"), Current, 1);
	}

	// The default is ІНВЕНТАР (spec §1), and the view carries the screen it was asked
	// for so the switcher cannot drift from the marked button.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	const FSarkoShelterView Garage = SarkoShelter::BuildView(FSarkoLastRaid(), Profile, true,
		FString(), FString(), EquipCatalog(), ESarkoShelterScreen::Garage);
	TestEqual(TEXT("the view carries the screen"),
		static_cast<int32>(Garage.Screen), static_cast<int32>(ESarkoShelterScreen::Garage));
	TestTrue(TEXT("and marks the matching destination"), Garage.Destinations[1].bCurrent);
	// The character and the raid button are built on EVERY screen: what the player is
	// wearing is not a fact about which screen is open.
	TestEqual(TEXT("the character is built on the garage screen too"), Garage.Character.Slots.Num(), 3);
	TestFalse(TEXT("and so is the raid button"), Garage.Raid.Label.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheCharacterPanelShowsEverySlotAtItsOwnSize,
	"Sarko.Equip.TheCharacterPanelShowsEverySlotAtItsOwnSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheCharacterPanelShowsEverySlotAtItsOwnSize::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog& Catalog = EquipCatalog();

	// Empty: three slots, all present, each at the LARGEST rect it accepts — so the
	// outline shows the room a weapon has rather than a 1x1 hole that grows when one
	// goes in (spec §2's table: the weapon slot is a rifle's 3x1).
	FSarkoEquipment Nothing;
	const FSarkoCharacterView Bare = SarkoShelter::BuildCharacterView(Nothing, Catalog);
	if (!TestEqual(TEXT("three slots, always"), Bare.Slots.Num(), 3))
	{
		return false;
	}
	TestFalse(TEXT("the figure has a heading"), Bare.Title.IsEmpty());
	TestFalse(TEXT("the pockets are captioned"), Bare.PocketsCaption.IsEmpty());
	TestEqual(TEXT("the pockets are the 2x2 carry page"), Bare.PocketsGrid, FIntPoint(2, 2));
	// Said out loud, because an empty 2x2 beside a full stash otherwise reads as a
	// grid that failed to draw rather than as the truth.
	TestFalse(TEXT("and say why they are empty here"), Bare.PocketsNote.IsEmpty());

	for (const FSarkoEquipSlotView& Slot : Bare.Slots)
	{
		TestFalse(TEXT("every slot is captioned"), Slot.Caption.IsEmpty());
		TestFalse(TEXT("and empty"), Slot.bOccupied);
		TestEqual(TEXT("and drawn at the slot's own largest rect"),
			Slot.Extent, SarkoEquip::EmptyExtent(Slot.Slot));
	}
	TestEqual(TEXT("the empty weapon slot is a rifle's 3x1"), Bare.Slots[0].Extent, FIntPoint(3, 1));

	// Occupied: each slot draws at the ITEM's own rect. A pistol is 2x1, which is
	// narrower than the slot it sits in — the item is the shape and the slot is the
	// space, and the panel shows whichever of the two is true.
	FSarkoEquipment Full;
	Full.Weapon = FName(TEXT("pistol"));
	Full.Backpack = FName(TEXT("backpack"));
	Full.Clothing = FName(TEXT("jacket"));
	const FSarkoCharacterView Kitted = SarkoShelter::BuildCharacterView(Full, Catalog);
	for (const FSarkoEquipSlotView& Slot : Kitted.Slots)
	{
		TestTrue(*FString::Printf(TEXT("slot '%s' is occupied"), SarkoEquip::WireName(Slot.Slot)),
			Slot.bOccupied);
		TestEqual(TEXT("and holds exactly one"), Slot.Stack.Quantity, 1);
		const FSarkoItemDef* Def = Catalog.Find(Slot.Stack.Item);
		if (TestNotNull(TEXT("of a catalog item"), Def))
		{
			TestEqual(TEXT("drawn at the item's own rect"),
				Slot.Extent, FIntPoint(Def->Width, Def->Height));
		}
	}
	TestEqual(TEXT("an equipped pistol is 2x1, not the slot's 3x1"),
		Kitted.Slots[0].Extent, FIntPoint(2, 1));

	// Catalogue drift KEEPS its slot and draws at 1x1. Hiding it would be the screen
	// silently unequipping something the server says is worn — and the next raid would
	// then debit an item the panel never showed.
	FSarkoEquipment Drifted;
	Drifted.Weapon = FName(TEXT("plasma_rifle"));
	const FSarkoCharacterView Odd = SarkoShelter::BuildCharacterView(Drifted, Catalog);
	TestTrue(TEXT("an unknown equipped id still occupies its slot"), Odd.Slots[0].bOccupied);
	TestEqual(TEXT("and is drawn at the smallest rect in the game"),
		Odd.Slots[0].Extent, FIntPoint(1, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheGarageLadderMirrorsTheBackendsTiers,
	"Sarko.Equip.TheGarageLadderMirrorsTheBackendsTiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheGarageLadderMirrorsTheBackendsTiers::RunTest(const FString& Parameters)
{
	// Mirrored from garage.go's tierOrder and mapsByTier, because no endpoint exposes
	// the ladder: /v1/profile returns this player's OWN unlocked maps, i.e. the ladder
	// cut off wherever they are — and the rungs above are exactly what a "what is
	// next" readout is for. The literals below are that file, so a reordering there
	// turns this red rather than quietly making the screen wrong.
	static const TCHAR* Order[] = { TEXT("ВЕЛОСИПЕД"), TEXT("МОТОЦИКЛ"), TEXT("АВТО"), TEXT("ВЕРТОЛІТ") };
	static const TCHAR* Maps[] = { TEXT("SWAMP"), TEXT("MOUNTAINS"), TEXT("INDUSTRIAL"), TEXT("AIRBASE") };

	// On foot: nothing built, the bicycle next.
	const TArray<FSarkoVehicleRung> None = SarkoShelter::VehicleLadder(TEXT("none"));
	if (!TestEqual(TEXT("four rungs"), None.Num(), 4))
	{
		return false;
	}
	for (int32 Index = 0; Index < None.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("rung %d names its vehicle"), Index),
			None[Index].Text.Contains(Order[Index]));
		TestTrue(*FString::Printf(TEXT("rung %d names the sector it opens"), Index),
			None[Index].Text.Contains(Maps[Index]));
		TestFalse(TEXT("nothing is built on foot"), None[Index].bBuilt);
	}
	TestTrue(TEXT("the bicycle is next"), None[0].bNext);
	TestFalse(TEXT("and only the bicycle"), None[1].bNext);

	// The ladder is CUMULATIVE (domain.UnlockedMaps walks tierOrder), so a car owner
	// owns the bicycle and the motorcycle too.
	const TArray<FSarkoVehicleRung> Car = SarkoShelter::VehicleLadder(TEXT("car"));
	TestTrue(TEXT("a car owner has a bicycle"), Car[0].bBuilt);
	TestTrue(TEXT("and a motorcycle"), Car[1].bBuilt);
	TestTrue(TEXT("and a car"), Car[2].bBuilt);
	TestFalse(TEXT("but not a helicopter"), Car[3].bBuilt);
	TestTrue(TEXT("which is what is next"), Car[3].bNext);

	// The top: everything built, nothing next.
	const TArray<FSarkoVehicleRung> Top = SarkoShelter::VehicleLadder(TEXT("helicopter"));
	int32 NextCount = 0;
	for (const FSarkoVehicleRung& Rung : Top)
	{
		TestTrue(TEXT("everything is built at the top"), Rung.bBuilt);
		NextCount += Rung.bNext ? 1 : 0;
	}
	TestEqual(TEXT("and nothing is next"), NextCount, 0);

	// An unknown FUTURE tier reads as the bottom rather than crashing the readout.
	// The ladder is a readout and not an entitlement, so understating it costs
	// nothing and overstating it would show a vehicle the player does not have.
	const TArray<FSarkoVehicleRung> Unknown = SarkoShelter::VehicleLadder(TEXT("submarine"));
	TestEqual(TEXT("an unknown tier still draws the whole ladder"), Unknown.Num(), 4);
	TestFalse(TEXT("and claims nothing is built"), Unknown[0].bBuilt);

	// And it reaches the view, on both the loaded and the unloaded path: the ladder is
	// a client-side constant read against the tier, so it is drawn before the profile
	// lands — at worst every rung is "not yet", which is what a new player's ladder
	// honestly looks like.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("bicycle");
	const FSarkoShelterView View = SarkoShelter::BuildView(FSarkoLastRaid(), Profile, true,
		FString(), FString(), SarkoLoot::GetItemCatalog(), ESarkoShelterScreen::Garage);
	if (TestEqual(TEXT("the view carries the ladder"), View.Garage.Ladder.Num(), 4))
	{
		TestTrue(TEXT("with the bicycle built"), View.Garage.Ladder[0].bBuilt);
		TestTrue(TEXT("and the motorcycle next"), View.Garage.Ladder[1].bNext);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheProfileParsesEquipment,
	"Sarko.Equip.TheProfileParsesEquipment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheProfileParsesEquipment::RunTest(const FString& Parameters)
{
	FSarkoProfile Profile;
	FString Error;

	// The shape store.Profile marshals.
	const FString Full = TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[],)"
		R"("equipment":{"weapon":"pistol","backpack":"backpack","clothing":"jacket"}})");
	if (TestTrue(TEXT("a profile with equipment parses"),
		SarkoBackend::ParseProfileResponse(Full, Profile, Error)))
	{
		TestEqual(TEXT("the weapon is read"), Profile.Equipment.Weapon, FName(TEXT("pistol")));
		TestEqual(TEXT("the bag is read"), Profile.Equipment.Backpack, FName(TEXT("backpack")));
		TestEqual(TEXT("the coat is read"), Profile.Equipment.Clothing, FName(TEXT("jacket")));
	}

	// ABSENT is "wearing nothing", not an error — the same direction
	// tutorial_completed takes, so a backend older than the field degrades to an
	// unarmed player instead of failing the parse and leaving the shelter offline.
	const FString Older = TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[]})");
	if (TestTrue(TEXT("a profile without equipment still parses"),
		SarkoBackend::ParseProfileResponse(Older, Profile, Error)))
	{
		TestFalse(TEXT("and the player wears nothing"), SarkoEquip::HasWeapon(Profile.Equipment));
	}

	// An empty object is the same thing, and it is what a player who just died gets.
	const FString Stripped = TEXT(R"({"player_id":"p","vehicle_tier":"none","equipment":{}})");
	if (TestTrue(TEXT("an empty equipment object parses"),
		SarkoBackend::ParseProfileResponse(Stripped, Profile, Error)))
	{
		TestTrue(TEXT("and every slot is empty"), Profile.Equipment.Weapon.IsNone()
			&& Profile.Equipment.Backpack.IsNone() && Profile.Equipment.Clothing.IsNone());
	}

	// Present-but-not-an-object is a FAULT, because it must not read as "empty".
	const FString Broken = TEXT(R"({"player_id":"p","vehicle_tier":"none","equipment":"pistol"})");
	TestFalse(TEXT("a non-object equipment field fails the parse"),
		SarkoBackend::ParseProfileResponse(Broken, Profile, Error));
	TestFalse(TEXT("and says so"), Error.IsEmpty());

	// A slot this build has no slot for is SKIPPED, not fatal: the server may grow a
	// fourth slot first, and refusing the whole profile over one would take the
	// stash down with it.
	const FString Future = TEXT(R"({"player_id":"p","vehicle_tier":"none",)"
		R"("equipment":{"helmet":"steel_pot","weapon":"pistol"}})");
	if (TestTrue(TEXT("an unknown slot does not fail the parse"),
		SarkoBackend::ParseProfileResponse(Future, Profile, Error)))
	{
		TestEqual(TEXT("and the slots this build knows still arrive"),
			Profile.Equipment.Weapon, FName(TEXT("pistol")));
	}

	// The equip response, which is REQUIRED to carry the object: it is the whole
	// answer to "what am I wearing now", and an empty struct shown after a successful
	// tap would read as the tap having unequipped everything.
	FSarkoEquipment Equipment;
	TestTrue(TEXT("the equip response parses"), SarkoBackend::ParseEquipmentResponse(
		TEXT(R"({"equipment":{"weapon":"pistol"}})"), Equipment, Error));
	TestEqual(TEXT("and carries the slot"), Equipment.Weapon, FName(TEXT("pistol")));
	TestFalse(TEXT("an equip response without the object is a fault"),
		SarkoBackend::ParseEquipmentResponse(TEXT(R"({"ok":true})"), Equipment, Error));
	// A refusal envelope is reported as one, by name, so the shelter can show the
	// server's own reason rather than "something went wrong".
	TestFalse(TEXT("a refusal envelope is not a success"), SarkoBackend::ParseEquipmentResponse(
		TEXT(R"({"error":{"code":"not_equippable","message":"that item is worn in the backpack slot, not clothing"}})"),
		Equipment, Error));
	TestTrue(TEXT("and the reason survives"), Error.Contains(TEXT("not_equippable")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
