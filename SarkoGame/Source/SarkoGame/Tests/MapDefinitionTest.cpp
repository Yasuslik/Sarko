#include "Misc/AutomationTest.h"

#include "Debug/SarkoOverviewShot.h"
#include "Map/SarkoMapBuilder.h"
#include "Map/SarkoMapDefinition.h"

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
		//
		// The three optional *string* fields, for the same reason 'id' has its
		// own row in Sarko.Map.RejectsBadIds: TryGetStringField is not a type
		// check. FJsonValueNumber and FJsonValueBoolean both override
		// TryGetString and stringify, so `"tier": 7` used to parse as the tier
		// FName "7" — a value that looks authored, cannot be found by grepping
		// the map file, and silently joins whatever loot table happens to be
		// keyed "7" (today: none, so the container rolls nothing).
		{ TEXT("tier is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"containers":[{"pos":[250,0,0],"tier":7}]})") },
		{ TEXT("zone is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"botSpawns":[{"pos":[250,0,0],"zone":3}]})") },
		{ TEXT("extraction name is a bool"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"extractions":[{"pos":[250,0,0],"name":true}]})") },
		// The same defect on the *number* side, and the dangerous half of it: the
		// "yaw is a string" row above passes even untightened, because "abc" does
		// not parse as a number. A quoted numeral does. TJsonValueString overrides
		// TryGetNumber, so `"yaw": "45"` used to succeed and yield 45 — a map file
		// that quietly holds strings where numbers belong, with nothing warning
		// and nothing to grep for. These two rows fail with a silent success
		// rather than a wrong value, which is why they are here and not implied
		// by the row above.
		{ TEXT("yaw is a quoted numeral"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":"45"}]})") },
		{ TEXT("radiusUU is a quoted numeral"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"extractions":[{"pos":[250,0,0],"radiusUU":"400"}]})") },
		// And the same defect on the two REQUIRED root numbers, which the optional
		// tightening above did not reach. Both values below are the map's real ones,
		// so the wrong behaviour is a completely silent success: the sector still
		// comes out 400 m across and the raid still lasts fifteen minutes, and the
		// only evidence is a pair of quotes nobody would think to grep for. A map
		// file that holds strings where numbers belong breaks the first time
		// anything reads it strictly, long after the quotes were introduced.
		{ TEXT("extentUU is a quoted numeral"),
			TEXT(R"({"id":"x","extentUU":"20000","raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}]})") },
		{ TEXT("raidDurationSeconds is a quoted numeral"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":"900","playerSpawns":[{"pos":[0,0,0],"yaw":0}]})") },
		// The last three reads that still went through TryGetStringField, i.e. that
		// still stringified a number. Ranked by consequence rather than by tidiness:
		//
		// A prop's 'kind' is the dangerous one. FName("7") is not a kind FindPropKind
		// knows, so SpawnProps logs and skips — the prop silently never spawns, from
		// a file that parses and a map that loads. Stage C authors hundreds of props;
		// one of them missing is not something anyone would notice.
		{ TEXT("prop kind is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"pos":[100,100,0],"kind":7}]})") },
		// The root 'id' becomes the map id "7": everything works and the map cannot
		// be found by searching the file for its own name.
		{ TEXT("root id is a number"),
			TEXT(R"({"id":7,"extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}]})") },
		// A fixed item's 'item' was caught downstream by the catalog lookup, but
		// reported as "'7' is not in Data/Items/items.json" — true, and it sends the
		// reader hunting for a missing item instead of a stray pair of quotes.
		{ TEXT("fixedItems item is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"containers":[{"pos":[250,0,0],"fixedItems":[{"item":7,"qty":1}]}]})") },
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

