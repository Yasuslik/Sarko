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

	/**
	 * True for the leafy part of a tree: the one thing on the map that hides
	 * ITSELF when the local player walks under it.
	 *
	 * The camera looks almost straight down (a 1400 uu boom at -70 degrees), so
	 * anything held 4-8 m over the pawn's head sits between the camera and the
	 * pawn and the player disappears. That is the whole reason `treeline` is a
	 * solid dark-green wall used only as a boundary rather than actual trees.
	 * Flagging the canopy instead lets a stand read as forest from a distance and
	 * open up overhead: ASarkoRaidGameState::UpdateCanopyFade hides every flagged
	 * part inside USarkoRaidSettings::CanopyFadeRadiusUU of the local pawn.
	 *
	 * Cosmetic and local, always. A flagged part must also be non-colliding
	 * (SarkoMap::Canopy is the only thing that sets this, and it enforces that),
	 * because the fade changes visibility and NOTHING else: a hidden canopy that
	 * stopped a bullet, or a visible one that did not, would each be a worse bug
	 * than an empty world. Cover and navigation are the trunks, which never fade.
	 */
	UPROPERTY()
	bool bCanopy = false;
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
	 * How many boxes the props section of a definition amounts to — the sum of
	 * every resolved kind's part count. Unknown kinds contribute nothing, exactly
	 * as SpawnProps skips them.
	 *
	 * Was CountPropActors, and the rename is the point rather than tidying: since
	 * the props became instances this number is no longer a count of ACTORS. It
	 * is the number of instances spread across CountInstancedComponents
	 * components, and the sector spawns exactly one actor for all of them. Every
	 * caller that meant "how much is there" still wants this; the caller that
	 * meant "what does the renderer pay" wants the other one.
	 */
	int32 CountPropParts(const FSarkoMapDefinition& Definition);

	/**
	 * How many instanced components those parts will end up in: the number of
	 * distinct (mesh, surface, collision, canopy) keys the definition uses.
	 *
	 * This is the honest budget number now. What costs money on a phone is draw
	 * calls, and one instanced component that agrees on mesh and material is one
	 * draw regardless of whether it holds four instances or four hundred — so a
	 * forest of three hundred trees is not three hundred of anything, it is two
	 * more components than the map already had.
	 *
	 * Pure, so it can be asserted under -nullrhi with no world; it predicts
	 * ASarkoPropField::GetInstancedComponentCount from the map file alone, and
	 * the two agreeing is what makes this a budget rather than a guess.
	 */
	int32 CountInstancedComponents(const FSarkoMapDefinition& Definition);
}
