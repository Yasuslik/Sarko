#include "Pawn/SarkoCharacter.h"

#include "Pawn/SarkoBody.h"
#include "Pawn/SarkoCharacterAnim.h"
#include "Pawn/SarkoSurvival.h"
#include "Pawn/SarkoWeaponVisuals.h"

#include "AI/SarkoNoise.h"
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
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

void ASarkoCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	// One net, called from two pawn classes that share no base of their own.
	if (ASarkoRaidGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr)
	{
		if (Mode->RecoverFallenPawn(*this))
		{
			return;
		}
	}
	// Could not recover — no authority, no raid game mode, or no layout to
	// return to. The engine's own behaviour (destroy) is the lesser evil: a pawn
	// that is neither destroyed nor moved keeps falling for the rest of the raid.
	Super::FellOutOfWorld(DmgType);
}

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
	// After the health component it reads and the backpack it spends from, so
	// both exist by the time its own BeginPlay runs (components begin in
	// creation order).
	SurvivalComponent = CreateDefaultSubobject<USarkoSurvivalComponent>(TEXT("Survival"));

	// Created last on purpose: it finds the health and weapon components by
	// class in BeginPlay, and component BeginPlay runs in creation order, so
	// both are already initialised by the time it looks for them.
	AnimComponent = CreateDefaultSubobject<USarkoCharacterAnimComponent>(TEXT("CharacterAnim"));
}

void ASarkoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoCharacter, AimDirection);
	// Everyone, deliberately — see the property's comment. What a pawn is
	// carrying is visible across a yard, so it cannot be owner-only.
	DOREPLIFETIME(ASarkoCharacter, EquippedWeaponItem);
	// Owner-only for the same reason the backpack is: "that player is extracting"
	// is the single most valuable thing an opponent could know.
	DOREPLIFETIME_CONDITION(ASarkoCharacter, ExtractZoneIndex, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASarkoCharacter, ExtractDwellSeconds, COND_OwnerOnly);
}

void ASarkoCharacter::BeginPlay()
{
	Super::BeginPlay();

	// A visible body. ACharacter's mesh is empty by default, so without
	// this the player cannot see their own character at all.
	SarkoBody::AttachCharacterMesh(*this, SarkoBody::ESide::Player);

	// And something in its hand. After the body, because the weapon hangs off
	// that mesh's `hand_r` bone and there is no bone to find before the skeletal
	// mesh is assigned. Runs on every machine: the id is replicated, the mesh is
	// local, and the server's own pawn gets no OnRep to do it for it.
	SarkoWeaponVisuals::SetHeldWeapon(*this, EquippedWeaponItem);

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoCharacter::HandleDeath);
	}
}

void ASarkoCharacter::SetEquippedWeaponItem(FName ItemId)
{
	if (!HasAuthority() || EquippedWeaponItem == ItemId)
	{
		return;
	}
	EquippedWeaponItem = ItemId;
	// The server holds no OnRep for its own change, so the listen host would
	// keep drawing the old gun without this.
	SarkoWeaponVisuals::SetHeldWeapon(*this, EquippedWeaponItem);
}

void ASarkoCharacter::OnRep_EquippedWeaponItem()
{
	SarkoWeaponVisuals::SetHeldWeapon(*this, EquippedWeaponItem);
}

