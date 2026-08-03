#include "Shelter/SarkoShelterView.h"

// For SarkoRaid::OutcomeLosesHaul. Reached transitively through the game
// instance header too, but named here because this file depends on it directly.
#include "Core/SarkoRaidGameState.h"
#include "Loot/SarkoItemGrid.h"

TArray<FSarkoItemStack> SarkoShelter::BicycleRecipe()
{
	// Verbatim from garage.go's recipes[TierBicycle]. Quantities included: the
	// readout is wrong without them (two wheels, not one).
	return {
		FSarkoItemStack{ FName(TEXT("bike_frame")), 1 },
		FSarkoItemStack{ FName(TEXT("wheel_small")), 2 },
		FSarkoItemStack{ FName(TEXT("chain")), 1 },
	};
}

FString SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome Outcome, bool bPersisted)
{
	switch (Outcome)
	{
	case ESarkoRaidOutcome::Extracted:
		return bPersisted ? FString(TEXT("ВИНЕСЕНО")) : FString(TEXT("ВИНЕСЕНО — НЕ ЗБЕРЕЖЕНО"));
	case ESarkoRaidOutcome::Died:
		return TEXT("ЗАГИНУВ");
	case ESarkoRaidOutcome::MIA:
		return TEXT("ЗНИК БЕЗВІСТИ");
	default:
		// InProgress is the "no raid yet" value: nothing is drawn.
		return FString();
	}
}

TArray<FString> SarkoShelter::BuildHaulLines(const FSarkoLastRaid& LastRaid, const FSarkoItemCatalog& Catalog)
{
	TArray<FString> Lines;
	if (LastRaid.Outcome == ESarkoRaidOutcome::InProgress)
	{
		return Lines;
	}

	// By outcome, not by "is the array empty". SarkoRaid::OutcomeLosesHaul is the
	// same rule FinishRaid consults before it clears the backpack, so the shelter
	// and the server agree on what a losing raid keeps by construction rather than
	// by both happening to be right.
	if (SarkoRaid::OutcomeLosesHaul(LastRaid.Outcome) || LastRaid.Haul.Num() == 0)
	{
		Lines.Add(TEXT("НІЧОГО НЕ ВИНЕСЕНО"));
		return Lines;
	}

	Lines.Reserve(LastRaid.Haul.Num());
	for (const FSarkoItemStack& Stack : LastRaid.Haul)
	{
		const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
		// The id is the fallback, not the label: an id on screen means items.json
		// and the backend have drifted, and that should be visible.
		Lines.Add(FString::Printf(TEXT("%s  x%d"),
			Def ? *Def->Name : *Stack.Item.ToString(), Stack.Quantity));
	}
	return Lines;
}

TArray<FSarkoItemStack> SarkoShelter::BuildStashStacks(const FSarkoProfile& Profile,
	const FSarkoItemCatalog& Catalog)
{
	TArray<FSarkoItemStack> Stacks = Profile.Stash;
	// Rows with nothing in them would draw as empty cells that are not empty
	// slots — a hole in the grid the player cannot fill.
	Stacks.RemoveAll([](const FSarkoItemStack& Stack) { return Stack.Quantity <= 0; });
	SarkoGrid::SortForStash(Stacks, Catalog);
	return Stacks;
}

