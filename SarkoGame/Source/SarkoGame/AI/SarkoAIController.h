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

	ESarkoAIState State = ESarkoAIState::Idle;
	float FireCooldown = 0.f;
	FVector PatrolTarget = FVector::ZeroVector;
};
