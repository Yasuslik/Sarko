#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

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

private:
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

	/** True between the press and the answer. A second debit is not undoable. */
	bool bCraftInFlight = false;
};
