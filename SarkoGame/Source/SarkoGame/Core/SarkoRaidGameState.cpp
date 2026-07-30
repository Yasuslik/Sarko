#include "Core/SarkoRaidGameState.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapDefinition.h"
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
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();

	// No GameMode instance exists on a client, so unlike the server (which
	// already loaded this in InitGame and hands it over directly through
	// SpawnPrebuiltLayout), this machine loads its own copy of the map
	// definition from disk — the same file, addressed by the same MapId
	// setting — mirroring how BuildLayout(Seed) below is independently
	// recomputed here rather than replicated.
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(Settings->MapId.ToString(), Definition, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameState: %s"), *Error);
	}

	SpawnPrebuiltLayout(SarkoMap::BuildLayout(Seed, *Settings), Definition);
}

void ASarkoRaidGameState::SpawnPrebuiltLayout(const FSarkoMapLayout& InLayout, const FSarkoMapDefinition& InDefinition)
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

	Layout = InLayout;
	SarkoMap::SpawnLayout(*World, Layout);
	// Props spawn wherever geometry does, so they appear on every machine —
	// server and client alike — exactly as the cover blocks already do.
	SarkoMap::SpawnProps(*World, InDefinition);
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
