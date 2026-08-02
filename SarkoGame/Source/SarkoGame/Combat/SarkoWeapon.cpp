#include "Combat/SarkoWeapon.h"

#include "AI/SarkoNoise.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Loot/SarkoBackpack.h"
#include "Net/UnrealNetwork.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "Pawn/SarkoSurvival.h"
#include "TimerManager.h"

FVector SarkoCombat::ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets)
{
	const int32 Best = BestAimAssistTarget(Origin, Direction, ConeHalfAngleDeg, CandidateTargets);
	return CandidateTargets.IsValidIndex(Best)
		? (CandidateTargets[Best] - Origin).GetSafeNormal()
		: Direction;
}

int32 SarkoCombat::BestAimAssistTarget(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets)
{
	const FVector Aim = Direction.GetSafeNormal();
	if (Aim.IsNearlyZero() || CandidateTargets.Num() == 0)
	{
		return INDEX_NONE;
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
		return INDEX_NONE;
	}

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));

	int32 Best = INDEX_NONE;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < CandidateTargets.Num(); ++Index)
	{
		const FVector& Target = CandidateTargets[Index];
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
			Best = Index;
		}
	}

	return Best;
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

int32 SarkoCombat::ReloadAmount(int32 InMagazine, int32 MagazineSize, int32 ReserveRounds)
{
	const int32 Capacity = FMath::Max(0, MagazineSize);
	// Clamped UP to zero as well as down to Capacity: a magazine holding more than
	// it should would otherwise make Room negative, and a negative transfer moves
	// rounds the wrong way through a caller that only knows how to add.
	const int32 Loaded = FMath::Clamp(InMagazine, 0, Capacity);
	const int32 Room = Capacity - Loaded;
	const int32 Reserve = FMath::Max(0, ReserveRounds);
	return FMath::Min(Room, Reserve);
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
	bEmptyReserveReported = false;
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

	// A SHOT IS THE LOUDEST THING IN THE GAME (spec §7), and it is reported here
	// — after the rate limit and after the round is spent — so that exactly the
	// shots that happened are the shots that are heard. Reporting from
	// RequestFire instead would have made a spammed, dropped request as loud as
	// a real one.
	//
	// Every shooter, not only the player: a bot firing at the player is a noise
	// its neighbours may investigate, which is the same rule applied honestly.
	// The instigator is carried so the shooter does not hear itself.
	if (USarkoNoiseSubsystem* Noise = World->GetSubsystem<USarkoNoiseSubsystem>())
	{
		Noise->ReportNoise(Owner->GetActorLocation(), SarkoNoise::EKind::Loud, Owner);
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

			// THE HIT MARKER (spec §4.2), and this is the only place it can
			// honestly be raised: the server traced, the server found the actor,
			// the server applied the damage. A client that drew a marker off its
			// own prediction would claim hits that cover rejected.
			//
			// Cast rather than a shared interface because there is exactly one
			// pawn class with a HUD to draw one on — a bot landing a shot has
			// nobody to tell.
			if (ASarkoCharacter* ShootingPlayer = Cast<ASarkoCharacter>(Owner))
			{
				ShootingPlayer->NotifyHitConfirmed(HitActor->GetActorLocation());
			}

			// The SHOOTER is in combat too, and that is the half a "since I was
			// last hit" timer would miss: a player winning a firefight would
			// otherwise regenerate through it while the bot is still shooting back
			// and missing. The component lookup is on a connecting shot only —
			// a few times a fight, never per tick.
			if (USarkoSurvivalComponent* Survival = Owner->FindComponentByClass<USarkoSurvivalComponent>())
			{
				Survival->NoteCombat();
			}
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

	// THE OTHER DRY CLICK (spec §1). A reload with nothing in the bag is refused
	// here rather than allowed to run and come back empty: ReloadSeconds of a
	// weapon that cannot shoot, ending in the same zero, is the button lying about
	// what it can do. One log line and nothing else — no timer, no state.
	//
	// Gated on the component EXISTING, not on the number: a pawn with no backpack
	// at all is a bot, and bots keep their infinite reload (see FinishReload).
	if (const USarkoBackpackComponent* Backpack = Owner->FindComponentByClass<USarkoBackpackComponent>())
	{
		if (Backpack->CountItem(SarkoLoot::AmmoItemId) <= 0)
		{
			if (!bEmptyReserveReported)
			{
				bEmptyReserveReported = true;
				UE_LOG(LogTemp, Display,
					TEXT("SarkoWeapon: dry click on reload — %d in the magazine and no ammo_9mm in the bag"),
					AmmoInMagazine);
			}
			return;
		}
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
	const int32 Capacity = FMath::Max(0, GetDefault<USarkoRaidSettings>()->MagazineSize);
	AActor* Owner = GetOwner();
	USarkoBackpackComponent* Backpack = Owner ? Owner->FindComponentByClass<USarkoBackpackComponent>() : nullptr;

	if (!Backpack)
	{
		// NO GRID, NO SCARCITY — and that is deliberate, not an oversight.
		// ASarkoEnemyCharacter has a weapon component and no backpack component, so
		// every bot reloads out of nothing exactly as it always has
		// (ASarkoAIController's reload call is untouched). Ammo is a decision the
		// player makes about cells they have to carry; a bot has no cells, no haul
		// to lose and nobody to spend the decision on, and giving it a grid would
		// mean authoring a reserve for four pawns whose whole job is to run out of
		// patience before they run out of bullets.
		AmmoInMagazine = Capacity;
	}
	else
	{
		// The transfer, server-side, where both the grid and the weapon live.
		// StartReload already refused the zero-reserve case, so this normally moves
		// something — but it is re-read rather than remembered, because
		// ReloadSeconds have passed since then and the player can have consumed,
		// dropped or died into a different bag in between.
		const int32 Before = AmmoInMagazine;
		const int32 Reserve = Backpack->CountItem(SarkoLoot::AmmoItemId);
		const int32 Wanted = SarkoCombat::ReloadAmount(Before, Capacity, Reserve);
		// RemoveItem is the authority; Taken is what actually left the grid, and the
		// magazine is credited with that rather than with what was asked for, so the
		// two can never disagree about how many rounds exist.
		const int32 Taken = Wanted > 0 ? Backpack->RemoveItem(SarkoLoot::AmmoItemId, Wanted) : 0;
		AmmoInMagazine = FMath::Clamp(Before, 0, Capacity) + Taken;

		UE_LOG(LogTemp, Display,
			TEXT("SarkoWeapon: reload — magazine %d -> %d, reserve %d -> %d"),
			Before, AmmoInMagazine, Reserve, Reserve - Taken);
	}

	bReloading = false;
	// The next empty magazine gets its own line. See ServerFire's dry click.
	bDryClickReported = false;
	bEmptyReserveReported = false;
}

int32 USarkoWeaponComponent::UnloadMagazine()
{
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		return 0;
	}
	const int32 Rounds = FMath::Max(0, AmmoInMagazine);
	AmmoInMagazine = 0;
	return Rounds;
}
