#include "Shelter/SarkoShelterWidget.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Loot/SarkoEquipment.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"
#include "UI/SarkoCellWidgets.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/**
	 * The canvas every size in this file is authored against: a **landscape** phone
	 * in **logical points**, iPhone 14/15 sized. The game is landscape-only, so the
	 * long edge is the width.
	 *
	 * Points and not pixels on purpose — the touch rule this screen has to satisfy
	 * is written in points ("≥ 44 pt"), so authoring in the same unit makes the rule
	 * checkable by reading the code instead of by guessing at a device's density.
	 */
	constexpr float DesignWidth = SarkoUI::DesignWidthPt;
	constexpr float DesignHeight = SarkoUI::DesignHeightPt;

	/**
	 * The horizontal margin, which on a landscape phone is a safe area and not a
	 * taste decision: rotated, an iPhone puts its Dynamic Island against the
	 * leading edge, and iOS reports that inset on **both** sides — 59 pt each on a
	 * 14 Pro — so that turning the phone the other way up does not reflow anything.
	 */
	constexpr float SideInset = 60.f;

	/** The bottom margin. The home indicator is 21 pt of the short edge in
	 *  landscape; 26 clears it and leaves the content visibly above it. */
	constexpr float BottomInset = 26.f;

	/**
	 * The destination column, and the whole of spec §1's navigation.
	 *
	 * LEFT-EDGE, because a phone held in landscape rests both thumbs on the edges
	 * and neither of them anywhere near the middle-top.
	 *
	 * 126 pt, and it took two frames to get there. At 96, with 12 pt labels and a
	 * "• " marker glued to the front of the current one, "ІНВЕНТАР" and "МАГАЗИН"
	 * were clipped AT BOTH ENDS — centred text in a box too narrow for it — so the
	 * column read "НВЕНТА" and "ІАГАЗИ", and "В РЕЙД" read "З РЕЙД". At 112, with the
	 * marker moved into a slot of its own and the label at 11 pt, they still lost
	 * their last letter. 126 leaves the longest label ~25 pt of slack, and the label
	 * is LEFT-justified now so that if a longer word ever appears it loses one end
	 * rather than both.
	 *
	 * It is deliberately not wider than that: every point here is a point the stash
	 * grid does not get, and SarkoUI::StashColumns is already down to seven paying
	 * for this column and the character panel.
	 */
	constexpr float NavColumnPt = 126.f;
	constexpr float NavGapPt = 14.f;

	/** Each destination is 46 pt tall, past the 44 pt tap-target minimum. Three of
	 *  them plus their gaps is 150 pt of a ~300 pt column, which is why the raid
	 *  button below them is pinned to the floor rather than stacked under them. */
	constexpr float NavButtonPt = 46.f;
	constexpr float NavButtonGapPt = 6.f;

	/**
	 * The character panel's width, and it is derived rather than chosen.
	 *
	 * The two 2×2 slots sit SIDE BY SIDE — the height arithmetic in
	 * BuildCharacterPanel is why — and a 2×2 is 92 pt (SarkoUI::CellExtentPt), so
	 * the CONTENT is 92 + 6 + 92 = 190. Around it: 2×5 of plate padding. 204 leaves
	 * 4 pt of slack, and the plate carries NO scroll box — see BuildCharacterPanel,
	 * which fits the body inside the plate's height rather than letting it scroll.
	 *
	 * Both numbers were settled by frames, and both failures looked like clipping:
	 * at 190 (the content width with nothing allowed for the plate) the backpack cell
	 * read "РЮК…" and the pockets page was cut down its right edge; at 206 WITH a
	 * scroll box the bar took ~16 pt off the inside and the backpack cell read
	 * "РЮКЗ…" — the second slot was being squeezed by exactly the width of the bar.
	 */
	constexpr float CharacterPanelPt = 204.f;
	constexpr float CharacterGapPt = 14.f;

	/**
	 * What is left for the stash: 844 − 2×60 − 126 − 14 − 204 − 14 = 366 pt, against
	 * SEVEN columns at 7×44 + 6×4 = 332 — so 32 pt for the scroll bar.
	 *
	 * The column count came DOWN from eight for this, and SarkoUI::StashColumns
	 * carries the whole history of that number and why this is not a return to the
	 * old workaround. The short version: the screen gained a destination column and a
	 * character panel, both out of the same 724 pt, and eight columns need 380.
	 *
	 * The static_assert is the point of writing it this way — it pins the grid and
	 * the column it lives in together, so widening the character panel or adding a
	 * fourth destination fails the BUILD instead of clipping the eighth column behind
	 * a scroll bar on a device.
	 */
	constexpr float StashColumnPt = DesignWidth - 2.f * SideInset
		- NavColumnPt - NavGapPt - CharacterPanelPt - CharacterGapPt;
	static_assert(StashColumnPt >= SarkoUI::StashColumns * SarkoUI::CellSizePt
		+ (SarkoUI::StashColumns - 1) * SarkoUI::CellGutterPt + 24.f,
		"the stash column no longer fits SarkoUI::StashColumns columns plus a scroll "
		"bar — narrow the character panel or the destination column, or take a column "
		"off the grid, but do not let the grid clip behind the bar");

	/** The garage screen's content width, and therefore how wide its labels wrap. */
	constexpr float GarageColumnPt = DesignWidth - 2.f * SideInset - NavColumnPt - NavGapPt;

	/** The craft button's content padding, and the width its label therefore has.
	 *  Half the garage column, because the ladder sits beside the recipe now. */
	constexpr float CraftButtonPadX = 14.f;
	constexpr float GarageHalfPt = GarageColumnPt * 0.5f - 10.f;
	constexpr float CraftLabelWrap = GarageHalfPt - 2.f * CraftButtonPadX;

	/** FCoreStyle is compiled into SlateCore, so no font asset is involved. */
	FSlateFontInfo ShelterFont(float Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	/**
	 * A flat opaque fill. FCoreStyle's "Border" brush is a BORDER_BRUSH — it draws
	 * the four edges and leaves the middle transparent — so using SBorder's default
	 * brush for the menu's background gives a see-through menu over the empty void
	 * of /Engine/Maps/Entry, which reads as a rendering fault. "WhiteBrush" is an
	 * FSlateColorBrush(White) and tints to whatever BorderBackgroundColor says.
	 */
	const FSlateBrush* FillBrush()
	{
		return FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	/**
	 * Slate colours are LINEAR and the frame is sRGB-encoded on the way out, so a
	 * value that looks like "almost black" as a number is not: linear 0.05 lands
	 * near sRGB 0.24, i.e. mid-grey. Every constant here is chosen in linear space
	 * for the sRGB result named beside it.
	 *
	 * Suffixed "Colour" rather than named Ink/Body/Warn: these are file-scope in an
	 * anonymous namespace, and a unity build puts this file in the same translation
	 * unit as whatever else the blob happens to contain — a plain `Body` here made a
	 * local `Body` in BackendClientTest.cpp a -Wshadow error (and -Werror) purely
	 * because a test file grew and the blobs regrouped.
	 */
	const FSlateColor InkColour(FLinearColor(0.011f, 0.013f, 0.017f));   // ~#1c1f22, near-black
	const FSlateColor BrightColour(FLinearColor(0.92f, 0.92f, 0.88f));   // ~#f7f7f3, the title
	const FSlateColor BodyColour(FLinearColor(0.62f, 0.64f, 0.66f));     // ~#d0d3d5, list rows
	const FSlateColor LabelColour(FLinearColor(0.22f, 0.23f, 0.25f));    // ~#8b8f94, section labels
	const FSlateColor WarnColour(FLinearColor(1.f, 0.55f, 0.06f));       // ~#ffc16a, status + refusals
	const FSlateColor RuleColour(FLinearColor(0.055f, 0.06f, 0.07f));    // ~#454a50, the hairlines
	/** ~#9be79f, a satisfied recipe part and a built vehicle. The same green for
	 *  "this is done" everywhere on this screen, and not two. */
	const FSlateColor MetColour(FLinearColor(0.35f, 0.85f, 0.40f));

	/**
	 * The figure, and it is four rounded boxes at ~2.5% of white.
	 *
	 * Dim on purpose: it is a silhouette the equipment is laid ON, so anything
	 * brighter competes with the cells that carry the actual information. Spec §6
	 * is explicit that the drawing will be crude and that this is acceptable while
	 * the slots read — which is what the captions do, and why the captions are not
	 * optional decoration.
	 */
	const FSlateColor BodyPartColour(FLinearColor(0.025f, 0.027f, 0.032f)); // ~#333a41

	/** A hairline, so the sections do not read as one blob. Hand-rolled rather
	 *  than SSeparator: FCoreStyle registers its "Separator" brush inside the
	 *  editor-styles block, so it is not guaranteed present in a -game process. */
	TSharedRef<SWidget> HorizontalRule()
	{
		return SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SBorder)
				.BorderImage(FillBrush())
				.BorderBackgroundColor(RuleColour)
			];
	}

	/** One limb of the figure. A plain filled box; the rounding comes from nothing
	 *  at all, because FCoreStyle's WhiteBrush is a rectangle — a rounded-box brush
	 *  per limb would be four more cached brushes for a silhouette nobody looks at. */
	TSharedRef<SWidget> BodyPart(float Width, float Height)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(Height)
			[
				SNew(SBorder)
				.BorderImage(FillBrush())
				.BorderBackgroundColor(BodyPartColour)
			];
	}
}

