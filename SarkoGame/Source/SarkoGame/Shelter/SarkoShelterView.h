#pragma once

#include "CoreMinimal.h"

#include "Core/SarkoGameInstance.h"
#include "Loot/SarkoEquipment.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"

/**
 * Which screen of the hub is showing (spec §1).
 *
 * The shelter is three destinations plus the raid now, not one panel that grew a
 * garage block in its corner. They are PEERS: switching is instant, nothing is
 * modal, and each screen is built by the same BuildView call with a different
 * value here — so a screen cannot accumulate state the others do not see.
 */
enum class ESarkoShelterScreen : uint8
{
	Inventory,
	Garage,
	Shop
};

/**
 * One button in the left-edge destination column.
 *
 * Left-EDGE and not middle-top, because a phone held in landscape puts both
 * thumbs on the edges (spec §1). Each is at least 44 pt tall, which is the
 * project's touch rule, and the current one is marked — a nav column where you
 * cannot tell where you are is a nav column that has to be re-read every time.
 */
struct FSarkoShelterDestination
{
	ESarkoShelterScreen Screen = ESarkoShelterScreen::Inventory;

	/** "ІНВЕНТАР" / "ГАРАЖ" / "МАГАЗИН". */
	FString Label;

	/** This is the screen currently drawn. */
	bool bCurrent = false;

	/**
	 * False for the shop, which is still a stub — and it is a DISABLED PEER rather
	 * than an absent one, because spec §1 wants the shape of the shelter to be
	 * right before the shop is: a player who cannot see that a shop is coming
	 * cannot form the expectation the shop will eventually satisfy.
	 */
	bool bEnabled = true;
};

/**
 * One equipment slot as the character panel draws it.
 *
 * The panel decides NOTHING: the caption, the rectangle and whether there is
 * anything in it are all settled here, so "does the weapon slot read as the
 * weapon slot" is a property of a pure function and a frame rather than of Slate
 * code that cannot be constructed under -nullrhi.
 */
struct FSarkoEquipSlotView
{
	ESarkoEquipSlot Slot = ESarkoEquipSlot::None;

	/** "ЗБРОЯ" / "ОДЯГ" / "РЮКЗАК". The half of the drawing that makes the crude
	 *  figure legible, which spec §6 names as the one thing it may not fail at. */
	FString Caption;

	/** What is in it. Quantity 0 means empty — equipment is never a stack of more
	 *  than one, so the quantity is a flag as much as a count. */
	FSarkoItemStack Stack;

	/** The rectangle to draw, in cells: the ITEM's own rect when something is worn
	 *  (a pistol is 2x1), and the slot's largest acceptable rect when it is empty
	 *  (the weapon slot is 3x1, a rifle's). The space and the shape are different
	 *  facts and the panel shows whichever one is true. */
	FIntPoint Extent = FIntPoint(1, 1);

	bool bOccupied = false;
};

/**
 * The character on the left of ІНВЕНТАР (spec §2).
 *
 * Pockets is in here as a grid size and a caption rather than as a slot: it is
 * the 2x2 carry page, shown inline because it is "always present, always yours",
 * and it is not equipped INTO — pre-loading the carry grid from the shelter is a
 * feature spec §5 does not add.
 */
struct FSarkoCharacterView
{
	/** "ХОДОК" — the section heading over the figure. */
	FString Title;

	/** In SarkoEquip::Slots() order, always all three, occupied or not. An absent
	 *  slot would make the body change shape as gear came and went. */
	TArray<FSarkoEquipSlotView> Slots;

	/** "КИШЕНІ", and the 2x2 grid under it. */
	FString PocketsCaption;
	FIntPoint PocketsGrid = FIntPoint(2, 2);

	/** "ПОРОЖНІ ДО РЕЙДУ" — why the pockets are empty on this screen. Without it
	 *  an empty 2x2 beside a full stash reads as a bug rather than as the truth,
	 *  which is that what you carry is packed in the raid and not here. */
	FString PocketsNote;
};

/**
 * The raid button, as words rather than as a state the widget has to interpret.
 *
 * It carries its own LABEL because of spec §4's dead-end guard: a player with no
 * weapon must still be able to raid, and the button says "БЕЗ ЗБРОЇ" instead of
 * going grey. That is the difference between a game that is hard and a game that
 * is over — a new player who dies with their only pistol equipped has nothing to
 * equip, and a disabled button would be the end of their save.
 */
struct FSarkoRaidButtonView
{
	/** "В РЕЙД", always and unchanged. The verb must not move or change wording:
	 *  it is the one control a player looks for without reading. */
	FString Label;

	/** "БЕЗ ЗБРОЇ" on a second, smaller line when the weapon slot is empty, and
	 *  empty otherwise. Two fields rather than one string with a newline in it,
	 *  because the two lines are drawn at different sizes — "БЕЗ ЗБРОЇ" at the
	 *  verb's own size does not fit the destination column's width. */
	FString SubLabel;