void ASarkoCharacter::HandleDeath(AActor* Killer)
{
	// A death after the outcome is settled must be impossible — the raid's own
	// ApplyDamage gate refuses the hit and the bots stand down — so reaching here
	// is a bug. It must not be a bug that costs the player their raid: FinishRaid
	// would correctly refuse the Died outcome, but the side-effects below would
	// still land, and ClearOnDeath() on an already-EXTRACTED raid empties the very
	// haul the summary is about to itemise and the backend is about to be told
	// about. So the settled result wins over the late death, loudly.
	if (IsRaidFinishedNow())
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: death arrived after the raid was already settled; the outcome and the haul stand"));
		return;
	}

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
	// Nor is a corpse standing over an open crate. Cleared on both sides, so no
	// panel survives the death that emptied the bag it was filling.
	OpenContainerIndex = INDEX_NONE;
	LocalOpenContainerIndex = INDEX_NONE;
	LocalOpenContainerSlots.Reset();

	// Nor is a corpse mid-extraction. Cleared before the game mode is told, so
	// the summary cannot flash a countdown from the frame the player died on.
	ExtractZoneIndex = INDEX_NONE;
	ExtractDwellSeconds = 0.f;

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveIntent = FVector2D::ZeroVector;
	MoveScale = 0.f;

	// The game mode owns the raid's outcome; the pawn only reports its own
	// death. KIA is this path and nothing else — the death handling above is not
	// duplicated there.
	if (ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr)
	{
		GameMode->HandlePlayerDied(this);
	}
}

void ASarkoCharacter::SetExtractProgress(int32 ZoneIndex, float DwellSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	ExtractZoneIndex = ZoneIndex;
	ExtractDwellSeconds = DwellSeconds;
}

bool ASarkoCharacter::IsRaidFinishedNow() const
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	return RaidState && RaidState->IsRaidFinished();
}

void ASarkoCharacter::FreezeForRaidEnd()
{
	if (!HasAuthority())
	{
		return;
	}

	// Collision is deliberately left alone, unlike HandleDeath: an extracted
	// player is still a solid body standing on the pad while the summary shows.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	MoveIntent = FVector2D::ZeroVector;
	MoveScale = 0.f;
	bIsAiming = false;
	LootChannelIndex = INDEX_NONE;
	LocalChannelIndex = INDEX_NONE;
	OpenContainerIndex = INDEX_NONE;
	LocalOpenContainerIndex = INDEX_NONE;
	LocalOpenContainerSlots.Reset();

	// Nor is a frozen pawn mid-extraction, for the same reason HandleDeath clears
	// these: the dwell chip is drawn from them, so leaving them set draws a live
	// countdown underneath the summary screen for the rest of the session — the
	// frozen frame's remainder, ticking nowhere.
	ExtractZoneIndex = INDEX_NONE;
	ExtractDwellSeconds = 0.f;
}

void ASarkoCharacter::RequestBeginLoot(int32 ContainerIndex)
{
	if (IsRaidFinishedNow())
	{
		return;
	}

	// Local copy first, so the progress bar starts moving this frame rather than
	// after a round trip — the same reason a shot is drawn before the server
	// confirms it (spec §10). Optimistic, so the server has to be able to take it
	// back: ClientLootRejected clears it on every refusal, or a refused begin
	// leaves the bar full for as long as the button is held.
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
	// IsLootable() rather than !IsRaidFinished(): it also refuses a raid whose
	// authoritative seed has not landed yet (the one HTTP round trip between
	// StartPlay and ASarkoRaidGameMode::ActivateRaid). Refusing is the safe
	// direction — a roll against the placeholder seed is a roll that disagrees with
	// every later re-derivation of the same container.
	if (!GameMode || !RaidState || !RaidState->IsLootable())
	{
		ClientLootRejected(ContainerIndex);
		return;
	}

	// Bounds check before the index is used for anything at all.
	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: loot request for out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		ClientLootRejected(ContainerIndex);
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive, RaidState->IsContainerEmptied(ContainerIndex)))
	{
		// Every refusal tells the client, so the optimistic bar RequestBeginLoot
		// started does not stay pinned at full for the rest of the hold.
		ClientLootRejected(ContainerIndex);
		return;
	}

	if (RaidState->IsContainerOpened(ContainerIndex))
	{
		// The channel is the price of DISCOVERY, and it has been paid. Re-opening
		// a crate you already emptied halfway must not cost another second and a
		// half of standing still in the open.
		ClientLootRejected(ContainerIndex);
		OpenContainerFor(ContainerIndex);
		return;
	}

	LootChannelIndex = ContainerIndex;
	LootChannelStartSeconds = GetWorld()->GetTimeSeconds();
}

