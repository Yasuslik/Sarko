#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoTravel.h"
#include "Kismet/GameplayStatics.h"
#include "Net/SarkoBackendClient.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTravelUrlsSelectTheRightGameMode,
	"Sarko.Shelter.TravelUrlsSelectTheRightGameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTravelUrlsSelectTheRightGameMode::RunTest(const FString& Parameters)
{
	// The raid is reached by a `game=` option on the travel URL and nothing else,
	// so a renamed or moved ASarkoRaidGameMode would silently send the player
	// into the shelter again — an infinite menu with no error anywhere.
	// UGameInstance::CreateGameModeForURL does LoadClass<AGameModeBase> on this
	// exact string; comparing it against the class's own path is what makes a
	// rename a red test instead of a broken game.
	const FString Expected = FString::Printf(TEXT("game=%s"),
		*ASarkoRaidGameMode::StaticClass()->GetPathName());
	TestEqual(TEXT("the raid option names the real raid game mode class"),
		SarkoTravel::RaidOptions(/*SeedOverride*/ 0), Expected);

	// A seed override is appended, not substituted: the game mode reads ?Seed=
	// in InitGame and it is the only reproduction tool this project has.
	//
	// The seed on the URL is the *wrapped int32*, not the backend's raw uint32.
	// 3402905197 was observed live and does not fit an int32 at all; SeedToInt32
	// wraps it to -892062099, which is the value ASarkoRaidGameMode::Seed actually
	// holds, so that is what has to be printed for a raid to be reproducible.
	const int32 WrappedSeed = SarkoBackend::SeedToInt32(3402905197);
	const FString WithSeed = SarkoTravel::RaidOptions(WrappedSeed);
	TestTrue(TEXT("a seed override keeps the game mode option"), WithSeed.Contains(Expected));
	TestTrue(TEXT("a seed override is appended as a URL option"), WithSeed.Contains(TEXT("?Seed=-892062099")));

	// And it survives the round trip through the exact parse InitGame performs.
	// The leading '?' is the one UGameplayStatics::OpenLevel prefixes to the whole
	// options string — without it UGameplayStatics::GrabOption reads nothing at
	// all and every option silently defaults.
	TestEqual(TEXT("the seed on the URL parses back to the same int32"),
		SarkoBackend::SeedToInt32(FCString::Atoi64(
			*UGameplayStatics::ParseOption(FString(TEXT("?")) + WithSeed, TEXT("Seed")))),
		WrappedSeed);

	// The shelter is reached by the ABSENCE of a game= option, falling through to
	// GlobalDefaultGameMode. Anything non-empty here would be a second way to
	// pick a game mode and would eventually disagree with the ini.
	TestTrue(TEXT("the shelter carries no options at all"), SarkoTravel::ShelterOptions().IsEmpty());

	// The map is the engine's empty level, by full package path. A bare "Entry"
	// resolves against the project's content directory, which has no Entry, and
	// MakeSureMapNameIsValid only *warns* — the travel then silently does nothing.
	TestEqual(TEXT("both trips load the engine's empty level by full path"),
		SarkoTravel::ShelterMapName(), FString(TEXT("/Engine/Maps/Entry")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
