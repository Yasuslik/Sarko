#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FSarkoDiedSignature, AActor* /*Killer*/);

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

	/**
	 * Server only. Clamps at zero, ignores non-positive amounts, and fires
	 * OnDied exactly once no matter how much overkill arrives.
	 */
	void ApplyDamage(float Amount, AActor* DamageInstigator);

	/** Test seam: puts the component in a known state without a world. */
	void ResetForTest(float NewMaxHealth);

	FSarkoDiedSignature OnDied;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(Replicated)
	bool bDead = false;
};
