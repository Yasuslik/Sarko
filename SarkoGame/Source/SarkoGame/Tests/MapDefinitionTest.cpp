#include "Misc/AutomationTest.h"

#include "Debug/SarkoOverviewShot.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOverviewHeightFitsSector,
	"Sarko.Debug.OverviewHeightFitsSector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOverviewHeightFitsSector::RunTest(const FString& Parameters)
{
	// A 90-degree vertical FOV sees exactly as far across as it is high, so a
	// sector of half-extent E needs at least E of height, and more for a
	// narrower FOV. Getting this wrong means the overview crops the map, which
	// is worse than useless: it looks like the map ends there.
	const float Height90 = SarkoDebug::HeightToFitSector(/*ExtentUU*/ 20000.f, /*VerticalFOVDegrees*/ 90.f);
	TestTrue(TEXT("a 90 degree FOV needs at least the sector's half-extent in height"), Height90 >= 20000.f);

	const float Height60 = SarkoDebug::HeightToFitSector(20000.f, 60.f);
	TestTrue(TEXT("a narrower FOV must pull the camera further back"), Height60 > Height90);

	// Margin: the frame should not end exactly at the sector edge, or the
	// outermost cover touches the screen border and cannot be judged.
	TestTrue(TEXT("there is headroom beyond the sector edge"), Height90 > 20000.f * 1.05f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

#include "Map/SarkoMapDefinition.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	const FString MinimalMapJson = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"blocks": [ { "kind": "wall", "pos": [-4200, 1800, 0], "yaw": 90, "extent": [800, 120, 200] } ],
		"props": [ { "kind": "car_wreck", "pos": [1200, -300, 0], "yaw": 35 } ],
		"containers": [ { "pos": [1250, -280, 0], "tier": "military" } ],
		"playerSpawns": [ { "pos": [-16000, 17000, 100], "yaw": 135 } ],
		"botSpawns": [ { "pos": [8000, -12000, 100], "zone": "deep" } ],
		"extractions": [ { "pos": [-14000, 19000, 0], "radiusUU": 400, "name": "North path" } ]
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapDefinitionParses,
	"Sarko.Map.DefinitionParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapDefinitionParses::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("a well-formed map parses"), SarkoMap::ParseDefinition(MinimalMapJson, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());

	TestEqual(TEXT("id survives"), Definition.Id, FString(TEXT("test")));
	TestEqual(TEXT("extent survives"), Definition.ExtentUU, 20000.f);
	TestEqual(TEXT("raid duration survives"), Definition.RaidDurationSeconds, 900.f);
	TestEqual(TEXT("one block"), Definition.Blocks.Num(), 1);
	TestEqual(TEXT("one prop"), Definition.Props.Num(), 1);
	TestEqual(TEXT("one container"), Definition.Containers.Num(), 1);
	TestEqual(TEXT("one player spawn"), Definition.PlayerSpawns.Num(), 1);
	TestEqual(TEXT("one bot spawn"), Definition.BotSpawns.Num(), 1);
	TestEqual(TEXT("one extraction"), Definition.Extractions.Num(), 1);

	// The block's numbers must land in the right fields, not merely be present.
	TestTrue(TEXT("block position is read in order x,y,z"),
		Definition.Blocks[0].Location.Equals(FVector(-4200.f, 1800.f, 0.f), 0.01f));
	TestEqual(TEXT("block yaw is read"), Definition.Blocks[0].Rotation.Yaw, 90.0);
	TestTrue(TEXT("block extent is read"),
		Definition.Blocks[0].Extent.Equals(FVector(800.f, 120.f, 200.f), 0.01f));
	TestEqual(TEXT("extraction name is read"), Definition.Extractions[0].Name, FString(TEXT("North path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapDefinitionRejectsBadInput,
	"Sarko.Map.DefinitionRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapDefinitionRejectsBadInput::RunTest(const FString& Parameters)
{
	// A map file is hand-edited, so it will be broken sooner or later. Every
	// rejection must name the problem: a silent empty map is the worst outcome,
	// because the game launches and simply has nothing in it.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json at all"),        TEXT("{{{") },
		{ TEXT("missing id"),             TEXT(R"({"extentUU":20000,"raidDurationSeconds":900})") },
		{ TEXT("missing extent"),         TEXT(R"({"id":"x","raidDurationSeconds":900})") },
		{ TEXT("negative extent"),        TEXT(R"({"id":"x","extentUU":-5,"raidDurationSeconds":900})") },
		{ TEXT("no player spawn"),        TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[]})") },
		{ TEXT("position not a triple"),  TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[1,2],"yaw":0}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		const bool bParsed = SarkoMap::ParseDefinition(Case.Value, Definition, Error);
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key), bParsed);
		TestFalse(FString::Printf(TEXT("error message is not empty: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDefinitionConvertsToLayout,
	"Sarko.Map.DefinitionConvertsToLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDefinitionConvertsToLayout::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::ParseDefinition(MinimalMapJson, Definition, Error))
	{
		AddError(FString::Printf(TEXT("fixture failed to parse: %s"), *Error));
		return false;
	}

	// The whole point of the seam: the existing in-memory type is unchanged, so
	// everything downstream of it keeps working untouched.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TestEqual(TEXT("extent carries over"), Layout.Extent, Definition.ExtentUU);
	TestEqual(TEXT("cover carries over"), Layout.Cover.Num(), Definition.Blocks.Num());
	TestEqual(TEXT("player starts carry over"), Layout.PlayerStarts.Num(), Definition.PlayerSpawns.Num());
	TestEqual(TEXT("bot spawns carry over"), Layout.EnemySpawns.Num(), Definition.BotSpawns.Num());
	return true;
}

#endif // WITH_AUTOMATION_TESTS

#include "Map/SarkoMapKinds.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropKindsAreComplete,
	"Sarko.Map.PropKindsAreComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropKindsAreComplete::RunTest(const FString& Parameters)
{
	// Every kind the Bridge map uses must resolve, or that prop silently does
	// not appear and the map has a hole in it that no test would otherwise see.
	const TArray<FName> UsedKinds = {
		TEXT("wall"), TEXT("car_wreck"), TEXT("bus"), TEXT("house"),
		TEXT("fuel_pump"), TEXT("freight_car"), TEXT("water_tower"),
		TEXT("sandbag"), TEXT("crate"), TEXT("pipe"), TEXT("bridge_deck")
	};

	for (const FName& Kind : UsedKinds)
	{
		FSarkoPropKind Resolved;
		const bool bFound = SarkoMap::FindPropKind(Kind, Resolved);
		TestTrue(FString::Printf(TEXT("kind '%s' resolves"), *Kind.ToString()), bFound);
		if (bFound)
		{
			TestTrue(FString::Printf(TEXT("kind '%s' has a positive extent"), *Kind.ToString()),
				Resolved.Extent.GetMin() > 0.f);
			TestTrue(FString::Printf(TEXT("kind '%s' names a mesh"), *Kind.ToString()),
				Resolved.Mesh.IsValid() || !Resolved.Mesh.ToString().IsEmpty());
		}
	}

	FSarkoPropKind Unknown;
	TestFalse(TEXT("an unknown kind does not resolve"), SarkoMap::FindPropKind(TEXT("nonsense"), Unknown));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
