#pragma once

#include "CoreMinimal.h"
#include "Core/SarkoRaidGameState.h"
#include "GameFramework/GameModeBase.h"
// Included, not forward-declared: SarkoExtract::FSarkoDwell is the value type of
// the Dwells map below, so the complete type has to be visible here.
#include "Loot/SarkoExtractionZone.h"
#include "Map/SarkoMapDefinition.h"
// Included, not forward-declared: FSarkoRaidSession is a USTRUCT held by value
// below, so the full type has to be here.
#include "Net/SarkoBackendClient.h"

#include "SarkoRaidGameMode.generated.h"

/**
 * Server-only raid authority. Loads the hand-authored map, spawns the bots
 * against it and starts the clock.
 *
 * The seed it carries comes from sarko-api's raid/start response and is handed
 * to the game state to replicate. It is the shared basis for
 * server-authoritative rolls (loot, in a later plan) — not for the map, which
 * every machine reads from the same shipped data file.
 */
UCLASS()
class ASarkoRaidGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASarkoRaidGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

	/**
	 * Places the player at one of the map file's authored player spawns instead
	 * of the default flow's PlayerStart search. There are no PlayerStart actors in the level
	 * because there is no authored level, so this bypasses
	 * FindPlayerStart/ChoosePlayerStart entirely (both require handing back an
	 * AActor, which would mean spawning a throwaway marker actor purely to
	 * carry a location) and instead calls RestartPlayerAtTransform directly —
	 * the engine's own transform-based spawn path, used symmetrically with the
	 * PlayerStart-based one.
	 */
	virtual void RestartPlayer(AController* NewPlayer) override;

	/**
	 * Advances every player's extraction dwell and expires the raid clock. The
	 * server's own copy of each pawn's location is what is measured — a client
	 * never gets to claim it is standing in a zone (same discipline as the loot
	 * channel).
	 */
	virtual void Tick(float DeltaSeconds) override;

	/** Called by the player pawn's death handler, server side. */
	void HandlePlayerDied(class ASarkoCharacter* Pawn);

	/**
	 * Ends the raid exactly once. Freezes input — on the server, by disabling
	 * every player pawn's movement, not merely by asking the client to stop
	 * sending — and (from Task 8) submits the result to the backend. Idempotent:
	 * a death on the same frame the clock expires must not submit twice, and the
	 * backend's idempotency is a safety net, not a licence.
	 */
	void FinishRaid(ESarkoRaidOutcome NewOutcome);

	/** Shared basis for server-authoritative rolls, replicated through the game state. Read from the `Seed` URL option when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Raid")
	int32 Seed = 1;

	/**
	 * The server's half of every loot roll. Generated in InitGame, **never
	 * replicated, never sent anywhere.**
	 *
	 * Without it the loot map is free: `Seed` is replicated (it is what tells a
	 * client the raid has begun), the loot tables ship inside the build, and
	 * SarkoLoot::RollContainer is a pure function — so `Seed ^ ContainerIndex`
	 * lets any client enumerate all 42 containers' contents before walking to one.
	 * With it, a container's stream seed is unknowable off the authority, and the
	 * only way contents reach a client is the owner-only backpack it just filled.
	 *
	 * A deliberately plain member and not a UPROPERTY: a game mode instance exists
	 * only on the server (AGameModeBase never replicates and is never spawned on a
	 * client), so there is no replication path to forget to exclude, and being a
	 * non-UPROPERTY means it also stays out of anything that walks reflected
	 * properties — a save game, a network dump, a `DumpAllProperties`.
	 *
	 * Not derived from Seed, and not seeded from the raid clock: both are things a
	 * client knows. Regenerated per raid, so nothing learned from one raid carries
	 * into the next — which is also the bound on how strong this is, since 64 bits
	 * of salt raise the cost of an offline sweep from one observed roll to a few
	 * rather than making it impossible (see SarkoLoot::ContainerSeed).
	 */
	int64 LootSalt = 0;

	/**
	 * Whether this raid uses the tutorial's static loot (spec §6.5).
	 *
	 * Set from GET /v1/profile's `tutorial_completed` before the raid goes live,
	 * or from USarkoRaidSettings::bOfflineTutorialLoot when there is no profile.
	 *
	 * A deliberately plain member and **not** a UPROPERTY, for exactly the reasons
	 * LootSalt is one: a game mode exists only on the server, so there is no
	 * replication path to forget to exclude, and a non-UPROPERTY also stays out of
	 * anything that walks reflected properties. It matters here because a client
	 * that knew the raid was in tutorial mode could read the authored layout
	 * straight out of its own copy of the map file — fixedItems is shipped data,
	 * unlike a roll, which needs the server-only salt.
	 */
	bool bTutorialLoot = false;

	/**
	 * Every container that has been opened this raid, keyed by container index,
	 * holding what is still in it.
	 *
	 * On the game mode, and a plain member rather than a UPROPERTY, for exactly
	 * the reasons LootSalt is: a game mode exists only on the server, so there is
	 * no replication path to forget to exclude and nothing that walks reflected
	 * properties can dump it. Spec §3's "a client sees a container's contents
	 * only after opening it, never before" is therefore a structural fact rather
	 * than a rule someone has to remember — the only way contents reach a client
	 * is ASarkoCharacter::ClientContainerContents, and only for an index that
	 * client has successfully opened.
	 *
	 * A TMap and not a sized array: key presence IS "this has been rolled", which
	 * is the spec's "rolled once, on first open" with no second flag to keep in
	 * step and no sizing hook to get wrong.
	 */
	TMap<int32, TArray<FSarkoItemStack>> ContainerInventories;

	/** Server only. Null for an index that has never been opened. */
	TArray<FSarkoItemStack>* FindContainerInventory(int32 ContainerIndex);

	/**
	 * Rolls a container's contents if this is its first open, stores them, and
	 * marks it Opened. Returns null and logs if the index is out of range or the
	 * tier has no table. Idempotent: a second open returns the SAME array, so a
	 * player who walks away and comes back sees what they left.
	 */
	TArray<FSarkoItemStack>* OpenContainerAt(int32 ContainerIndex);

	/** The layout this raid was loaded from; pawns spawn against it. */
	FSarkoMapLayout CachedLayout;

	/**
	 * The hand-authored definition this raid was loaded from. Populated in
	 * InitGame, before any login can occur; StartPlay reads its
	 * RaidDurationSeconds and hands it to the game state so props spawn from
	 * the same source the geometry does.
	 */
	FSarkoMapDefinition CachedDefinition;