void ASarkoCharacter::ClientLootRejected_Implementation(int32 ContainerIndex)
{
	// Only the request that was actually refused. A player standing between two
	// crates can have moved on to a second index before this lands (the controller
	// re-requests when the nearest container changes mid-hold), and clearing that
	// newer channel would stall a begin the server did accept.
	if (LocalChannelIndex == ContainerIndex)
	{
		LocalChannelIndex = INDEX_NONE;
	}
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
			Settings.InteractRadiusUU, bAlive, RaidState->IsContainerEmptied(LootChannelIndex)))
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	if (GetWorld()->GetTimeSeconds() - LootChannelStartSeconds < Settings.LootChannelSeconds)
	{
		return;
	}

	// Channel complete. The 1.5 s buys an OPEN, not a haul: opening is the risk,
	// taking is fast (spec §4). Nothing is rolled or transferred inline any more,
	// which is why CompleteLootChannel — and its unconditional Mark, which
	// destroyed whatever did not fit — no longer exists.
	const int32 Index = LootChannelIndex;
	LootChannelIndex = INDEX_NONE;
	OpenContainerFor(Index);
}

void ASarkoCharacter::OpenContainerFor(int32 ContainerIndex)
{
	// OPENING A CRATE MAKES NO NOISE, and that is a decision rather than an
	// omission (spec §7; the design study's container-noise idea was deferred).
	// Looting already costs LootChannelSeconds of standing perfectly still in
	// the open, which is the one moment the player is defenceless; charging that
	// moment a second time by summoning the guard would make searching strictly
	// worse than shooting the guard first. If this is ever revisited, one
	// `Noise->ReportNoise(GetActorLocation(), SarkoNoise::EKind::Quiet, this)`
	// here is the whole change — see AI/SarkoNoise.h.
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}
	TArray<FSarkoItemStack>* Inventory = GameMode->OpenContainerAt(ContainerIndex);
	if (!Inventory)
	{
		return;
	}

	OpenContainerIndex = ContainerIndex;
	UE_LOG(LogTemp, Display, TEXT("SarkoCharacter: opened container %d, holding %d stack(s)"),
		ContainerIndex, Inventory->Num());

	// Opening takes nothing. The client is told what is in the crate and the
	// player decides, one cell at a time — which is the entire point of this
	// feature and the defect the plan set out to fix: a hold used to vacuum the
	// container and evaporate whatever did not fit.
	ClientContainerContents(ContainerIndex, *Inventory);
}

bool ASarkoCharacter::TakeSlotInto(TArray<FSarkoItemStack>& Inventory, int32 SlotIndex,
	TArray<FSarkoItemStack>& Bag)
{
	if (!Inventory.IsValidIndex(SlotIndex) || Inventory[SlotIndex].Quantity <= 0)
	{
		return false;
	}

	// A backpack is worn, not carried: it does not occupy a cell, which is the
	// only way spec §2.3's 4 + 8 = 12 adds up. A SECOND backpack is ordinary
	// loot and takes a cell like anything else — it is worth carrying home.
	if (Inventory[SlotIndex].Item == SarkoLoot::BackpackItemId
		&& BackpackComponent && !BackpackComponent->IsWearingBackpack())
	{
		BackpackComponent->EquipBackpack(SarkoLoot::BackpackItemId);
		// Decrement rather than RemoveAt: a slot that somehow held two bags would
		// otherwise lose the second one, which is the very thing this task exists
		// to stop happening.
		if (--Inventory[SlotIndex].Quantity <= 0)
		{
			Inventory.RemoveAt(SlotIndex);
		}
		return true;
	}

	return SarkoLoot::TransferOne(Inventory, SlotIndex, Bag, SarkoLoot::GetItemCatalog(),
		BackpackComponent ? BackpackComponent->GetCarryPages() : TArray<FSarkoGridPage>()) > 0;
}

