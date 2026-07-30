#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapBuilder.h"

#include "SarkoMapDefinition.generated.h"

/** A placeable object: a wreck, a fuel pump, a freight car. Kind picks the mesh. */
USTRUCT()
struct FSarkoMapProp
{
	GENERATED_BODY()

	UPROPERTY()
	FName Kind;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;
};

/** Where a lootable container sits, and how good its contents are. */
USTRUCT()
struct FSarkoLootContainerSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Tier;
};

/** A place the player can leave the raid from. Mechanic lands in a later plan. */
USTRUCT()
struct FSarkoExtractionSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float RadiusUU = 400.f;

	UPROPERTY()
	FString Name;
};

/** A bot spawn, tagged with the risk zone it belongs to. */
USTRUCT()
struct FSarkoBotSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Zone;
};

/**
 * A whole hand-authored map, exactly as it appears in the data file.
 *
 * Distinct from FSarkoMapLayout on purpose: the definition is what a designer
 * writes and can include things the spawner does not care about (extraction
 * names, container tiers, zone tags), while the layout is the reduced form the
 * existing spawn code already consumes.
 */
USTRUCT()
struct FSarkoMapDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	float ExtentUU = 0.f;

	UPROPERTY()
	float RaidDurationSeconds = 0.f;

	UPROPERTY()
	TArray<FSarkoCoverBlock> Blocks;

	UPROPERTY()
	TArray<FSarkoMapProp> Props;

	UPROPERTY()
	TArray<FSarkoLootContainerSpot> Containers;

	UPROPERTY()
	TArray<FTransform> PlayerSpawns;

	UPROPERTY()
	TArray<FSarkoBotSpot> BotSpawns;

	UPROPERTY()
	TArray<FSarkoExtractionSpot> Extractions;
};

namespace SarkoMap
{
	/**
	 * Parses a map file. Pure: text in, definition out, no disk and no world,
	 * which is what lets the schema be tested from string literals.
	 *
	 * Every failure sets OutError to something that names the problem. A map
	 * file is hand-edited, so it will be broken eventually, and the worst
	 * outcome is a silent empty map — the game launches with nothing in it and
	 * no clue why.
	 */
	bool ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError);

	/** Reduces a definition to the layout the existing spawn code consumes. */
	FSarkoMapLayout ToLayout(const FSarkoMapDefinition& Definition);

	/** Reads Data/Maps/<MapId>.json from the project directory. */
	bool LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError);
}
