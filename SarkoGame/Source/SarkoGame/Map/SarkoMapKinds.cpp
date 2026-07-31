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
	 * One part of a composite kind, offset from the prop's origin. Used by the
	 * three composites below — the pylon, the road sign and the trailer — which
	 * are the whole reason parts were added.
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
	 * Sizes are chosen against a ~176 uu tall pawn: a wreck is cover you can shoot
	 * over, a house is tall enough to break line of sight entirely, sandbags are
	 * crouch-height. That relationship is the level design — the numbers are not
	 * arbitrary, and Sarko.Map.PropKindScaleMatchesThePawn asserts them.
	 *
	 * EXTENTS ARE HALF-EXTENTS, so a kind's height above the floor is TWICE its
	 * Extent.Z. Every comment below therefore quotes the full height and its
	 * multiple of the pawn, because "chest-high" written next to a half-extent is
	 * how the two oldest entries here came to say the opposite of what they are:
	 * car_wreck is 150 uu (0.85x the pawn — shoulder-, not chest-high) and
	 * fuel_pump is 220 uu (1.25x — a sight blocker, not cover). Both extents are
	 * frozen by the 238 props already placed against them; the lesson is not.
	 *
	 * The first eleven extents are byte-identical to the pre-parts table: 238 props
	 * in bridge.json are placed with pos.z equal to their kind's half-height, so a
	 * changed half-height moves every instance of that kind at once. All eleven are
	 * single-box, so their part offsets are zero and the map does not move.
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

			// ---- ТЗ §15 / §32: filling the world. Large forms, not hundreds of
			// small ones — every entry below is one actor except the three
			// composites, and the north needs ~90 of them in total.

			// A boulder, 210 x 180 x 130 uu. Sphere rather than cube so the
			// silhouette is not a fourth kind of box. 130 uu is 0.74x the pawn —
			// chest-high: cover you shoot over standing, and the tallest thing in
			// the shoot-over set except the wreck.
			{ TEXT("rock"),             Box(Sphere,   FVector(105.f, 90.f, 65.f),   true,  ESarkoSurface::Structure) },
			// The one thing the player walks through, 270 x 260 x 90 uu. Wide and
			// LOW on purpose: at 90 uu (0.51x the pawn, thigh-high) it is visibly
			// shorter than the 110 uu floor of real cover, so nobody crouches
			// behind it and discovers by walking through that it was never there.
			{ TEXT("bush"),             Box(Sphere,   FVector(135.f, 130.f, 45.f),  false, ESarkoSurface::Vegetation) },
			// A fallen log, 660 uu long and 110 uu thick — 0.63x the pawn,
			// waist-high cover. A box, not a cylinder: FSarkoPropPart has no roll,
			// so a cylinder would stand upright like a stump.
			{ TEXT("log"),              Box(Cube,     FVector(330.f, 55.f, 55.f),   true,  ESarkoSurface::Timber) },
			// Fence: 800 uu long, 24 uu thin, 184 uu tall — 1.05x the pawn, so it
			// is a sight blocker rather than cover. A line of them reads as a
			// boundary from above and cuts line of sight at ground level.
			{ TEXT("fence_section"),    Box(Cube,     FVector(400.f, 12.f, 92.f),   true,  ESarkoSurface::Timber) },
			// Jersey barrier, 300 uu long and 110 uu tall (0.63x the pawn,
			// waist-high — the real thing is about a metre). Low, heavy, and the
			// pale concrete tone, which is what makes a row of them read as a
			// deliberate closure rather than as scattered junk.
			{ TEXT("concrete_barrier"), Box(Cube,     FVector(150.f, 45.f, 55.f),   true,  ESarkoSurface::Concrete) },

			// Post plus plate: the plate is pale concrete so it catches the eye
			// from above, which is the entire job of a road sign in this game.
			// The post blocks (you cannot walk through a pole) and stands 300 uu;
			// the plate hangs from 265 to 375 uu — clear of the pawn's head, and
			// non-colliding, so it never snags anyone. Both parts share the prop's
			// vertical axis, which is why the plate stays over the post at any yaw.
			{ TEXT("road_sign"),        Composite({
				Part(Cube, FVector(8.f, 8.f, 150.f),   FVector(0.f, 0.f, 150.f), true,  ESarkoSurface::Structure),
				Part(Cube, FVector(85.f, 10.f, 55.f),  FVector(0.f, 0.f, 320.f), false, ESarkoSurface::Concrete),
			}) },

			// Cargo trailer: body plus tow bar. Rust, because it belongs to the
			// industrial and roadside vocabulary, and ТЗ L01 puts junk loot in one.
			// The body is 700 x 240 x 220 uu riding from 30 to 250 uu — the 30 uu
			// gap is where its wheels would be, and 250 uu (1.42x the pawn) makes
			// it a sight blocker. The bar butts exactly against the body's -X face
			// at 350 uu, so the two boxes touch without overlapping.
			{ TEXT("trailer"),          Composite({
				Part(Cube, FVector(350.f, 120.f, 110.f), FVector(0.f, 0.f, 140.f),   true, ESarkoSurface::Rust),
				Part(Cube, FVector(90.f, 15.f, 12.f),    FVector(-440.f, 0.f, 62.f), true, ESarkoSurface::Rust),
			}) },

			// ЛЭП pylon: two vertical legs 280 uu apart and two crossarms. Not an
			// A-frame — FSarkoPropPart has no roll, so a leaning leg is not
			// expressible; two parallel uprights read correctly from directly above
			// anyway and cost four actors instead of the six a four-legged tower
			// would. The legs run 0 to 1800 uu (18 m, 10x the pawn) and collide;
			// the crossarms sit at 15 m and 17.8 m and do NOT, so they cost no
			// physics and cannot be walked into.
			{ TEXT("pylon"),            Composite({
				Part(Cube, FVector(30.f, 30.f, 900.f),  FVector(-140.f, 0.f, 900.f), true,  ESarkoSurface::Rust),
				Part(Cube, FVector(30.f, 30.f, 900.f),  FVector(140.f, 0.f, 900.f),  true,  ESarkoSurface::Rust),
				Part(Cube, FVector(420.f, 25.f, 20.f),  FVector(0.f, 0.f, 1500.f),   false, ESarkoSurface::Structure),
				Part(Cube, FVector(300.f, 25.f, 20.f),  FVector(0.f, 0.f, 1780.f),   false, ESarkoSurface::Structure),
			}) },

			// The forest, as a border. NOT trees: a canopy hides the player from
			// a top-down camera, which is a gameplay defect and not a look
			// (spec §5.3). Tile these along an edge — 1200 uu long each, 400 uu
			// deep, 1000 uu (10 m, 5.7x the pawn) tall, dark green, impassable.
			// This is what closes off the east until Stage C builds it.
			{ TEXT("treeline"),         Box(Cube,     FVector(600.f, 200.f, 500.f), true,  ESarkoSurface::Vegetation) },
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

FVector SarkoMap::PartWorldLocation(const FVector& PropLocation, float PropYawDegrees, const FSarkoPropPart& Part)
{
	// Yaw only: FSarkoPropPart has no pitch or roll, which is why a fallen log is
	// a flat box rather than a cylinder laid on its side.
	return PropLocation + FRotator(0.f, PropYawDegrees, 0.f).RotateVector(Part.Offset);
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