	/** False ONLY while the very first profile fetch is in flight. Never false for
	 *  want of a weapon — see the struct comment. */
	bool bEnabled = false;

	/** Nothing in the weapon slot. Colours the label; decides nothing else. */
	bool bUnarmed = false;

	/**
	 * THE SEAM FOR ВИЛАЗКА (spec §4.5), which is the next task and is deliberately
	 * not built here.
	 *
	 * The sortie is a SECOND button beside В РЕЙД, showing either "ВИЛАЗКА" or the
	 * cooldown remaining — so it belongs in this struct, as a second label and a
	 * second enabled flag, and the destination column already has the room for it
	 * (the raid button sits alone at the foot of a column with slack above it).
	 *
	 * Everything it implies is the SERVER's: free entry, the granted kit, and the
	 * cooldown are decided and enforced by sarko-api, and the client only displays
	 * the remaining time. So the shape this will take is a mode parameter on
	 * /v1/raid/start plus two more strings here — and NOT a client-side timer, and
	 * not a client-chosen kit. Nothing in this file should acquire the ability to
	 * decide either.
	 */
};

/** One rung of the garage's vehicle ladder (spec §3). */
struct FSarkoVehicleRung
{
	/** "ВЕЛОСИПЕД — SWAMP": the vehicle and the sector it opens. */
	FString Text;

	/** At or below the player's tier, i.e. owned. */
	bool bBuilt = false;

	/** The one the recipe above is for. Exactly one rung has this, unless the
	 *  ladder is finished. */
	bool bNext = false;
};

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
/**
 * One recipe entry as the screen says it: the sentence, and whether it is done.
 *
 * The flag is not derivable from the sentence any more, and that is the point.
 * The line used to print the raw held quantity, so a stash with three chains in
 * it read "Ланцюг  3/1" — which is not a surplus to a player, it is arithmetic
 * that has gone wrong, and a screen that looks like it is miscounting is a screen
 * you stop trusting. It now prints the requirement as MET (1/1) and carries
 * bMet so the widget can colour it, which is what makes "satisfied" a state
 * rather than a number the reader has to compare.
 */
struct FSarkoGaragePart
{
	/** "Ланцюг  1/1" — have CLAMPED to need, never above it. */
	FString Text;

	/** The full required quantity is in the stash. Green, in the widget. */
	bool bMet = false;
};

struct FSarkoGarageView
{
	/** "ГАРАЖ: ВЕЛОСИПЕД 2/3", or "…—/3" while the profile is unknown. */
	FString Title;

	/** One line per recipe entry, in the recipe's order. */
	TArray<FSarkoGaragePart> PartLines;

	/** True only when every part's full quantity is in the stash AND the profile
	 *  was actually fetched. A craft offered against an unknown stash is a 409
	 *  waiting to happen. */
	bool bCanCraft = false;

	/** "ЗІБРАТИ ВЕЛОСИПЕД", or "НЕ ВИСТАЧАЄ: Мале колесо", or "ВЕЛОСИПЕД ГОТОВИЙ". */
	FString CraftLabel;

	/** The tier is past none, so there is nothing left to press. */
	bool bBuilt = false;

	/**
	 * The whole ladder — what is built and what is next (spec §3).
	 *
	 * It exists because the garage has a screen of its own now: the cramped corner
	 * it used to live in had room for a recipe and one button, so the only thing a
	 * player could see was the step they were on, and a progression whose shape is
	 * invisible is not felt as a progression.
	 */
	TArray<FSarkoVehicleRung> Ladder;
};

struct FSarkoShelterView
{
	/** "УКРИТТЯ". */
	FString Title;

	/** Which screen is drawn. The widget shows one of three peers. */
	ESarkoShelterScreen Screen = ESarkoShelterScreen::Inventory;

	/** The left-edge destination column, always all three in a fixed order: a nav
	 *  column whose entries move is a nav column you have to read. */
	TArray<FSarkoShelterDestination> Destinations;

	/** The character and its slots. Drawn on ІНВЕНТАР; built unconditionally,
	 *  because "what am I wearing" is not a fact about which screen is open. */
	FSarkoCharacterView Character;

	/** The raid button's label and enabled state. Visible on EVERY screen — it is
	 *  the verb the whole shelter serves (spec §1). */
	FSarkoRaidButtonView Raid;

	/** "ВИНЕСЕНО" / "ЗАГИНУВ" / "ЗНИК БЕЗВІСТИ", or empty before the first raid. Task 5. */
	FString OutcomeTitle;

	/** The haul, one line per stack. Task 5. */
	TArray<FString> HaulLines;

