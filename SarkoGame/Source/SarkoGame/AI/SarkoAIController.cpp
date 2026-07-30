#include "AI/SarkoAIController.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidSettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "CollisionQueryParams.h"

ESarkoAIState SarkoAI::DecideState(
	ESarkoAIState Current,
	bool bHasTarget,
	float DistanceToTarget,
	bool bHasLineOfSight,
	float HearingRadius,
	float FiringRange)
{
	// Nothing to react to: wander.
	if (!bHasTarget || DistanceToTarget > HearingRadius)
	{
		return ESarkoAIState::Patrol;
	}

	// Seen and close enough to hit: shoot. Otherwise close the distance.
	if (bHasLineOfSight && DistanceToTarget <= FiringRange)
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
	// instead, with a single short forward trace for obstacle avoidance: the
	// map is a flat plane with box cover and the view is top-down, so one
	// trace along the desired direction is enough to tell whether the
	// straight line is clear.
	const FVector TraceStart = Self->GetActorLocation();
	const FVector TraceEnd = TraceStart + FVector(DesiredDirection, 0.f) * Settings.AIAvoidanceTraceDistanceUU;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SarkoAIAvoidance), /*bTraceComplex*/ false, Self);
	FHitResult Hit;
	const bool bForwardBlocked = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);

	const FVector2D SteerDirection = SarkoAI::ComputeSteerDirection(DesiredDirection, bForwardBlocked, Settings.AIAvoidanceSteerDegrees);

	if (bLogThisTick)
	{
		UE_LOG(LogTemp, Log, TEXT("SarkoAI: loc=%s target=%s blocked=%d steer=%s"),
			*Self->GetActorLocation().ToString(), *TargetLocation.ToString(), bForwardBlocked, *SteerDirection.ToString());
	}

	Self->AddMovementInput(FVector(SteerDirection, 0.f), 1.f);
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
		Settings.EnemyHearingRadiusUU, Settings.WeaponRangeUU * 0.5f);

	DebugLogAccum += DeltaSeconds;
	const bool bLogThisTick = Settings.bLogAIDiagnostics && DebugLogAccum > 1.f;
	if (bLogThisTick)
	{
		DebugLogAccum = 0.f;
	}

	switch (State)
	{
	case ESarkoAIState::Patrol:
		if (FVector::Dist2D(Self->GetActorLocation(), PatrolTarget) < 200.f)
		{
			const float Extent = Settings.MapExtent * 0.8f;
			PatrolTarget = FVector(FMath::FRandRange(-Extent, Extent), FMath::FRandRange(-Extent, Extent), Self->GetActorLocation().Z);
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