float SSarkoShelterWidget::UiScaleForViewport(FVector2D ViewportSize)
{
	// SarkoUI::OverlayPointScale, which divides the game layer manager's own DPI
	// factor out: this widget is added to the VIEWPORT OVERLAY, and
	// SGameLayerManager already wraps that overlay in an SDPIScaler
	// (SGameLayerManager.cpp:113). Scaling by the raw point scale here COMPOUNDED
	// with it — 9% too large on a phone, a third too small in a 1560x720 window —
	// so the layout was being composed on the wrong canvas depending on the
	// screenshot size. Dividing it out makes a point on this screen measure a point
	// on the glass, which is what the ≥44 pt tap rule is written in.
	return SarkoUI::OverlayPointScale(ViewportSize);
}

float SSarkoShelterWidget::UiScale() const
{
	// Nothing here is scaled automatically. UMG runs every widget through
	// UUserInterfaceSettings' DPI curve; AddViewportWidgetContent does not — it adds
	// the widget to the viewport overlay at 1:1 Slate units, which are physical
	// pixels. On a 2556-pixel-tall phone an unscaled 46-unit button is 46 physical
	// pixels, under 16 pt at 3x — a third of the tap target the touch rule requires.
	FVector2D ViewportSize(DesignWidth, DesignHeight);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	return UiScaleForViewport(ViewportSize);
}