#include "Core/SarkoRaidSettings.h"
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
		if (!bFound)
		{
			continue;
		}
		// A kind with no parts resolves successfully and spawns nothing — the
		// worst possible outcome, because the map looks authored and is empty.
		TestTrue(FString::Printf(TEXT("kind '%s' has at least one part"), *Kind.ToString()),
			Resolved.Parts.Num() >= 1);

		// "Above the floor" is only a statement about a COMPOSITE kind, and this
		// is the trap the authoring convention exists to name. A part's Offset is
		// measured from the *prop's* origin, not from the floor. A single-box
		// kind is authored with pos.z = the kind's half-height in the map file, so
		// its one part is correctly at Offset.Z = 0 and its bottom is at the
		// floor only once the prop's own location is added — Offset.Z - Extent.Z
		// is -half-height for every one of the eleven legacy kinds, and asserting
		// otherwise would demand that all 238 shipped props be re-authored. A
		// composite is authored with pos.z = 0 instead, so there its part offsets
		// ARE heights above the floor and the check is meaningful.
		const bool bComposite = Resolved.Parts.Num() > 1;
		for (int32 Index = 0; Index < Resolved.Parts.Num(); ++Index)
		{
			const FSarkoPropPart& Part = Resolved.Parts[Index];
			TestTrue(FString::Printf(TEXT("kind '%s' part %d has a positive extent"), *Kind.ToString(), Index),
				Part.Extent.GetMin() > 0.f);
			TestTrue(FString::Printf(TEXT("kind '%s' part %d names a mesh"), *Kind.ToString(), Index),
				!Part.Mesh.ToString().IsEmpty());
			if (bComposite)
			{
				// A part whose bottom is underground is invisible, and if it
				// collides it is an invisible wall. Tolerance of 1 uu for a part
				// authored flush with the floor.
				TestTrue(FString::Printf(TEXT("kind '%s' part %d sits on or above the floor"), *Kind.ToString(), Index),
					Part.Offset.Z - Part.Extent.Z >= -1.f);
			}
			else
			{
				// The single-box counterpart of the same invariant, and the one
				// that actually keeps the shipped map still: no offset at all.
				TestTrue(FString::Printf(TEXT("single-box kind '%s' has no offset"), *Kind.ToString()),
					Part.Offset.IsNearlyZero());
			}
		}
	}

	// The eleven kinds that existed before parts did must be single-box and must
	// keep their exact extents, because bridge.json's 238 props are placed with
	// pos.z equal to the kind's own half-height. A changed half-height sinks or
	// floats every instance of that kind at once.
	//
	// ONE ROW HAS MOVED SINCE THIS WAS WRITTEN, and it is fuel_pump's X and Y.
	// Extent.Z is 110 exactly as it always was — that is the number the three
	// placed pumps' pos.z is pinned to, and it is the only one of the three that
	// can move a prop. The footprint went 60x40 -> 65x34 because the dispenser
	// stopped being a cube: the mesh is 1.17 x 0.62 x 1.98 m and those are its
	// own proportions at the pinned height, so the alternative to this edit was
	// a stretched pump. The right response to a mesh arriving is to re-derive X
	// and Y from Z and the mesh, which is what the kind table's own comment has
	// said since car_wreck; the guard that matters is Z, and Z is untouched.
	const TArray<TPair<FName, FVector>> LegacyExtents = {
		{ TEXT("wall"),        FVector(400.f, 60.f, 140.f) },
		{ TEXT("car_wreck"),   FVector(230.f, 95.f, 75.f) },
		{ TEXT("bus"),         FVector(600.f, 130.f, 160.f) },
		{ TEXT("house"),       FVector(500.f, 400.f, 300.f) },
		{ TEXT("fuel_pump"),   FVector(65.f, 34.f, 110.f) },
		{ TEXT("freight_car"), FVector(700.f, 150.f, 200.f) },
		{ TEXT("water_tower"), FVector(220.f, 220.f, 700.f) },
		{ TEXT("sandbag"),     FVector(180.f, 70.f, 55.f) },
		{ TEXT("crate"),       FVector(70.f, 70.f, 70.f) },
		{ TEXT("pipe"),        FVector(90.f, 90.f, 600.f) },
		{ TEXT("bridge_deck"), FVector(900.f, 300.f, 30.f) },
	};
	// Stated separately from the table above so it cannot be edited by accident:
	// the half-HEIGHT of every one of these is frozen, full stop. This is the
	// claim the 238 shipped pos.z values actually depend on.
	static const TMap<FName, float> FrozenHalfHeight = {
		{ TEXT("wall"), 140.f },        { TEXT("car_wreck"), 75.f },
		{ TEXT("bus"), 160.f },         { TEXT("house"), 300.f },
		{ TEXT("fuel_pump"), 110.f },   { TEXT("freight_car"), 200.f },
		{ TEXT("water_tower"), 700.f }, { TEXT("sandbag"), 55.f },
		{ TEXT("crate"), 70.f },        { TEXT("pipe"), 600.f },
		{ TEXT("bridge_deck"), 30.f },
	};
	for (const TPair<FName, FVector>& Expected : LegacyExtents)
	{
		FSarkoPropKind Resolved;
		if (!SarkoMap::FindPropKind(Expected.Key, Resolved) || Resolved.Parts.Num() != 1)
		{
			AddError(FString::Printf(TEXT("kind '%s' must still be a single box"), *Expected.Key.ToString()));
			continue;
		}
		TestTrue(FString::Printf(TEXT("kind '%s' keeps its extent"), *Expected.Key.ToString()),
			Resolved.Parts[0].Extent.Equals(Expected.Value, 0.01f));
		TestEqual(FString::Printf(TEXT("kind '%s' keeps the half-height its pos.z values assume"),
			*Expected.Key.ToString()),
			static_cast<float>(Resolved.Parts[0].Extent.Z), FrozenHalfHeight[Expected.Key]);
		TestTrue(FString::Printf(TEXT("kind '%s' keeps its zero offset"), *Expected.Key.ToString()),
			Resolved.Parts[0].Offset.IsNearlyZero());
		TestTrue(FString::Printf(TEXT("kind '%s' still blocks movement"), *Expected.Key.ToString()),
			Resolved.Parts[0].bBlocksMovement);
		// The mesh matters as much as the extent: swapping a cylinder for a cube
		// keeps every number in this table true and still changes the map. So it
		// is pinned BY PATH, per kind, rather than by "is it a primitive" — which
		// is what this line used to say and what stopped being the point the day
		// two of these eleven got real art.
		//
		// SIX OF THE ELEVEN NOW NAME A MESH, and four of those were added by the
		// procedural pass: the meshes were authored TO these extents rather than
		// found and fitted, so freight_car, sandbag and crate are exact to the
		// unit against their metres and not one of their 79 placed props moved.
		// What is left on a primitive is left there on purpose — `wall`, `house`
		// and `bus` are abstractions rather than objects (a "wall" prop is a
		// fence, a shelter end and a yard boundary depending on where it stands),
		// `bridge_deck` is road paint, and `pipe` is the one primitive that is
		// not a lie from overhead: a cylinder seen from above is a circle, which
		// is what a standing pipe is.
		static const TMap<FName, FString> ExpectedMesh = {
			{ TEXT("wall"),        TEXT("/Engine/BasicShapes/Cube.Cube") },
			{ TEXT("car_wreck"),   TEXT("/Game/ThirdParty/Cars/NormalCar1.NormalCar1") },
			{ TEXT("bus"),         TEXT("/Engine/BasicShapes/Cube.Cube") },
			{ TEXT("house"),       TEXT("/Engine/BasicShapes/Cube.Cube") },
			{ TEXT("fuel_pump"),   TEXT("/Game/Generated/Props/FuelPump.FuelPump") },
			{ TEXT("freight_car"), TEXT("/Game/Generated/Props/FreightWagon.FreightWagon") },
			{ TEXT("water_tower"), TEXT("/Game/ThirdParty/ZombieApocalypse/WaterTower.WaterTower") },
			{ TEXT("sandbag"),     TEXT("/Game/Generated/Props/SandbagStack.SandbagStack") },
			{ TEXT("crate"),       TEXT("/Game/Generated/Props/Crate.Crate") },
			{ TEXT("pipe"),        TEXT("/Engine/BasicShapes/Cylinder.Cylinder") },
			{ TEXT("bridge_deck"), TEXT("/Engine/BasicShapes/Cube.Cube") },
		};
		TestEqual(FString::Printf(TEXT("kind '%s' keeps its mesh"), *Expected.Key.ToString()),
			Resolved.Parts[0].Mesh.ToString(), ExpectedMesh[Expected.Key]);
		// Every legacy kind was painted Palette::Structure before surfaces existed,
		// so anything else here repaints the shipped map — and every departure is
		// NAMED here rather than allowed by a loosened rule, so that repainting
		// eight per cent of the sector stays a decision somebody wrote down.
		//
		//  * bridge_deck (ТЗ §5): the sector had a pale deck with rails that
		//    vanished into it, which is the inverse of what §5 asks for. Asphalt
		//    here, and the pale tone moved to the new bridge_rail kind.
		//  * freight_car: rolling stock is rust, not concrete. Twelve wagons in
		//    Structure grey read as twelve blocks parked in a field, and the
		//    siding is the one place in the sector with a named industrial
		//    identity to lose.
		//  * crate: it is made of wood. Forty-three of them stand against timber
		//    houses, pallets and log piles in exactly the same grey as the walls
		//    they lean on, which is the single largest block of miscoloured
		//    geometry left in the map.
		static const TMap<FName, ESarkoSurface> Repainted = {
			{ TEXT("bridge_deck"), ESarkoSurface::Asphalt },
			{ TEXT("freight_car"), ESarkoSurface::Rust },
			{ TEXT("crate"),       ESarkoSurface::Timber },
		};
		if (const ESarkoSurface* Deliberate = Repainted.Find(Expected.Key))
		{
			TestEqual(FString::Printf(TEXT("kind '%s' carries its named surface"), *Expected.Key.ToString()),
				static_cast<uint8>(Resolved.Parts[0].Surface), static_cast<uint8>(*Deliberate));
		}
		else
		{
			TestEqual(FString::Printf(TEXT("kind '%s' keeps the structure surface"), *Expected.Key.ToString()),
				static_cast<uint8>(Resolved.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Structure));
		}
	}

	FSarkoPropKind Unknown;
	TestFalse(TEXT("an unknown kind does not resolve"), SarkoMap::FindPropKind(TEXT("nonsense"), Unknown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropInstanceCountIsWithinTheMobileBudget,
	"Sarko.Map.PropInstanceCountIsWithinTheMobileBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropInstanceCountIsWithinTheMobileBudget::RunTest(const FString& Parameters)
{
	// How much geometry the props section amounts to. Renamed from
	// PropActorCount, and the rename is load-bearing: a prop part is an INSTANCE
	// now, not an actor, and the sector spawns exactly one actor for the lot.
	// Sarko.Map.BridgeStaysInsideTheActorBudget carries the whole argument for
	// why, and bounds the two numbers that decide performance (actors, and
	// instanced components). This one bounds the third: sheer amount of stuff.
	FSarkoMapDefinition Map;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const int32 PropParts = SarkoMap::CountPropParts(Map);
	TestTrue(TEXT("every prop resolves, so the count is not silently short"),
		PropParts >= Map.Props.Num());
	// 1200, against 1087 after the procedural prop pass — roughly 880 with the
	// forest in, 401 before it, and the difference is mostly the two stack kinds,
	// which buy a silhouette with three or four instances of a mesh the map was
	// already drawing. 113 of headroom left is the tightest this has been, and
	// the ceiling is deliberately NOT being raised with it: unlike the component
	// count, this number really is about triangles, memory and physics bodies,
	// and the next person to want a hundred more instances should have to argue
	// for them. The old ceiling here was 420 and it was a hard ACTOR limit;
	// this one is soft and about triangles, memory and physics bodies rather than
	// draw calls, which is why it is generous. An instance costs a transform and
	// a static body; it does not cost a UObject, a tick registration or a draw
	// call of its own.
	//
	// It is not unbounded, because "instances are free" is how a map ends up with
	// forty thousand of them and a two-second level load on a phone. If this
	// fails, the question to ask is whether the map needs the geometry — not
	// whether to raise the number again.
	TestTrue(FString::Printf(TEXT("props stay inside the mobile geometry budget (%d)"), PropParts),
		PropParts <= 1200);

	// Was an equality until Stage C: the shipped map used no composite kind, so
	// parts and authored entries were the same number. Bridge_West places pylons,
	// road signs and trailers, and every tree is a trunk plus a canopy, so the
	// relation is now strictly greater — asserted, because a *fall back* to
	// equality would mean a composite had silently collapsed to one box and the
	// pylon had lost its crossarms, or every tree its canopy.
	TestTrue(FString::Printf(TEXT("composite kinds are in use (%d parts from %d props)"),
		PropParts, Map.Props.Num()), PropParts > Map.Props.Num());

	// One authored prop of a single-box kind is exactly one actor: this is the
	// promise that adding parts cost the existing map nothing.
	FSarkoMapDefinition OneCrate;
	FString ParseError;
	const FString Json = TEXT(R"({
		"id": "one",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"props": [ { "kind": "crate", "pos": [100, 100, 70] } ]
	})");
	if (!SarkoMap::ParseDefinition(Json, OneCrate, ParseError))
	{
		AddError(FString::Printf(TEXT("fixture failed to parse: %s"), *ParseError));
		return false;
	}
	TestEqual(TEXT("a single-box prop is one part"), SarkoMap::CountPropParts(OneCrate), 1);

	// An unknown kind spawns nothing, so it must count as nothing — otherwise
	// the budget number would be optimistic in exactly the case where the map
	// is broken.
	FSarkoMapDefinition Nonsense;
	const FString BadJson = TEXT(R"({
		"id": "bad",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"props": [ { "kind": "nonsense", "pos": [0, 0, 0] } ]
	})");
	if (SarkoMap::ParseDefinition(BadJson, Nonsense, ParseError))
	{
		TestEqual(TEXT("an unresolvable kind contributes no parts"), SarkoMap::CountPropParts(Nonsense), 0);
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBlocksCarrySurfaceAndCollision,
	"Sarko.Map.BlocksCarrySurfaceAndCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBlocksCarrySurfaceAndCollision::RunTest(const FString& Parameters)
{
	// A road and a water strip are flat boxes that must not be walked into, and
	// a building wall is a coloured box that must be. One block type covers all
	// of it with two optional fields, which is why there is no separate
	// "surfaces" section and no second spawn path.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"blocks": [
			{ "id": "wall", "pos": [0, 0, 175], "extent": [400, 15, 175] },
			{ "id": "road", "pos": [0, 4000, 2], "extent": [550, 6000, 2], "surface": "asphalt", "blocksMovement": false },
			{ "id": "creek", "pos": [0, -4000, 5], "extent": [20000, 700, 3], "surface": "water", "blocksMovement": false }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	const bool bParsed = SarkoMap::ParseDefinition(Json, Definition, Error);
	TestTrue(FString::Printf(TEXT("surfaced blocks parse: %s"), *Error), bParsed);
	if (!bParsed)
	{
		return false;
	}
	TestEqual(TEXT("three blocks"), Definition.Blocks.Num(), 3);

	// Backward compatibility, stated as an assertion: a block written before
	// this task means exactly what it meant before.
	TestEqual(TEXT("an unsurfaced block defaults to Structure"),
		static_cast<uint8>(Definition.Blocks[0].Surface), static_cast<uint8>(ESarkoSurface::Structure));
	TestTrue(TEXT("an unsurfaced block still blocks movement"), Definition.Blocks[0].bBlocksMovement);

	TestEqual(TEXT("the road's surface is read"),
		static_cast<uint8>(Definition.Blocks[1].Surface), static_cast<uint8>(ESarkoSurface::Asphalt));
	TestFalse(TEXT("the road does not block movement"), Definition.Blocks[1].bBlocksMovement);
	TestEqual(TEXT("the water's surface is read"),
		static_cast<uint8>(Definition.Blocks[2].Surface), static_cast<uint8>(ESarkoSurface::Water));
	TestFalse(TEXT("the water does not block movement"), Definition.Blocks[2].bBlocksMovement);

	// The layout is what the spawner consumes, so the two new fields have to
	// survive the reduction or a road spawns as grey cover.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TestEqual(TEXT("all three blocks reach the layout"), Layout.Cover.Num(), 3);
	TestFalse(TEXT("the road is still non-colliding in the layout"), Layout.Cover[1].bBlocksMovement);
	TestEqual(TEXT("the water is still water in the layout"),
		static_cast<uint8>(Layout.Cover[2].Surface), static_cast<uint8>(ESarkoSurface::Water));

	// A non-colliding block is still a first-class entry everywhere that reads
	// blocks as data — only its physics body is gone. If a road stopped being
	// id-bearing, Stage C could not reference it in a report.
	TArray<FString> Ids;
	FString IdError;
	TestTrue(FString::Printf(TEXT("ids still collected across surfaces: %s"), *IdError),
		SarkoMap::CollectIds(Definition, Ids, IdError));
	TestTrue(TEXT("the non-colliding road keeps its id"), Ids.Contains(TEXT("road")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRejectsBadSurfaceFields,
	"Sarko.Map.RejectsBadSurfaceFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRejectsBadSurfaceFields::RunTest(const FString& Parameters)
{
	// An unknown or mistyped surface must not silently fall back to grey: a
	// typo in "asphalt" would produce a light grey highway across a dark map
	// and look like a lighting bug.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("unknown surface name"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"surface":"tarmac"}]})") },
		{ TEXT("surface is not a string"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"surface":3}]})") },
		{ TEXT("blocksMovement is not a bool"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"blocksMovement":"no"}]})") },
		// The nastier half of the same defect: FString::ToBool() answers "false"
		// to anything it does not recognise, so a misspelt literal would parse
		// as a wall with its collision quietly removed.
		{ TEXT("blocksMovement is a misspelt bool literal"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"blocksMovement":"ture"}]})") },
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

