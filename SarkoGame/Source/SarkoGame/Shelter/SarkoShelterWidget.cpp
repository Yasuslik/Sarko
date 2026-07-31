#include "Shelter/SarkoShelterWidget.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/**
	 * The canvas every size in this file is authored against: a portrait phone in
	 * **logical points**, iPhone 14/15 sized. Points and not pixels on purpose —
	 * the touch rule this screen has to satisfy is written in points ("≥ 44 pt"),
	 * so authoring in the same unit makes the rule checkable by reading the code
	 * instead of by guessing at a device's pixel density.
	 */
	constexpr float DesignWidth = 390.f;
	constexpr float DesignHeight = 844.f;

	/**
	 * Keeps the raid button out of the bottom of the screen.
	 *
	 * The plan's touch rule puts "В РЕЙД" in the lower-middle third and never
	 * within 8% of an edge. 150 of the 844-point canvas is 17.8%, which lands the
	 * button's centre around 75% down — low enough to be a thumb's natural target,
	 * high enough that it is not where a thumb rests during a raid, and well clear
	 * of the iOS home indicator.
	 */
	constexpr float ButtonBottomInset = 150.f;

	/**
	 * The content column, in points, once the outer padding is taken off.
	 *
	 * Fixed rather than filling, so a landscape desktop window gets a centred
	 * phone-shaped column instead of a full-bleed strip of text across 1600 pixels
	 * with the stash lines a hand's width from the buttons. Portrait is unaffected:
	 * 390 - 2*18 is exactly what the column already got there.
	 */
	constexpr float ContentWidth = DesignWidth - 36.f;

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
	 */
	const FSlateColor Ink(FLinearColor(0.011f, 0.013f, 0.017f));   // ~#1c1f22, near-black
	const FSlateColor Bright(FLinearColor(0.92f, 0.92f, 0.88f));   // ~#f7f7f3, the title
	const FSlateColor Body(FLinearColor(0.62f, 0.64f, 0.66f));     // ~#d0d3d5, stash rows
	const FSlateColor Label(FLinearColor(0.22f, 0.23f, 0.25f));    // ~#8b8f94, section labels
	const FSlateColor Warn(FLinearColor(1.f, 0.55f, 0.06f));       // ~#ffc16a, the status line
	const FSlateColor Rule(FLinearColor(0.055f, 0.06f, 0.07f));    // ~#454a50, the hairlines

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
				.BorderBackgroundColor(Rule)
			];
	}
}

float SSarkoShelterWidget::UiScaleForViewport(FVector2D ViewportSize)
{
	if (ViewportSize.X < 1.f || ViewportSize.Y < 1.f)
	{
		return 1.f;
	}
	// min, not max: taking the larger ratio would overflow the canvas along the
	// other axis, which for the status line means a backend error running off the
	// side of the screen.
	return FMath::Clamp(
		FMath::Min(static_cast<float>(ViewportSize.X) / DesignWidth,
			static_cast<float>(ViewportSize.Y) / DesignHeight),
		0.5f, 6.f);
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
			// and a translucent menu over it reads as a rendering fault.
			SNew(SBorder)
			.BorderImage(FillBrush())
			.BorderBackgroundColor(Ink)
			.Padding(FMargin(18.f, 22.f, 18.f, ButtonBottomInset))
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(ContentWidth)
				[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SAssignNew(TitleText, STextBlock)
					.Font(ShelterFont(30.f))
					.ColorAndOpacity(Bright)
					.Text(FText::FromString(TEXT("УКРИТТЯ")))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
				[
					HorizontalRule()
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SAssignNew(OutcomeText, STextBlock)
					.Font(ShelterFont(17.f))
					.ColorAndOpacity(Bright)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
				[
					SAssignNew(HaulBox, SVerticalBox)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
				[
					SAssignNew(GarageText, STextBlock)
					.Font(ShelterFont(17.f))
					.ColorAndOpacity(Bright)
				]

				// A static section label. Not part of FSarkoShelterView because it
				// is a constant, not data — without it a bare list of item lines
				// under the garage line has nothing saying what it is.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.Font(ShelterFont(11.f))
					.ColorAndOpacity(Label)
					.Text(FText::FromString(TEXT("СХОВОК")))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					HorizontalRule()
				]

				// The stash can be any length, which is the reason this screen is
				// Slate and not DrawHUD primitives: SScrollBox is the whole feature.
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(StashBox, SVerticalBox)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 12.f)
				[
					// Wrapped, not one line: the status line carries a verbatim
					// backend error and on a 720-wide phone screen a single line of
					// it runs straight off the edge.
					SAssignNew(StatusText, STextBlock)
					.Font(ShelterFont(12.f))
					.ColorAndOpacity(Warn)
					.AutoWrapText(true)
				]

				// Buttons last, so they sit in the lower-middle third — reachable
				// by either thumb, and clear of the edges (ButtonBottomInset).
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().Padding(8.f)
					[
						SAssignNew(RaidButton, SButton)
						// Measured, not assumed: on a 1179x2556 render (iPhone 14 Pro
						// native, UI scale 3.02) this comes out 72pt tall and 165pt
						// wide, comfortably past the 44pt minimum tap target. The
						// horizontal padding is 22 and not more because at 30 the two
						// buttons together overran the canvas and sat 9pt from the
						// screen edge.
						.ContentPadding(FMargin(22.f, 18.f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						// An attribute, not a one-shot value: SetView flips the flag
						// and Slate re-reads it, so nothing has to tick to keep the
						// button honest.
						.IsEnabled_Lambda([this]() { return bRaidEnabled; })
						.OnClicked(this, &SSarkoShelterWidget::HandleEnterRaid)
						[
							SNew(STextBlock)
							.Font(ShelterFont(17.f))
							.Text(FText::FromString(TEXT("В РЕЙД")))
						]
					]

					+ SHorizontalBox::Slot().AutoWidth().Padding(8.f)
					[
						// Stub, and disabled rather than absent: spec §6.5 wants the
						// shop visible so the shape of the shelter is right, and it
						// says "subscription later, no P2W" — a button that did
						// anything now would be a design decision this stage has not
						// made. Two lines because one runs into the raid button on a
						// 720-wide screen.
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
			]
		]
	];
}

void SSarkoShelterWidget::SetView(const FSarkoShelterView& View)
{
	bRaidEnabled = View.bRaidEnabled;

	TitleText->SetText(FText::FromString(View.Title));
	OutcomeText->SetText(FText::FromString(View.OutcomeTitle));
	GarageText->SetText(FText::FromString(View.GarageLine));
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
				.ColorAndOpacity(Body)
				.Text(FText::FromString(Line))
			];
		}
	};

	Fill(HaulBox, View.HaulLines, 14.f);
	Fill(StashBox, View.StashLines, 15.f);
}

TSharedPtr<SWidget> SSarkoShelterWidget::WidgetToFocus() const
{
	return RaidButton;
}

FReply SSarkoShelterWidget::HandleEnterRaid()
{
	OnEnterRaid.ExecuteIfBound();
	return FReply::Handled();
}
