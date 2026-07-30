#include "Pawn/SarkoHealthComponent.h"

#include "Net/UnrealNetwork.h"

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

void USarkoHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator)
{
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
