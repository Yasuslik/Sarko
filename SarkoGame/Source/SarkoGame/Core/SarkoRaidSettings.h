#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SarkoRaidSettings.generated.h"

/**
 * Every tunable for the raid, in one place, editable from Config/DefaultGame.ini.
 * Nothing in gameplay code hardcodes a balance number: the whole point of the
 * slice is to change these quickly while looking for what feels good.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Sarko Raid"))
class USarkoRaidSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** How long a raid runs before the timer expires. */
	UPROPERTY(EditAnywhere, config, Category = "Raid")
	float RaidDurationSeconds = 480.f;

	/** Half-extent of the square play area, in unreal units (10000 uu = 100 m). */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	float MapExtent = 10000.f;

	/** How many cover blocks the generator scatters. */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	int32 CoverCount = 40;

	UPROPERTY(EditAnywhere, config, Category = "Movement")
	float WalkSpeed = 400.f;

	/**
	 * Stick deflection below this magnitude counts as centred. Without a
	 * dead zone, a resting thumb's tiny drift off true-centre would read as
	 * a non-zero move intent and creep the character.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Movement")
	float MoveStickDeadZone = 0.15f;

	/**
	 * Half-angle of the cone inside which a shot snaps to a target. This is a
	 * nudge that compensates for a thumb, not an aimbot — it is applied on the
	 * server, identically for everyone, so it never becomes an advantage.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float AimConeHalfAngleDegrees = 6.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponRangeUU = 4000.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponDamage = 22.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float ReloadSeconds = 2.2f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyHearingRadiusUU = 2500.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyFireIntervalSeconds = 0.9f;

	/** Chase/patrol speed. A separate knob from the player's WalkSpeed because
	 * how fast the enemy closes distance is its own balance question. */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyWalkSpeed = 340.f;

	/**
	 * There is no navmesh in this project (the map is spawned procedurally at
	 * runtime, so nothing is baked, and nothing configures runtime generation
	 * either) — the enemy steers directly at its target instead of pathing.
	 * This is how far ahead it casts a single forward trace to decide whether
	 * the straight line to the target is blocked and it needs to steer around
	 * something instead of walking into it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceTraceDistanceUU = 150.f;

	/**
	 * How sharply the enemy turns away from its target direction when the
	 * avoidance trace reports the way ahead is blocked, in degrees. Applied as
	 * a single fixed rotation of the desired direction rather than a search,
	 * which is enough to curve around one box of cover on a flat plane.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceSteerDegrees = 60.f;

	/**
	 * Per-second NAVDIAG-style AI logging, off by default because it is noise
	 * from every enemy every tick. Flip on only when actively debugging AI
	 * movement.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	bool bLogAIDiagnostics = false;
};
