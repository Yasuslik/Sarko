#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemGrid.h"

class SWidget;
struct FSarkoInventoryStyles;

/**
 * The cell, once, for every grid in the game.
 *
 * Extracted out of SSarkoInventoryPanel rather than reimplemented in the
 * shelter: spec §2 asks the stash to reuse "the container panel's brushes,
 * palette and labels — one visual language for 'things you own'", and two
 * implementations of one look diverge on the first tweak. The panel keeps its
 * animation, its buttons and its refusal signals; only what a cell *is* moved.
 *
 * NO BINARY ASSETS. Every brush comes from FSarkoInventoryStyles, which builds
 * FSlateRoundedBoxBrushes in C++; every font is FCoreStyle's.
 */
namespace SarkoUI
{
	/**
	 * The stash's grid, in the shelter's right column.
	 *
	 * SEVEN and not the eight the layout table computes, and the missing column is
	 * the shelter's known ~9 % scale deviation made visible. SSarkoShelterWidget
	 * scales itself by PointScaleForViewport while SGameLayerManager is already
	 * scaling the overlay by ~1.092, so the two compound: the right column is
	 * 405.9 pt on paper and 405.9 / 1.092 = 371.7 pt on the glass. Eight columns
	 * is 8*44 + 7*4 = 380 pt — nine points too many, and the frame showed the last
	 * column sliced in half behind the scroll bar. Seven is 332 pt, which clears
	 * the scroll bar with room.
	 *
	 * This goes back to eight the day the shelter adopts SarkoUI::OverlayPointScale
	 * like the container panel already has; that change moves every number in this
	 * screen and is deliberately not made in the same commit as the grid.
	 *
	 * Five rows is what ~271 pt of viewport shows, with a sixth peeking so the
	 * grid visibly scrolls.
	 */
	constexpr int32 StashColumns = 7;
	constexpr int32 StashMinRows = 5;

	/** A w x h item is ONE rounded box spanning its cells and the gutters between
	 *  them — 2x1 is 92 pt wide, not two 44 pt boxes with a seam down the middle.
	 *  The seam is what would make a rifle read as two objects. */
	FVector2D CellExtentPt(FIntPoint Size);

	/** A slot's top-left corner within its page, at the 48 pt pitch. */
	FVector2D CellOriginPt(const FSarkoGridSlot& Slot);

	/**
	 * What is written *inside* a cell: the shortened label, and the count when
	 * there is more than one. No box and no rim, so the container row — whose
	 * cells are SButtons that bring their own FButtonStyle — can share the exact
	 * same interior as the two grids that draw their own borders.
	 */
	TSharedRef<SWidget> BuildCellContent(const FSarkoItemStack& Stack);

	/** An occupied cell: category fill, category rim, the shortened label, and the
	 *  count when there is more than one. Not a button — the panel wraps it in
	 *  one where a tap means something, and the stash never does. */
	TSharedRef<SWidget> BuildStackCell(const FSarkoItemStack& Stack,
		const FSarkoInventoryStyles& Styles, FIntPoint Size);

	/** An empty slot: a thinner rim and a body barely above the plate. */
	TSharedRef<SWidget> BuildEmptyCell(const FSarkoInventoryStyles& Styles, FIntPoint Size);

	/**
	 * A last chance to wrap one placed stack's cell before it goes on the canvas,
	 * keyed by its index in the stacks array.
	 *
	 * The container panel needs it and the stash does not: the panel plays a
	 * transfer flash and a grow-into-place scale on whichever cell just received,
	 * and both are per-cell decorations that only it knows about. Passing a hook
	 * rather than moving the animation in here keeps the shared cell a cell.
	 */
	using FStackCellDecorator = TFunction<TSharedRef<SWidget>(int32 StackIndex, TSharedRef<SWidget> Cell)>;

	/**
	 * One whole page, drawn absolutely: an SConstraintCanvas sized to the page,
	 * with every empty cell laid down first and every placed stack over it at its
	 * own rectangle.
	 *
	 * A canvas and not nested boxes, because a 2x1 item spans two columns and a
	 * row of SBoxes cannot express that without the caller tracking which cells a
	 * previous row already claimed — which is the placement logic, written a
	 * second time, in Slate. The layout is already computed; this only draws it.
	 */
	TSharedRef<SWidget> BuildGridPage(const TArray<FSarkoItemStack>& Stacks,
		const TArray<FSarkoGridSlot>& Slots, int32 PageIndex,
		const FSarkoGridPage& Page, const FSarkoInventoryStyles& Styles,
		const FStackCellDecorator& Decorate = FStackCellDecorator());
}