void ASarkoCharacter::TakeAllFrom(int32 ContainerIndex)
{
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	TArray<FSarkoItemStack>* Inventory = GameMode ? GameMode->FindContainerInventory(ContainerIndex) : nullptr;
	if (!Inventory || !BackpackComponent)
	{
		return;
	}

	// Slot 0 repeatedly, because a drained slot is REMOVED and the next one
	// shifts down into its place. Bounded by the grid size rather than by
	// "until it stops moving" alone, so no data file can turn this into a spin.
	TArray<FSarkoItemStack> Bag = BackpackComponent->GetSlots();
	int32 Moved = 0;
	for (int32 Guard = 0; Guard < SarkoLoot::ContainerCells && Inventory->Num() > 0; ++Guard)
	{
		if (!TakeSlotInto(*Inventory, 0, Bag))
		{
			// The first refusal ends the pass: slot 0 is the only slot tried, and
			// what does not fit now will not fit later. The rest simply stays.
			break;
		}
		++Moved;
	}

	if (Moved > 0)
	{
		BackpackComponent->SetSlots(Bag);
	}
	// Once, not per item: one write-back, one emptied check, one client push.
	FinishTransfer(ContainerIndex, *Inventory);
}

void ASarkoCharacter::FinishTransfer(int32 ContainerIndex, const TArray<FSarkoItemStack>& Inventory)
{
	ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && Inventory.Num() == 0)
	{
		// Emptied only now, and only because it is ACTUALLY empty. This one line
		// is the vanishing-loot fix: the old code marked here unconditionally.
		RaidState->SetContainerState(ContainerIndex, ESarkoContainerState::Emptied);
	}
	ClientContainerContents(ContainerIndex, Inventory);
}

void ASarkoCharacter::ServerTakeItem_Implementation(int32 ContainerIndex, int32 SlotIndex)
{
	// Spec §3's order, unchanged and not to be reordered: raid live and not
	// settled -> index in range -> container opened -> pawn alive -> server
	// re-measured distance -> slot non-empty -> capacity available. Anything that
	// fails logs and changes nothing. This mirrors ServerBeginLoot, which is
	// already hostile-input safe, and every index below is checked before it
	// indexes anything at all.
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState || !RaidState->IsLootable())
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::RaidOver);
		return;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: take from out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::Gone);
		return;
	}

	TArray<FSarkoItemStack>* Inventory = GameMode->FindContainerInventory(ContainerIndex);
	if (!Inventory)
	{
		// Never opened. A client asking to take from a container it has not
		// opened is asking for the loot map; it gets nothing and it learns nothing.
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::NotOpen);
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	// The server's OWN copy of this pawn's location, re-measured. A client-supplied
	// position would be pointless to send and pointless to trust, exactly as in
	// ServerRequestFire.
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive,
			RaidState->IsContainerEmptied(ContainerIndex)))
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::TooFar);
		return;
	}

	if (!Inventory->IsValidIndex(SlotIndex))
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::Gone);
		return;
	}

	if (!BackpackComponent)
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::NoSpace);
		return;
	}

	TArray<FSarkoItemStack> Bag = BackpackComponent->GetSlots();
	if (!TakeSlotInto(*Inventory, SlotIndex, Bag))
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::NoSpace);
		return;
	}
	// Server-side write-back of the cells this pawn now carries.
	BackpackComponent->SetSlots(Bag);
	FinishTransfer(ContainerIndex, *Inventory);
}

void ASarkoCharacter::ServerTakeAll_Implementation(int32 ContainerIndex)
{
	// The same §3 chain, run ONCE, then the loop. Deliberately not four calls to
	// ServerTakeItem_Implementation: that would re-validate per item and send
	// four client RPCs for one button press.
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState || !RaidState->IsLootable())
	{
		ClientTransferRefused(ContainerIndex, INDEX_NONE, ESarkoTakeRefusal::RaidOver);
		return;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: take-all from out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		ClientTransferRefused(ContainerIndex, INDEX_NONE, ESarkoTakeRefusal::Gone);
		return;
	}

	if (!GameMode->FindContainerInventory(ContainerIndex))
	{
		ClientTransferRefused(ContainerIndex, INDEX_NONE, ESarkoTakeRefusal::NotOpen);
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive,
			RaidState->IsContainerEmptied(ContainerIndex)))
	{
		ClientTransferRefused(ContainerIndex, INDEX_NONE, ESarkoTakeRefusal::TooFar);
		return;
	}

	TakeAllFrom(ContainerIndex);
}

