#pragma once

#include "CoreMinimal.h"
// Included, not forward-declared: SarkoEncounter::FEncounterRuntime is held by
// value in the array below, so the complete type has to be visible here.
#include "Core/SarkoEncounters.h"
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
	 * Server only: puts the profile's equipped weapon in the fresh pawn's hand.
	 *
	 * Here rather than in the pawn's own BeginPlay because the pawn cannot know:
	 * the equipment lives on USarkoGameInstance, which is the host's cache of
	 * ITS OWN player's profile — asking for it from inside a pawn would give
	 * every pawn on a listen server the host's gun.
	 */
	void ShowEquippedWeapon(AController* NewPlayer);

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

	/**
	 * What is left of this raid's enemy allowance.
	 *
	 * A plain member and **never a UPROPERTY**, for exactly the reasons LootSalt
	 * is one: a game mode instance exists only on the server, so there is no
	 * replication path to forget to exclude, and a non-UPROPERTY also stays out
	 * of anything that walks reflected properties. It matters here because "how
	 * many enemies are left in this map" is the single most valuable thing a
	 * client could know in a game whose whole tension is not knowing.
	 *
	 * Public so a test and the log can read it; nothing outside this class
	 * writes it.
	 */
	int32 EncounterBudgetRemaining = 0;

	/** How many encounters have spent budget this raid. Zero means the next one is the first fight. */
	int32 EncountersFired = 0;

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
	 *  so the profile hop sits between auth and the session opening.
	 *
	 *  It ADOPTS a ВИЛАЗКА the shelter already started (USarkoGameInstance's
	 *  PendingSortie) instead of starting one of its own — the granted kit only exists
	 *  in the start response, and the reveal it feeds needs the shelter's character
	 *  panel. */
	void BeginRaidSession();

	/** /v1/raid/confirm -> ActivateRaid, for a session that is already open — whichever
	 *  side opened it. One copy, because everything after the start is identical for a
	 *  raid and for a ВИЛАЗКА: the deadline is the server's and the clock is derived
	 *  from it, so two copies would be two places for that rule to drift. */
	void ConfirmAdoptedSession();

	/** Everything after a failed call: log, use the local seed, keep playing (spec §4.6). */
	void FallBackToOfflineRaid(const FString& Reason);

	/** Marks the seed authoritative, starts the clock and lets containers open. */
	void ActivateRaid(int32 AuthoritativeSeed, float ClockSeconds);

public:
	/**
	 * A pawn has crossed KillZ. Put it back on the nearest player spawn, shout
	 * about it, and let the raid continue.
	 *
	 * Public because both pawn classes call it from their own FellOutOfWorld —
	 * ASarkoCharacter and ASarkoEnemyCharacter derive from ACharacter separately,
	 * so there is no shared base to hang this on and duplicating the body would
	 * be two nets that drift apart.
	 *
	 * Returns false when it could not recover (no authority, or no layout), in
	 * which case the caller must fall through to the engine's own behaviour: a
	 * pawn that is neither destroyed nor moved falls forever, which is the bug
	 * this exists to fix arriving by a different door.
	 */
	bool RecoverFallenPawn(APawn& Pawn);

private:

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

	/**
	 * Starts the encounter system for this raid: picks the tutorial or normal
	 * budget, sizes the runtime array and starts the 0.25 s evaluation timer.
	 *
	 * Called from ActivateRaid rather than StartPlay because the budget depends
	 * on bTutorialLoot, which is not known until the profile has been read — and
	 * because a raid that is not live yet must not be spawning enemies at a
	 * player who cannot act.
	 */
	void InitialiseEncounters();

	/**
	 * The whole encounter system, run on a timer under HasAuthority(). Arms
	 * triggers, spends budget and places enemies; every rule it applies is a
	 * pure function in SarkoEncounter, and everything it needs from the world
	 * (distance, line of sight, spawning) it does here.
	 */
	void EvaluateEncounters();

	/**
	 * True when nothing solid stands between the two points, i.e. when a bot
	 * created at From would be looking straight at To. The spawn-placement test:
	 * a point that has line of sight to the player is a point that must not be
	 * used, however far away it is.
	 */
	bool HasLineOfSightBetween(const FVector& From, const FVector& To, const AActor* IgnoreActor) const;

	/** The nearest living player pawn, or null. The encounter system measures against this. */
	class APawn* FindNearestLivingPlayerPawn() const;

	/**
	 * WHO THE PLAYER CAN SEE — the enemy half of limited vision (vision spec §3),
	 * decided here and nowhere else.
	 *
	 * An enemy is drawn when it is inside the player's cone AND in line of sight.
	 * Both halves are evaluated on the SERVER, from the server's own copies of
	 * both pawns, and the answer is pushed out as AActor::bHidden — which is a
	 * replicated property, so a remote client receives "you may not draw this"
	 * rather than the position of a body it is supposed to be surprised by. The
	 * client draws; it never decides. A client that draws an enemy it should not
	 * see is a cheat surface even in the single-player raid this game ships, and
	 * this project already keeps loot rolls, damage and outcomes on the authority
	 * for exactly the same reason.
	 *
	 * Throttled to USarkoRaidSettings::VisionUpdateIntervalSeconds rather than run
	 * per frame: one trace per living enemy per update against a 3-5 enemy budget
	 * is the whole cost, and the answer cannot change meaningfully inside 16 ms.
	 *
	 * ONE PLAYER. The visibility of an actor is a per-actor fact, not a
	 * per-viewer one, so with two players in a raid the first one's cone would
	 * decide what the second one sees. The shipped raid is standalone and
	 * single-player; a co-op raid needs per-connection relevancy
	 * (AActor::IsNetRelevantFor, or a per-viewer replication condition) and that
	 * is a networking change, not a drawing one. The refusal is explicit and
	 * logged rather than silent — see the implementation.
	 */
	void UpdateEnemyVisibility(float DeltaSeconds);

	/** Seconds since the last visibility pass. See UpdateEnemyVisibility. */
	float VisionUpdateAccumulator = 0.f;

	/** So the multi-player refusal above is said once per raid, not per tick. */
	bool bVisionMultiPlayerWarned = false;

	/** Per-encounter server state, index-aligned with CachedDefinition.Encounters. */
	TArray<SarkoEncounter::FEncounterRuntime> EncounterRuntimes;

	/**
	 * Indices into CachedDefinition.Encounters, sorted by the authored `order`.
	 * Built once: two triggers arming in the same evaluation must resolve the
	 * same way every raid, or "the first fight is the gas station" is luck.
	 */
	TArray<int32> EncounterOrder;

	FTimerHandle EncounterTimerHandle;

	/**
	 * ?EncounterBudget=N on the travel URL, or -1 for "use the map's".
	 *
	 * A verification knob in the same spirit as ?Seed=, and server-side only —
	 * it is read in InitGame, lives on the game mode and is never replicated, so
	 * a joining client can neither set nor read it. It exists because the
	 * budget's most important behaviour is the one the shipped tutorial data can
	 * never exhibit: the tutorial's four encounters cost exactly the tutorial's
	 * four points, so a REFUSAL — the law actually being applied — is
	 * unobservable in a live raid unless the ceiling can be lowered.
	 */
	int32 EncounterBudgetOverride = -1;

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

	/**
	 * Which closed extraction zones have already said so in the log.
	 *
	 * ExtractTick runs every frame and a player can stand on a shut pad for
	 * minutes, so the refusal is announced ONCE per zone per raid rather than per
	 * tick. Cleared by ActivateRaid alongside Dwells, so a second raid in one
	 * process says it again.
	 */
	TSet<int32> AnnouncedClosedZones;
};
