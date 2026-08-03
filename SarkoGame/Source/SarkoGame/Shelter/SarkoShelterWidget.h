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
		/** Fired when the craft button is pressed. The controller owns the call. */
		SLATE_EVENT(FSimpleDelegate, OnCraft)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the changing text. Called once per profile fetch, never per frame. */
	void SetView(const FSarkoShelterView& View);

	/** Latches the craft button off while a craft is in flight. Re-enabled by the
	 *  SetView that follows the refetched profile, which is also the moment the
	 *  answer is actually known. */
	void SetCraftInFlight(bool bInFlight);

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
	 * Every size in the .cpp is authored against an 844x390 **landscape** canvas —
	 * a phone in **logical points**, not pixels — and scaled to the real viewport
	 * by this factor. Exposed so a test could pin the phone case without a
	 * viewport.
	 *
	 * min(W/844, H/390) so the whole canvas always fits: taking one axis alone
	 * would overflow the other, which for the status line means a backend error
	 * running off the side of the screen.
	 */
	static float UiScaleForViewport(FVector2D ViewportSize);

#if !UE_BUILD_SHIPPING
	/**
	 * Test-only: fires the raid button's **own** OnClicked handler, and only while
	 * that button is genuinely enabled — so nothing can start a raid the player
	 * could not have started by pressing it. Returns false while it is disabled
	 * (i.e. before the first /v1/profile has landed), which is what lets a caller
	 * poll instead of guessing at a delay.
	 *
	 * It exists because a headless run has no fingers: pressing a Slate button for
	 * real needs hit-testing against live geometry, and the shelter → raid hop is
	 * otherwise the one edge of the loop no automated run can cross. It is
	 * deliberately not a "travel to the raid" helper — going through the button's
	 * enabled state and its delegate is the whole point.
	 *
	 * SButton::SimulateClick is itself declared `#if !UE_BUILD_SHIPPING` in the
	 * engine, so this cannot survive into a shipping build even if this guard were
	 * removed: it would fail to link.
	 */
	bool SimulateEnterRaidClickIfEnabled();

	/** Same shape and same reasoning for the craft button: it goes through the
	 *  button's own enabled state and its delegate, so nothing can craft what the
	 *  player could not have crafted by pressing it. */
	bool SimulateCraftClickIfEnabled();
#endif

private:
	/** UiScaleForViewport of the live viewport, or 1 when there is no viewport. */
	float UiScale() const;

	FReply HandleEnterRaid();
	FReply HandleCraft();

	FSimpleDelegate OnEnterRaid;
	FSimpleDelegate OnCraft;

	/** True while the first profile fetch is in flight; the raid button reads it
	 *  through an attribute, so no per-frame work is needed to keep it in sync. */
	bool bRaidEnabled = false;

	TSharedPtr<class SButton> RaidButton;

	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> OutcomeText;
	TSharedPtr<STextBlock> GarageText;
	TSharedPtr<SVerticalBox> GarageParts;
	TSharedPtr<class SButton> CraftButton;
	TSharedPtr<STextBlock> CraftLabel;
	TSharedPtr<STextBlock> CraftLineText;
	TSharedPtr<STextBlock> StatusText;

	/** Read through an attribute by the craft button, so nothing has to tick to
	 *  keep it honest. False while a craft is in flight as well as when the parts
	 *  are short — a second press would be a second debit. */
	bool bCraftEnabled = false;

	/** Rebuilt wholesale on SetView. A few dozen rows, a few times per session. */
	TSharedPtr<SVerticalBox> HaulBox;

	/** "ЩЕ НЕ БУЛО РЕЙДІВ" under the ОСТАННІЙ РЕЙД heading, or empty once there
	 *  has been one. The left column's answer to StashNoteText: a labelled block
	 *  saying it is empty is composed, an unlabelled void is not. */
	TSharedPtr<STextBlock> HaulNoteText;

	/**
	 * The stash grid, not a list of lines: the same cell the raid's crate panel
	 * draws (spec §2, one visual language for "things you own"). An SBox rather
	 * than an SVerticalBox because SarkoUI::BuildGridPage returns one whole page
	 * and the box just holds it.
	 */
	TSharedPtr<class SBox> StashBox;

	/** "СХОВОК ПОРОЖНІЙ", drawn OVER the grid rather than instead of it, so an
	 *  empty stash still shows the shape it will fill. */
	TSharedPtr<STextBlock> StashNoteText;
};
