#include "AI/SarkoEnemyCharacter.h"

#include "Pawn/SarkoBody.h"
#include "Pawn/SarkoCharacterAnim.h"

#include "AI/SarkoAIController.h"
#include "AI/SarkoBotArchetypes.h"
#include "Combat/SarkoWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Pawn/SarkoHealthComponent.h"

void ASarkoEnemyCharacter::FellOutOfWorld(const UDamageType& DmgType)
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

ASarkoEnemyCharacter::ASarkoEnemyCharacter()
{
	AIControllerClass = ASarkoAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	HealthComponent = CreateDefaultSubobject<USarkoHealthComponent>(TEXT("Health"));
	HealthComponent->SetTeam(ESarkoTeam::Enemy);
	WeaponComponent = CreateDefaultSubobject<USarkoWeaponComponent>(TEXT("Weapon"));
	// Same cosmetic driver the player uses, created after health and the weapon
	// so both exist by the time it looks for them (see its BeginPlay).
	AnimComponent = CreateDefaultSubobject<USarkoCharacterAnimComponent>(TEXT("CharacterAnim"));

	GetCharacterMovement()->bOrientRotationToMovement = true;
	// No balance literals: the enemy's move speed is its own tunable, distinct
	// from the player's WalkSpeed (spec discipline — every number that affects
	// feel lives in USarkoRaidSettings so it can be tuned from the ini).
	GetCharacterMovement()->MaxWalkSpeed = GetDefault<USarkoRaidSettings>()->EnemyWalkSpeed;
}

void ASarkoEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Quinn and a red tint, so it reads as hostile at a glance from directly
	// above — a different body from the player's Manny, not the same one
	// recoloured, so friend/foe survives even if the tint does nothing.
	SarkoBody::AttachCharacterMesh(*this, SarkoBody::ESide::Enemy);

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoEnemyCharacter::HandleDeath);
	}
}

void ASarkoEnemyCharacter::ApplyArchetypeAndPost(FName ArchetypeId, const FVector& PostPos, float LeashUU)
{
	if (!HasAuthority())
	{
		return;
	}

	// The post first, and unconditionally: a bot with no leash is a bot that
	// random-walks the sector, and that must not depend on the archetype name
	// resolving.
	if (ASarkoAIController* AIController = Cast<ASarkoAIController>(GetController()))
	{
		AIController->SetPost(PostPos, LeashUU);
	}

	FSarkoBotArchetype Archetype;
	if (!SarkoAI::FindBotArchetype(ArchetypeId, Archetype))
	{
		UE_LOG(LogTemp, Error,
			TEXT("SarkoEnemy: archetype '%s' is not in the archetype table — the map parser should have refused this file"),
			*ArchetypeId.ToString());
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = Archetype.WalkSpeed;
	if (HealthComponent)
	{
		HealthComponent->InitialiseMaxHealth(Archetype.MaxHealth);
	}
	if (WeaponComponent)
	{
		WeaponComponent->SetDamageOverride(Archetype.Damage);
	}
	if (ASarkoAIController* AIController = Cast<ASarkoAIController>(GetController()))
	{
		AIController->SetPerception(Archetype.HearingRadiusUU, Archetype.FiringRangeUU, Archetype.FireIntervalSeconds);
	}
}

void ASarkoEnemyCharacter::HandleDeath(AActor* Killer)
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The corpse stays as a marker; loot arrives in the next plan.
	SetLifeSpan(60.f);
}
