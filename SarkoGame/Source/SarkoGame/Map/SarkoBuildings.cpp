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
	 * `Thickness` is the minimum wall that must survive at each end and between
	 * two doors: a stub shorter than the wall is thick reads as a floating fleck
	 * of geometry, and a zero-length one would emit a degenerate block that the
	 * map parser rejects (extent components must be positive) — turning an
	 * authoring slip into "the whole map failed to load".
	 */
	bool ValidateGaps(const FString& BuildingId, const TCHAR* WallName, float SpanMin, float SpanMax,
		float Thickness, TArray<FGap>& Gaps, FString& OutError)
	{
		Gaps.Sort([](const FGap& A, const FGap& B) { return A.Min < B.Min; });

		for (int32 Index = 0; Index < Gaps.Num(); ++Index)
		{
			const FGap& Gap = Gaps[Index];
			if (Gap.Min < SpanMin + Thickness - KINDA_SMALL_NUMBER ||
				Gap.Max > SpanMax - Thickness + KINDA_SMALL_NUMBER)
			{
				OutError = FString::Printf(
					TEXT("building '%s': a doorway on the %s wall runs to %.1f..%.1f, which leaves less than %.1f uu of wall at an end (the wall spans %.1f..%.1f)"),
					*BuildingId, WallName, Gap.Min, Gap.Max, Thickness, SpanMin, SpanMax);
				return false;
			}
			if (Index > 0)
			{
				const float Between = Gap.Min - Gaps[Index - 1].Max;
				if (Between < Thickness - KINDA_SMALL_NUMBER)
				{
					OutError = Between < 0.f
						? FString::Printf(
							TEXT("building '%s': two doorways on the %s wall overlap by %.1f uu"),
							*BuildingId, WallName, -Between)
						: FString::Printf(
							TEXT("building '%s': two doorways on the %s wall leave only %.1f uu of wall between them; a pier must be at least as wide as the wall is thick (%.1f uu)"),
							*BuildingId, WallName, Between, Thickness);
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

	for (const FSideSpec& Side : Sides)
	{
		TArray<FGap> Gaps;
		for (const FSarkoBuildingDoor& Door : Building.Doors)
		{
			if (Door.Side == Side.Side)
			{
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

	// ---- Interior walls.
	const float InnerX = HalfX - T;
	const float InnerY = HalfY - T;

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

		const float Across = bAlongY ? Wall.From.X : Wall.From.Y;
		const float SpanMin = bAlongY ? FMath::Min(Wall.From.Y, Wall.To.Y) : FMath::Min(Wall.From.X, Wall.To.X);
		const float SpanMax = bAlongY ? FMath::Max(Wall.From.Y, Wall.To.Y) : FMath::Max(Wall.From.X, Wall.To.X);
		const float AcrossLimit = bAlongY ? InnerX : InnerY;
		const float SpanLimit = bAlongY ? InnerY : InnerX;

		if (FMath::Abs(Across) > AcrossLimit + 1.f ||
			SpanMin < -SpanLimit - 1.f || SpanMax > SpanLimit + 1.f)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] reaches outside the footprint (interior is %.0f..%.0f by %.0f..%.0f)"),
				*Id, WallIndex, -InnerX, InnerX, -InnerY, InnerY);
			OutBlocks.Reset();
			return false;
		}
		if (SpanMax - SpanMin < T)
		{
			OutError = FString::Printf(TEXT("building '%s': interiorWalls[%d] is %.1f uu long, shorter than it is thick"),
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
			const FSarkoBuildingInteriorWall& Prior = Building.InteriorWalls[Other];
			const bool bPriorAlongY = FMath::IsNearlyEqual(Prior.From.X, Prior.To.X, 1.f);
			if (bPriorAlongY != bAlongY)
			{
				continue; // perpendicular walls make a corner, not a corridor
			}
			const float PriorAcross = bAlongY ? Prior.From.X : Prior.From.Y;
			const float PriorMin = bAlongY ? FMath::Min(Prior.From.Y, Prior.To.Y) : FMath::Min(Prior.From.X, Prior.To.X);
			const float PriorMax = bAlongY ? FMath::Max(Prior.From.Y, Prior.To.Y) : FMath::Max(Prior.From.X, Prior.To.X);
			if (PriorMax <= SpanMin || PriorMin >= SpanMax)
			{
				continue; // they never face each other
			}
			const float Clear = FMath::Abs(Across - PriorAcross) - T;
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
			// (unlike a perimeter side) is not necessarily zero.
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
			OutBlocks.Add(bAlongY
				? MakeWall(Building, SegmentId, FVector2D(Across, Centre), FVector2D(T * 0.5f, Half))
				: MakeWall(Building, SegmentId, FVector2D(Centre, Across), FVector2D(Half, T * 0.5f)));
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
