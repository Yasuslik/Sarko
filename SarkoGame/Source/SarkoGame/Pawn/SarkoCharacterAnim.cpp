#include "Pawn/SarkoCharacterAnim.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Combat/SarkoWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/Character.h"
#include "Pawn/SarkoBody.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

namespace
{
	const TCHAR* IdlePath = TEXT("/Game/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle");

	/**
	 * The pistol aim pose, not the unarmed idle: it holds both arms out along the
	 * facing, which is the one thing a top-down camera struggles to show. A
	 * standing figure seen from above is a pair of shoulders and a head; the same
	 * figure aiming is unmistakably pointed somewhere.
	 */
	const TCHAR* IdleAimingPath = TEXT("/Game/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS");

	/**
	 * Clockwise from forward, matching SarkoAnimation::EightWayIndex. The pistol
	 * set rather than the unarmed one, because these pawns are carrying pistols:
	 * the arms are up and in front, which reads as "armed" at the size a pawn
	 * occupies on a phone, and it matches the pistol reload played below.
	 */
	const TCHAR* JogPaths[SarkoAnimation::EightWayCount] = {
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Fwd.MF_Pistol_Jog_Fwd"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Fwd_Right.MF_Pistol_Jog_Fwd_Right"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Right.MF_Pistol_Jog_Right"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Bwd_Right.MF_Pistol_Jog_Bwd_Right"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Bwd.MF_Pistol_Jog_Bwd"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Bwd_Left.MF_Pistol_Jog_Bwd_Left"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Left.MF_Pistol_Jog_Left"),
		TEXT("/Game/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Fwd_Left.MF_Pistol_Jog_Fwd_Left")
	};

	const TCHAR* ReloadPath = TEXT("/Game/Mannequins/Anims/Pistol/MM_Pistol_Reload.MM_Pistol_Reload");

	/**
	 * MM_Pistol_DryFire, not MM_Pistol_Fire.
	 *
	 * MM_Pistol_Fire (and MM_Rifle_Fire) are *additive*
	 * (AAT_RotationOffsetMeshSpace): they describe an offset to be blended on top
	 * of a base pose, which is something only an AnimInstance graph can do.
	 * Played on its own in single-node mode an additive sequence deforms the mesh
	 * relative to the reference pose and looks like a bug. DryFire is the one
	 * full-body (AAT_None) trigger-pull in the set, so it is what a shot shows.
	 */
	const TCHAR* FirePath = TEXT("/Game/Mannequins/Anims/Pistol/MM_Pistol_DryFire.MM_Pistol_DryFire");

	/** Three front deaths, so a pile of corpses is not three copies of one pose. */
	const TCHAR* DeathPaths[] = {
		TEXT("/Game/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"),
		TEXT("/Game/Mannequins/Anims/Death/MM_Death_Front_02.MM_Death_Front_02"),
		TEXT("/Game/Mannequins/Anims/Death/MM_Death_Front_03.MM_Death_Front_03")
	};

	/**
	 * Below this the pawn is standing. Not zero: a character that has just
	 * stopped, or is being pushed a fraction of a unit by depenetration, would
	 * otherwise flicker between idle and a run cycle every frame.
	 */
	constexpr float MovingSpeedThreshold = 25.f;

	UAnimSequence* LoadSequence(const TCHAR* Path)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Path);
		if (!Sequence)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoAnim: '%s' failed to load; that state will not animate"), Path);
		}
		return Sequence;
	}
}

ESarkoAnimState SarkoAnimation::ChooseState(bool bDead, bool bReloading, bool bFiringWindow, bool bMoving, bool bAiming)
{
	if (bDead)
	{
		return ESarkoAnimState::Death;
	}
	if (bReloading)
	{
		return ESarkoAnimState::Reload;
	}
	if (bFiringWindow && !bMoving)
	{
		return ESarkoAnimState::Fire;
	}
	if (bMoving)
	{
		return ESarkoAnimState::Jog;
	}
	return bAiming ? ESarkoAnimState::IdleAiming : ESarkoAnimState::Idle;
}

