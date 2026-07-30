#include "Pawn/SarkoCharacter.h"

#include "Pawn/SarkoBody.h"

#include "Camera/CameraComponent.h"
#include "Combat/SarkoWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoLootContainer.h"
#include "Loot/SarkoLootTable.h"
#include "Map/SarkoMapDefinition.h"
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
	BackpackComponent = CreateDefaultSubobject<USarkoBackpackComponent>(TEXT("Backpack"));
}

void ASarkoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoCharacter, AimDirection);
}

void ASarkoCharacter::BeginPlay()
{
	Super::BeginPlay();

	// A visible body. ACharacter's mesh is empty by default, so without
	// this the player cannot see their own character at all.
	SarkoBody::AttachPlaceholderBody(*this, FLinearColor(0.25f, 0.5f, 0.95f));

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoCharacter::HandleDeath);
	}
}

void ASarkoCharacter::HandleDeath(AActor* Killer)
{
	// Spec §4.4: died means the haul is gone. This runs before the game mode is
	// told, so by the time a result is submitted there is nothing to credit.
	if (BackpackComponent)
	{
		BackpackComponent->ClearOnDeath();
	}

	// A corpse is not mid-loot. The channel's own per-tick CanInteract re-check
	// would catch this a frame later anyway; clearing it here means there is no
	// frame in which a dead pawn is still opening a crate.
	LootChannelIndex = INDEX_NONE;
	LocalChannelIndex = INDEX_NONE;

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveIntent = FVector2D::ZeroVector;
}

void ASarkoCharacter::RequestBeginLoot(int32 ContainerIndex)
{
	// Local copy first, so the progress bar starts moving this frame rather than
	// after a round trip — the same reason a shot is drawn before the server
	// confirms it (spec §10).
	LocalChannelIndex = ContainerIndex;
	LocalChannelStartSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (HasAuthority())
	{
		ServerBeginLoot_Implementation(ContainerIndex);
	}
	else
	{
		ServerBeginLoot(ContainerIndex);
	}
}

void ASarkoCharacter::RequestCancelLoot()
{
	LocalChannelIndex = INDEX_NONE;
	if (HasAuthority())
	{
		ServerCancelLoot_Implementation();
	}
	else
	{
		ServerCancelLoot();
	}
}

float ASarkoCharacter::GetLootChannelElapsed() const
{
	const int32 Index = HasAuthority() ? LootChannelIndex : LocalChannelIndex;
	const float Start = HasAuthority() ? LootChannelStartSeconds : LocalChannelStartSeconds;
	if (Index == INDEX_NONE || !GetWorld())
	{
		return 0.f;
	}
	return FMath::Max(0.f, GetWorld()->GetTimeSeconds() - Start);
}

void ASarkoCharacter::ServerBeginLoot_Implementation(int32 ContainerIndex)
{
	const ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState)
	{
		return;
	}

	// Bounds check before the index is used for anything at all.
	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: loot request for out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive, RaidState->IsContainerLooted(ContainerIndex)))
	{
		return;
	}

	LootChannelIndex = ContainerIndex;
	LootChannelStartSeconds = GetWorld()->GetTimeSeconds();
}

void ASarkoCharacter::ServerCancelLoot_Implementation()
{
	LootChannelIndex = INDEX_NONE;
}

void ASarkoCharacter::TickLootChannel()
{
	if (LootChannelIndex == INDEX_NONE)
	{
		return;
	}

	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState)
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(LootChannelIndex))
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const bool bAlive = HealthComponent && !HealthComponent->IsDead();

	// Re-checked every tick, not only at the start: walking away or dying
	// mid-channel must cancel it, and both are things the server sees first.
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[LootChannelIndex].Location,
			Settings.InteractRadiusUU, bAlive, RaidState->IsContainerLooted(LootChannelIndex)))
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	if (GetWorld()->GetTimeSeconds() - LootChannelStartSeconds < Settings.LootChannelSeconds)
	{
		return;
	}

	// Channel complete. Roll here and now — never ahead of time: pre-rolled
	// contents replicate and can be read out of memory, which is a loot map
	// (slice-1 spec §6.1).
	const int32 Index = LootChannelIndex;
	LootChannelIndex = INDEX_NONE;

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(Spots[Index].Tier);
	if (!Table)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoCharacter: container %d has tier '%s' with no loot table"),
			Index, *Spots[Index].Tier.ToString());
		RaidState->MarkContainerLooted(Index);
		return;
	}

	FRandomStream Stream(SarkoLoot::ContainerSeed(RaidState->Seed, Index));
	const TArray<FSarkoItemStack> Rolled = SarkoLoot::RollContainer(*Table, Stream);

	int32 Taken = 0;
	int32 LeftBehind = 0;
	for (const FSarkoItemStack& Stack : Rolled)
	{
		const int32 Leftover = BackpackComponent ? BackpackComponent->AddItem(Stack.Item, Stack.Quantity) : Stack.Quantity;
		Taken += Stack.Quantity - Leftover;
		LeftBehind += Leftover;
	}

	// Marked looted either way. Spec §4.3 allows partial loot, and re-opening a
	// container to fish out the remainder would re-run the roll and duplicate
	// the part already taken — the same roll, credited twice.
	RaidState->MarkContainerLooted(Index);

	UE_LOG(LogTemp, Display, TEXT("SarkoCharacter: looted container %d (tier %s): took %d units, left %d behind"),
		Index, *Spots[Index].Tier.ToString(), Taken, LeftBehind);
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

	const FVector Direction = FVector(AimDirection);

	if (HasAuthority())
	{
		WeaponComponent->ServerFire(GetMuzzleLocation(), Direction);
		return;
	}
	// Effects can be drawn locally right away; only the server decides the hit.
	ServerRequestFire(Direction);
}

void ASarkoCharacter::ServerRequestFire_Implementation(FVector Direction)
{
	// Re-check death on the server: RequestFire's client-side check can be
	// stale during the round trip before bDead replicates back down, and a
	// modified client could skip that check entirely. The server's own
	// HealthComponent is the only copy that can be trusted here.
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

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

	// The loot channel is the server's, and only the server's: it is what decides
	// whether the haul is real.
	if (HasAuthority())
	{
		TickLootChannel();
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
