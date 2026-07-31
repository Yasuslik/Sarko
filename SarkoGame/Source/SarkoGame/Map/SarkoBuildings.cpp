#include "Map/SarkoBuildings.h"

namespace
{
	/** A doorway projected onto its wall's own axis: the interval to remove. */
	struct FGap
	{
		float Min = 0.f;
		float Max = 0.f;
	};

	/**
	 * Splits a wall's span into the segments left over once the gaps are cut out.
	 *
	 * The gaps arrive sorted and validated, so this is arithmetic and cannot
	 * fail; every rule about where a door may be lives in ValidateGaps below,
	 * where it can produce an error message naming the door.
	 */
	void SegmentsBetweenGaps(float SpanMin, float SpanMax, const TArray<FGap>& Gaps, TArray<FGap>& OutSegments)
	{
		float Cursor = SpanMin;
		for (const FGap& Gap : Gaps)
		{
			if (Gap.Min - Cursor > KINDA_SMALL_NUMBER)
			{
				OutSegments.Add({ Cursor, Gap.Min });
			}
			Cursor = Gap.Max;
		}
		if (SpanMax - Cursor > KINDA_SMALL_NUMBER)
		{
			OutSegments.Add({ Cursor, SpanMax });
		}
	}

	/**
	 * Every rule about doorways on one wall, in one place.
	 *
	 * The minimum surviving stub is FMath::Max(Thickness, MinWallStubUU) — see
	 * SarkoMap::MinWallStubUU for why one thickness is not enough. A zero-length
	 * stub would also emit a degenerate block that the map parser rejects (extent
	 * components must be positive), turning an authoring slip into "the whole map
	 * failed to load"; both ends of that range are covered by one number.
	 */
	bool ValidateGaps(const FString& BuildingId, const TCHAR* WallName, float SpanMin, float SpanMax,
		float Thickness, TArray<FGap>& Gaps, FString& OutError)
	{
		Gaps.Sort([](const FGap& A, const FGap& B) { return A.Min < B.Min; });

		const float MinStub = FMath::Max(Thickness, SarkoMap::MinWallStubUU);

		for (int32 Index = 0; Index < Gaps.Num(); ++Index)
		{
			const FGap& Gap = Gaps[Index];
			if (Gap.Min < SpanMin + MinStub - KINDA_SMALL_NUMBER ||
				Gap.Max > SpanMax - MinStub + KINDA_SMALL_NUMBER)
			{
				OutError = FString::Printf(
					TEXT("building '%s': a doorway on the %s wall runs to %.1f..%.1f, which leaves less than %.1f uu of wall at an end (the wall spans %.1f..%.1f); a shorter stub is a free-standing splinter, not a wall"),
					*BuildingId, WallName, Gap.Min, Gap.Max, MinStub, SpanMin, SpanMax);
				return false;
			}
			if (Index > 0)
			{
				const float Between = Gap.Min - Gaps[Index - 1].Max;
				if (Between < MinStub - KINDA_SMALL_NUMBER)
				{
					OutError = Between < 0.f
						? FString::Printf(
							TEXT("building '%s': two doorways on the %s wall overlap by %.1f uu"),
							*BuildingId, WallName, -Between)
						: FString::Printf(
							TEXT("building '%s': two doorways on the %s wall leave only %.1f uu of wall between them; a pier must be at least %.1f uu wide or it reads as a floating post"),
							*BuildingId, WallName, Between, MinStub);
					return false;
				}
			}
		}
		return true;
	}

	/** One wall segment, as a block, in the building's local frame. */
	FSarkoCoverBlock MakeWall(const FSarkoBuilding& Building, const FString& Id,
		const FVector2D& LocalCentre, const FVector2D& LocalHalfExtent)
	{
		FSarkoCoverBlock Block;
		Block.Id = Id;
		Block.Surface = Building.Surface;
		Block.bBlocksMovement = true;
		Block.Extent = FVector(LocalHalfExtent.X, LocalHalfExtent.Y, Building.WallHeightUU * 0.5f);

		// Local -> world: rotate the offset by the building's yaw, then translate.
		// Rotating the offset AND the block is both halves of the job; doing only
		// the second shears the shell open at every angle except zero.
		const FRotator Rotation(0.f, Building.Yaw, 0.f);
		const FVector Rotated = Rotation.RotateVector(FVector(LocalCentre.X, LocalCentre.Y, 0.f));
		Block.Location = Building.Location + Rotated + FVector(0.f, 0.f, Building.WallHeightUU * 0.5f);
		Block.Rotation = Rotation;
		return Block;
	}

