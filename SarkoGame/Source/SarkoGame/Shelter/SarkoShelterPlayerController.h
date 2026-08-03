#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

// For ESarkoShelterScreen and ESarkoEquipSlot, both held by value below.
#include "Shelter/SarkoShelterView.h"

#include "SarkoShelterPlayerController.generated.h"

/**
 * Owns the shelter widget, fetches the profile, and starts the raid.
 *
 * The widget lives here rather than on the game mode because a viewport widget
 * belongs to a local player, and because EndPlay is the one place guaranteed to
 * run before a level travel — a widget that is not removed there survives into
 * the raid and covers the HUD, with the raid still fully playable underneath it.
 */
UCLASS()
class ASarkoShelterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASarkoShelterPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Screenshots the menu as it actually renders, DelaySeconds in — long enough
	 * by default that the profile fetch has landed and the stash is real; a short
	 * delay instead photographs the "З'ЄДНАННЯ..." state, which is the other half
	 * of what has to be legible.
	 *
	 * `Shot showui` and not `HighResShot`: HighResShot goes through the scene
	 * renderer and captures no Slate at all, which for a screen that is *entirely*
	 * Slate is a black PNG. Scripts/shelter-shot.sh is the driver.
	 *
	 * UFUNCTION(Exec) cannot live inside an #if — UHT rejects it — so it is
	 * declared unconditionally and the body is guarded (ASarkoPlayerController's
	 * SarkoOverview is the precedent).
	 *
	 * BeginPlay also calls this itself when the command line carries
	 * `-SarkoShelterShot=<seconds>`, which is the only way to photograph the
	 * shelter the *raid returns to*: -ExecCmds is queued once at engine init and
	 * never re-run for the world a travel loads.
	 */
	UFUNCTION(Exec)
	void SarkoShelterShot(float DelaySeconds = 6.f);

	/**
	 * Debug only: drops a bicycle's worth of parts into the **cached client-side**
	 * profile DelaySeconds in, so the craft button's enabled state can be
	 * photographed.
	 *
	 * It writes nothing to the backend and crafts nothing — pressing the button
	 * afterwards would still go to the real /v1/garage/craft and be refused with
	 * insufficient_items, which is correct: this fakes the *readout*, never the
	 * entitlement. ASarkoPlayerController::SarkoDebugLoot is the precedent, and
	 * the reason is the same one — a headless run has no way to earn a state that
	 * takes three raids.
	 *
	 * Delayed rather than immediate because -ExecCmds is queued at engine init and
	 * runs before the first /v1/profile lands; applied at t=0 the fetch's
	 * RecordProfile would overwrite it a second later.
	 *
	 * UFUNCTION(Exec) cannot live inside an #if — UHT rejects it — so it is
	 * declared unconditionally and the body is guarded.
	 */
	UFUNCTION(Exec)
	void SarkoDebugParts(float DelaySeconds = 4.f);

	/**
	 * Debug only: drops a MIXED haul into the cached client-side profile so the
	 * stash grid can be photographed with something in it.
	 *
	 * Same shape and the same honesty as SarkoDebugParts above — it writes nothing
	 * to the backend and entitles nothing; it fakes the readout so a headless run
	 * can answer "does this grid look like the crate panel's grid", which is a
	 * question only a frame someone reads can settle. Deliberately spans every
	 * category the palette has a hue for and includes 2x1, 2x2 and 3x2 items, so
	 * the frame also answers whether a multi-cell item draws as ONE box.
	 *
	 * bShortAPart leaves ONE wheel where the recipe wants two, which is the only
	 * way to photograph the garage block's other state: a mixed stash otherwise
	 * satisfies the whole bicycle, so `SarkoDebugStash 1` can only ever show every
	 * part met and the "НЕ ВИСТАЧАЄ: <part>" half of spec §3 goes unphotographed.
	 * It changes a quantity and not the item list, so the grid in the frame is the
	 * same grid either way and the two shots are comparable.
	 */
	UFUNCTION(Exec)
	void SarkoDebugStash(float DelaySeconds = 4.f, bool bShortAPart = false);

	/**
	 * Debug only: switches the hub to a screen by index (0 ІНВЕНТАР, 1 ГАРАЖ,
	 * 2 МАГАЗИН), DelaySeconds in, so a headless run can photograph a screen a
	 * finger would have had to reach.
	 *
	 * It goes through the same SelectScreen the nav buttons fire, so what it
	 * photographs is what a tap produces and not a second code path.
	 */
	UFUNCTION(Exec)
	void SarkoDebugScreen(int32 ScreenIndex = 0, float DelaySeconds = 4.f);

	/**
	 * Debug only: fills the **cached** equipment so the character panel can be
	 * photographed with something in every slot, DelaySeconds in.
	 *
	 * Same honesty as SarkoDebugStash and SarkoDebugParts, and the same limit: it
	 * writes nothing to the backend and entitles nothing. Pressing В РЕЙД afterwards
	 * sends this as the loadout and /v1/raid/start debits it from the REAL stash,
	 * refusing what is not there — which is correct, and is why this fakes the
	 * readout and never the entitlement.
	 *
	 * It equips through SarkoEquip::Accepts, not by assignment, so a frame taken
	 * this way is a frame of the real rules. bRefuse instead taps a deliberately
	 * WRONG item into a slot, which is the only way a headless run can photograph
	 * the refusal — the rule and the reason are the shipped ones.
	 */
	UFUNCTION(Exec)
	void SarkoDebugEquip(float DelaySeconds = 4.f, bool bRefuse = false);