TOptional<FSlateRenderTransform> SSarkoShelterWidget::StashCellTransform(int32 StackIndex) const
{
	// The refused cell shakes, two full cycles, and ends exactly where it started
	// (Sarko.UI.RefusalShakeStartsAndEndsAtRest holds that property for the shared
	// function). Every other cell gets the identity, which is all of them almost
	// always — this is read as an attribute so nothing has to tick.
	if (!RefusalCurve.IsPlaying() || StackIndex != RefusedStashIndex)
	{
		return TOptional<FSlateRenderTransform>();
	}
	const float Offset = SarkoUI::RefusalShakeOffsetPt(RefusalCurve.GetLerp());
	return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(Offset, 0.f)));
}

const FSlateBrush* SSarkoShelterWidget::CharacterPlateBrush() const
{
	// Signal 2 of the refusal: the character plate's rim pulses amber, because the
	// character is where the refused item was headed. As a choice of BAKED brush and
	// not an animated tint: SBorder folds a brush's own tint into what it draws with
	// (SBorder.cpp:115), so a transparent-bodied rim has a final alpha of zero and
	// FSlateDrawElement culls the whole element, outline included. See
	// FSarkoInventoryStyles::RefusalGlow.
	if (RefusalCurve.IsPlaying())
	{
		if (const FSlateBrush* Glow = Styles->RefusalGlowFor(FMath::Sin(PI * RefusalCurve.GetLerp())))
		{
			return Glow;
		}
	}
	return &Styles->PanelBrush;
}