	/** An emitted interior wall segment's footprint in the building's local frame. */
	struct FLocalRect
	{
		float MinX = 0.f;
		float MaxX = 0.f;
		float MinY = 0.f;
		float MaxY = 0.f;

		bool Contains(float X, float Y) const
		{
			// Inclusive on every edge: a grid sample that lands exactly on a wall's
			// face must count as blocked, or the fill leaks along the seam.
			return X >= MinX - KINDA_SMALL_NUMBER && X <= MaxX + KINDA_SMALL_NUMBER
				&& Y >= MinY - KINDA_SMALL_NUMBER && Y <= MaxY + KINDA_SMALL_NUMBER;
		}
	};

	/** A place a pawn provably stands: the floor just inside one perimeter doorway. */
	struct FDoorSeed
	{
		FVector2D Local = FVector2D::ZeroVector;
		const TCHAR* WallName = nullptr;
	};

	/**
	 * Whether every part of the interior can be walked to from a perimeter doorway.
	 *
	 * This is the one rule that is about the SHAPE rather than about any single
	 * wall, and it is deliberately one rule rather than three. "No wall near a
	 * corner boxes off a triangle", "no doorless divider seals a room", "no pair of
	 * walls makes a courtyard" are all the same defect — an enclosed pocket — and
	 * each of them individually is a rule Stage C can route around with a shape
	 * nobody thought of. A flood fill cannot be routed around: either the pocket
	 * connects to a doorway or it does not.
	 *
	 * Method: sample the interior rectangle (inside the perimeter's inner faces) on
	 * a grid of at most T/2, mark every cell whose centre is inside an interior
	 * wall, flood 4-connected from each doorway, then look at what is left. A wall
	 * is T thick and the grid is at most T/2, so a wall's cross-section always
	 * contains at least two rows of blocked cells and the fill can never slip
	 * through one; 4-connectivity means it cannot slip through a corner either.
	 *
	 * Two things it deliberately does not do. It does not model the pawn's radius —
	 * a gap of one cell counts as open, and "wide enough to walk through" is
	 * MinInteriorPassageUU's job on the walls and MinDoorwayUU's on the doorways.
	 * And it says nothing about a CLOSED building: with no doorways there is
	 * nothing to seed from and nothing to reach, which is why the caller skips it.
	 *
	 * Cost is one byte and a handful of float compares per cell: the largest
	 * building on the shipped map is 2200x1500, which is 142x96 = 13,632 cells,
	 * and only buildings that actually have interior walls are checked at all.
	 */
	bool InteriorIsReachable(const FString& BuildingId, float InnerX, float InnerY, float Thickness,
		const TArray<FLocalRect>& Walls, const TArray<FDoorSeed>& Seeds, FString& OutError)
	{
		// Cells tile the interior exactly, so no sample centre ever falls outside
		// it. That matters: a cell centre beyond an inner face would sit inside a
		// perimeter wall, which this grid does not know about, and the fill would
		// happily walk around the end of an interior wall that butts into it.
		const float Step = Thickness * 0.5f;
		const int32 NumX = FMath::Max(1, FMath::FloorToInt(2.f * InnerX / Step));
		const int32 NumY = FMath::Max(1, FMath::FloorToInt(2.f * InnerY / Step));
		const float StepX = 2.f * InnerX / static_cast<float>(NumX);
		const float StepY = 2.f * InnerY / static_cast<float>(NumY);
		const auto CentreX = [=](int32 I) { return -InnerX + (static_cast<float>(I) + 0.5f) * StepX; };
		const auto CentreY = [=](int32 J) { return -InnerY + (static_cast<float>(J) + 0.5f) * StepY; };

		enum : uint8 { Free = 0, Solid = 1, Reached = 2, Counted = 3 };
		TArray<uint8> Cells;
		Cells.Init(Free, NumX * NumY);
		for (int32 J = 0; J < NumY; ++J)
		{
			for (int32 I = 0; I < NumX; ++I)
			{
				for (const FLocalRect& Rect : Walls)
				{
					if (Rect.Contains(CentreX(I), CentreY(J)))
					{
						Cells[J * NumX + I] = Solid;
						break;
					}
				}
			}
		}

		TArray<int32> Frontier;
		for (const FDoorSeed& Seed : Seeds)
		{
			const int32 I = FMath::Clamp(FMath::FloorToInt((Seed.Local.X + InnerX) / StepX), 0, NumX - 1);
			const int32 J = FMath::Clamp(FMath::FloorToInt((Seed.Local.Y + InnerY) / StepY), 0, NumY - 1);
			const int32 Index = J * NumX + I;
			if (Cells[Index] == Solid)
			{
				// The doorway opens onto a wall. Nothing else in the expander can
				// see this: the doorway is a real gap and the wall is a legal wall,
				// they simply cross.
				OutError = FString::Printf(
					TEXT("building '%s': an interior wall stands in the %s doorway, so the doorway opens onto a wall"),
					*BuildingId, Seed.WallName);
				return false;
			}
			if (Cells[Index] == Free)
			{
				Cells[Index] = Reached;
				Frontier.Add(Index);
			}
		}

		while (Frontier.Num() > 0)
		{
			const int32 Index = Frontier.Pop(EAllowShrinking::No);
			const int32 I = Index % NumX;
			const int32 J = Index / NumX;
			const int32 Neighbours[4] = {
				I > 0 ? Index - 1 : INDEX_NONE,
				I < NumX - 1 ? Index + 1 : INDEX_NONE,
				J > 0 ? Index - NumX : INDEX_NONE,
				J < NumY - 1 ? Index + NumX : INDEX_NONE,
			};
			for (const int32 Neighbour : Neighbours)
			{
				if (Neighbour != INDEX_NONE && Cells[Neighbour] == Free)
				{
					Cells[Neighbour] = Reached;
					Frontier.Add(Neighbour);
				}
			}
		}

		// What is left is one or more enclosed pockets. Measured, not merely
		// counted: a one-cell-wide sliver between two walls is geometry and a
		// 285x385 closet is a room, and the difference is the bounding box.
		for (int32 Start = 0; Start < Cells.Num(); ++Start)
		{
			if (Cells[Start] != Free)
			{
				continue;
			}
			int32 MinI = NumX;
			int32 MaxI = -1;
			int32 MinJ = NumY;
			int32 MaxJ = -1;
			Cells[Start] = Counted;
			Frontier.Reset();
			Frontier.Add(Start);
			while (Frontier.Num() > 0)
			{
				const int32 Index = Frontier.Pop(EAllowShrinking::No);
				const int32 I = Index % NumX;
				const int32 J = Index / NumX;
				MinI = FMath::Min(MinI, I);
				MaxI = FMath::Max(MaxI, I);
				MinJ = FMath::Min(MinJ, J);
				MaxJ = FMath::Max(MaxJ, J);
				const int32 Neighbours[4] = {
					I > 0 ? Index - 1 : INDEX_NONE,
					I < NumX - 1 ? Index + 1 : INDEX_NONE,
					J > 0 ? Index - NumX : INDEX_NONE,
					J < NumY - 1 ? Index + NumX : INDEX_NONE,
				};
				for (const int32 Neighbour : Neighbours)
				{
					if (Neighbour != INDEX_NONE && Cells[Neighbour] == Free)
					{
						Cells[Neighbour] = Counted;
						Frontier.Add(Neighbour);
					}
				}
			}
			const float Width = static_cast<float>(MaxI - MinI + 1) * StepX;
			const float Depth = static_cast<float>(MaxJ - MinJ + 1) * StepY;
			if (Width >= SarkoMap::MinSealedRoomUU && Depth >= SarkoMap::MinSealedRoomUU)
			{
				OutError = FString::Printf(
					TEXT("building '%s': the interior walls seal off a %.0fx%.0f uu region around local (%.0f..%.0f, %.0f..%.0f) that no doorway reaches; a room the player cannot walk into is loot nobody can pick up"),
					*BuildingId, Width, Depth,
					CentreX(MinI) - StepX * 0.5f, CentreX(MaxI) + StepX * 0.5f,
					CentreY(MinJ) - StepY * 0.5f, CentreY(MaxJ) + StepY * 0.5f);
				return false;
			}
		}
		return true;
	}
}