void ASarkoCharacter::ServerCloseContainer_Implementation()
{
	const int32 Closed = OpenContainerIndex;
	OpenContainerIndex = INDEX_NONE;
	if (Closed != INDEX_NONE)
	{
		ClientContainerClosed(Closed);
	}
}

void ASarkoCharacter::ClientContainerContents_Implementation(int32 ContainerIndex,
	const TArray<FSarkoItemStack>& Slots)
{
	LocalOpenContainerIndex = ContainerIndex;
	LocalOpenContainerSlots = Slots;
	OnContainerViewChanged.Broadcast();
}

void ASarkoCharacter::ClientContainerClosed_Implementation(int32 ContainerIndex)
{
	// Only the container that was actually closed: a player who walked from one
	// crate straight into another can have an open panel for a newer index by the
	// time this lands, and clearing that would blank a panel the server kept.
	if (LocalOpenContainerIndex != ContainerIndex)
	{
		return;
	}
	LocalOpenContainerIndex = INDEX_NONE;
	LocalOpenContainerSlots.Reset();
	OnContainerViewChanged.Broadcast();
}

void ASarkoCharacter::ClientTransferRefused_Implementation(int32 ContainerIndex, int32 SlotIndex,
	ESarkoTakeRefusal Reason)
{
	OnTakeRefused.Broadcast(SlotIndex, Reason);
}

void ASarkoCharacter::RequestTakeItem(int32 ContainerIndex, int32 SlotIndex)
{
	if (IsRaidFinishedNow())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerTakeItem_Implementation(ContainerIndex, SlotIndex);
	}
	else
	{
		ServerTakeItem(ContainerIndex, SlotIndex);
	}
}

void ASarkoCharacter::RequestConsumeItem(int32 SlotIndex)
{
	if (IsRaidFinishedNow())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerConsumeItem_Implementation(SlotIndex);
	}
	else
	{
		ServerConsumeItem(SlotIndex);
	}
}

void ASarkoCharacter::ServerConsumeItem_Implementation(int32 SlotIndex)
{
	// The same shape as ServerTakeItem's §3 chain, minus the container half:
	// raid live and not settled -> pawn alive -> the pawn has cells -> the cell
	// holds a consumable. There is no distance to re-measure, because the item is
	// already in the player's own hands; everything else is re-derived from the
	// server's copy and nothing is taken from the client but the index.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsLootable())
	{
		return;
	}
	if (!HealthComponent || HealthComponent->IsDead())
	{
		return;
	}
	if (!BackpackComponent || !SurvivalComponent)
	{
		return;
	}

	// A working copy, written back once, exactly as the take path does — so the
	// cells and the meters move together or not at all.
	TArray<FSarkoItemStack> Bag = BackpackComponent->GetSlots();
	if (!SurvivalComponent->ConsumeFromBag(Bag, SlotIndex))
	{
		// Silently: an out-of-range or non-consumable index is either a stale
		// panel or a hostile client, and neither deserves an answer that tells
		// them what IS in that cell.
		return;
	}
	BackpackComponent->SetSlots(Bag);

	UE_LOG(LogTemp, Display,
		TEXT("SarkoCharacter: consumed from cell %d — food %.0f%%, water %.0f%%, health %.0f"),
		SlotIndex, SurvivalComponent->GetFoodExact(), SurvivalComponent->GetWaterExact(),
		HealthComponent->GetHealth());

	if (IsLocallyControlled())
	{
		// Standalone/listen server: there is no RPC to travel, so the panel's
		// delegate is broadcast here. The controller still defers the rebuild.
		OnContainerViewChanged.Broadcast();
	}
	else
	{
		ClientConsumeApplied();
	}
}