FSarkoGarageView SarkoShelter::BuildGarageView(const FSarkoProfile& Profile, bool bProfileLoaded)
{
	const TArray<FSarkoItemStack> Recipe = BicycleRecipe();
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	FSarkoGarageView View;

	if (!bProfileLoaded)
	{
		// An em dash where the count goes. The recipe's entry count is a
		// client-side constant and stays real; only the held-parts number depends
		// on a stash this client has not seen. Stating "0/3" as fact under a
		// "З'ЄДНАННЯ..." status is the bug this branch exists to prevent.
		View.Title = FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД —/%d"), Recipe.Num());
		View.CraftLabel = TEXT("З'ЄДНАННЯ...");
		return View;
	}

	// Compared against the literal "none" rather than an enum, because
	// vehicle_tier is a string on the wire and an unknown future tier must not
	// crash this readout. The ladder is cumulative, so anything past none already
	// owns the bicycle — the only recipe this file mirrors.
	if (!Profile.VehicleTier.IsEmpty() && Profile.VehicleTier != TEXT("none"))
	{
		View.bBuilt = true;
		View.Title = TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ");
		View.CraftLabel = TEXT("ВЕЛОСИПЕД ГОТОВИЙ");
		return View;
	}

	int32 Met = 0;
	FString FirstMissing;
	View.PartLines.Reserve(Recipe.Num());
	for (const FSarkoItemStack& Part : Recipe)
	{
		// Linear over both lists: three entries against a stash of at most a few
		// dozen rows, computed once per profile fetch rather than per frame.
		const FSarkoItemStack* Held = Profile.Stash.FindByPredicate(
			[&Part](const FSarkoItemStack& Stack) { return Stack.Item == Part.Item; });
		const int32 Have = Held ? Held->Quantity : 0;
		const FSarkoItemDef* Def = Catalog.Find(Part.Item);
		// The id is the fallback, not the label: an id on screen means items.json
		// and the backend have drifted, and that should be visible.
		const FString Name = Def ? Def->Name : Part.Item.ToString();

		// CLAMPED, and this is the whole of flaw 3: three chains against a recipe
		// that wants one printed "Ланцюг  3/1", which reads as a bug rather than as
		// a surplus — have/need is a progress bar in words, and a progress bar past
		// 100% is a miscount. The requirement is what the line is about, so a met
		// one says 1/1 and the extra two are simply not this line's business (they
		// are visible in the stash grid on the right, as two more chain cells).
		//
		// The missing case is untouched and still carries both numbers — "Мале
		// колесо  1/2" is the one fact this block exists to deliver, and colour is
		// added to it rather than substituted for it.
		const bool bMet = Have >= Part.Quantity;
		View.PartLines.Add(FSarkoGaragePart{
			FString::Printf(TEXT("%s  %d/%d"), *Name, FMath::Min(Have, Part.Quantity), Part.Quantity),
			bMet });
		if (bMet)
		{
			++Met;
		}
		else if (FirstMissing.IsEmpty())
		{
			FirstMissing = Name;
		}
	}

	View.Title = FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД %d/%d"), Met, Recipe.Num());
	View.bCanCraft = (Met == Recipe.Num());
	// Never a dead button: enabled it says what it builds, disabled it says what
	// is stopping it, and there is no third state where it says nothing.
	View.CraftLabel = View.bCanCraft
		? FString(TEXT("ЗІБРАТИ ВЕЛОСИПЕД"))
		: FString::Printf(TEXT("НЕ ВИСТАЧАЄ: %s"), *FirstMissing);
	return View;
}

TArray<FSarkoVehicleRung> SarkoShelter::VehicleLadder(const FString& CurrentTier)
{
	// Verbatim from garage.go's tierOrder and mapsByTier, minus TierNone — "on
	// foot" is not a rung of a vehicle ladder, and the bridge it unlocks is the
	// sector the player is already standing in.
	struct FRung { const TCHAR* Tier; const TCHAR* Name; const TCHAR* Map; };
	static const FRung Rungs[] = {
		{ TEXT("bicycle"),    TEXT("ВЕЛОСИПЕД"), TEXT("SWAMP") },
		{ TEXT("motorcycle"), TEXT("МОТОЦИКЛ"),  TEXT("MOUNTAINS") },
		{ TEXT("car"),        TEXT("АВТО"),      TEXT("INDUSTRIAL") },
		{ TEXT("helicopter"), TEXT("ВЕРТОЛІТ"),  TEXT("AIRBASE") },
	};

	// Where the player is, as an index into the array above. -1 for "none" and for
	// any tier this build does not know — an unknown FUTURE tier reads as the
	// bottom of the ladder rather than crashing the readout, and the ladder is a
	// readout and not an entitlement, so understating it costs nothing.
	int32 Current = -1;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rungs); ++Index)
	{
		if (CurrentTier == Rungs[Index].Tier)
		{
			Current = Index;
			break;
		}
	}

	TArray<FSarkoVehicleRung> Ladder;
	Ladder.Reserve(UE_ARRAY_COUNT(Rungs));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rungs); ++Index)
	{
		// The ladder is CUMULATIVE (domain.UnlockedMaps walks tierOrder), so
		// everything at or below the current tier is owned.
		Ladder.Add(FSarkoVehicleRung{
			FString::Printf(TEXT("%s — %s"), Rungs[Index].Name, Rungs[Index].Map),
			/*bBuilt*/ Index <= Current,
			/*bNext*/ Index == Current + 1 });
	}
	return Ladder;
}

