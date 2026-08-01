#include "Shelter/SarkoShelterWidget.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"
#include "UI/SarkoCellWidgets.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
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
	 * checkable by reading the code instead of by guessing at a device's pixel
	 * density.
	 *
	 * Turning 390x844 into 844x390 is not a rotation of the layout: 390 points of
	 * height is less than half of what the portrait column had, and a single
	 * stacked column does not fit in it at any legible font size. The screen is two
	 * columns now — see Construct.
	 */
	constexpr float DesignWidth = SarkoUI::DesignWidthPt;
	constexpr float DesignHeight = SarkoUI::DesignHeightPt;

	/**
	 * The horizontal margin, which on a landscape phone is a safe area and not a
	 * taste decision: rotated, an iPhone puts its Dynamic Island against the
	 * leading edge, and iOS reports that inset on **both** sides — 59 pt each on a
	 * 14 Pro — so that turning the phone the other way up does not reflow anything.
	 * 60 covers the largest inset current hardware reports, and the design canvas
	 * is close enough to 1:1 with a phone's points (min(2556/844, 1179/390) ≈ 3.02
	 * on a 14 Pro, and points are pixels/3) that a point constant is the right unit
	 * to say it in.
	 *
	 * On a desktop window it is simply a margin, which is what it looked like
	 * before at 18.
	 */
	constexpr float SideInset = 60.f;

	/**
	 * The bottom margin. The home indicator is 21 pt of the short edge in
	 * landscape; 26 clears it and leaves the buttons visibly above it rather than
	 * touching it.
	 *
	 * This replaces the portrait ButtonBottomInset of 150, which existed to push
	 * "В РЕЙД" up into the lower-middle third of an 844-point column. There is no
	 * lower-middle third of a 390-point one — 150 would be 38% of the screen, and
	 * the buttons would sit above the middle with the stash squeezed under them.
	 * In landscape both thumbs already rest at the bottom corners, which is where
	 * the buttons now are, so the rule the 150 was serving is met by the layout
	 * instead of by an inset.
	 */
	constexpr float BottomInset = 26.f;

	/**
	 * How the two columns split the width. The left column carries the outcome,
	 * the haul, the status line and the buttons; the right carries the stash.
	 *
	 * The stash gets the larger share because it is the list that can be any
	 * length and the one thing on this screen the player actually reads — in
	 * portrait it was a scroll box a few lines tall wedged between the garage line
	 * and the buttons.
	 */
	constexpr float LeftColumnFill = 0.42f;
	constexpr float RightColumnFill = 0.58f;

	/** The gutter between the two columns. */
	constexpr float ColumnGap = 28.f;

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
	 * near sRGB 0.24, i.e. mid-grey. The first version of this screen specified
	 * 0.05 for the background and rendered as a flat grey slab with barely any
	 * contrast against the text. Every constant here is chosen in linear space for
	 * the sRGB result named beside it.
	 *
	 * Suffixed "Colour" rather than named Ink/Body/Warn: these are file-scope in an
	 * anonymous namespace, and a unity build puts this file in the same translation
	 * unit as whatever else the blob happens to contain — a plain `Body` here made
	 * a local `Body` in BackendClientTest.cpp a -Wshadow error (and -Werror) purely
	 * because a test file grew and the blobs regrouped.
	 */
	const FSlateColor InkColour(FLinearColor(0.011f, 0.013f, 0.017f));   // ~#1c1f22, near-black
	const FSlateColor BrightColour(FLinearColor(0.92f, 0.92f, 0.88f));   // ~#f7f7f3, the title
	const FSlateColor BodyColour(FLinearColor(0.62f, 0.64f, 0.66f));     // ~#d0d3d5, stash rows
	const FSlateColor LabelColour(FLinearColor(0.22f, 0.23f, 0.25f));    // ~#8b8f94, section labels
	const FSlateColor WarnColour(FLinearColor(1.f, 0.55f, 0.06f));       // ~#ffc16a, the status line
	const FSlateColor RuleColour(FLinearColor(0.055f, 0.06f, 0.07f));    // ~#454a50, the hairlines

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
}

float SSarkoShelterWidget::UiScaleForViewport(FVector2D ViewportSize)
{
	// The rule itself now lives in UI/SarkoUiScale.h, because the in-raid HUD
	// needs the identical one: a player who can read "В РЕЙД" and then cannot read
	// the ammo count on the same phone is looking at a bug. Its behaviour is
	// unchanged — min of the two ratios, clamped — and Sarko.UI.PointScale pins
	// the two callers to the same numbers.
	return SarkoUI::PointScaleForViewport(ViewportSize);
}