void ASarkoCharacter::ClientConsumeApplied_Implementation()
{
	// Deferred by the controller (HandleContainerViewChanged), which is the whole
	// point of routing through this delegate: this can land inside
	// SButton::ExecuteOnClick on a listen server, and rebuilding the grid there
	// destroys the button mid-click.
	OnContainerViewChanged.Broadcast();
}

void ASarkoCharacter::RequestTakeAll(int32 ContainerIndex)
{
	if (IsRaidFinishedNow())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerTakeAll_Implementation(ContainerIndex);
	}
	else
	{
		ServerTakeAll(ContainerIndex);
	}
}

void ASarkoCharacter::RequestCloseContainer()
{
	// The local mirror clears immediately: closing a panel must never wait for a
	// round trip, and the server's own close is idempotent.
	LocalOpenContainerIndex = INDEX_NONE;
	LocalOpenContainerSlots.Reset();
	OnContainerViewChanged.Broadcast();

	if (HasAuthority())
	{
		ServerCloseContainer_Implementation();
	}
	else
	{
		ServerCloseContainer();
	}
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
	// A finished raid does not accept aim either: leaving this open would let a
	// non-cooperating client keep swinging its pawn around under the summary
	// screen, and aim is what the next fire request is taken from.
	if (IsRaidFinishedNow())
	{
		return;
	}
	AimDirection = NewAim;
	bIsAiming = bInIsAiming;
}

void ASarkoCharacter::ServerSetAimState_Implementation(bool bInIsAiming)
{
	if (IsRaidFinishedNow())
	{
		return;
	}
	bIsAiming = bInIsAiming;
}

FVector ASarkoCharacter::GetMuzzleLocation() const
{
	// Chest height, slightly ahead of the capsule.
	return GetActorLocation() + FVector(0.f, 0.f, 40.f) + FVector(AimDirection) * 60.f;
}

void ASarkoCharacter::RequestReload()
{
	if (!WeaponComponent || (HealthComponent && HealthComponent->IsDead()) || IsRaidFinishedNow())
	{
		return;
	}
	// Nothing to do, and refused rather than started: a reload at a full magazine
	// would cost ReloadSeconds of a weapon that cannot shoot, which is a worse
	// outcome than the press doing nothing.
	if (WeaponComponent->IsReloading()
		|| WeaponComponent->GetAmmoInMagazine() >= GetDefault<USarkoRaidSettings>()->MagazineSize)
	{
		return;
	}

	if (HasAuthority())
	{
		WeaponComponent->StartReload();
		return;
	}
	ServerRequestReload();
}

void ASarkoCharacter::ServerRequestReload_Implementation()
{
	// Re-checked on the server for the same reason ServerRequestFire re-checks
	// death and the raid's outcome: the client's copy can be stale during the
	// round trip, and a modified client can skip the check entirely.
	if (!WeaponComponent || (HealthComponent && HealthComponent->IsDead()) || IsRaidFinishedNow())
	{
		return;
	}
	// StartReload already no-ops while a reload is in flight and already refuses
	// without authority, so this is safe to call unconditionally from here.
	WeaponComponent->StartReload();
}

void ASarkoCharacter::RequestFire()
{
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

	// Checked here as well as in ServerRequestFire_Implementation: on a listen
	// server or a standalone run the authority path below calls the weapon
	// directly and never passes through the RPC's gate.
	if (IsRaidFinishedNow())
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

	// Same reasoning, for the raid's outcome: once the raid is over the server
	// stops honouring fire requests, rather than trusting the client to stop
	// sending them. A shot fired after an extraction would damage a pawn in a
	// raid whose result has already been decided.
	if (IsRaidFinishedNow())
	{
		return;
	}

	// The server re-derives the origin from its own copy of the pawn so a
	// client cannot shoot from an arbitrary position.
	WeaponComponent->ServerFire(GetMuzzleLocation(), Direction);
}

