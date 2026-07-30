#include "Core/SarkoRaidGameMode.h"

#include "AI/SarkoEnemyCharacter.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Map/SarkoMapBuilder.h"
#include "Pawn/SarkoCharacter.h"
#include "UI/SarkoHUD.h"

ASarkoRaidGameMode::ASarkoRaidGameMode()
{
	GameStateClass = ASarkoRaidGameState::StaticClass();
	DefaultPawnClass = ASarkoCharacter::StaticClass();
	PlayerControllerClass = ASarkoPlayerController::StaticClass();
	HUDClass = ASarkoHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ASarkoRaidGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// ?Seed=12345 on the travel URL. Defaults to 1 so a bare PIE session rolls
	// the same way every launch rather than being accidentally random. It does
	// not affect the map — that comes from the file loaded below.
	const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("Seed"));
	if (!SeedOption.IsEmpty())
	{
		Seed = FCString::Atoi(*SeedOption);
	}

	// Load the layout here, not in StartPlay: UEngine::LoadMap spawns every
	// local player's pawn (which calls RestartPlayer) before it calls
	// UWorld::BeginPlay, which is what invokes StartPlay. Waiting for
	// StartPlay to populate CachedLayout means the very first RestartPlayer
	// of a standalone or PIE launch always finds it empty and falls back to
	// the world origin. InitGame runs ahead of player spawning in the same
	// LoadMap sequence, and loading a definition from disk needs no world or
	// game state, so it can safely run this early. The game state may not
	// exist yet at this point, so this only loads the definition; StartPlay
	// below hands it to the game state to spawn once a world and game state
	// both exist.
	FSarkoMapDefinition Definition;
	FString Error;
	if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId.ToString(), Definition, Error))
	{
		CachedDefinition = Definition;
		CachedLayout = SarkoMap::ToLayout(Definition);
	}
	else
	{
		// Loud, and empty. A hand-edited map file will break eventually, and a
		// silently empty raid is the confusing outcome: the game starts, the
		// player falls through nothing, and nothing says why.
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameMode: %s"), *Error);
	}
}

void ASarkoRaidGameMode::StartPlay()
{
	Super::StartPlay();

	// The map itself never crosses the network. Every machine — server
	// included — ends up with identical geometry because every machine reduces
	// the same shipped data file with the same pure ToLayout, so the layouts
	// cannot disagree, and the server never pays to replicate or simulate
	// hundreds of static scenery actors. Seed replicates through
	// ASarkoRaidGameState for loot rolls, not for geometry.
	if (HasAuthority())
	{
		if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
		{
			RaidState->Seed = Seed;
			// The server already has the layout and definition from InitGame,
			// so it hands them over directly instead of reloading. The
			// server never receives its own OnRep notify, so it must trigger
			// the spawn explicitly; clients build and spawn their own copy
			// via OnRep_Seed once the replicated value arrives, since they
			// have no game mode instance to hand them a precomputed layout.
			RaidState->SpawnPrebuiltLayout(CachedLayout, CachedDefinition);
			CachedLayout = RaidState->GetLayout();
		}

		// Enemies are real replicated actors (unlike the map's static geometry),
		// so only the server spawns them; replication hands them to clients.
		if (UWorld* World = GetWorld())
		{
			for (const FVector& Spawn : CachedLayout.EnemySpawns)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				World->SpawnActor<ASarkoEnemyCharacter>(ASarkoEnemyCharacter::StaticClass(), Spawn, FRotator::ZeroRotator, Params);
			}
		}
	}

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		// Prefer the map's own duration when it set one — per-map duration
		// (15 minutes here, 30 on real maps) — and fall back to the settings
		// default otherwise, e.g. when the map failed to load.
		const float DurationSeconds = CachedDefinition.RaidDurationSeconds > 0.f
			? CachedDefinition.RaidDurationSeconds
			: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;
		RaidState->StartRaidClock(DurationSeconds);
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
