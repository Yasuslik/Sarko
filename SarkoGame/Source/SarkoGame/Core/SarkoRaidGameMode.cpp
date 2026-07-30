#include "Core/SarkoRaidGameMode.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Map/SarkoMapBuilder.h"

ASarkoRaidGameMode::ASarkoRaidGameMode()
{
	GameStateClass = ASarkoRaidGameState::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ASarkoRaidGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// ?Seed=12345 on the travel URL. Defaults to 1 so a bare PIE session is
	// still deterministic rather than accidentally random.
	const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("Seed"));
	if (!SeedOption.IsEmpty())
	{
		Seed = FCString::Atoi(*SeedOption);
	}
}

void ASarkoRaidGameMode::StartPlay()
{
	Super::StartPlay();

	// The map itself never crosses the network. Only Seed (four bytes)
	// replicates, through ASarkoRaidGameState; every machine — server
	// included — calls BuildLayout then SpawnLayout locally from that value.
	// BuildLayout is a pure function of (Seed, Settings), so the geometry it
	// produces is bit-for-bit identical everywhere it runs: the layouts
	// cannot disagree, and the server never pays to replicate or simulate
	// forty-plus static cover actors.
	if (HasAuthority())
	{
		if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
		{
			RaidState->Seed = Seed;
			// The server never receives its own OnRep notify, so it must
			// trigger the build explicitly; clients build via OnRep_Seed
			// once the replicated value arrives.
			RaidState->BuildAndSpawnLayout();
			CachedLayout = RaidState->GetLayout();
		}
	}

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		RaidState->StartRaidClock(GetDefault<USarkoRaidSettings>()->RaidDurationSeconds);
	}
}
