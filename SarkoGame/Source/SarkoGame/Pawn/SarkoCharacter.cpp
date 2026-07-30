#include "Pawn/SarkoCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/SarkoWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

FVector2D SarkoAim::StickToWorldDirection(FVector2D Stick, float CameraYaw)
{
	if (Stick.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}

	// Screen "up" (+Y on the stick) is world forward for an unrotated camera.
	const FVector Planar(Stick.Y, Stick.X, 0.f);
	const FVector Rotated = FRotator(0.f, CameraYaw, 0.f).RotateVector(Planar);
	const FVector Normalised = Rotated.GetSafeNormal();
	return FVector2D(Normalised.X, Normalised.Y);
}

float SarkoAim::MoveIntentScale(FVector2D Stick, float DeadZone)
{
	const float Magnitude = Stick.Size();
	if (Magnitude < DeadZone)
	{
		return 0.f;
	}
	return FMath::Min(Magnitude, 1.f);
}

ASarkoCharacter::ASarkoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
	Movement->MaxWalkSpeed = GetDefault<USarkoRaidSettings>()->WalkSpeed;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 1400.f;
	CameraBoom->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;
	// bUsePawnControlRotation only stops the boom following the controller's
	// view rotation; bInherit{Pitch,Yaw,Roll} default true and make the boom
	// follow the *actor's* rotation instead, which Tick changes every frame
	// to face aim or travel. Without turning all three off, the "top-down"
	// camera would spin with the character instead of holding a fixed world
	// orientation.
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom);
	TopDownCamera->bUsePawnControlRotation = false;

	HealthComponent = CreateDefaultSubobject<USarkoHealthComponent>(TEXT("Health"));
	WeaponComponent = CreateDefaultSubobject<USarkoWeaponComponent>(TEXT("Weapon"));
}

void ASarkoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoCharacter, AimDirection);
}

void ASarkoCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoCharacter::HandleDeath);
	}
}

void ASarkoCharacter::HandleDeath(AActor* Killer)
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveIntent = FVector2D::ZeroVector;
}

void ASarkoCharacter::SetMoveIntent(FVector2D Intent)
{
	const float DeadZone = GetDefault<USarkoRaidSettings>()->MoveStickDeadZone;
	MoveScale = SarkoAim::MoveIntentScale(Intent, DeadZone);
	// Keep the direction normalised — MoveScale alone carries the deflection
	// magnitude, so AddMovementInput's scale argument gives partial push
	// partial speed instead of every non-zero push snapping to WalkSpeed.
	MoveIntent = MoveScale > 0.f ? Intent.GetSafeNormal() : FVector2D::ZeroVector;
}

void ASarkoCharacter::SetAimIntent(FVector2D Intent, bool bInIsAiming)
{
	const bool bAimingStateChanged = bInIsAiming != bIsAiming;
	bIsAiming = bInIsAiming;

	FVector NewAim(AimDirection);
	const bool bHasDirection = !Intent.IsNearlyZero();
	if (bHasDirection)
	{
		// A centred stick must not overwrite AimDirection with a zero vector —
		// the pawn should keep facing where it last aimed, not snap to a
		// default facing.
		NewAim = FVector(Intent.X, Intent.Y, 0.f).GetSafeNormal();
		AimDirection = NewAim;
	}

	// The client applies its aim locally for responsiveness and tells the
	// server, which republishes it. The server never trusts it for damage —
	// it re-traces from its own copy when the shot is taken.
	if (HasAuthority())
	{
		return;
	}

	if (bHasDirection)
	{
		// Continuous, high-frequency while the stick is deflected: an
		// occasional dropped packet is corrected by the next frame, so this
		// stays unreliable.
		ServerSetAim(NewAim, bInIsAiming);
	}
	else if (bAimingStateChanged)
	{
		// The stick just centred — releasing aim is the normal, every-shot
		// gesture, and this is the only signal that tells the server it
		// happened. Unlike the continuous updates above, a dropped packet
		// here pins the server's bIsAiming forever, so this goes out
		// reliably instead.
		ServerSetAimState(bInIsAiming);
	}
}

void ASarkoCharacter::ServerSetAim_Implementation(FVector_NetQuantizeNormal NewAim, bool bInIsAiming)
{
	AimDirection = NewAim;
	bIsAiming = bInIsAiming;
}

void ASarkoCharacter::ServerSetAimState_Implementation(bool bInIsAiming)
{
	bIsAiming = bInIsAiming;
}

FVector ASarkoCharacter::GetMuzzleLocation() const
{
	// Chest height, slightly ahead of the capsule.
	return GetActorLocation() + FVector(0.f, 0.f, 40.f) + FVector(AimDirection) * 60.f;
}

void ASarkoCharacter::RequestFire()
{
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

	const FVector Origin = GetMuzzleLocation();
	const FVector Direction = FVector(AimDirection);

	if (HasAuthority())
	{
		WeaponComponent->ServerFire(Origin, Direction);
		return;
	}
	// Effects can be drawn locally right away; only the server decides the hit.
	ServerRequestFire(Origin, Direction);
}

void ASarkoCharacter::ServerRequestFire_Implementation(FVector Origin, FVector Direction)
{
	// The server re-derives the origin from its own copy of the pawn so a
	// client cannot shoot from an arbitrary position.
	WeaponComponent->ServerFire(GetMuzzleLocation(), Direction);
}

void ASarkoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (MoveScale > 0.f)
	{
		AddMovementInput(FVector(MoveIntent.X, MoveIntent.Y, 0.f), MoveScale);
	}

	// A corpse must not keep rotating to face its last aim direction.
	const bool bIsDead = HealthComponent && HealthComponent->IsDead();

	// Face the aim while aiming, otherwise face travel — spec §9.
	const FVector Facing = bIsAiming
		? FVector(AimDirection)
		: FVector(MoveIntent.X, MoveIntent.Y, 0.f);

	if (!bIsDead && !Facing.IsNearlyZero())
	{
		const FRotator Target(0.f, Facing.Rotation().Yaw, 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Target, DeltaSeconds, 12.f));
	}
}
