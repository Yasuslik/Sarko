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
	 * Drives the white hit flash (spec §4.3), and nothing else.
	 *
	 * On EVERY machine, not just the server: the flash is a fact about a body
	 * somebody is looking at. It is driven by polling
	 * USarkoHealthComponent::GetDamageSerial rather than by a delegate or a
	 * multicast, because a counter that replicates works identically on a client
	 * (where the change arrives with the health) and on a listen server (where
	 * this pawn is the one being damaged locally) — the same trick
	 * USarkoCharacterAnimComponent uses to infer a shot from the magazine count.
	 *
	 * Two material writes per hit, not one per frame: the tick compares a byte
	 * and returns.
	 */
	virtual void Tick(float DeltaSeconds) override;

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

	/**
	 * SERVER ONLY: world seconds this bot was last inside a player's vision cone
	 * with line of sight (vision spec §3). Negative until it has been.
	 *
	 * Owned by ASarkoRaidGameMode::UpdateEnemyVisibility and read by nothing
	 * else. It lives on the pawn rather than in a map on the game mode because it
	 * is one float per bot with exactly the bot's own lifetime — a map keyed on
	 * weak pointers would need pruning, and pruning a container on a tick path is
	 * the sort of bookkeeping that outlives the reason for it.
	 *
	 * NOT REPLICATED and never to be: "when was I last seen" is a fact about the
	 * player's screen, and a client that knew it would know it was hidden.
	 */
	float LastSeenByPlayerSeconds = -1000.f;

protected:
	void HandleDeath(AActor* Killer);

	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<USarkoHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<USarkoWeaponComponent> WeaponComponent;

	/** Drives the mesh's pose. Purely cosmetic; the same component the player uses. */
	UPROPERTY(VisibleAnywhere, Category = "Visuals")
	TObjectPtr<USarkoCharacterAnimComponent> AnimComponent;

private:
	/**
	 * The damage serial this machine has already reacted to. INDEX_NONE until the
	 * first tick, so a client that joins a raid already in progress records the
	 * count it finds rather than flashing once for a hit it never saw.
	 */
	int32 SeenDamageSerial = INDEX_NONE;

	/** When the current flash ends, or a negative time for "not flashing". */
	float FlashEndSeconds = -1.f;
};
