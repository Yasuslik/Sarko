#include "Combat/SarkoWeapon.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Pawn/SarkoHealthComponent.h"
#include "TimerManager.h"

FVector SarkoCombat::ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets)
{
	const FVector Aim = Direction.GetSafeNormal();
	if (Aim.IsNearlyZero() || CandidateTargets.Num() == 0)
	{
		return Direction;
	}

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));

	const FVector* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FVector& Target : CandidateTargets)
	{
		const FVector ToTarget = Target - Origin;
		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		if (ToTargetDir.IsNearlyZero())
		{
			continue;
		}

		// Strictly inside the cone. Anything else is left alone — this single
		// comparison is what separates assistance from aimbotting.
		if (FVector::DotProduct(Aim, ToTargetDir) < CosLimit)
		{
			continue;
		}

		const float DistanceSq = ToTarget.SizeSquared();
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = &Target;
		}
	}

	return Best ? (*Best - Origin).GetSafeNormal() : Direction;
}

USarkoWeaponComponent::USarkoWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USarkoWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USarkoWeaponComponent, AmmoInMagazine);
	DOREPLIFETIME(USarkoWeaponComponent, bReloading);
}

void USarkoWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	AmmoInMagazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
}

void USarkoWeaponComponent::ResetForTest(int32 Rounds)
{
	AmmoInMagazine = Rounds;
	bReloading = false;
}

void USarkoWeaponComponent::ServerFire(FVector Origin, FVector Direction)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority())
	{
		return;
	}

	if (!CanFire())
	{
		// A fire request that lands while the magazine is empty (or a reload
		// is already running) is the only signal a human tester gets that
		// something needs to happen — there is no separate reload button on a
		// two-thumbstick touch layout. StartReload() itself no-ops if a reload
		// is already in flight, so this is safe to call every time.
		StartReload();
		return;
	}

	--AmmoInMagazine;
	if (AmmoInMagazine == 0)
	{
		// The magazine just ran dry from this very shot: start the reload
		// immediately instead of waiting for the next fire attempt, so the
		// weapon is already coming back online while the player notices.
		StartReload();
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// Collect plausible targets, then let the pure helper decide the nudge.
	TArray<FVector> Candidates;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (Other == Owner)
		{
			continue;
		}
		if (const USarkoHealthComponent* Health = Other->FindComponentByClass<USarkoHealthComponent>())
		{
			if (!Health->IsDead())
			{
				Candidates.Add(Other->GetActorLocation());
			}
		}
	}

	const FVector Adjusted = SarkoCombat::ApplyAimAssist(Origin, Direction, Settings.AimConeHalfAngleDegrees, Candidates);
	const FVector End = Origin + Adjusted * Settings.WeaponRangeUU;

	// Cover must stop bullets, so this is a real trace against world geometry
	// rather than a distance check.
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Pawn, Params))
	{
		return;
	}

	if (AActor* HitActor = Hit.GetActor())
	{
		if (USarkoHealthComponent* Health = HitActor->FindComponentByClass<USarkoHealthComponent>())
		{
			Health->ApplyDamage(Settings.WeaponDamage, Owner);
		}
	}
}

void USarkoWeaponComponent::StartReload()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bReloading)
	{
		return;
	}

	bReloading = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReloadTimer, this, &USarkoWeaponComponent::FinishReload,
			GetDefault<USarkoRaidSettings>()->ReloadSeconds, false);
	}
}

void USarkoWeaponComponent::FinishReload()
{
	AmmoInMagazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
	bReloading = false;
}