private:
	/** Switches the hub's screen and redraws. What the nav buttons fire. */
	void SelectScreen(ESarkoShelterScreen Screen);

	/**
	 * A tap on a stash cell: equip it, if a slot will take it.
	 *
	 * The rule is SarkoEquip::Accepts and the refusal is its reason, drawn by the
	 * widget's three signals. On success the cached profile is updated immediately
	 * — so the tap feels instant — and the server is told; a refusal from the server
	 * replaces the local answer, because the server is the authority on what this
	 * player owns.
	 */
	void EquipStack(int32 StackIndex);

	/** A tap on an equipped slot: take it off. Always legal — an empty slot is legal
	 *  for every slot, including the weapon (spec §4's dead-end guard). */
	void UnequipSlot(ESarkoEquipSlot Slot);

	/** POST /v1/profile/equipment, then reconcile. Shared by both taps. */
	void SendEquipment(ESarkoEquipSlot Slot, FName Item);
	/** Rebuilds the view from the game instance's state and hands it to the widget. */
	void RefreshWidget();

	/** Authenticates if needed, then GETs /v1/profile. One round trip per entry. */
	void FetchProfile();

	void EnterRaid();

	/** POST /v1/garage/craft, then refetch the profile. The server decides which
	 *  tier is next and debits the parts in one transaction, so there is nothing
	 *  for this side to compute and nothing to patch into the cached profile. */
	void Craft();

#if !UE_BUILD_SHIPPING
	/**
	 * `-SarkoAutoRaid=<seconds>`: presses "В РЕЙД" for a run that has no fingers.
	 *
	 * Debug-only and test-only, and it does not travel by itself — it polls the
	 * widget's own button until that button is enabled and then fires the button's
	 * OnClicked, so a headless run crosses the shelter → raid hop through exactly
	 * the path a player's thumb uses. What it still cannot prove is Slate
	 * hit-testing: that a press landing on those pixels reaches this button.
	 *
	 * Polls rather than firing once at DelaySeconds because the button is gated on
	 * the first /v1/profile, and a fixed delay would race the network.
	 */
	void StartAutoRaid(float DelaySeconds);
	void TryAutoRaid();

	FTimerHandle AutoRaidTimer;
	int32 AutoRaidAttempts = 0;
#endif

	TSharedPtr<class SSarkoShelterWidget> Widget;

	/** The last failure, shown verbatim. Empty when everything is current. */
	FString LastError;

	/** "ЗІБРАНО. ВІДКРИТО: SWAMP", kept for the rest of this shelter visit. */
	FString LastCraftLine;

	/** Which destination is showing. ІНВЕНТАР by default (spec §1), and it lives
	 *  here rather than in the widget because the widget decides nothing — it is an
	 *  input to BuildView like every other piece of state on this screen. */
	ESarkoShelterScreen CurrentScreen = ESarkoShelterScreen::Inventory;

	/** True between the press and the answer. A second debit is not undoable. */
	bool bCraftInFlight = false;

	/**
	 * Debug only, and set by nothing else: SarkoDebugEquip raises it so its taps go
	 * through the real rule but not through the network.
	 *
	 * It exists because the debug stash is a FAKE cached profile, so the server would
	 * refuse most of what a tap into it equips — and a refusal calls FetchProfile,
	 * which would replace the faked stash with the real one halfway through the
	 * shot. Suppressing the send is the only part of the flow this can afford to skip:
	 * SarkoEquip::Accepts, the refusal, the view and the frame are all the shipped
	 * ones. Nothing is entitled either way — /v1/raid/start still debits the real
	 * stash and refuses what is not in it.
	 */
	bool bDebugSuppressEquipSend = false;
};