TArray<FSarkoShelterDestination> SarkoShelter::BuildDestinations(ESarkoShelterScreen Current)
{
	return {
		FSarkoShelterDestination{ ESarkoShelterScreen::Inventory, TEXT("ІНВЕНТАР"),
			Current == ESarkoShelterScreen::Inventory, true },
		FSarkoShelterDestination{ ESarkoShelterScreen::Garage, TEXT("ГАРАЖ"),
			Current == ESarkoShelterScreen::Garage, true },
		// Disabled, and present anyway. Spec §1 wants the shop to be "a peer rather
		// than a greyed button in a corner": it is still greyed, but it is greyed in
		// the place a shop will be, which is what makes the shelter's shape right
		// before its contents are.
		FSarkoShelterDestination{ ESarkoShelterScreen::Shop, TEXT("МАГАЗИН"),
			Current == ESarkoShelterScreen::Shop, false },
	};
}

FSarkoCharacterView SarkoShelter::BuildCharacterView(const FSarkoEquipment& Equipment,
	const FSarkoItemCatalog& Catalog)
{
	FSarkoCharacterView View;
	View.Title = TEXT("ХОДОК");
	View.PocketsCaption = TEXT("КИШЕНІ");
	View.PocketsGrid = FIntPoint(2, 2);
	// Said out loud, because an empty 2x2 beside a full stash otherwise reads as a
	// grid that failed to draw. The pockets are what you carry IN a raid; there is
	// nothing in them in the shelter, and pre-packing them is a feature spec §5
	// deliberately does not add.
	View.PocketsNote = TEXT("ПОРОЖНІ ДО РЕЙДУ");

	View.Slots.Reserve(SarkoEquip::Slots().Num());
	for (ESarkoEquipSlot Slot : SarkoEquip::Slots())
	{
		FSarkoEquipSlotView SlotView;
		SlotView.Slot = Slot;
		SlotView.Caption = SarkoEquip::SlotCaption(Slot);

		const FName Item = SarkoEquip::Get(Equipment, Slot);
		SlotView.bOccupied = !Item.IsNone();
		if (!SlotView.bOccupied)
		{
			// Empty: the slot's own largest rect, so the outline shows the room a
			// weapon has rather than a 1x1 hole that grows when one goes in.
			SlotView.Extent = SarkoEquip::EmptyExtent(Slot);
			View.Slots.Add(SlotView);
			continue;
		}

		SlotView.Stack = FSarkoItemStack{ Item, 1 };
		const FSarkoItemDef* Def = Catalog.Find(Item);
		// 1x1 for an id the catalog does not know, and the slot KEEPS it: the server
		// says this is worn, and quietly showing an empty slot instead would be the
		// screen lying about what the next raid will debit. The cell draws the raw id,
		// which is how catalogue drift stays visible everywhere else on this screen.
		SlotView.Extent = Def ? FIntPoint(Def->Width, Def->Height) : FIntPoint(1, 1);
		View.Slots.Add(SlotView);
	}
	return View;
}

FSarkoRaidButtonView SarkoShelter::BuildRaidButton(const FSarkoEquipment& Equipment,
	bool bProfileLoaded, const FString& Error)
{
	FSarkoRaidButtonView View;
	View.bUnarmed = !SarkoEquip::HasWeapon(Equipment);

	// THE DEAD-END GUARD (spec §4). bEnabled does not consult bUnarmed, and it must
	// never learn to: a new player who dies with their only weapon equipped has
	// nothing to equip, and a button that refused them would be the end of the save
	// rather than a hard moment in it. The map's authored loot includes a pistol and
	// the backend's one-time starter kit stays, so unarmed is a recoverable choice
	// — and the button says which choice it is instead of going grey and saying
	// nothing.
	//
	// It is said on a SECOND LINE rather than by replacing "В РЕЙД": the verb must
	// not move or change wording, because it is the one control on this screen a
	// player looks for without reading it.
	View.Label = TEXT("В РЕЙД");
	View.SubLabel = View.bUnarmed ? FString(TEXT("БЕЗ ЗБРОЇ")) : FString();

	// Disabled only while the first fetch is genuinely still in flight. A FAILED
	// fetch still allows a raid: it degrades to offline on its own, and the game
	// must never hard-lock on the network (spec §4.6).
	View.bEnabled = bProfileLoaded || !Error.IsEmpty();
	return View;
}

