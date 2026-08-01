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
	 * One pawn's extraction progress, and **which zone it belongs to**.
	 *
	 * The zone index is the fix for a real bug: AdvanceDwell takes only a boolean,
	 * so a pawn crossing straight from one zone into another kept its accumulated
	 * seconds and could extract from a zone it had stood in for a single frame.
	 * Carrying the identity makes "five seconds in *this* zone" a rule instead of
	 * an accident of the geometry not overlapping.
	 *
	 * A plain struct, not a USTRUCT: it lives in a TMap on the game mode (server
	 * only, never replicated — a client must not learn that somebody is extracting)
	 * and in these pure functions, and nothing reflects over it.
	 */
	struct FSarkoDwell
	{
		int32 ZoneIndex = INDEX_NONE;
		float Seconds = 0.f;
	};

	/**
	 * Advances the dwell for a pawn now standing in ZoneIndex (INDEX_NONE when
	 * outside every zone). Pure.
	 *
	 * Three rules, all tested:
	 *  - outside → forget everything. Leaving RESETS, it does not pause (spec
	 *    §4.5); pausing would let a player stitch five seconds together out of
	 *    safe fragments, and the dwell exists to make the last five seconds of a
	 *    raid dangerous;
	 *  - a different zone than last frame → this is an entry frame: the count
	 *    restarts, keyed to the new zone. Applies at any delta, including zero;
	 *  - the same zone → accumulate, with the same per-frame clamp AdvanceDwell
	 *    applies, so a hitch cannot complete an extraction nobody stood through.
	 *
	 * ASarkoRaidGameMode::ActivateRaid clears its whole dwell map, which makes the
	 * frame the raid goes live an entry frame for every pawn — so a pawn parked on
	 * a pad since spawn owes the FULL dwell from activation. That is deliberate:
	 * no dwell may accrue before there is a session to submit the result to, and
	 * the alternative reading would hand an instant extraction to anyone who
	 * spawned on a pad.
	 */
	FSarkoDwell AdvanceDwellInZone(const FSarkoDwell& Current, int32 ZoneIndex, float DeltaSeconds);

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

	/**
	 * Whether a zone will accept a dwell at this point in the raid. Pure.
	 *
	 * A zone with no `opensAfterSeconds` (zero, the default) is open from the
	 * first frame, which is what every extraction on every map was until the
	 * west cordon. The boundary belongs to OPEN: at exactly OpensAfterSeconds the
	 * zone is live, because "opens at 10:00" that refuses at 10:00 is a bug
	 * report.
	 *
	 * Elapsed is raid-clock seconds since activation, derived on the server from
	 * the clock it started (duration minus RemainingSeconds) and never from
	 * anything a client sends.
	 */
	bool IsZoneOpen(float OpensAfterSeconds, float ElapsedSeconds);

	/** Seconds left before IsZoneOpen turns true, or zero once it has. Pure, and
	 *  what the HUD counts down on a closed zone. */
	float SecondsUntilOpen(float OpensAfterSeconds, float ElapsedSeconds);
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
