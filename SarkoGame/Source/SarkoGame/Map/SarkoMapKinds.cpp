#include "Map/SarkoMapKinds.h"

#include "Map/SarkoMapDefinition.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const FString Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	/** One box, centred on the prop's origin. The shape of every legacy kind. */
	FSarkoPropKind Box(const FString& Mesh, const FVector& Extent, bool bBlocks, ESarkoSurface Surface)
	{
		FSarkoPropPart Part;
		Part.Mesh = FSoftObjectPath(Mesh);
		Part.Extent = Extent;
		Part.bBlocksMovement = bBlocks;
		Part.Surface = Surface;

		FSarkoPropKind Kind;
		Kind.Parts.Add(Part);
		return Kind;
	}

	/**
	 * One part of a composite kind, offset from the prop's origin. Unused by the
	 * eleven single-box kinds below; it exists because the next task's pylon and
	 * road sign are the whole reason parts were added.
	 */
	FSarkoPropPart Part(const FString& Mesh, const FVector& Extent, const FVector& Offset,
		bool bBlocks, ESarkoSurface Surface)
	{
		FSarkoPropPart Result;
		Result.Mesh = FSoftObjectPath(Mesh);
		Result.Extent = Extent;
		Result.Offset = Offset;
		Result.bBlocksMovement = bBlocks;
		Result.Surface = Surface;
		return Result;
	}

	/** Several boxes as one authored entry. pos.z = 0; the parts carry height. */
	FSarkoPropKind Composite(TArray<FSarkoPropPart> Parts)
	{
		FSarkoPropKind Kind;
		Kind.Parts = MoveTemp(Parts);
		return Kind;
	}

	/**
	 * The whole prop vocabulary of the Bridge sector.
	 *
	 * Sizes are chosen against a ~176 uu tall pawn: a car wreck is chest-high
	 * cover you can shoot over, a house is tall enough to break line of sight
	 * entirely, sandbags are crouch-height. That relationship is the level
	 * design — the numbers are not arbitrary.
	 *
	 * Every extent below is byte-identical to the pre-parts table: 238 props in
	 * bridge.json are placed with pos.z equal to their kind's half-height, so a
	 * changed half-height moves every instance of that kind at once. All eleven
	 * are single-box, so their part offsets are zero and the map does not move.
	 *
	 * A COMPOSITE kind (several parts, each with its own Offset) is authored with
	 * pos.z = 0 in the map file instead — see FSarkoPropPart::Offset. The two
	 * conventions must not be mixed for one kind.
	 */
	const TMap<FName, FSarkoPropKind>& KindTable()
	{
		static const TMap<FName, FSarkoPropKind> Table = {
			{ TEXT("wall"),        Box(Cube,     FVector(400.f, 60.f, 140.f),  true, ESarkoSurface::Structure) },
			{ TEXT("car_wreck"),   Box(Cube,     FVector(230.f, 95.f, 75.f),   true, ESarkoSurface::Structure) },
			{ TEXT("bus"),         Box(Cube,     FVector(600.f, 130.f, 160.f), true, ESarkoSurface::Structure) },
			{ TEXT("house"),       Box(Cube,     FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Structure) },
			{ TEXT("fuel_pump"),   Box(Cube,     FVector(60.f, 40.f, 110.f),   true, ESarkoSurface::Structure) },
			{ TEXT("freight_car"), Box(Cube,     FVector(700.f, 150.f, 200.f), true, ESarkoSurface::Structure) },
			{ TEXT("water_tower"), Box(Cylinder, FVector(220.f, 220.f, 700.f), true, ESarkoSurface::Structure) },
			{ TEXT("sandbag"),     Box(Cube,     FVector(180.f, 70.f, 55.f),   true, ESarkoSurface::Structure) },
			{ TEXT("crate"),       Box(Cube,     FVector(70.f, 70.f, 70.f),    true, ESarkoSurface::Structure) },
			{ TEXT("pipe"),        Box(Cylinder, FVector(90.f, 90.f, 600.f),   true, ESarkoSurface::Structure) },
			{ TEXT("bridge_deck"), Box(Cube,     FVector(900.f, 300.f, 30.f),  true, ESarkoSurface::Structure) },
		};
		return Table;
	}
}

bool SarkoMap::FindPropKind(FName Kind, FSarkoPropKind& OutKind)
{
	if (const FSarkoPropKind* Found = KindTable().Find(Kind))
	{
		OutKind = *Found;
		return true;
	}
	return false;
}

int32 SarkoMap::CountPropActors(const FSarkoMapDefinition& Definition)
{
	int32 Total = 0;
	for (const FSarkoMapProp& Prop : Definition.Props)
	{
		FSarkoPropKind Kind;
		if (FindPropKind(Prop.Kind, Kind))
		{
			Total += Kind.Parts.Num();
		}
	}
	return Total;
}
