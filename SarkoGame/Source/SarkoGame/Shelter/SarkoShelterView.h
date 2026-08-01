#pragma once

#include "CoreMinimal.h"

#include "Core/SarkoGameInstance.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"

/**
 * Everything the shelter shows, as plain strings.
 *
 * The whole menu is decided here and merely *drawn* by SSarkoShelterWidget, for
 * the same reason the map parser is separate from the map spawner: a function
 * taking values and returning values is testable under -nullrhi, where there is
 * no Slate application to build a widget into. It is also what will survive when
 * this Slate menu is eventually rebuilt in UMG — a UMG widget consumes exactly
 * these strings.
 */
/**
 * The garage block: the recipe with have/need per part, and one button whose
 * label is either what it will build or what is stopping it.
 *
 * Its own struct rather than four more loose fields on FSarkoShelterView,
 * because the widget builds it as one unit and because "can this be crafted"
 * and "why not" have to be decided together — the failure mode this replaces is
 * a disabled button with no explanation (spec §3).
 */
struct FSarkoGarageView
{
	/** "ГАРАЖ: ВЕЛОСИПЕД 2/3", or "…—/3" while the profile is unknown. */
	FString Title;

	/** One "<UA name>  n/m" line per recipe entry, in the recipe's order. */
	TArray<FString> PartLines;

	/** True only when every part's full quantity is in the stash AND the profile
	 *  was actually fetched. A craft offered against an unknown stash is a 409
	 *  waiting to happen. */
	bool bCanCraft = false;

	/** "ЗІБРАТИ ВЕЛОСИПЕД", or "НЕ ВИСТАЧАЄ: Мале колесо", or "ВЕЛОСИПЕД ГОТОВИЙ". */
	FString CraftLabel;

	/** The tier is past none, so there is nothing left to press. */
	bool bBuilt = false;
};

struct FSarkoShelterView
{
	/** "УКРИТТЯ". */
	FString Title;

	/** "ВИНЕСЕНО" / "ЗАГИНУВ" / "ЗНИК БЕЗВІСТИ", or empty before the first raid. Task 5. */
	FString OutcomeTitle;

	/** The haul, one line per stack. Task 5. */
	TArray<FString> HaulLines;

	/** The garage block. Replaces the old one-line GarageLine. */
	FSarkoGarageView Garage;

	/** "ВІДКРИТО: SWAMP" for the rest of this shelter visit, or empty. The payoff
	 *  sentence for every raid before it (spec §3). */
	FString CraftLine;

	/** One line per stash row, or a single "СХОВОК ПОРОЖНІЙ". **Empty** — not the
	 *  porozhniy line — while the profile has not been fetched. */
	TArray<FString> StashLines;

	/** "З'ЄДНАННЯ..." while fetching, "ОФЛАЙН: <reason>" on failure, empty when
	 *  everything is current. */
	FString StatusLine;

	/** False only while the very first profile fetch is still in flight. An
	 *  offline shelter still lets the player raid (spec §4.6). */
	bool bRaidEnabled = false;
};

namespace SarkoShelter
{
	/**
	 * The bicycle recipe, mirrored from sarko-api/internal/domain/garage.go's
	 * unexported `recipes[TierBicycle]`.
	 *
	 * Mirrored because no endpoint exposes a recipe: /v1/profile returns
	 * `vehicle_tier` and `stash`, and /v1/garage/craft only accepts or refuses.
	 * The three ids are already pinned to garage.go by the existing
	 * Sarko.Loot.RealItemCatalogIsUsable test, so a rename on the backend turns a
	 * test red rather than quietly making this readout wrong. Adding
	 * GET /v1/garage/recipe is the proper fix and is out of this stage's scope.
	 */
	TArray<FSarkoItemStack> BicycleRecipe();

	/**
	 * The banner for the raid that just ended, or empty before the first raid.
	 *
	 * MIA is named separately from death even though spec §4.5 makes them the
	 * same *mechanically* (both lose the haul, both submit as `died`): the player
	 * needs to know whether they were shot or ran out of clock, because those are
	 * different mistakes.
	 *
	 * An extraction that was not persisted is labelled, so a haul shown above a
	 * stash that does not contain it is explained rather than mysterious. A lost
	 * haul had nothing to persist, so it carries no such label.
	 */
	FString BuildOutcomeTitle(ESarkoRaidOutcome Outcome, bool bPersisted);

	/**
	 * The haul, one "<UA name>  x<qty>" line per stack — the list that spec §6.5
	 * moves out of the raid HUD and into the shelter.
	 *
	 * Empty (no block at all) before the first raid. Exactly one line, "НІЧОГО НЕ
	 * ВИНЕСЕНО", for every losing outcome **and** for an extraction that carried
	 * nothing. Losing outcomes are refused by outcome rather than by an empty
	 * array, so a haul that reached this struct by some future path still cannot
	 * be shown as banked.
	 */
	TArray<FString> BuildHaulLines(const FSarkoLastRaid& LastRaid, const FSarkoItemCatalog& Catalog);

	/**
	 * One "<UA name>  x<qty>" line per stash row, in the server's order, with the
	 * item id as the fallback label for an id the catalog does not know — an id on
	 * screen is the visible symptom of items.json drifting from the backend, and
	 * hiding the row instead would hide items the player actually owns.
	 *
	 * A fetched-but-empty stash yields exactly one line, "СХОВОК ПОРОЖНІЙ".
	 */
	TArray<FString> BuildStashLines(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog);

	/**
	 * The garage block. Pure: a profile in, strings and one bool out.
	 *
	 * Counts recipe *entries* whose full required quantity is in the stash — one
	 * wheel of two does not count, because the craft call would refuse and a
	 * shelter that disagrees with the backend is worse than one that says less.
	 *
	 * Past TierNone it reports **the bicycle** as built, whatever the tier is:
	 * the ladder is cumulative (domain.UnlockedMaps walks tierOrder, and this
	 * stage's ladder is `none → bicycle → …`), so any tier above none owns one,
	 * and the bicycle is the only recipe this file mirrors.
	 *
	 * bProfileLoaded false is NOT the same as an empty stash: an unfetched
	 * profile would otherwise read as a truthful "0/3" told to the player as fact
	 * under a "З'ЄДНАННЯ..." status — and worse after a raid, where
	 * RecordRaidOutcome clears bProfileLoaded but keeps CachedProfile, so the
	 * player who just extracted the third part would be shown yesterday's 2/3.
	 * The row keeps its shape (that is what says the garage exists at all); only
	 * the number is withheld, and the button is refused with it.
	 */
	FSarkoGarageView BuildGarageView(const FSarkoProfile& Profile, bool bProfileLoaded);

	/** Maps in After that were not in Before, in After's order. What the shelter
	 *  says the craft just opened. */
	TArray<FString> NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After);

	/** Assembles the whole screen. Pure. */
	FSarkoShelterView BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
		bool bProfileLoaded, const FString& Error, const FString& CraftLine,
		const FSarkoItemCatalog& Catalog);
}
