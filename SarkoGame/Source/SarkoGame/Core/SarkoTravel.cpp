#include "Core/SarkoTravel.h"

#include "Core/SarkoRaidGameMode.h"
#include "Kismet/GameplayStatics.h"

FString SarkoTravel::ShelterMapName()
{
	return TEXT("/Engine/Maps/Entry");
}

FString SarkoTravel::RaidOptions(int32 SeedOverride)
{
	FString Options = FString::Printf(TEXT("game=%s"), *ASarkoRaidGameMode::StaticClass()->GetPathName());
	if (SeedOverride != 0)
	{
		// Appended with its own '?', because UGameplayStatics::OpenLevel prefixes
		// the whole options string with exactly one '?' and every further option
		// needs its own separator.
		Options += FString::Printf(TEXT("?Seed=%d"), SeedOverride);
	}
	return Options;
}

FString SarkoTravel::ShelterOptions()
{
	return FString();
}

void SarkoTravel::TravelTo(UObject* WorldContext, const FString& Options)
{
	UE_LOG(LogTemp, Display, TEXT("SarkoTravel: travelling to %s with options '%s'"),
		*ShelterMapName(), Options.IsEmpty() ? TEXT("(none — the shelter)") : *Options);

	// bAbsolute = true. See the header: relative travel inherits the previous
	// URL's options, and inheriting `game=` here is an infinite raid loop.
	UGameplayStatics::OpenLevel(WorldContext, FName(*ShelterMapName()), /*bAbsolute*/ true, Options);
}
