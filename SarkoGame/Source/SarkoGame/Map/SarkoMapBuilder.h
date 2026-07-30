#pragma once

#include "CoreMinimal.h"

#include "SarkoMapBuilder.generated.h"

class USarkoRaidSettings;

/** One piece of cover: a box the player can hide behind and shots cannot cross. */
USTRUCT()
struct FSarkoCoverBlock
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector Extent = FVector(200.f, 200.f, 150.f);
};

/** A complete raid layout. Derived only from the seed and the settings. */
USTRUCT()
struct FSarkoMapLayout
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoCoverBlock> Cover;

	UPROPERTY()
	TArray<FVector> PlayerStarts;

	UPROPERTY()
	TArray<FVector> EnemySpawns;

	UPROPERTY()
	float Extent = 0.f;
};

namespace SarkoMap
{
	/**
	 * Pure: seed in, layout out. No world, no actors, no side effects — which is
	 * why the layout rules can be tested headlessly and why every machine in a
	 * match generates an identical map from the seed sarko-api handed out.
	 */
	FSarkoMapLayout BuildLayout(int32 Seed, const USarkoRaidSettings& Settings);

	/** Spawns floor and cover for a layout using engine primitive meshes. */
	void SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout);
}
