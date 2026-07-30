#pragma once

#include "CoreMinimal.h"

#include "SarkoMapBuilder.generated.h"

// Forward-declared at global scope, not inside namespace SarkoMap below: an
// elaborated-type-specifier ("struct FSarkoMapDefinition") written directly
// inside a namespace block, with no prior visible declaration, introduces
// the name into *that* namespace instead of finding the real global type —
// silently creating a second, permanently-incomplete SarkoMap::FSarkoMapDefinition
// that shadows the real ::FSarkoMapDefinition from SarkoMapDefinition.h for
// every unqualified lookup inside namespace SarkoMap from then on. That broke
// SarkoMapDefinition.cpp's own ParseDefinition/ToLayout bodies once this header
// started forward-declaring the type. Declaring it here instead, then
// referring to the plain (unqualified, un-re-elaborated) name below, avoids
// that trap.
struct FSarkoMapDefinition;

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

/**
 * A complete raid layout: the reduced form of a hand-authored map definition
 * that the spawn code below consumes. Produced by SarkoMap::ToLayout, never
 * generated — the map is a data file, and this is what is left of it once the
 * designer-facing fields (names, tiers, zone tags) have been dropped.
 */
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
	/** Spawns floor and cover for a layout using engine primitive meshes. */
	void SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout);

	/**
	 * Spawns everything in a definition that is not already covered by
	 * SpawnLayout's floor and cover: props and container markers. Logs and
	 * skips an unknown kind rather than substituting a default, because a
	 * silently wrong prop is harder to notice than a missing one.
	 */
	void SpawnProps(UWorld& World, const FSarkoMapDefinition& Definition);
}
