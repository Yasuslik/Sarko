#pragma once

#include "CoreMinimal.h"

#include "SarkoItemCatalog.generated.h"

/**
 * What an item is for. Beyond spec §4.1's {id, name, stackSize}, and needed:
 * ТЗ §30 forbids the `med` loot tier from yielding weapons or vehicle parts,
 * and without a category on the item that rule cannot be checked by anything
 * except a human reading the table.
 */
UENUM()
enum class ESarkoItemCategory : uint8
{
	Weapon,
	Ammo,
	Med,
	Junk,
	Valuable,
	VehiclePart,
	/**
	 * Worn equipment. Today that is exactly one thing — the backpack — and it is
	 * its own category rather than `junk` because the panel paints a cell by its
	 * category and this is the one item whose colour has to say "this is not
	 * cargo, this is what carries the cargo". Appended last on purpose: this is a
	 * uint8 UENUM and inserting in the middle renumbers every value above it.
	 */
	Gear,

	/**
	 * Something you USE, in the raid, by tapping it in your own grid.
	 *
	 * Its own category and not `med` or `valuable`, which is the question spec §4
	 * asks and this is the answer. `med` is what a wound needs and is answered
	 * with the medkit; `valuable` is what the stash wants and is answered by
	 * carrying it home. Food and water are neither: they are the only items in
	 * the game whose value is spent where you stand, and the panel has to say so
	 * — the tappable cells in the player's own grid are exactly this category
	 * and nothing else (SSarkoInventoryPanel::DecorateCarryCell), so "which of my
	 * things can I use" is a colour rather than a memory test.
	 *
	 * Appended last, for the reason Gear's comment gives: this is a uint8 UENUM
	 * and inserting in the middle renumbers every value above it.
	 */
	Consumable
};

/**
 * A quantity of one item id. The unit of loot, of the backpack, and of the
 * backend's `{"item_id","quantity"}` wire shape.
 */
USTRUCT()
struct FSarkoItemStack
{
	GENERATED_BODY()

	UPROPERTY()
	FName Item;

	UPROPERTY()
	int32 Quantity = 0;
};

/** One catalog entry. Presentation plus the two rules loot needs: stacking and category. */
USTRUCT()
struct FSarkoItemDef
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	/** Ukrainian display name. The HUD draws this, never the id. */
	UPROPERTY()
	FString Name;

	/** How many of this item share one backpack slot. Always >= 1. */
	UPROPERTY()
	int32 StackSize = 1;

	/**
	 * The rectangle this item occupies, in whole cells. No rotation: every item
	 * is authored in the orientation it occupies (spec §1).
	 *
	 * Authored in items.json and REQUIRED there, not defaulted — spec §5 names
	 * careless sizing as the way the "a rifle needs a backpack" rule quietly
	 * stops holding, and a field that may be omitted is a field that will be.
	 * Sarko.Loot.ItemSizesMatchTheDesignTable is the second half of that guard.
	 */
	UPROPERTY()
	int32 Width = 1;

	UPROPERTY()
	int32 Height = 1;

	UPROPERTY()
	ESarkoItemCategory Category = ESarkoItemCategory::Junk;
};

/**
 * Every item in the game.
 *
 * The backend is the source of truth for item *ids* — they are free-form TEXT
 * in `stash_items`, and `sarko-api/internal/domain/garage.go` fixes the vehicle
 * part ids by naming them in its recipes. This catalog is presentation plus the
 * client-side rules, and Task 2 makes the backend reject any id that is not in
 * it, so the two can only drift with a test going red.
 */
USTRUCT()
struct FSarkoItemCatalog
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoItemDef> Items;

	/** Null for an unknown id — never a default item. A default would put an
	 *  invented id into a raid result and get the whole haul rejected. */
	const FSarkoItemDef* Find(FName Id) const;
};

namespace SarkoLoot
{
	/**
	 * Parses the catalog. Pure: text in, catalog out, no disk and no world,
	 * which is what lets the schema be tested from string literals.
	 *
	 * Every failure sets OutError to something that names the problem, for the
	 * same reason the map parser does: this file is hand-edited, and a silently
	 * half-loaded catalog surfaces as a rejected raid result fifteen minutes
	 * later instead of as an error at load.
	 */
	bool ParseItemCatalog(const FString& Json, FSarkoItemCatalog& OutCatalog, FString& OutError);

	/** Reads Data/Items/items.json from the project directory. */
	bool LoadItemCatalogFromDisk(FSarkoItemCatalog& OutCatalog, FString& OutError);

	/**
	 * The process-wide catalog, loaded on first use. Logs Error and returns an
	 * empty catalog if the file is broken; callers then find nothing, which is
	 * the safe direction — no loot rather than invented loot.
	 */
	const FSarkoItemCatalog& GetItemCatalog();
}
