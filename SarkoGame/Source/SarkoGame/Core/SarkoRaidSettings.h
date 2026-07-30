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
};
