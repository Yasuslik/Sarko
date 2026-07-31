#include "Misc/AutomationTest.h"

#include "Map/SarkoBuildings.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** A 2000 x 1500 shed with two doors — the shape of half the ТЗ's ledger. */
	FSarkoBuilding MakeShed()
	{
		FSarkoBuilding Building;
		Building.Id = TEXT("test_shed");
		Building.Location = FVector(0.f, 0.f, 0.f);
		Building.SizeUU = FVector2D(2000.f, 1500.f);
		Building.WallHeightUU = 350.f;
		Building.WallThicknessUU = 30.f;
		Building.Surface = ESarkoSurface::Structure;
		return Building;
	}

	FSarkoBuildingDoor MakeDoor(ESarkoBuildingSide Side, float Offset, float Width)
	{
		FSarkoBuildingDoor Door;
		Door.Side = Side;
		Door.OffsetUU = Offset;
		Door.WidthUU = Width;
		return Door;
	}

	/**
	 * Walks a straight line in the horizontal plane and counts how many samples
	 * land inside a wall. Sampling rather than analysis on purpose: it tests the
	 * geometry that was actually emitted, not the geometry the expander meant to
	 * emit, so an off-by-one in a segment's centre cannot hide behind matching
	 * arithmetic in the test.
	 */
	int32 CountInside(const TArray<FSarkoCoverBlock>& Blocks, const FVector2D& From, const FVector2D& To, int32 Samples)
	{
		int32 Inside = 0;
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const double Alpha = static_cast<double>(Index) / static_cast<double>(Samples);
			if (SarkoMap::IsPointInsideBlocksXY(From + (To - From) * Alpha, Blocks))
			{
				++Inside;
			}
		}
		return Inside;
	}

	/**
	 * A point of the building's local frame, in world space.
	 *
	 * Only the yaw test needs this, and it needs it because rotation is affine:
	 * rotating two endpoints and interpolating between them in the world is the
	 * same line as interpolating locally and rotating, so the sealed-shell walk
	 * works unchanged at any angle.
	 */
	FVector2D LocalToWorld(const FSarkoBuilding& Building, const FVector2D& Local)
	{
		const FVector Rotated = FRotator(0.f, Building.Yaw, 0.f).RotateVector(FVector(Local.X, Local.Y, 0.f));
		return FVector2D(Building.Location.X + Rotated.X, Building.Location.Y + Rotated.Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoClosedBuildingIsSealed,
	"Sarko.Map.ClosedBuildingIsSealed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoClosedBuildingIsSealed::RunTest(const FString& Parameters)
{
	// A building with no doors is the degenerate case the ТЗ's ledger needs four
	// times (N01, S06, S17, S18 are закрыты) and it must be genuinely sealed:
	// four walls, meeting at the corners, with nothing inside and no lid.
	const FSarkoBuilding Shed = MakeShed();
	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a closed building expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Shed, Blocks, Error));
	TestEqual(TEXT("a closed rectangle is exactly four walls"), Blocks.Num(), 4);

	const float T = Shed.WallThicknessUU;
	const float HalfX = Shed.SizeUU.X * 0.5f;
	const float HalfY = Shed.SizeUU.Y * 0.5f;

	// No roof and no floor slab: a top-down camera must see inside, and spec
	// §5.1 forbids both. Every emitted block is a WALL, i.e. thin on exactly one
	// horizontal axis. A roof or a floor would be thick on both.
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		const bool bThinX = Block.Extent.X <= T * 0.5f + 0.01f;
		const bool bThinY = Block.Extent.Y <= T * 0.5f + 0.01f;
		TestTrue(TEXT("every emitted block is a wall, thin on one axis"), bThinX || bThinY);
		TestFalse(TEXT("no block is thin on both axes (that would be a post, not a wall)"), bThinX && bThinY);
		TestTrue(TEXT("walls are the building's height"),
			FMath::IsNearlyEqual(static_cast<float>(Block.Extent.Z), Shed.WallHeightUU * 0.5f, 0.01f));
		TestTrue(TEXT("walls stand on the floor, not through it"),
			FMath::IsNearlyEqual(static_cast<float>(Block.Location.Z), Shed.WallHeightUU * 0.5f, 0.01f));
		TestTrue(TEXT("walls block movement and bullets"), Block.bBlocksMovement);
		TestEqual(TEXT("walls carry the building's surface"),
			static_cast<uint8>(Block.Surface), static_cast<uint8>(Shed.Surface));
		TestTrue(TEXT("every wall is traceable to its building"), Block.Id.StartsWith(Shed.Id));
	}

	// The interior is empty. This is the "no roof, no floor" assertion from the
	// other side: if either existed, the centre of the building would be solid.
	TestFalse(TEXT("the middle of the building is walkable"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(0.f, 0.f), Blocks));

	// The shell closes. Walk each wall's centre line end to end: with no doors,
	// every single sample must be inside a wall. One gap anywhere and the
	// building is not a building.
	const float WallY = HalfY - T * 0.5f;
	const float WallX = HalfX - T * 0.5f;
	TestEqual(TEXT("the north wall has no gaps"),
		CountInside(Blocks, FVector2D(-HalfX + 1.f, WallY), FVector2D(HalfX - 1.f, WallY), 200), 201);
	TestEqual(TEXT("the south wall has no gaps"),
		CountInside(Blocks, FVector2D(-HalfX + 1.f, -WallY), FVector2D(HalfX - 1.f, -WallY), 200), 201);
	TestEqual(TEXT("the east wall has no gaps"),
		CountInside(Blocks, FVector2D(WallX, -HalfY + T + 1.f), FVector2D(WallX, HalfY - T - 1.f), 200), 201);
	TestEqual(TEXT("the west wall has no gaps"),
		CountInside(Blocks, FVector2D(-WallX, -HalfY + T + 1.f), FVector2D(-WallX, HalfY - T - 1.f), 200), 201);

	// No wall overlaps another. Overlapping geometry is not a rendering problem
	// here, it is a review problem: it makes "how much wall did we emit" and
	// "how much wall is there" different numbers, which is how a missing segment
	// hides.
	for (int32 A = 0; A < Blocks.Num(); ++A)
	{
		for (int32 B = A + 1; B < Blocks.Num(); ++B)
		{
			const bool bOverlapX = FMath::Abs(Blocks[A].Location.X - Blocks[B].Location.X)
				< Blocks[A].Extent.X + Blocks[B].Extent.X - 0.01f;
			const bool bOverlapY = FMath::Abs(Blocks[A].Location.Y - Blocks[B].Location.Y)
				< Blocks[A].Extent.Y + Blocks[B].Extent.Y - 0.01f;
			TestFalse(FString::Printf(TEXT("walls %d and %d do not overlap"), A, B), bOverlapX && bOverlapY);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDoorwaysAreRealGaps,
	"Sarko.Map.DoorwaysAreRealGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDoorwaysAreRealGaps::RunTest(const FString& Parameters)
{
	// The single invariant this whole abstraction exists to guarantee: a doorway
	// declared in data is an actual hole in the actual geometry. The failure it
	// guards against is not theoretical — an off-by-half-thickness in a segment
	// centre leaves a 15 uu lip across the opening, which is invisible in a
	// screenshot and stops the pawn dead.
	FSarkoBuilding Shed = MakeShed();
	Shed.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 300.f, 320.f));
	Shed.Doors.Add(MakeDoor(ESarkoBuildingSide::South, -400.f, 300.f));

	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a building with two doors expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Shed, Blocks, Error));
	// Two doors, each splitting one side in two: 4 + 2 = 6 segments.
	TestEqual(TEXT("two doors turn four walls into six segments"), Blocks.Num(), 6);

	const float T = Shed.WallThicknessUU;
	const float WallX = Shed.SizeUU.X * 0.5f - T * 0.5f;
	const float WallY = Shed.SizeUU.Y * 0.5f - T * 0.5f;

	// The east door: clear across its whole declared width, at the wall's own
	// centre line. Sampled 1 uu inside each edge so the test is about the gap
	// and not about floating-point equality at the boundary.
	TestEqual(TEXT("the east doorway is completely open"),
		CountInside(Blocks, FVector2D(WallX, 300.f - 160.f + 1.f), FVector2D(WallX, 300.f + 160.f - 1.f), 60), 0);
	// And the wall resumes immediately outside it, both sides. A "gap" that is
	// really the end of the wall would pass the test above and fail this one.
	TestTrue(TEXT("the east wall resumes north of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, 300.f + 160.f + 5.f), Blocks));
	TestTrue(TEXT("the east wall resumes south of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, 300.f - 160.f - 5.f), Blocks));

	TestEqual(TEXT("the south doorway is completely open"),
		CountInside(Blocks, FVector2D(-400.f - 150.f + 1.f, -WallY), FVector2D(-400.f + 150.f - 1.f, -WallY), 60), 0);
	TestTrue(TEXT("the south wall resumes east of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-400.f + 150.f + 5.f, -WallY), Blocks));
	TestTrue(TEXT("the south wall resumes west of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-400.f - 150.f - 5.f, -WallY), Blocks));

	// The doorway is wide enough to walk through carrying a backpack: the pawn's
	// capsule is ~68 uu across, and ТЗ §13 sets the floor at 250 uu.
	TestTrue(TEXT("no shipped doorway is narrower than the minimum"),
		Shed.Doors[0].WidthUU >= SarkoMap::MinDoorwayUU && Shed.Doors[1].WidthUU >= SarkoMap::MinDoorwayUU);

	// Total wall length on a side equals the side's length minus the door: the
	// arithmetic check that complements the sampling. A doubled segment or a
	// missing stub changes this number and nothing else.
	float SouthLength = 0.f;
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		if (FMath::IsNearlyEqual(static_cast<float>(Block.Location.Y), -WallY, 0.01f))
		{
			SouthLength += static_cast<float>(Block.Extent.X) * 2.f;
		}
	}
	TestTrue(TEXT("the south wall is exactly its length minus the doorway"),
		FMath::IsNearlyEqual(SouthLength, Shed.SizeUU.X - 300.f, 0.05f));

	// Sides with no doors are untouched by the doors on other sides.
	TestEqual(TEXT("the north wall is still one unbroken segment"),
		CountInside(Blocks, FVector2D(-Shed.SizeUU.X * 0.5f + 1.f, WallY),
			FVector2D(Shed.SizeUU.X * 0.5f - 1.f, WallY), 200), 201);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteriorWallsDivideAndPass,
	"Sarko.Map.InteriorWallsDivideAndPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteriorWallsDivideAndPass::RunTest(const FString& Parameters)
{
	// ТЗ §9's АЗС is "зал + подсобка + служебная" — three rooms in one 2200x1500
	// shell. That is two interior walls and two interior doorways, and both the
	// dividing and the passing have to be true at once.
	FSarkoBuilding Station = MakeShed();
	Station.Id = TEXT("test_station");
	Station.SizeUU = FVector2D(2200.f, 1500.f);
	Station.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 320.f));
	Station.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 600.f, 300.f));

	const float T = Station.WallThicknessUU;
	const float InnerY = Station.SizeUU.Y * 0.5f - T;

	// One wall across the depth at local x = -300, with a door in it.
	FSarkoBuildingInteriorWall Divider;
	Divider.From = FVector2D(-300.f, -InnerY);
	Divider.To = FVector2D(-300.f, InnerY);
	Divider.bHasDoor = true;
	Divider.Door = MakeDoor(ESarkoBuildingSide::North /* ignored for interior walls */, 200.f, 300.f);
	Station.InteriorWalls.Add(Divider);

	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a divided building expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Station, Blocks, Error));
	// 4 perimeter walls + 2 door splits + 2 interior segments = 8.
	TestEqual(TEXT("segment count accounts for every wall and every door"), Blocks.Num(), 8);

	// Both rooms are walkable...
	TestFalse(TEXT("the west room is walkable"), SarkoMap::IsPointInsideBlocksXY(FVector2D(-700.f, 0.f), Blocks));
	TestFalse(TEXT("the east room is walkable"), SarkoMap::IsPointInsideBlocksXY(FVector2D(500.f, 0.f), Blocks));

	// ...they are genuinely separated: a straight line between them crosses the
	// divider everywhere except at the doorway.
	TestTrue(TEXT("the divider is solid away from its door"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, -500.f), Blocks));
	TestTrue(TEXT("the divider is solid on the far side too"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, 600.f), Blocks));

	// ...and the interior doorway is a real, walkable gap of the declared width.
	TestEqual(TEXT("the interior doorway is completely open"),
		CountInside(Blocks, FVector2D(-300.f, 200.f - 150.f + 1.f), FVector2D(-300.f, 200.f + 150.f - 1.f), 60), 0);
	TestTrue(TEXT("the interior doorway is at least the minimum passage"),
		Station.InteriorWalls[0].Door.WidthUU >= SarkoMap::MinInteriorPassageUU);

	// A wall with no door is one unbroken segment — the closet/store-room case.
	FSarkoBuilding Solid = Station;
	Solid.InteriorWalls[0].bHasDoor = false;
	TArray<FSarkoCoverBlock> SolidBlocks;
	TestTrue(TEXT("a doorless divider expands"), SarkoMap::ExpandBuilding(Solid, SolidBlocks, Error));
	TestEqual(TEXT("a doorless divider is one segment"), SolidBlocks.Num(), 7);
	TestTrue(TEXT("a doorless divider is solid where the door used to be"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, 200.f), SolidBlocks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingExpansionRejectsBadGeometry,
	"Sarko.Map.BuildingExpansionRejectsBadGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingExpansionRejectsBadGeometry::RunTest(const FString& Parameters)
{
	// A building is hand-authored, so it will be wrong eventually, and every one
	// of these produces geometry that looks plausible in a screenshot and is
	// broken to play: a 200 uu door the pawn cannot fit through, a door hanging
	// off the end of a wall, a room with one exit, a 120 uu corridor.
	TArray<TPair<FString, FSarkoBuilding>> BadCases;

	{
		FSarkoBuilding B = MakeShed();
		B.Id.Reset();
		BadCases.Add({ TEXT("no id"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.SizeUU = FVector2D(400.f, 1500.f);
		BadCases.Add({ TEXT("footprint too small to stand in"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.WallHeightUU = 120.f;
		BadCases.Add({ TEXT("wall shorter than the pawn"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.WallThicknessUU = 0.f;
		BadCases.Add({ TEXT("zero wall thickness"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 320.f));
		BadCases.Add({ TEXT("exactly one exit"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 200.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		BadCases.Add({ TEXT("doorway narrower than 250 uu"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 950.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		BadCases.Add({ TEXT("doorway runs off the end of its wall"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -100.f, 400.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 100.f, 400.f));
		BadCases.Add({ TEXT("two doorways on one side overlap"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		// -350..-50 and -30..270: a 20 uu pier, thinner than the 30 uu wall it is
		// part of. The plan's original fixture (offsets -200 and 160) leaves 60 uu
		// between the openings, which is a legitimately narrow but real pier and
		// is correctly accepted; the numbers here are the case the label names.
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -200.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 120.f, 300.f));
		BadCases.Add({ TEXT("two doorways leave only a sliver of wall between them"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Diagonal;
		Diagonal.From = FVector2D(-400.f, -400.f);
		Diagonal.To = FVector2D(400.f, 400.f);
		B.InteriorWalls.Add(Diagonal);
		BadCases.Add({ TEXT("diagonal interior wall"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Outside;
		Outside.From = FVector2D(-400.f, -3000.f);
		Outside.To = FVector2D(-400.f, 3000.f);
		B.InteriorWalls.Add(Outside);
		BadCases.Add({ TEXT("interior wall reaches outside the footprint"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		// Its east face sits 105 uu from the inner face of the east wall: an
		// unreachable closet.
		FSarkoBuildingInteriorWall TooClose;
		TooClose.From = FVector2D(850.f, -700.f);
		TooClose.To = FVector2D(850.f, 700.f);
		B.InteriorWalls.Add(TooClose);
		BadCases.Add({ TEXT("interior passage narrower than 250 uu"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Divider;
		Divider.From = FVector2D(0.f, -700.f);
		Divider.To = FVector2D(0.f, 700.f);
		Divider.bHasDoor = true;
		Divider.Door = MakeDoor(ESarkoBuildingSide::North, 0.f, 180.f);
		B.InteriorWalls.Add(Divider);
		BadCases.Add({ TEXT("interior doorway narrower than 250 uu"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		// Two parallel dividers 200 uu apart: a corridor the pawn cannot use.
		// Not in the plan's list, but the pairwise rule it describes has no
		// other coverage, and an untested rule is a rule that might not run.
		FSarkoBuildingInteriorWall First;
		First.From = FVector2D(-300.f, -700.f);
		First.To = FVector2D(-300.f, 700.f);
		FSarkoBuildingInteriorWall Second;
		Second.From = FVector2D(-70.f, -700.f);
		Second.To = FVector2D(-70.f, 700.f);
		B.InteriorWalls.Add(First);
		B.InteriorWalls.Add(Second);
		BadCases.Add({ TEXT("two interior walls form a 200 uu corridor"), B });
	}

	for (const TPair<FString, FSarkoBuilding>& Case : BadCases)
	{
		TArray<FSarkoCoverBlock> Blocks;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ExpandBuilding(Case.Value, Blocks, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
		// A rejected building leaves nothing behind: a caller that ignores the
		// return value must not find half a shed in its array.
		TestEqual(FString::Printf(TEXT("nothing survives a rejection: %s"), *Case.Key), Blocks.Num(), 0);
	}

	// And the shapes that must be accepted, so the rules above cannot be
	// "correct" by rejecting everything: closed, and two doors on one side.
	{
		TArray<FSarkoCoverBlock> Blocks;
		FString Error;
		TestTrue(TEXT("a closed building is legal"), SarkoMap::ExpandBuilding(MakeShed(), Blocks, Error));

		FSarkoBuilding TwoOnASide = MakeShed();
		TwoOnASide.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -600.f, 300.f));
		TwoOnASide.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 600.f, 300.f));
		TestTrue(FString::Printf(TEXT("two well-separated doors on one side are legal: %s"), *Error),
			SarkoMap::ExpandBuilding(TwoOnASide, Blocks, Error));
		// Three segments on the north side, one each on the other three sides.
		TestEqual(TEXT("two doors on one side make three segments there, six in all"), Blocks.Num(), 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingYawRotatesTheWholeShell,
	"Sarko.Map.BuildingYawRotatesTheWholeShell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingYawRotatesTheWholeShell::RunTest(const FString& Parameters)
{
	// A yawed building must be the unyawed one turned, doors included. The bug
	// this guards is the one that only shows at non-zero angles: rotating a
	// wall's own orientation but not its offset, which shears the shell open.
	FSarkoBuilding Flat = MakeShed();
	Flat.Location = FVector(5000.f, -3000.f, 0.f);
	Flat.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 200.f, 300.f));
	Flat.Doors.Add(MakeDoor(ESarkoBuildingSide::West, -200.f, 300.f));

	FSarkoBuilding Turned = Flat;
	Turned.Yaw = 90.f;

	TArray<FSarkoCoverBlock> FlatBlocks;
	TArray<FSarkoCoverBlock> TurnedBlocks;
	FString Error;
	TestTrue(TEXT("the flat building expands"), SarkoMap::ExpandBuilding(Flat, FlatBlocks, Error));
	TestTrue(TEXT("the turned building expands"), SarkoMap::ExpandBuilding(Turned, TurnedBlocks, Error));
	TestEqual(TEXT("yaw changes nothing about how many walls there are"),
		TurnedBlocks.Num(), FlatBlocks.Num());

	const FRotator Rotation(0.f, 90.f, 0.f);
	for (int32 Index = 0; Index < FlatBlocks.Num(); ++Index)
	{
		const FVector ExpectedLocation = Flat.Location
			+ Rotation.RotateVector(FlatBlocks[Index].Location - Flat.Location);
		TestTrue(FString::Printf(TEXT("wall %d is where rotating it puts it"), Index),
			TurnedBlocks[Index].Location.Equals(ExpectedLocation, 0.05f));
		TestTrue(FString::Printf(TEXT("wall %d keeps its own dimensions"), Index),
			TurnedBlocks[Index].Extent.Equals(FlatBlocks[Index].Extent, 0.01f));
		// FRotator members are doubles in 5.8, so compare with Equals and a
		// tolerance rather than a float literal.
		TestTrue(FString::Printf(TEXT("wall %d is turned with the building"), Index),
			TurnedBlocks[Index].Rotation.Equals(
				FRotator(0.0, FlatBlocks[Index].Rotation.Yaw + 90.0, 0.0), 0.01f));
	}

	// And the doorway is still a doorway after the rotation: the east door at
	// local (+X) lands on the world +Y side once turned 90 degrees.
	const float WallX = Flat.SizeUU.X * 0.5f - Flat.WallThicknessUU * 0.5f;
	const FVector2D LocalDoorCentre(WallX, 200.f);
	const FVector Rotated = Rotation.RotateVector(FVector(LocalDoorCentre.X, LocalDoorCentre.Y, 0.f));
	TestFalse(TEXT("the doorway is still open after the building is turned"),
		SarkoMap::IsPointInsideBlocksXY(
			FVector2D(Flat.Location.X + Rotated.X, Flat.Location.Y + Rotated.Y), TurnedBlocks));

	// 90 degrees is the friendly angle: a shell that was rotated in the wrong
	// frame can still come out square. 45 cannot hide anything, so the sealed
	// walk from ClosedBuildingIsSealed is repeated there, in local coordinates
	// mapped through the same yaw.
	FSarkoBuilding Diagonal = MakeShed();
	Diagonal.Id = TEXT("test_shed_diagonal");
	Diagonal.Location = FVector(-1200.f, 900.f, 0.f);
	Diagonal.Yaw = 45.f;

	TArray<FSarkoCoverBlock> DiagonalBlocks;
	TestTrue(TEXT("a 45 degree building expands"), SarkoMap::ExpandBuilding(Diagonal, DiagonalBlocks, Error));
	TestEqual(TEXT("a 45 degree closed shell is still four walls"), DiagonalBlocks.Num(), 4);
	TestFalse(TEXT("the middle of a 45 degree building is walkable"),
		SarkoMap::IsPointInsideBlocksXY(LocalToWorld(Diagonal, FVector2D(0.f, 0.f)), DiagonalBlocks));

	const float DiagT = Diagonal.WallThicknessUU;
	const float DiagHalfX = Diagonal.SizeUU.X * 0.5f;
	const float DiagHalfY = Diagonal.SizeUU.Y * 0.5f;
	const float DiagWallX = DiagHalfX - DiagT * 0.5f;
	const float DiagWallY = DiagHalfY - DiagT * 0.5f;
	TestEqual(TEXT("the north wall has no gaps at 45 degrees"),
		CountInside(DiagonalBlocks,
			LocalToWorld(Diagonal, FVector2D(-DiagHalfX + 1.f, DiagWallY)),
			LocalToWorld(Diagonal, FVector2D(DiagHalfX - 1.f, DiagWallY)), 200), 201);
	TestEqual(TEXT("the south wall has no gaps at 45 degrees"),
		CountInside(DiagonalBlocks,
			LocalToWorld(Diagonal, FVector2D(-DiagHalfX + 1.f, -DiagWallY)),
			LocalToWorld(Diagonal, FVector2D(DiagHalfX - 1.f, -DiagWallY)), 200), 201);
	TestEqual(TEXT("the east wall has no gaps at 45 degrees"),
		CountInside(DiagonalBlocks,
			LocalToWorld(Diagonal, FVector2D(DiagWallX, -DiagHalfY + DiagT + 1.f)),
			LocalToWorld(Diagonal, FVector2D(DiagWallX, DiagHalfY - DiagT - 1.f)), 200), 201);
	TestEqual(TEXT("the west wall has no gaps at 45 degrees"),
		CountInside(DiagonalBlocks,
			LocalToWorld(Diagonal, FVector2D(-DiagWallX, -DiagHalfY + DiagT + 1.f)),
			LocalToWorld(Diagonal, FVector2D(-DiagWallX, DiagHalfY - DiagT - 1.f)), 200), 201);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingExpansionIsDeterministic,
	"Sarko.Map.BuildingExpansionIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingExpansionIsDeterministic::RunTest(const FString& Parameters)
{
	// The expander runs on every machine that loads the map, so identical input
	// must give byte-identical output in the same order — otherwise a wall is in
	// a different place on the server than on the client, which is a desync that
	// looks like a physics bug.
	FSarkoBuilding Building = MakeShed();
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -500.f, 320.f));
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 400.f, 300.f));
	FSarkoBuildingInteriorWall Divider;
	Divider.From = FVector2D(100.f, -720.f);
	Divider.To = FVector2D(100.f, 720.f);
	Divider.bHasDoor = true;
	Divider.Door = MakeDoor(ESarkoBuildingSide::North, -100.f, 300.f);
	Building.InteriorWalls.Add(Divider);

	TArray<FSarkoCoverBlock> First;
	TArray<FSarkoCoverBlock> Second;
	FString Error;
	TestTrue(TEXT("first expansion succeeds"), SarkoMap::ExpandBuilding(Building, First, Error));
	TestTrue(TEXT("second expansion succeeds"), SarkoMap::ExpandBuilding(Building, Second, Error));
	TestEqual(TEXT("the same building yields the same wall count"), Second.Num(), First.Num());
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("wall %d is identical"), Index),
			First[Index].Location.Equals(Second[Index].Location, 0.0001f) &&
			First[Index].Extent.Equals(Second[Index].Extent, 0.0001f));
		TestEqual(FString::Printf(TEXT("wall %d has the same id"), Index), First[Index].Id, Second[Index].Id);
	}

	// Ids are unique within a building, because Task 1 makes duplicate ids a
	// load error and a building that generates two identical wall ids would make
	// the whole map unloadable — from data that looks perfectly reasonable.
	TSet<FString> Ids;
	for (const FSarkoCoverBlock& Block : First)
	{
		TestFalse(FString::Printf(TEXT("wall id '%s' is unique"), *Block.Id), Ids.Contains(Block.Id));
		Ids.Add(Block.Id);
	}

	// ExpandBuildings resets rather than appends, and accumulates across
	// buildings in author order.
	FSarkoBuilding Second1 = MakeShed();
	Second1.Id = TEXT("test_shed_2");
	Second1.Location = FVector(8000.f, 0.f, 0.f);
	TArray<FSarkoCoverBlock> Many;
	Many.Add(FSarkoCoverBlock()); // must be discarded
	TestTrue(TEXT("expanding a list succeeds"),
		SarkoMap::ExpandBuildings({ Building, Second1 }, Many, Error));
	TestEqual(TEXT("the list expansion resets its output and sums the parts"),
		Many.Num(), First.Num() + 4);

	// Ids stay unique ACROSS buildings too, which is what Task 1's CollectIds
	// checks over the whole definition: the building's own id is the prefix, so
	// two buildings can only collide if their ids do, and that is already an
	// error one level up.
	TSet<FString> ManyIds;
	for (const FSarkoCoverBlock& Block : Many)
	{
		TestFalse(FString::Printf(TEXT("wall id '%s' is unique across buildings"), *Block.Id),
			ManyIds.Contains(Block.Id));
		ManyIds.Add(Block.Id);
	}

	// A rejected building in the middle of a list takes the whole list down and
	// leaves nothing behind: a partly-expanded map is worse than none, because
	// half a building looks like a building.
	FSarkoBuilding Broken = MakeShed();
	Broken.Id = TEXT("test_shed_broken");
	Broken.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 0.f, 300.f));
	TArray<FSarkoCoverBlock> None;
	TestFalse(TEXT("one bad building fails the whole list"),
		SarkoMap::ExpandBuildings({ Second1, Broken }, None, Error));
	TestEqual(TEXT("a failed list expansion leaves nothing behind"), None.Num(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
