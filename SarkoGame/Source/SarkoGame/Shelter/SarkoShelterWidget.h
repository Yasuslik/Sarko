#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Widgets/SCompoundWidget.h"

#include "Shelter/SarkoShelterView.h"

class STextBlock;
class SVerticalBox;
class SWidgetSwitcher;
struct FSarkoInventoryStyles;

/** Fired with the destination the player pressed. */
DECLARE_DELEGATE_OneParam(FSarkoOnSelectScreen, ESarkoShelterScreen);

/** Fired with the index, in the view's StashStacks, of the stash cell tapped. */
DECLARE_DELEGATE_OneParam(FSarkoOnEquipStack, int32);

/** Fired with the slot whose occupant was tapped. */
DECLARE_DELEGATE_OneParam(FSarkoOnUnequipSlot, ESarkoEquipSlot);

/**
 * The shelter hub.
 *
 * Slate built in C++, not UMG: a UMG widget is a .uasset and this project ships
 * no binary assets. Every style comes from FCoreStyle and from
 * FSarkoInventoryStyles' C++-constructed brushes — so this looks like a debug
 * menu, and that is the honest cost of "MVP shelter, zero assets".
 *
 * THREE SCREENS AS PEERS (spec §1), not one panel that grew. A left-edge column
 * of destinations chooses between them through an SWidgetSwitcher, so each screen
 * is a sibling with its own layout rather than another block appended to a
 * growing vertical box — which is what the garage was, wedged into a corner of
 * the stash screen with room for a recipe and one button.
 *
 * It owns no game state and decides nothing: SetView hands it an already-built
 * FSarkoShelterView and it writes those strings and rectangles into its widgets.
 * The only state it does own is ANIMATION — the refusal shake, glow and note —
 * which is transient, per-tap, and has no meaning outside a frame; the REASON a
 * refusal states is still decided by SarkoEquip::Accepts and handed in.
 */
class SSarkoShelterWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSarkoShelterWidget) {}
		/** Fired when "В РЕЙД" is pressed. The controller owns the travel. */
		SLATE_EVENT(FSimpleDelegate, OnEnterRaid)
		/**
		 * Fired when "ВИЛАЗКА" is pressed (spec §4.5). The controller owns everything
		 * that follows — the free start, the kit reveal and the travel.
		 *
		 * A SECOND event and not a parameter on OnEnterRaid, because the two are
		 * different requests to the server and the widget must not be the thing that
		 * chooses between them by inspecting its own state.
		 */
		SLATE_EVENT(FSimpleDelegate, OnEnterSortie)
		/** Fired when the craft button is pressed. The controller owns the call. */
		SLATE_EVENT(FSimpleDelegate, OnCraft)
		/** Fired when a destination in the left column is pressed. */
		SLATE_EVENT(FSarkoOnSelectScreen, OnSelectScreen)
		/** Fired when a stash cell is tapped — the equip half of spec §2's tap. */
		SLATE_EVENT(FSarkoOnEquipStack, OnEquipStack)
		/** Fired when an equipped slot is tapped — the unequip half. */
		SLATE_EVENT(FSarkoOnUnequipSlot, OnUnequipSlot)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the changing text and both grids. Called once per profile fetch and
	 *  once per screen switch, never per frame. */
	void SetView(const FSarkoShelterView& View);

	/** Latches the craft button off while a craft is in flight. Re-enabled by the
	 *  SetView that follows the refetched profile, which is also the moment the
	 *  answer is actually known. */
	void SetCraftInFlight(bool bInFlight);

	/**
	 * The refusal, all three signals at once, on the stash cell that was tapped:
	 * the cell SHAKES, the character panel's rim pulses AMBER, and the reason is
	 * SPELLED OUT under the figure.
	 *
	 * The same discipline the container panel has, and deliberately the same
	 * signals — a refusal that looks different in the shelter than in a raid is two
	 * languages for one idea. The reason is passed in rather than composed here:
	 * SarkoEquip::Accepts decides it, and this only draws it.
	 *
	 * StashIndex is which cell to shake; INDEX_NONE still glows and still states the
	 * reason, so a refusal with no obvious cell is never a silent one.
	 */
	void PlayRefusal(int32 StashIndex, const FString& Reason);

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
	 * SButton::SimulateClick is itself declared `#if !UE_BUILD_SHIPPING` in the
	 * engine, so this cannot survive into a shipping build even if this guard were
	 * removed: it would fail to link.
	 */
	bool SimulateEnterRaidClickIfEnabled();

	/** Same shape and same reasoning for the craft button: it goes through the
	 *  button's own enabled state and its delegate, so nothing can craft what the
	 *  player could not have crafted by pressing it. */
	bool SimulateCraftClickIfEnabled();

	/** Same shape again for the sortie button, so a headless screenshot run can take a
	 *  free run without a finger — and cannot take one the player could not have. */
	bool SimulateSortieClickIfEnabled();
#endif

