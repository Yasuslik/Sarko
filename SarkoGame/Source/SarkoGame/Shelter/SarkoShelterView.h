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
struct FSarkoShelterView
{
	/** "УКРИТТЯ". */
	FString Title;

	/** "ВИНЕСЕНО" / "ЗАГИНУВ" / "ЗНИК БЕЗВІСТИ", or empty before the first raid. Task 5. */
	FString OutcomeTitle;

	/** The haul, one line per stack. Task 5. */
	TArray<FString> HaulLines;

	/** "ГАРАЖ: ВЕЛОСИПЕД 1/3", or "ГАРАЖ: ВЕЛОСИПЕД —/3" while the profile is
	 *  unknown — the count comes out of the stash, so it is exactly as unknown as
	 *  the stash is. */
	FString GarageLine;

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
	 * "ГАРАЖ: ВЕЛОСИПЕД n/3", counting recipe *entries* whose full required
	 * quantity is in the stash — one wheel of two does not count, because the
	 * craft call would refuse and a shelter that disagrees with the backend is
	 * worse than one that says less.
	 *
	 * Past TierNone it reports **the bicycle** as built, whatever the tier is:
	 * the ladder is cumulative (domain.UnlockedMaps walks tierOrder, and this
	 * stage's ladder is `none → bicycle → …`), so any tier above none owns one,
	 * and the bicycle is the only recipe this file mirrors.
	 *
	 * Only ever called with a profile that was actually fetched — see
	 * UnknownGarageLine and the gate in BuildView.
	 */
	FString BuildGarageLine(const FSarkoProfile& Profile);

	/**
	 * The garage row while the profile is unknown: "ГАРАЖ: ВЕЛОСИПЕД —/3".
	 *
	 * An unfetched profile carries an empty stash and an empty tier, which
	 * BuildGarageLine would happily read as a truthful "0/3" — told to the player
	 * as fact underneath a "З'ЄДНАННЯ..." status, and worse after a raid, where
	 * RecordRaidOutcome clears bProfileLoaded but keeps CachedProfile: the player
	 * who just extracted the third part would be shown yesterday's 2/3, and a
	 * failed re-fetch would leave it wrong for the whole visit. The row is kept
	 * (rather than dropped like the stash lines) because its shape is what tells
	 * the player the garage exists at all; only the number is withheld.
	 */
	FString UnknownGarageLine();

	/** Assembles the whole screen. Pure. */
	FSarkoShelterView BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
		bool bProfileLoaded, const FString& Error, const FSarkoItemCatalog& Catalog);
}
