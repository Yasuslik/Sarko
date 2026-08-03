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
	 * The stash's grid, in the shelter's ІНВЕНТАР screen.
	 *
	 * SEVEN since 2026-08-03, and this is the third value it has had, so the whole
	 * history is worth having in one place:
	 *
	 *  - it was 7 as a WORKAROUND for the shelter's ~9% scale deviation (the widget
	 *    scaled itself by PointScaleForViewport while SGameLayerManager was already
	 *    scaling the overlay), which sliced the eighth column in half behind the
	 *    scroll bar;
	 *  - it became 8 when that debt was PAID — UiScaleForViewport is
	 *    SarkoUI::OverlayPointScale, the layer manager's factor divides out, and a
	 *    point on this screen is a point;
	 *  - it is 7 again now for a completely different reason, and NOT a return to
	 *    the workaround. The screen gained a left-edge destination column and a
	 *    character panel with equipment slots on it, and both take width from the
	 *    same 724 pt: 112 of navigation + 220 of character + two 14 pt gutters
	 *    leaves 364, against 7*44 + 6*4 = 332 and ~26 pt of scroll bar. Eight
	 *    columns need 380 and would clip.
	 *
	 * The old warning against 7 was that it "leaves a 74 pt strip of nothing down
	 * the right of the grid" — that was true when the column stayed 406 pt wide
	 * whatever the grid did. It does not apply here: the COLUMN was narrowed with
	 * the grid, and SSarkoShelterWidget's static_assert pins the two together, so
	 * neither a wider panel nor a wider grid can silently reintroduce the clip.
	 *
	 * A 3x2 bike frame still fits: seven columns is four more than the widest item
	 * in the game.
	 *
	 * Five rows is what the visible height shows, with a sixth peeking so the grid
	 * visibly scrolls.
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
	 * More than one slot, so its interior is laid out as a PLATE rather than as a
	 * cell: label centred, type scaled, count beside the label.
	 *
	 * A 3x2 frame and a 2x2 wheel were the flaw this splits out. Drawn like a 1x1
	 * — a 7.5 pt label pinned to the top-left corner and a 10 pt number in the
	 * far bottom-right — a 140x92 pt rectangle reads as an UNFILLED PANEL with a
	 * stray tag on it, not as an object: the two marks are 150 pt apart and the
	 * eye never groups them.
	 */
	inline bool IsMultiCell(FIntPoint Size)
	{
		return FMath::Max(1, Size.X) * FMath::Max(1, Size.Y) > 1;
	}

	/**
	 * The label's type size for a cell of this footprint, in points.
	 *
	 * Scaled by the GEOMETRIC MEAN of the rectangle's two sides — literally "with
	 * the rectangle", and the one measure that answers a 2x1 (which gains only
	 * width) differently from a 2x2 (which gains both). 0.17 of that mean is the
	 * factor, and it is not a new number: a 1x1 is 44 pt square and 44 * 0.17 is
	 * 7.48, i.e. exactly the CellLabelPt this screen was already judged at. So the
	 * rule is anchored on the size that works and only ever grows from it.
	 *
	 * BOUNDED at both ends. The floor is CellLabelPt, so nothing this returns is
	 * ever smaller than the size a 1x1 draws — the fix for truncation must not
	 * shrink type anywhere. The ceiling is 15 pt: a 3x2's mean would ask for 19,
	 * and a label that large stops being a label and starts being a headline
	 * competing with the screen's own title at 26.
	 */
	float CellLabelPtFor(FIntPoint Size);

	/** The count's type size, at the same 4:3 ratio to the label it has on a 1x1
	 *  (10 : 7.5) — the count is the thing the eye stops on and it stays the larger
	 *  of the two at every footprint. */
	float CellCountPtFor(FIntPoint Size);

	/**
	 * What is written *inside* a cell: the label, and the count when there is more
	 * than one. No box and no rim, so the container row — whose cells are SButtons
	 * that bring their own FButtonStyle — can share the exact same interior as the
	 * two grids that draw their own borders.
	 *
	 * A 1x1 (the default, and what the container row always is) is unchanged:
	 * label top-left, count bottom-right, at the sizes this screen was judged at.
	 * Anything larger is laid out as a plate — see IsMultiCell.
	 */
	TSharedRef<SWidget> BuildCellContent(const FSarkoItemStack& Stack, FIntPoint Size = FIntPoint(1, 1));

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
