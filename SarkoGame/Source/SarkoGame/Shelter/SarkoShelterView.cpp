#include "Shelter/SarkoShelterView.h"

// For SarkoRaid::OutcomeLosesHaul. Reached transitively through the game
// instance header too, but named here because this file depends on it directly.
#include "Core/SarkoRaidGameState.h"

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

TArray<FString> SarkoShelter::BuildStashLines(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog)
{
	TArray<FString> Lines;
	if (Profile.Stash.Num() == 0)
	{
		Lines.Add(TEXT("СХОВОК ПОРОЖНІЙ"));
		return Lines;
	}

	Lines.Reserve(Profile.Stash.Num());
	for (const FSarkoItemStack& Stack : Profile.Stash)
	{
		const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
		Lines.Add(FString::Printf(TEXT("%s  x%d"),
			Def ? *Def->Name : *Stack.Item.ToString(), Stack.Quantity));
	}
	return Lines;
}

FString SarkoShelter::BuildGarageLine(const FSarkoProfile& Profile)
{
	// Anything past the starting tier already owns the bicycle: the garage ladder
	// is cumulative (domain.UnlockedMaps walks tierOrder), so "not none" means
	// built. Compared against the literal "none" rather than an enum because
	// vehicle_tier is a string on the wire and an unknown future tier must not
	// crash this readout.
	if (!Profile.VehicleTier.IsEmpty() && Profile.VehicleTier != TEXT("none"))
	{
		return TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ");
	}

	const TArray<FSarkoItemStack> Recipe = BicycleRecipe();
	int32 Met = 0;
	for (const FSarkoItemStack& Part : Recipe)
	{
		// Linear over both lists: three entries against a stash of at most a few
		// dozen rows, computed once per profile fetch rather than per frame.
		const FSarkoItemStack* Held = Profile.Stash.FindByPredicate(
			[&Part](const FSarkoItemStack& Stack) { return Stack.Item == Part.Item; });
		if (Held && Held->Quantity >= Part.Quantity)
		{
			++Met;
		}
	}
	return FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД %d/%d"), Met, Recipe.Num());
}

FSarkoShelterView SarkoShelter::BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
	bool bProfileLoaded, const FString& Error, const FSarkoItemCatalog& Catalog)
{
	FSarkoShelterView View;
	View.Title = TEXT("УКРИТТЯ");

	View.OutcomeTitle = BuildOutcomeTitle(LastRaid.Outcome, LastRaid.bPersisted);
	View.HaulLines = BuildHaulLines(LastRaid, Catalog);

	View.GarageLine = BuildGarageLine(Profile);

	// The stash is drawn only when it is known. An unfetched profile has an empty
	// Stash array, which is indistinguishable from a genuinely empty stash unless
	// this branch exists — and telling a player their haul vanished is the single
	// worst thing this screen can do.
	if (bProfileLoaded)
	{
		View.StashLines = BuildStashLines(Profile, Catalog);
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
