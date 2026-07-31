#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoTravel.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"
#include "Shelter/SarkoShelterView.h"

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

namespace
{
	/** A catalog built in the test rather than read from disk, so these tests pin
	 *  the *formatting* rules and do not fail when items.json gains an entry. */
	FSarkoItemCatalog MakeTestCatalog()
	{
		FSarkoItemCatalog Catalog;
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("scrap_metal")), TEXT("Металолом"), 10, ESarkoItemCategory::Junk });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("medkit")),      TEXT("Аптечка"),   3,  ESarkoItemCategory::Med });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("bike_frame")),  TEXT("Рама"),      1,  ESarkoItemCategory::VehiclePart });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("chain")),       TEXT("Ланцюг"),    1,  ESarkoItemCategory::VehiclePart });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("wheel_small")), TEXT("Колесо"),    2,  ESarkoItemCategory::VehiclePart });
		return Catalog;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStashLinesUseUkrainianNames,
	"Sarko.Shelter.StashLinesUseUkrainianNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStashLinesUseUkrainianNames::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = MakeTestCatalog();

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	Profile.Stash = {
		FSarkoItemStack{ FName(TEXT("scrap_metal")), 14 },
		FSarkoItemStack{ FName(TEXT("medkit")), 2 },
		// An id the catalog does not know. It must still be shown: a stash row the
		// player owns and cannot see is worse than an ugly line, and an id on
		// screen is the visible symptom of items.json drifting from the backend.
		FSarkoItemStack{ FName(TEXT("mystery_cog")), 1 },
	};

	const TArray<FString> Lines = SarkoShelter::BuildStashLines(Profile, Catalog);
	TestEqual(TEXT("one line per stash row, none dropped"), Lines.Num(), 3);
	TestEqual(TEXT("the UA display name is used, never the id"), Lines[0], FString(TEXT("Металолом  x14")));
	TestEqual(TEXT("server order is preserved"), Lines[1], FString(TEXT("Аптечка  x2")));
	TestEqual(TEXT("an unknown id falls back to the id itself"), Lines[2], FString(TEXT("mystery_cog  x1")));

	FSarkoProfile Empty;
	Empty.PlayerId = TEXT("p");
	Empty.VehicleTier = TEXT("none");
	const TArray<FString> EmptyLines = SarkoShelter::BuildStashLines(Empty, Catalog);
	TestEqual(TEXT("an empty stash says so rather than drawing nothing"), EmptyLines.Num(), 1);
	TestEqual(TEXT("and says it in Ukrainian"), EmptyLines[0], FString(TEXT("СХОВОК ПОРОЖНІЙ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoGarageLineCountsBicycleParts,
	"Sarko.Shelter.GarageLineCountsBicycleParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoGarageLineCountsBicycleParts::RunTest(const FString& Parameters)
{
	// Spec §6.5 asks for "bicycle 0/3 parts". Three is the number of recipe
	// ENTRIES, not the number of units (the recipe is 1 frame + 2 wheels + 1
	// chain = 4 units), and a part counts only when the stash holds the full
	// required quantity — two wheels, not one.
	TestEqual(TEXT("the mirrored recipe has three entries"), SarkoShelter::BicycleRecipe().Num(), 3);

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	TestEqual(TEXT("an empty stash is 0/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 0/3")));

	Profile.Stash = { FSarkoItemStack{ FName(TEXT("bike_frame")), 1 } };
	TestEqual(TEXT("one satisfied entry is 1/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 1/3")));

	// One wheel of the two required does not count: the craft call would be
	// rejected, and a shelter that says 2/3 while /v1/garage/craft says no is
	// lying to the player.
	Profile.Stash.Add(FSarkoItemStack{ FName(TEXT("wheel_small")), 1 });
	TestEqual(TEXT("a partially-held entry does not count"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 1/3")));

	Profile.Stash.Last().Quantity = 2;
	Profile.Stash.Add(FSarkoItemStack{ FName(TEXT("chain")), 1 });
	TestEqual(TEXT("all three entries held is 3/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 3/3")));

	// Past the bicycle, the line names the tier the player has rather than
	// counting parts they no longer need.
	Profile.VehicleTier = TEXT("bicycle");
	TestEqual(TEXT("an owned bicycle is reported as owned"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShelterViewSeparatesUnknownFromEmpty,
	"Sarko.Shelter.ViewSeparatesUnknownFromEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShelterViewSeparatesUnknownFromEmpty::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = MakeTestCatalog();
	const FSarkoLastRaid NoRaidYet;

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");

	// Not fetched yet: the stash is unknown, not empty. Drawing "СХОВОК
	// ПОРОЖНІЙ" here would tell a player their raid credited nothing.
	const FSarkoShelterView Loading = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ false,
		FString(), Catalog);
	TestEqual(TEXT("the title is the shelter's name"), Loading.Title, FString(TEXT("УКРИТТЯ")));
	TestEqual(TEXT("an unfetched profile draws no stash lines at all"), Loading.StashLines.Num(), 0);
	TestEqual(TEXT("and says it is connecting"), Loading.StatusLine, FString(TEXT("З'ЄДНАННЯ...")));
	TestFalse(TEXT("the raid button is disabled until the profile lands"), Loading.bRaidEnabled);

	// Fetched and genuinely empty.
	const FSarkoShelterView Loaded = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ true,
		FString(), Catalog);
	TestEqual(TEXT("a fetched empty stash says so"), Loaded.StashLines.Num(), 1);
	TestEqual(TEXT("no status line once the profile is in"), Loaded.StatusLine, FString());
	TestTrue(TEXT("the raid button is live once the profile is in"), Loaded.bRaidEnabled);
	TestEqual(TEXT("the garage line is present"), Loaded.GarageLine, FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 0/3")));

	// Failed: the error is shown verbatim and the raid is still allowed, because
	// spec §4.6's offline degradation says the game never hard-locks on network —
	// an offline raid plays and persists nothing.
	const FSarkoShelterView Failed = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ false,
		TEXT("/v1/profile: HTTP 401 unauthorized"), Catalog);
	TestEqual(TEXT("the error is shown, not swallowed"), Failed.StatusLine,
		FString(TEXT("ОФЛАЙН: /v1/profile: HTTP 401 unauthorized")));
	TestTrue(TEXT("an offline shelter can still start a raid"), Failed.bRaidEnabled);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