TArray<FString> SarkoShelter::NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After)
{
	TArray<FString> New;
	for (const FString& Map : After)
	{
		if (!Before.Contains(Map))
		{
			New.Add(Map);
		}
	}
	return New;
}

FSarkoShelterView SarkoShelter::BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
	bool bProfileLoaded, const FString& Error, const FString& CraftLine,
	const FSarkoItemCatalog& Catalog, ESarkoShelterScreen Screen)
{
	FSarkoShelterView View;
	View.Title = TEXT("УКРИТТЯ");
	View.Screen = Screen;
	View.Destinations = BuildDestinations(Screen);

	// Built on EVERY screen, not only ІНВЕНТАР. What the player is wearing is not a
	// fact about which screen is open, and the raid button — which is on all three
	// — reads the same equipment for its "БЕЗ ЗБРОЇ" label.
	//
	// It is drawn from the profile whether or not the profile has been FETCHED, and
	// that is the opposite of the rule the stash follows below. The reason is that
	// the two unknowns are different: an unfetched profile's empty STASH is
	// indistinguishable from a genuinely empty one, so drawing it would tell a
	// player their haul vanished. An unfetched profile's empty EQUIPMENT draws as
	// three empty slots, which is what the screen would show anyway a moment before
	// the fetch lands, and the raid button's label degrades to "БЕЗ ЗБРОЇ" — the
	// pessimistic direction, and one that grants nothing.
	View.Character = BuildCharacterView(Profile.Equipment, Catalog);
	View.Raid = BuildRaidButton(Profile.Equipment, bProfileLoaded, Error);

	View.OutcomeTitle = BuildOutcomeTitle(LastRaid.Outcome, LastRaid.bPersisted);
	View.HaulLines = BuildHaulLines(LastRaid, Catalog);
	if (LastRaid.Outcome == ESarkoRaidOutcome::InProgress)
	{
		// No raid this session. BuildHaulLines deliberately returns NO lines here
		// (an empty haul line would claim a raid happened and carried nothing), and
		// this is the note that fills the block it leaves empty.
		View.HaulNote = TEXT("ЩЕ НЕ БУЛО РЕЙДІВ");
	}

	// The stash *and* the garage count are drawn only when they are known. An
	// unfetched profile has an empty Stash array, which is indistinguishable from a
	// genuinely empty stash unless this branch exists — and telling a player their
	// haul vanished is the single worst thing this screen can do.
	//
	// The garage count is the same fact read a second way (it counts recipe entries
	// against that same stash), so it is gated by the same flag. Ungated it stated
	// "ВЕЛОСИПЕД 0/3" as fact under a "З'ЄДНАННЯ..." status on first boot, and
	// showed the pre-raid count after a raid — RecordRaidOutcome clears
	// bProfileLoaded but deliberately keeps CachedProfile so the screen can draw
	// immediately, so the player who just extracted the third part read 2/3, and a
	// failed re-fetch left it that way for the whole visit.
	View.Garage = BuildGarageView(Profile, bProfileLoaded);
	// The ladder is a client-side constant read against the tier, so it is drawn
	// even before the profile lands: at worst every rung is "not yet", which is what
	// a new player's ladder honestly looks like.
	View.Garage.Ladder = VehicleLadder(Profile.VehicleTier);
	View.CraftLine = CraftLine;

	if (bProfileLoaded)
	{
		View.StashStacks = BuildStashStacks(Profile, Catalog);
		if (View.StashStacks.Num() == 0)
		{
			View.StashNote = TEXT("СХОВОК ПОРОЖНІЙ");
		}
	}

	if (!Error.IsEmpty())
	{
		// Verbatim, including the endpoint and the HTTP code: this is the player's
		// only view of spec §4.6's loud degradation, and a friendly paraphrase
		// would cost the one piece of information that identifies the fault.
		View.StatusLine = FString::Printf(TEXT("ОФЛАЙН: %s"), *Error);
	}
	else if (!bProfileLoaded)
	{
		View.StatusLine = TEXT("З'ЄДНАННЯ...");
	}

	// The same fact BuildRaidButton settled, kept as its own field because the
	// widget's enabled attribute and the existing tests both read it. One source:
	// two answers here would be two answers on screen.
	View.bRaidEnabled = View.Raid.bEnabled;
	return View;
}
