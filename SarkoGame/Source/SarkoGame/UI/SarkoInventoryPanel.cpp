#include "UI/SarkoInventoryPanel.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoLootContainer.h"
#include "Loot/SarkoLootTable.h"
#include "Pawn/SarkoCharacter.h"
#include "Styling/CoreStyle.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

int32 SarkoUI::PlayerGridRows(int32 PlayerCells)
{
	// At least one row. A pawn with no backpack component at all reports zero
	// capacity for one frame during possession, and a zero-row panel is a sliver
	// that appears and then jumps to full height — worse than an empty grid.
	return FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(0, PlayerCells), GridColumns));
}

float SarkoUI::InventoryPanelHeightPt(int32 PlayerCells)
{
	const int32 Rows = PlayerGridRows(PlayerCells);
	const float PlayerGridPt = Rows * CellSizePt + (Rows - 1) * CellGutterPt;
	const float ContainerGridPt = CellSizePt;   // one row of four; see SarkoLoot::ContainerCells
	return PanelPadYPt + TakeAllRowPt + GridGapPt + ContainerGridPt
		+ DividerPt + HeaderRowPt + GridGapPt + PlayerGridPt + PanelPadYPt;
}

FBox2D SarkoUI::InventoryPanelRect(FBox2D SafeFrame, int32 PlayerCells, float PointScale)
{
	const float Width = PanelWidthPt * PointScale;
	const float Height = InventoryPanelHeightPt(PlayerCells) * PointScale;
	const FVector2D Max(SafeFrame.Max.X - PanelRightInsetPt * PointScale,
		SafeFrame.Max.Y - PanelBottomInsetPt * PointScale);
	return FBox2D(FVector2D(Max.X - Width, Max.Y - Height), Max);
}

FBox2D SarkoUI::InteractButtonRectFor(const ASarkoCharacter* Pawn, FBox2D SafeFrame, float PointScale)
{
	const FBox2D Ordinary = SarkoInput::InteractButtonRect(SafeFrame);
	if (!Pawn || Pawn->GetOpenContainerIndex() == INDEX_NONE)
	{
		return Ordinary;
	}

	const int32 Cells = Pawn->BackpackComponent ? Pawn->BackpackComponent->GetCellCount() : GridColumns;
	const FBox2D Panel = InventoryPanelRect(SafeFrame, Cells, PointScale);
	return SarkoInput::InteractButtonRectBesidePanel(SafeFrame, Panel);
}

namespace
{
	/** FCoreStyle is compiled into SlateCore, so no font asset is involved. */
	FSlateFontInfo PanelFont(float Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	/** Uppercases a whole string with SarkoUI::UpperChar, which handles Cyrillic
	 *  where FString::ToUpper (ASCII-only) silently would not. */
	FString UpperAll(const FString& In)
	{
		FString Out = In;
		for (int32 Index = 0; Index < Out.Len(); ++Index)
		{
			Out[Index] = SarkoUI::UpperChar(Out[Index]);
		}
		return Out;
	}

	/** The tier of the container this pawn has open, uppercased, or empty. Read
	 *  off the game state's container registry rather than replicated: the tier is
	 *  already on every client, on an actor that is already in a list. */
	FString OpenContainerTier(const ASarkoCharacter* Pawn)
	{
		if (!Pawn || Pawn->GetOpenContainerIndex() == INDEX_NONE)
		{
			return FString();
		}
		const ASarkoRaidGameState* RaidState =
			Pawn->GetWorld() ? Pawn->GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
		if (!RaidState)
		{
			return FString();
		}
		for (const TWeakObjectPtr<ASarkoLootContainer>& Weak : RaidState->GetContainers())
		{
			const ASarkoLootContainer* Container = Weak.Get();
			if (Container && Container->ContainerIndex == Pawn->GetOpenContainerIndex())
			{
				return UpperAll(Container->Tier.ToString());
			}
		}
		return FString();
	}

	ESarkoItemCategory CategoryOf(FName Item)
	{
		const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Item);
		return Def ? Def->Category : ESarkoItemCategory::Junk;
	}
}

FVector2D SSarkoInventoryPanel::ViewportPx() const
{
	FVector2D Size(SarkoUI::DesignWidthPt, SarkoUI::DesignHeightPt);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return Size;
}

float SSarkoInventoryPanel::OverlayScale() const
{
	// Not PointScaleForViewport: SGameLayerManager already wraps the whole
	// viewport overlay in an SDPIScaler of its own, so scaling by the raw factor
	// here would compound with it and render the panel ~9% over the points it
	// claims — which would then not line up with the HUD drawn underneath it.
	return SarkoUI::OverlayPointScale(ViewportPx());
}

