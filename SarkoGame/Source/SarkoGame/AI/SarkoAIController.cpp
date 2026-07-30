#include "AI/SarkoAIController.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

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

	static float DebugLogAccum = 0.f;
	DebugLogAccum += DeltaSeconds;
	const bool bDebugLogThisTick = DebugLogAccum > 1.f;
	if (bDebugLogThisTick) { DebugLogAccum = 0.f; }

	switch (State)
	{
	case ESarkoAIState::Patrol:
		if (FVector::Dist2D(Self->GetActorLocation(), PatrolTarget) < 200.f)
		{
			const float Extent = Settings.MapExtent * 0.8f;
			PatrolTarget = FVector(FMath::FRandRange(-Extent, Extent), FMath::FRandRange(-Extent, Extent), Self->GetActorLocation().Z);
		}
		{
			const EPathFollowingRequestResult::Type Result = MoveToLocation(PatrolTarget, 100.f);
			if (bDebugLogThisTick)
			{
				UE_LOG(LogTemp, Warning, TEXT("NAVDIAG Patrol loc=%s target=%s moveResult=%d"), *Self->GetActorLocation().ToString(), *PatrolTarget.ToString(), (int32)Result);
			}
		}
		break;

	case ESarkoAIState::Chase:
		if (Target)
		{
			const EPathFollowingRequestResult::Type Result = MoveToActor(Target, 200.f);
			if (bDebugLogThisTick)
			{
				UE_LOG(LogTemp, Warning, TEXT("NAVDIAG Chase loc=%s target=%s moveResult=%d"), *Self->GetActorLocation().ToString(), *Target->GetActorLocation().ToString(), (int32)Result);
			}
		}
		break;

	case ESarkoAIState::Shoot:
		StopMovement();
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
