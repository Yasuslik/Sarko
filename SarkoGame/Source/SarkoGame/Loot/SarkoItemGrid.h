#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoItemGrid.generated.h"

/**
 * One rectangular grid of whole cells. Pockets are 2x2; a worn backpack adds a
 * separate 4x2; the shelter's stash is one wide page that grows.
 */
USTRUCT()
struct FSarkoGridPage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Columns = 0;

	UPROPERTY()
	int32 Rows = 0;

	int32 Cells() const { return FMath::Max(0, Columns) * FMath::Max(0, Rows); }
};

/**
 * Where one stack sits: which page, its top-left cell, and the rectangle it
 * occupies. Page == INDEX_NONE means it could not be placed at all.
 *
 * DERIVED, never stored and never replicated. The authority is the ORDER of the
 * stacks in the array, which is already replicated (USarkoBackpackComponent::
 * Slots) and already the wire shape the backend takes. Placement is a pure
 * function of that order, so the server, the owning client and the panel all
 * compute the same layout without a byte of new traffic — and topping up a
 * stack changes a Quantity, never an index, so nothing on screen ever moves.
 */
USTRUCT()
struct FSarkoGridSlot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Page = INDEX_NONE;

	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;

	UPROPERTY()
	int32 W = 1;

	UPROPERTY()
	int32 H = 1;

	bool IsPlaced() const { return Page != INDEX_NONE; }
};

/**
 * Spatial placement, entirely pure: catalog and arrays in, arrays out. No
 * component, no world, no settings object.
 *
 * That is deliberate and load-bearing. This is the rule that decides whether a
 * haul survives a raid, and it is the rule three screens have to agree about —
 * the raid's panel, the shelter's stash and the HUD's readout — so it is unit
 * tested with no world, no actor and no Slate application, under -nullrhi.
 */
namespace SarkoGrid
{
	/** An item's rectangle. 1x1 for an id the catalog does not know, so an
	 *  unknown id can never claim more room than the smallest thing in the game
	 *  — AddToGrid refuses it outright anyway. */
	FIntPoint SizeOf(const FSarkoItemCatalog& Catalog, FName Item);

	/**
	 * The pages a pawn carries: pockets always, the backpack only while one is
	 * worn. Two pages rather than one growing grid, because the player must be
	 * able to see at a glance what survives losing the bag, and because a bag is
	 * a thing you find, wear and lose as a unit (spec §1.2).
	 *
	 * Non-positive dimensions are clamped away rather than trusted: a page with
	 * a negative column count would make every bounds check below inconsistent,
	 * which is a haul that half-fits.
	 */
	TArray<FSarkoGridPage> CarryPages(bool bBackpackWorn, FIntPoint Pockets, FIntPoint Backpack);

	/** Total cells across every page. */
	int32 TotalCells(const TArray<FSarkoGridPage>& Pages);

	/** Cells these stacks occupy, by area — sum of w*h, not a count of stacks. */
	int32 UsedCells(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog);

	/**
	 * First fit: for each stack in order, scan page 0 then page 1 …, and within
	 * a page scan left to right, top to bottom, taking the first free rectangle
	 * of the right shape (spec §1).
	 *
	 * Index-aligned with Stacks — one FSarkoGridSlot per stack, always, so a
	 * caller can never mis-pair a rectangle with an item. A stack that does not
	 * fit gets Page = INDEX_NONE and is skipped; later, smaller stacks are still
	 * tried, because refusing everything after one oversized item would make a
	 * bike frame in slot 0 empty the whole panel.
	 *
	 * The scan BACKFILLS by construction: a 1x1 arriving after a 2x1 skipped a
	 * single trailing cell lands in that cell, because the scan always starts
	 * from the top-left. Without it an exactly-packed bag strands its last item,
	 * which is precisely the tutorial's case.
	 */
	TArray<FSarkoGridSlot> Place(const TArray<FSarkoItemStack>& Stacks,
		const FSarkoItemCatalog& Catalog, const TArray<FSarkoGridPage>& Pages);

