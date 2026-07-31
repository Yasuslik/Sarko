#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Shelter/SarkoShelterView.h"

class STextBlock;
class SVerticalBox;

/**
 * The shelter menu.
 *
 * Slate built in C++, not UMG: a UMG widget is a .uasset and this project ships
 * no binary assets. Every style comes from FCoreStyle, which is compiled into
 * SlateCore — so this looks like a debug menu, and that is the honest cost of
 * "MVP shelter, zero assets" (see the plan's Decision 1).
 *
 * It owns no state and decides nothing: SetView hands it an already-built
 * FSarkoShelterView and it writes those strings into its text blocks. That is
 * what keeps every rule in this screen testable under -nullrhi, where no Slate
 * application exists.
 */
class SSarkoShelterWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSarkoShelterWidget) {}
		/** Fired when "В РЕЙД" is pressed. The controller owns the travel. */
		SLATE_EVENT(FSimpleDelegate, OnEnterRaid)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the changing text. Called once per profile fetch, never per frame. */
	void SetView(const FSarkoShelterView& View);

	/**
	 * What FInputModeUIOnly should focus: the raid button, not this widget.
	 *
	 * SCompoundWidget is not focusable, and handing `this` to SetWidgetToFocus
	 * logs `InputMode:UIOnly - Attempting to focus Non-Focusable widget` at Error
	 * on every boot and then focuses nothing. Focusing the button also makes
	 * Enter/gamepad-accept start a raid, which is the one thing this screen does.
	 */
	TSharedPtr<SWidget> WidgetToFocus() const;

	/**
	 * Every size below is authored against a 720x1280 portrait canvas and scaled
	 * to the real viewport by this factor. Exposed so a test could pin the phone
	 * case without a viewport.
	 *
	 * min(W/720, H/1280) so the whole canvas always fits: taking height alone
	 * would overflow a landscape window sideways.
	 */
	static float UiScaleForViewport(FVector2D ViewportSize);

private:
	/** UiScaleForViewport of the live viewport, or 1 when there is no viewport. */
	float UiScale() const;

	FReply HandleEnterRaid();

	FSimpleDelegate OnEnterRaid;

	/** True while the first profile fetch is in flight; the raid button reads it
	 *  through an attribute, so no per-frame work is needed to keep it in sync. */
	bool bRaidEnabled = false;

	TSharedPtr<class SButton> RaidButton;

	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> OutcomeText;
	TSharedPtr<STextBlock> GarageText;
	TSharedPtr<STextBlock> StatusText;

	/** Rebuilt wholesale on SetView. A few dozen rows, a few times per session. */
	TSharedPtr<SVerticalBox> HaulBox;
	TSharedPtr<SVerticalBox> StashBox;
};
