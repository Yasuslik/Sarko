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

	/**
	 * No wall overlaps another wall, over every pair.
	 *
	 * A free function rather than three copies of a nested loop, because that is
	 * exactly how this invariant came to be asserted for the doorless unrotated
	 * shed and for nothing else — while the shipped map's АЗС quietly intersected
	 * its own divider over 15x30 uu. Overlapping walls are not a rendering
	 * nuisance: they make "how much wall did we emit" and "how much wall is there"
	 * different numbers, which is where a missing segment hides.
	 *
	 * Uses SarkoMap::BlocksOverlapXY — the expander's own predicate, which honours
	 * yaw — so the same check runs at 0 and at 45 degrees. The axis-aligned
	 * comparison this replaces could not see a rotated building at all.
	 */
	void TestNoWallOverlaps(FAutomationTestBase& Test, const TArray<FSarkoCoverBlock>& Blocks, const TCHAR* What)
	{
		for (int32 A = 0; A < Blocks.Num(); ++A)
		{
			for (int32 B = A + 1; B < Blocks.Num(); ++B)
			{
				Test.TestFalse(
					FString::Printf(TEXT("%s: '%s' and '%s' do not overlap"), What, *Blocks[A].Id, *Blocks[B].Id),
					SarkoMap::BlocksOverlapXY(Blocks[A], Blocks[B]));
			}
		}
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
	// First, the predicate the no-overlap assertions rest on, in the direction that
	// cannot be verified by any of them: a pair that DOES overlap. Every other use
	// of BlocksOverlapXY in this file and in BridgeMapTest asserts a false, so a
	// predicate that always returned false would make all of them pass — including
	// on the shipped map, whose АЗС was the reason the check was written.
	{
		FSarkoCoverBlock First;
		First.Location = FVector(0.f, 0.f, 175.f);
		First.Extent = FVector(200.f, 15.f, 175.f);
		FSarkoCoverBlock Second = First;
		// End to end, 5 uu into each other: the exact shape of the АЗС's defect,
		// which was a 15 uu intrusion.
		Second.Location = FVector(395.f, 0.f, 175.f);
		TestTrue(TEXT("two walls overlapping by 5 uu are reported as overlapping"),
			SarkoMap::BlocksOverlapXY(First, Second));
		Second.Location = FVector(415.f, 0.f, 175.f);
		TestFalse(TEXT("two walls 15 uu apart are not"), SarkoMap::BlocksOverlapXY(First, Second));
		Second.Location = FVector(400.f, 0.f, 175.f);
		TestFalse(TEXT("two walls that butt face to face are not"), SarkoMap::BlocksOverlapXY(First, Second));
		// And a thin box turned across another, which is what an axis-aligned
		// comparison cannot answer: it crosses, and it shares no world-axis interval
		// wider than the boxes' own extents.
		Second.Rotation = FRotator(0.f, 90.f, 0.f);
		Second.Location = FVector(100.f, 0.f, 175.f);
		TestTrue(TEXT("a wall crossing another at 90 degrees is reported as overlapping"),
			SarkoMap::BlocksOverlapXY(First, Second));
		Second.Location = FVector(100.f, 300.f, 175.f);
		TestFalse(TEXT("the same turned wall moved clear of it is not"),
			SarkoMap::BlocksOverlapXY(First, Second));
	}

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

	TestNoWallOverlaps(*this, Blocks, TEXT("closed shed"));
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

	// The invariant that used to be asserted for the doorless unrotated shed only,
	// which is how the shipped АЗС's own two interior walls came to intersect.
	TestNoWallOverlaps(*this, Blocks, TEXT("divided station"));

	// A second interior wall butting into the first, which is the АЗС's real shape:
	// зал | подсобка across the depth, then служебная off the подсобка. Its east end
	// is authored ON the divider's centre line, because that is how a person draws
	// "it meets that wall" — and the expander trims it to the divider's near face
	// so the two butt instead of overlapping.
	FSarkoBuilding Tee = Station;
	// The divider's own doorway is moved south of where the служебная wall lands,
	// exactly as bridge.json's АЗС does it: a wall that ends inside another wall's
	// doorway ends in mid-air, and no rule here is about that.
	Tee.InteriorWalls[0].Door.OffsetUU = -400.f;
	FSarkoBuildingInteriorWall Service;
	Service.From = FVector2D(-300.f, 200.f);
	Service.To = FVector2D(-1070.f, 200.f);
	Service.bHasDoor = true;
	Service.Door = MakeDoor(ESarkoBuildingSide::North /* ignored */, 0.f, 300.f);
	Tee.InteriorWalls.Add(Service);

	TArray<FSarkoCoverBlock> TeeBlocks;
	TestTrue(FString::Printf(TEXT("a tee of two interior walls expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Tee, TeeBlocks, Error));
	TestNoWallOverlaps(*this, TeeBlocks, TEXT("teed station"));
	// The trim is half a thickness and no more: the wall stops at x = -315, the
	// divider occupies -315..-285, so there is no overlap AND no hairline gap
	// between them. A test for "they do not overlap" alone would pass a wall that
	// had been trimmed a metre short.
	TestTrue(TEXT("the trimmed wall still meets the divider it butts into"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-320.f, 200.f), TeeBlocks));
	TestTrue(TEXT("and its own far end still meets the perimeter"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-1065.f, 200.f), TeeBlocks));

	// A wall with no door: legal only when both sides of it can still be reached,
	// so the west room gets its own perimeter doorway. Without one this is the
	// sealed-room case Sarko.Map.BuildingRejectsUnreachableRooms rejects — which is
	// the whole point: "solid divider" is a shape, not a licence to wall off a
	// store room nobody can enter.
	FSarkoBuilding Solid = Station;
	Solid.InteriorWalls[0].bHasDoor = false;
	Solid.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
	TArray<FSarkoCoverBlock> SolidBlocks;
	TestTrue(FString::Printf(TEXT("a doorless divider with a door into each room expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Solid, SolidBlocks, Error));
	// 4 perimeter walls, split by three doorways, plus one unbroken divider.
	TestEqual(TEXT("a doorless divider is one segment"), SolidBlocks.Num(), 8);
	TestTrue(TEXT("a doorless divider is solid where the door used to be"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, 200.f), SolidBlocks));
	TestNoWallOverlaps(*this, SolidBlocks, TEXT("station with a solid divider"));
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
		// The case the old rule accepted, and the reason the rule is no longer "one
		// wall thickness": offsets -200 and +130 leave a pier of EXACTLY 30 uu, so
		// the expander emitted a 30x30x350 pillar with a doorway on either side of
		// it — free-standing geometry that ClosedBuildingIsSealed already declares
		// is not a wall, from data that passed every check.
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -200.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 130.f, 300.f));
		BadCases.Add({ TEXT("two doorways leave a pier of exactly one wall thickness"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		// And the same defect at the END of a wall: a door running to -970 on a wall
		// that ends at -1000 left a 30x30 corner post. Exactly one thickness, so the
		// old rule said yes.
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -800.f, 340.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		BadCases.Add({ TEXT("a doorway leaves a corner post of exactly one wall thickness"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		// Two solid walls meeting near a corner. Each one passes every rule there
		// is — inside the footprint, long enough, 555 uu clear of the perimeter and
		// of each other — and together they seal a ~285x385 closet that no doorway
		// reaches. This is the case that argues for a flood fill instead of a third
		// narrow rule: nothing local is wrong here, only the shape.
		FSarkoBuildingInteriorWall AlongY;
		AlongY.From = FVector2D(670.f, 320.f);
		AlongY.To = FVector2D(670.f, 720.f);
		FSarkoBuildingInteriorWall AlongX;
		AlongX.From = FVector2D(670.f, 320.f);
		AlongX.To = FVector2D(970.f, 320.f);
		B.InteriorWalls.Add(AlongY);
		B.InteriorWalls.Add(AlongX);
		BadCases.Add({ TEXT("two corner walls seal an unreachable closet"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		// A doorless divider across the whole depth, with both perimeter doorways on
		// the same side of it: the west half of the building is a sealed box. The
		// expander used to emit this happily, and Stage C authoring a container into
		// that half would produce loot nobody can ever reach.
		FSarkoBuildingInteriorWall Sealed;
		Sealed.From = FVector2D(-300.f, -720.f);
		Sealed.To = FVector2D(-300.f, 720.f);
		B.InteriorWalls.Add(Sealed);
		BadCases.Add({ TEXT("a doorless divider seals half the building"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		// An interior wall that runs straight through a perimeter doorway. The
		// doorway is a real gap, the wall is a legal wall, and walking in puts the
		// player nose-first into it. No per-wall rule can see this either.
		FSarkoBuildingInteriorWall AcrossTheDoor;
		AcrossTheDoor.From = FVector2D(0.f, -720.f);
		AcrossTheDoor.To = FVector2D(0.f, 720.f);
		AcrossTheDoor.bHasDoor = true;
		AcrossTheDoor.Door = MakeDoor(ESarkoBuildingSide::North, 0.f, 300.f);
		B.InteriorWalls.Add(AcrossTheDoor);
		BadCases.Add({ TEXT("an interior wall stands in a perimeter doorway"), B });
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

	// The two exactly-one-thickness rows above are the ones the old rule accepted,
	// so it matters that the STUB rule is what refuses them and not some other
	// check that happens to trip over the same offsets. Named explicitly, because a
	// row in the table above passes on any rejection at all.
	{
		FSarkoBuilding Pier = MakeShed();
		Pier.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -200.f, 300.f));
		Pier.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 130.f, 300.f));
		TArray<FSarkoCoverBlock> Blocks;
		FString Error;
		TestFalse(TEXT("a 30 uu pier is refused"), SarkoMap::ExpandBuilding(Pier, Blocks, Error));
		TestTrue(FString::Printf(TEXT("and refused for being a floating post: %s"), *Error),
			Error.Contains(TEXT("floating post")));

		FSarkoBuilding Post = MakeShed();
		Post.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -800.f, 340.f));
		Post.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		TestFalse(TEXT("a 30 uu corner post is refused"), SarkoMap::ExpandBuilding(Post, Blocks, Error));
		TestTrue(FString::Printf(TEXT("and refused for being a splinter: %s"), *Error),
			Error.Contains(TEXT("splinter")));

		// 120 uu is the line, so a pier just over it is still legal — the rule is a
		// minimum and not a ban on narrow piers.
		FSarkoBuilding NarrowButLegal = MakeShed();
		NarrowButLegal.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -285.f, 300.f));
		NarrowButLegal.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 285.f, 300.f));
		TestTrue(FString::Printf(TEXT("a 270 uu pier is legal: %s"), *Error),
			SarkoMap::ExpandBuilding(NarrowButLegal, Blocks, Error));
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

	// And no wall overlaps another at 45 degrees either. This is the assertion that
	// could not previously exist: the axis-aligned comparison it replaces compares
	// world-axis extents, and every wall of a 45-degree building overlaps every
	// other one on both world axes, so the old check reported the whole shell as
	// broken. SarkoMap::BlocksOverlapXY honours the yaw, so the invariant is now
	// the same invariant at every angle.
	TestNoWallOverlaps(*this, DiagonalBlocks, TEXT("45 degree shed"));
	TestNoWallOverlaps(*this, TurnedBlocks, TEXT("90 degree shed with doors"));

	// A yawed building with interior walls, because the trimming is done in the
	// local frame and this is what proves the rotation does not undo it.
	FSarkoBuilding TurnedRooms = MakeShed();
	TurnedRooms.Id = TEXT("test_shed_turned_rooms");
	TurnedRooms.SizeUU = FVector2D(2200.f, 1500.f);
	TurnedRooms.Yaw = 30.f;
	TurnedRooms.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 320.f));
	TurnedRooms.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 600.f, 300.f));
	FSarkoBuildingInteriorWall TurnedDivider;
	TurnedDivider.From = FVector2D(-300.f, -720.f);
	TurnedDivider.To = FVector2D(-300.f, 720.f);
	TurnedDivider.bHasDoor = true;
	TurnedDivider.Door = MakeDoor(ESarkoBuildingSide::North, -400.f, 320.f);
	FSarkoBuildingInteriorWall TurnedService;
	TurnedService.From = FVector2D(-300.f, 200.f);
	TurnedService.To = FVector2D(-1070.f, 200.f);
	TurnedService.bHasDoor = true;
	TurnedService.Door = MakeDoor(ESarkoBuildingSide::North, 0.f, 300.f);
	TurnedRooms.InteriorWalls.Add(TurnedDivider);
	TurnedRooms.InteriorWalls.Add(TurnedService);

	TArray<FSarkoCoverBlock> TurnedRoomBlocks;
	TestTrue(FString::Printf(TEXT("a yawed building with two interior walls expands: %s"), *Error),
		SarkoMap::ExpandBuilding(TurnedRooms, TurnedRoomBlocks, Error));
	TestNoWallOverlaps(*this, TurnedRoomBlocks, TEXT("30 degree three-room building"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingRejectsUnreachableRooms,
	"Sarko.Map.BuildingRejectsUnreachableRooms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingRejectsUnreachableRooms::RunTest(const FString& Parameters)
{
	// The sealed-closet case, authored exactly: two solid walls near the north-east
	// corner, one along Y at x = InnerX-300 spanning y = InnerY-400..InnerY, one
	// along X at y = InnerY-400 spanning x = InnerX-300..InnerX. Every existing
	// rule says yes to both — they are inside the footprint, longer than they are
	// thick, and 285 and 385 uu clear of the perimeter, well past the 250 uu
	// passage minimum. Together they box off a ~285x385 uu room that neither
	// doorway reaches, which is where Stage C would put a container and where the
	// player would never find one.
	//
	// The rejection has its own test, rather than only a row in the bad-case table,
	// because what matters is WHICH rule fires: a table row passes if the building
	// is refused for any reason at all, and a fill that never runs would still let
	// the table go green the day someone tightens an unrelated clearance.
	const float T = 30.f;
	FSarkoBuilding Building = MakeShed();
	Building.Id = TEXT("test_sealed_closet");
	Building.WallThicknessUU = T;
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));

	const float InnerX = Building.SizeUU.X * 0.5f - T;
	const float InnerY = Building.SizeUU.Y * 0.5f - T;

	FSarkoBuildingInteriorWall AlongY;
	AlongY.From = FVector2D(InnerX - 300.f, InnerY - 400.f);
	AlongY.To = FVector2D(InnerX - 300.f, InnerY);
	FSarkoBuildingInteriorWall AlongX;
	AlongX.From = FVector2D(InnerX - 300.f, InnerY - 400.f);
	AlongX.To = FVector2D(InnerX, InnerY - 400.f);
	Building.InteriorWalls.Add(AlongY);
	Building.InteriorWalls.Add(AlongX);

	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestFalse(TEXT("a building with a sealed closet is refused"),
		SarkoMap::ExpandBuilding(Building, Blocks, Error));
	TestEqual(TEXT("nothing survives the rejection"), Blocks.Num(), 0);
	// The reachability check is the one that must fire, not a clearance rule that
	// happens to trip over the same numbers.
	TestTrue(FString::Printf(TEXT("the error names the sealed region: %s"), *Error),
		Error.Contains(TEXT("seal")) && Error.Contains(TEXT("no doorway reaches")));

	// The same corner, one doorway cut into it, must be LEGAL — otherwise the check
	// above would be satisfied by a rule that refuses every building with two
	// interior walls, and Stage C could not author a back room at all. The closet is
	// 600 uu here rather than the 285x385 above for an ordinary reason: a wall needs
	// room for a 250 uu opening plus a stub at each end, so a 285 uu wall cannot
	// hold a door and the smaller shape can only ever be sealed.
	FSarkoBuilding Opened = MakeShed();
	Opened.Id = TEXT("test_back_room");
	Opened.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
	Opened.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
	FSarkoBuildingInteriorWall OpenSide;
	OpenSide.From = FVector2D(InnerX - 600.f, InnerY - 600.f);
	OpenSide.To = FVector2D(InnerX - 600.f, InnerY);
	OpenSide.bHasDoor = true;
	OpenSide.Door = MakeDoor(ESarkoBuildingSide::North /* ignored */, 0.f, 300.f);
	FSarkoBuildingInteriorWall ClosedSide;
	ClosedSide.From = FVector2D(InnerX - 600.f, InnerY - 600.f);
	ClosedSide.To = FVector2D(InnerX, InnerY - 600.f);
	Opened.InteriorWalls.Add(OpenSide);
	Opened.InteriorWalls.Add(ClosedSide);

	TArray<FSarkoCoverBlock> OpenedBlocks;
	TestTrue(FString::Printf(TEXT("the same corner with a doorway into it is legal: %s"), *Error),
		SarkoMap::ExpandBuilding(Opened, OpenedBlocks, Error));
	TestNoWallOverlaps(*this, OpenedBlocks, TEXT("back room with a doorway"));
	// And the back room really is a room: solid where the walls are, open where the
	// doorway is. A fill that accepted this because the walls vanished would pass
	// the assertion above.
	TestTrue(TEXT("the back room's dividing wall is solid away from its doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(InnerX - 600.f, InnerY - 50.f), OpenedBlocks));
	TestFalse(TEXT("the back room's doorway is open"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(InnerX - 600.f, InnerY - 300.f), OpenedBlocks));

	// MinSealedRoomUU has no negative fixture on purpose, and the reason is worth
	// writing down: with MinInteriorPassageUU forcing every interior wall 250 uu
	// clear of any parallel face, the smallest pocket that can be enclosed at all
	// is already wider than the threshold on both axes. It is a floor for the day
	// that passage rule changes, not a case today's schema can reach.
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