bool SarkoMap::BlocksOverlapXY(const FSarkoCoverBlock& A, const FSarkoCoverBlock& B, float Slack)
{
	const FVector2D Delta(B.Location.X - A.Location.X, B.Location.Y - A.Location.Y);

	// One face normal of one box, against both boxes' half-extents projected onto
	// it. Four of these — two normals each — decide a rectangle pair exactly.
	const auto Separates = [&Delta](const FSarkoCoverBlock& Owner, const FSarkoCoverBlock& Other, bool bOwnerXAxis, float InSlack)
	{
		const FVector AxisVector = Owner.Rotation.RotateVector(bOwnerXAxis ? FVector::XAxisVector : FVector::YAxisVector);
		const FVector2D Axis(AxisVector.X, AxisVector.Y);
		const FVector OtherX = Other.Rotation.RotateVector(FVector::XAxisVector);
		const FVector OtherY = Other.Rotation.RotateVector(FVector::YAxisVector);
		const double Reach = FMath::Abs(FVector2D::DotProduct(FVector2D(OtherX.X, OtherX.Y), Axis)) * Other.Extent.X
			+ FMath::Abs(FVector2D::DotProduct(FVector2D(OtherY.X, OtherY.Y), Axis)) * Other.Extent.Y
			+ (bOwnerXAxis ? Owner.Extent.X : Owner.Extent.Y);
		return FMath::Abs(FVector2D::DotProduct(Delta, Axis)) >= Reach - InSlack;
	};

	return !(Separates(A, B, true, Slack) || Separates(A, B, false, Slack)
		|| Separates(B, A, true, Slack) || Separates(B, A, false, Slack));
}