private:
	/** UiScaleForViewport of the live viewport, or 1 when there is no viewport. */
	float UiScale() const;

	FReply HandleEnterRaid();
	FReply HandleEnterSortie();
	FReply HandleCraft();

	/** The three screens, built once in Construct and switched between. Separate
	 *  functions and not one 900-line Construct: they are peers, and a peer that is
	 *  a nested block of another peer's layout is a peer only on paper. */
	TSharedRef<SWidget> BuildInventoryScreen();
	TSharedRef<SWidget> BuildCharacterPanel();
	TSharedRef<SWidget> BuildGarageScreen();
	TSharedRef<SWidget> BuildShopScreen();

	FSimpleDelegate OnEnterRaid;
	FSimpleDelegate OnEnterSortie;
	FSimpleDelegate OnCraft;
	FSarkoOnSelectScreen OnSelectScreen;
	FSarkoOnEquipStack OnEquipStack;
	FSarkoOnUnequipSlot OnUnequipSlot;

	/**
	 * The process-wide styles, held as a shared ref for this widget's lifetime.
	 *
	 * SButton stores a raw `const FButtonStyle*` and points its brushes into it, so
	 * a style built on the stack inside Construct dangles the moment Construct
	 * returns — and the symptom is not a crash, it is a screen that draws garbage,
	 * intermittently, on a device. SSarkoInventoryPanel carries the same note.
	 */
	TSharedPtr<const FSarkoInventoryStyles> Styles;

	/** True while the first profile fetch is in flight; the raid button reads it
	 *  through an attribute, so no per-frame work is needed to keep it in sync. */
	bool bRaidEnabled = false;

	TSharedPtr<class SButton> RaidButton;
	TSharedPtr<STextBlock> RaidLabel;
	TSharedPtr<STextBlock> RaidSubLabel;

	/**
	 * ВИЛАЗКА, directly under В РЕЙД at the foot of the destination column (spec §4.5:
	 * "a second button beside В РЕЙД").
	 *
	 * Under and not beside, literally: the column is 126 pt wide and two buttons side
	 * by side in it would be 56 pt each, under the 44 pt tap minimum once padding is
	 * taken out — and "beside" in a left-edge column means "the next thing your thumb
	 * reaches", which is below. В РЕЙД stays the lower and larger of the two, because
	 * it is the verb the shelter serves and the sortie is the exception.
	 */
	TSharedPtr<class SButton> SortieButton;
	TSharedPtr<STextBlock> SortieLabel;
	TSharedPtr<STextBlock> SortieSubLabel;

	/** Read by the sortie button through an attribute. False during the cooldown and
	 *  before the first profile — a DISPLAY of the server's rule, never the rule (the
	 *  server refuses `sortie_cooldown` by name regardless). */
	bool bSortieEnabled = false;

	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> StatusText;

	/** The destination column, rebuilt on SetView: three buttons, the current one
	 *  marked. Rebuilt rather than restyled because "which one is current" is the
	 *  only thing that changes and there are three of them. */
	TSharedPtr<SVerticalBox> NavBox;

	/** ІНВЕНТАР / ГАРАЖ / МАГАЗИН as siblings. Index order matches
	 *  ESarkoShelterScreen. */
	TSharedPtr<SWidgetSwitcher> ScreenSwitcher;

	// ---- ІНВЕНТАР ------------------------------------------------------------

	/** The character plate, which is what the refusal glow is laid over. */
	TSharedPtr<class SBorder> CharacterPlate;
	TSharedPtr<STextBlock> CharacterTitle;

	/** One box per equipment slot, in SarkoEquip::Slots() order, refilled by
	 *  SetView. An SBox because the slot's rectangle changes with what is in it. */
	TArray<TSharedPtr<class SBox>> SlotBoxes;
	TArray<TSharedPtr<STextBlock>> SlotCaptions;

	/** The inline 2x2 carry page and the sentence saying why it is empty here. */
	TSharedPtr<class SBox> PocketsBox;
	TSharedPtr<STextBlock> PocketsCaption;
	TSharedPtr<STextBlock> PocketsNote;

	/** "ПМ — НЕ ЗБРОЯ" for the 240 ms of a refusal, then empty. Under the figure,
	 *  where the refused thing was headed. */
	TSharedPtr<STextBlock> RefusalNote;

	/**
	 * The stash grid, not a list of lines: the same cell the raid's crate panel
	 * draws (spec §2, one visual language for "things you own"). Every cell is now
	 * TAPPABLE — that is the equip gesture — where before the stash was inert.
	 */
	TSharedPtr<class SBox> StashBox;
	TSharedPtr<STextBlock> StashNoteText;

	// ---- ГАРАЖ ---------------------------------------------------------------

	TSharedPtr<STextBlock> GarageText;
	TSharedPtr<SVerticalBox> GarageParts;
	TSharedPtr<SVerticalBox> GarageLadder;
	TSharedPtr<class SButton> CraftButton;
	TSharedPtr<STextBlock> CraftLabel;
	TSharedPtr<STextBlock> CraftLineText;

	/** Read through an attribute by the craft button, so nothing has to tick to
	 *  keep it honest. False while a craft is in flight as well as when the parts
	 *  are short — a second press would be a second debit. */
	bool bCraftEnabled = false;

	// ---- ОСТАННІЙ РЕЙД (on ІНВЕНТАР, under the stash) -----------------------

	TSharedPtr<STextBlock> OutcomeText;
	TSharedPtr<SVerticalBox> HaulBox;
	TSharedPtr<STextBlock> HaulNoteText;

	// ---- the refusal ---------------------------------------------------------

	/**
	 * 240 ms, matching SSarkoInventoryPanel::RefusalCurve exactly. Nothing loops:
	 * FCurveSequence::Play registers an active timer, and a looping one would hold
	 * it open for as long as the screen is up — a frame of Slate work every frame
	 * for something that is not moving.
	 */
	FCurveSequence RefusalCurve{ 0.f, 0.240f, ECurveEaseFunction::Linear };

	/** Which stash cell was refused. Read by an attribute, so nothing ticks. */
	int32 RefusedStashIndex = INDEX_NONE;

	/** +-4 pt over two cycles on the refused cell, starting and ending at rest. */
	TOptional<FSlateRenderTransform> StashCellTransform(int32 StackIndex) const;

	/** The amber rim on the character plate while a refusal plays, as a choice of
	 *  baked brush — an animated tint cannot draw a transparent-bodied rim at all
	 *  (FSarkoInventoryStyles::RefusalGlow says why). */
	const FSlateBrush* CharacterPlateBrush() const;
};
