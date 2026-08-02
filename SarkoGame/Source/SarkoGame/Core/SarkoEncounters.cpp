#include "Core/SarkoEncounters.h"

#include "Math/RandomStream.h"

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

TArray<int32> SarkoEncounter::BuildActivationOrder(
	const TArray<int32>& AuthoredOrders,
	const TArray<bool>& Optional,
	bool bTutorial,
	int32 Seed)
{
	TArray<int32> Result;
	const int32 Count = FMath::Min(AuthoredOrders.Num(), Optional.Num());
	Result.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// The one thing `optional` actually does: a tutorial raid cannot reach a
		// row that is not part of its curriculum.
		if (bTutorial && Optional[Index])
		{
			continue;
		}
		Result.Add(Index);
	}

	if (bTutorial)
	{
		// Authored order, ties broken by file order — stable across machines
		// because the file is. Identical to what InitialiseEncounters did before
		// the rotation existed, which is the point: the tutorial did not change.
		Result.Sort([&AuthoredOrders](int32 A, int32 B)
		{
			return AuthoredOrders[A] != AuthoredOrders[B] ? AuthoredOrders[A] < AuthoredOrders[B] : A < B;
		});
		return Result;
	}

	// Sorted FIRST, then shuffled. Sorting a list that is about to be shuffled
	// looks pointless and is not: it makes the input to the shuffle a function of
	// the authored orders rather than of the array's construction, so the result
	// depends on the SEED and the FILE and on nothing else. Without it, moving a
	// row within the JSON would silently change every raid that ever ran.
	Result.Sort([&AuthoredOrders](int32 A, int32 B)
	{
		return AuthoredOrders[A] != AuthoredOrders[B] ? AuthoredOrders[A] < AuthoredOrders[B] : A < B;
	});

	// Fisher-Yates, back to front, over the same deterministic stream the loot
	// rolls use. No allocation per step and no std::shuffle: the engine's stream
	// is the one whose sequence is guaranteed identical on every platform, which
	// is what makes `?Seed=` a reproduction tool rather than a suggestion.
	FRandomStream Stream(Seed);
	for (int32 Index = Result.Num() - 1; Index > 0; --Index)
	{
		const int32 Swap = Stream.RandRange(0, Index);
		if (Swap != Index)
		{
			Result.Swap(Index, Swap);
		}
	}
	return Result;
}
