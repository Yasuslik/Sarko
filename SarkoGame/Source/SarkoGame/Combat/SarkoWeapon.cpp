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

int32 SarkoCombat::StartingRounds(int32 Configured, int32 MagazineSize)
{
	const int32 Capacity = FMath::Max(0, MagazineSize);
	if (Configured < 0)
	{
		return Capacity;
	}
	return FMath::Clamp(Configured, 0, Capacity);
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
	AmmoInMagazine = SarkoCombat::StartingRounds(
		GetDefault<USarkoRaidSettings>()->StartingMagazineRounds,
		GetDefault<USarkoRaidSettings>()->MagazineSize);
}

void USarkoWeaponComponent::ResetForTest(int32 Rounds)
{
	AmmoInMagazine = Rounds;
	bReloading = false;
	bDryClickReported = false;
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
		// THE DRY CLICK, and the deliberate absence of anything else.
		//
		// This branch used to call StartReload(): a trigger pull on an empty
		// magazine reloaded the weapon for you. Owner decision (spec §3): reload
		// is manual only, so an empty magazine plus a trigger pull is a dry click
		// and nothing more, until the player presses the reload button. The
		// button's empty-state pulse (SarkoUI::ESarkoReloadState::Empty, drawn by
		// ASarkoHUD::DrawReload) is the primary signal now, and the tutorial
		// teaches it by starting the raid on a partial magazine
		// (USarkoRaidSettings::StartingMagazineRounds) so the pulse appears at the
		// spawn camp with nothing on the map that can hurt anyone yet.
		//
		// Logged once per dry spell rather than per request: the aim stick sends a
		// fire request every MinFireIntervalSeconds while it is held, which would
		// be about seven identical lines a second for as long as the thumb stays
		// down. bDryClickReported clears on the next round loaded, so the next
		// empty magazine speaks again. Sound replaces this line when there is any.
		if (AmmoInMagazine <= 0 && !bReloading && !bDryClickReported)
		{
			bDryClickReported = true;
			UE_LOG(LogTemp, Display,
				TEXT("SarkoWeapon: dry click — the magazine is empty and reload is manual (press reload)"));
		}
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
	// The second removed auto-reload (spec §3). The shot that empties the
	// magazine used to start a reload of its own; now it just empties the
	// magazine, and the next trigger pull is the dry click above.

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
	// The next empty magazine gets its own line. See ServerFire's dry click.
	bDryClickReported = false;
}
