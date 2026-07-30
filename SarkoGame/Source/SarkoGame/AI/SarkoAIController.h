#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "SarkoAIController.generated.h"

UENUM()
enum class ESarkoAIState : uint8
{
	Idle,
	Patrol,
	Chase,
	Shoot
};

namespace SarkoAI
{
	/**
	 * The whole AI decision, as a pure function of what the enemy can perceive.
	 * A C++ state machine rather than a Behavior Tree because BTs, Blackboards
	 * and StateTrees are all binary assets — unwritable and untestable here.
	 */
	ESarkoAIState DecideState(
		ESarkoAIState Current,
		bool bHasTarget,
		float DistanceToTarget,
		bool bHasLineOfSight,
		float HearingRadius,
		float FiringRange);

	/**
	 * Pure steering decision: there is no navmesh in this project, so the
	 * enemy steers straight at its target rather than pathing to it. Given the
	 * (already-normalised) direction to the target and whether a forward
	 * obstacle trace reports the way ahead is blocked, returns the direction
	 * to actually move in this tick — the desired direction unchanged when
	 * clear, or that direction rotated by AvoidanceSteerDegrees when blocked,
	 * so the enemy curves around whatever it is about to walk into instead of
	 * pushing against it and stalling. No world access — the trace itself is
	 * cast by the controller; this only decides what to do with the result.
	 */
	FVector2D ComputeSteerDirection(
		FVector2D DesiredDirection,
		bool bForwardBlocked,
		float AvoidanceSteerDegrees);

	/**
	 * Pure: given the direction the pawn wanted to go and the impact normal
	 * of whatever blocked it, decides which side has more room. Returns +1
	 * to steer by a positive rotation of the desired direction, -1 for a
	 * negative one — the caller multiplies this into the degrees it passes
	 * to ComputeSteerDirection. Replaces always rotating the same fixed way,
	 * which is what let the enemy wedge itself in a concave pocket: a fixed
	 * rotation can point straight into more geometry just as easily as the
	 * original desired direction did. No world access — the trace itself is
	 * cast by the controller.
	 */
	float ChooseSteerSign(FVector2D DesiredDirection, FVector2D ImpactNormal2D);
}

/** Drives one enemy pawn from the decision function above. */
UCLASS()
class ASarkoAIController : public AAIController
{
	GENERATED_BODY()

public:
	ASarkoAIController();

	virtual void Tick(float DeltaSeconds) override;

	ESarkoAIState GetState() const { return State; }

private:
	APawn* FindNearestLivingPlayer() const;

	/**
	 * Moves the possessed pawn one tick toward TargetLocation by direct
	 * steering (AddMovementInput) instead of a navmesh path — there is no
	 * navmesh in this project, so MoveToLocation/MoveToActor would always
	 * fail and leave the enemy standing still forever. Casts a short forward
	 * trace to decide whether the straight line is blocked, then defers the
	 * actual steering math to the pure SarkoAI::ComputeSteerDirection.
	 */
	void SteerToward(const FVector& TargetLocation, const class USarkoRaidSettings& Settings, bool bLogThisTick);

	/** Picks a fresh, far-off wander point and forces a Patrol-style steer this tick. */
	void RerollPatrolTarget(const APawn& Self, const class USarkoRaidSettings& Settings);

	ESarkoAIState State = ESarkoAIState::Idle;
	float FireCooldown = 0.f;
	FVector PatrolTarget = FVector::ZeroVector;

	/**
	 * Per-instance (unlike the previous NAVDIAG code's function-local
	 * `static`, which was shared and interleaved logging across every enemy)
	 * accumulator so bLogAIDiagnostics, when turned on, logs at roughly once
	 * per second per enemy instead of flooding the log every tick.
	 */
	float DebugLogAccum = 0.f;

	/**
	 * Stuck detector state. StuckReferenceLocation is the last place the pawn
	 * is known to have made real progress from; StuckSeconds is how long it
	 * has stayed within AIStuckDisplacementThresholdUU of that point. This is
	 * a backstop independent of the steering math above — it fires
	 * regardless of *why* the pawn has not moved, which is what guarantees no
	 * permanent freeze even if avoidance itself is defeated by the geometry.
	 */
	FVector StuckReferenceLocation = FVector::ZeroVector;
	float StuckSeconds = 0.f;
	bool bStuckReferenceInitialised = false;
};
