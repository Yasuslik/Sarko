#pragma once

#include "CoreMinimal.h"

#include "SarkoMapKinds.generated.h"

/**
 * How one prop kind is built. Every mesh is an engine primitive referenced by
 * path: this project authors no assets, so a "car wreck" is a scaled box until
 * real art arrives at the same coordinates.
 */
USTRUCT()
struct FSarkoPropKind
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Mesh;

	/** Half-extents in unreal units. The pawn is ~176 uu tall for scale. */
	UPROPERTY()
	FVector Extent = FVector(100.f);

	/** False for decoration the player can walk through, true for cover. */
	UPROPERTY()
	bool bBlocksMovement = true;
};

namespace SarkoMap
{
	/** Looks up a kind by name. False for an unknown kind — never a default. */
	bool FindPropKind(FName Kind, FSarkoPropKind& OutKind);
}