int32 SSarkoInventoryPanel::PlayerCells() const
{
	const ASarkoCharacter* P = Pawn.Get();
	return (P && P->BackpackComponent) ? P->BackpackComponent->GetCellCount() : SarkoUI::GridColumns;
}

FMargin SSarkoInventoryPanel::PanelPadding() const
{
	// SafeFrame answers in viewport pixels; everything inside the DPI scaler is
	// in points. One division is the whole bridge, and it is done here rather
	// than inside InventoryPanelRect so that the same pure function serves the
	// HUD (which works in pixels) without either caller converting twice.
	const FVector2D Viewport = ViewportPx();
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, SarkoUI::PointScaleForViewport(Viewport));
	const FBox2D SafePx = SarkoInput::SafeFrame(Viewport);
	const FBox2D SafePt(SafePx.Min / Scale, SafePx.Max / Scale);
	const FBox2D Rect = SarkoUI::InventoryPanelRect(SafePt, PlayerCells(), 1.f);

	// Left and top only: the box is HAlign_Left/VAlign_Top inside a scaler that
	// covers the whole viewport, so these two numbers place it outright.
	return FMargin(FMath::Max(0.f, Rect.Min.X), FMath::Max(0.f, Rect.Min.Y), 0.f, 0.f);
}

FOptionalSize SSarkoInventoryPanel::PanelHeight() const
{
	return FOptionalSize(SarkoUI::InventoryPanelHeightPt(PlayerCells()));
}

float SSarkoInventoryPanel::PanelOpacity() const
{
	if (bExiting)
	{
		// Fade only, and fast: leaving must feel instant.
		return 1.f - ExitCurve.GetLerp();
	}
	// IsPlaying and not GetLerp alone: a finished sequence is "settled", and a
	// sequence that was never played would otherwise report zero and draw an
	// invisible panel.
	return EntryCurve.IsPlaying() ? EntryCurve.GetLerp() : 1.f;
}

FLinearColor SSarkoInventoryPanel::PanelTint() const
{
	const float Alpha = PanelOpacity();
	return FLinearColor(1.f, 1.f, 1.f, Alpha);
}

TOptional<FSlateRenderTransform> SSarkoInventoryPanel::PlateTransform() const
{
	if (!EntryCurve.IsPlaying())
	{
		return TOptional<FSlateRenderTransform>();
	}
	// In from the right edge, in POINTS — the transform is applied to the plate,
	// which lives inside the DPI scaler, so no factor is involved.
	const float Offset = SarkoUI::EntrySlidePt * (1.f - EntryCurve.GetLerp());
	return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(Offset, 0.f)));
}

const FSlateBrush* SSarkoInventoryPanel::RefusalGlowBrush() const
{
	// Signal 1: the player grid's rim pulses amber. Only NoSpace lights it — a
	// shake with no amber means "you moved", and that distinction is the
	// difference between a player retrying and a player understanding.
	if (!RefusalCurve.IsPlaying() || LastRefusal != ESarkoTakeRefusal::NoSpace)
	{
		return nullptr;
	}
	return Styles->RefusalGlowFor(FMath::Sin(PI * RefusalCurve.GetLerp()));
}

FSlateColor SSarkoInventoryPanel::BackpackHeaderColour() const
{
	// Signal 2: a STATE, not a flash. The header stays amber for as long as the
	// bag is full, and it is the same amber ASarkoHUD::DrawBackpack uses — two
	// different ambers for one fact would be worse than none.
	return FSlateColor(bBagFull ? SarkoUI::AmberWarn : SarkoUI::HeaderColour);
}

TOptional<FSlateRenderTransform> SSarkoInventoryPanel::ContainerCellTransform(int32 SlotIndex) const
{
	// Signal 3: the refused cell shakes, two full cycles, and ends exactly where
	// it started (Sarko.UI.RefusalShakeStartsAndEndsAtRest). Every refusal reason
	// shakes; only NoSpace also lights the amber above.
	if (!RefusalCurve.IsPlaying() || SlotIndex != RefusedSlot)
	{
		return TOptional<FSlateRenderTransform>();
	}
	const float Offset = SarkoUI::RefusalShakeOffsetPt(RefusalCurve.GetLerp());
	return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(Offset, 0.f)));
}

