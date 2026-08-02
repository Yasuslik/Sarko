#include "Pawn/SarkoHealthComponent.h"

#include "Core/SarkoRaidGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Pawn/SarkoSurvival.h"
#include "UI/SarkoCombatFeedback.h"

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
	// "That pawn was hit" — every machine, because the white flash happens on the
	// machine that is looking at the body.
	DOREPLIFETIME(USarkoHealthComponent, DamageSerial);
	// "…and it came from over there" — the victim only. The camera is world-locked
	// with no cursor, so this is information the player cannot otherwise get, and
	// therefore information nobody else may be handed for free.
	DOREPLIFETIME_CONDITION(USarkoHealthComponent, LastDamageYawByte, COND_OwnerOnly);
}

float USarkoHealthComponent::GetLastDamageYawDegrees() const
{
	return SarkoFeedback::ByteToYaw(LastDamageYawByte);
}

void USarkoHealthComponent::InitialiseMaxHealth(float NewMaxHealth)
{
	const AActor* Owner = GetOwner();
	if ((Owner && !Owner->HasAuthority()) || NewMaxHealth <= 0.f)
	{
		return;
	}
	MaxHealth = NewMaxHealth;
	Health = NewMaxHealth;
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

	// THE DAMAGE DIRECTION (spec §4.1), riding along with the health above rather
	// than on a channel of its own. The server is the only side that knows the
	// instigator, and this is the only moment it knows it.
	//
	// From the VICTIM toward the SHOOTER: the HUD draws an arc pointing at where
	// the shot came from, which under a world-locked camera with no cursor is
	// information the player has no other way to obtain. A hit with no instigator
	// (a fall, a future hazard) leaves the previous direction alone and only bumps
	// the serial — a stale arrow is worse than no arrow, and the serial still
	// drives the flash and the flinch.
	if (DamageInstigator)
	{
		if (const AActor* Owner = GetOwner())
		{
			const FVector ToShooter = DamageInstigator->GetActorLocation() - Owner->GetActorLocation();
			if (!ToShooter.IsNearlyZero())
			{
				LastDamageYawByte = SarkoFeedback::YawToByte(ToShooter.Rotation().Yaw);
			}
		}
	}
	// Wraps at 256 by design: consumers compare it with the value they last saw,
	// so all that is ever asked of it is that it CHANGES. Two hundred and fifty-six
	// hits inside one frame is not a case.
	++DamageSerial;

	// Regeneration is gated on this (SarkoSurvival::RegenPerSecond): a pawn that
	// was shot a moment ago is in combat, whatever else is true. Set before the
	// survival check below, so even a killing blow marks the moment.
	if (USarkoSurvivalComponent* Survival = GetOwner()
			? GetOwner()->FindComponentByClass<USarkoSurvivalComponent>() : nullptr)
	{
		Survival->NoteCombat();
	}

	if (Health > 0.f)
	{
		return;
	}

	// Latch the flag before broadcasting so a handler that deals more damage
	// cannot re-enter and fire death twice.
	bDead = true;
	OnDied.Broadcast(DamageInstigator);
}

void USarkoHealthComponent::Heal(float Amount)
{
	// The mirror image of ApplyDamage's guards, in the same order and for the
	// same reasons. A settled raid heals nobody: the outcome is decided, and a
	// pawn that is about to be listed as KIA must not quietly come back.
	if (IsRaidFinishedNow())
	{
		return;
	}
	if (Amount <= 0.f || bDead)
	{
		return;
	}
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		return;
	}

	// Capped at the pool, never above it: both callers (out-of-combat
	// regeneration and vodka) run repeatedly and would otherwise walk health past
	// full, which the HUD's bar would draw as an overfull rectangle.
	Health = FMath::Min(MaxHealth, Health + Amount);
}
