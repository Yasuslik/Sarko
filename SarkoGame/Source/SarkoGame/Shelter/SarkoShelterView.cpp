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

		View.PartLines.Add(FString::Printf(TEXT("%s  %d/%d"), *Name, Have, Part.Quantity));
		if (Have >= Part.Quantity)
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
	const FSarkoItemCatalog& Catalog)
{
	FSarkoShelterView View;
	View.Title = TEXT("УКРИТТЯ");

	View.OutcomeTitle = BuildOutcomeTitle(LastRaid.Outcome, LastRaid.bPersisted);
	View.HaulLines = BuildHaulLines(LastRaid, Catalog);

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

	// Disabled only while the first fetch is genuinely still in flight. A failed
	// fetch still allows a raid: the raid degrades to offline on its own and the
	// game must never hard-lock on the network (spec §4.6).
	View.bRaidEnabled = bProfileLoaded || !Error.IsEmpty();
	return View;
}