TOptional<FSlateRenderTransform> SSarkoInventoryPanel::PlayerCellTransform(int32 SlotIndex) const
{
	if (!TransferCurve.IsPlaying() || SlotIndex != ReceivingCell)
	{
		return TOptional<FSlateRenderTransform>();
	}
	// 0.86 -> 1.0 about the cell's centre. The pivot is set beside this, so the
	// cell grows into place rather than out of its top-left corner.
	const float Scale = FMath::Lerp(0.86f, 1.f, TransferCurve.GetLerp());
	return TOptional<FSlateRenderTransform>(FSlateRenderTransform(Scale));
}

const FSlateBrush* SSarkoInventoryPanel::TransferFlashBrush(int32 SlotIndex) const
{
	if (!TransferCurve.IsPlaying() || SlotIndex != ReceivingCell)
	{
		return nullptr;
	}
	// White rim, fading out into the category colour underneath it — as a baked
	// brush, for the same reason the refusal glow is one.
	return Styles->TransferFlashFor(1.f - TransferCurve.GetLerp());
}

void SSarkoInventoryPanel::Construct(const FArguments& InArgs)
{
	Pawn = InArgs._Pawn;

	// The styles are the process-wide ones, NOT built here. SButton stores a raw
	// const FButtonStyle* and points its brushes into it, so a style constructed
	// in this function dangles the moment it returns — and the symptom is not a
	// crash, it is a panel that draws garbage, intermittently, on a device.
	Styles = FSarkoInventoryStyles::Get();

	// The ROOT is SelfHitTestInvisible, and so is every box, border and label
	// below it. That is the mechanism — not a hope — by which a thumb landing on
	// the panel's padding, on the plate, or on the player's own grid falls
	// through to SViewport and drives the sticks. Spec §4: "The player remains
	// steerable while it is open — they are standing in the open, and should be
	// able to run." Only the four container cells and the take-all row are
	// Visible, because those are the only things there is anything to press.
	SetVisibility(EVisibility::SelfHitTestInvisible);
	SetColorAndOpacity(TAttribute<FLinearColor>::CreateSP(this, &SSarkoInventoryPanel::PanelTint));

	ChildSlot
	[
		// Resolution independence in one node: inside this, a Slate unit is a
		// point on the 844x390 landscape canvas. See OverlayScale.
		SNew(SDPIScaler)
		.DPIScale(TAttribute<float>::CreateSP(this, &SSarkoInventoryPanel::OverlayScale))
		[
			SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(TAttribute<FMargin>::CreateSP(this, &SSarkoInventoryPanel::PanelPadding))
			[
				SNew(SBox)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.WidthOverride(SarkoUI::PanelWidthPt)
				.HeightOverride(TAttribute<FOptionalSize>::CreateSP(this, &SSarkoInventoryPanel::PanelHeight))
				[
					// The plate: rounded, outlined, translucent at 0.86 — and NOT a
					// dim of the world behind it. A bot crossing behind this has to
					// stay a moving silhouette (spec §5).
					SAssignNew(Plate, SBorder)
					.Visibility(EVisibility::SelfHitTestInvisible)
					.BorderImage(&Styles->PanelBrush)
					.Padding(FMargin(SarkoUI::PanelPadPt, SarkoUI::PanelPadYPt))
					[
						SNew(SVerticalBox)

						// The header row is the take-all button, whole. There is no
						// close button in here: a third 44 pt target in 216 pt of
						// width would crowd the grid, so the existing interact
						// button becomes close instead (ASarkoHUD::DrawInteract).
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.HeightOverride(SarkoUI::TakeAllRowPt)
							[
								SNew(SButton)
								.ButtonStyle(&Styles->TakeAllRow)
								.ContentPadding(FMargin(8.f, 0.f))
								.VAlign(VAlign_Center)
								.OnClicked(FOnClicked::CreateSP(this, &SSarkoInventoryPanel::HandleTakeAll))
								[
									SNew(SHorizontalBox)

									+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
									[
										SNew(SVerticalBox)

										+ SVerticalBox::Slot().AutoHeight()
										[
											SNew(STextBlock)
											.Font(PanelFont(SarkoUI::SectionLabelPt))
											.ColorAndOpacity(FSlateColor(SarkoUI::HeaderColour))
											.Text(FText::FromString(TEXT("ОБШУК")))
										]

										+ SVerticalBox::Slot().AutoHeight()
										[
											// The tier is the headline: it is what says
											// whether this crate was worth the walk.
											SAssignNew(ContainerHeader, STextBlock)
											.Font(PanelFont(SarkoUI::TierPt))
											.ColorAndOpacity(FSlateColor(SarkoUI::CellCountColour))
											.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
										]
									]

									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
										.Padding(6.f, 0.f, 0.f, 0.f)
									[
										// Two lines, right-aligned. On one line this
										// label is 105 pt of a 172 pt row and left the
										// tier beside it truncated to "MILIT…";
										// stacked it is 42, and both columns then read
										// as a label over its value.
										SNew(STextBlock)
										.Font(PanelFont(SarkoUI::TakeAllPt))
										.ColorAndOpacity(FSlateColor(SarkoUI::AmberWarn))
										.Justification(ETextJustify::Right)
										.LineHeightPercentage(0.92f)
										.Text(FText::FromString(TEXT("ЗАБРАТИ\nВСЕ")))
									]
								]
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, SarkoUI::GridGapPt, 0.f, 0.f)
						[
							SNew(SBox)
							.Visibility(EVisibility::SelfHitTestInvisible)
							.HeightOverride(SarkoUI::CellSizePt)
							[
								SAssignNew(ContainerRow, SHorizontalBox)
							]
						]

						// The hairline, padded to the 12 pt the stack allots it.
						+ SVerticalBox::Slot().AutoHeight()
							.Padding(0.f, (SarkoUI::DividerPt - 1.f) * 0.5f, 0.f, (SarkoUI::DividerPt - 1.f) * 0.5f)
						[
							SNew(SBox)
							.Visibility(EVisibility::SelfHitTestInvisible)
							.HeightOverride(1.f)
							[
								SNew(SBorder)
								.Visibility(EVisibility::SelfHitTestInvisible)
								.BorderImage(&Styles->HairlineBrush)
							]
						]

						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.Visibility(EVisibility::SelfHitTestInvisible)
							.HeightOverride(SarkoUI::HeaderRowPt)
							.VAlign(VAlign_Center)
							[
								SAssignNew(BackpackHeader, STextBlock)
								.Font(PanelFont(SarkoUI::SectionHeaderPt))
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.ColorAndOpacity(TAttribute<FSlateColor>::CreateSP(
									this, &SSarkoInventoryPanel::BackpackHeaderColour))
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, SarkoUI::GridGapPt, 0.f, 0.f)
						[
							SNew(SOverlay)
							.Visibility(EVisibility::SelfHitTestInvisible)

							+ SOverlay::Slot()
							[
								SAssignNew(PlayerGrid, SVerticalBox)
							]

							// The refusal pulse, laid OVER the whole player grid.
							+ SOverlay::Slot()
							[
								SNew(SBorder)
								.Visibility(EVisibility::SelfHitTestInvisible)
								.BorderImage(TAttribute<const FSlateBrush*>::CreateSP(
									this, &SSarkoInventoryPanel::RefusalGlowBrush))
							]
						]
					]
				]
			]
		]
	];

	Plate->SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
		this, &SSarkoInventoryPanel::PlateTransform));

	// 140 ms, CubicOut, and nothing loops: FCurveSequence::Play registers an
	// active timer, and a looping one would hold it open for as long as the panel
	// is up — a frame's worth of Slate work every frame for a thing not moving.
	EntryCurve.Play(SharedThis(this));

	Refresh();
}