float SarkoAnimation::RelativeYaw(FVector PlanarVelocity, float FacingYawDegrees)
{
	PlanarVelocity.Z = 0.f;
	if (PlanarVelocity.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float TravelYaw = FMath::RadiansToDegrees(FMath::Atan2(PlanarVelocity.Y, PlanarVelocity.X));
	return FRotator::NormalizeAxis(TravelYaw - FacingYawDegrees);
}

int32 SarkoAnimation::EightWayIndex(float RelativeYawDegrees)
{
	const float Positive = FRotator::ClampAxis(RelativeYawDegrees);
	// Rounding rather than truncating, so the bucket is centred on its own
	// direction: 40 degrees off forward is still mostly forward, 50 is not.
	return FMath::RoundToInt(Positive / 45.f) % EightWayCount;
}

float SarkoAnimation::PlayRateForDuration(float AnimLength, float TargetSeconds)
{
	if (AnimLength <= KINDA_SMALL_NUMBER || TargetSeconds <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return FMath::Clamp(AnimLength / TargetSeconds, 0.1f, 4.f);
}

int32 SarkoAnimation::DeathVariantForPawn(uint32 PawnUniqueId)
{
	return static_cast<int32>(PawnUniqueId % UE_ARRAY_COUNT(DeathPaths));
}

TArray<FString> SarkoAnimation::AllAssetPaths()
{
	TArray<FString> Paths;
	Paths.Add(SarkoBody::MeshPathForSide(SarkoBody::ESide::Player));
	Paths.Add(SarkoBody::MeshPathForSide(SarkoBody::ESide::Enemy));
	Paths.Add(IdlePath);
	Paths.Add(IdleAimingPath);
	for (const TCHAR* Path : JogPaths)
	{
		Paths.Add(Path);
	}
	Paths.Add(ReloadPath);
	Paths.Add(FirePath);
	for (const TCHAR* Path : DeathPaths)
	{
		Paths.Add(Path);
	}
	return Paths;
}

USarkoCharacterAnimComponent::USarkoCharacterAnimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USarkoCharacterAnimComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoAnim: attached to '%s', which is not a character"), *GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	Mesh = Character->GetMesh();
	Health = Character->FindComponentByClass<USarkoHealthComponent>();
	Weapon = Character->FindComponentByClass<USarkoWeaponComponent>();

	LoadSequences();
}

void USarkoCharacterAnimComponent::LoadSequences()
{
	IdleSequence = LoadSequence(IdlePath);
	IdleAimingSequence = LoadSequence(IdleAimingPath);

	JogSequences.Reset(SarkoAnimation::EightWayCount);
	for (const TCHAR* Path : JogPaths)
	{
		JogSequences.Add(LoadSequence(Path));
	}

	ReloadSequence = LoadSequence(ReloadPath);
	FireSequence = LoadSequence(FirePath);

	const int32 Variant = SarkoAnimation::DeathVariantForPawn(GetOwner() ? GetOwner()->GetUniqueID() : 0);
	DeathSequence = LoadSequence(DeathPaths[Variant]);
}

UAnimSequence* USarkoCharacterAnimComponent::SequenceFor(ESarkoAnimState State, int32 DirectionIndex) const
{
	switch (State)
	{
	case ESarkoAnimState::Idle:
		return IdleSequence;
	case ESarkoAnimState::IdleAiming:
		// Falls back to the plain idle rather than to nothing, so a missing aim
		// pose costs the extended arms and not the whole character standing in
		// its reference T-pose.
		return IdleAimingSequence ? IdleAimingSequence : IdleSequence;
	case ESarkoAnimState::Jog:
		return JogSequences.IsValidIndex(DirectionIndex) ? JogSequences[DirectionIndex].Get() : nullptr;
	case ESarkoAnimState::Reload:
		return ReloadSequence;
	case ESarkoAnimState::Fire:
		return FireSequence;
	case ESarkoAnimState::Death:
		return DeathSequence;
	default:
		return nullptr;
	}
}

void USarkoCharacterAnimComponent::Apply(ESarkoAnimState State, int32 DirectionIndex)
{
	if (bApplied && State == CurrentState && DirectionIndex == CurrentDirection)
	{
		return;
	}

	UAnimSequence* Sequence = SequenceFor(State, DirectionIndex);
	if (!Sequence || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		// No mesh yet (SarkoBody assigns it in the owner's BeginPlay, which runs
		// after this component's) or a sequence that failed to load. Leaving
		// bApplied false means the next tick tries again rather than latching a
		// state that was never actually shown.
		return;
	}

	// Only Jog and the two idles loop. A death or a reload that looped would
	// either resurrect the corpse every two seconds or restart the reload
	// forever; PlayAnimation's second argument is the whole difference.
	const bool bLooping = State == ESarkoAnimState::Idle
		|| State == ESarkoAnimState::IdleAiming
		|| State == ESarkoAnimState::Jog;

	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Sequence, bLooping);

	if (State == ESarkoAnimState::Reload)
	{
		// The reload the player waits out is USarkoRaidSettings::ReloadSeconds,
		// not whatever length this sequence happens to be. Stretching the
		// sequence to fit is what keeps the animation and the ammo counter
		// telling the same story instead of the pawn finishing early and
		// standing frozen mid-reload.
		const float Target = GetDefault<USarkoRaidSettings>()->ReloadSeconds;
		Mesh->SetPlayRate(SarkoAnimation::PlayRateForDuration(Sequence->GetPlayLength(), Target));
	}
	else
	{
		Mesh->SetPlayRate(1.f);
	}

	CurrentState = State;
	CurrentDirection = DirectionIndex;
	bApplied = true;

	if (State == ESarkoAnimState::Death)
	{
		// The corpse is final: nothing can change its state again, and the mesh
		// holds the sequence's last frame on its own. Ticking a dead pawn's
		// animation logic for the rest of the raid buys nothing.
		SetComponentTickEnabled(false);
	}
}

void USarkoCharacterAnimComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner || !Mesh)
	{
		return;
	}

	const bool bDead = Health && Health->IsDead();
	const bool bReloading = Weapon && Weapon->IsReloading();

	// A shot is inferred from the replicated magazine count dropping, so this
	// works on every machine without an animation RPC. The first tick only
	// records the count: the magazine is filled at BeginPlay, and a pawn that
	// started with rounds in it has not fired.
	const int32 Ammo = Weapon ? Weapon->GetAmmoInMagazine() : 0;
	const float Now = Owner->GetWorld() ? Owner->GetWorld()->GetTimeSeconds() : 0.f;
	if (PreviousAmmo != INDEX_NONE && Ammo < PreviousAmmo && FireSequence)
	{
		FireWindowEndSeconds = Now + FireSequence->GetPlayLength();
		if (CurrentState == ESarkoAnimState::Fire)
		{
			// A second shot inside the first one's window is still a second
			// shot: Apply short-circuits on an unchanged state, so without this
			// the pose would freeze rather than re-trigger. Only cleared for
			// Fire — doing it unconditionally would restart the run cycle from
			// frame zero every time a running pawn shot.
			bApplied = false;
		}
	}
	PreviousAmmo = Ammo;

	const FVector Velocity = Owner->GetVelocity();
	const bool bMoving = FVector(Velocity.X, Velocity.Y, 0.f).Size() > MovingSpeedThreshold;

	const ASarkoCharacter* PlayerPawn = Cast<ASarkoCharacter>(Owner);
	const bool bAiming = PlayerPawn && PlayerPawn->IsAiming();

	const ESarkoAnimState State = SarkoAnimation::ChooseState(bDead, bReloading, Now < FireWindowEndSeconds, bMoving, bAiming);

	const int32 Direction = State == ESarkoAnimState::Jog
		? SarkoAnimation::EightWayIndex(SarkoAnimation::RelativeYaw(Velocity, Owner->GetActorRotation().Yaw))
		: INDEX_NONE;

	Apply(State, Direction);
}