void ASarkoCharacter::NotifyHitConfirmed(const FVector& VictimLocation)
{
	if (!HasAuthority())
	{
		return;
	}
	// One RPC per connecting shot, which is a few times a fight. On a listen
	// server or a standalone raid the owning connection is this machine, so the
	// implementation below simply runs here — which is what makes the marker
	// photographable from a headless -game run at all.
	ClientHitConfirmed(VictimLocation);
}

void ASarkoCharacter::ClientHitConfirmed_Implementation(FVector_NetQuantize VictimLocation)
{
	HitMarkerLocation = VictimLocation;
	HitMarkerSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

bool ASarkoCharacter::GetHitMarker(FVector& OutVictimLocation, float& OutAgeSeconds) const
{
	const UWorld* World = GetWorld();
	if (!World || HitMarkerSeconds < 0.f)
	{
		return false;
	}
	const float Age = World->GetTimeSeconds() - HitMarkerSeconds;
	if (Age < 0.f || Age > GetDefault<USarkoRaidSettings>()->HitMarkerSeconds)
	{
		return false;
	}
	OutVictimLocation = HitMarkerLocation;
	OutAgeSeconds = Age;
	return true;
}

void ASarkoCharacter::ReportMovementNoise(float DeltaSeconds)
{
	// SERVER ONLY, and from the server's own copy of the velocity (spec §7). The
	// stick deflection that produced it lives on the client and is not replicated;
	// the resulting speed is, because movement is. So "how loudly is this player
	// moving" is a question the server can answer without trusting anybody — which
	// is the whole reason the threshold is a fraction of MaxWalkSpeed rather than
	// a stick value.
	//
	// The PLAYER's pawn and not every pawn: bots do not sneak, and bots hearing
	// each other's footsteps would turn four patrolling scavs into a standing
	// wave of mutual investigation. A bot's SHOT is still heard (SarkoWeapon).
	UWorld* World = GetWorld();
	if (!World || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Now = World->GetTimeSeconds();
	// Throttled: a report per frame would churn the ring sixty times a second to
	// say what one entry already says, and the bot walking to a footstep 0.25 s
	// old goes to a point 100 uu from the one it would otherwise have picked.
	if (Now - LastNoiseReportSeconds < FMath::Max(0.01f, Settings.NoiseMovementIntervalSeconds))
	{
		return;
	}
	LastNoiseReportSeconds = Now;

	USarkoNoiseSubsystem* Noise = World->GetSubsystem<USarkoNoiseSubsystem>();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Noise || !Movement)
	{
		return;
	}

	// KindForSpeed maps a zero-ish speed to Silent and ReportNoise drops it, so
	// standing still costs a division and nothing else. Standing still is the one
	// state with no event at all.
	const float Speed = GetVelocity().Size2D();
	Noise->ReportMovementNoise(GetActorLocation(), Speed, Movement->MaxWalkSpeed, this);

	if (Settings.bLogAIDiagnostics)
	{
		// SILENCE IS INDISTINGUISHABLE FROM BROKEN in a log that only speaks when
		// a noise is made — a run that produces no movement events cannot tell
		// "the player was walking quietly" from "the player never moved". This
		// line is the difference, and it is four times a second on a debug flag
		// that is off by default.
		UE_LOG(LogTemp, Log, TEXT("SarkoNoise: player at %.0f uu/s of %.0f (%.0f%% deflection) -> %s"),
			Speed, Movement->MaxWalkSpeed,
			Movement->MaxWalkSpeed > 0.f ? Speed / Movement->MaxWalkSpeed * 100.f : 0.f,
			Speed / FMath::Max(1.f, Movement->MaxWalkSpeed) >= Settings.NoiseRunSpeedFraction ? TEXT("RUNNING, audible")
				: (Speed / FMath::Max(1.f, Movement->MaxWalkSpeed) >= Settings.NoiseMoveSpeedFraction ? TEXT("walking, quiet")
					: TEXT("standing, SILENT")));
	}
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
		ReportMovementNoise(DeltaSeconds);
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
