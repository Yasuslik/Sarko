#include "Misc/AutomationTest.h"

#include "AI/SarkoBotArchetypes.h"
#include "Core/SarkoRaidSettings.h"
#include "Loot/SarkoExtractionZone.h"
#include "Map/SarkoBuildings.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	bool LoadBridge(FSarkoMapDefinition& Out, FString& Error)
	{
		return SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Out, Error);
	}

	// There is deliberately no file-local point-in-block helper any more.
	// SarkoMap::IsPointInsideBlocksXY is the one predicate, and it is the one the
	// expander's own invariants use: two copies is exactly how one of them ends
	// up not knowing about building walls.
	//
	// What IS file-local is the FILTER below. Since Task 8 the map contains flat
	// non-colliding blocks — roads, water, the ravine bed — and "nothing spawns
	// inside geometry" (ТЗ §19 "спавны не в препятствиях") means solid geometry:
	// a bot standing on the highway and a container beside a dirt track are both
	// correct, and both are points inside a block. So every obstruction check
	// below asks the one predicate about the solid blocks only. Filtering the
	// input is not a second predicate; a second predicate is what this file used
	// to have and what Task 7 deleted.
	TArray<FSarkoCoverBlock> SolidOnly(const TArray<FSarkoCoverBlock>& Blocks)
	{
		TArray<FSarkoCoverBlock> Solid;
		Solid.Reserve(Blocks.Num());
		for (const FSarkoCoverBlock& Block : Blocks)
		{
			if (Block.bBlocksMovement)
			{
				Solid.Add(Block);
			}
		}
		return Solid;
	}

	/**
	 * Every authored encounter spawn point on the map, flattened, remembering
	 * which encounter it belongs to.
	 *
	 * Since the realism stage the shipped map has NO `botSpawns`: enemies arrive
	 * from encounters. Every "nothing spawns inside geometry" invariant in this
	 * file used to walk Map.BotSpawns and would otherwise now be a green verdict
	 * about an empty array — which is the exact shape of a test that stops
	 * testing. These points are what those invariants walk instead.
	 */
	struct FEncounterPoint
	{
		FString Id;
		FVector Location;
		FVector2D PostPos;
		FName Archetype;
		int32 EncounterIndex = INDEX_NONE;
		FString EncounterId;
	};

	TArray<FEncounterPoint> EncounterSpawnPoints(const FSarkoMapDefinition& Map)
	{
		TArray<FEncounterPoint> Points;
		for (int32 Index = 0; Index < Map.Encounters.Num(); ++Index)
		{
			for (const FSarkoEncounterSpawn& Spawn : Map.Encounters[Index].Spawns)
			{
				Points.Add({ Spawn.Id, Spawn.Location, Spawn.PostPos, Spawn.Archetype, Index, Map.Encounters[Index].Id });
			}
		}
		return Points;
	}

	/** The number of enemies the tutorial's authored encounters actually put on the map. */
	int32 TutorialEnemyCount(const FSarkoMapDefinition& Map)
	{
		int32 Total = 0;
		for (const FSarkoEncounter& Encounter : Map.Encounters)
		{
			Total += FMath::Min(Encounter.BudgetCost, Encounter.MaxAlive);
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeMapIsValid,
	"Sarko.Map.BridgeMapIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeMapIsValid::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const float Extent = Map.ExtentUU;
	TestEqual(TEXT("the sector is 400 m across"), Extent, 20000.f);
	// TEN MINUTES, not fifteen (spec §2). The measured route is ~186 s of walking:
	// in a 900 s raid the clock was scenery and the last five minutes were surplus,
	// and 600 s is the one number that turns it into a budget without making the
	// authored route tight.
	TestEqual(TEXT("the raid is 10 minutes"), Map.RaidDurationSeconds, 600.f);

	// MapExtent (settings) and extentUU (map file) are two copies of one number.
	// Disagreement is silent and ugly in both directions: too small and the AI
	// patrols a box inside the map while the overview crops the sector; too
	// large and bots walk off the floor. Load through the settings' own MapId,
	// so this also pins that the configured map is the one that exists.
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);
	if (Settings)
	{
		FSarkoMapDefinition Configured;
		FString ConfiguredError;
		const bool bConfiguredLoaded = SarkoMap::LoadDefinitionFromDisk(
			Settings->MapId.ToString(), Configured, ConfiguredError);
		TestTrue(FString::Printf(TEXT("the configured map '%s' exists on disk: %s"),
			*Settings->MapId.ToString(), *ConfiguredError), bConfiguredLoaded);
		if (bConfiguredLoaded)
		{
			TestEqual(TEXT("MapExtent agrees with the map file's extentUU"),
				Settings->MapExtent, Configured.ExtentUU);
			// Not compared against USarkoRaidSettings::RaidDurationSeconds on
			// purpose: the map file wins there by design (SarkoRaidGameMode
			// falls back to the setting only when the definition carries no
			// duration), so those two are allowed to differ and do. What must
			// hold is that the map the settings point at is this map.
			TestEqual(TEXT("RaidDurationSeconds is not contradicted by the map file"),
				Configured.RaidDurationSeconds, Map.RaidDurationSeconds);
		}
	}

	// Everything inside the sector.
	const auto CheckInside = [this, Extent](const FVector& Point, const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s is inside the sector"), What),
			FMath::Abs(Point.X) <= Extent && FMath::Abs(Point.Y) <= Extent);
	};
	for (const FSarkoCoverBlock& Block : Map.Blocks)          { CheckInside(Block.Location, TEXT("a block")); }
	for (const FSarkoMapProp& Prop : Map.Props)               { CheckInside(Prop.Location, TEXT("a prop")); }
	for (const FSarkoLootContainerSpot& C : Map.Containers)   { CheckInside(C.Location, TEXT("a container")); }
	for (const FSarkoBotSpot& B : Map.BotSpawns)              { CheckInside(B.Location, TEXT("a bot spawn")); }
	const TArray<FEncounterPoint> EncounterPoints = EncounterSpawnPoints(Map);
	for (const FEncounterPoint& P : EncounterPoints)
	{
		CheckInside(P.Location, TEXT("an encounter spawn point"));
		CheckInside(FVector(P.PostPos.X, P.PostPos.Y, 0.f), TEXT("an encounter post"));
	}

	// Nobody starts inside geometry — solid geometry, see SolidOnly.
	const TArray<FSarkoCoverBlock> SolidBlocks = SolidOnly(Map.Blocks);
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		CheckInside(Spawn.GetLocation(), TEXT("a player spawn"));
		TestFalse(TEXT("no player spawn sits inside a block"),
			SarkoMap::IsPointInsideBlocksXY(
				FVector2D(Spawn.GetLocation().X, Spawn.GetLocation().Y), SolidBlocks));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestFalse(TEXT("no bot spawn sits inside a block"),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Bot.Location.X, Bot.Location.Y), SolidBlocks));
	}
	for (const FEncounterPoint& Point : EncounterPoints)
	{
		TestFalse(FString::Printf(TEXT("encounter spawn point '%s' does not sit inside a block"), *Point.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Point.Location.X, Point.Location.Y), SolidBlocks));
		TestFalse(FString::Printf(TEXT("the post of '%s' is not inside a block"), *Point.Id),
			SarkoMap::IsPointInsideBlocksXY(Point.PostPos, SolidBlocks));
	}

	// Every prop kind resolves, or that prop silently does not appear.
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		FSarkoPropKind Kind;
		TestTrue(FString::Printf(TEXT("prop kind '%s' resolves"), *Prop.Kind.ToString()),
			SarkoMap::FindPropKind(Prop.Kind, Kind));
	}

	// Content density: the frame must not be an empty plain.
	TestTrue(TEXT("there are at least 12 points of interest worth of props"), Map.Props.Num() >= 40);
	TestTrue(TEXT("there is loot to find"), Map.Containers.Num() >= 15);
	// "There are bots" moved from botSpawns to encounters with the realism
	// stage. Written against the encounters rather than the (now empty) bot
	// array on purpose: a count of an array nobody fills is a green verdict
	// about nothing.
	TestTrue(TEXT("there are enemies to meet"), Map.Encounters.Num() >= 3);
	TestTrue(TEXT("and a ceiling on how many"), Map.EncounterBudget.bAuthored);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeExtractionsAreOnOuterEdges,
	"Sarko.Map.BridgeExtractionsAreOnOuterEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeExtractionsAreOnOuterEdges::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("there is more than one way out"), Map.Extractions.Num() >= 2);

	// This sector is a corner of the eventual 800x800 map: the north and west
	// edges stay world border, while east and south open into neighbouring
	// sectors. An extraction on an inner edge would vanish when the map grows.
	const float Edge = Map.ExtentUU * 0.85f;
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		const bool bOnNorth = Spot.Location.Y >= Edge;
		const bool bOnWest = Spot.Location.X <= -Edge;
		TestTrue(FString::Printf(TEXT("extraction '%s' is on an outer edge"), *Spot.Name),
			bOnNorth || bOnWest);
		TestTrue(FString::Printf(TEXT("extraction '%s' has a usable radius"), *Spot.Name),
			Spot.RadiusUU >= 200.f);
		TestFalse(FString::Printf(TEXT("extraction '%s' is named"), *Spot.Name), Spot.Name.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeRiskGradientExists,
	"Sarko.Map.BridgeRiskGradientExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeRiskGradientExists::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The design's core promise: the far half is worth crossing for, and more
	// dangerous. If loot and bots are spread evenly the map has no gradient and
	// the bridge means nothing.
	int32 NearBots = 0;
	int32 FarBots = 0;
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		(Bot.Location.Y > 0.f ? NearBots : FarBots)++;
	}
	// Posts, not spawn points: where a bot HOLDS is where the danger is. A spawn
	// point is a door that has to be far from the player at one instant; the
	// post is the ground the player has to cross.
	for (const FEncounterPoint& Point : EncounterSpawnPoints(Map))
	{
		(Point.PostPos.Y > 0.f ? NearBots : FarBots)++;
	}
	TestTrue(TEXT("the far half holds more bots than the near half"), FarBots > NearBots);

	int32 FarGoodLoot = 0;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		if (Spot.Location.Y < 0.f && (Spot.Tier == TEXT("good") || Spot.Tier == TEXT("military")))
		{
			++FarGoodLoot;
		}
	}
	TestTrue(TEXT("the best loot is across the ravine"), FarGoodLoot >= 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeBuildingsAreEnterable,
	"Sarko.Map.BridgeBuildingsAreEnterable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeBuildingsAreEnterable::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("the sector has walkable buildings at all"), Map.Buildings.Num() >= 3);

	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	// Walls, not roads: since Task 8 the layout also carries flat non-colliding
	// road, water and ravine-bed blocks, and standing on one of those is the
	// opposite of being stuck in a wall.
	const TArray<FSarkoCoverBlock> SolidCover = SolidOnly(Layout.Cover);
	for (const FSarkoBuilding& Building : Map.Buildings)
	{
		TestFalse(FString::Printf(TEXT("building '%s' is named"), *Building.Id), Building.Id.IsEmpty());
		TestTrue(FString::Printf(TEXT("building '%s' is inside the sector"), *Building.Id),
			FMath::Abs(Building.Location.X) <= Map.ExtentUU && FMath::Abs(Building.Location.Y) <= Map.ExtentUU);
		// ТЗ §13: two exits or none. The expander enforces it, but a shipped
		// walkable building with zero doors would pass that and still be a box.
		TestTrue(FString::Printf(TEXT("building '%s' is closed or has two exits"), *Building.Id),
			Building.Doors.Num() == 0 || Building.Doors.Num() >= 2);

		// The centre of every building is standable. This is the one assertion
		// that would catch "the expander emitted a floor" or "the interior walls
		// filled the room" in real authored data rather than in a fixture.
		if (Building.Doors.Num() > 0)
		{
			TestFalse(FString::Printf(TEXT("building '%s' has a standable interior"), *Building.Id),
				SarkoMap::IsPointInsideBlocksXY(
					FVector2D(Building.Location.X, Building.Location.Y), SolidCover));
		}
	}

	// Every id in the spawned layout names exactly one box. The parser refuses a
	// collision between an authored block id and a generated wall id
	// (Sarko.Map.BuildingsFailLoudly pins the rule); this is the same rule
	// checked against the real file, which is where it would actually bite.
	TSet<FString> SeenIds;
	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		if (Block.Id.IsEmpty())
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("layout id '%s' is not a duplicate"), *Block.Id),
			SeenIds.Contains(Block.Id));
		SeenIds.Add(Block.Id);
	}

	// Nobody spawns inside a wall — now including building walls, which the old
	// version of this check could not see because it only looked at Map.Blocks.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestFalse(TEXT("no player spawn sits inside a building wall"),
			SarkoMap::IsPointInsideBlocksXY(
				FVector2D(Spawn.GetLocation().X, Spawn.GetLocation().Y), SolidCover));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestFalse(FString::Printf(TEXT("bot '%s' does not spawn inside a wall"), *Bot.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Bot.Location.X, Bot.Location.Y), SolidCover));
	}
	for (const FEncounterPoint& Point : EncounterSpawnPoints(Map))
	{
		TestFalse(FString::Printf(TEXT("encounter spawn point '%s' is not inside a wall"), *Point.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Point.Location.X, Point.Location.Y), SolidCover));
		TestFalse(FString::Printf(TEXT("the post of '%s' is not inside a wall"), *Point.Id),
			SarkoMap::IsPointInsideBlocksXY(Point.PostPos, SolidCover));
	}
	// ТЗ §29: "перед контейнером 120 uu свободно" — a container buried in a wall
	// cannot be looted, and a building wall is the easiest thing to bury one in.
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestFalse(FString::Printf(TEXT("container '%s' is not inside a wall"), *Spot.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Spot.Location.X, Spot.Location.Y), SolidCover));
	}

	// No two pieces of solid geometry intersect, over the whole shipped map.
	//
	// This is the invariant BuildingTest asserted for a doorless test fixture and
	// nowhere else, and the map was violating it: bridge_gas_station's служебная
	// wall ended on the зал divider's centre line and the two overlapped over
	// 15x30 uu. A fixture cannot catch that, because the shape that produces it is
	// one nobody would build a fixture out of — a person writes "this wall meets
	// that wall" and means the face, and the file says the centre line.
	//
	// Solid only: since Task 8 the layout also carries flat road, water and
	// ravine-bed blocks, and those overlap each other constantly and correctly —
	// a dirt track crossing asphalt is two coats of paint on the same floor.
	for (int32 A = 0; A < SolidCover.Num(); ++A)
	{
		for (int32 B = A + 1; B < SolidCover.Num(); ++B)
		{
			TestFalse(
				FString::Printf(TEXT("solid blocks %d ('%s') and %d ('%s') do not intersect"),
					A, *SolidCover[A].Id, B, *SolidCover[B].Id),
				SarkoMap::BlocksOverlapXY(SolidCover[A], SolidCover[B]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgePropsClearTheWalls,
	"Sarko.Map.BridgePropsClearTheWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgePropsClearTheWalls::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// No prop stands inside a building wall.
	//
	// Props and buildings are authored in different sections of the file, in
	// different coordinate habits — a prop is a world position, a wall is derived
	// from a building's centre, size and yaw — so nothing about the file makes this
	// visible to the person editing it. Two props were found sitting in walls by
	// hand during Stage B, which is precisely the wrong way to find the third.
	//
	// A prop half-buried in a wall is not cosmetic on a top-down map: it is the
	// silhouette the player reads cover from, and a crate whose visible half is
	// inside masonry reads as cover that can be stood behind and cannot be.
	TArray<FSarkoCoverBlock> Walls;
	FString ExpandError;
	if (!SarkoMap::ExpandBuildings(Map.Buildings, Walls, ExpandError))
	{
		AddError(FString::Printf(TEXT("the shipped buildings failed to expand: %s"), *ExpandError));
		return false;
	}
	const TArray<FSarkoCoverBlock> SolidWalls = SolidOnly(Walls);
	TestTrue(TEXT("there are walls to be clear of"), SolidWalls.Num() > 0);

	int32 PartsChecked = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Prop.Kind, Kind))
		{
			continue; // Sarko.Map.BridgeMapIsValid is the test that fails for this
		}
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			// The part as it will actually be spawned: SpawnProps gives every part
			// the prop's yaw and PartWorldLocation's rotated offset, so a composite
			// is compared where it stands rather than where its origin is.
			FSarkoCoverBlock Box;
			Box.Id = Prop.Id.IsEmpty() ? Prop.Kind.ToString() : Prop.Id;
			Box.Location = SarkoMap::PartWorldLocation(Prop.Location, Prop.Yaw, Part);
			Box.Rotation = FRotator(0.f, Prop.Yaw, 0.f);
			Box.Extent = Part.Extent;
			++PartsChecked;

			for (const FSarkoCoverBlock& Wall : SolidWalls)
			{
				TestFalse(
					FString::Printf(TEXT("prop '%s' does not overlap wall '%s'"), *Box.Id, *Wall.Id),
					SarkoMap::BlocksOverlapXY(Box, Wall));
			}
		}
	}
	// A guard against the guard: if the kind lookup or the props section ever comes
	// back empty this test would pass by checking nothing at all.
	TestEqual(TEXT("every prop part was checked"), PartsChecked, SarkoMap::CountPropParts(Map));
	TestTrue(TEXT("there are props to check"), PartsChecked >= 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeHasReadableGroundSurfaces,
	"Sarko.Map.BridgeHasReadableGroundSurfaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeHasReadableGroundSurfaces::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	int32 Roads = 0;
	int32 Water = 0;
	int32 RavineBed = 0;
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		switch (Block.Surface)
		{
			case ESarkoSurface::Asphalt:
			case ESarkoSurface::Dirt:   ++Roads; break;
			// Shallow is water: ТЗ §6's «мелкая вода» at the ford is a lighter tone
			// laid on the same bed, and every rule below that keeps a river out of
			// the fields has to bind it too.
			case ESarkoSurface::Shallow:
			case ESarkoSurface::Water:  ++Water; break;
			case ESarkoSurface::Ravine: ++RavineBed; break;
			default: break;
		}
		// Every flat surface must be walk-over, and every walk-over surface must
		// be flat. A non-colliding block a metre tall is an invisible ramp the
		// pawn climbs; a colliding road is a kerb across the whole map.
		const bool bFlat = Block.Extent.Z <= 30.f;
		const bool bGroundSurface = Block.Surface == ESarkoSurface::Asphalt
			|| Block.Surface == ESarkoSurface::Dirt
			|| Block.Surface == ESarkoSurface::Water
			|| Block.Surface == ESarkoSurface::Shallow
			|| Block.Surface == ESarkoSurface::Ravine;
		if (bGroundSurface)
		{
			TestTrue(FString::Printf(TEXT("ground surface '%s' is flat"), *Block.Id), bFlat);
			TestFalse(FString::Printf(TEXT("ground surface '%s' does not block movement"), *Block.Id),
				Block.bBlocksMovement);
		}
		if (!Block.bBlocksMovement)
		{
			TestTrue(FString::Printf(TEXT("non-colliding block '%s' is flat enough to walk over"), *Block.Id), bFlat);
		}
	}

	TestTrue(TEXT("the sector has roads"), Roads >= 6);
	TestTrue(TEXT("the ravine has water"), Water >= 1);
	TestTrue(TEXT("the ravine has a dark bed"), RavineBed >= 1);

	// ТЗ §5: water sits inside y = -700..+700, the ravine bed inside -2200..+2200.
	// Water outside the bed is a river running across the map.
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		if (Block.Surface == ESarkoSurface::Water || Block.Surface == ESarkoSurface::Shallow)
		{
			TestTrue(FString::Printf(TEXT("water block '%s' stays in the ravine bed"), *Block.Id),
				FMath::Abs(Block.Location.Y) + Block.Extent.Y <= 900.f);
		}
		if (Block.Surface == ESarkoSurface::Ravine)
		{
			TestTrue(FString::Printf(TEXT("ravine bed '%s' stays inside the ravine"), *Block.Id),
				FMath::Abs(Block.Location.Y) + Block.Extent.Y <= 2400.f);
		}
	}

	// The ravine is NOT dug (spec §5.2, restated in Task 8): the bed is a slab a
	// few uu ABOVE the floor, and crossability stays the rim walls' job. A
	// negative z here would be the first step of a pit, which buys fall damage,
	// stuck pawns and nav holes on iOS for zero gameplay.
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		if (Block.Surface == ESarkoSurface::Ravine || Block.Surface == ESarkoSurface::Water
			|| Block.Surface == ESarkoSurface::Shallow)
		{
			TestTrue(FString::Printf(TEXT("'%s' sits on the floor rather than digging through it"), *Block.Id),
				Block.Location.Z - Block.Extent.Z >= 0.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeStaysInsideTheActorBudget,
	"Sarko.Map.BridgeStaysInsideTheActorBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeStaysInsideTheActorBudget::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// ТЗ §16 is a budget, and this is the whole bill: one floor, every block
	// including expanded building walls, ONE prop field, a container each, a pad
	// each, a bot each, the player, and two lights.
	//
	// "One prop field" is the whole of this task's effect on this number. Every
	// prop part in the sector is an instance inside ASarkoPropField now, not an
	// actor of its own.
	//
	// The bot term is now the ENCOUNTER BUDGET rather than the botSpawns count,
	// and it is charged in full even though it is never all standing at once:
	// the budget is the ceiling on how many enemy pawns can exist in one raid,
	// which is exactly what an actor budget wants to know. In practice the
	// sector bills fewer — the first ninety seconds of a raid replicate zero
	// enemies now, against six from second zero before.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const int32 BudgetedEnemies = Map.BotSpawns.Num() + FMath::Max(Map.EncounterBudget.Tutorial, Map.EncounterBudget.Normal);
	const int32 Actors = 1 + Layout.Cover.Num() + 1
		+ Map.Containers.Num() + Map.Extractions.Num() + BudgetedEnemies + 1 + 2;
	const int32 PropComponents = SarkoMap::CountInstancedComponents(Map);
	const int32 PropParts = SarkoMap::CountPropParts(Map);

	AddInfo(FString::Printf(
		TEXT("bridge.json spawns %d actors (%d blocks+walls) and %d instanced components holding %d prop instances from %d props"),
		Actors, Layout.Cover.Num(), PropComponents, PropParts, Map.Props.Num()));

	// THE TRIGGER THIS TEST CARRIED HAS FIRED, AND THIS IS THE RESPONSE.
	//
	// What it used to say: 560 actors, of which 401 were prop parts against a
	// sub-ceiling of 420; instancing was considered and deliberately deferred,
	// because SpawnMeshBox gave every actor its own UMaterialInstanceDynamic and
	// UE only batches primitives sharing a mesh AND a material, so HISM would
	// have bought nothing until those went away. The written response order was
	// (1) shared per-surface material instances, (2) HISM per (mesh, surface) for
	// the fill kinds, (3) cut content — never (3) first.
	//
	// (1) landed in Stage B. The forest is what makes (2) due: a stand the player
	// can walk into is hundreds of trees, and at two parts each the old scheme
	// runs out of budget before the first stand is finished. So step (2) is taken
	// here, and taken further than the note proposed — every kind is instanced,
	// not just the fill ones, because one spawn path is simpler than a spawn path
	// plus a list of exceptions that would have to be kept in step with the kind
	// table by hand.
	//
	// WHAT THE TWO NUMBERS NOW MEAN, and they mean different things:
	//
	//  * ACTORS is what the engine has to tick, replicate, garbage-collect and
	//    hold UObject overhead for. It went from ~540 to ~140 in this commit and
	//    it no longer grows with content at all — a thousand more trees add zero
	//    actors. 180 is a generous ceiling over the ~140 the sector bills today;
	//    it is deliberately not tight, because it is no longer the interesting
	//    number and a tight bound on an uninteresting number is just noise.
	//
	//  * INSTANCED COMPONENTS is what the renderer pays. One component that
	//    agrees on mesh and material is one draw call whether it holds four
	//    instances or four hundred, so THIS is the number a phone feels. It is a
	//    function of the KIND TABLE, not of the map: it goes up when someone adds
	//    a (mesh, surface, collision, canopy) combination nobody was using, and
	//    it does not move when someone plants two hundred trees. 24 is the
	//    ceiling against roughly 14 in use — room for a real amount of new
	//    vocabulary, and low enough that a change which silently split one
	//    component per prop would fail here loudly.
	//
	// The instance count is NOT bounded here on purpose; it is bounded in
	// Sarko.Map.PropInstanceCountIsWithinTheMobileBudget, which is where the
	// "how much geometry is there" question lives.
	//
	// The trigger that reopens THIS decision is unchanged in kind: a PACKAGED iOS
	// build that misses 30 fps in this sector. The response order left is (a) cut
	// draw calls by merging surfaces — fewer components, i.e. fewer colours; (b)
	// cull distances on the fill components; (c) cut content. Still never (c)
	// first. A failure here is not a bug: it is this decision coming due again.
	TestTrue(FString::Printf(TEXT("the sector spawns at most 180 actors (it spawns %d)"), Actors),
		Actors <= 180);
	TestTrue(FString::Printf(TEXT("the props draw in at most 24 instanced components (they use %d)"), PropComponents),
		PropComponents <= 24);

	// Guards against the guard. A key scheme that collapsed to one component
	// would paint the whole sector one colour and pass a ceiling; a key scheme
	// that degenerated to one component per part would put us back where we
	// started with extra steps, and neither shows up as a wrong total.
	TestTrue(FString::Printf(TEXT("the props are genuinely batched (%d instances in %d components)"),
		PropParts, PropComponents), PropParts > PropComponents * 10);
	TestTrue(TEXT("the palette did not collapse into one component"), PropComponents >= 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestLedgerIsAuthored,
	"Sarko.Map.BridgeWestLedgerIsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestLedgerIsAuthored::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The owner's Bridge_West numbers, as numbers. The full map's 42/16 move to
	// docs/design/bridge-full-map-tz.md as the acceptance bar for Stage D; this
	// is the bar for the sector that ships now.
	TestEqual(TEXT("nineteen containers"), Map.Containers.Num(), 19);
	TestEqual(TEXT("four player spawns"), Map.PlayerSpawns.Num(), 4);
	// FOUR since the map-comfort pass: E1/E2/E3 from ТЗ §12, plus the west
	// cordon. Two of the four are reachable now — E1 from the first second, and
	// the west cordon for the last five minutes only.
	TestEqual(TEXT("four extractions, two of them reachable"), Map.Extractions.Num(), 4);

	// THE SIX POSTED BOTS ARE GONE. Owner decision: three to five enemies for
	// the whole tutorial raid, every one of them an event. They are replaced by
	// three encounters costing 1 + 1 + 2 against a budget of 4 — one at the gas
	// station, one on the depot approach, two in the warehouse, and a quiet walk
	// home. The `botSpawns` shape stays supported for non-tutorial content; this
	// map simply authors none.
	TestEqual(TEXT("no statically posted bots"), Map.BotSpawns.Num(), 0);
	TestEqual(TEXT("three encounters"), Map.Encounters.Num(), 3);
	TestEqual(TEXT("a tutorial budget of four"), Map.EncounterBudget.Tutorial, 4);
	TestEqual(TEXT("and the first fight is one enemy"), Map.EncounterBudget.FirstFightMaxAlive, 1);

	int32 TotalCost = 0;
	for (const FSarkoEncounter& Encounter : Map.Encounters)
	{
		TotalCost += Encounter.BudgetCost;
	}
	TestEqual(TEXT("the three encounters spend the tutorial budget exactly"), TotalCost, Map.EncounterBudget.Tutorial);
	TestEqual(TEXT("and put four enemies on the map, in the order 1, 1, 2"), TutorialEnemyCount(Map), 4);

	// Order is what makes "the first fight is the gas station" data rather than
	// luck: it is the tie-break when two triggers arm in the same evaluation.
	TArray<const FSarkoEncounter*> ByOrder;
	for (const FSarkoEncounter& Encounter : Map.Encounters)
	{
		ByOrder.Add(&Encounter);
	}
	ByOrder.Sort([](const FSarkoEncounter& A, const FSarkoEncounter& B) { return A.Order < B.Order; });
	TestEqual(TEXT("the gas station is first"), ByOrder[0]->Id, FString(TEXT("bridge_enc_gas_station")));
	TestEqual(TEXT("and it is one enemy"), ByOrder[0]->MaxAlive, 1);
	TestEqual(TEXT("the depot approach is second"), ByOrder[1]->Id, FString(TEXT("bridge_enc_depot_approach")));
	TestEqual(TEXT("also one"), ByOrder[1]->MaxAlive, 1);
	TestEqual(TEXT("the warehouse is last"), ByOrder[2]->Id, FString(TEXT("bridge_enc_rail_warehouse")));
	TestEqual(TEXT("and it is the two-bot fight the escalation builds to"), ByOrder[2]->MaxAlive, 2);
	for (const FSarkoEncounter& Encounter : Map.Encounters)
	{
		TestTrue(FString::Printf(TEXT("tutorial encounter '%s' is one-shot"), *Encounter.Id), Encounter.bOneShot);
	}

	// Two triggers that overlap are two events that happen at once, which is how
	// a 1/1/2 escalation collapses into a single four-bot moment. The gas
	// station and the depot approach deliberately DO overlap (the route is only
	// 8600 uu long and the gas trigger arms 2600 uu before the station), so this
	// is asserted for the pair it matters for: the last two, which are the ones
	// that would otherwise put three enemies on the map in one breath.
	{
		const float Separation = FVector2D::Distance(ByOrder[1]->Trigger.Location, ByOrder[2]->Trigger.Location);
		TestTrue(FString::Printf(TEXT("the depot approach and the warehouse triggers do not overlap (%.0f uu apart, radii %.0f + %.0f)"),
			Separation, ByOrder[1]->Trigger.RadiusUU, ByOrder[2]->Trigger.RadiusUU),
			Separation > ByOrder[1]->Trigger.RadiusUU + ByOrder[2]->Trigger.RadiusUU);
	}

	// 3 junk / 7 common / 6 good / 1 med / 2 military.
	TMap<FName, int32> Tiers;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		Tiers.FindOrAdd(Spot.Tier)++;
	}
	TestEqual(TEXT("three junk"), Tiers.FindRef(TEXT("junk")), 3);
	TestEqual(TEXT("seven common"), Tiers.FindRef(TEXT("common")), 7);
	TestEqual(TEXT("six good"), Tiers.FindRef(TEXT("good")), 6);
	TestEqual(TEXT("one med"), Tiers.FindRef(TEXT("med")), 1);
	TestEqual(TEXT("two military"), Tiers.FindRef(TEXT("military")), 2);

	// The active third: the barrier Task 2 builds runs at x = -6100 north of the
	// ravine and x = -9100 south of it, so nothing the player is meant to reach
	// may be authored east of those lines. Checked as data rather than left to
	// the closure, because a container behind the barrier is loot that exists,
	// is on the ledger, and can never be picked up.
	const auto InActiveThird = [](const FVector& P)
	{
		return P.Y > 0.f ? P.X <= -6100.f : P.X <= -9100.f;
	};
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestTrue(FString::Printf(TEXT("container '%s' is inside the active third"), *Spot.Id),
			InActiveThird(Spot.Location));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestTrue(FString::Printf(TEXT("bot '%s' is inside the active third"), *Bot.Id),
			InActiveThird(Bot.Location));
	}
	for (const FEncounterPoint& Point : EncounterSpawnPoints(Map))
	{
		TestTrue(FString::Printf(TEXT("encounter spawn point '%s' is inside the active third"), *Point.Id),
			InActiveThird(Point.Location));
		TestTrue(FString::Printf(TEXT("the post of '%s' is inside the active third"), *Point.Id),
			InActiveThird(FVector(Point.PostPos.X, Point.PostPos.Y, 0.f)));
	}
	for (const FSarkoEncounter& Encounter : Map.Encounters)
	{
		TestTrue(FString::Printf(TEXT("the trigger of '%s' is inside the active third"), *Encounter.Id),
			InActiveThird(FVector(Encounter.Trigger.Location.X, Encounter.Trigger.Location.Y, 0.f)));
	}
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestTrue(TEXT("every player spawn is inside the active third"),
			InActiveThird(Spawn.GetLocation()));
	}

	// THE PLACEMENT INVARIANT, REPLACED.
	//
	// What this used to assert: every pair of bot posts is >= 1800 uu apart. It
	// tested the wrong thing, and the map it was protecting failed the right one
	// while passing this one. Distance between two bots is not what hurts the
	// player — what hurts the player is standing on a point they MUST stand on
	// and being inside two bots' hearing at the same time. Measured on the
	// shipped map before this change: bridge_loot_rail_mil_01, the pistol, the
	// tutorial's climax, was 1170 uu from bridge_bot_rail_west and 1414 uu from
	// bridge_bot_rail_warehouse — inside both, at the moment the player has the
	// least to fight with. Every pair test passed.
	//
	// So: containers, not bot pairs. For every container and every extraction,
	// the posts that can hear it must all belong to AT MOST ONE ENCOUNTER. Two
	// bots from the SAME encounter hearing one crate is the authored two-bot
	// fight (the warehouse is exactly that, on purpose). Two bots from two
	// unrelated encounters converging on it is the bug.
	//
	// Hearing is per archetype, not the project default: the whole reason
	// archetypes carry their own hearing radius is that "how far a bot listens"
	// is a property of the bot.
	{
		const TArray<FEncounterPoint> Posts = EncounterSpawnPoints(Map);
		TArray<TPair<FString, FVector2D>> MustStandOn;
		for (const FSarkoLootContainerSpot& Spot : Map.Containers)
		{
			MustStandOn.Emplace(Spot.Id, FVector2D(Spot.Location.X, Spot.Location.Y));
		}
		for (const FSarkoExtractionSpot& Spot : Map.Extractions)
		{
			MustStandOn.Emplace(Spot.Id, FVector2D(Spot.Location.X, Spot.Location.Y));
		}

		int32 HeardByAnyone = 0;
		for (const TPair<FString, FVector2D>& Point : MustStandOn)
		{
			TSet<int32> ListeningEncounters;
			FString Listeners;
			for (const FEncounterPoint& Post : Posts)
			{
				FSarkoBotArchetype Archetype;
				if (!SarkoAI::FindBotArchetype(Post.Archetype, Archetype))
				{
					// Sarko.Map.BridgeMapIsValid is not the test that fails for
					// this; the PARSER is, at load. Reaching here means the table
					// and the parser have drifted apart.
					AddError(FString::Printf(TEXT("'%s' has archetype '%s', which is not in the archetype table"),
						*Post.Id, *Post.Archetype.ToString()));
					continue;
				}
				const float Distance = FVector2D::Distance(Point.Value, Post.PostPos);
				if (Distance <= Archetype.HearingRadiusUU)
				{
					ListeningEncounters.Add(Post.EncounterIndex);
					Listeners += FString::Printf(TEXT("%s (%s, %.0f uu of %.0f) "),
						*Post.Id, *Post.EncounterId, Distance, Archetype.HearingRadiusUU);
				}
			}
			if (ListeningEncounters.Num() > 0)
			{
				++HeardByAnyone;
			}
			TestTrue(FString::Printf(
				TEXT("'%s' is inside the hearing of at most one encounter's posts — heard by: %s"),
				*Point.Key, Listeners.IsEmpty() ? TEXT("nobody") : *Listeners),
				ListeningEncounters.Num() <= 1);
		}

		// A guard against the guard. If every post drifted out of earshot of
		// every container this test would pass by testing nothing — and a map
		// where no crate is ever guarded is a map with no tension in it.
		TestTrue(FString::Printf(TEXT("some containers ARE guarded (%d of %d points are heard by someone)"),
			HeardByAnyone, MustStandOn.Num()), HeardByAnyone >= 4);
	}

	// ТЗ §8: "бот не виден при появлении". The nearest post is a whole zone
	// away, so a raid cannot begin within earshot of anything. With encounters
	// this is doubly true — nothing is spawned at all until the player walks to
	// a POI — but the authored geometry still has to hold on its own.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		for (const FSarkoBotSpot& Bot : Map.BotSpawns)
		{
			const float Distance = FVector2D(
				Spawn.GetLocation().X - Bot.Location.X,
				Spawn.GetLocation().Y - Bot.Location.Y).Size();
			TestTrue(FString::Printf(TEXT("bot '%s' is not visible from a spawn (%.0f uu)"),
				*Bot.Id, Distance), Distance >= 6000.f);
		}
		for (const FEncounterPoint& Point : EncounterSpawnPoints(Map))
		{
			const float Distance = FVector2D::Distance(
				FVector2D(Spawn.GetLocation().X, Spawn.GetLocation().Y),
				FVector2D(Point.Location.X, Point.Location.Y));
			TestTrue(FString::Printf(TEXT("encounter spawn point '%s' is not visible from a player spawn (%.0f uu)"),
				*Point.Id, Distance), Distance >= 6000.f);
		}
	}

	// And the trigger circles cannot reach the spawn camp: an encounter that
	// arms while the player is still choosing which crate to open would put an
	// enemy on the map before the raid has taught anything.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		for (const FSarkoEncounter& Encounter : Map.Encounters)
		{
			const float Distance = FVector2D::Distance(
				FVector2D(Spawn.GetLocation().X, Spawn.GetLocation().Y), Encounter.Trigger.Location);
			TestTrue(FString::Printf(TEXT("the trigger of '%s' does not reach a player spawn (%.0f uu, radius %.0f)"),
				*Encounter.Id, Distance, Encounter.Trigger.RadiusUU),
				Distance > Encounter.Trigger.RadiusUU + 6000.f);
		}
	}

	// ТЗ §12's E1, to the unit. This is the one extraction a Bridge_West player
	// can reach, and the tutorial's last teaching beat is standing in it.
	const FSarkoExtractionSpot* E1 = Map.Extractions.FindByPredicate(
		[](const FSarkoExtractionSpot& Spot) { return Spot.Id == TEXT("bridge_extract_north_path"); });
	TestNotNull(TEXT("E1 exists by id"), E1);
	if (E1)
	{
		TestTrue(TEXT("E1 is at ТЗ §12's (-15500, +19500)"),
			FMath::IsNearlyEqual(static_cast<float>(E1->Location.X), -15500.f, 1.f) &&
			FMath::IsNearlyEqual(static_cast<float>(E1->Location.Y), 19500.f, 1.f));
		TestTrue(TEXT("E1 is inside the active third"), InActiveThird(E1->Location));
	}
	// THE SECOND REACHABLE EXIT, and the answer to the 93-second walk home. It is
	// in the active third on purpose — the one extraction other than E1 that is —
	// and it is gated by TIME rather than by geometry.
	const FSarkoExtractionSpot* West = Map.Extractions.FindByPredicate(
		[](const FSarkoExtractionSpot& Spot) { return Spot.Id == TEXT("bridge_extract_west_cordon"); });
	TestNotNull(TEXT("the west cordon exists by id"), West);
	if (West)
	{
		TestTrue(TEXT("the west cordon is reachable"), InActiveThird(West->Location));
		// 240, forced by spec §2 and matching what spec §3.3 asks for anyway. It
		// was 600 — the last five minutes of a 900 s raid — and the raid is 600 s
		// now, so 600 would have been a pad that opens exactly as the clock expires:
		// a dead extraction dressed as a choice, and an assertion below that could
		// never hold. 240 makes it a live option while the player is still deep in
		// the west, which is when "run for it or loot one more crate" is a question.
		TestEqual(TEXT("and it opens four minutes in"), West->OpensAfterSeconds, 240.f);
		TestTrue(TEXT("which is a real wait, and a real window: shut for a while, then open for most of the raid"),
			West->OpensAfterSeconds > 0.f
				&& West->OpensAfterSeconds >= Map.RaidDurationSeconds * 0.25f
				&& West->OpensAfterSeconds <= Map.RaidDurationSeconds * 0.6f);
		// It has to be a genuine alternative or it is scenery: closer to the last
		// loot on the route than E1 is, and on the far side of no crossing.
		const FSarkoLootContainerSpot* Mil02 = Map.Containers.FindByPredicate(
			[](const FSarkoLootContainerSpot& Spot) { return Spot.Id == TEXT("bridge_loot_rail_mil_02"); });
		if (Mil02 && E1)
		{
			const float ToWest = FVector2D(West->Location.X - Mil02->Location.X,
				West->Location.Y - Mil02->Location.Y).Size();
			const float ToE1 = FVector2D(E1->Location.X - Mil02->Location.X,
				E1->Location.Y - Mil02->Location.Y).Size();
			TestTrue(FString::Printf(
					TEXT("the west cordon (%.0f uu from the last crate) is a real shortcut against E1 (%.0f uu)"),
					ToWest, ToE1),
				ToWest < ToE1 * 0.75f);
		}
	}

	// E2 and E3 are data behind the closure, deliberately: ТЗ §12 lists three and
	// Stage D opens them. They must NOT be inside the active third, or the sector
	// would ship with working exits the route never has to be learned for.
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		if (Spot.Id != TEXT("bridge_extract_north_path") && Spot.Id != TEXT("bridge_extract_west_cordon"))
		{
			TestFalse(FString::Printf(TEXT("extraction '%s' is behind the closure"), *Spot.Id),
				InActiveThird(Spot.Location));
		}
	}

	// Exactly one extraction is open from the first second. Two would make the
	// timed one pointless; none would make the raid unwinnable for ten minutes.
	int32 OpenAtStart = 0;
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		if (InActiveThird(Spot.Location) && SarkoExtract::IsZoneOpen(Spot.OpensAfterSeconds, 0.f))
		{
			++OpenAtStart;
		}
	}
	TestEqual(TEXT("exactly one reachable extraction is open at second zero"), OpenAtStart, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestIsEnclosed,
	"Sarko.Map.BridgeWestIsEnclosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestIsEnclosed::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const TArray<FSarkoCoverBlock> Solid = SolidOnly(Layout.Cover);

	// Walk each barrier's centre line at 100 uu — a quarter of the pawn's own
	// width — and require solid geometry at every sample. Sampling is the only
	// honest check here: "there are six blocks" says nothing about whether they
	// meet, and the failure mode of a barrier is a gap, not a missing piece.
	const auto RequireSolidAlongY = [this, &Solid](float X, float FromY, float ToY, const TCHAR* What)
	{
		int32 Holes = 0;
		float FirstHole = 0.f;
		for (float Y = FromY; Y <= ToY; Y += 100.f)
		{
			if (!SarkoMap::IsPointInsideBlocksXY(FVector2D(X, Y), Solid))
			{
				if (Holes == 0) { FirstHole = Y; }
				++Holes;
			}
		}
		TestEqual(FString::Printf(TEXT("%s is unbroken (first hole at y=%.0f of %d samples)"),
			What, FirstHole, Holes), Holes, 0);
	};
	const auto RequireSolidAlongX = [this, &Solid](float Y, float FromX, float ToX, const TCHAR* What)
	{
		int32 Holes = 0;
		float FirstHole = 0.f;
		for (float X = FromX; X <= ToX; X += 100.f)
		{
			if (!SarkoMap::IsPointInsideBlocksXY(FVector2D(X, Y), Solid))
			{
				if (Holes == 0) { FirstHole = X; }
				++Holes;
			}
		}
		TestEqual(FString::Printf(TEXT("%s is unbroken (first hole at x=%.0f of %d samples)"),
			What, FirstHole, Holes), Holes, 0);
	};

	// The east closure. Each run stops at the ravine, where the rim walls close
	// everything between x = -13600 and -1100 already — so the two runs plus the
	// rims are one continuous flank, and the step between them is invisible
	// because it happens inside a gorge nobody can walk along.
	RequireSolidAlongY(-6100.f, 2100.f, 20000.f, TEXT("the east closure, north of the ravine"));
	RequireSolidAlongY(-9100.f, -20000.f, -2100.f, TEXT("the east closure, south of the ravine"));

	// The world border. Without it the rail depot at y = -19000 is 1000 uu from
	// the floor's edge, and walking off a 400 m plane is a fall, a KillZ death and
	// a lost haul — the worst way to lose a raid, and not one the ТЗ ever asked
	// for. The ravine's mouth at the map's west edge gets its own piece: the
	// gorge is reachable through the pipes, and it runs straight off the world.
	RequireSolidAlongY(-19850.f, -19700.f, -2100.f, TEXT("the west border, south of the ravine"));
	RequireSolidAlongY(-19850.f, -1500.f, 1500.f, TEXT("the west border across the ravine mouth"));
	RequireSolidAlongY(-19850.f, 2100.f, 19700.f, TEXT("the west border, north of the ravine"));
	RequireSolidAlongX(-19850.f, -20000.f, -9300.f, TEXT("the south border"));

	// The north border has exactly one mouth, and it is E1. Both halves are
	// unbroken; the gap between them contains the extraction and nothing else.
	RequireSolidAlongX(19850.f, -20000.f, -16100.f, TEXT("the north border west of E1"));
	RequireSolidAlongX(19850.f, -14900.f, -6300.f, TEXT("the north border east of E1"));
	TestFalse(TEXT("E1's mouth is open"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-15500.f, 19850.f), Solid));

	// And the mouth is E1's, not a hole beside it: the open span is 1200 uu and
	// E1's 500 uu radius sits inside it.
	TestTrue(TEXT("the mouth is where the extraction is"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-16200.f, 19850.f), Solid) &&
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-14800.f, 19850.f), Solid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeSpawnsClearTheProps,
	"Sarko.Map.BridgeSpawnsClearTheProps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeSpawnsClearTheProps::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// Nobody starts inside a PROP.
	//
	// The gap this closes: every other "spawns are not in geometry" invariant in
	// this file asks about blocks and about expanded building walls, and a prop is
	// neither. So the ledger could put — and did put — a bot inside a freight car
	// at (-12300,-15600) with a full green suite, because a 1400 x 300 uu solid
	// box that happens to be authored in the "props" section was invisible to
	// every check that existed. The pawn spawns embedded in it, and the first
	// thing the player meets is an enemy that cannot walk.
	//
	// Same shape as Sarko.Map.BridgePropsClearTheWalls, from the other side: each
	// part is built as the box it will actually be spawned as — the prop's yaw and
	// PartWorldLocation's rotated offset — so a composite is tested where it
	// stands rather than where its origin is.
	TArray<FSarkoCoverBlock> SolidParts;
	int32 NonCollidingParts = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Prop.Kind, Kind))
		{
			continue; // Sarko.Map.BridgeMapIsValid is the test that fails for this
		}
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			// Walk-through decoration is skipped on purpose: a bush is the one kind
			// with no collision, and a container standing in one is a container in
			// a bush. Failing that would be failing correct authoring.
			if (!Part.bBlocksMovement)
			{
				++NonCollidingParts;
				continue;
			}
			FSarkoCoverBlock Box;
			Box.Id = Prop.Id.IsEmpty() ? Prop.Kind.ToString() : Prop.Id;
			Box.Location = SarkoMap::PartWorldLocation(Prop.Location, Prop.Yaw, Part);
			Box.Rotation = FRotator(0.f, Prop.Yaw, 0.f);
			Box.Extent = Part.Extent;
			SolidParts.Add(Box);
		}
	}

	// A guard against the guard, in two directions: every part of every prop was
	// classified (so an empty props section or a failed kind lookup fails HERE
	// rather than passing by checking nothing), and there is a real amount of
	// solid geometry to be clear of.
	TestEqual(TEXT("every prop part was classified as solid or walk-through"),
		SolidParts.Num() + NonCollidingParts, SarkoMap::CountPropParts(Map));
	TestTrue(FString::Printf(TEXT("there are solid prop parts to clear (%d)"), SolidParts.Num()),
		SolidParts.Num() >= 200);

	// Named points, because the failure message has to say which prop: "a bot
	// spawn sits inside a block" sent someone hunting through 21 blocks once.
	TArray<TPair<FString, FVector2D>> Points;
	for (int32 Index = 0; Index < Map.PlayerSpawns.Num(); ++Index)
	{
		const FVector Location = Map.PlayerSpawns[Index].GetLocation();
		const FString Id = Map.PlayerSpawnIds.IsValidIndex(Index) && !Map.PlayerSpawnIds[Index].IsEmpty()
			? Map.PlayerSpawnIds[Index]
			: FString::Printf(TEXT("playerSpawns[%d]"), Index);
		Points.Emplace(Id, FVector2D(Location.X, Location.Y));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		Points.Emplace(Bot.Id, FVector2D(Bot.Location.X, Bot.Location.Y));
	}
	// Encounter spawn points are exactly the case this test exists for: the
	// original bug was a hand-placed bot inside a freight car, and an encounter
	// spawn point is the same hand-placed coordinate under a new key. The post
	// is checked too — a bot that holds inside a solid prop cannot patrol.
	const TArray<FEncounterPoint> EncounterPoints = EncounterSpawnPoints(Map);
	for (const FEncounterPoint& Point : EncounterPoints)
	{
		Points.Emplace(Point.Id, FVector2D(Point.Location.X, Point.Location.Y));
		Points.Emplace(Point.Id + TEXT(" (post)"), Point.PostPos);
	}
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		Points.Emplace(Spot.Id, FVector2D(Spot.Location.X, Spot.Location.Y));
	}

	int32 Comparisons = 0;
	for (const TPair<FString, FVector2D>& Point : Points)
	{
		for (const FSarkoCoverBlock& Part : SolidParts)
		{
			++Comparisons;
			// One-element array rather than a second predicate: SarkoMap's own
			// point-in-block test is the one the expander uses, and two copies of
			// it is exactly how one of them ends up not knowing about yaw.
			TestFalse(
				FString::Printf(TEXT("'%s' at (%.0f, %.0f) does not stand inside prop '%s'"),
					*Point.Key, Point.Value.X, Point.Value.Y, *Part.Id),
				SarkoMap::IsPointInsideBlocksXY(Point.Value, { Part }));
		}
	}

	TestEqual(TEXT("every player spawn, bot spawn, encounter point and container was checked"), Points.Num(),
		Map.PlayerSpawns.Num() + Map.BotSpawns.Num() + EncounterPoints.Num() * 2 + Map.Containers.Num());
	TestTrue(FString::Printf(TEXT("the ledger's 4 spawns, 7 encounter points (and their posts) and 19 containers were checked (%d)"),
		Points.Num()), Points.Num() >= 29);
	TestEqual(TEXT("every point was compared against every solid part"),
		Comparisons, Points.Num() * SolidParts.Num());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