float SSarkoShelterWidget::UiScale() const
{
	// Nothing here is scaled automatically. UMG runs every widget through
	// UUserInterfaceSettings' DPI curve; AddViewportWidgetContent does not — it
	// adds the widget to the viewport overlay at 1:1 Slate units, which are
	// physical pixels. On a 2556-pixel tall phone an unscaled 56-unit button is 56
	// physical pixels — under 19pt at 3x, a third of the 44pt tap target this
	// project's touch rule requires. So the scale is computed here instead, and it
	// is what turns the sizes in this file into the points they claim to be.
	//
	// Not UUserInterfaceSettings::GetDPIScaleBasedOnSize: its default rule is
	// ShortestSide against a curve tuned for desktop logical resolutions, which
	// returns ~0.67 for a 720-wide window and shrinks the menu on exactly the
	// form factor it has to be usable on.
	FVector2D ViewportSize(DesignWidth, DesignHeight);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	return UiScaleForViewport(ViewportSize);
}

void SSarkoShelterWidget::Construct(const FArguments& InArgs)
{
	OnEnterRaid = InArgs._OnEnterRaid;
	OnCraft = InArgs._OnCraft;

	// Logged once, at construction, because the whole layout is expressed in
	// points and this factor is the only thing turning it into pixels — a wrong
	// scale is a menu with unreachable buttons and nothing else says so.
	FVector2D ViewportSize(0.f, 0.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	UE_LOG(LogTemp, Display, TEXT("SarkoShelter: viewport %.0fx%.0f px, UI scale %.3f (canvas %.0fx%.0f pt)"),
		ViewportSize.X, ViewportSize.Y, UiScale(), DesignWidth, DesignHeight);

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
			.Padding(FMargin(SideInset, 16.f, SideInset, BottomInset))
			[
				SNew(SVerticalBox)

				// Header. The title and the garage line share one row rather than
				// stacking: 390 points of height is the scarce axis now, and the
				// garage line is a short label ("ГАРАЖ: ВЕЛОСИПЕД 1/3") that was
				// costing a whole row of it.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
					[
						SAssignNew(TitleText, STextBlock)
						.Font(ShelterFont(26.f))
						.ColorAndOpacity(BrightColour)
						.Text(FText::FromString(TEXT("УКРИТТЯ")))
					]

					// Right-aligned against the far edge, which is the horizontal
					// room landscape gained and portrait never had.
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
						.HAlign(HAlign_Right).Padding(16.f, 0.f, 0.f, 3.f)
					[
						SAssignNew(GarageText, STextBlock)
						.Font(ShelterFont(15.f))
						.ColorAndOpacity(BrightColour)
						.Justification(ETextJustify::Right)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 10.f)
				[
					HorizontalRule()
				]

				// The body: what the last raid did on the left, what the player
				// owns on the right. Two columns and not one, because a single
				// stacked column in 390 points of height fits the title, the
				// outcome and the buttons and leaves the stash about three lines.
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().FillWidth(LeftColumnFill)
						.Padding(0.f, 0.f, ColumnGap * 0.5f, 0.f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SAssignNew(OutcomeText, STextBlock)
							.Font(ShelterFont(17.f))
							.ColorAndOpacity(BrightColour)
						]

						// Scrolled, and it takes the column's slack: a long haul
						// used to push the status line and the buttons off the
						// bottom of a 390 pt canvas, and the garage block below is
						// three more rows plus a 48 pt button. FillHeight here does
						// the pushing the bare SSpacer used to do, and does it
						// without ever clipping.
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(HaulBox, SVerticalBox)
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
						[
							SAssignNew(GarageParts, SVerticalBox)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							// The payoff sentence. Empty until a craft succeeds, and
							// it stays for the rest of the visit rather than
							// flashing: this is what every raid before it was for.
							SAssignNew(CraftLineText, STextBlock)
							.Font(ShelterFont(13.f))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.85f, 0.40f)))
							.AutoWrapText(true)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(SBox)
							// 48 pt tall, past the 44 pt tap-target minimum. Width is
							// the left column's, so "НЕ ВИСТАЧАЄ: Мале колесо" fits
							// on one line at 13 pt rather than being ellipsed into a
							// button that no longer explains anything.
							.HeightOverride(48.f)
							[
								SAssignNew(CraftButton, SButton)
								.ContentPadding(FMargin(14.f, 0.f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.IsEnabled_Lambda([this]() { return bCraftEnabled; })
								.OnClicked(this, &SSarkoShelterWidget::HandleCraft)
								[
									SAssignNew(CraftLabel, STextBlock)
									.Font(ShelterFont(13.f))
									.Justification(ETextJustify::Center)
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								]
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 8.f)
						[
							// Wrapped, not one line: the status line carries a
							// verbatim backend error, and this column is narrower
							// than the whole screen was.
							SAssignNew(StatusText, STextBlock)
							.Font(ShelterFont(12.f))
							.ColorAndOpacity(WarnColour)
							.AutoWrapText(true)
						]

						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
							[
								SAssignNew(RaidButton, SButton)
								// Measured, not assumed: with ShelterFont(17) the
								// label is ~20 pt tall, so 2*18 of vertical padding
								// makes the button ~56 pt — past the 44 pt minimum
								// tap target with room to spare. Unchanged from the
								// portrait layout, because the rule is in points and
								// points did not move: the DPI scale is still
								// min(W/844, H/390) ≈ 3.02 on a 1179x2556 phone.
								.ContentPadding(FMargin(22.f, 18.f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								// An attribute, not a one-shot value: SetView flips
								// the flag and Slate re-reads it, so nothing has to
								// tick to keep the button honest.
								.IsEnabled_Lambda([this]() { return bRaidEnabled; })
								.OnClicked(this, &SSarkoShelterWidget::HandleEnterRaid)
								[
									SNew(STextBlock)
									.Font(ShelterFont(17.f))
									.Text(FText::FromString(TEXT("В РЕЙД")))
								]
							]

							+ SHorizontalBox::Slot().AutoWidth()
							[
								// Stub, and disabled rather than absent: spec §6.5
								// wants the shop visible so the shape of the shelter
								// is right, and it says "subscription later, no P2W"
								// — a button that did anything now would be a design
								// decision this stage has not made. Two lines of
								// ShelterFont(10) plus 2*12 of padding is ~48 pt,
								// which still clears the 44 pt rule.
								SNew(SButton)
								.ContentPadding(FMargin(12.f, 12.f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.IsEnabled(false)
								[
									SNew(STextBlock)
									.Font(ShelterFont(10.f))
									.Justification(ETextJustify::Center)
									.Text(FText::FromString(TEXT("МАГАЗИН\nНЕЗАБАРОМ")))
								]
							]
						]
					]

					+ SHorizontalBox::Slot().FillWidth(RightColumnFill)
						.Padding(ColumnGap * 0.5f, 0.f, 0.f, 0.f)
					[
						SNew(SVerticalBox)

						// A static section label. Not part of FSarkoShelterView
						// because it is a constant, not data — without it a bare
						// list of item lines has nothing saying what it is.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(STextBlock)
							.Font(ShelterFont(11.f))
							.ColorAndOpacity(LabelColour)
							.Text(FText::FromString(TEXT("СХОВОК")))
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							HorizontalRule()
						]

						// The stash is the SAME cell grid the raid's crate panel draws
						// (spec §2: one visual language for "things you own"), and it
						// can be any height — which is the reason this screen is Slate
						// and not DrawHUD primitives. It grows downward and scrolls; it
						// is never packed by the player.
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(StashBox, SBox)
								]
							]

							+ SOverlay::Slot()
							.HAlign(HAlign_Center).VAlign(VAlign_Top)
							.Padding(0.f, 24.f, 0.f, 0.f)
							[
								// Over the grid, not instead of it: an empty stash still
								// shows the shape it will fill.
								SAssignNew(StashNoteText, STextBlock)
								.Font(ShelterFont(15.f))
								.ColorAndOpacity(LabelColour)
							]
						]
					]
				]
			]
		]
	];
}

