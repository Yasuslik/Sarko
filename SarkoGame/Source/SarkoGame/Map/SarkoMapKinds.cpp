#include "Map/SarkoMapKinds.h"

#include "Map/SarkoMapDefinition.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const FString Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	/**
	 * The fourth primitive, and it exists in this table for exactly one job: a
	 * conifer canopy. From the game's camera (a 1400 uu boom at -70 degrees, so
	 * 20 degrees off vertical) a sphere and a cone have the same circular
	 * silhouette, and the 20 degrees is all the difference there is to work with
	 * — which is precisely why it is worth having both. A stand of nothing but
	 * spheres reads as a field of green bubbles; one cone in three gives the
	 * skyline the notches that say "trees".
	 */
	const FString Cone = TEXT("/Engine/BasicShapes/Cone.Cone");

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

	/**
	 * The leafy part of a tree. The only place FSarkoPropPart::bCanopy is set,
	 * and it hard-wires the two properties a canopy must have alongside it:
	 * NEVER colliding (the fade changes visibility only, so collision must not
	 * depend on it — see the field's own comment) and always Vegetation, because
	 * a canopy that is not green is not a canopy.
	 */
	FSarkoPropPart Canopy(const FString& Mesh, const FVector& Extent, const FVector& Offset)
	{
		FSarkoPropPart Result;
		Result.Mesh = FSoftObjectPath(Mesh);
		Result.Extent = Extent;
		Result.Offset = Offset;
		Result.bBlocksMovement = false;
		Result.Surface = ESarkoSurface::Vegetation;
		Result.bCanopy = true;
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
			// ТЗ §5's «тёмный асфальт»: the ONE legacy kind that is not Structure,
			// and it is a deliberate exception rather than a drift. The deck and the
			// parapets were both grey, which made the deck the pale thing and the
			// rails invisible against it — the inverse of what §5 asks for. The
			// extents are untouched, so not one of the nine deck props moved.
			{ TEXT("bridge_deck"), Box(Cube,     FVector(900.f, 300.f, 30.f),  true, ESarkoSurface::Asphalt) },

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

			// ---- THE FOREST. Four kinds, each a trunk the player can hide behind
			// with a canopy held clear over its head.
			//
			// The shape of every one of them is the same argument. COLLISION IS THE
			// TRUNK AND ONLY THE TRUNK: a canopy is four to eight metres up, nobody
			// can reach it, and a bullet or a pawn stopped by leaves the player
			// cannot see the underside of is the most infuriating kind of invisible
			// wall. So the canopies are authored through SarkoMap::Canopy, which
			// hard-wires bBlocksMovement = false, and the trunk is what the fight
			// actually happens around.
			//
			// The trunks are deliberately fat — 80 to 110 uu across, against a pawn
			// roughly 70 uu wide. A realistically thin tree is not cover on a
			// top-down map; it is a post you die beside. Every trunk also clears the
			// 176 uu pawn (350 uu at the shortest), so a tree breaks line of sight
			// outright rather than being something to shoot over.
			//
			// Three living sizes and one dead one, because a stand built from one
			// kind reads as a stamp. Variation here is the entire difference between
			// "a forest" and "an array".
			//
			// These replace nothing: `treeline` below stays exactly what it was, the
			// impassable dark-green wall that closes the sector. The distinction is
			// now meaningful rather than an apology — a treeline is the edge of the
			// world, a tree is cover you walk into.

			// The default deciduous tree. Trunk 110 uu across running 0..520 uu
			// (3.0x the pawn); canopy a 600 uu ball hanging 420..860 uu, so its
			// underside is 2.4x the pawn's height and there is a clear storey to
			// walk and fight in beneath it. Canopy and trunk overlap by 100 uu, so
			// when the canopy fades out the trunk is not left with a gap over it.
			{ TEXT("tree"),             Composite({
				Part(Cylinder, FVector(55.f, 55.f, 260.f),   FVector(0.f, 0.f, 260.f), true, ESarkoSurface::Bark),
				Canopy(Sphere, FVector(300.f, 300.f, 220.f), FVector(0.f, 0.f, 640.f)),
			}) },

			// The conifer, and the tallest thing in the forest: trunk 90 uu across
			// running 0..680 uu (3.9x the pawn), cone canopy 540 uu across from 570
			// to 1230 uu (7.0x). The cone is the reason this kind exists — see the
			// Cone constant above. Its canopy floor is the highest of the four, so a
			// stand of these is the most open one to fight in.
			{ TEXT("tree_tall"),        Composite({
				Part(Cylinder, FVector(45.f, 45.f, 340.f),   FVector(0.f, 0.f, 340.f), true, ESarkoSurface::Bark),
				Canopy(Cone,   FVector(270.f, 270.f, 330.f), FVector(0.f, 0.f, 900.f)),
			}) },

			// The undergrowth tree. Trunk 80 uu across, 0..350 uu (2.0x the pawn),
			// canopy 430 uu across from 300 to 620 uu. 300 uu is the tightest
			// headroom in the table and it is still 1.7x the pawn — a player walking
			// under one is never touching it, which is what keeps the fade a purely
			// visual event.
			{ TEXT("tree_small"),       Composite({
				Part(Cylinder, FVector(40.f, 40.f, 175.f),   FVector(0.f, 0.f, 175.f), true, ESarkoSurface::Bark),
				Canopy(Sphere, FVector(215.f, 215.f, 160.f), FVector(0.f, 0.f, 460.f)),
			}) },

			// A dead trunk with its top snapped off: 100 uu across from 0 to 460 uu,
			// then a 56 uu spar from 450 to 750 uu. NO CANOPY at all, which makes it
			// the one tree that never fades — a permanent vertical landmark inside a
			// stand whose roof comes and goes, and the thing that stops a faded
			// clearing from looking like nothing was ever there.
			//
			// Composite rather than a single tapered box on purpose: two parts also
			// keeps it on the pos.z = 0 authoring convention its three neighbours
			// use, so a whole stand is authored with one z value instead of trees at
			// 0 and dead trees at 230.
			{ TEXT("tree_dead"),        Composite({
				Part(Cylinder, FVector(50.f, 50.f, 230.f), FVector(0.f, 0.f, 230.f), true, ESarkoSurface::Bark),
				Part(Cylinder, FVector(28.f, 28.f, 150.f), FVector(0.f, 0.f, 600.f), true, ESarkoSurface::Bark),
			}) },

			// The forest as a BORDER, which is a different object from the four
			// kinds above and stays one. Tile these along an edge — 1200 uu long
			// each, 400 uu deep, 1000 uu (10 m, 5.7x the pawn) tall, dark green,
			// impassable. This is what closes off the east, and it is opaque on
			// purpose: you are not meant to see, walk or shoot through the edge of
			// the world. A `tree` is the opposite of it in every one of those.
			{ TEXT("treeline"),         Box(Cube,     FVector(600.f, 200.f, 500.f), true,  ESarkoSurface::Vegetation) },

			// ТЗ §5's «светлые борта». A wall's exact twin — 400x60x140 — so the
			// eighteen parapet and approach-rail props are re-kinded in place
			// without one of them moving; only the colour differs, and that is the
			// whole point: the deck went dark, so the rails have to go pale or the
			// crossing loses its silhouette from above.
			{ TEXT("bridge_rail"),      Box(Cube,     FVector(400.f, 60.f, 140.f),  true, ESarkoSurface::Concrete) },
			// ТЗ §14's «деревня тёплая» / «промзона ржавая». house's exact extents
			// (500x400x300), so a cluster is re-kinded in place. Sixteen identical
			// grey boxes made the village and the промзона differ only by how the
			// boxes were arranged; hue is the cheapest cluster identity there is.
			{ TEXT("house_timber"),     Box(Cube,     FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Timber) },
			{ TEXT("house_industrial"), Box(Cube,     FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Rust) },
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

int32 SarkoMap::CountPropParts(const FSarkoMapDefinition& Definition)
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

int32 SarkoMap::CountInstancedComponents(const FSarkoMapDefinition& Definition)
{
	// The key has to be the same four fields ASarkoPropField::FindOrCreateComponent
	// keys on, or this number predicts nothing. Mesh and surface because that is
	// what a draw call is; collision because a component carries one setting for
	// every instance in it; the canopy flag because canopies are the only
	// instances that ever change and are deliberately kept out of the components
	// that never do.
	TSet<FString> Keys;
	for (const FSarkoMapProp& Prop : Definition.Props)
	{
		FSarkoPropKind Kind;
		if (!FindPropKind(Prop.Kind, Kind))
		{
			continue;
		}
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			Keys.Add(FString::Printf(TEXT("%s|%d|%d|%d"),
				*Part.Mesh.ToString(), static_cast<int32>(Part.Surface),
				Part.bBlocksMovement ? 1 : 0, Part.bCanopy ? 1 : 0));
		}
	}
	return Keys.Num();
}