namespace
{
	/**
	 * How far above the floor a kind's tallest part reaches, in unreal units.
	 *
	 * The arithmetic differs by authoring convention and that is the whole trap
	 * (see FSarkoPropPart::Offset). A SINGLE-BOX kind is placed in the map file
	 * with pos.z equal to its own half-height, so its top is 2 * Extent.Z above the
	 * floor. A COMPOSITE is placed with pos.z = 0 and each part carries its own
	 * centre height, so its top is Offset.Z + Extent.Z.
	 *
	 * Using the composite formula for both — which reads naturally and is wrong —
	 * halves every single-box kind's height, and then "a fence blocks sight" is a
	 * claim about 92 uu instead of 184 and every threshold below means nothing.
	 * That is exactly how car_wreck came to be described as chest-high cover at
	 * 0.85x the pawn's own height.
	 *
	 * bOnlyColliding restricts the measurement to parts that actually stop the
	 * player, because "is this cover" is a question about collision, not paint.
	 */
	float TopOfKindUU(const FSarkoPropKind& Kind, bool bOnlyColliding)
	{
		const bool bComposite = Kind.Parts.Num() > 1;
		float Top = 0.f;
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			if (bOnlyColliding && !Part.bBlocksMovement)
			{
				continue;
			}
			const float PartTop = bComposite
				? static_cast<float>(Part.Offset.Z + Part.Extent.Z)
				: 2.f * static_cast<float>(Part.Extent.Z);
			Top = FMath::Max(Top, PartTop);
		}
		return Top;
	}

	/** How far above the floor a part's BOTTOM sits, by the same two conventions. */
	float BottomOfPartUU(const FSarkoPropKind& Kind, const FSarkoPropPart& Part)
	{
		return Kind.Parts.Num() > 1
			? static_cast<float>(Part.Offset.Z - Part.Extent.Z)
			: 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNewPropKindsExist,
	"Sarko.Map.NewPropKindsExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNewPropKindsExist::RunTest(const FString& Parameters)
{
	// Spec §5.3's vocabulary, by name. A missing kind here is a Stage C author
	// discovering mid-sector that the thing the ТЗ asked for does not exist.
	const TArray<FName> Required = {
		TEXT("rock"), TEXT("bush"), TEXT("log"), TEXT("fence_section"), TEXT("road_sign"),
		TEXT("concrete_barrier"), TEXT("trailer"), TEXT("pylon"), TEXT("treeline"),
		// The forest. Listed here for the same reason the nine above are: this
		// loop also asserts that bridge.json PLACES each one, and a tree kind
		// nothing plants is a kind whose extents have never been seen in a frame.
		TEXT("tree"), TEXT("tree_tall"), TEXT("tree_small"), TEXT("tree_dead")
	};

	for (const FName& Kind : Required)
	{
		FSarkoPropKind Resolved;
		const bool bFound = SarkoMap::FindPropKind(Kind, Resolved);
		TestTrue(FString::Printf(TEXT("kind '%s' resolves"), *Kind.ToString()), bFound);
		if (!bFound)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("kind '%s' has parts"), *Kind.ToString()), Resolved.Parts.Num() >= 1);
		for (int32 Index = 0; Index < Resolved.Parts.Num(); ++Index)
		{
			const FSarkoPropPart& Piece = Resolved.Parts[Index];
			TestTrue(FString::Printf(TEXT("'%s' part %d has a positive extent"), *Kind.ToString(), Index),
				Piece.Extent.GetMin() > 0.f);
			// Was "names an engine mesh", and it stopped being true the day the
			// forest got real art. The claim worth keeping is that a part names
			// a mesh THIS PROJECT SHIPS: an engine primitive, something under
			// /Game/ThirdParty, or one of the meshes we generate ourselves under
			// /Game/Generated/Props. A path to none of the three is a prop that
			// silently does not appear (SpawnProps logs and skips), which is
			// exactly the failure this line was added to catch.
			// Sarko.Config.PropMeshBoundsAreNormalised is the other half —
			// it loads the /Game ones and checks they are the shape the extents
			// below assume.
			const FString MeshPath = Piece.Mesh.ToString();
			TestTrue(FString::Printf(TEXT("'%s' part %d names a mesh this project ships (%s)"),
				*Kind.ToString(), Index, *MeshPath),
				MeshPath.StartsWith(TEXT("/Engine/BasicShapes/")) || MeshPath.StartsWith(TEXT("/Game/ThirdParty/"))
					|| MeshPath.StartsWith(TEXT("/Game/Generated/Props/")));
			// Gated deliberately. Offset is measured from the PROP's origin, and
			// it is the JSON `pos.z` that carries a single-box kind's half-height
			// (the authoring convention this task documents). Ungated, this line
			// fails for every single-box kind: 0 - 140 < -1.
			TestTrue(FString::Printf(TEXT("'%s' part %d is on or above the floor"), *Kind.ToString(), Index),
				Resolved.Parts.Num() == 1
					? Piece.Offset.IsNearlyZero()
					: Piece.Offset.Z - Piece.Extent.Z >= -1.f);
		}
	}

	// The two composites are the reason parts exist. If either collapses back to
	// one box, the abstraction bought nothing.
	FSarkoPropKind Pylon;
	if (SarkoMap::FindPropKind(TEXT("pylon"), Pylon))
	{
		TestTrue(TEXT("a pylon is composite"), Pylon.Parts.Num() >= 4);
	}
	FSarkoPropKind Sign;
	if (SarkoMap::FindPropKind(TEXT("road_sign"), Sign))
	{
		TestTrue(TEXT("a road sign is a post and a plate"), Sign.Parts.Num() >= 2);
	}

	// Stage C places all nine. The assertion inverts: the point of the kinds was
	// always that a sector would use them, and a kind nothing places is a kind
	// whose extents have never been seen in a frame (which is why Task 2 of the
	// Bridge_West plan photographs one of each before authoring the rest).
	FSarkoMapDefinition Bridge;
	FString LoadError;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Bridge, LoadError))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *LoadError));
		return false;
	}
	for (const FName& Kind : Required)
	{
		const bool bPlaced = Bridge.Props.ContainsByPredicate(
			[&Kind](const FSarkoMapProp& Prop) { return Prop.Kind == Kind; });
		TestTrue(FString::Printf(TEXT("bridge.json places '%s'"), *Kind.ToString()), bPlaced);
	}

	// Composites now cost more parts than they cost authored entries, which is
	// exactly what they were for. The relation that still has to hold is that
	// every prop resolves — a kind that did not would silently contribute zero
	// and make the budget number optimistic in the one case where the map is
	// broken. The absolute ceiling lives in
	// Sarko.Map.PropInstanceCountIsWithinTheMobileBudget.
	int32 ExpectedParts = 0;
	for (const FSarkoMapProp& Prop : Bridge.Props)
	{
		FSarkoPropKind Resolved;
		TestTrue(FString::Printf(TEXT("placed kind '%s' resolves"), *Prop.Kind.ToString()),
			SarkoMap::FindPropKind(Prop.Kind, Resolved));
		ExpectedParts += Resolved.Parts.Num();
	}
	TestEqual(TEXT("the part count is the sum of every placed kind's parts"),
		SarkoMap::CountPropParts(Bridge), ExpectedParts);
	TestTrue(TEXT("composites are actually in use, so the sum exceeds the entry count"),
		SarkoMap::CountPropParts(Bridge) > Bridge.Props.Num());

	// Every distinct (mesh, surface, collision, canopy) combination the sector
	// uses is one instanced component, and one draw call. This is the prediction
	// ASarkoPropField has to match at runtime, which is why it is computed from
	// the same four fields in the same order — a key that drifted from
	// FindOrCreateComponent's would bound a number nothing pays.
	const int32 Components = SarkoMap::CountInstancedComponents(Bridge);
	AddInfo(FString::Printf(TEXT("bridge.json needs %d instanced components for %d prop parts"),
		Components, SarkoMap::CountPropParts(Bridge)));
	TestTrue(TEXT("the sector's vocabulary fits in a handful of draw calls"),
		Components > 0 && Components < SarkoMap::CountPropParts(Bridge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropKindScaleMatchesThePawn,
	"Sarko.Map.PropKindScaleMatchesThePawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropKindScaleMatchesThePawn::RunTest(const FString& Parameters)
{
	// The pawn is ~176 uu tall. Whether a prop is shootable-over cover or a
	// sight blocker is a gameplay fact, so it is asserted rather than left to
	// whoever next edits a number in the table.
	constexpr float PawnHeightUU = 176.f;

	const auto TopOf = [this](FName Name, float& OutTop) -> bool
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Name, Kind) || Kind.Parts.Num() == 0)
		{
			AddError(FString::Printf(TEXT("kind '%s' does not resolve"), *Name.ToString()));
			return false;
		}
		OutTop = TopOfKindUU(Kind, /*bOnlyColliding*/ true);
		return true;
	};

	// Cover you shoot over: below the pawn's full height, above its knees.
	float ShortestCoverUU = TNumericLimits<float>::Max();
	for (const FName& Name : { FName(TEXT("car_wreck")), FName(TEXT("sandbag")),
		FName(TEXT("concrete_barrier")), FName(TEXT("log")), FName(TEXT("rock")),
		// The yard's two waist-high kinds. A drum is 114 uu and a run of pipe is
		// 100 — both inside the shoot-over band, which is the claim that stops
		// someone "improving" a barrel to a realistic 90 cm and quietly turning
		// the only cover on a forecourt into an ankle-high trip hazard.
		FName(TEXT("barrel")), FName(TEXT("pipe_run")) })
	{
		float Top = 0.f;
		if (TopOf(Name, Top))
		{
			TestTrue(FString::Printf(TEXT("'%s' can be shot over (%.0f uu)"), *Name.ToString(), Top),
				Top < PawnHeightUU);
			TestTrue(FString::Printf(TEXT("'%s' is tall enough to be cover (%.0f uu)"), *Name.ToString(), Top),
				Top > 60.f);
			ShortestCoverUU = FMath::Min(ShortestCoverUU, Top);
		}
	}

	// Sight blockers: taller than the pawn, so they cut line of sight outright.
	// The four trees are in this list rather than the shoot-over one deliberately:
	// what collides on a tree is its TRUNK, and a trunk you can shoot over is a
	// stump. TopOf measures colliding parts only, so this is a claim about the
	// trunks and says nothing about the canopies — which is exactly right, since
	// a canopy blocks neither sight nor anything else.
	for (const FName& Name : { FName(TEXT("house")), FName(TEXT("wall")),
		FName(TEXT("treeline")), FName(TEXT("fence_section")),
		FName(TEXT("tree")), FName(TEXT("tree_tall")), FName(TEXT("tree_small")),
		FName(TEXT("tree_dead")),
		// The yard's three tall ones. tank_wagon and crate_stack are here for the
		// obvious reason; spool is here because 2.2 m is a decision — a cable drum
		// could as easily have been modelled at 1.2 m, and at 1.2 m it would be a
		// thing you shoot over rather than the sight blocker the scrap yard's
		// approach was authored around.
		FName(TEXT("tank_wagon")), FName(TEXT("crate_stack")), FName(TEXT("spool")) })
	{
		float Top = 0.f;
		if (TopOf(Name, Top))
		{
			TestTrue(FString::Printf(TEXT("'%s' blocks sight (%.0f uu)"), *Name.ToString(), Top),
				Top >= PawnHeightUU);
		}
	}

	// A bush the player cannot walk through is a snag, not decoration.
	FSarkoPropKind Bush;
	if (SarkoMap::FindPropKind(TEXT("bush"), Bush))
	{
		for (const FSarkoPropPart& Piece : Bush.Parts)
		{
			TestFalse(TEXT("a bush never blocks movement"), Piece.bBlocksMovement);
		}
		TestEqual(TEXT("a bush is vegetation"),
			static_cast<uint8>(Bush.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
	}

	// The canopy contract, on every kind that has one.
	//
	// Three claims, and each one is a bug that would otherwise only be found by
	// playing: a canopy that collides is an invisible wall four metres up; a
	// canopy low enough to touch turns a cosmetic fade into a gameplay event; and
	// a canopy nothing can hide is a canopy that permanently hides the player,
	// which is the entire reason this sector had a treeline instead of a forest.
	constexpr float CanopyHeadroomUU = 1.5f * PawnHeightUU;
	int32 CanopiesSeen = 0;
	for (const FName& Name : { FName(TEXT("tree")), FName(TEXT("tree_tall")), FName(TEXT("tree_small")) })
	{
		FSarkoPropKind Tree;
		if (!SarkoMap::FindPropKind(Name, Tree))
		{
			continue;
		}
		bool bHasCanopy = false;
		bool bHasSolidTrunk = false;
		for (const FSarkoPropPart& Piece : Tree.Parts)
		{
			if (!Piece.bCanopy)
			{
				bHasSolidTrunk |= Piece.bBlocksMovement;
				continue;
			}
			bHasCanopy = true;
			++CanopiesSeen;
			TestFalse(FString::Printf(TEXT("'%s' canopy never blocks movement"), *Name.ToString()),
				Piece.bBlocksMovement);
			TestEqual(FString::Printf(TEXT("'%s' canopy is vegetation"), *Name.ToString()),
				static_cast<uint8>(Piece.Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
			TestTrue(FString::Printf(TEXT("'%s' canopy clears the pawn's head (%.0f uu vs %.0f uu)"),
				*Name.ToString(), BottomOfPartUU(Tree, Piece), CanopyHeadroomUU),
				BottomOfPartUU(Tree, Piece) >= CanopyHeadroomUU);
		}
		TestTrue(FString::Printf(TEXT("'%s' has a canopy"), *Name.ToString()), bHasCanopy);
		TestTrue(FString::Printf(TEXT("'%s' has a solid trunk to hide behind"), *Name.ToString()), bHasSolidTrunk);
	}
	TestTrue(TEXT("the canopy flag is actually set somewhere"), CanopiesSeen >= 3);

	// THE АЗС ROOF: the second fading part in the table and the first that is not
	// a tree. It gets its own block rather than joining the loop above because
	// two of the three claims are the same and the third is its opposite — a
	// canopy that is not Vegetation would be a bug on a tree and is the whole
	// point here — and because the roof reaches the headroom rule by a different
	// route: a tree's canopy hangs where the tree grew, and this one is held up
	// by pillars whose height is a number somebody can change.
	FSarkoPropKind GasCanopy;
	if (SarkoMap::FindPropKind(TEXT("gas_canopy"), GasCanopy))
	{
		bool bHasRoof = false;
		bool bHasPillar = false;
		for (const FSarkoPropPart& Piece : GasCanopy.Parts)
		{
			if (!Piece.bCanopy)
			{
				bHasPillar |= Piece.bBlocksMovement;
				continue;
			}
			bHasRoof = true;
			TestFalse(TEXT("the АЗС roof never blocks movement"), Piece.bBlocksMovement);
			TestNotEqual(TEXT("the АЗС roof is not foliage"),
				static_cast<uint8>(Piece.Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
			TestTrue(FString::Printf(TEXT("the АЗС roof clears the pawn's head (%.0f uu vs %.0f uu)"),
				BottomOfPartUU(GasCanopy, Piece), CanopyHeadroomUU),
				BottomOfPartUU(GasCanopy, Piece) >= CanopyHeadroomUU);

			// THE ONE NUMBER THAT MAKES THE ROOF LEGAL. The fade is measured from
			// the part's own centre, so every corner of the roof has to be inside
			// the fade radius or there are places a player can stand UNDER a roof
			// that is still being drawn — which is precisely the failure ТЗ §13
			// names and precisely why this file's own note said a canopy roof
			// could not exist. It is why the roof is 1400 uu square and not the
			// 1800 of the pad beneath it, and enlarging it silently breaks the
			// only argument that let it be built.
			const float HalfDiagonalUU = FMath::Sqrt(
				FMath::Square(static_cast<float>(Piece.Extent.X)) +
				FMath::Square(static_cast<float>(Piece.Extent.Y)));
			const float FadeRadiusUU = GetDefault<USarkoRaidSettings>()->CanopyFadeRadiusUU;
			TestTrue(FString::Printf(
				TEXT("the whole АЗС roof fits inside the fade radius (half-diagonal %.0f uu vs %.0f uu)"),
				HalfDiagonalUU, FadeRadiusUU), HalfDiagonalUU <= FadeRadiusUU);
		}
		TestTrue(TEXT("the АЗС canopy has a roof"), bHasRoof);
		TestTrue(TEXT("the АЗС canopy stands on something solid"), bHasPillar);
	}

	// The dead tree is the exception that keeps the fade honest: no canopy at all,
	// so it is the one tree that is still standing when a stand opens up overhead.
	FSarkoPropKind DeadTree;
	if (SarkoMap::FindPropKind(TEXT("tree_dead"), DeadTree))
	{
		for (const FSarkoPropPart& Piece : DeadTree.Parts)
		{
			TestFalse(TEXT("a dead tree has no canopy to fade"), Piece.bCanopy);
		}
	}

	// A treeline is the map's boundary: impassable, dark green, and taller than
	// anything the player can climb (there is no climbing).
	FSarkoPropKind Treeline;
	if (SarkoMap::FindPropKind(TEXT("treeline"), Treeline))
	{
		TestTrue(TEXT("a treeline blocks movement"), Treeline.Parts[0].bBlocksMovement);
		TestEqual(TEXT("a treeline is vegetation"),
			static_cast<uint8>(Treeline.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
		TestTrue(TEXT("a treeline is long enough to tile into a border"),
			Treeline.Parts[0].Extent.X >= 400.f);
	}

	// Nothing may be a skyscraper: the top-down camera frames the whole sector
	// from 20000+ uu, and a 50 m prop is a smear across the frame.
	const TArray<FName> AllKinds = {
		TEXT("wall"), TEXT("car_wreck"), TEXT("bus"), TEXT("house"), TEXT("fuel_pump"),
		TEXT("freight_car"), TEXT("water_tower"), TEXT("sandbag"), TEXT("crate"), TEXT("pipe"),
		TEXT("bridge_deck"), TEXT("bridge_rail"), TEXT("house_timber"), TEXT("house_industrial"),
		TEXT("rock"), TEXT("bush"), TEXT("log"), TEXT("fence_section"),
		TEXT("road_sign"), TEXT("concrete_barrier"), TEXT("trailer"), TEXT("pylon"), TEXT("treeline"),
		TEXT("tree"), TEXT("tree_tall"), TEXT("tree_small"), TEXT("tree_dead"),
		// The procedural pass's ten. Listed rather than enumerated from
		// AllPropKindNames on purpose — this list is the record of which kinds
		// somebody has actually thought about, and a kind that appears in the
		// table without appearing here is a kind nobody has judged.
		TEXT("gas_canopy"), TEXT("station_sign"), TEXT("tank_wagon"), TEXT("barrel"),
		TEXT("barrel_fallen"), TEXT("pallet"), TEXT("pallet_stack"), TEXT("pipe_run"),
		TEXT("spool"), TEXT("crate_stack")
	};
	for (const FName& Name : AllKinds)
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Name, Kind))
		{
			AddError(FString::Printf(TEXT("kind '%s' does not resolve"), *Name.ToString()));
			continue;
		}
		const float Top = TopOfKindUU(Kind, /*bOnlyColliding*/ false);
		TestTrue(FString::Printf(TEXT("'%s' is under 25 m tall (%.0f uu)"), *Name.ToString(), Top),
			Top <= 2500.f);
		for (const FSarkoPropPart& Piece : Kind.Parts)
		{
			TestTrue(FString::Printf(TEXT("'%s' is under 20 m wide"), *Name.ToString()),
				Piece.Extent.X <= 1000.f && Piece.Extent.Y <= 1000.f);

			// The bush defect, generalised so it cannot come back under another
			// name: a part that stands ON the floor and does NOT collide is a lie
			// the player can only discover by walking into it, so it must not be
			// tall enough to read as cover from above. Parts held overhead — a
			// sign's plate, a pylon's crossarms — are exempt, because nobody takes
			// cover behind something two and a half metres off the ground.
			if (Piece.bBlocksMovement || BottomOfPartUU(Kind, Piece) > 60.f || ShortestCoverUU > 1e5f)
			{
				continue;
			}
			const bool bComposite = Kind.Parts.Num() > 1;
			const float PartTop = bComposite
				? static_cast<float>(Piece.Offset.Z + Piece.Extent.Z)
				: 2.f * static_cast<float>(Piece.Extent.Z);
			TestTrue(FString::Printf(
				TEXT("'%s' is walk-through, so it must be shorter than real cover (%.0f uu vs %.0f uu)"),
				*Name.ToString(), PartTop, ShortestCoverUU), PartTop < ShortestCoverUU);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCompositePartsRotateWithTheProp,
	"Sarko.Map.CompositePartsRotateWithTheProp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCompositePartsRotateWithTheProp::RunTest(const FString& Parameters)
{
	// Until this task nothing in the table had a non-zero part offset, so the
	// rotation in SpawnProps was dead arithmetic that no test could reach: every
	// offset was zero, and zero is invariant under every yaw. The pylon's legs and
	// the trailer's tow bar are the first offsets a yaw can act on, and getting
	// the frame wrong here does not crash — it quietly slides half of every
	// composite off the other half, at every angle except zero.
	const FVector Origin(1000.f, -2000.f, 0.f);

	FSarkoPropKind Pylon;
	if (!SarkoMap::FindPropKind(TEXT("pylon"), Pylon) || Pylon.Parts.Num() < 2)
	{
		AddError(TEXT("the pylon must be composite for this test to mean anything"));
		return false;
	}
	// Authored: leg at local (-140, 0, 900) — the west leg of an unrotated pylon.
	const FSarkoPropPart& WestLeg = Pylon.Parts[0];
	TestTrue(TEXT("the first pylon part is the offset leg this test assumes"),
		WestLeg.Offset.Equals(FVector(-140.f, 0.f, 900.f), 0.01f));

	// Yaw 0 is the identity, which is the promise that keeps every prop authored
	// before parts existed at exactly its old transform.
	TestTrue(TEXT("at yaw 0 a part sits at its raw offset"),
		SarkoMap::PartWorldLocation(Origin, 0.f, WestLeg).Equals(Origin + WestLeg.Offset, 0.01f));

	// Yaw 90 turns local +X into world +Y, so a leg 140 uu local-west lands 140 uu
	// world-south. An inverse rotation — the easy mistake — puts it 140 uu north
	// instead, which looks plausible until a second part disagrees.
	TestTrue(TEXT("at yaw 90 the west leg swings to world -Y"),
		SarkoMap::PartWorldLocation(Origin, 90.f, WestLeg)
			.Equals(Origin + FVector(0.f, -140.f, 900.f), 0.01f));
	TestTrue(TEXT("at yaw 180 the west leg swings to world +X"),
		SarkoMap::PartWorldLocation(Origin, 180.f, WestLeg)
			.Equals(Origin + FVector(140.f, 0.f, 900.f), 0.01f));

	// Height is never touched by yaw: a rotation that leaked into Z would bury or
	// float half a composite.
	for (const float Yaw : { 0.f, 37.f, 90.f, 180.f, -45.f, 315.f })
	{
		for (const FSarkoPropPart& Part : Pylon.Parts)
		{
			const FVector World = SarkoMap::PartWorldLocation(Origin, Yaw, Part);
			TestEqual(FString::Printf(TEXT("yaw %.0f does not change a part's height"), Yaw),
				static_cast<float>(World.Z), static_cast<float>(Origin.Z + Part.Offset.Z), 0.01f);
			// Rigid body, not a free-for-all: every part keeps its distance from
			// the prop's origin, so the composite turns rather than deforming.
			TestEqual(FString::Printf(TEXT("yaw %.0f preserves a part's distance from the origin"), Yaw),
				static_cast<float>((World - Origin).Size()), static_cast<float>(Part.Offset.Size()), 0.01f);
		}
	}

	// The design claim spelled out: the plate stays over the post at any yaw,
	// because both are on the prop's own vertical axis.
	FSarkoPropKind Sign;
	if (SarkoMap::FindPropKind(TEXT("road_sign"), Sign) && Sign.Parts.Num() >= 2)
	{
		const FVector Post = SarkoMap::PartWorldLocation(Origin, 63.f, Sign.Parts[0]);
		const FVector Plate = SarkoMap::PartWorldLocation(Origin, 63.f, Sign.Parts[1]);
		TestEqual(TEXT("a road sign's plate stays directly over its post"),
			static_cast<float>(FVector2D(Plate - Post).Size()), 0.f, 0.01f);
		TestTrue(TEXT("and above it"), Plate.Z > Post.Z);
	}

	// An off-axis offset at an off-axis yaw, checked against arithmetic done by
	// hand: the trailer's tow bar is 440 uu local-west, so at yaw 45 it is
	// 440/sqrt(2) uu west and the same distance south.
	FSarkoPropKind Trailer;
	if (SarkoMap::FindPropKind(TEXT("trailer"), Trailer) && Trailer.Parts.Num() >= 2)
	{
		const FSarkoPropPart& TowBar = Trailer.Parts[1];
		const float Diagonal = 440.f / FMath::Sqrt(2.f);
		TestTrue(TEXT("the tow bar is the 440 uu offset this test assumes"),
			FMath::IsNearlyEqual(static_cast<float>(TowBar.Offset.X), -440.f, 0.01f));
		TestTrue(TEXT("at yaw 45 the tow bar lands on the diagonal"),
			SarkoMap::PartWorldLocation(Origin, 45.f, TowBar).Equals(
				Origin + FVector(-Diagonal, -Diagonal, static_cast<float>(TowBar.Offset.Z)), 0.05f));
	}

	// A single-box kind has a zero offset, so no yaw can move it at all — this is
	// the assertion that pins that the 238 shipped props are untouched by any of
	// the above.
	FSarkoPropKind Crate;
	if (SarkoMap::FindPropKind(TEXT("crate"), Crate) && Crate.Parts.Num() == 1)
	{
		for (const float Yaw : { 0.f, 35.f, 137.f, -90.f })
		{
			TestTrue(FString::Printf(TEXT("a single-box prop does not move at yaw %.0f"), Yaw),
				SarkoMap::PartWorldLocation(Origin, Yaw, Crate.Parts[0]).Equals(Origin, 0.0001f));
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS

#include "Map/SarkoBuildings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingsParseAndReachTheLayout,
	"Sarko.Map.BuildingsParseAndReachTheLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingsParseAndReachTheLayout::RunTest(const FString& Parameters)
{
	// One JSON object per building, and the spawner sees walls. If the walls do
	// not reach Layout.Cover then buildings are decoration: the player walks
	// through them and every test about doorways is describing nothing.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "id": "s1", "pos": [0, 0, 100], "yaw": 0 } ],
		"blocks": [ { "id": "rim", "pos": [0, 5000, 500], "extent": [2000, 300, 500] } ],
		"buildings": [
			{
				"id": "test_shop",
				"pos": [-13500, -9000, 0],
				"size": [2200, 1500],
				"surface": "timber",
				"doors": [
					{ "side": "E", "offset": 0, "width": 320 },
					{ "side": "S", "offset": 600, "width": 300 }
				],
				"interiorWalls": [
					{ "from": [-300, -720], "to": [-300, 720], "door": { "offset": 200, "width": 300 } }
				]
			},
			{ "id": "test_bunker", "pos": [4000, 4000, 0], "size": [1200, 1000], "yaw": 45 }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(FString::Printf(TEXT("buildings parse: %s"), *Error),
		SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("two buildings"), Definition.Buildings.Num(), 2);
	if (Definition.Buildings.Num() != 2)
	{
		AddError(FString::Printf(TEXT("fixture failed to parse: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("the id is read"), Definition.Buildings[0].Id, FString(TEXT("test_shop")));
	TestTrue(TEXT("the footprint is read"), Definition.Buildings[0].SizeUU.Equals(FVector2D(2200.f, 1500.f), 0.01f));
	TestEqual(TEXT("the surface is read"),
		static_cast<uint8>(Definition.Buildings[0].Surface), static_cast<uint8>(ESarkoSurface::Timber));
	TestEqual(TEXT("both doors are read"), Definition.Buildings[0].Doors.Num(), 2);
	TestEqual(TEXT("a door's side is read"),
		static_cast<uint8>(Definition.Buildings[0].Doors[0].Side), static_cast<uint8>(ESarkoBuildingSide::East));
	TestEqual(TEXT("a door's width is read"), Definition.Buildings[0].Doors[0].WidthUU, 320.f);
	TestEqual(TEXT("the interior wall is read"), Definition.Buildings[0].InteriorWalls.Num(), 1);
	TestTrue(TEXT("the interior wall's door is read"), Definition.Buildings[0].InteriorWalls[0].bHasDoor);
	TestEqual(TEXT("wallHeight defaults to 350"), Definition.Buildings[0].WallHeightUU, 350.f);
	TestEqual(TEXT("the second building's yaw is read"), Definition.Buildings[1].Yaw, 45.f);
	// A building with no doors is legal — the ТЗ's four closed entries.
	TestEqual(TEXT("a closed building has no doors"), Definition.Buildings[1].Doors.Num(), 0);

	// The layout is what SpawnLayout consumes. Authored blocks first, then every
	// building's walls, so an index into Layout.Cover means the same thing on
	// every machine.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TArray<FSarkoCoverBlock> ShopWalls;
	TArray<FSarkoCoverBlock> BunkerWalls;
	FString ExpandError;
	SarkoMap::ExpandBuilding(Definition.Buildings[0], ShopWalls, ExpandError);
	SarkoMap::ExpandBuilding(Definition.Buildings[1], BunkerWalls, ExpandError);
	TestEqual(TEXT("the layout holds the authored block plus every expanded wall"),
		Layout.Cover.Num(), 1 + ShopWalls.Num() + BunkerWalls.Num());
	TestEqual(TEXT("the authored block comes first"), Layout.Cover[0].Id, FString(TEXT("rim")));
	TestTrue(TEXT("the first expanded wall follows it"), Layout.Cover[1].Id.StartsWith(TEXT("test_shop")));

	// And the doorway is a doorway in the LAYOUT, not merely in the expander:
	// this is the end-to-end version of Task 5's invariant.
	const float WallX = -13500.f + 2200.f * 0.5f - 30.f * 0.5f;
	TestFalse(TEXT("the shop's east doorway is open in the spawned layout"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, -9000.f), Layout.Cover));
	TestTrue(TEXT("the shop's east wall is solid beside the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, -9000.f + 400.f), Layout.Cover));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingsFailLoudly,
	"Sarko.Map.BuildingsFailLoudly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingsFailLoudly::RunTest(const FString& Parameters)
{
	// Broken building geometry must be a LOAD error, not a spawn-time surprise:
	// ToLayout has no error channel, so anything that could fail has to fail in
	// the parser, where the message reaches a human.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("buildings is not an array"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"buildings":{}})") },
		{ TEXT("building with no id"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"pos":[0,0,0],"size":[2000,1500]}]})") },
		{ TEXT("building with no size"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0]}]})") },
		{ TEXT("size is not a pair"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500,300]}]})") },
		{ TEXT("a quoted numeral where a size belongs"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":["2000",1500]}]})") },
		{ TEXT("unknown door side"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],
				"doors":[{"side":"up","offset":0,"width":300},{"side":"S","offset":0,"width":300}]}]})") },
		// The expander's own rules must be reachable from the parser, or a
		// 200 uu doorway ships and nobody finds out until a pawn sticks.
		{ TEXT("doorway below the minimum"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],
				"doors":[{"side":"E","offset":0,"width":200},{"side":"W","offset":0,"width":300}]}]})") },
		{ TEXT("only one exit"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],"doors":[{"side":"E","offset":0,"width":300}]}]})") },
		// Ids are one namespace across the whole file, buildings included.
		{ TEXT("a building's id collides with a prop's"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]}],
				"buildings":[{"id":"same","pos":[0,0,0],"size":[2000,1500]}]})") },
		// ...and generated wall ids share that namespace too. A hand-authored
		// block called "b_north_0" and a building called "b" would put two
		// different boxes in Layout.Cover under one name, so "the wall at
		// b_north_0" would mean either of them depending on which loop found it
		// first. Ruled out here rather than left to be discovered in a report.
		{ TEXT("an authored block id collides with a generated wall id"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"id":"b_north_0","pos":[5000,5000,100],"extent":[100,100,100]}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500]}]})") },
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