void SSarkoShelterWidget::Construct(const FArguments& InArgs)
{
	OnEnterRaid = InArgs._OnEnterRaid;
	OnCraft = InArgs._OnCraft;
	OnSelectScreen = InArgs._OnSelectScreen;
	OnEquipStack = InArgs._OnEquipStack;
	OnUnequipSlot = InArgs._OnUnequipSlot;

	// The PROCESS-WIDE styles, not built here: SButton stores a raw
	// const FButtonStyle* and points its brushes into it, so a style constructed in
	// this function dangles the moment it returns — and the symptom is not a crash,
	// it is a screen that draws garbage, intermittently, on a device.
	Styles = FSarkoInventoryStyles::Get();

	// Logged once, at construction, because the whole layout is expressed in points
	// and this factor is the only thing turning it into pixels — a wrong scale is a
	// menu with unreachable buttons and nothing else says so.
	FVector2D ViewportSize(0.f, 0.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	UE_LOG(LogTemp, Display, TEXT("SarkoShelter: viewport %.0fx%.0f px, UI scale %.3f (canvas %.0fx%.0f pt)"),
		ViewportSize.X, ViewportSize.Y, UiScale(), DesignWidth, DesignHeight);

	SlotBoxes.SetNum(SarkoEquip::Slots().Num());
	SlotCaptions.SetNum(SarkoEquip::Slots().Num());

	ChildSlot
	[
		// Resolution independence, in one node. See UiScale().
		SNew(SDPIScaler)
		.DPIScale(TAttribute<float>::CreateSP(this, &SSarkoShelterWidget::UiScale))
		[
			// Opaque, full-screen: /Engine/Maps/Entry behind this is an empty void
			// and a translucent menu over it reads as a rendering fault. The
			// background reaches under the cutouts on purpose; only the content is
			// inset, because an unpainted strip beside a notch is the thing that
			// actually looks broken.
			SNew(SBorder)
			.BorderImage(FillBrush())
			.BorderBackgroundColor(InkColour)
			.Padding(FMargin(SideInset, 14.f, SideInset, BottomInset))
			[
				SNew(SVerticalBox)

				// The header spans the whole width rather than sitting in the
				// destination column: "УКРИТТЯ" at 22 pt is wider than the 96 pt
				// column, and the status line has to have room for a verbatim
				// backend error.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
					[
						SAssignNew(TitleText, STextBlock)
						.Font(ShelterFont(22.f))
						.ColorAndOpacity(BrightColour)
						.Text(FText::FromString(TEXT("УКРИТТЯ")))
					]

					// Right-aligned against the far edge. Wrapped, not one line: it
					// carries a verbatim backend error and a refused equip's reason.
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
						.HAlign(HAlign_Right).Padding(16.f, 0.f, 0.f, 2.f)
					[
						SAssignNew(StatusText, STextBlock)
						.Font(ShelterFont(11.f))
						.ColorAndOpacity(WarnColour)
						.Justification(ETextJustify::Right)
						.AutoWrapText(true)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 10.f)
				[
					HorizontalRule()
				]

				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)

					// ---- the destination column (spec §1) -----------------------
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, NavGapPt, 0.f)
					[
						SNew(SBox)
						.WidthOverride(NavColumnPt)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(NavBox, SVerticalBox)
							]

							// The slack goes HERE, between the destinations and the
							// verb. That is what pins "В РЕЙД" to the floor of the
							// column, where a thumb already rests, whatever the
							// screen above it happens to be showing.
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SSpacer).Size(FVector2D(1.f, 1.f))
							]

							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(RaidButton, SButton)
								.ContentPadding(FMargin(8.f, 12.f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								// An attribute, not a one-shot value: SetView flips
								// the flag and Slate re-reads it, so nothing has to
								// tick to keep the button honest.
								//
								// It is NEVER false for want of a weapon — spec §4's
								// dead-end guard. FSarkoRaidButtonView::bEnabled is
								// the only input, and it does not consult the
								// equipment.
								.IsEnabled_Lambda([this]() { return bRaidEnabled; })
								.OnClicked(this, &SSarkoShelterWidget::HandleEnterRaid)
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									[
										SAssignNew(RaidLabel, STextBlock)
										.Font(ShelterFont(15.f))
										.Justification(ETextJustify::Center)
										.Text(FText::FromString(TEXT("В РЕЙД")))
									]

									// "БЕЗ ЗБРОЇ", amber, on its own line under the
									// verb. Collapsed when armed — an empty text block
									// still measures its font and would reserve a line.
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									[
										SAssignNew(RaidSubLabel, STextBlock)
										.Font(ShelterFont(10.f))
										.ColorAndOpacity(WarnColour)
										.Justification(ETextJustify::Center)
									]
								]
							]
						]
					]

					// ---- the screens, as peers ---------------------------------
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SAssignNew(ScreenSwitcher, SWidgetSwitcher)

						// Index order matches ESarkoShelterScreen, which is what
						// SetView indexes with. A switcher and not three visibility
						// attributes: only the active child is laid out at all, so an
						// off-screen grid costs nothing.
						+ SWidgetSwitcher::Slot()
						[
							BuildInventoryScreen()
						]

						+ SWidgetSwitcher::Slot()
						[
							BuildGarageScreen()
						]

						+ SWidgetSwitcher::Slot()
						[
							BuildShopScreen()
						]
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> SSarkoShelterWidget::BuildInventoryScreen()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, CharacterGapPt, 0.f)
		[
			SNew(SBox)
			.WidthOverride(CharacterPanelPt)
			[
				BuildCharacterPanel()
			]
		]

		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)

			// A static section label. Not part of FSarkoShelterView because it is a
			// constant, not data — without it a bare grid has nothing saying what it is.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
			[
				SNew(STextBlock)
				.Font(ShelterFont(11.f))
				.ColorAndOpacity(LabelColour)
				.Text(FText::FromString(TEXT("СХОВОК")))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				HorizontalRule()
			]

			// The stash is the SAME cell grid the raid's crate panel draws (spec §2:
			// one visual language for "things you own"), and it can be any height —
			// which is the reason this screen is Slate and not DrawHUD primitives.
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SAssignNew(StashBox, SBox)
					]
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center).VAlign(VAlign_Top)
				.Padding(0.f, 20.f, 0.f, 0.f)
				[
					// Over the grid, not instead of it: an empty stash still shows the
					// shape it will fill.
					SAssignNew(StashNoteText, STextBlock)
					.Font(ShelterFont(15.f))
					.ColorAndOpacity(LabelColour)
				]
			]

			// The last raid, under the stash rather than in a column of its own.
			// It used to own 42% of the screen's width; it is three short lines, and
			// the width it was holding is what the character panel is drawn in.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				HorizontalRule()
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(STextBlock)
					.Font(ShelterFont(11.f))
					.ColorAndOpacity(LabelColour)
					.Text(FText::FromString(TEXT("ОСТАННІЙ РЕЙД")))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SAssignNew(OutcomeText, STextBlock)
					.Font(ShelterFont(14.f))
					.ColorAndOpacity(BrightColour)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SAssignNew(HaulNoteText, STextBlock)
					.Font(ShelterFont(12.f))
					.ColorAndOpacity(LabelColour)
				]

				// The haul itself, laid out horizontally: it is at most a few stacks
				// and it is a footer now, so a vertical list would push the grid up.
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SAssignNew(HaulBox, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SSarkoShelterWidget::BuildCharacterPanel()
{
	// THE HEIGHT ARITHMETIC, which is what decided that the two 2×2 slots sit side
	// by side rather than stacked. The body has ~300 pt: a 2×2 is 92 pt and a 3×1
	// weapon slot is 44, so stacking weapon + clothing + backpack + pockets is
	// 44 + 3×92 = 320 pt of cells before a single caption — over budget on its own.
	// Side by side it is 44 + 92 + 92 = 228, plus three 13 pt captions and the
	// panel's own label and rule: 293. It is inside an SScrollBox anyway, so a
	// point over stays reachable instead of being clipped.
	const TArray<ESarkoEquipSlot>& Slots = SarkoEquip::Slots();
	check(Slots.Num() == 3);

	// Slots() order is weapon, clothing, backpack — the weapon in the hands at the
	// top, then the coat on the chest and the bag on the back, side by side.
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// The heading is INSIDE the plate rather than above it, and it SHARES its row with
	// the weapon slot's caption. Both are height, and height is what the plate has
	// none of: a separate heading with a rule under it cost ~23 pt and a caption row of
	// its own another 14, against a body that overran the plate by 13. "ЗБРОЯ" is
	// still directly above the weapon slot, because this row is.
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SAssignNew(CharacterTitle, STextBlock)
			.Font(ShelterFont(11.f))
			.ColorAndOpacity(LabelColour)
		]

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
			.HAlign(HAlign_Right)
		[
			SAssignNew(SlotCaptions[0], STextBlock)
			.Font(ShelterFont(9.5f))
			.ColorAndOpacity(LabelColour)
		]
	];

	// ---- the weapon, in the hands, with the head beside it ---------------------
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(4.f, 0.f, 8.f, 0.f)
		[
			// The head. It is here rather than centred above everything because the
			// weapon slot is 140 pt of a 190 pt panel and there is no room for a
			// centred head on its own row — beside the gun it also reads as a figure
			// holding one.
			BodyPart(24.f, 24.f)
		]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(SlotBoxes[0], SBox)
		]
	];

	// ---- the shoulders, joining the two 2x2 slots into a torso -----------------
	// 6 pt and almost no padding. It is the only thing on this panel that is purely
	// decorative, so it is the first thing that gave when the pockets page needed
	// height: what makes the two 2x2 cells read as a chest and a back is that they
	// are captioned and joined, and a 6 pt bar joins them as well as a 8 pt one.
	Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 1.f)
	[
		BodyPart(120.f, 6.f)
	];

	// ---- the coat and the bag, side by side ------------------------------------
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[
				SAssignNew(SlotCaptions[1], STextBlock)
				.Font(ShelterFont(9.5f))
				.ColorAndOpacity(LabelColour)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(SlotBoxes[1], SBox)
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(SarkoUI::CellGutterPt + 2.f, 0.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[
				SAssignNew(SlotCaptions[2], STextBlock)
				.Font(ShelterFont(9.5f))
				.ColorAndOpacity(LabelColour)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(SlotBoxes[2], SBox)
			]
		]
	];

	// ---- the pockets, between the legs -----------------------------------------
	// The caption and the note share ONE line, and that is a height decision a frame
	// forced: on its own row the note pushed the pockets page below the fold of the
	// plate's scroll box, so the 2×2 the spec asks to be "always present" was the one
	// thing you had to scroll to see.
	Body->AddSlot().AutoHeight().Padding(0.f, 1.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SAssignNew(PocketsCaption, STextBlock)
			.Font(ShelterFont(9.5f))
			.ColorAndOpacity(LabelColour)
		]

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
		[
			SAssignNew(PocketsNote, STextBlock)
			.Font(ShelterFont(8.5f))
			.ColorAndOpacity(LabelColour)
		]
	];
	Body->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 8.f, 0.f)
		[
			BodyPart(14.f, 92.f)
		]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(PocketsBox, SBox)
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8.f, 0.f, 0.f, 0.f)
		[
			BodyPart(14.f, 92.f)
		]
	];

	// NO SCROLL BOX, and that is a decision two frames forced rather than a
	// simplification. A scroll box reserved ~16 pt of the plate's inside width for its
	// bar, which squeezed the second 2x2 slot and ellipsised the backpack cell to
	// "РЮКЗ…"; and it put the pockets page — the one thing spec §2 calls "always
	// present" — below the fold, so the player had to scroll to find it. The body is
	// made to FIT instead: ~276 pt against the ~303 the plate gets, which is why the
	// heading is inside the plate and the decorations are as thin as they are.
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				// The plate is what the amber refusal rim is drawn on, which is why the
				// panel has one at all: it is the surface the refused item was aimed at.
				SAssignNew(CharacterPlate, SBorder)
				.BorderImage(TAttribute<const FSlateBrush*>::CreateSP(
					this, &SSarkoShelterWidget::CharacterPlateBrush))
				.Padding(FMargin(5.f, 5.f))
				.VAlign(VAlign_Top)
				[
					Body
				]
			]

			// The refusal's third signal: the reason, in words, at the foot of the
			// figure — where the item the player just tapped was headed.
			//
			// OVER the plate rather than a row inside it, exactly as the container
			// panel's own refusal note is drawn over its grid, and for the same reason
			// a frame made obvious: as a row it added ~15 pt to a body that already
			// filled the plate, so the note appeared BELOW the plate's border, floating
			// in the background. Over it, it costs no layout at all and nothing reflows
			// when a tap is refused.
			+ SOverlay::Slot()
			.HAlign(HAlign_Left).VAlign(VAlign_Bottom)
			.Padding(FMargin(8.f, 0.f, 8.f, 6.f))
			[
				SAssignNew(RefusalNote, STextBlock)
				.Font(ShelterFont(9.5f))
				.ColorAndOpacity(WarnColour)
				.WrapTextAt(CharacterPanelPt - 20.f)
			]
		];
}

