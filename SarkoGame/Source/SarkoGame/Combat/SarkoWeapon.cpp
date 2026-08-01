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

	// The cone test is horizontal only. Origin is the muzzle, offset above
	// the ground by a fixed height, while every candidate is a pawn's
	// *centre* at ground-relative Z — comparing the full 3D angle bakes that
	// constant vertical offset into the result, and at close range it
	// dominates: with a 6 deg cone and a 40uu muzzle height, anything closer
	// than ~380uu reads as outside the cone even dead-centre. A purely
	// horizontal aim assist has no reason to care about that Z difference at
	// all, so the cone comparison (only the comparison — the final aimed
	// direction below still points at the real 3D target) is done in 2D.
	const FVector2D Aim2D = FVector2D(Aim.X, Aim.Y).GetSafeNormal();
	if (Aim2D.IsNearlyZero())
	{
		return Direction;
	}

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));

	const FVector* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FVector& Target : CandidateTargets)
	{
		const FVector ToTarget = Target - Origin;
		const FVector2D ToTargetDir2D = FVector2D(ToTarget.X, ToTarget.Y).GetSafeNormal();
		if (ToTargetDir2D.IsNearlyZero())
		{
			continue;
		}

		// Strictly inside the cone. Anything else is left alone — this single
		// comparison is what separates assistance from aimbotting.
		if (FVector2D::DotProduct(Aim2D, ToTargetDir2D) < CosLimit)
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

FVector SarkoCombat::NormalizeFireDirection(FVector Direction)
{
	FVector Normalized = Direction;
	const float Size = Normalized.Size();
	if (Size < KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}
	return Normalized / Size;
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

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	const float Now = World->GetTimeSeconds();
	if (Now - LastFireTimeSeconds < Settings.MinFireIntervalSeconds)
	{
		// Rate limited: unlike the enemy's own EnemyFireIntervalSeconds
		// cooldown, nothing else stops a client from sending fire requests
		// faster than a human can pull the trigger and emptying the
		// magazine in a single frame. No ammo is spent and no reload starts
		// — the request is simply dropped.
		return;
	}

	// The direction is client-supplied (RequestFire's own copy, or the RPC
	// argument on the server-to-client path) and carries no guarantee of
	// unit length or even non-zero length. Tracing WeaponRangeUU along an
	// un-normalized (1000,0,0) would reach 200x the intended range, so this
	// is normalized here — the only place ServerFire ever traces from — and
	// a degenerate result bails out entirely rather than tracing anywhere.
	const FVector NormalizedDirection = SarkoCombat::NormalizeFireDirection(Direction);
	if (NormalizedDirection.IsNearlyZero())
	{
		return;
	}

	LastFireTimeSeconds = Now;
	--AmmoInMagazine;
	if (AmmoInMagazine == 0)
	{
		// The magazine just ran dry from this very shot: start the reload
		// immediately instead of waiting for the next fire attempt, so the
		// weapon is already coming back online while the player notices.
		StartReload();
	}

	// Collect plausible targets, then let the pure helper decide the nudge.
	// Restricted to foes: without this, candidates are every living pawn but
	// the shooter, so with eight enemies a shot at the player can be nudged
	// onto a nearer enemy instead — enemies quietly killing each other over
	// an 8-minute raid is a playtest confound with nothing to do with
	// controllability.
	const USarkoHealthComponent* OwnerHealth = Owner->FindComponentByClass<USarkoHealthComponent>();
	const ESarkoTeam OwnerTeam = OwnerHealth ? OwnerHealth->GetTeam() : ESarkoTeam::Player;

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
			if (!Health->IsDead() && SarkoCombat::IsFoe(OwnerTeam, Health->GetTeam()))
			{
				Candidates.Add(Other->GetActorLocation());
			}
		}
	}

	// ApplyAimAssist echoes its input back unchanged when nothing qualifies,
	// so passing the already-normalized direction in guarantees Adjusted is
	// unit length either way — this is what makes tracing only
	// Settings.WeaponRangeUU along it safe.
	const FVector Adjusted = SarkoCombat::ApplyAimAssist(Origin, NormalizedDirection, Settings.AimConeHalfAngleDegrees, Candidates);
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
			Health->ApplyDamage(DamageOverride > 0.f ? DamageOverride : Settings.WeaponDamage, Owner);
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

	// bReloading must only latch once a timer actually exists to clear it
	// again. Setting it first and checking the world second meant a call
	// with no world left bReloading stuck true forever with nothing left to
	// ever set it back — the weapon bricked permanently rather than merely
	// failing to reload this one time.
	if (UWorld* World = GetWorld())
	{
		bReloading = true;
		World->GetTimerManager().SetTimer(ReloadTimer, this, &USarkoWeaponComponent::FinishReload,
			GetDefault<USarkoRaidSettings>()->ReloadSeconds, false);
	}
}

void USarkoWeaponComponent::FinishReload()
{
	AmmoInMagazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
	bReloading = false;
}
