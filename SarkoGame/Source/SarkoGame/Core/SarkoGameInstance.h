#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
// Included, not forward-declared: FSarkoProfile is a USTRUCT held by value below.
#include "Net/SarkoBackendClient.h"
// For ESarkoRaidOutcome, which is declared with the game state.
#include "Core/SarkoRaidGameState.h"

#include "SarkoGameInstance.generated.h"

/**
 * What the shelter says about the raid the player just finished.
 *
 * Spec §6.5 moves the EXTRACTED summary out of the raid HUD and into the
 * shelter, and a level travel destroys the game mode that knew the outcome — so
 * the outcome and the haul have to be handed to something that outlives the
 * travel. This is that something.
 */
USTRUCT()
struct FSarkoLastRaid
{
	GENERATED_BODY()

	UPROPERTY()
	ESarkoRaidOutcome Outcome = ESarkoRaidOutcome::InProgress;

	/** What was carried out. Empty for every losing outcome, by construction:
	 *  ASarkoRaidGameMode::FinishRaid clears the backpack before it writes the
	 *  outcome (SarkoRaid::OutcomeLosesHaul is the rule). */
	UPROPERTY()
	TArray<FSarkoItemStack> Haul;

	/**
	 * False when the raid ran offline or the result submission failed, so the
	 * shelter can say "не збережено" instead of showing a haul that the stash
	 * below it does not contain. Without this the two halves of the shelter
	 * screen contradict each other and the player is told the network is fine.
	 */
	UPROPERTY()
	bool bPersisted = false;
};

/**
 * The one object that survives a level travel.
 *
 * Registered through GameInstanceClass in DefaultEngine.ini (UGameMapsSettings
 * is config=Engine — putting it in DefaultGame.ini silently loads
 * /Script/Engine.GameInstance and everything below quietly stops existing).
 *
 * It owns three things, all of them things that must not be re-derived per level:
 *  - the backend client, and with it the JWT: one anonymous auth per app launch
 *    instead of one per shelter/raid trip;
 *  - the last fetched profile, so the shelter can draw immediately and refresh
 *    behind the drawing;
 *  - the last raid's outcome and haul.
 *
 * **What happens to an in-flight HTTP request when the player travels.** The
 * request itself is unaffected: FHttpModule owns it, and the client that started
 * it is now owned here rather than by the dying game mode, so it cannot be
 * destroyed mid-request and the completion always runs on a live client. What
 * dies is the *world-bound receiver* — every completion handler holds a
 * TWeakObjectPtr to the actor that asked (the raid game mode, the shelter's
 * player controller) and returns early once that is gone, so a reply that lands
 * after the travel is parsed, logged and dropped rather than written into a torn
 * down world. The one deliberate exception is FinishRaid's SubmitResult, which
 * captures the client strongly and does its logging inside the lambda, so the
 * raid's result is still submitted and still logged while the level is unloading.
 * Nothing is retried across a travel: the shelter re-fetches the profile on entry
 * (RecordRaidOutcome invalidates the cached one), which is cheaper and more
 * honest than resuming a request whose answer is already stale.
 */
UCLASS()
class USarkoGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/**
	 * The shared backend client, created on first use.
	 *
	 * Shared rather than owned outright by whoever asks: an HTTP completion can
	 * land after the world that started it is gone, and this object is what keeps
	 * the client alive across that boundary. ASarkoRaidGameMode::FinishRaid still
	 * captures it strongly in its own completion lambda — that is now belt and
	 * braces rather than the only thing holding it, and it stays because the
	 * lambda must outlive even this object during shutdown.
	 */
	TSharedPtr<class FSarkoBackendClient> EnsureBackend();

	/** Null when nothing has needed the backend yet. Never creates one. */
	TSharedPtr<class FSarkoBackendClient> GetBackend() const { return Backend; }

	/**
	 * The last profile fetched from /v1/profile. Read by the shelter (to draw)
	 * and by the raid (for bTutorialCompleted) — but the raid re-fetches rather
	 * than trusting this, because a raid may be entered directly from the command
	 * line with no shelter visit at all.
	 */
	UPROPERTY()
	FSarkoProfile CachedProfile;

	/** False until a profile has actually been fetched. Distinguishes "empty
	 *  stash" from "no idea yet", which the shelter draws differently. */
	UPROPERTY()
	bool bProfileLoaded = false;

	UPROPERTY()
	FSarkoLastRaid LastRaid;

	/** True once any raid has ended, so the shelter knows whether to draw a
	 *  summary block at all. InProgress is the "never raided" value. */
	bool HasFinishedARaid() const { return LastRaid.Outcome != ESarkoRaidOutcome::InProgress; }

	/** Called by ASarkoRaidGameMode as the raid settles, before it travels. */
	void RecordRaidOutcome(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul, bool bPersisted);

	/** Called by the shelter when a fresh profile lands. */
	void RecordProfile(const FSarkoProfile& Profile);

private:
	TSharedPtr<class FSarkoBackendClient> Backend;
};
