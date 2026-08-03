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
 * A raid result that has not been accepted by sarko-api yet.
 *
 * Everything /v1/raid/result needs and nothing else. The session TOKEN is in
 * here, which is why this file lives under Saved/ on the player's own device and
 * is deleted the moment the result lands: it is single-use, the server keeps
 * only its hash, and without it the haul cannot be claimed at all.
 */
USTRUCT()
struct FSarkoPendingResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString SessionId;

	UPROPERTY()
	FString SessionToken;

	/** The WIRE outcome ("extracted"/"died"/"mia"), already converted, so a
	 *  resubmission a launch later does not depend on an enum's numbering. */
	UPROPERTY()
	FString Outcome;

	UPROPERTY()
	TArray<FSarkoItemStack> Items;

	bool IsValid() const { return !SessionId.IsEmpty() && !SessionToken.IsEmpty() && !Outcome.IsEmpty(); }
};

namespace SarkoResult
{
	/**
	 * How long to wait before attempt N (0-based). Pure.
	 *
	 * Exponential from Base, capped at Max: 2, 4, 8, 16, 32, 60, 60… A fixed
	 * delay would hammer a server that is down, and no delay at all is what the
	 * old single attempt effectively was.
	 */
	float RetryDelaySeconds(int32 Attempt, float BaseSeconds, float MaxSeconds);

	/** The unsent result as JSON, and back. Pure — no disk — so the round trip is
	 *  tested without a filesystem, and so the shape is one function's business. */
	FString SerialisePending(const FSarkoPendingResult& Pending);
	bool ParsePending(const FString& Json, FSarkoPendingResult& Out, FString& OutError);

	/** Saved/SarkoPendingResult.json, absolute. */
	FString PendingResultPath();
}

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
	 * Picks up an unsent result left by a previous launch, and starts trying.
	 *
	 * A raid result is the only thing this game produces that cannot be recreated:
	 * the session token is one-time, the haul is gone from the world, and the
	 * session expires on the server and closes as `died`. So it is written to disk
	 * BEFORE the first attempt and only deleted once the server has taken it — an
	 * iOS suspension, a crash or a dead network during the request costs nothing
	 * but a delay.
	 */
	virtual void Init() override;
	virtual void Shutdown() override;

	/**
	 * Submits a raid result, and keeps trying.
	 *
	 * Called by ASarkoRaidGameMode::FinishRaid instead of talking to the client
	 * directly, because a raid ends seconds before the world is travelled away
	 * and a retry schedule cannot live in a dying game mode. Nothing waits on
	 * this: the return to the shelter is started independently and is never held
	 * hostage to the network.
	 */
	void SubmitRaidResultWithRetry(const FSarkoRaidSession& Session, const FString& WireOutcome,
		const TArray<FSarkoItemStack>& Items);

	/** True while a result is on disk waiting to be accepted. */
	bool HasPendingResult() const { return Pending.IsValid(); }

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

	/**
	 * A ВИЛАЗКА the SHELTER already started, waiting for the raid world to adopt
	 * (spec §4.5).
	 *
	 * Why the shelter starts it at all, when every other raid is started by the raid
	 * game mode: the granted kit only exists in /v1/raid/start's answer, and the kit is
	 * the point — "the variance is the appeal" is a reveal, and a reveal has to happen
	 * on a screen that has a character panel on it. Starting it in the raid world would
	 * mean the player never sees what they were lent until they are already holding it.
	 *
	 * It lives HERE and not on the shelter's controller because that controller is
	 * destroyed by the travel it triggers. This object is the one thing that survives.
	 *
	 * ADOPTED ONCE: ASarkoRaidGameMode::BeginRaidSession takes it with
	 * TakePendingSortie(), which clears it, so a session can never be confirmed twice
	 * or ride into a second raid. A sortie that is started and never travelled to is
	 * abandoned as `pending` and voided by the server's sweeper after PENDING_TTL —
	 * which costs nothing and does not burn the cooldown, because the cooldown counts
	 * only sorties that actually closed.
	 */
	UPROPERTY()
	FSarkoRaidSession PendingSortie;

	/** True when a started-but-not-yet-entered sortie is waiting. Reads the session id,
	 *  which is the one field a successful start always fills. */
	bool HasPendingSortie() const { return !PendingSortie.SessionId.IsEmpty(); }

	/** The pending sortie, cleared as it is handed over. Consuming rather than reading
	 *  is the whole guarantee that one granted kit becomes at most one raid. */
	FSarkoRaidSession TakePendingSortie();

private:
	TSharedPtr<class FSarkoBackendClient> Backend;

	/** The unsent result, mirrored on disk. Empty session id means none. */
	UPROPERTY()
	FSarkoPendingResult Pending;

	/** How many times this launch has tried. Reset when a new result is queued;
	 *  it is the input to the backoff and the bound on the loop. */
	int32 PendingAttempt = 0;

	/** Guard against two schedules running at once — a resume at Init and a fresh
	 *  raid result arriving would otherwise both hold a timer. */
	bool bPendingInFlight = false;

	FTimerHandle PendingRetryTimer;

	/** Writes Pending to Saved/, or removes the file when Pending is empty. */
	void PersistPending();

	/** One attempt: authenticate if needed, then POST. Reschedules itself on
	 *  failure until MaxPendingAttempts, and stops — the file survives, and the
	 *  next launch picks it up from Init. */
	void AttemptPendingSubmission();

	/**
	 * Retries per launch. Six attempts is 2+4+8+16+32+60 = about two minutes of
	 * trying, which covers every transient this has been seen to hit; beyond that
	 * the network is not transiently down and the disk copy is the answer.
	 */
	static constexpr int32 MaxPendingAttempts = 6;
	static constexpr float PendingRetryBaseSeconds = 2.f;
	static constexpr float PendingRetryMaxSeconds = 60.f;
};
