#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SarkoExtractionZone.generated.h"

class UStaticMeshComponent;

// Forward-declared at global scope, not as an elaborated type inside the
// namespace below: `const struct FSarkoExtractionSpot&` written inside
// `namespace SarkoExtract` declares a second, permanently-incomplete
// SarkoExtract::FSarkoExtractionSpot that shadows the real one. That exact bug
// already happened once in SarkoMapBuilder.h.
struct FSarkoExtractionSpot;

namespace SarkoExtract
{
	/**
	 * Largest per-frame delta the dwell will accept, in seconds.
	 *
	 * A loading stall or a breakpoint produces one enormous delta, and without a
	 * clamp that single frame would complete most of an extraction the player
	 * never actually stood through.
	 */
	constexpr float MaxDwellStepSeconds = 0.5f;

	/**
	 * Advances the extraction dwell timer. Pure.
	 *
	 * Leaving the zone **resets to zero**, not pauses (spec §4.5). Pausing would
	 * let a player assemble five seconds out of safe fragments — step in, step
	 * behind cover, step in again — and the dwell exists precisely to make the
	 * last five seconds of a raid dangerous.
	 */
	float AdvanceDwell(float CurrentSeconds, bool bInsideZone, float DeltaSeconds);

	/**
	 * Index of the zone the pawn is standing in, or INDEX_NONE.
	 *
	 * Planar: a zone is a circle painted on the ground and the pawn's origin is
	 * at capsule centre, so a 3D test would shrink every zone by the pawn's half
	 * height for no reason. Same discipline as SarkoLoot::CanInteract, and like
	 * it, only ever called with the server's own copy of the pawn's location
	 * when it decides anything.
	 */
	int32 FindZoneContaining(const FVector& PawnLocation, const TArray<FSarkoExtractionSpot>& Zones);
}

/**
 * A place the player can leave the raid from.
 *
 * Visual only. The dwell is measured by the game mode against the map
 * definition, on the server, so this actor never decides anything: it exists so
 * the zone can be seen from a top-down camera, and it carries no collision at
 * all (a collision volume here would be a second, disagreeing source of truth
 * about whether the player is inside).
 *
 * Like containers, spawned locally on every machine from the map file and never
 * replicated.
 */
UCLASS()
class ASarkoExtractionZone : public AActor
{
	GENERATED_BODY()

public:
	ASarkoExtractionZone();

	virtual void BeginPlay() override;

	void SetupFromSpot(int32 InIndex, const FString& InName, float InRadiusUU);

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	int32 ZoneIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	FString ZoneName;

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	float RadiusUU = 500.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Extraction")
	TObjectPtr<UStaticMeshComponent> Pad;
};
