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
	TestEqual(TEXT("every prop part was checked"), PartsChecked, SarkoMap::CountPropActors(Map));
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
	// including expanded building walls, every prop part, a container each, a
	// pad each, a bot each, the player, and two lights.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const int32 Actors = 1 + Layout.Cover.Num() + SarkoMap::CountPropActors(Map)
		+ Map.Containers.Num() + Map.Extractions.Num() + Map.BotSpawns.Num() + 1 + 2;

	AddInfo(FString::Printf(TEXT("bridge.json spawns %d actors (%d blocks+walls, %d prop actors from %d props)"),
		Actors, Layout.Cover.Num(), SarkoMap::CountPropActors(Map), Map.Props.Num()));

	// 560 is Bridge_West's ceiling, and it is a DECISION rather than a drift.
	//
	// Where the number comes from. With the east closure, the world border and
	// ТЗ §15's fill of the north in, the bill is 459: 1 floor, 32 authored blocks
	// (12 of them the closure and the border), 32 walls expanded from 5 buildings,
	// 363 prop actors from 351 authored props (the difference is the composite
	// kinds — pylons, road signs, trailers), 19 containers, 3 extraction pads, 6
	// bots, the pawn and two lights. The three camps, the tutorial loot layout and
	// the landmark pass still to come add ~70 more, which is the ~529 the
	// Bridge_West plan projects. 560 leaves about thirty of headroom over that:
	// enough for a crate beside a container, not enough to hide a fill that
	// doubled.
	//
	// Instancing was considered here and deliberately NOT taken. What costs money
	// is draw calls, and SpawnMeshBox -> PaintFlat gives every actor its own
	// UMaterialInstanceDynamic — UE only auto-instances primitives that share a
	// mesh AND a material, so moving these into HISM components would not reduce
	// the draw count until the per-actor MIDs go away first. The cheap mitigation
	// is therefore one shared material instance per ESarkoSurface (eleven instead
	// of five hundred), which is a change to the single spawn path and belongs
	// with its own before/after measurement, not inside a content stage. The
	// saving that WAS available here has been taken: the closure is 12 blocks
	// instead of ~50 tiled treeline props.
	//
	// The trigger that reopens this: a PACKAGED iOS build that misses 30 fps in
	// this sector. Response order is (1) shared per-surface material instances,
	// measured; (2) HISM per (mesh, surface) pair for the fill kinds only — rock,
	// bush, log, fence_section, treeline, about 150 of the prop actors; (3)
	// cutting content. Never (3) first. Nothing this stage added ticks, allocates
	// per frame or replicates, which is why 500-odd static cubes is not where
	// ТЗ §16's budget is expected to break.
	//
	// A failure here still means what it always meant: not a bug, but this
	// decision coming due again.
	TestTrue(FString::Printf(TEXT("the sector spawns at most 560 actors (it spawns %d)"), Actors), Actors <= 560);
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
	TestEqual(TEXT("six bots"), Map.BotSpawns.Num(), 6);
	TestEqual(TEXT("four player spawns"), Map.PlayerSpawns.Num(), 4);
	TestEqual(TEXT("three extractions, one of them reachable"), Map.Extractions.Num(), 3);

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
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestTrue(TEXT("every player spawn is inside the active third"),
			InActiveThird(Spawn.GetLocation()));
	}

	// ТЗ §11: the first fight is one bot. Eight bots that all heard the player
	// and converged turned a firefight into an execution once already, and the
	// hearing radius is 1800 uu — so no two bots may be able to hear the same
	// shot. This is the whole reason six positions were chosen by hand.
	for (int32 A = 0; A < Map.BotSpawns.Num(); ++A)
	{
		for (int32 B = A + 1; B < Map.BotSpawns.Num(); ++B)
		{
			const float Distance = FVector2D(
				Map.BotSpawns[A].Location.X - Map.BotSpawns[B].Location.X,
				Map.BotSpawns[A].Location.Y - Map.BotSpawns[B].Location.Y).Size();
			TestTrue(FString::Printf(TEXT("bots '%s' and '%s' are %.0f uu apart (>= 1800)"),
				*Map.BotSpawns[A].Id, *Map.BotSpawns[B].Id, Distance), Distance >= 1800.f);
		}
	}

	// ТЗ §8: "бот не виден при появлении". The nearest bot is a whole zone away.
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
	// E2 and E3 are data behind the closure, deliberately: ТЗ §12 lists three and
	// Stage D opens them. They must NOT be inside the active third, or the sector
	// would ship with three working exits and no reason to learn the route.
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		if (Spot.Id != TEXT("bridge_extract_north_path"))
		{
			TestFalse(FString::Printf(TEXT("extraction '%s' is behind the closure"), *Spot.Id),
				InActiveThird(Spot.Location));
		}
	}
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
		SolidParts.Num() + NonCollidingParts, SarkoMap::CountPropActors(Map));
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

	TestEqual(TEXT("every player spawn, bot spawn and container was checked"), Points.Num(),
		Map.PlayerSpawns.Num() + Map.BotSpawns.Num() + Map.Containers.Num());
	TestTrue(FString::Printf(TEXT("the ledger's 4 spawns, 6 bots and 19 containers were checked (%d)"),
		Points.Num()), Points.Num() >= 29);
	TestEqual(TEXT("every point was compared against every solid part"),
		Comparisons, Points.Num() * SolidParts.Num());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