void SSarkoShelterWidget::SetView(const FSarkoShelterView& View)
{
	bRaidEnabled = View.bRaidEnabled;

	TitleText->SetText(FText::FromString(View.Title));
	OutcomeText->SetText(FText::FromString(View.OutcomeTitle));
	GarageText->SetText(FText::FromString(View.Garage.Title));
	CraftLabel->SetText(FText::FromString(View.Garage.CraftLabel));
	CraftLineText->SetText(FText::FromString(View.CraftLine));
	bCraftEnabled = View.Garage.bCanCraft;
	StatusText->SetText(FText::FromString(View.StatusLine));

	const auto Fill = [](const TSharedPtr<SVerticalBox>& Box, const TArray<FString>& Lines, float Size)
	{
		Box->ClearChildren();
		for (const FString& Line : Lines)
		{
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(STextBlock)
				.Font(ShelterFont(Size))
				.ColorAndOpacity(BodyColour)
				.Text(FText::FromString(Line))
			];
		}
	};

	Fill(HaulBox, View.HaulLines, 14.f);
	Fill(GarageParts, View.Garage.PartLines, 13.f);

	StashNoteText->SetText(FText::FromString(View.StashNote));

	// Rebuilt wholesale, once per profile fetch and once per craft — never per
	// frame. Slate is not a tick path.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const int32 Rows = SarkoGrid::StashRowsFor(View.StashStacks, Catalog,
		SarkoUI::StashColumns, SarkoUI::StashMinRows);
	const FSarkoGridPage Page{ SarkoUI::StashColumns, Rows };
	const TArray<FSarkoGridSlot> Slots = SarkoGrid::Place(View.StashStacks, Catalog, { Page });
	StashBox->SetContent(SarkoUI::BuildGridPage(View.StashStacks, Slots, /*PageIndex*/ 0,
		Page, *FSarkoInventoryStyles::Get()));
}

void SSarkoShelterWidget::SetCraftInFlight(bool bInFlight)
{
	// A second press while the first is in flight is a second debit. The button
	// is re-enabled by the SetView that follows the refetched profile, which is
	// also the moment the answer is actually known.
	if (bInFlight)
	{
		bCraftEnabled = false;
	}
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
	// Same reason as above: the engine's SimulateClick does not consult the
	// enabled state, so a scripted press would craft against a stash the button
	// itself was refusing.
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
