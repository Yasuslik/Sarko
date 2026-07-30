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

	/** Seed for the procedural layout. Read from the `Seed` URL option when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Raid")
	int32 Seed = 1;

	/** The layout this raid was built from; pawns spawn against it. */
	FSarkoMapLayout CachedLayout;
};
