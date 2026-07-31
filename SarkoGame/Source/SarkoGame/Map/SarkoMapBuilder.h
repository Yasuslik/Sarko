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

	/**
	 * The sector's lighting, in the header so the numbers the readability
	 * argument rests on can be asserted rather than trusted.
	 *
	 * Mobile forward shading supports exactly ONE directional light — a second
	 * makes the engine warn on screen that lights are "competing to be the
	 * single one used for forward shading" and then pick one by brightness. The
	 * ambient here is a sky light, which is spherical-harmonic irradiance and
	 * not a second directional light, so it does not touch that path.
	 */
	namespace Lighting
	{
		/** Steep rather than horizontal, so a top-down camera sees lit surfaces. */
		const FRotator SunRotation(-55.f, 30.f, 0.f);

		/** Bright enough to read grey boxes on a phone screen in daylight. */
		constexpr float SunIntensityLux = 6.f;

		/**
		 * How much of the sun's shadow is actually occluded. 1.0 is the engine
		 * default and produced near-black stripes beside every wall; 0.0 removes
		 * shadows entirely, which would undo the reason virtual shadow maps are
		 * deliberately enabled in DefaultEngine.ini. This is a scalar in the
		 * light's shader parameters: it costs nothing at all.
		 */
		constexpr float ShadowAmount = 0.6f;

		/**
		 * The engine's own map-template ambient cubemap, referenced by path —
		 * this project authors no assets. A sky light with SLS_SpecifiedCubemap
		 * and no cubemap is treated as INVALID by the engine and contributes
		 * nothing, so a broken path here is a silent loss of all ambient;
		 * Sarko.Config.LightingHasAnAmbientTerm pins that it resolves.
		 */
		const TCHAR* const AmbientCubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");

		/** Captured once at spawn. 32 keeps the processed cubemap around 50 KB. */
		constexpr int32 AmbientCubemapResolution = 32;

		/** Enough to lift unlit faces off black, far short of flattening the sun. */
		constexpr float AmbientIntensity = 1.0f;

		/** Cool, against the sun's warm — a shadowed wall reads blue-grey, not black. */
		const FLinearColor AmbientColour(0.55f, 0.62f, 0.78f);

		/**
		 * The lower hemisphere. bLowerHemisphereIsBlack is turned off so the
		 * undersides of things are not pure black, but this stays dim: a bright
		 * ground bounce lights everything from below and reads as a missing
		 * shadow.
		 */
		const FLinearColor GroundBounceColour(0.050f, 0.045f, 0.030f);
	}

	/**
	 * The one material instance for a surface, created on first ask and shared by
	 * everything that wears it — the floor, every cover block, and every instanced
	 * prop component.
	 *
	 * Public because sharing is no longer just a memory argument. A unique
	 * material is a unique shader binding, and UE batches primitives only when
	 * they agree on mesh AND material, so a second instance for the same surface
	 * silently un-batches everything painted with it. Anything that needs to
	 * ANIMATE a parameter needs its own instance and must say so, rather than
	 * quietly mutating the one every block in the sector is using.
	 *
	 * Null (and a log line) if the base material fails to load; callers keep the
	 * engine's placeholder rather than losing their geometry.
	 */
	UMaterialInterface* SharedSurfaceMaterial(ESarkoSurface Surface);

	/** Spawns floor and cover for a layout using engine primitive meshes. */
	void SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout);

	/**
	 * Spawns everything in a definition that is not already covered by
	 * SpawnLayout's floor and cover: the props. Logs and skips an unknown kind
	 * rather than substituting a default, because a silently wrong prop is
	 * harder to notice than a missing one.
	 *
	 * ONE actor — an ASarkoPropField holding every part as an instance. It was an
	 * actor per part until the forest, and 401 of them; see the class comment on
	 * ASarkoPropField for why that had to change and what made it possible.
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