	/**
	 * "ЩЕ НЕ БУЛО РЕЙДІВ" before the first raid of the session, otherwise empty.
	 *
	 * The exact counterpart of StashNote, and it exists for a layout reason that is
	 * also an honesty one: with no summary the whole upper-left of the landscape
	 * screen was blank, so the garage block and the buttons sank to the bottom and
	 * the screen read half-drawn. A heading with a sentence under it saying there
	 * is nothing yet occupies the same region and *explains* it, which is what
	 * "composed" means here. It is a NOTE and not a haul line, so nothing can
	 * mistake it for something that was carried.
	 */
	FString HaulNote;

	/** The garage block. Replaces the old one-line GarageLine. */
	FSarkoGarageView Garage;

	/** "ВІДКРИТО: SWAMP" for the rest of this shelter visit, or empty. The payoff
	 *  sentence for every raid before it (spec §3). */
	FString CraftLine;

	/**
	 * The stash, as stacks rather than strings, already sorted for the grid.
	 * **Empty — not a message** — while the profile has not been fetched: an
	 * unfetched profile carries an empty stash, and telling a player their haul
	 * vanished is the single worst thing this screen can do.
	 */
	TArray<FSarkoItemStack> StashStacks;

	/** "СХОВОК ПОРОЖНІЙ" for a fetched-but-empty stash, or empty. Drawn OVER the
	 *  grid, so an empty stash still shows the grid it will fill. */
	FString StashNote;

	/** "З'ЄДНАННЯ..." while fetching, "ОФЛАЙН: <reason>" on failure, or a refused
	 *  equip's reason verbatim — empty when everything is current. */
	FString StatusLine;

	/** False only while the very first profile fetch is still in flight. An
	 *  offline shelter still lets the player raid (spec §4.6).
	 *
	 *  Kept beside Raid.bEnabled, which is the same fact: this is the one the
	 *  existing tests read, and Raid carries the label the button needs. */
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
	 * The stash, sorted by category then name (spec §2). The catalog is passed in
	 * rather than fetched, so this stays pure and testable under -nullrhi.
	 *
	 * Every row survives, including one whose id the catalog does not know: an id
	 * on screen is the visible symptom of items.json drifting from the backend,
	 * and hiding the row instead would hide an item the player actually owns.
	 * Only a row with nothing in it is dropped — it would draw as an empty cell
	 * that is not an empty slot, a hole in the grid the player cannot fill.
	 */
	TArray<FSarkoItemStack> BuildStashStacks(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog);

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

	/**
	 * The vehicle ladder, mirrored from sarko-api/internal/domain/garage.go's
	 * unexported `tierOrder` and `mapsByTier`.
	 *
	 * Mirrored for the same reason BicycleRecipe is, and the comment there is the
	 * whole argument: no endpoint exposes the ladder. /v1/profile returns
	 * `vehicle_tier` and the maps THIS player has unlocked, which is the ladder cut
	 * off at wherever they are — so the rungs above are unknowable from the wire,
	 * and the rungs above are exactly what a "what is next" readout is for. Adding
	 * GET /v1/garage/ladder is the proper fix and is out of this stage's scope.
	 *
	 * The tier ids are wire values and the UA names are presentation. Only the
	 * BICYCLE is craftable from this client today — the later tiers' parts are
	 * deliberately absent from domain.ItemDefs, so nothing can yield them — and the
	 * ladder says so by showing the rungs greyed rather than by hiding them.
	 */
	TArray<FSarkoVehicleRung> VehicleLadder(const FString& CurrentTier);

	/** Maps in After that were not in Before, in After's order. What the shelter
	 *  says the craft just opened. */
	TArray<FString> NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After);

	/**
	 * The three destinations, with Current marked. Pure, and a fixed order.
	 */
	TArray<FSarkoShelterDestination> BuildDestinations(ESarkoShelterScreen Current);

	/**
	 * The character and its slots, from the profile's equipment.
	 *
	 * The catalog decides each occupied slot's rectangle, so an equipped pistol
	 * draws at 2x1 and an equipped rifle would draw at 3x1 without this function
	 * changing. An id the catalog does not know keeps its slot and draws at 1x1:
	 * hiding it would silently unequip something the server says is worn.
	 */
	FSarkoCharacterView BuildCharacterView(const FSarkoEquipment& Equipment,
		const FSarkoItemCatalog& Catalog);

	/**
	 * The raid button. **Never disabled for want of a weapon** — this is spec §4's
	 * dead-end guard, and it is the whole reason this is a function with a test
	 * rather than two lines in the widget.
	 */
	FSarkoRaidButtonView BuildRaidButton(const FSarkoEquipment& Equipment,
		bool bProfileLoaded, const FString& Error);

	/** Assembles the whole screen. Pure. */
	FSarkoShelterView BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
		bool bProfileLoaded, const FString& Error, const FString& CraftLine,
		const FSarkoItemCatalog& Catalog, ESarkoShelterScreen Screen);
}
