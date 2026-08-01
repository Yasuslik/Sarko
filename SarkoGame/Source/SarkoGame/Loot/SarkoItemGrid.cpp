#include "Loot/SarkoItemGrid.h"

FIntPoint SarkoGrid::SizeOf(const FSarkoItemCatalog& Catalog, FName Item)
{
	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		return FIntPoint(1, 1);
	}
	return FIntPoint(FMath::Max(1, Def->Width), FMath::Max(1, Def->Height));
}

TArray<FSarkoGridPage> SarkoGrid::CarryPages(bool bBackpackWorn, FIntPoint Pockets, FIntPoint Backpack)
{
	TArray<FSarkoGridPage> Pages;
	Pages.Add(FSarkoGridPage{ FMath::Max(0, Pockets.X), FMath::Max(0, Pockets.Y) });
	if (bBackpackWorn)
	{
		Pages.Add(FSarkoGridPage{ FMath::Max(0, Backpack.X), FMath::Max(0, Backpack.Y) });
	}
	return Pages;
}

int32 SarkoGrid::TotalCells(const TArray<FSarkoGridPage>& Pages)
{
	int32 Total = 0;
	for (const FSarkoGridPage& Page : Pages)
	{
		Total += Page.Cells();
	}
	return Total;
}

int32 SarkoGrid::UsedCells(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog)
{
	int32 Used = 0;
	for (const FSarkoItemStack& Stack : Stacks)
	{
		const FIntPoint Size = SizeOf(Catalog, Stack.Item);
		Used += Size.X * Size.Y;
	}
	return Used;
}

namespace
{
	/**
	 * The occupancy of every page, as one flat bit array per page.
	 *
	 * A TBitArray and not a TSet of coordinates: the grids in this game are at
	 * most a few hundred cells, this is walked on every take and on every panel
	 * rebuild, and a linear scan over bits is both faster and much easier to
	 * reason about than a hash lookup per candidate cell.
	 */
	struct FOccupancy
	{
		TArray<TBitArray<>> Pages;

		explicit FOccupancy(const TArray<FSarkoGridPage>& InPages)
		{
			Pages.Reserve(InPages.Num());
			for (const FSarkoGridPage& Page : InPages)
			{
				Pages.Emplace(false, Page.Cells());
			}
		}

		bool IsFree(const FSarkoGridPage& Page, int32 PageIndex, int32 X, int32 Y, int32 W, int32 H) const
		{
			if (X < 0 || Y < 0 || X + W > Page.Columns || Y + H > Page.Rows)
			{
				return false;
			}
			for (int32 Row = Y; Row < Y + H; ++Row)
			{
				for (int32 Column = X; Column < X + W; ++Column)
				{
					if (Pages[PageIndex][Row * Page.Columns + Column])
					{
						return false;
					}
				}
			}
			return true;
		}

		void Occupy(const FSarkoGridPage& Page, int32 PageIndex, int32 X, int32 Y, int32 W, int32 H)
		{
			for (int32 Row = Y; Row < Y + H; ++Row)
			{
				for (int32 Column = X; Column < X + W; ++Column)
				{
					Pages[PageIndex][Row * Page.Columns + Column] = true;
				}
			}
		}
	};

	/** First fit across every page, in page order then row-major within a page.
	 *  Returns an unplaced slot when the shape fits nowhere. */
	FSarkoGridSlot FirstFit(const FOccupancy& Occupancy, const TArray<FSarkoGridPage>& Pages, FIntPoint Size)
	{
		for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
		{
			const FSarkoGridPage& Page = Pages[PageIndex];
			// Rows outer, columns inner: "left to right, top to bottom" (spec §1).
			// Starting from (0,0) every time is what makes the scan backfill a
			// hole a wider item skipped over.
			for (int32 Y = 0; Y + Size.Y <= Page.Rows; ++Y)
			{
				for (int32 X = 0; X + Size.X <= Page.Columns; ++X)
				{
					if (Occupancy.IsFree(Page, PageIndex, X, Y, Size.X, Size.Y))
					{
						return FSarkoGridSlot{ PageIndex, X, Y, Size.X, Size.Y };
					}
				}
			}
		}
		return FSarkoGridSlot{ INDEX_NONE, 0, 0, Size.X, Size.Y };
	}
}

TArray<FSarkoGridSlot> SarkoGrid::Place(const TArray<FSarkoItemStack>& Stacks,
	const FSarkoItemCatalog& Catalog, const TArray<FSarkoGridPage>& Pages)
{
	TArray<FSarkoGridSlot> Slots;
	Slots.Reserve(Stacks.Num());

	FOccupancy Occupancy(Pages);
	for (const FSarkoItemStack& Stack : Stacks)
	{
		const FSarkoGridSlot Slot = FirstFit(Occupancy, Pages, SizeOf(Catalog, Stack.Item));
		if (Slot.IsPlaced())
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
		// An unplaced stack does NOT stop the loop: a bike frame that will not fit
		// must not hide the eight one-cell items behind it.
		Slots.Add(Slot);
	}
	return Slots;
}

int32 SarkoGrid::AddToGrid(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
	const TArray<FSarkoGridPage>& Pages, FName Item, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return 0;
	}

	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		// Refused whole. A guessed stack size or a guessed rectangle puts an id
		// the backend will reject into the raid result, and the haul dies with it.
		return Quantity;
	}

	const int32 StackSize = FMath::Max(1, Def->StackSize);
	int32 Remaining = Quantity;

	// 1. Top up existing partial stacks. Free: a stack occupies one rectangle
	//    regardless of count (spec §1.1), so this costs no space at all.
	for (FSarkoItemStack& Stack : Stacks)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Stack.Item != Item || Stack.Quantity >= StackSize)
		{
			continue;
		}
		const int32 Moved = FMath::Min(StackSize - Stack.Quantity, Remaining);
		Stack.Quantity += Moved;
		Remaining -= Moved;
	}

	if (Remaining <= 0)
	{
		return 0;
	}

	// 2. Open new rectangles while one fits. The occupancy is built once and
	//    carried across the loop rather than re-derived per stack, which for a
	//    120-round pour into a 12-cell grid is the difference between two
	//    placements and twenty-four.
	const FIntPoint Size(FMath::Max(1, Def->Width), FMath::Max(1, Def->Height));
	FOccupancy Occupancy(Pages);
	for (const FSarkoGridSlot& Slot : Place(Stacks, Catalog, Pages))
	{
		if (Slot.IsPlaced())
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
	}

	while (Remaining > 0)
	{
		const FSarkoGridSlot Slot = FirstFit(Occupancy, Pages, Size);
		if (!Slot.IsPlaced())
		{
			// No space of that shape. The remainder stays where it came from —
			// spec §1's partial fit, and the vanishing-loot rule.
			break;
		}
		Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		const int32 Moved = FMath::Min(StackSize, Remaining);
		Stacks.Add(FSarkoItemStack{ Item, Moved });
		Remaining -= Moved;
	}

	return Remaining;
}
