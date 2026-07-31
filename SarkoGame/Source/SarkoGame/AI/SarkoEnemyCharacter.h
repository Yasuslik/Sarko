#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "SarkoEnemyCharacter.generated.h"

class USarkoCharacterAnimComponent;
class USarkoHealthComponent;
class USarkoWeaponComponent;

/** One enemy archetype — the slice deliberately has exactly one. */
UCLASS()
class ASarkoEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASarkoEnemyCharacter();

	virtual void BeginPlay() override;

protected:
	void HandleDeath(AActor* Killer);

	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<USarkoHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<USarkoWeaponComponent> WeaponComponent;

	/** Drives the mesh's pose. Purely cosmetic; the same component the player uses. */
	UPROPERTY(VisibleAnywhere, Category = "Visuals")
	TObjectPtr<USarkoCharacterAnimComponent> AnimComponent;
};
