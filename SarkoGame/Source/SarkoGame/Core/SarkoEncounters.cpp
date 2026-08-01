#include "Core/SarkoEncounters.h"

bool SarkoEncounter::UpdateBeyondRearm(bool bWasBeyond, float DistanceUU, float ArmAfterUU)
{
	// A latch, not a comparison: it opens when the player goes beyond the band
	// and stays open until something else closes it (arming, or a refusal).
	return bWasBeyond || DistanceUU > ArmAfterUU;
}

bool SarkoEncounter::ShouldArm(bool bFired, bool bOneShot, bool bBeyondRearm, float DistanceUU, float RadiusUU)
{
	if (bFired && bOneShot)
	{
		return false;
	}
	return bBeyondRearm && DistanceUU <= RadiusUU;
}

int32 SarkoEncounter::AllowedSpawnCount(
	int32 BudgetRemaining,
	int32 BudgetCost,
	int32 MaxAlive,
	int32 AliveNow,
	int32 AuthoredPoints,
	bool bFirstFight,
	int32 FirstFightMaxAlive)
{
	// The budget is the law. An event that does not fit does not happen.
	if (BudgetCost < 1 || BudgetCost > BudgetRemaining)
	{
		return 0;
	}

	int32 Allowed = FMath::Min3(BudgetCost, MaxAlive - AliveNow, AuthoredPoints);
	if (bFirstFight)
	{
		Allowed = FMath::Min(Allowed, FirstFightMaxAlive);
	}
	return FMath::Max(0, Allowed);
}

bool SarkoEncounter::SpawnPointQualifies(float DistanceToPlayerUU, bool bSpawnPointSeesPlayer, float MinDistanceUU)
{
	return DistanceToPlayerUU >= MinDistanceUU && !bSpawnPointSeesPlayer;
}

bool SarkoEncounter::ShouldAbandonDeferral(float DeferredSeconds, float MaxDeferSeconds)
{
	return DeferredSeconds >= MaxDeferSeconds;
}
