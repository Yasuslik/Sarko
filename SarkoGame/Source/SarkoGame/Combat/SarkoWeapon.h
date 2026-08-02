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

	/**
	 * How many rounds a weapon spawns with. Pure, because it is the arithmetic
	 * behind the tutorial's only lesson about reloading.
	 *
	 * Configured is USarkoRaidSettings::StartingMagazineRounds: negative means
	 * "a full magazine" (the answer for every non-teaching raid), and anything
	 * else is clamped into [0, MagazineSize] so a careless config cannot hand out
	 * a magazine deeper than the weapon has. A broken MagazineSize (zero or
	 * negative) yields zero rather than a negative count.
	 *
	 * Three of eight is what ships (spec §3): with auto-reload gone, a player who
	 * has never pressed the reload button dry-clicks at the first bot and dies —
	 * so the raid begins three trigger pulls away from an empty magazine, at the
	 * spawn camp, thirty-odd safe seconds before anything can hurt them.
	 */
	int32 StartingRounds(int32 Configured, int32 MagazineSize);

	/**
	 * How many rounds a reload moves out of the bag and into the magazine: the
	 * room left in the magazine, or everything the bag has, whichever is less.
	 *
	 * This one expression is the whole scarcity stage (spec §1). Reload used to
	 * assign MagazineSize unconditionally and nothing ever read `ammo_9mm` from the
	 * grid, so the 8-round magazine, the halved loot weights and the route's 46
	 * authored rounds were all decoration on an infinite supply.
	 *
	 * PARTIAL RELOADS ARE THE POINT, not a degenerate case: three rounds in the bag
	 * load three rounds, and the player walks into the next fight knowing exactly
	 * that. A zero result means the bag is empty — the caller takes the dry-click
	 * path rather than spending ReloadSeconds to move nothing.
	 *
	 * Pure, and every input is clamped rather than trusted: a broken MagazineSize
	 * (zero or negative) yields zero, an over-full magazine yields zero rather than
	 * a negative transfer that would ADD rounds to the bag, and a negative reserve
	 * — which no honest grid produces, but which arithmetic elsewhere could — is
	 * read as empty.
	 */
	int32 ReloadAmount(int32 InMagazine, int32 MagazineSize, int32 ReserveRounds);
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

	/**
	 * Server only. Starts the reload timer — unless the owner carries a grid with
	 * no `ammo_9mm` in it, in which case this is the reload half of the dry click:
	 * one log line and nothing else, no timer, no ReloadSeconds of a weapon that
	 * would come back just as empty (spec §1).
	 *
	 * An owner with NO backpack component at all is the bot case and is unchanged:
	 * see FinishReload.
	 */
	void StartReload();

	/**
	 * Server only. Empties the magazine and returns what was in it, so the caller
	 * can put those rounds somewhere — which is exactly once, at extraction, where
	 * ASarkoRaidGameMode folds them back into the grid before the haul is read
	 * (spec §1).
	 *
	 * Zeroing here rather than at the call site is what makes the credit
	 * exactly-once by construction: a second call finds an empty magazine and
	 * returns zero, so no ordering mistake can pay the player twice.
	 */
	int32 UnloadMagazine();

	/**
	 * Per-instance damage from the bot archetype table. Non-positive (the
	 * default) means "use USarkoRaidSettings::WeaponDamage", which is what the
	 * player's own weapon and every unarchetyped pawn get.
	 *
	 * Not replicated and not a UPROPERTY: damage is applied on the server inside
	 * ServerFire, so a client has no use for the number and no way to disagree
	 * about it.
	 */
	void SetDamageOverride(float NewDamage) { DamageOverride = NewDamage; }

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

	/** See SetDamageOverride. Non-positive means "use the project setting". */
	float DamageOverride = -1.f;

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

	/**
	 * Whether this empty magazine has already said so in the log.
	 *
	 * Server-side and cosmetic: a held aim stick sends a fire request every
	 * MinFireIntervalSeconds, so an unguarded dry-click line is about seven
	 * identical lines a second. Cleared by FinishReload and by ResetForTest, so
	 * every *new* empty magazine gets exactly one line.
	 */
	bool bDryClickReported = false;

	/**
	 * Whether this empty *bag* has already said so in the log.
	 *
	 * bDryClickReported's sibling, for the other dry click. The reload button is a
	 * discrete tap rather than a held stick, so a human cannot spam it — but
	 * ServerRequestReload is an RPC and a modified client can, and an unguarded
	 * line there is a log a hostile client can fill. Cleared by FinishReload and by
	 * ResetForTest, so the next empty bag after a successful reload speaks again.
	 */
	bool bEmptyReserveReported = false;
};