bool SarkoMap::ParseBuildingSide(const FString& Name, ESarkoBuildingSide& Out)
{
	if (Name.Equals(TEXT("N"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("north"), ESearchCase::IgnoreCase)) { Out = ESarkoBuildingSide::North; return true; }
	if (Name.Equals(TEXT("E"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("east"), ESearchCase::IgnoreCase))  { Out = ESarkoBuildingSide::East;  return true; }
	if (Name.Equals(TEXT("S"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("south"), ESearchCase::IgnoreCase)) { Out = ESarkoBuildingSide::South; return true; }
	if (Name.Equals(TEXT("W"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("west"), ESearchCase::IgnoreCase))  { Out = ESarkoBuildingSide::West;  return true; }
	return false;
}

bool SarkoMap::IsPointInsideBlocksXY(const FVector2D& Point, const TArray<FSarkoCoverBlock>& Blocks)
{
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		const FVector Delta(Point.X - Block.Location.X, Point.Y - Block.Location.Y, 0.f);
		const FVector Local = Block.Rotation.UnrotateVector(Delta);
		if (FMath::Abs(Local.X) <= Block.Extent.X && FMath::Abs(Local.Y) <= Block.Extent.Y)
		{
			return true;
		}
	}
	return false;
}

bool SarkoMap::ExpandBuilding(const FSarkoBuilding& Building, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)
{
	OutBlocks.Reset();
	OutError.Reset();

	const FString& Id = Building.Id;
	if (Id.IsEmpty())
	{
		OutError = TEXT("a building has no 'id'; every building must be named (ТЗ §18)");
		return false;
	}

	const float T = Building.WallThicknessUU;
	const float H = Building.WallHeightUU;
	const float HalfX = Building.SizeUU.X * 0.5f;
	const float HalfY = Building.SizeUU.Y * 0.5f;

	if (T < 10.f || T > 200.f)
	{
		OutError = FString::Printf(TEXT("building '%s': wallThickness %.1f is outside 10..200 uu"), *Id, T);
		return false;
	}
	if (H < MinWallHeightUU || H > 800.f)
	{
		OutError = FString::Printf(
			TEXT("building '%s': wallHeight %.1f is outside %.0f..800 uu — below %.0f it does not break the line of sight of a 176 uu pawn"),
			*Id, H, MinWallHeightUU, MinWallHeightUU);
		return false;
	}
	// A room the pawn cannot stand and turn in is not a room. 500 uu of clear
	// interior on each axis is the floor: a 250 uu passage plus something to
	// pass to.
	if (Building.SizeUU.X - 2.f * T < 500.f || Building.SizeUU.Y - 2.f * T < 500.f)
	{
		OutError = FString::Printf(
			TEXT("building '%s': footprint %.0fx%.0f with %.0f uu walls leaves %.0fx%.0f of interior; each axis needs at least 500 uu"),
			*Id, Building.SizeUU.X, Building.SizeUU.Y, T,
			Building.SizeUU.X - 2.f * T, Building.SizeUU.Y - 2.f * T);
		return false;
	}
	if (T * 4.f > FMath::Min(Building.SizeUU.X, Building.SizeUU.Y))
	{
		OutError = FString::Printf(TEXT("building '%s': walls %.0f uu thick are too heavy for a %.0fx%.0f footprint"),
			*Id, T, Building.SizeUU.X, Building.SizeUU.Y);
		return false;
	}
	// ТЗ §13: "у проходимого здания два выхода". Zero is a closed building and
	// legal; one is a room a single bot in the doorway turns into a coffin.
	if (Building.Doors.Num() == 1)
	{
		OutError = FString::Printf(
			TEXT("building '%s': one doorway. A building the player can enter needs two exits (ТЗ §13); a closed building has none"),
			*Id);
		return false;
	}
	for (const FSarkoBuildingDoor& Door : Building.Doors)
	{
		if (Door.WidthUU < MinDoorwayUU)
		{
			OutError = FString::Printf(TEXT("building '%s': a doorway is %.1f uu wide; the minimum is %.0f (ТЗ §13)"),
				*Id, Door.WidthUU, MinDoorwayUU);
			return false;
		}
		if (Door.WidthUU < PreferredDoorwayUU)
		{
			// A warning, not an error: 250 is legal and 300-350 is preferred, and
			// the difference is comfort rather than correctness.
			UE_LOG(LogTemp, Warning, TEXT("SarkoMap: building '%s' has a %.0f uu doorway; ТЗ §13 prefers %.0f-350"),
				*Id, Door.WidthUU, PreferredDoorwayUU);
		}
	}

	// The inner faces of the perimeter: the rectangle an interior wall lives in,
	// and the rectangle the reachability fill searches.
	const float InnerX = HalfX - T;
	const float InnerY = HalfY - T;

	// ---- Perimeter. N and S span the full width; E and W are shortened by one
	// thickness at each end so they butt into them, which is what makes "no wall
	// overlaps another wall" exactly true rather than nearly true.
	struct FSideSpec
	{
		ESarkoBuildingSide Side;
		const TCHAR* Name;
		bool bAlongX;
		float Across;   // the fixed coordinate of the wall's centre line
		float SpanMin;
		float SpanMax;
	};
	const FSideSpec Sides[4] = {
		{ ESarkoBuildingSide::North, TEXT("north"), true,   HalfY - T * 0.5f, -HalfX,       HalfX },
		{ ESarkoBuildingSide::East,  TEXT("east"),  false,  HalfX - T * 0.5f, -HalfY + T,   HalfY - T },
		{ ESarkoBuildingSide::South, TEXT("south"), true,  -HalfY + T * 0.5f, -HalfX,       HalfX },
		{ ESarkoBuildingSide::West,  TEXT("west"),  false, -HalfX + T * 0.5f, -HalfY + T,   HalfY - T },
	};

	// Where the reachability flood fill starts: one point of floor just inside
	// each perimeter doorway. Collected here rather than derived later because
	// this loop is the only place that knows which side a door is on.
	TArray<FDoorSeed> DoorSeeds;

	for (const FSideSpec& Side : Sides)
	{
		TArray<FGap> Gaps;
		for (const FSarkoBuildingDoor& Door : Building.Doors)
		{
			if (Door.Side == Side.Side)
			{
				// 1 uu inside the wall's inner face, which is at least a fifth of a
				// grid cell for the thinnest legal wall, so this always lands in the
				// row of cells the doorway opens onto.
				switch (Side.Side)
				{
					case ESarkoBuildingSide::North: DoorSeeds.Add({ FVector2D(Door.OffsetUU, InnerY - 1.f), Side.Name }); break;
					case ESarkoBuildingSide::South: DoorSeeds.Add({ FVector2D(Door.OffsetUU, -InnerY + 1.f), Side.Name }); break;
					case ESarkoBuildingSide::East:  DoorSeeds.Add({ FVector2D(InnerX - 1.f, Door.OffsetUU), Side.Name }); break;
					case ESarkoBuildingSide::West:  DoorSeeds.Add({ FVector2D(-InnerX + 1.f, Door.OffsetUU), Side.Name }); break;
				}
				// Offset is measured from the wall's midpoint, and every span
				// above is symmetric about zero, so the midpoint IS zero.
				Gaps.Add({ Door.OffsetUU - Door.WidthUU * 0.5f, Door.OffsetUU + Door.WidthUU * 0.5f });
			}
		}
		if (!ValidateGaps(Id, Side.Name, Side.SpanMin, Side.SpanMax, T, Gaps, OutError))
		{
			OutBlocks.Reset();
			return false;
		}

		TArray<FGap> Segments;
		SegmentsBetweenGaps(Side.SpanMin, Side.SpanMax, Gaps, Segments);
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const float Centre = (Segments[Index].Min + Segments[Index].Max) * 0.5f;
			const float Half = (Segments[Index].Max - Segments[Index].Min) * 0.5f;
			const FString WallId = FString::Printf(TEXT("%s_%s_%d"), *Id, Side.Name, Index);
			OutBlocks.Add(Side.bAlongX
				? MakeWall(Building, WallId, FVector2D(Centre, Side.Across), FVector2D(Half, T * 0.5f))
				: MakeWall(Building, WallId, FVector2D(Side.Across, Centre), FVector2D(T * 0.5f, Half)));
		}
	}

	// ---- Interior walls, in two passes.
	//
	// Pass one reads every wall's geometry and checks the two rules that depend on
	// nothing else: axis alignment, and staying inside the footprint. Pass two
	// checks everything that is a statement about the wall as EMITTED — its length,
	// its passages, its doorway, its segments — and between the two sits the
	// shortening, because a wall that butts into another wall is shorter than the
	// line the author drew and every one of those rules is about the shorter wall.
	struct FInteriorSpan
	{
		bool bAlongY = false;
		float Across = 0.f;
		float SpanMin = 0.f;
		float SpanMax = 0.f;
	};
	TArray<FInteriorSpan> Spans;
	Spans.Reserve(Building.InteriorWalls.Num());

	for (int32 WallIndex = 0; WallIndex < Building.InteriorWalls.Num(); ++WallIndex)
	{
		const FSarkoBuildingInteriorWall& Wall = Building.InteriorWalls[WallIndex];
		const bool bAlongY = FMath::IsNearlyEqual(Wall.From.X, Wall.To.X, 1.f);
		const bool bAlongX = FMath::IsNearlyEqual(Wall.From.Y, Wall.To.Y, 1.f);
		if (bAlongY == bAlongX)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] runs from (%.0f,%.0f) to (%.0f,%.0f), which is %s; interior walls must be axis-aligned"),
				*Id, WallIndex, Wall.From.X, Wall.From.Y, Wall.To.X, Wall.To.Y,
				bAlongX ? TEXT("zero-length") : TEXT("diagonal"));
			OutBlocks.Reset();
			return false;
		}

		FInteriorSpan Span;
		Span.bAlongY = bAlongY;
		Span.Across = bAlongY ? Wall.From.X : Wall.From.Y;
		Span.SpanMin = bAlongY ? FMath::Min(Wall.From.Y, Wall.To.Y) : FMath::Min(Wall.From.X, Wall.To.X);
		Span.SpanMax = bAlongY ? FMath::Max(Wall.From.Y, Wall.To.Y) : FMath::Max(Wall.From.X, Wall.To.X);

		const float AcrossLimit = bAlongY ? InnerX : InnerY;
		const float SpanLimit = bAlongY ? InnerY : InnerX;
		if (FMath::Abs(Span.Across) > AcrossLimit + 1.f ||
			Span.SpanMin < -SpanLimit - 1.f || Span.SpanMax > SpanLimit + 1.f)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] reaches outside the footprint (interior is %.0f..%.0f by %.0f..%.0f)"),
				*Id, WallIndex, -InnerX, InnerX, -InnerY, InnerY);
			OutBlocks.Reset();
			return false;
		}
		Spans.Add(Span);
	}

	// Where one interior wall's endpoint lands on a CROSSING wall's centre line,
	// pull it back to that wall's near face — half a thickness — so the two butt
	// instead of overlapping.
	//
	// The perimeter already does this: E and W are shortened by a full thickness at
	// each end so they meet N and S at the corners, and an interior wall reaching
	// InnerY stops exactly at the north wall's inner face. Only interior-into-
	// interior was missed, and the shipped map hit it: bridge_gas_station's
	// служебная wall ends at x = -300, which is the зал divider's own centre line,
	// so the two intersected over 15x30 uu. That is not a rendering nuisance — it
	// makes "how much wall did we emit" and "how much wall is there" different
	// numbers, which is the crack a missing segment hides in.
	//
	// Exactly one of any pair moves, or a corner where two walls meet end to end
	// would open a T/2 hole between them. A wall that ends INSIDE another's span
	// (a tee) always yields, because there the geometry says which one is the
	// through-wall; where both ends coincide (an ell) the later-authored one
	// yields, which is arbitrary but stable, and stability is what the server and
	// the client need to agree on. Read from a snapshot so the result cannot depend
	// on the order the pairs happen to be visited in.
	{
		const TArray<FInteriorSpan> Authored = Spans;
		const float Tolerance = 1.f;
		for (int32 Index = 0; Index < Spans.Num(); ++Index)
		{
			for (int32 Other = 0; Other < Authored.Num(); ++Other)
			{
				if (Other == Index || Authored[Other].bAlongY == Authored[Index].bAlongY)
				{
					continue; // parallel walls never butt into one another
				}
				const FInteriorSpan& Crossing = Authored[Other];
				// Does the crossing wall's body actually reach this wall's line?
				if (Authored[Index].Across < Crossing.SpanMin - Tolerance ||
					Authored[Index].Across > Crossing.SpanMax + Tolerance)
				{
					continue;
				}
				const bool bTee = Authored[Index].Across > Crossing.SpanMin + Tolerance
					&& Authored[Index].Across < Crossing.SpanMax - Tolerance;
				if (!bTee && Index < Other)
				{
					continue; // an ell: the later wall is the one that yields
				}
				if (FMath::Abs(Authored[Index].SpanMax - Crossing.Across) <= Tolerance)
				{
					Spans[Index].SpanMax = Crossing.Across - T * 0.5f;
				}
				if (FMath::Abs(Authored[Index].SpanMin - Crossing.Across) <= Tolerance)
				{
					Spans[Index].SpanMin = Crossing.Across + T * 0.5f;
				}
			}
		}
	}

	// Every interior segment's footprint in the local frame, for the reachability
	// fill below. Collected as the segments are emitted rather than recomputed, so
	// the fill is asking about the geometry that exists and not about a second
	// derivation of it that could disagree.
	TArray<FLocalRect> InteriorRects;

	for (int32 WallIndex = 0; WallIndex < Building.InteriorWalls.Num(); ++WallIndex)
	{
		const FSarkoBuildingInteriorWall& Wall = Building.InteriorWalls[WallIndex];
		const bool bAlongY = Spans[WallIndex].bAlongY;
		const float Across = Spans[WallIndex].Across;
		const float SpanMin = Spans[WallIndex].SpanMin;
		const float SpanMax = Spans[WallIndex].SpanMax;
		const float AcrossLimit = bAlongY ? InnerX : InnerY;

		if (SpanMax - SpanMin < T)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] is %.1f uu long once trimmed where it meets another wall, shorter than it is thick"),
				*Id, WallIndex, SpanMax - SpanMin);
			OutBlocks.Reset();
			return false;
		}

		// ТЗ §13's "внутренний проход ≥250": the clear space between this wall
		// and anything parallel it could form a corridor with. Checked against
		// the two parallel perimeter faces and against every earlier parallel
		// interior wall whose span overlaps this one — a wall that does not
		// overlap does not form a corridor and is not compared.
		const float ClearToPerimeter = (AcrossLimit - FMath::Abs(Across)) - T * 0.5f;
		if (ClearToPerimeter < MinInteriorPassageUU)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] leaves %.1f uu between it and the outer wall; the minimum passage is %.0f (ТЗ §13)"),
				*Id, WallIndex, ClearToPerimeter, MinInteriorPassageUU);
			OutBlocks.Reset();
			return false;
		}
		for (int32 Other = 0; Other < WallIndex; ++Other)
		{
			if (Spans[Other].bAlongY != bAlongY)
			{
				continue; // perpendicular walls make a corner, not a corridor
			}
			if (Spans[Other].SpanMax <= SpanMin || Spans[Other].SpanMin >= SpanMax)
			{
				continue; // they never face each other
			}
			const float Clear = FMath::Abs(Across - Spans[Other].Across) - T;
			if (Clear < MinInteriorPassageUU)
			{
				OutError = FString::Printf(
					TEXT("building '%s': interiorWalls[%d] and interiorWalls[%d] leave a %.1f uu corridor; the minimum passage is %.0f (ТЗ §13)"),
					*Id, WallIndex, Other, Clear, MinInteriorPassageUU);
				OutBlocks.Reset();
				return false;
			}
		}

		TArray<FGap> Gaps;
		if (Wall.bHasDoor)
		{
			if (Wall.Door.WidthUU < MinInteriorPassageUU)
			{
				OutError = FString::Printf(
					TEXT("building '%s': interiorWalls[%d] has a %.1f uu doorway; the minimum interior passage is %.0f (ТЗ §13)"),
					*Id, WallIndex, Wall.Door.WidthUU, MinInteriorPassageUU);
				OutBlocks.Reset();
				return false;
			}
			// Interior offsets are measured from the wall's own midpoint, which
			// (unlike a perimeter side) is not necessarily zero — and which is the
			// midpoint of the wall as emitted, after any trimming above, so a
			// doorway authored at offset 0 is centred in the wall that is actually
			// there rather than in the line the author drew through another wall.
			const float Mid = (SpanMin + SpanMax) * 0.5f;
			Gaps.Add({ Mid + Wall.Door.OffsetUU - Wall.Door.WidthUU * 0.5f,
			           Mid + Wall.Door.OffsetUU + Wall.Door.WidthUU * 0.5f });
		}
		const FString WallName = FString::Printf(TEXT("interior %d"), WallIndex);
		if (!ValidateGaps(Id, *WallName, SpanMin, SpanMax, T, Gaps, OutError))
		{
			OutBlocks.Reset();
			return false;
		}

		TArray<FGap> Segments;
		SegmentsBetweenGaps(SpanMin, SpanMax, Gaps, Segments);
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const float Centre = (Segments[Index].Min + Segments[Index].Max) * 0.5f;
			const float Half = (Segments[Index].Max - Segments[Index].Min) * 0.5f;
			const FString SegmentId = FString::Printf(TEXT("%s_interior%d_%d"), *Id, WallIndex, Index);
			const FVector2D LocalCentre = bAlongY ? FVector2D(Across, Centre) : FVector2D(Centre, Across);
			const FVector2D LocalHalf = bAlongY ? FVector2D(T * 0.5f, Half) : FVector2D(Half, T * 0.5f);
			OutBlocks.Add(MakeWall(Building, SegmentId, LocalCentre, LocalHalf));
			InteriorRects.Add({
				static_cast<float>(LocalCentre.X - LocalHalf.X), static_cast<float>(LocalCentre.X + LocalHalf.X),
				static_cast<float>(LocalCentre.Y - LocalHalf.Y), static_cast<float>(LocalCentre.Y + LocalHalf.Y) });
		}
	}

	// ---- Reachability. Every rule above is about one wall or one pair of walls;
	// this one is about the shape they make together, which is the only way to
	// catch a pocket that no individual wall is wrong about. Skipped for a closed
	// building (no doorways, nothing to reach) and for a building with no interior
	// walls (nothing that could enclose anything).
	if (DoorSeeds.Num() > 0 && InteriorRects.Num() > 0)
	{
		if (!InteriorIsReachable(Id, InnerX, InnerY, T, InteriorRects, DoorSeeds, OutError))
		{
			OutBlocks.Reset();
			return false;
		}
	}

	return true;
}

bool SarkoMap::ExpandBuildings(const TArray<FSarkoBuilding>& Buildings, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)
{
	OutBlocks.Reset();
	OutError.Reset();

	TArray<FSarkoCoverBlock> One;
	for (const FSarkoBuilding& Building : Buildings)
	{
		if (!ExpandBuilding(Building, One, OutError))
		{
			OutBlocks.Reset();
			return false;
		}
		OutBlocks.Append(One);
	}
	return true;
}
