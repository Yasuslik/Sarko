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
	 * KillZ. The pawn has left the world; ASarkoRaidGameMode::RecoverFallenPawn
	 * puts it back on the nearest player spawn instead of deleting it.
	 *
	 * Overridden rather than left to the engine because AActor::FellOutOfWorld
	 * DESTROYS the actor: for the player that is a raid lost with the whole haul
	 * in the bag, for a reason nobody can see, and for a bot it is an encounter
	 * that silently never finishes. Falling out is always a bug in the world —
	 * the border exists so it cannot happen — so the response is to log loudly
	 * and cost a second, not to punish the player for it. Super is still called
	 * when recovery is impossible, because falling forever is worse than dying.
	 */
	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

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
