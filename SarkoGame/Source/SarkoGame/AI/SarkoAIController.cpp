#include "AI/SarkoAIController.h"

#include "Combat/SarkoWeapon.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

ESarkoAIState SarkoAI::DecideState(
	ESarkoAIState Current,
	bool bHasTarget,
	float DistanceToTarget,
	bool bHasLineOfSight,
	float HearingRadius,
	float FiringRange,
	float ShootHysteresisRangeUU)
{
	// Nothing to react to: wander.
	if (!bHasTarget || DistanceToTarget > HearingRadius)
	{
		return ESarkoAIState::Patrol;
	}

	// Seen and close enough to hit: shoot. Already shooting tolerates
	// drifting up to ShootHysteresisRangeUU past FiringRange before giving
	// it up — the hysteresis band that stops the state chattering
	// Chase<->Shoot every tick right at the boundary. Entering Shoot from
	// any other state still requires the plain FiringRange, unwidened.
	const float EffectiveFiringRange = (Current == ESarkoAIState::Shoot)
		? FiringRange + ShootHysteresisRangeUU
		: FiringRange;

	if (bHasLineOfSight && DistanceToTarget <= EffectiveFiringRange)
	{
		return ESarkoAIState::Shoot;
	}
	return ESarkoAIState::Chase;
}

FVector2D SarkoAI::ComputeSteerDirection(
	FVector2D DesiredDirection,
	bool bForwardBlocked,
	float AvoidanceSteerDegrees)
{
	if (!bForwardBlocked || DesiredDirection.IsNearlyZero())
	{
		return DesiredDirection;
	}

	// Rotate the desired direction to the side by a fixed angle so the enemy
	// curves around whatever the forward trace hit instead of pushing
	// straight into it and stalling in place — a single fixed rotation is
	// enough to clear one box of cover on a flat plane.
	const float Radians = FMath::DegreesToRadians(AvoidanceSteerDegrees);
	const float CosA = FMath::Cos(Radians);
	const float SinA = FMath::Sin(Radians);
	const FVector2D Rotated(
		DesiredDirection.X * CosA - DesiredDirection.Y * SinA,
		DesiredDirection.X * SinA + DesiredDirection.Y * CosA);
	return Rotated.GetSafeNormal();
}

float SarkoAI::ChooseSteerSign(FVector2D DesiredDirection, FVector2D ImpactNormal2D)
{
	// The 2D perp-dot product's sign tells us which side of the desired
	// direction the impact normal leans toward; steering toward the opposite
	// side is steering toward the side with more room. A fixed rotation
	// (the old behaviour) ignores this entirely and can just as easily turn
	// into more geometry as away from it.
	const float Cross = DesiredDirection.X * ImpactNormal2D.Y - DesiredDirection.Y * ImpactNormal2D.X;
	return Cross >= 0.f ? 1.f : -1.f;
}

ASarkoAIController::ASarkoAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

APawn* ASarkoAIController::FindNearestLivingPlayer() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Candidate = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (!Candidate)
		{
			continue;
		}
		const USarkoHealthComponent* Health = Candidate->FindComponentByClass<USarkoHealthComponent>();
		if (!Health || Health->IsDead())
		{
			continue;
		}
		if (const APawn* Self = GetPawn())
		{
			const float DistSq = FVector::DistSquared(Self->GetActorLocation(), Candidate->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Candidate;
			}
		}
	}
	return Nearest;
}