TSharedRef<SWidget> SSarkoInventoryPanel::BuildCellContent(const FSarkoItemStack& Stack) const
{
	const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Stack.Item);
	const FString Label = SarkoUI::CellLabel(Def ? Def->Name : Stack.Item.ToString());

	TSharedRef<SOverlay> Content = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);

	Content->AddSlot()
		.HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.Font(PanelFont(SarkoUI::CellLabelPt))
			.ColorAndOpacity(FSlateColor(SarkoUI::CellLabelColour))
			// A 36 pt strip of cell interior cannot hold every label even after
			// CellLabel's nine-character cut, and a word running out of its cell
			// reads as a rendering fault. Ellipsis, never a clip.
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(FText::FromString(Label))
		];

	// Only when there is more than one: "1" on every cell is noise, and the
	// count is meant to be the thing the eye stops on when it matters.
	if (Stack.Quantity > 1)
	{
		Content->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Font(PanelFont(SarkoUI::CellCountPt))
				.ColorAndOpacity(FSlateColor(SarkoUI::CellCountColour))
				.Text(FText::AsNumber(Stack.Quantity))
			];
	}

	return Content;
}

TSharedRef<SWidget> SSarkoInventoryPanel::BuildEmptyCell() const
{
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(SarkoUI::CellSizePt)
		.HeightOverride(SarkoUI::CellSizePt)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(&Styles->EmptyCell.Normal)
		];
}

