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

	/**
	 * Pure guard against a client sending a non-unit or zero Direction: a
	 * plain FVector carries no guarantee of unit length, and the caller
	 * (ServerFire) traces WeaponRangeUU along whatever this returns, so an
	 * un-normalized (1000,0,0) would otherwise reach 200x the intended
	 * range. Returns the unit-length direction, or the exact zero vector for
	 * any input too small to have a meaningful direction — callers must
	 * treat a zero result as "bail, do not fire".
	 */
	FVector NormalizeFireDirection(FVector Direction);
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

	/**
	 * Server only: traces, applies aim assist and damage. Direction need not
	 * be unit length or non-degenerate — it is normalized internally, with a
	 * zero-length result treated as an invalid request and ignored.
	 */
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

	/**
	 * World time (GetWorld()->GetTimeSeconds()) of the last shot that
	 * actually fired, used to enforce a minimum interval between shots —
	 * the enemy already has EnemyFireIntervalSeconds as its own cooldown;
	 * nothing enforced an equivalent for the player, so a client could
	 * otherwise empty the magazine in a single frame by spamming fire
	 * requests. Starts low enough that the very first shot is never
	 * rate-limited.
	 */
	float LastFireTimeSeconds = -1000.f;
};
