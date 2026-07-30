#include "Core/SarkoRaidGameState.h"

#include "Core/SarkoRaidSettings.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Net/UnrealNetwork.h"

bool SarkoRaid::CanFinishRaid(ESarkoRaidOutcome Current, ESarkoRaidOutcome Requested)
{
	return Current == ESarkoRaidOutcome::InProgress && Requested != ESarkoRaidOutcome::InProgress;
}

bool SarkoRaid::OutcomeLosesHaul(ESarkoRaidOutcome Outcome)
{
	// Whitelist, not a blacklist: a fourth outcome added later loses the haul until
	// somebody decides otherwise, which is the safe default — the failure mode of
	// the other direction is loot credited for a raid nobody survived.
	return Outcome != ESarkoRaidOutcome::InProgress && Outcome != ESarkoRaidOutcome::Extracted;
}

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
	DOREPLIFETIME(ASarkoRaidGameState, LootedContainers);
	DOREPLIFETIME(ASarkoRaidGameState, Outcome);
	DOREPLIFETIME(ASarkoRaidGameState, bSessionReady);
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
	// setting — rather than receiving geometry by replication.
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(Settings->MapId.ToString(), Definition, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameState: %s"), *Error);
	}

	// ToLayout over the file, and nothing derived from Seed: the server reduces
	// the same definition the same way (SarkoRaidGameMode::InitGame), and both
	// sides must run the same pure function over the same file or the client
	// walks into cover the server does not have. Seed is replicated so loot
	// rolls agree and to signal that the raid has begun; it shapes no geometry.
	SpawnPrebuiltLayout(SarkoMap::ToLayout(Definition), Definition);
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
	// Containers are actors now, not marker boxes, and they spawn here so both
	// the server and each client build the same set from the same file.
	SarkoMap::SpawnLootContainers(*World, InDefinition);
	// Visual only, and local for the same reason: the dwell is measured by the
	// game mode against this same definition, on the server.
	SarkoMap::SpawnExtractionZones(*World, InDefinition);
	SizeLootState(InDefinition.Containers.Num());
	bLayoutBuilt = true;
}

void ASarkoRaidGameState::SizeLootState(int32 ContainerCount)
{
	if (!HasAuthority())
	{
		return;
	}
	if (LootedContainers.Num() != ContainerCount)
	{
		LootedContainers.SetNumZeroed(FMath::Max(0, ContainerCount));
	}
}

bool ASarkoRaidGameState::IsContainerLooted(int32 ContainerIndex) const
{
	// Out of range reads as "not looted" rather than asserting: on a client the
	// array can legitimately arrive a frame after the containers spawn.
	return LootedContainers.IsValidIndex(ContainerIndex) && LootedContainers[ContainerIndex] != 0;
}

void ASarkoRaidGameState::MarkContainerLooted(int32 ContainerIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!LootedContainers.IsValidIndex(ContainerIndex))
	{
		// A client-supplied index that got this far is either a stale map or a
		// forged RPC. Loud, and nothing happens.
		UE_LOG(LogTemp, Warning, TEXT("SarkoRaidGameState: container index %d is out of range (%d containers)"),
			ContainerIndex, LootedContainers.Num());
		return;
	}
	LootedContainers[ContainerIndex] = 1;

	// The server never receives its own OnRep, so it refreshes explicitly.
	OnRep_LootedContainers();
}

void ASarkoRaidGameState::OnRep_LootedContainers()
{
	for (int32 Index = RegisteredContainers.Num() - 1; Index >= 0; --Index)
	{
		if (ASarkoLootContainer* Container = RegisteredContainers[Index].Get())
		{
			Container->RefreshVisualState();
		}
		else
		{
			RegisteredContainers.RemoveAtSwap(Index);
		}
	}
}

void ASarkoRaidGameState::RegisterContainer(ASarkoLootContainer* Container)
{
	if (Container)
	{
		RegisteredContainers.AddUnique(Container);
	}
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

	// A finished raid's clock stops where it stopped. Not cosmetic: letting it
	// keep draining to zero behind the summary screen leaves IsRaidOver() true
	// for a raid that was extracted from, which is a trap for anything added
	// later that reads the clock rather than the outcome.
	if (IsRaidFinished())
	{
		return;
	}
	RemainingSeconds = FMath::Max(0.f, RemainingSeconds - DeltaSeconds);
}
