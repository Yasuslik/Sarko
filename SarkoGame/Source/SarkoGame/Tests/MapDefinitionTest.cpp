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
		// A wrong-typed array section must be distinguishable from an absent
		// one: "blocks":{} is not an array, and must not silently mean zero blocks.
		{ TEXT("blocks is wrong type, not an array"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"blocks":{}})") },
		// GetNumberField defaults a non-numeric yaw to 0.0 with only an internal
		// log; the caller never sees it, so this must become a named error.
		{ TEXT("yaw is a string"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":"abc"}]})") },
		// An empty/missing kind means no mesh is chosen downstream for a prop —
		// the same silent no-op that already cost a session once.
		{ TEXT("prop with no kind"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"props":[{"pos":[100,100,0],"yaw":0}]})") },
		// A non-number inside a pos triple must not be silently coerced to 0.0.
		{ TEXT("pos contains a non-number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[1200,"abc",0],"yaw":0}]})") },
		// A negative extent component describes geometry that cannot exist.
		{ TEXT("negative block extent"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"blocks":[{"pos":[0,0,0],"yaw":0,"extent":[-100,200,150]}]})") },
		// A zero-radius extraction zone can never trigger.
		{ TEXT("zero radiusUU"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"extractions":[{"pos":[0,0,0],"radiusUU":0}]})") },
		// fixedItems has its own bad-case table next to its positive case, in
		// Sarko.Loot.ContainersMayCarryFixedItems — including the empty-list and
		// fractional-qty rows, which belong beside their siblings rather than here.
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
	FSarkoMapDefinitionOptionalSectionsMayBeAbsent,
	"Sarko.Map.DefinitionOptionalSectionsMayBeAbsent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapDefinitionOptionalSectionsMayBeAbsent::RunTest(const FString& Parameters)
{
	// blocks, props, containers, botSpawns and extractions are all optional —
	// a map that never places any of them must still parse. It would be easy
	// to fix the five findings above by making every section mandatory, which
	// would break this schema; pin that it does not.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [-16000, 17000, 100], "yaw": 135 } ]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("a map with no optional sections still parses"), SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("no blocks"), Definition.Blocks.Num(), 0);
	TestEqual(TEXT("no props"), Definition.Props.Num(), 0);
	TestEqual(TEXT("no containers"), Definition.Containers.Num(), 0);
	TestEqual(TEXT("no bot spawns"), Definition.BotSpawns.Num(), 0);
	TestEqual(TEXT("no extractions"), Definition.Extractions.Num(), 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapIdsAreOptionalAndUnique,
	"Sarko.Map.IdsAreOptionalAndUnique",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapIdsAreOptionalAndUnique::RunTest(const FString& Parameters)
{
	// Ids are how a report, a bug and a test all name the same object (ТЗ §18).
	// Optional in the pure parser so every fixture in this suite stays valid;
	// unique always, because two objects answering to one name is worse than
	// neither having one — a fix applied to "bridge_house_d1" would silently
	// land on whichever of them the code happened to find first.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"blocks": [ { "id": "b1", "kind": "wall", "pos": [0, 0, 100], "extent": [100, 100, 100] } ],
		"props": [ { "id": "p1", "kind": "crate", "pos": [200, 0, 70] } ],
		"containers": [ { "id": "c1", "pos": [250, 0, 0], "tier": "junk" } ],
		"playerSpawns": [ { "id": "s1", "pos": [-16000, 17000, 100], "yaw": 135 } ],
		"botSpawns": [ { "id": "n1", "pos": [8000, -12000, 100], "zone": "deep" } ],
		"extractions": [ { "id": "e1", "pos": [-14000, 19000, 0], "radiusUU": 400, "name": "North" } ]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	const bool bParsed = SarkoMap::ParseDefinition(Json, Definition, Error);
	TestTrue(FString::Printf(TEXT("a fully identified map parses: %s"), *Error), bParsed);
	if (!bParsed)
	{
		return false;
	}
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("a block's id is read"), Definition.Blocks[0].Id, FString(TEXT("b1")));
	TestEqual(TEXT("a prop's id is read"), Definition.Props[0].Id, FString(TEXT("p1")));
	TestEqual(TEXT("a container's id is read"), Definition.Containers[0].Id, FString(TEXT("c1")));
	TestEqual(TEXT("a bot's id is read"), Definition.BotSpawns[0].Id, FString(TEXT("n1")));
	TestEqual(TEXT("an extraction's id is read"), Definition.Extractions[0].Id, FString(TEXT("e1")));
	// Player spawns are FTransforms and cannot carry a field, so their ids ride
	// a parallel array. The arrays must stay index-aligned or an id names the
	// wrong spawn, which is worse than no id at all.
	TestEqual(TEXT("player spawn ids are index-aligned with the spawns"),
		Definition.PlayerSpawnIds.Num(), Definition.PlayerSpawns.Num());
	TestEqual(TEXT("a player spawn's id is read"), Definition.PlayerSpawnIds[0], FString(TEXT("s1")));

	// Every id collected, in file order, from one call.
	TArray<FString> Ids;
	FString CollectError;
	TestTrue(TEXT("ids collect cleanly"), SarkoMap::CollectIds(Definition, Ids, CollectError));
	TestEqual(TEXT("six ids in the file, six collected"), Ids.Num(), 6);

	// The same fixture with no ids at all must still parse: this is the promise
	// that a hand-written map from before this task keeps working.
	FSarkoMapDefinition Anonymous;
	TestTrue(TEXT("a map with no ids anywhere still parses"),
		SarkoMap::ParseDefinition(MinimalMapJson, Anonymous, Error));
	TArray<FString> NoIds;
	TestTrue(TEXT("collecting from an anonymous map is not an error"),
		SarkoMap::CollectIds(Anonymous, NoIds, CollectError));
	TestEqual(TEXT("an anonymous map yields no ids"), NoIds.Num(), 0);
	// An anonymous map still gets one id per spawn slot, or RequireIdentifiedEntries
	// would walk a shorter array than the spawns it is supposed to be checking.
	TestEqual(TEXT("an anonymous map's spawn ids are still index-aligned"),
		Anonymous.PlayerSpawnIds.Num(), Anonymous.PlayerSpawns.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapRejectsBadIds,
	"Sarko.Map.RejectsBadIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapRejectsBadIds::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> BadCases = {
		// A duplicate across two *different* sections is the realistic mistake:
		// copy a container line, paste it as a prop, forget to rename.
		{ TEXT("duplicate id across sections"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]}],
				"containers":[{"id":"same","pos":[150,100,0],"tier":"junk"}]})") },
		{ TEXT("duplicate id inside one section"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]},{"id":"same","kind":"crate","pos":[300,100,70]}]})") },
		// Present-but-empty is not "absent": it is a field the author started
		// filling in and abandoned, and it would collide with the next one.
		{ TEXT("empty id string"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"","kind":"crate","pos":[100,100,70]}]})") },
		// Same discipline as every other optional field in this parser.
		{ TEXT("id is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":7,"kind":"crate","pos":[100,100,70]}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Case.Value, Definition, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShippedMapsMustIdentifyEveryPlaceable,
	"Sarko.Map.ShippedMapsMustIdentifyEveryPlaceable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShippedMapsMustIdentifyEveryPlaceable::RunTest(const FString& Parameters)
{
	// The stricter rule that only the on-disk path enforces. Blocks and props
	// are exempt because there are 246 of them and none is ever referred to
	// individually; containers, spawns and extractions each carry state or a
	// ledger row, so an anonymous one cannot be audited.
	const FString Anonymous = TEXT(R"({
		"id": "anon",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"containers": [ { "pos": [250, 0, 0], "tier": "junk" } ]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::ParseDefinition(Anonymous, Definition, Error))
	{
		AddError(FString::Printf(TEXT("fixture must still parse: %s"), *Error));
		return false;
	}
	FString RequireError;
	TestFalse(TEXT("an anonymous container fails the shipped-map rule"),
		SarkoMap::RequireIdentifiedEntries(Definition, RequireError));
	TestTrue(FString::Printf(TEXT("the failure names the section: %s"), *RequireError),
		RequireError.Contains(TEXT("containers")));

	// And the real map must pass it, through the real entry point. Both bools are
	// computed before the message is formatted: FString::Printf's arguments are
	// evaluated in an unspecified order, so an error read in the same expression
	// that produces it prints empty exactly when it matters.
	FSarkoMapDefinition Bridge;
	FString LoadError;
	const bool bBridgeLoaded = SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Bridge, LoadError);
	TestTrue(FString::Printf(TEXT("bridge.json loads: %s"), *LoadError), bBridgeLoaded);
	if (!bBridgeLoaded)
	{
		return false;
	}
	FString BridgeRequireError;
	const bool bBridgeIdentified = SarkoMap::RequireIdentifiedEntries(Bridge, BridgeRequireError);
	TestTrue(FString::Printf(TEXT("bridge.json identifies every placeable: %s"), *BridgeRequireError),
		bBridgeIdentified);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
