#include "AI/SarkoEnemyCharacter.h"

#include "AI/SarkoAIController.h"
#include "Combat/SarkoWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Pawn/SarkoHealthComponent.h"

ASarkoEnemyCharacter::ASarkoEnemyCharacter()
{
	AIControllerClass = ASarkoAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	HealthComponent = CreateDefaultSubobject<USarkoHealthComponent>(TEXT("Health"));
	HealthComponent->SetTeam(ESarkoTeam::Enemy);
	WeaponComponent = CreateDefaultSubobject<USarkoWeaponComponent>(TEXT("Weapon"));

	GetCharacterMovement()->bOrientRotationToMovement = true;
	// No balance literals: the enemy's move speed is its own tunable, distinct
	// from the player's WalkSpeed (spec discipline — every number that affects
	// feel lives in USarkoRaidSettings so it can be tuned from the ini).
	GetCharacterMovement()->MaxWalkSpeed = GetDefault<USarkoRaidSettings>()->EnemyWalkSpeed;
}

void ASarkoEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoEnemyCharacter::HandleDeath);
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
