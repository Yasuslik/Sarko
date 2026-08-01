#include "UI/SarkoCellWidgets.h"

#include "Loot/SarkoItemCatalog.h"
#include "Styling/CoreStyle.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** FCoreStyle is compiled into SlateCore, so no font asset is involved. */
	FSlateFontInfo SarkoCellFont(float Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	ESarkoItemCategory SarkoCellCategoryOf(FName Item)
	{
		const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Item);
		return Def ? Def->Category : ESarkoItemCategory::Junk;
	}
}

FVector2D SarkoUI::CellExtentPt(FIntPoint Size)
{
	const float W = static_cast<float>(FMath::Max(1, Size.X));
	const float H = static_cast<float>(FMath::Max(1, Size.Y));
	return FVector2D(W * CellSizePt + (W - 1.f) * CellGutterPt,
		H * CellSizePt + (H - 1.f) * CellGutterPt);
}

FVector2D SarkoUI::CellOriginPt(const FSarkoGridSlot& Slot)
{
	const float Pitch = CellSizePt + CellGutterPt;
	return FVector2D(Slot.X * Pitch, Slot.Y * Pitch);
}

TSharedRef<SWidget> SarkoUI::BuildCellContent(const FSarkoItemStack& Stack)
{
	const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Stack.Item);
	const FString Label = CellLabel(Def ? Def->Name : Stack.Item.ToString());

	TSharedRef<SOverlay> Content = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);

	Content->AddSlot()
		.HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.Font(SarkoCellFont(CellLabelPt))
			.ColorAndOpacity(FSlateColor(CellLabelColour))
			// A 36 pt strip of cell interior cannot hold every label even after
			// CellLabel's nine-character cut, and a word running out of its cell
			// reads as a rendering fault. Ellipsis, never a clip.
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(FText::FromString(Label))
		];

	// Only when there is more than one: "1" on every cell is noise, and the count
	// is meant to be the thing the eye stops on when it matters.
	if (Stack.Quantity > 1)
	{
		Content->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Font(SarkoCellFont(CellCountPt))
				.ColorAndOpacity(FSlateColor(CellCountColour))
				.Text(FText::AsNumber(Stack.Quantity))
			];
	}

	return Content;
}

TSharedRef<SWidget> SarkoUI::BuildStackCell(const FSarkoItemStack& Stack,
	const FSarkoInventoryStyles& Styles, FIntPoint Size)
{
	const FVector2D Extent = CellExtentPt(Size);
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(Extent.X)
		.HeightOverride(Extent.Y)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(&Styles.CellByCategory[static_cast<int32>(SarkoCellCategoryOf(Stack.Item))].Normal)
			.Padding(FMargin(CellPadPt))
			[
				BuildCellContent(Stack)
			]
		];
}

TSharedRef<SWidget> SarkoUI::BuildEmptyCell(const FSarkoInventoryStyles& Styles, FIntPoint Size)
{
	const FVector2D Extent = CellExtentPt(Size);
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(Extent.X)
		.HeightOverride(Extent.Y)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(&Styles.EmptyCell.Normal)
		];
}

TSharedRef<SWidget> SarkoUI::BuildGridPage(const TArray<FSarkoItemStack>& Stacks,
	const TArray<FSarkoGridSlot>& Slots, int32 PageIndex,
	const FSarkoGridPage& Page, const FSarkoInventoryStyles& Styles,
	const FStackCellDecorator& Decorate)
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
		.Visibility(EVisibility::SelfHitTestInvisible);

	const auto Put = [&Canvas](TSharedRef<SWidget> Widget, FVector2D Origin, FVector2D Extent)
	{
		Canvas->AddSlot()
			// Anchors at the top-left and an explicit offset: the page's own size is
			// fixed by the SBox around it, so absolute placement in points is exactly
			// what the already-computed layout says.
			.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
			.Alignment(FVector2D(0.f, 0.f))
			.AutoSize(false)
			.Offset(FMargin(Origin.X, Origin.Y, Extent.X, Extent.Y))
			[
				Widget
			];
	};

	// Every cell as an empty slot first, so the grid reads as a grid even when it
	// is nearly bare — then the occupied rectangles over the top of them.
	for (int32 Y = 0; Y < Page.Rows; ++Y)
	{
		for (int32 X = 0; X < Page.Columns; ++X)
		{
			Put(BuildEmptyCell(Styles, FIntPoint(1, 1)),
				CellOriginPt(FSarkoGridSlot{ PageIndex, X, Y, 1, 1 }), CellExtentPt(FIntPoint(1, 1)));
		}
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FSarkoGridSlot& Slot = Slots[Index];
		if (Slot.Page != PageIndex || !Stacks.IsValidIndex(Index))
		{
			continue;
		}
		const FIntPoint Size(Slot.W, Slot.H);
		TSharedRef<SWidget> Cell = BuildStackCell(Stacks[Index], Styles, Size);
		if (Decorate)
		{
			Cell = Decorate(Index, Cell);
		}
		Put(Cell, CellOriginPt(Slot), CellExtentPt(Size));
	}

	const float Pitch = SarkoUI::CellSizePt + SarkoUI::CellGutterPt;
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(FMath::Max(0, Page.Columns) * Pitch - SarkoUI::CellGutterPt)
		.HeightOverride(FMath::Max(0, Page.Rows) * Pitch - SarkoUI::CellGutterPt)
		[
			Canvas
		];
}
