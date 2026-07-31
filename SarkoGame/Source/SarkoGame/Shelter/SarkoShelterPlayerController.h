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

	TSharedPtr<class SSarkoShelterWidget> Widget;

	/** The last failure, shown verbatim. Empty when everything is current. */
	FString LastError;
};
