#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FSarkoDiedSignature, AActor* /*Killer*/);

/** This slice has exactly two sides — no squads, no per-player teams. */
UENUM()
enum class ESarkoTeam : uint8
{
	Player,
	Enemy
};

namespace SarkoCombat
{
	/**
	 * Pure friend/foe distinction: a candidate is only ever a valid
	 * aim-assist or damage target for a shooter on the other team. Without
	 * this, aim-assist candidates are every living pawn but the shooter, so
	 * with eight enemies a shot at the player can be nudged onto a nearer
	 * enemy instead — a quiet, ongoing confound for an 8-minute playtest.
	 * The simplest distinction that works for exactly two sides.
	 */
	bool IsFoe(ESarkoTeam OwnerTeam, ESarkoTeam CandidateTeam);
}

/** Health and death. The server is the only thing that may change either. */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoHealthComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	bool IsDead() const { return bDead; }
	ESarkoTeam GetTeam() const { return Team; }
	void SetTeam(ESarkoTeam NewTeam) { Team = NewTeam; }

	/**
	 * Server only. Clamps at zero, ignores non-positive amounts, and fires
	 * OnDied exactly once no matter how much overkill arrives.
	 *
	 * Refuses damage outright once the raid has an outcome. That is the first of
	 * the three layers that make an extracted player untouchable: the outcome
	 * enum already refuses a second FinishRaid, but a bullet that lands after the
	 * extraction would still run the death *side-effects* — and clearing the
	 * backpack is one of them, which turns a real haul into an EXTRACTED summary
	 * with nothing in it.
	 */
	void ApplyDamage(float Amount, AActor* DamageInstigator);

	/** Test seam: puts the component in a known state without a world. */
	void ResetForTest(float NewMaxHealth);

	/**
	 * Test seam: stands in for the game-state lookup ApplyDamage makes, so the
	 * "a settled raid takes no more damage" gate is exercisable by a NewObject
	 * component, which has no world and therefore no game state to settle. Only
	 * tests ever set it; in a raid it stays false and the real lookup decides.
	 */
	void SetRaidFinishedForTest(bool bFinished) { bRaidFinishedForTest = bFinished; }

	FSarkoDiedSignature OnDied;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(Replicated)
	bool bDead = false;

	/**
	 * Not replicated: the server is the only side that ever collects
	 * aim-assist/damage candidates, so clients have no need for each other's
	 * team. Defaults to Player; each side's pawn class sets its own in its
	 * constructor (ASarkoCharacter leaves this default, ASarkoEnemyCharacter
	 * overrides it to Enemy).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	ESarkoTeam Team = ESarkoTeam::Player;

private:
	/**
	 * True once this machine's game state carries a real outcome. One lookup per
	 * damage event, on the server-only path, so a settled raid costs nothing in
	 * the common case and cannot be reached at all off the authority.
	 */
	bool IsRaidFinishedNow() const;

	/** Only SetRaidFinishedForTest writes this. See its comment. */
	bool bRaidFinishedForTest = false;
};
