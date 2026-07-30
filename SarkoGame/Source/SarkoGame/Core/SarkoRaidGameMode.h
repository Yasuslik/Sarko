#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Map/SarkoMapBuilder.h"

#include "SarkoRaidGameMode.generated.h"

/**
 * Server-only raid authority. Builds the map from the seed and starts the clock.
 * The seed comes from sarko-api's raid/start response, so every client in a
 * match generates the same layout.
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
	 * Places the player at a procedural layout point instead of the default
	 * flow's PlayerStart search. There are no PlayerStart actors in the level
	 * because there is no authored level, so this bypasses
	 * FindPlayerStart/ChoosePlayerStart entirely (both require handing back an
	 * AActor, which would mean spawning a throwaway marker actor purely to
	 * carry a location) and instead calls RestartPlayerAtTransform directly —
	 * the engine's own transform-based spawn path, used symmetrically with the
	 * PlayerStart-based one.
	 */
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Seed for the procedural layout. Read from the `Seed` URL option when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Raid")
	int32 Seed = 1;

	/** The layout this raid was built from; pawns spawn against it. */
	FSarkoMapLayout CachedLayout;

private:
	/** Round-robins through CachedLayout.PlayerStarts across spawns and respawns. */
	int32 NextPlayerStartIndex = 0;
};