	/**
	 * Adds Quantity of Item, stacking by the catalog's stackSize, and returns how
	 * much did **not** fit.
	 *
	 * Replaces SarkoLoot::AddToBackpack. Partial stacks are topped up before any
	 * new rectangle is opened — a stack occupies one rectangle regardless of
	 * count (spec §1.1), so a top-up is free and must be tried first, or the
	 * grid fills with half-empty stacks and stops meaning anything.
	 *
	 * An unknown item is refused whole: guessing a size or a stack size would put
	 * an id the backend rejects into a raid result.
	 */
	int32 AddToGrid(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
		const TArray<FSarkoGridPage>& Pages, FName Item, int32 Quantity);

	/**
	 * How many units of one id these stacks hold, summed across every stack that
	 * carries it. Pure, and the inverse question to AddToGrid's: "how much of this
	 * is in the bag right now".
	 *
	 * The reserve behind the magazine (spec §1) is this number, so it is read on
	 * the server before a reload moves rounds and on the owning client every frame
	 * the HUD draws — the second is why it is a plain loop over an array the caller
	 * already holds by reference and allocates nothing.
	 */
	int32 CountItem(const TArray<FSarkoItemStack>& Stacks, FName Item);

	/**
	 * Removes up to Quantity units of Item and returns how many actually left —
	 * fewer than asked for when the stacks hold less, zero when they hold none.
	 * The exact inverse of AddToGrid, and the other half of what makes ammo a
	 * consumable rather than a decoration.
	 *
	 * Drains from the LAST matching stack backwards, for two reasons that point the
	 * same way. AddToGrid tops partial stacks up before opening a new rectangle, so
	 * the trailing stack of an id is the least-full one — draining it first keeps
	 * the earlier stacks whole instead of leaving a row of half-empty ones. And a
	 * stack that empties is removed from the array, which shifts every stack after
	 * it: taking from the back shifts the fewest, so the cells the player is looking
	 * at do not slide sideways while they reload.
	 *
	 * Pure — no component, no world — because the reload arithmetic is the rule the
	 * whole scarcity stage rests on and it is unit tested with neither.
	 */
	int32 RemoveFromGrid(TArray<FSarkoItemStack>& Stacks, FName Item, int32 Quantity);

	/**
	 * Category, then display name, then id. The stash's order (spec §2) — the
	 * scarcity is in the raid, not in storage, so the one job this order has is
	 * that the same item is always in the same place.
	 *
	 * StableSort and a total order down to the id, so it is idempotent: sorting an
	 * already-sorted stash must not move anything, or the grid reshuffles on every
	 * redraw and "always in the same place" stops being true.
	 */
	void SortForStash(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog);

	/**
	 * How many rows a grid this wide needs to hold these stacks, never fewer than
	 * MinRows.
	 *
	 * Grown by probing rather than by dividing area by width: a 3x2 frame in an
	 * 8-wide grid can leave cells that nothing fits into, so the honest answer is
	 * the smallest row count for which Place() places everything. Bounded, because
	 * an item wider than the grid would otherwise never place and this would spin.
	 */
	int32 StashRowsFor(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
		int32 Columns, int32 MinRows);

	/**
	 * Where to draw the rectangle that would NOT fit, so the player can see why.
	 *
	 * The free cell with the longest run of free cells to its right, scanning
	 * pages in order and rows top to bottom — i.e. the most convincing gap on
	 * screen, the one the player is already looking at thinking "but there is
	 * space right there". Drawn at the refused Size, the ghost then runs out of
	 * that gap and over the cell that actually blocked it, which is the whole
	 * argument made in one rectangle.
	 *
	 * A completely full grid anchors at page 0's origin rather than returning
	 * nothing: the loudest refusal must not be the one that draws nothing.
	 *
	 * The returned slot carries the REFUSED size, not a size that fits — it is a
	 * ghost, not a placement, and it is expected to overhang an OCCUPIED cell.
	 * Its origin is pulled back so the rectangle stays on the page wherever the
	 * page can hold it at all: a gap in the last column would otherwise anchor a
	 * 2x1 half off the plate, where the missing edge reads as a clipping fault and
	 * the ghost overhangs nothing. The pull-back is at most W-1 columns, so the
	 * gap itself is always still under the ghost.
	 */
	FSarkoGridSlot RefusalAnchor(const TArray<FSarkoGridSlot>& Placed,
		const TArray<FSarkoGridPage>& Pages, FIntPoint Size);
}