TSharedRef<SWidget> SSarkoInventoryPanel::BuildContainerCell(const FSarkoItemStack& Stack, int32 SlotIndex)
{
	TSharedPtr<SButton> Button;
	TSharedRef<SBox> Cell = SNew(SBox)
		.WidthOverride(SarkoUI::CellSizePt)
		.HeightOverride(SarkoUI::CellSizePt)
		[
			SAssignNew(Button, SButton)
			.ButtonStyle(&Styles->CellByCategory[static_cast<int32>(CategoryOf(Stack.Item))])
			.ContentPadding(FMargin(SarkoUI::CellPadPt))
			.OnClicked(FOnClicked::CreateSP(this, &SSarkoInventoryPanel::HandleTakeSlot, SlotIndex))
			[
				BuildCellContent(Stack)
			]
		];

	Cell->SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
		this, &SSarkoInventoryPanel::ContainerCellTransform, SlotIndex));

	ContainerButtons[SlotIndex] = Button;
	return Cell;
}

TSharedRef<SWidget> SSarkoInventoryPanel::BuildPlayerCell(const FSarkoItemStack& Stack, int32 SlotIndex)
{
	// Deliberately NOT a button. Putting an item back is out of scope for this
	// slice, and a hit-testable player grid would eat every tap in the bottom
	// right of the screen — which is exactly where the aim thumb lives. As a
	// border it is SelfHitTestInvisible, so the player keeps aiming through it.
	TSharedRef<SBox> Cell = SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(SarkoUI::CellSizePt)
		.HeightOverride(SarkoUI::CellSizePt)
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)

			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.BorderImage(&Styles->CellByCategory[static_cast<int32>(CategoryOf(Stack.Item))].Normal)
				.Padding(FMargin(SarkoUI::CellPadPt))
				[
					BuildCellContent(Stack)
				]
			]

			// The transfer flash: a white rim over the receiving cell, fading into
			// the category colour under it across 120 ms.
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.BorderImage(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSarkoInventoryPanel::TransferFlashBrush, SlotIndex))
			]
		];

	Cell->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Cell->SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
		this, &SSarkoInventoryPanel::PlayerCellTransform, SlotIndex));
	return Cell;
}

void SSarkoInventoryPanel::Refresh()
{
	ASarkoCharacter* P = Pawn.Get();
	if (!P || !ContainerRow.IsValid() || !PlayerGrid.IsValid())
	{
		return;
	}

	const FString Tier = OpenContainerTier(P);
	ContainerHeader->SetText(FText::FromString(Tier.IsEmpty() ? FString(TEXT("—")) : Tier));

	// --- the crate ---------------------------------------------------------
	const TArray<FSarkoItemStack>& Slots = P->GetOpenContainerSlots();
	ContainerRow->ClearChildren();
	ContainerButtons.Reset();
	ContainerButtons.SetNum(SarkoLoot::ContainerCells);
	for (int32 Index = 0; Index < SarkoLoot::ContainerCells; ++Index)
	{
		const bool bOccupied = Slots.IsValidIndex(Index) && Slots[Index].Quantity > 0;
		ContainerRow->AddSlot().AutoWidth()
			.Padding(Index == 0 ? 0.f : SarkoUI::CellGutterPt, 0.f, 0.f, 0.f)
			[
				bOccupied ? BuildContainerCell(Slots[Index], Index) : BuildEmptyCell()
			];
	}

	// --- the bag -----------------------------------------------------------
	static const TArray<FSarkoItemStack> NoSlots;
	const TArray<FSarkoItemStack>& Bag = P->BackpackComponent ? P->BackpackComponent->GetSlots() : NoSlots;
	const int32 Limit = PlayerCells();
	bBagFull = Bag.Num() >= Limit;

	BackpackHeader->SetText(FText::FromString(
		FString::Printf(TEXT("РЮКЗАК %d/%d"), Bag.Num(), Limit)));

	// Which cell received, so the transfer animation has something to play on.
	// The first index whose stack is not what it was is the answer: a take either
	// tops up a partial stack in place or opens the next free cell, and both show
	// up here as exactly one changed index.
	ReceivingCell = INDEX_NONE;
	if (bHasPreviousBag)
	{
		for (int32 Index = 0; Index < Bag.Num(); ++Index)
		{
			if (!PreviousBag.IsValidIndex(Index)
				|| PreviousBag[Index].Item != Bag[Index].Item
				|| PreviousBag[Index].Quantity != Bag[Index].Quantity)
			{
				ReceivingCell = Index;
				break;
			}
		}
	}
	PreviousBag = Bag;
	bHasPreviousBag = true;

	PlayerGrid->ClearChildren();
	const int32 Rows = SarkoUI::PlayerGridRows(Limit);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible);
		for (int32 Column = 0; Column < SarkoUI::GridColumns; ++Column)
		{
			const int32 Index = Row * SarkoUI::GridColumns + Column;
			const bool bOccupied = Index < Limit && Bag.IsValidIndex(Index) && Bag[Index].Quantity > 0;
			// Past the capacity there is no cell at all, not an empty one: a
			// four-pocket pawn must not be shown eight slots it cannot use.
			TSharedRef<SWidget> Cell = Index >= Limit
				? StaticCastSharedRef<SWidget>(SNew(SSpacer).Size(FVector2D(SarkoUI::CellSizePt, SarkoUI::CellSizePt)))
				: (bOccupied ? BuildPlayerCell(Bag[Index], Index) : BuildEmptyCell());
			RowBox->AddSlot().AutoWidth()
				.Padding(Column == 0 ? 0.f : SarkoUI::CellGutterPt, 0.f, 0.f, 0.f)
				[
					Cell
				];
		}
		PlayerGrid->AddSlot().AutoHeight()
			.Padding(0.f, Row == 0 ? 0.f : SarkoUI::CellGutterPt, 0.f, 0.f)
			[
				RowBox
			];
	}

	if (ReceivingCell != INDEX_NONE)
	{
		TransferCurve.Play(SharedThis(this));
	}
}

