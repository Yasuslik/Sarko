#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoWeapon.generated.h"

namespace SarkoCombat
{
	/**
	 * Nudges a shot toward the nearest target inside a narrow cone, and returns
	 * the direction unchanged when nothing qualifies. Pure so the fairness rule
	 * is unit tested: it must never reach a target outside the cone, or the
	 * "aim assist" becomes an aimbot. Applied on the server for everyone
	 * equally, so it can never be a paid or platform advantage (spec §9, §12).
	 */
	FVector ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets);
}

/** Hitscan weapon. Only the server decides whether a shot connected. */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	bool CanFire() const { return AmmoInMagazine > 0 && !bReloading; }
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }
	bool IsReloading() const { return bReloading; }

	/** Server only: traces, applies aim assist and damage. */
	void ServerFire(FVector Origin, FVector Direction);

	void StartReload();

	/** Test seams — no world required. */
	void ResetForTest(int32 Rounds);
	void ConsumeRoundForTest() { AmmoInMagazine = FMath::Max(0, AmmoInMagazine - 1); }
	void SetReloadingForTest(bool bValue) { bReloading = bValue; }

protected:
	UPROPERTY(Replicated)
	int32 AmmoInMagazine = 0;

	UPROPERTY(Replicated)
	bool bReloading = false;

private:
	void FinishReload();

	FTimerHandle ReloadTimer;
};
