#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapPalette.h"

#include "SarkoMapKinds.generated.h"

// Forward-declared at global scope, never as an elaborated specifier inside
// namespace SarkoMap below — see the comment in Map/SarkoMapBuilder.h for the
// bug that rule exists to prevent.
struct FSarkoMapDefinition;

/**
 * One box of a prop.
 *
 * Every mesh is an engine primitive referenced by path: this project authors no
 * assets, so a "car wreck" is a scaled box until real art arrives at the same
 * coordinates.
 */
USTRUCT()
struct FSarkoPropPart
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Mesh;

	/** Half-extents in unreal units. The pawn is ~176 uu tall for scale. */
	UPROPERTY()
	FVector Extent = FVector(100.f);

	/**
	 * Offset from the prop's own origin, in the prop's unrotated frame; the
	 * prop's yaw rotates it. Zero for a single-box kind, which is what keeps
	 * every prop authored before parts existed at exactly its old transform.
	 *
	 * AUTHORING CONVENTION: a single-box prop is placed in the map file with
	 * pos.z equal to the kind's own half-height, so it rests on the floor. A
	 * COMPOSITE kind is placed with pos.z = 0 and each part carries its own
	 * centre height here. Mixing the two conventions buries or floats the prop.
	 */
	UPROPERTY()
	FVector Offset = FVector::ZeroVector;

	/** False for decoration the player can walk through, true for cover. */
	UPROPERTY()
	bool bBlocksMovement = true;

	/** What this part is made of, for colour (ТЗ §14). */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;
};

/**
 * How one prop kind is built: one or more boxes, spawned as one actor each.
 *
 * A list rather than a single box because ТЗ §32 asks for things that are not
 * boxes — a pylon is legs plus crossarms, a road sign is a post plus a plate,
 * a fuel canopy is a roof plus posts — and authoring those as separate props
 * means a designer keeps four entries in formation by hand for every one.
 */
USTRUCT()
struct FSarkoPropKind
{
	GENERATED_BODY()

	/** Never empty for a kind that resolves; pinned by a test. */
	UPROPERTY()
	TArray<FSarkoPropPart> Parts;
};

namespace SarkoMap
{
	/** Looks up a kind by name. False for an unknown kind — never a default. */
	bool FindPropKind(FName Kind, FSarkoPropKind& OutKind);

	/**
	 * Where one part of a prop stands in world space.
	 *
	 * A part's Offset is authored in the prop's OWN frame, so it has to rotate
	 * with the prop: rotating the part but not its offset (or the reverse) shears
	 * a composite apart at every yaw except zero, and a road sign's plate ends up
	 * hanging in the air beside its post.
	 *
	 * Pulled out of SpawnProps purely so it can be asserted: automation runs under
	 * -nullrhi with no world to spawn into, and until composites existed no kind
	 * had a non-zero offset for a yaw to act on. This is not a second spawn path —
	 * SpawnMeshBox remains the only one.
	 */
	FVector PartWorldLocation(const FVector& PropLocation, float PropYawDegrees, const FSarkoPropPart& Part);

	/**
	 * How many actors the props section of a definition will spawn — the sum of
	 * every resolved kind's part count. Unknown kinds contribute nothing,
	 * exactly as SpawnProps skips them. This is the ТЗ §16 budget number, and a
	 * test holds it to a ceiling.
	 */
	int32 CountPropActors(const FSarkoMapDefinition& Definition);
}