void SSarkoInventoryPanel::PlayRefusal(int32 SlotIndex, ESarkoTakeRefusal Reason)
{
	// Three signals at once, all of them on the PLAYER's half except the shake:
	// 1. the player grid's outline pulses amber (NoSpace only);
	// 2. the РЮКЗАК header turns amber and stays amber while the bag is full;
	// 3. the refused container cell shakes, +-4 pt, two cycles.
	//
	// "I tapped and nothing happened" is the failure mode the spec names, and
	// one signal is not enough to beat it: a shake alone is read as a glitch and
	// a colour alone is missed by a thumb that is covering the cell.
	RefusedSlot = SlotIndex;
	LastRefusal = Reason;

	if (const ASarkoCharacter* P = Pawn.Get())
	{
		if (P->BackpackComponent)
		{
			bBagFull = P->BackpackComponent->GetUsedCells() >= P->BackpackComponent->GetCellCount();
		}
	}

	// From the start every time — FCurveSequence::Play sets StartTime to now — so
	// a second refusal restarts rather than continuing a curve already half spent.
	RefusalCurve.Play(SharedThis(this));
}

void SSarkoInventoryPanel::PlayExit()
{
	if (bExiting)
	{
		return;
	}
	bExiting = true;
	ExitCurve.Play(SharedThis(this));
}

bool SSarkoInventoryPanel::IsExitFinished() const
{
	return bExiting && !ExitCurve.IsPlaying();
}

FReply SSarkoInventoryPanel::HandleTakeSlot(int32 SlotIndex)
{
	if (ASarkoCharacter* P = Pawn.Get())
	{
		// The client's own view of what is open. Every field is re-validated
		// server-side against the server's copy — this index is a request, not
		// an authority.
		P->RequestTakeItem(P->GetOpenContainerIndex(), SlotIndex);
	}
	return FReply::Handled();
}

FReply SSarkoInventoryPanel::HandleTakeAll()
{
	if (ASarkoCharacter* P = Pawn.Get())
	{
		P->RequestTakeAll(P->GetOpenContainerIndex());
	}
	return FReply::Handled();
}

#if !UE_BUILD_SHIPPING
bool SSarkoInventoryPanel::SimulateTapContainerCell(int32 SlotIndex)
{
	// IsEnabled() is checked here and not left to SimulateClick: the engine's
	// SimulateClick calls ExecuteOnClick() directly and does not consult the
	// enabled state, so without this a scripted press could take an item the
	// player could not have tapped — proving the opposite of what it is for.
	if (!ContainerButtons.IsValidIndex(SlotIndex) || !ContainerButtons[SlotIndex].IsValid()
		|| !ContainerButtons[SlotIndex]->IsEnabled())
	{
		return false;
	}
	ContainerButtons[SlotIndex]->SimulateClick();
	return true;
}
#endif