TSharedRef<SWidget> SSarkoShelterWidget::BuildGarageScreen()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
		[
			SAssignNew(GarageText, STextBlock)
			.Font(ShelterFont(17.f))
			.ColorAndOpacity(BrightColour)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			HorizontalRule()
		]

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)

			// The recipe and its button on the left, the ladder on the right. Two
			// columns because the garage has a whole screen now: the cramped corner
			// it used to live in had room for the step you were on and nothing else,
			// and a progression whose shape is invisible is not felt as one.
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 20.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(GarageHalfPt)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
					[
						SNew(STextBlock)
						.Font(ShelterFont(11.f))
						.ColorAndOpacity(LabelColour)
						.Text(FText::FromString(TEXT("ДЕТАЛІ")))
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SAssignNew(GarageParts, SVerticalBox)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						// The payoff sentence. Empty until a craft succeeds, and it
						// stays for the rest of the visit rather than flashing: this is
						// what every raid before it was for.
						SAssignNew(CraftLineText, STextBlock)
						.Font(ShelterFont(12.f))
						.ColorAndOpacity(MetColour)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						// 48 pt tall, past the 44 pt tap-target minimum.
						.HeightOverride(48.f)
						[
							SAssignNew(CraftButton, SButton)
							.ContentPadding(FMargin(CraftButtonPadX, 0.f))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.IsEnabled_Lambda([this]() { return bCraftEnabled; })
							.OnClicked(this, &SSarkoShelterWidget::HandleCraft)
							[
								// WRAPPED, not ellipsed, and at an EXPLICIT width. A
								// centred ellipsis cuts BOTH ends, so
								// "НЕ ВИСТАЧАЄ: Рама велосипеда" read
								// "Е ВИСТАЧАЄ: Рама велосипед" — a label that names the
								// missing part with its name mangled. And AutoWrapText
								// inside a centre-aligned button takes its width from
								// the text's own desired width, so the two feed each
								// other down to the widest single word.
								SAssignNew(CraftLabel, STextBlock)
								.Font(ShelterFont(13.f))
								.Justification(ETextJustify::Center)
								.WrapTextAt(CraftLabelWrap)
							]
						]
					]
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
				[
					SNew(STextBlock)
					.Font(ShelterFont(11.f))
					.ColorAndOpacity(LabelColour)
					.Text(FText::FromString(TEXT("ТЕХНІКА")))
				]

				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(GarageLadder, SVerticalBox)
					]
				]
			]
		];
}