namespace
{
	/**
	 * Everything an encounter file needs around the section under test. Written
	 * as a wrapper rather than repeated inline because every row of the bad-case
	 * table below differs from the good one in exactly one place, and that is
	 * the property the table is trying to demonstrate.
	 */
	FString EncounterFile(const TCHAR* BudgetAndEncounters)
	{
		return FString::Printf(
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,)")
			TEXT(R"("playerSpawns":[{"id":"p","pos":[0,0,0],"yaw":0}],%s})"),
			BudgetAndEncounters);
	}

	const TCHAR* const GoodBudget = TEXT(R"("encounterBudget":{"tutorial":4,"normal":8,"firstFightMaxAlive":1})");

	/** One well-formed encounter, with the fields the bad cases perturb. */
	FString GoodEncounter(const TCHAR* Overrides = TEXT(""))
	{
		return FString::Printf(
			TEXT(R"("encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
			TEXT(R"("trigger":{"kind":"radius","pos":[-13500,-9000],"radiusUU":2600,"armAfterUU":3400},)")
			TEXT(R"("spawns":[{"id":"s1","pos":[-14600,-10700,150],"archetype":"scav_pistol",)")
			TEXT(R"("postPos":[-14600,-10700],"leashUU":1400}]%s}])"), Overrides);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapParsesEncounters,
	"Sarko.Map.ParsesEncounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapParsesEncounters::RunTest(const FString& Parameters)
{
	// The positive case first, field by field, because every rejection below is
	// only meaningful against a shape that is known to be accepted.
	FSarkoMapDefinition Definition;
	FString Error;
	const FString Json = EncounterFile(*FString::Printf(TEXT("%s,%s"), GoodBudget, *GoodEncounter()));
	if (!TestTrue(FString::Printf(TEXT("a well-formed encounter file parses: %s"), *Error),
			SarkoMap::ParseDefinition(Json, Definition, Error)))
	{
		return false;
	}

	TestEqual(TEXT("the budget is read"), Definition.EncounterBudget.Tutorial, 4);
	TestEqual(TEXT("the normal ladder is read"), Definition.EncounterBudget.Normal, 8);
	TestEqual(TEXT("the first fight is capped at one"), Definition.EncounterBudget.FirstFightMaxAlive, 1);
	TestTrue(TEXT("the budget records that it was authored"), Definition.EncounterBudget.bAuthored);

	if (!TestEqual(TEXT("one encounter"), Definition.Encounters.Num(), 1))
	{
		return false;
	}
	const FSarkoEncounter& Encounter = Definition.Encounters[0];
	TestEqual(TEXT("its id"), Encounter.Id, FString(TEXT("e1")));
	TestEqual(TEXT("its order"), Encounter.Order, 10);
	TestEqual(TEXT("its cost"), Encounter.BudgetCost, 1);
	TestEqual(TEXT("its ceiling"), Encounter.MaxAlive, 1);
	TestTrue(TEXT("it is one-shot"), Encounter.bOneShot);
	TestEqual(TEXT("the trigger is a radius"), static_cast<int32>(Encounter.Trigger.Kind),
		static_cast<int32>(ESarkoTriggerKind::Radius));
	TestEqual(TEXT("the trigger radius"), Encounter.Trigger.RadiusUU, 2600.f);
	TestEqual(TEXT("the hysteresis band"), Encounter.Trigger.ArmAfterUU, 3400.f);
	if (!TestEqual(TEXT("one authored spawn point"), Encounter.Spawns.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the spawn's archetype"), Encounter.Spawns[0].Archetype, FName(TEXT("scav_pistol")));
	TestEqual(TEXT("the spawn's leash"), Encounter.Spawns[0].LeashUU, 1400.f);
	TestEqual(TEXT("the spawn's post"), Encounter.Spawns[0].PostPos, FVector2D(-14600.f, -10700.f));

	// The spawn point and the post are DIFFERENT fields, and the parser must
	// keep them apart: where a pawn is created has to satisfy "far away and out
	// of sight at this instant", while where it holds has to be the interesting
	// ground at the POI. A parser that read one into the other would pass every
	// assertion above if the map happened to author them equal.
	const FString SplitJson = EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
		TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
		TEXT(R"("spawns":[{"id":"s1","pos":[-11000,-17500,150],"archetype":"scav_pistol","postPos":[-16000,-16500],"leashUU":1400}]}])"), GoodBudget));
	FSarkoMapDefinition Split;
	if (TestTrue(TEXT("a spawn point away from its post parses"),
			SarkoMap::ParseDefinition(SplitJson, Split, Error)) && Split.Encounters.Num() == 1)
	{
		TestEqual(TEXT("the pawn is created where 'pos' says"),
			Split.Encounters[0].Spawns[0].Location, FVector(-11000.f, -17500.f, 150.f));
		TestEqual(TEXT("and holds where 'postPos' says"),
			Split.Encounters[0].Spawns[0].PostPos, FVector2D(-16000.f, -16500.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapRejectsBadEncounters,
	"Sarko.Map.RejectsBadEncounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapRejectsBadEncounters::RunTest(const FString& Parameters)
{
	// Same discipline as every other section's table: an encounter file is
	// hand-edited, so it will be broken eventually, and the worst outcome is the
	// silent one — a raid that quietly contains no enemies, or contains them
	// without a ceiling. Every row here is one field away from the good file in
	// Sarko.Map.ParsesEncounters.
	const TArray<TPair<FString, FString>> BadCases = {
		// Encounters with no ceiling is the one cross-section rule: the budget is
		// the primary object of the system, and a map without it is a map whose
		// enemy count is bounded by nothing at all.
		{ TEXT("encounters with no encounterBudget"),
			EncounterFile(*GoodEncounter()) },
		{ TEXT("encounterBudget is not an object"),
			EncounterFile(*FString::Printf(TEXT(R"("encounterBudget":4,%s)"), *GoodEncounter())) },
		{ TEXT("encounterBudget missing firstFightMaxAlive"),
			EncounterFile(*FString::Printf(TEXT(R"("encounterBudget":{"tutorial":4,"normal":8},%s)"), *GoodEncounter())) },
		{ TEXT("a zero tutorial budget, which is a map with encounters that can never fire"),
			EncounterFile(*FString::Printf(TEXT(R"("encounterBudget":{"tutorial":0,"normal":8,"firstFightMaxAlive":1},%s)"), *GoodEncounter())) },
		{ TEXT("a quoted numeral for the budget"),
			EncounterFile(*FString::Printf(TEXT(R"("encounterBudget":{"tutorial":"4","normal":8,"firstFightMaxAlive":1},%s)"), *GoodEncounter())) },
		// JSON has one number type, so a fraction would be truncated and the
		// author's mistake would become the raid's ceiling with nothing logged.
		{ TEXT("a fractional budget"),
			EncounterFile(*FString::Printf(TEXT(R"("encounterBudget":{"tutorial":4.5,"normal":8,"firstFightMaxAlive":1},%s)"), *GoodEncounter())) },

		{ TEXT("encounters is not an array"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":{})"), GoodBudget)) },
		{ TEXT("an anonymous encounter"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a missing order, which makes 'the first fight is the gas station' luck"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a fractional budgetCost"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1.5,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a zero budgetCost, which is a free encounter"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":0,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		// A default here would be a silent decision about whether a POI can hand
		// out enemies twice, which is the difference between a tutorial and a
		// grinder.
		{ TEXT("a missing oneShot"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a stringy oneShot, which FString::ToBool would read as false"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":"ture",)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a maxAlive higher than the number of authored doors"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":3,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },

		{ TEXT("a missing trigger"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		// An unlisted kind must not fall back to radius: a typo would become a
		// trigger of the wrong shape in the right place, which reads as a
		// gameplay bug and not as a data bug.
		{ TEXT("an unknown trigger kind"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"volume","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a trigger pos that is a triple, not a pair"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a zero trigger radius"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":0,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		// A hysteresis band of zero width is the pumping bug the field exists to
		// stop, and it looks exactly like a correct file.
		{ TEXT("armAfterUU equal to radiusUU — a hysteresis band of zero width"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":200,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("armAfterUU inside radiusUU"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":2600,"armAfterUU":1000},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },

		{ TEXT("no spawns at all — an encounter that can never fire"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},"spawns":[]}])"), GoodBudget)) },
		{ TEXT("an anonymous spawn point"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		// The archetype is checked against the table HERE, at load, for the same
		// reason a fixedItems id is checked against the catalog here: the
		// alternative is a bot that silently never appears, mid-raid, from a file
		// that parses.
		{ TEXT("an archetype the table does not know"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_bazooka","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a missing postPos, which is a bot with nowhere to hold"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","leashUU":1400}]}])"), GoodBudget)) },
		// A bot with no leash is the bug the leash exists to fix.
		{ TEXT("a zero leash"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":0}]}])"), GoodBudget)) },
		{ TEXT("a missing leash"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"s1","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0]}]}])"), GoodBudget)) },
		// Ids are one namespace across the whole file, encounters and their spawn
		// points included: a report, a test or a person reading "the thing called
		// X" does not know which section X was declared in.
		{ TEXT("an encounter id that collides with a spawn point id"),
			EncounterFile(*FString::Printf(TEXT(R"(%s,"encounters":[{"id":"same","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"same","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}])"), GoodBudget)) },
		{ TEXT("a spawn point id that collides with a container id"),
			FString::Printf(TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,)")
				TEXT(R"("playerSpawns":[{"id":"p","pos":[0,0,0],"yaw":0}],"containers":[{"id":"dup","pos":[250,0,0]}],%s,)")
				TEXT(R"("encounters":[{"id":"e1","order":10,"budgetCost":1,"maxAlive":1,"oneShot":true,)")
				TEXT(R"("trigger":{"kind":"radius","pos":[0,0],"radiusUU":100,"armAfterUU":200},)")
				TEXT(R"("spawns":[{"id":"dup","pos":[0,0,0],"archetype":"scav_pistol","postPos":[0,0],"leashUU":1400}]}]})"), GoodBudget) },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Case.Value, Definition, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}

	// A map with no encounters at all is NOT an error: the pre-encounter
	// `botSpawns` shape still works and non-tutorial content uses it. Asserted
	// here so the table above cannot drift into rejecting every old map file.
	FSarkoMapDefinition Legacy;
	FString LegacyError;
	TestTrue(TEXT("a map with neither encounters nor a budget still parses"),
		SarkoMap::ParseDefinition(EncounterFile(TEXT(R"("botSpawns":[{"id":"b","pos":[250,0,0],"zone":"deep"}])")),
			Legacy, LegacyError));
	TestEqual(TEXT("and carries no encounters"), Legacy.Encounters.Num(), 0);
	TestFalse(TEXT("and knows its budget was never authored"), Legacy.EncounterBudget.bAuthored);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOnlySceneryLeavesTheSector,
	"Sarko.Map.OnlySceneryLeavesTheSector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The edge skirt's rule, and the half of it that matters is the refusal.
 *
 * The skirt (spec §3.6) is ground and trees ~4000 uu OUTSIDE the sector, so that
 * the border fades into wilderness instead of cutting to black. Allowing it took
 * a choice: widen the bound for everyone, or name the exception. Widening it
 * would have let a container, an encounter door, a bot or an exit out there too —
 * silently, because before this the bound was one assertion inside one test about
 * one map file — and a piece of gameplay in the void is content that cannot be
 * reached, played or debugged.
 *
 * So the exception is a field a row has to write, only `blocks` and `props` carry
 * it, and a PLAYABLE entry outside the sector is a parse error that says which
 * entry and by how much. The protection is stronger than the thing it replaced.
 */
bool FSarkoOnlySceneryLeavesTheSector::RunTest(const FString& Parameters)
{
	// Scenery, flagged, just outside: this is the skirt, and it must load.
	{
		const FString Json = TEXT(R"({
			"id": "test", "extentUU": 20000, "raidDurationSeconds": 600,
			"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
			"blocks": [
				{ "id": "skirt_band", "pos": [0, 22000, 2], "extent": [24000, 800, 2],
				  "surface": "skirt_mid", "blocksMovement": false, "skirt": true }
			],
			"props": [ { "kind": "treeline", "pos": [0, 22400, 500], "yaw": 0, "skirt": true } ]
		})");
		FSarkoMapDefinition Definition;
		FString Error;
		const bool bParsed = SarkoMap::ParseDefinition(Json, Definition, Error);
		TestTrue(FString::Printf(TEXT("a flagged skirt block and prop load: %s"), *Error), bParsed);
		if (bParsed)
		{
			TestTrue(TEXT("the block remembers it is skirt"), Definition.Blocks[0].bSkirt);
			TestTrue(TEXT("and so does the prop"), Definition.Props[0].bSkirt);
			TestEqual(TEXT("the skirt tones parse by name"),
				static_cast<uint8>(Definition.Blocks[0].Surface), static_cast<uint8>(ESarkoSurface::SkirtMid));
			// It reaches the spawner, or the skirt is a field nobody draws.
			const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
			TestTrue(TEXT("and reaches the layout still flagged"), Layout.Cover[0].bSkirt);
		}
	}

	// The default is unchanged: a block written before the skirt existed is not
	// skirt, and stays bounded.
	{
		const FString Json = TEXT(R"({
			"id": "test", "extentUU": 20000, "raidDurationSeconds": 600,
			"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
			"blocks": [ { "id": "wall", "pos": [0, 0, 175], "extent": [400, 15, 175] } ]
		})");
		FSarkoMapDefinition Definition;
		FString Error;
		TestTrue(TEXT("an old block still parses"), SarkoMap::ParseDefinition(Json, Definition, Error));
		TestFalse(TEXT("and is not skirt"), Definition.Blocks[0].bSkirt);
	}

	// EVERY WAY OUT OF THE SECTOR THAT IS NOT SCENERY. One row per section, each
	// 1000 uu past the border, each expected to be a named refusal — this is the
	// list of things that could otherwise be authored into the void.
	const TArray<TPair<FString, FString>> MustFail = {
		{ TEXT("an unflagged block"),
			TEXT(R"("blocks":[{"id":"b","pos":[0,21000,2],"extent":[100,100,2]}])") },
		{ TEXT("an unflagged prop"),
			TEXT(R"("props":[{"id":"p","kind":"crate","pos":[0,21000,70]}])") },
		{ TEXT("a container"),
			TEXT(R"("containers":[{"id":"c","pos":[-21000,0,35],"tier":"good"}])") },
		{ TEXT("a bot spawn"),
			TEXT(R"("botSpawns":[{"id":"b","pos":[0,-21000,0],"zone":"deep"}])") },
		{ TEXT("an extraction"),
			TEXT(R"("extractions":[{"id":"e","pos":[21000,0,0],"radiusUU":500,"name":"nowhere"}])") },
	};
	for (const TPair<FString, FString>& Case : MustFail)
	{
		const FString Json = FString::Printf(TEXT(R"({"id":"test","extentUU":20000,"raidDurationSeconds":600,)")
			TEXT(R"("playerSpawns":[{"pos":[0,0,100],"yaw":0}],%s})"), *Case.Value);
		FSarkoMapDefinition Definition;
		FString Error;
		const bool bParsed = SarkoMap::ParseDefinition(Json, Definition, Error);
		TestFalse(FString::Printf(TEXT("%s outside the sector is refused"), *Case.Key), bParsed);
		TestFalse(FString::Printf(TEXT("%s: the refusal says something"), *Case.Key), Error.IsEmpty());
		// Named, not "a map failed to load": the whole point of moving this into
		// the parser was that the message reaches the person editing the file.
		TestTrue(FString::Printf(TEXT("%s: the refusal names the coordinate — '%s'"), *Case.Key, *Error),
			Error.Contains(TEXT("21000")) || Error.Contains(TEXT("-21000")));
	}

	// A player spawn is gameplay too, and it is the one section with no id.
	{
		const FString Json = TEXT(R"({"id":"test","extentUU":20000,"raidDurationSeconds":600,)")
			TEXT(R"("playerSpawns":[{"pos":[0,25000,100],"yaw":0}]})");
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(TEXT("a player spawn outside the sector is refused"),
			SarkoMap::ParseDefinition(Json, Definition, Error));
	}

	// And the skirt flag is a licence to stand just outside, not a licence to
	// author a second map: SkirtMarginUU is the ceiling and it is enforced.
	{
		const FString Json = FString::Printf(
			TEXT(R"({"id":"test","extentUU":20000,"raidDurationSeconds":600,)")
			TEXT(R"("playerSpawns":[{"pos":[0,0,100],"yaw":0}],)")
			TEXT(R"("blocks":[{"id":"far","pos":[0,%.0f,2],"extent":[100,100,2],"skirt":true}]})"),
			20000.f + SarkoMap::SkirtMarginUU + 1000.f);
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(TEXT("a skirt entry far past the margin is still refused"),
			SarkoMap::ParseDefinition(Json, Definition, Error));
		TestTrue(FString::Printf(TEXT("and the refusal says it is the skirt limit — '%s'"), *Error),
			Error.Contains(TEXT("skirt")));
	}

	// The shipped map is the real subject: it carries a skirt, and it loads.
	{
		FSarkoMapDefinition Bridge;
		FString Error;
		const bool bLoaded = SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Bridge, Error);
		TestTrue(FString::Printf(TEXT("bridge.json loads under the bound: %s"), *Error), bLoaded);
		if (bLoaded)
		{
			int32 Outside = 0;
			for (const FSarkoCoverBlock& Block : Bridge.Blocks)
			{
				if (Block.bSkirt) { ++Outside; }
			}
			for (const FSarkoMapProp& Prop : Bridge.Props)
			{
				if (Prop.bSkirt) { ++Outside; }
			}
			TestTrue(FString::Printf(TEXT("and it has a skirt to be bounded (%d entries)"), Outside), Outside > 50);
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
