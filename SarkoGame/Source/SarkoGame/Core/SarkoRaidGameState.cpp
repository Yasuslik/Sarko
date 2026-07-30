#include "Core/SarkoRaidGameState.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/UnrealNetwork.h"

ASarkoRaidGameState::ASarkoRaidGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASarkoRaidGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoRaidGameState, RemainingSeconds);
	// A replicated UPROPERTY that is never registered here silently never
	// replicates — nothing in a single-player test would catch that, since
	// the server is also the only client and always sees its own value.
	DOREPLIFETIME(ASarkoRaidGameState, Seed);
}

void ASarkoRaidGameState::OnRep_Seed()
{
	BuildAndSpawnLayout();
}

void ASarkoRaidGameState::BuildAndSpawnLayout()
{
	if (bLayoutBuilt)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	Layout = SarkoMap::BuildLayout(Seed, *GetDefault<USarkoRaidSettings>());
	SarkoMap::SpawnLayout(*World, Layout);
	bLayoutBuilt = true;
}

void ASarkoRaidGameState::StartRaidClock(float DurationSeconds)
{
	RemainingSeconds = DurationSeconds;
	bClockStarted = true;
}

void ASarkoRaidGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The clock only runs on the server; clients receive RemainingSeconds.
	if (!HasAuthority() || !bClockStarted)
	{
		return;
	}
	RemainingSeconds = FMath::Max(0.f, RemainingSeconds - DeltaSeconds);
}
