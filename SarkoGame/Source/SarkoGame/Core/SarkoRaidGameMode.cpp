#include "Core/SarkoRaidGameMode.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Map/SarkoMapBuilder.h"
#include "Pawn/SarkoCharacter.h"

ASarkoRaidGameMode::ASarkoRaidGameMode()
{
	GameStateClass = ASarkoRaidGameState::StaticClass();
	DefaultPawnClass = ASarkoCharacter::StaticClass();
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

	// Compute the layout here, not in StartPlay: UEngine::LoadMap spawns
	// every local player's pawn (which calls RestartPlayer) before it calls
	// UWorld::BeginPlay, which is what invokes StartPlay. Waiting for
	// StartPlay to populate CachedLayout means the very first RestartPlayer
	// of a standalone or PIE launch always finds it empty and falls back to
	// the world origin. InitGame runs ahead of player spawning in the same
	// LoadMap sequence, and SarkoMap::BuildLayout is a pure function of
	// (Seed, Settings) — no world or game state required — so it can safely
	// run this early. The game state may not exist yet at this point, so
	// this only computes the layout; StartPlay below hands it to the game
	// state to spawn once a world and game state both exist.
	CachedLayout = SarkoMap::BuildLayout(Seed, *GetDefault<USarkoRaidSettings>());
}

void ASarkoRaidGameMode::StartPlay()
{
	Super::StartPlay();

	// The map itself never crosses the network. Only Seed (four bytes)
	// replicates, through ASarkoRaidGameState; every machine — server
	// included — ends up with the identical geometry, because BuildLayout is
	// a pure function of (Seed, Settings): the layouts cannot disagree, and
	// the server never pays to replicate or simulate forty-plus static cover
	// actors.
	if (HasAuthority())
	{
		if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
		{
			RaidState->Seed = Seed;
			// The server already has the layout from InitGame, so it hands
			// that over directly instead of recomputing it. The server
			// never receives its own OnRep notify, so it must trigger the
			// spawn explicitly; clients build and spawn their own copy via
			// OnRep_Seed once the replicated value arrives, since they have
			// no game mode instance to hand them a precomputed layout.
			RaidState->SpawnPrebuiltLayout(CachedLayout);
			CachedLayout = RaidState->GetLayout();
		}
	}

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		RaidState->StartRaidClock(GetDefault<USarkoRaidSettings>()->RaidDurationSeconds);
	}
}

void ASarkoRaidGameMode::RestartPlayer(AController* NewPlayer)
{
	if (CachedLayout.PlayerStarts.Num() > 0)
	{
		const int32 Index = NextPlayerStartIndex % CachedLayout.PlayerStarts.Num();
		++NextPlayerStartIndex;

		const FTransform SpawnTransform(FRotator::ZeroRotator, CachedLayout.PlayerStarts[Index]);
		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
		return;
	}

	Super::RestartPlayer(NewPlayer);
}