TSharedRef<SWidget> SSarkoShelterWidget::BuildShopScreen()
{
	// A stub, and it says so in the middle of its own screen rather than as a greyed
	// button in a corner (spec §1). Nothing here is interactive: a control that did
	// anything now would be a design decision this stage has not made, and spec §5
	// is explicit that the shop stays a stub.
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
		[
			SNew(STextBlock)
			.Font(ShelterFont(17.f))
			.ColorAndOpacity(BrightColour)
			.Text(FText::FromString(TEXT("МАГАЗИН")))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			HorizontalRule()
		]

		+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(ShelterFont(14.f))
			.ColorAndOpacity(LabelColour)
			.Text(FText::FromString(TEXT("НЕЗАБАРОМ")))
		];
}

void SSarkoShelterWidget::SetView(const FSarkoShelterView& View)
{
	bRaidEnabled = View.Raid.bEnabled;

	TitleText->SetText(FText::FromString(View.Title));
	GarageText->SetText(FText::FromString(View.Garage.Title));
	CraftLabel->SetText(FText::FromString(View.Garage.CraftLabel));
	bCraftEnabled = View.Garage.bCanCraft;

	// COLLAPSED when empty, not merely blank. An STextBlock measures its FONT and
	// not its string, so an empty one still reserves a whole line — and several of
	// these are empty most of the time (no raid yet, nothing crafted, nothing wrong,
	// armed), so they would reserve rows of a column that has none to spare.
	// SBoxPanel skips a collapsed slot's PADDING as well as its size.
	const auto SetLine = [](const TSharedPtr<STextBlock>& Text, const FString& Value)
	{
		Text->SetText(FText::FromString(Value));
		Text->SetVisibility(Value.IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
	};

	SetLine(OutcomeText, View.OutcomeTitle);
	SetLine(CraftLineText, View.CraftLine);
	SetLine(StatusText, View.StatusLine);
	SetLine(HaulNoteText, View.HaulNote);
	RaidLabel->SetText(FText::FromString(View.Raid.Label));
	SetLine(RaidSubLabel, View.Raid.SubLabel);

	ScreenSwitcher->SetActiveWidgetIndex(static_cast<int32>(View.Screen));

	// ---- the destination column ---------------------------------------------
	// Rebuilt rather than restyled: three buttons, and the only thing that changes
	// is which of them is current.
	NavBox->ClearChildren();
	for (const FSarkoShelterDestination& Destination : View.Destinations)
	{
		const ESarkoShelterScreen Screen = Destination.Screen;
		NavBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, NavButtonGapPt)
		[
			SNew(SBox)
			// 46 pt, past the 44 pt tap-target minimum, and it does not depend on
			// the label's length.
			.HeightOverride(NavButtonPt)
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.f, 0.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.IsEnabled(Destination.bEnabled)
				.OnClicked_Lambda([this, Screen]()
				{
					OnSelectScreen.ExecuteIfBound(Screen);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)

					// The marker gets a slot of ITS OWN rather than being glued to the
					// front of the label. Glued, it counted towards the label's desired
					// width, and a centred label wider than its button clips at BOTH
					// ends — which is how a frame came back reading "НВЕНТА".
					//
					// It is redundant with the colour on purpose: "which destination am
					// I on" must not be carried by hue alone.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(8.f)
						[
							SNew(STextBlock)
							.Font(ShelterFont(11.f))
							.ColorAndOpacity(BrightColour)
							.Text(FText::FromString(Destination.bCurrent ? TEXT("•") : TEXT(" ")))
						]
					]

					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(ShelterFont(11.f))
						// LEFT, not centred, and that is what a frame settled: a centred
						// label wider than its box loses a letter off BOTH ends, so
						// "ІНВЕНТАР" came back as "НВЕНТА" and the column was unreadable.
						// Left-justified, an overflow costs the last letter only.
						.Justification(ETextJustify::Left)
						// The current destination is BRIGHT and the others are body
						// grey. A marker and a colour, not a highlight box: a button
						// that changes shape when selected changes the column's layout.
						.ColorAndOpacity(Destination.bCurrent ? BrightColour : BodyColour)
						.Text(FText::FromString(Destination.Label))
					]
				]
			]
		];
	}

	// ---- the character ------------------------------------------------------
	CharacterTitle->SetText(FText::FromString(View.Character.Title));
	PocketsCaption->SetText(FText::FromString(View.Character.PocketsCaption));
	SetLine(PocketsNote, View.Character.PocketsNote);

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	for (int32 Index = 0; Index < SlotBoxes.Num() && Index < View.Character.Slots.Num(); ++Index)
	{
		const FSarkoEquipSlotView& Slot = View.Character.Slots[Index];
		SlotCaptions[Index]->SetText(FText::FromString(Slot.Caption));

		if (!Slot.bOccupied)
		{
			// An empty slot draws at the slot's own largest rect, so the outline is
			// the ROOM a weapon has. Not tappable: there is nothing to take off.
			SlotBoxes[Index]->SetContent(SarkoUI::BuildEmptyCell(*Styles, Slot.Extent));
			continue;
		}

		// The SAME cell the stash and the crate panel draw (spec §2), wrapped in an
		// invisible tap so that tapping it takes it off. InvisibleTap adds
		// hit-testing and no pixels, which is what keeps one item looking like one
		// item wherever it is sitting.
		const ESarkoEquipSlot SlotId = Slot.Slot;
		SlotBoxes[Index]->SetContent(
			SNew(SButton)
			.ButtonStyle(&Styles->InvisibleTap)
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this, SlotId]()
			{
				OnUnequipSlot.ExecuteIfBound(SlotId);
				return FReply::Handled();
			})
			[
				SarkoUI::BuildStackCell(Slot.Stack, *Styles, Slot.Extent)
			]);
	}

	// The pockets, as an empty carry page. SarkoUI::BuildGridPage with no stacks
	// draws exactly the empty cells, which is the honest picture: what you carry is
	// packed in the raid, not here.
	{
		const FSarkoGridPage Page{ View.Character.PocketsGrid.X, View.Character.PocketsGrid.Y };
		PocketsBox->SetContent(SarkoUI::BuildGridPage({}, {}, /*PageIndex*/ 0, Page, *Styles));
	}

	// ---- the garage ---------------------------------------------------------
	// The recipe's lines carry a colour as well as a sentence: a met requirement is
	// GREEN and a short one stays body grey. That is what makes "Ланцюг 1/1" read as
	// satisfied rather than as a number the player has to compare against another
	// number — and the information colour carries is deliberately redundant, because
	// the craft button below still spells the missing part out in words.
	GarageParts->ClearChildren();
	for (const FSarkoGaragePart& Part : View.Garage.PartLines)
	{
		GarageParts->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			SNew(STextBlock)
			.Font(ShelterFont(13.f))
			.ColorAndOpacity(Part.bMet ? MetColour : BodyColour)
			.Text(FText::FromString(Part.Text))
		];
	}

	GarageLadder->ClearChildren();
	for (const FSarkoVehicleRung& Rung : View.Garage.Ladder)
	{
		// Three states, and the marker carries the same information as the colour so
		// that neither is load-bearing alone: a tick for what is built, an arrow for
		// what is next, a dash for the rest.
		const TCHAR* Marker = Rung.bBuilt ? TEXT("+") : (Rung.bNext ? TEXT(">") : TEXT("-"));
		const FSlateColor Colour = Rung.bBuilt ? MetColour : (Rung.bNext ? BrightColour : LabelColour);
		GarageLadder->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(STextBlock)
			.Font(ShelterFont(13.f))
			.ColorAndOpacity(Colour)
			.Text(FText::FromString(FString::Printf(TEXT("%s %s"), Marker, *Rung.Text)))
		];
	}

	// ---- the last raid ------------------------------------------------------
	HaulBox->ClearChildren();
	for (const FString& Line : View.HaulLines)
	{
		HaulBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(ShelterFont(12.f))
			.ColorAndOpacity(BodyColour)
			.Text(FText::FromString(Line))
		];
	}

	// ---- the stash ----------------------------------------------------------
	StashNoteText->SetText(FText::FromString(View.StashNote));

	// Rebuilt wholesale, once per profile fetch and once per tap — never per frame.
	// Slate is not a tick path.
	const int32 Rows = SarkoGrid::StashRowsFor(View.StashStacks, Catalog,
		SarkoUI::StashColumns, SarkoUI::StashMinRows);
	const FSarkoGridPage Page{ SarkoUI::StashColumns, Rows };
	const TArray<FSarkoGridSlot> Slots = SarkoGrid::Place(View.StashStacks, Catalog, { Page });

	// EVERY stash cell is tappable now, which is the equip gesture (spec §2). The
	// decorator hook is what makes that possible without a second grid renderer: the
	// container panel uses the same seam for its transfer flash.
	SarkoUI::FStackCellDecorator Decorate =
		[this](int32 StackIndex, TSharedRef<SWidget> Cell) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(&Styles->InvisibleTap)
			.ContentPadding(FMargin(0.f))
			.RenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
				this, &SSarkoShelterWidget::StashCellTransform, StackIndex))
			.OnClicked_Lambda([this, StackIndex]()
			{
				OnEquipStack.ExecuteIfBound(StackIndex);
				return FReply::Handled();
			})
			[
				Cell
			];
	};

	StashBox->SetContent(SarkoUI::BuildGridPage(View.StashStacks, Slots, /*PageIndex*/ 0,
		Page, *Styles, Decorate));
}

