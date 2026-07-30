#include "Pawn/SarkoHealthComponent.h"

#include "Core/SarkoRaidGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

bool SarkoCombat::IsFoe(ESarkoTeam OwnerTeam, ESarkoTeam CandidateTeam)
{
	return OwnerTeam != CandidateTeam;
}

USarkoHealthComponent::USarkoHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USarkoHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USarkoHealthComponent, Health);
	DOREPLIFETIME(USarkoHealthComponent, MaxHealth);
	DOREPLIFETIME(USarkoHealthComponent, bDead);
}

void USarkoHealthComponent::ResetForTest(float NewMaxHealth)
{
	MaxHealth = NewMaxHealth;
	Health = NewMaxHealth;
	bDead = false;
}

bool USarkoHealthComponent::IsRaidFinishedNow() const
{
	// Same shape as the authority guard below: a component with no world (the
	// NewObject test seam) reads as "not finished" rather than crashing, which is
	// exactly why SetRaidFinishedForTest exists to say otherwise.
	if (bRaidFinishedForTest)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const ASarkoRaidGameState* RaidState = World ? World->GetGameState<ASarkoRaidGameState>() : nullptr;
	return RaidState && RaidState->IsRaidFinished();
}

void USarkoHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator)
{
	// Earliest, because everything below it is a side-effect a decided raid must
	// not have. Once the outcome is settled the result is settled with it: an
	// extracted player standing frozen on the pad is still a target the bots were
	// shooting at last frame, and a hit that lands here reaches HandleDeath, which
	// clears the backpack. FinishRaid(Died) is refused by the outcome enum, so the
	// summary would read EXTRACTED over an empty haul and the submitted result
	// would credit nothing. One game-state lookup, and only on this path — the
	// server is the only side that ever applies damage.
	if (IsRaidFinishedNow())
	{
		return;
	}

	// Non-positive damage is either a bug upstream or an attempt to heal through
	// the damage path. Either way it must do nothing.
	if (Amount <= 0.f || bDead)
	{
		return;
	}

	// Only the server may change health. Without this guard nothing structurally
	// stops a client from calling ApplyDamage directly; a NewObject test
	// component has no owner, so the null check keeps that seam working.
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		return;
	}

	Health = FMath::Max(0.f, Health - Amount);
	if (Health > 0.f)
	{
		return;
	}

	// Latch the flag before broadcasting so a handler that deals more damage
	// cannot re-enter and fire death twice.
	bDead = true;
	OnDied.Broadcast(DamageInstigator);
}
