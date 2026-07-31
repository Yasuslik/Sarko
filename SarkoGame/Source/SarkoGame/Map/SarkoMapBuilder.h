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
	/**
	 * The sector palette, §14 of the map design: ground muted green-brown,
	 * everything built on it a neutral grey that reads as a different material.
	 *
	 * These are linear-space base colours, not sRGB swatches — they are fed
	 * straight into a material's BaseColor, which is linear. An sRGB value
	 * pasted here comes out roughly twice as bright as intended.
	 *
	 * Exposed in the header rather than buried in the .cpp so the contrast the
	 * whole readability argument rests on is something a test can assert.
	 */
	namespace Palette
	{
		/**
		 * Ground: desaturated olive/khaki. Dark enough that grey cover sits on
		 * top of it rather than dissolving into it — which is exactly what
		 * happened before, when floor and cover both wore the engine grid
		 * material and measured (156,155,151) against (158,157,153) in the same
		 * frame. Three levels apart is not cover.
		 */
		const FLinearColor Ground(0.046f, 0.051f, 0.028f);

		/** Cover and props: neutral grey, deliberately much lighter than the ground. */
		const FLinearColor Structure(0.150f, 0.150f, 0.155f);

		/** Ground roughness. Near-matte, so a 400 m plane cannot catch a specular sheet. */
		constexpr float GroundRoughness = 0.92f;

		/** Cover roughness. Slightly glossier than the ground, which helps the edges catch light. */
		constexpr float StructureRoughness = 0.75f;
	}

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