void SSarkoShelterWidget::SetCraftInFlight(bool bInFlight)
{
	// A second press while the first is in flight is a second debit. The button is
	// re-enabled by the SetView that follows the refetched profile, which is also the
	// moment the answer is actually known.
	if (bInFlight)
	{
		bCraftEnabled = false;
	}
}

void SSarkoShelterWidget::PlayRefusal(int32 StashIndex, const FString& Reason)
{
	RefusedStashIndex = StashIndex;
	if (RefusalNote.IsValid())
	{
		// The reason, verbatim, from SarkoEquip::Accepts. It stays on screen after
		// the 240 ms of motion has finished: the shake says "no" and the sentence says
		// why, and a sentence that vanishes with the animation is a sentence nobody
		// reads. It is replaced by the next tap, successful or not.
		RefusalNote->SetText(FText::FromString(Reason));
		RefusalNote->SetVisibility(Reason.IsEmpty()
			? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
	}
	RefusalCurve.Play(AsShared());
}

#if !UE_BUILD_SHIPPING
bool SSarkoShelterWidget::SimulateEnterRaidClickIfEnabled()
{
	// IsEnabled() is checked here and not left to SimulateClick: the engine's
	// SimulateClick calls ExecuteOnClick() directly and does *not* consult the
	// enabled state, so without this a scripted press would happily start a raid
	// while the button was still greyed out waiting for the profile — proving the
	// opposite of what it is there to prove.
	if (!RaidButton.IsValid() || !RaidButton->IsEnabled())
	{
		return false;
	}
	RaidButton->SimulateClick();
	return true;
}

bool SSarkoShelterWidget::SimulateCraftClickIfEnabled()
{
	// Same reason as above: the engine's SimulateClick does not consult the enabled
	// state, so a scripted press would craft against a stash the button itself was
	// refusing.
	if (!CraftButton.IsValid() || !CraftButton->IsEnabled())
	{
		return false;
	}
	CraftButton->SimulateClick();
	return true;
}
#endif

TSharedPtr<SWidget> SSarkoShelterWidget::WidgetToFocus() const
{
	return RaidButton;
}

FReply SSarkoShelterWidget::HandleEnterRaid()
{
	OnEnterRaid.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSarkoShelterWidget::HandleCraft()
{
	OnCraft.ExecuteIfBound();
	return FReply::Handled();
}
