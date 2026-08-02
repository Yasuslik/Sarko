#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "SarkoAIController.generated.h"

UENUM()
enum class ESarkoAIState : uint8
{
	Idle,
	Patrol,
	/**
	 * Heard, not seen. The state that replaces chasing through a wall (ТЗ §11,
	 * «агра через стены нет»): the bot walks once to where the noise came from
	 * and, finding nothing there, goes back to its post. It is also what gives
	 * the player the thing an extraction game needs — the ability to break
	 * contact by breaking line of sight.
	 */
	Investigate,
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
	/**
	 * ShootHysteresisRangeUU widens the "stay in Shoot" range past
	 * FiringRange only while Current is already Shoot, and does not affect
	 * the range required to *enter* Shoot from another state. Without this,
	 * a pawn hovering at exactly FiringRange (a common resting distance,
	 * since Chase closes distance until it crosses the threshold) would
	 * chatter Chase<->Shoot every tick as floating-point distance drifted by
	 * fractions of a uu around the boundary.
	 */
	/**
	 * bHasLineOfSight is a GATE and not merely a shooting condition. Before the
	 * realism stage a perceived target was chased whether or not the bot could
	 * see it, so a shot fired anywhere in a 1800 uu circle pulled every bot in it
	 * through the geometry and into the player's face. A target that cannot be
	 * seen produces Investigate — but only if the caller has a noise to
	 * investigate (bInvestigationActive), because since spec §7 that is what
	 * hearing means.
	 *
	 * THERE IS NO HEARING RADIUS PARAMETER, and that is the §7 change. This
	 * function used to take one and return Patrol for anything outside it, which
	 * made proximity itself a perception: standing still in a bush 1000 uu from a
	 * bot was "heard". Hearing is now a noise event
	 * (USarkoNoiseSubsystem), resolved by the caller, and all this function is
	 * told is whether an investigation is live.
	 *
	 * SightRangeUU replaces the bound the hearing radius used to provide as a
	 * side effect. Without it, deleting hearing would have given every bot
	 * unlimited vision down any open sight line on the map.
	 */
	ESarkoAIState DecideState(
		ESarkoAIState Current,
		bool bHasTarget,
		float DistanceToTarget,
		bool bHasLineOfSight,
		float SightRangeUU,
		float FiringRange,
		float ShootHysteresisRangeUU,
		bool bInvestigationActive);

	/**
	 * Pure: a patrol point inside the bot's leash. AngleRand01 and RadiusRand01
	 * are the two [0,1) randoms the caller draws, passed in rather than drawn
	 * here so the one rule worth asserting — the result is never further from
	 * the post than LeashUU — is testable without a world or a seed.
	 *
	 * The radius is scaled by sqrt(RadiusRand01) so points are uniform over the
	 * disc rather than crowded at its centre; a bot that spends its patrol
	 * standing on its own post reads as a bot that is not patrolling.
	 */
	FVector PatrolPointInLeash(const FVector& PostPos, float LeashUU, float AngleRand01, float RadiusRand01);

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
	virtual void OnPossess(APawn* InPawn) override;

	ESarkoAIState GetState() const { return State; }

	/**
	 * Where this bot holds, and how far from it it may wander. Called by the
	 * encounter director right after SpawnActor, from the authored `spawns[]`
	 * row; a bot nobody tells takes its own spawn location as its post in
	 * OnPossess, which is the same discipline with no data behind it.
	 */
	void SetPost(const FVector& InPostPos, float InLeashUU);

	const FVector& GetPostPos() const { return PostPos; }
	float GetLeashUU() const;

	/**
	 * Per-instance overrides from the bot archetype table. A non-positive value
	 * means "use the project setting", which is what an unarchetyped bot gets.
	 *
	 * InHearingSensitivity is a MULTIPLIER on a noise event's radius since spec
	 * §7, not a radius of its own. See FSarkoBotArchetype::HearingSensitivity.
	 */
	void SetPerception(float InHearingSensitivity, float InFiringRangeUU, float InFireIntervalSeconds);

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

	/** Picks a fresh wander point INSIDE THE LEASH and forces a Patrol-style steer this tick. */
	void RerollPatrolTarget(const APawn& Self, const class USarkoRaidSettings& Settings);

	ESarkoAIState State = ESarkoAIState::Idle;
	float FireCooldown = 0.f;

	/**
	 * Where this bot holds. Set from the map's authored `postPos` by the
	 * encounter director, or from the pawn's own spawn location in OnPossess.
	 *
	 * It used to be that there was no post at all and PatrolTarget started at
	 * FVector::ZeroVector — the world origin, which on the shipped map is the
	 * closed bridge inside the sealed two-thirds. Every bot's first move was
	 * therefore east into the closure wall, the stuck detector fired 2 s later,
	 * and the reroll scattered it anywhere in a 320x320 m square.
	 */
	FVector PostPos = FVector::ZeroVector;

	/** Negative means "no authored leash" — GetLeashUU falls back to the setting. */
	float AuthoredLeashUU = -1.f;

	/** Starts AT the post, not at the world origin. */
	FVector PatrolTarget = FVector::ZeroVector;

	/** Archetype overrides; non-positive means "use the project setting". */
	float HearingSensitivityOverride = -1.f;
	float FiringRangeOverrideUU = -1.f;
	float FireIntervalOverrideSeconds = -1.f;

	/**
	 * WHERE THE NOISE WAS — not where the target is (spec §7).
	 *
	 * This is the whole difference between hunting a sound and wallhacking. It
	 * used to be assigned `Target->GetActorLocation()` on every tick the target
	 * was inside the hearing radius and out of sight, which meant the bot walked
	 * to the player's LIVE position while claiming to be investigating a memory.
	 * It is now the position carried by the noise event the bot heard, so a
	 * player who fires and moves is chased to where they fired from.
	 *
	 * Cleared on arrival or on timeout, at which point the bot drops back to
	 * Patrol and its patrol target is reset to the post, so "looked, found
	 * nothing, went home" is a single code path.
	 */
	FVector InvestigateTarget = FVector::ZeroVector;
	bool bInvestigating = false;
	float InvestigateSeconds = 0.f;

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
