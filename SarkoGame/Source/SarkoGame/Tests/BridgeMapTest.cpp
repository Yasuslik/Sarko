#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	bool LoadBridge(FSarkoMapDefinition& Out, FString& Error)
	{
		return SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Out, Error);
	}

	bool IsInsideBlock(const FVector& Point, const TArray<FSarkoCoverBlock>& Blocks)
	{
		for (const FSarkoCoverBlock& Block : Blocks)
		{
			const FVector Local = Block.Rotation.UnrotateVector(Point - Block.Location);
			if (FMath::Abs(Local.X) <= Block.Extent.X && FMath::Abs(Local.Y) <= Block.Extent.Y)
			{
				return true;
			}
		}
		return false;
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

	// Nobody starts inside geometry.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		CheckInside(Spawn.GetLocation(), TEXT("a player spawn"));
		TestFalse(TEXT("no player spawn sits inside a block"), IsInsideBlock(Spawn.GetLocation(), Map.Blocks));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestFalse(TEXT("no bot spawn sits inside a block"), IsInsideBlock(Bot.Location, Map.Blocks));
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

#endif // WITH_AUTOMATION_TESTS