private:
	/**
	 * Kicks off auth → start → confirm. Called from the tail of StartPlay, **not**
	 * from BeginPlay: AGameModeBase::StartPlay dispatches BeginPlay to every actor
	 * including this one, so a BeginPlay override runs *inside* Super::StartPlay()
	 * — before this mode has spawned the layout and the bots. Activating a raid
	 * that early is the same InitGame-vs-StartPlay ordering trap that already bit
	 * the player-spawn path.
	 */
	void BeginBackendSession();

	/**
	 * Everything after auth: start, confirm and ActivateRaid. Split out of
	 * BeginBackendSession because auth is now skipped whenever the player arrived
	 * from the shelter with a live JWT (the client and its token ride
	 * USarkoGameInstance across the travel), so the rest of the chain has two
	 * entry points — one synchronous, one from the auth completion.
	 */
	void OnAuthenticated();

	/** Sets bTutorialLoot and logs what the map can actually deliver in that mode. */
	void SetTutorialLoot(bool bEnabled);

	/** /v1/raid/start -> /v1/raid/confirm -> ActivateRaid. Split from OnAuthenticated
	 *  so the profile hop sits between auth and the session opening. */
	void BeginRaidSession();

	/** Everything after a failed call: log, use the local seed, keep playing (spec §4.6). */
	void FallBackToOfflineRaid(const FString& Reason);

	/** Marks the seed authoritative, starts the clock and lets containers open. */
	void ActivateRaid(int32 AuthoritativeSeed, float ClockSeconds);

	/**
	 * Hands the outcome and haul to the game instance and schedules the trip back
	 * to the shelter. Called once, from the tail of FinishRaid, on every path that
	 * writes an outcome — a path that writes one and does not call this strands the
	 * player on the outcome banner forever.
	 *
	 * The travel is on a timer rather than immediate so the outcome banner is
	 * visible where the raid ended, and the timer is a weak-lambda delegate
	 * because it is scheduled on a game mode that a travel is about to destroy.
	 */
	void ReturnToShelter(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul);

	/**
	 * The map file's own raid duration, or the settings default when the map
	 * carried none (a bridge.json that failed to load reads as zero, and a
	 * zero-second clock is a raid that expires into MIA on the spawn frame).
	 */
	float MapClockSeconds() const;

	/** Shared, because the client is used across several async callbacks. */
	TSharedPtr<class FSarkoBackendClient> Backend;

	/** Empty session id means offline: nothing to submit and nothing to save. */
	FSarkoRaidSession Session;

	/** The result goes out exactly once per raid; the backend's idempotency is a net, not a licence. */
	bool bSessionSubmitted = false;

	/** So the loud offline line is logged once per raid rather than per failed hop. */
	bool bOfflineDegraded = false;

	/** Round-robins through CachedLayout.PlayerStarts across spawns and respawns. */
	int32 NextPlayerStartIndex = 0;

	/**
	 * Server-side per-pawn extraction progress, keyed weakly so a destroyed pawn's
	 * entry cannot keep it alive; stale entries are pruned when their key goes
	 * stale rather than left to accumulate.
	 *
	 * The value carries the zone it belongs to, so progress cannot leak across a
	 * zone boundary. Cleared wholesale in ActivateRaid, which makes the frame the
	 * raid goes live an entry frame for every pawn.
	 */
	TMap<TWeakObjectPtr<class ASarkoCharacter>, SarkoExtract::FSarkoDwell> Dwells;
};
