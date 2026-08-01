#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "SarkoEnemyCharacter.generated.h"

class USarkoCharacterAnimComponent;
class USarkoHealthComponent;
class USarkoWeaponComponent;

/**
 * One enemy pawn. What kind of enemy it is arrives from the map's authored
 * `spawns[]` row through ApplyArchetypeAndPost, not from a subclass — the
 * archetype table is numbers (SarkoAI::GetBotArchetypes), and a numbers table
 * does not need three actor classes to express three rows.
 */
UCLASS()
class ASarkoEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASarkoEnemyCharacter();

	virtual void BeginPlay() override;

	/**
	 * Server only, called by the encounter director immediately after
	 * SpawnActor. Pushes the archetype's numbers into the components that own
	 * them and tells the controller where this bot holds.
	 *
	 * An archetype the table does not know is a loud no-op rather than a silent
	 * default: the map parser already refuses such a file, so reaching here with
	 * one means the table and the parser have drifted apart.
	 */
	void ApplyArchetypeAndPost(FName ArchetypeId, const FVector& PostPos, float LeashUU);

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
