#pragma once

#include "CoreMinimal.h"
#include "Core/SarkoRaidGameState.h"
#include "GameFramework/GameModeBase.h"
#include "Map/SarkoMapDefinition.h"

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
	 * into the next.
	 */
	int32 LootSalt = 0;

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
	/** Round-robins through CachedLayout.PlayerStarts across spawns and respawns. */
	int32 NextPlayerStartIndex = 0;

	/**
	 * Server-side per-pawn dwell, in seconds. Keyed weakly so a destroyed pawn's
	 * entry cannot keep it alive; stale entries are pruned when their key goes
	 * stale rather than left to accumulate.
	 */
	TMap<TWeakObjectPtr<class ASarkoCharacter>, float> DwellSeconds;
};
