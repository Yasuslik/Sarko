#include "Core/SarkoRaidGameMode.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Kismet/GameplayStatics.h"

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

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		RaidState->StartRaidClock(GetDefault<USarkoRaidSettings>()->RaidDurationSeconds);
	}
}
