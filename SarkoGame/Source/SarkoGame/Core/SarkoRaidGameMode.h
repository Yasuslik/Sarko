#pragma once

#include "CoreMinimal.h"
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

	/** Shared basis for server-authoritative rolls, replicated through the game state. Read from the `Seed` URL option when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Raid")
	int32 Seed = 1;

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
};
