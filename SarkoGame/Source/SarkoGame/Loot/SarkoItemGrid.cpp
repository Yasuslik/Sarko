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

void SarkoGrid::SortForStash(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog)
{
	Stacks.StableSort([&Catalog](const FSarkoItemStack& A, const FSarkoItemStack& B)
	{
		const FSarkoItemDef* DefA = Catalog.Find(A.Item);
		const FSarkoItemDef* DefB = Catalog.Find(B.Item);

		// An id the catalog does not know sorts last rather than first: it is the
		// visible symptom of items.json drifting from the backend, and it belongs
		// at the bottom of the grid where it is odd rather than at the top where
		// it looks like the most important thing the player owns.
		const int32 CategoryA = DefA ? static_cast<int32>(DefA->Category) : MAX_int32;
		const int32 CategoryB = DefB ? static_cast<int32>(DefB->Category) : MAX_int32;
		if (CategoryA != CategoryB)
		{
			return CategoryA < CategoryB;
		}

		const FString NameA = DefA ? DefA->Name : A.Item.ToString();
		const FString NameB = DefB ? DefB->Name : B.Item.ToString();
		// CaseSensitive, i.e. by code point: FString's own operator< folds case
		// through TChar::ToUpper, which is ASCII-only, so every Ukrainian name
		// would compare through a fold that does nothing — right by accident on
		// this catalog and wrong the moment a Latin name is added beside them.
		const int32 NameOrder = NameA.Compare(NameB, ESearchCase::CaseSensitive);
		if (NameOrder != 0)
		{
			return NameOrder < 0;
		}
		// The id is the tiebreaker, so the order is TOTAL and therefore idempotent.
		return A.Item.LexicalLess(B.Item);
	});
}

int32 SarkoGrid::StashRowsFor(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
	int32 Columns, int32 MinRows)
{
	const int32 SafeColumns = FMath::Max(1, Columns);
	int32 Rows = FMath::Max(1, MinRows);

	// The bound: one row per stack plus the tallest item is always enough, and it
	// stops an item wider than the grid — which can never place — from spinning
	// this forever. Such an item is a data bug the catalog's own size test
	// catches; this is only here so a bad file cannot hang the shelter.
	const int32 Ceiling = FMath::Max(Rows, Stacks.Num() * 2 + 2);
	while (Rows < Ceiling)
	{
		const TArray<FSarkoGridPage> Page = { FSarkoGridPage{ SafeColumns, Rows } };
		const TArray<FSarkoGridSlot> Slots = Place(Stacks, Catalog, Page);
		const bool bAllPlaced = !Slots.ContainsByPredicate(
			[](const FSarkoGridSlot& Slot) { return !Slot.IsPlaced(); });
		if (bAllPlaced)
		{
			return Rows;
		}
		++Rows;
	}
	return Rows;
}

FSarkoGridSlot SarkoGrid::RefusalAnchor(const TArray<FSarkoGridSlot>& Placed,
	const TArray<FSarkoGridPage>& Pages, FIntPoint Size)
{
	const int32 W = FMath::Max(1, Size.X);
	const int32 H = FMath::Max(1, Size.Y);

	FOccupancy Occupancy(Pages);
	for (const FSarkoGridSlot& Slot : Placed)
	{
		if (Slot.IsPlaced() && Pages.IsValidIndex(Slot.Page))
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
	}

	// Page 0's origin is the fallback, so a completely full grid still draws a
	// ghost: the loudest refusal must not be the one that draws nothing.
	FSarkoGridSlot Best{ 0, 0, 0, W, H };
	int32 BestRun = -1;
	for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
	{
		const FSarkoGridPage& Page = Pages[PageIndex];
		for (int32 Y = 0; Y < Page.Rows; ++Y)
		{
			for (int32 X = 0; X < Page.Columns; ++X)
			{
				if (!Occupancy.IsFree(Page, PageIndex, X, Y, 1, 1))
				{
					continue;
				}
				int32 Run = 0;
				while (X + Run < Page.Columns && Occupancy.IsFree(Page, PageIndex, X + Run, Y, 1, 1))
				{
					++Run;
				}
				// Strictly greater, so the FIRST longest run wins — the gap nearest
				// the top-left, which is where the eye starts.
				if (Run > BestRun)
				{
					BestRun = Run;
					// Pulled back so the whole rectangle stays ON the page when the page
					// is big enough to hold it anywhere. Without this a gap in the last
					// column anchors a 2x1 half off the plate, where the missing edge
					// reads as a clipping fault rather than as a refusal — and, worse,
					// the ghost then overhangs NOTHING, when overhanging the cell that
					// blocked it is the entire argument.
					//
					// The gap is still covered: the pull-back is at most W-1 columns, so
					// the rectangle always still contains (X, Y). What changes is which
					// side of the gap the surplus falls on.
					const int32 DrawX = FMath::Max(0, FMath::Min(X, Page.Columns - W));
					const int32 DrawY = FMath::Max(0, FMath::Min(Y, Page.Rows - H));
					Best = FSarkoGridSlot{ PageIndex, DrawX, DrawY, W, H };
				}
			}
		}
	}
	return Best;
}
