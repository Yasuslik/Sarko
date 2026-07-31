#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapPalette.h"

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

	/**
	 * Optional stable name (ТЗ §18). Optional on a block because there are
	 * hundreds and none is referenced individually; carried anyway so an
	 * expanded building's walls can be traced back to their building.
	 */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector Extent = FVector(200.f, 200.f, 150.f);

	/**
	 * What this block is made of, for colour. Structure by default, so every
	 * block authored before surfaces existed keeps the grey it had.
	 */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;

	/**
	 * False turns the block into a flat surface the player walks over: a road,
	 * a water strip, a ravine bed. True — the default — is cover, which is what
	 * every block in the sector was before this field existed.
	 *
	 * "Does not block movement" is literal and total: the spawned actor gets
	 * ECollisionEnabled::NoCollision, so it stops neither pawns nor bullets nor
	 * line traces. A road is paint on the floor. Everything that reads a block
	 * as *data* still sees it — CollectIds names it, ToLayout carries it, the
	 * palette colours it — only the physics body is gone.
	 */
	UPROPERTY()
	bool bBlocksMovement = true;
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
	// The palette (ESarkoSurface, Palette::ColourFor, the named constants) lives
	// in Map/SarkoMapPalette.h, included above: the kind table needs it too, and
	// it must not have to include the spawner to get a colour.

	/** Spawns floor and cover for a layout using engine primitive meshes. */
	void SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout);

	/**
	 * Spawns everything in a definition that is not already covered by
	 * SpawnLayout's floor and cover: the props. Logs and skips an unknown kind
	 * rather than substituting a default, because a silently wrong prop is
	 * harder to notice than a missing one.
	 */
	void SpawnProps(UWorld& World, const FSarkoMapDefinition& Definition);

	/**
	 * Spawns one ASarkoLootContainer per definition entry, in array order, so
	 * ContainerIndex means the same thing on every machine. Deterministic and
	 * local — nothing here replicates.
	 */
	void SpawnLootContainers(UWorld& World, const FSarkoMapDefinition& Definition);

	/**
	 * Spawns one visual pad per extraction spot, in array order, on every
	 * machine — so ZoneIndex means the same thing everywhere, exactly as
	 * ContainerIndex does. The pads decide nothing: the dwell is measured by the
	 * game mode against the definition, server side.
	 */
	void SpawnExtractionZones(UWorld& World, const FSarkoMapDefinition& Definition);
}
