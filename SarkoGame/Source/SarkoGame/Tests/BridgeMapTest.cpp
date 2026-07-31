#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
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
	TestEqual(TEXT("the tutorial raid is 15 minutes"), Map.RaidDurationSeconds, 900.f);

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
	TestTrue(TEXT("there are bots"), Map.BotSpawns.Num() >= 6);
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
	// ТЗ §29: "перед контейнером 120 uu свободно" — a container buried in a wall
	// cannot be looted, and a building wall is the easiest thing to bury one in.
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestFalse(FString::Printf(TEXT("container '%s' is not inside a wall"), *Spot.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Spot.Location.X, Spot.Location.Y), SolidCover));
	}
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
		if (Block.Surface == ESarkoSurface::Water)
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
		if (Block.Surface == ESarkoSurface::Ravine || Block.Surface == ESarkoSurface::Water)
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
	// including expanded building walls, every prop part, a container each, a
	// pad each, a bot each, the player, and two lights.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const int32 Actors = 1 + Layout.Cover.Num() + SarkoMap::CountPropActors(Map)
		+ Map.Containers.Num() + Map.Extractions.Num() + Map.BotSpawns.Num() + 1 + 2;

	AddInfo(FString::Printf(TEXT("bridge.json spawns %d actors (%d blocks+walls, %d prop actors from %d props)"),
		Actors, Layout.Cover.Num(), SarkoMap::CountPropActors(Map), Map.Props.Num()));

	// 450 is the ceiling for Stage B. Stage C's full ledger (≈20 buildings, a
	// treeline border, the north filled per §15) will pass it, and that is the
	// point at which instanced static meshes stop being premature — see the
	// Global Constraints. A failure here is not a bug, it is that decision
	// coming due.
	TestTrue(FString::Printf(TEXT("the sector spawns at most 450 actors (it spawns %d)"), Actors), Actors <= 450);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