void ASarkoAIController::SteerToward(const FVector& TargetLocation, const USarkoRaidSettings& Settings, bool bLogThisTick)
{
	APawn* Self = GetPawn();
	UWorld* World = GetWorld();
	if (!Self || !World)
	{
		return;
	}

	const FVector ToTarget = TargetLocation - Self->GetActorLocation();
	const FVector2D DesiredDirection = FVector2D(ToTarget.X, ToTarget.Y).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return;
	}

	// There is no navmesh in this project — the map is spawned procedurally
	// at runtime, so nothing is baked, and nothing configures runtime
	// generation either — so MoveToLocation/MoveToActor always fail and would
	// leave the enemy standing still forever. Steer straight at the target
	// instead, and probe ahead with a swept capsule rather than a line from
	// the capsule centre — a line reports "clear" even when geometry clips
	// the pawn's shoulder, since the pawn's own collision capsule is roughly
	// AIAvoidanceProbeRadiusUU wide.
	float ProbeHalfHeight = Settings.AIAvoidanceProbeRadiusUU;
	if (const ACharacter* Character = Cast<ACharacter>(Self))
	{
		ProbeHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	const FCollisionShape ProbeShape = FCollisionShape::MakeCapsule(Settings.AIAvoidanceProbeRadiusUU, ProbeHalfHeight);
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SarkoAIAvoidance), /*bTraceComplex*/ false, Self);
	const FVector TraceStart = Self->GetActorLocation();

	const auto ProbeDirection = [&](const FVector2D& Direction, FHitResult& OutHit) -> bool
	{
		const FVector TraceEnd = TraceStart + FVector(Direction, 0.f) * Settings.AIAvoidanceTraceDistanceUU;
		return World->SweepSingleByChannel(OutHit, TraceStart, TraceEnd, FQuat::Identity, ECC_WorldStatic, ProbeShape, QueryParams);
	};

	FHitResult Hit;
	const bool bForwardBlocked = ProbeDirection(DesiredDirection, Hit);

	FVector2D SteerDirection = DesiredDirection;
	bool bFoundClearDirection = !bForwardBlocked;

	if (bForwardBlocked)
	{
		// Re-trace the chosen direction instead of trusting one fixed
		// rotation blindly: use the impact normal to pick a side, try that
		// side, then the other, then both again at a wider angle. Bounded to
		// four extra sweeps (two angles x two sides) so this can never loop.
		const FVector2D ImpactNormal2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y);
		const float ChosenSign = SarkoAI::ChooseSteerSign(DesiredDirection, ImpactNormal2D);

		const float CandidateAngles[] = { Settings.AIAvoidanceSteerDegrees, Settings.AIAvoidanceWideSteerDegrees };
		for (const float Angle : CandidateAngles)
		{
			for (const float SideSign : { ChosenSign, -ChosenSign })
			{
				const FVector2D Candidate = SarkoAI::ComputeSteerDirection(DesiredDirection, true, SideSign * Angle);
				FHitResult SideHit;
				if (!ProbeDirection(Candidate, SideHit))
				{
					SteerDirection = Candidate;
					bFoundClearDirection = true;
					break;
				}
			}
			if (bFoundClearDirection)
			{
				break;
			}
		}
	}

	if (bLogThisTick)
	{
		UE_LOG(LogTemp, Log, TEXT("SarkoAI: loc=%s target=%s blocked=%d clear=%d steer=%s"),
			*Self->GetActorLocation().ToString(), *TargetLocation.ToString(), bForwardBlocked, bFoundClearDirection, *SteerDirection.ToString());
	}

	// Every bounded attempt this tick was blocked: do not push against
	// geometry. The stuck detector in Tick is the backstop that guarantees
	// this can never be a permanent freeze even so.
	if (bFoundClearDirection)
	{
		Self->AddMovementInput(FVector(SteerDirection, 0.f), 1.f);
	}
}

void ASarkoAIController::RerollPatrolTarget(const APawn& Self, const USarkoRaidSettings& Settings)
{
	const float Extent = Settings.MapExtent * 0.8f;
	PatrolTarget = FVector(FMath::FRandRange(-Extent, Extent), FMath::FRandRange(-Extent, Extent), Self.GetActorLocation().Z);
}

void ASarkoAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Self = GetPawn();
	if (!Self || !Self->HasAuthority())
	{
		return;
	}

	const USarkoHealthComponent* SelfHealth = Self->FindComponentByClass<USarkoHealthComponent>();
	if (SelfHealth && SelfHealth->IsDead())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	FireCooldown = FMath::Max(0.f, FireCooldown - DeltaSeconds);

	APawn* Target = FindNearestLivingPlayer();
	const float Distance = Target ? FVector::Dist(Self->GetActorLocation(), Target->GetActorLocation()) : 0.f;
	const bool bLineOfSight = Target ? LineOfSightTo(Target) : false;

	State = SarkoAI::DecideState(State, Target != nullptr, Distance, bLineOfSight,
		Settings.EnemyHearingRadiusUU, Settings.WeaponRangeUU * 0.5f, Settings.AIShootHysteresisRangeUU);

	DebugLogAccum += DeltaSeconds;
	const bool bLogThisTick = Settings.bLogAIDiagnostics && DebugLogAccum > 1.f;
	if (bLogThisTick)
	{
		DebugLogAccum = 0.f;
	}

	// Stuck detector: a backstop against any steering failure, independent of
	// *why* the pawn has not moved. Patrol's own re-roll only fires once the
	// pawn gets within 200uu of PatrolTarget, so an enemy wedged 3000uu away
	// from an unreachable target would otherwise never pick a new one and
	// freeze for the rest of the raid.
	if (!bStuckReferenceInitialised)
	{
		StuckReferenceLocation = Self->GetActorLocation();
		bStuckReferenceInitialised = true;
	}

	bool bForceFallbackToPatrol = false;
	const float DisplacementFromReference = FVector::Dist2D(Self->GetActorLocation(), StuckReferenceLocation);
	if (DisplacementFromReference > Settings.AIStuckDisplacementThresholdUU)
	{
		StuckReferenceLocation = Self->GetActorLocation();
		StuckSeconds = 0.f;
	}
	else
	{
		StuckSeconds += DeltaSeconds;
		if (StuckSeconds >= Settings.AIStuckTimeThresholdSeconds)
		{
			RerollPatrolTarget(*Self, Settings);
			bForceFallbackToPatrol = true;
			StuckReferenceLocation = Self->GetActorLocation();
			StuckSeconds = 0.f;

			if (bLogThisTick || Settings.bLogAIDiagnostics)
			{
				UE_LOG(LogTemp, Log, TEXT("SarkoAI: stuck detector fired at loc=%s, new PatrolTarget=%s"),
					*Self->GetActorLocation().ToString(), *PatrolTarget.ToString());
			}
		}
	}

	// A stuck Chase falls back to Patrol for this tick so it steers toward
	// the fresh wander point instead of straight at the same blocked
	// geometry it was already failing to get around.
	const ESarkoAIState EffectiveState = (bForceFallbackToPatrol && State == ESarkoAIState::Chase)
		? ESarkoAIState::Patrol
		: State;

	switch (EffectiveState)
	{
	case ESarkoAIState::Patrol:
		if (FVector::Dist2D(Self->GetActorLocation(), PatrolTarget) < 200.f)
		{
			RerollPatrolTarget(*Self, Settings);
		}
		SteerToward(PatrolTarget, Settings, bLogThisTick);
		break;

	case ESarkoAIState::Chase:
		if (Target)
		{
			SteerToward(Target->GetActorLocation(), Settings, bLogThisTick);
		}
		break;

	case ESarkoAIState::Shoot:
		// No movement input this tick — direct steering has no path-following
		// component to stop, so standing still while shooting just means not
		// calling AddMovementInput; the character's own braking deceleration
		// handles the rest.
		if (Target && FireCooldown <= 0.f)
		{
			if (USarkoWeaponComponent* Weapon = Self->FindComponentByClass<USarkoWeaponComponent>())
			{
				const FVector Origin = Self->GetActorLocation() + FVector(0.f, 0.f, 40.f);
				const FVector Direction = (Target->GetActorLocation() - Origin).GetSafeNormal();
				if (!Weapon->CanFire())
				{
					Weapon->StartReload();
				}
				else
				{
					Weapon->ServerFire(Origin, Direction);
					FireCooldown = Settings.EnemyFireIntervalSeconds;
				}
			}
		}
		break;

	default:
		break;
	}
}
