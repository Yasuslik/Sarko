#include "Map/SarkoMapKinds.h"

#include "Map/SarkoMapDefinition.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/**
	 * The imported meshes, all CC0, all from Quaternius — see
	 * Content/ThirdParty/LICENSES.md for what was taken and what was rejected.
	 *
	 * EVERY ONE OF THESE IS NORMALISED TO THE SAME -50..50 uu BOX AS THE ENGINE
	 * CUBE, by Scripts/prepare-assets.py. That is the whole reason a real mesh can
	 * be dropped into this table without touching a line of ASarkoPropField:
	 * `Extent / 50` is still the scale and an Extent is still exactly the prop's
	 * half-extent in world units. What the player collides with is the convex
	 * hull Interchange fits at import, which is CONTAINED BY that box rather than
	 * equal to it — tighter, never larger, which is the right way round: an
	 * extent is an upper bound on the prop, so the spawn-clearance and
	 * wall-clearance assertions that reason in extents stay true.
	 * Sarko.Config.ThirdPartyMeshBoundsAreNormalised asserts it rather
	 * than trusting it, because a re-import that lost the normalisation would
	 * change the size of every prop in the sector and break no compile.
	 *
	 * The price of the normalisation is that an Extent whose PROPORTIONS differ
	 * from the mesh's own stretches it. So the extents below are authored in each
	 * mesh's own proportions wherever the map allows: a single-box kind's
	 * Extent.Z is pinned by the pos.z of every prop already placed with it, so
	 * X and Y are derived from Z and the mesh's true dimensions, and only the two
	 * fully-pinned kinds (car_wreck, water_tower) keep an extent that distorts —
	 * by 17% and 8%, which is a slightly tall car and a slightly stretched tower
	 * and neither is visible from 1400 uu up.
	 */
	const FString MeshTree = TEXT("/Game/ThirdParty/UltimateNature/CommonTree_3.CommonTree_3");
	const FString MeshTreeCanopy = TEXT("/Game/ThirdParty/UltimateNature/CommonTree_3_Canopy.CommonTree_3_Canopy");
	const FString MeshTreeSmall = TEXT("/Game/ThirdParty/UltimateNature/CommonTree_4.CommonTree_4");
	const FString MeshTreeSmallCanopy = TEXT("/Game/ThirdParty/UltimateNature/CommonTree_4_Canopy.CommonTree_4_Canopy");
	const FString MeshPine = TEXT("/Game/ThirdParty/UltimateNature/PineTree_5.PineTree_5");
	const FString MeshPineCanopy = TEXT("/Game/ThirdParty/UltimateNature/PineTree_5_Canopy.PineTree_5_Canopy");
	const FString MeshTreeDead = TEXT("/Game/ThirdParty/UltimateNature/CommonTree_Dead_3.CommonTree_Dead_3");
	const FString MeshRock = TEXT("/Game/ThirdParty/UltimateNature/Rock_6.Rock_6");
	const FString MeshBush = TEXT("/Game/ThirdParty/UltimateNature/Bush_1_Canopy.Bush_1_Canopy");
	const FString MeshLog = TEXT("/Game/ThirdParty/UltimateNature/WoodLog.WoodLog");
	const FString MeshCar = TEXT("/Game/ThirdParty/Cars/NormalCar1.NormalCar1");
	const FString MeshWaterTower = TEXT("/Game/ThirdParty/ZombieApocalypse/WaterTower.WaterTower");

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
			// A real car body now, and the extent is untouched, so not one of the
			// fifty-one wrecks moved. NormalCar1 is 4.22 x 1.81 x 1.18 m, and this
			// extent is 4.60 x 1.90 x 1.50 — 4% narrower and 17% taller than the
			// mesh's own proportions. That 17% is the one visible compromise in
			// the table and it was taken deliberately: pos.z is 75 on every one of
			// the fifty-one, so the alternative to a slightly tall car is moving
			// all of them.
			{ TEXT("car_wreck"),   Box(MeshCar,  FVector(230.f, 95.f, 75.f),   true, ESarkoSurface::Structure) },
			{ TEXT("bus"),         Box(Cube,     FVector(600.f, 130.f, 160.f), true, ESarkoSurface::Structure) },
			{ TEXT("house"),       Box(Cube,     FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Structure) },
			{ TEXT("fuel_pump"),   Box(Cube,     FVector(60.f, 40.f, 110.f),   true, ESarkoSurface::Structure) },
			{ TEXT("freight_car"), Box(Cube,     FVector(700.f, 150.f, 200.f), true, ESarkoSurface::Structure) },
			// The sector's one landmark that the brief names by itself, and it was
			// a grey cylinder. The mesh is a legged tower with a tank on top; the
			// extent is untouched (the tower is 5.4 x 5.3 x 9.4 m and this is
			// 4.4 x 4.4 x 14.0, so it is stretched 8% tall against its width) and
			// the single placed instance did not move.
			{ TEXT("water_tower"), Box(MeshWaterTower, FVector(220.f, 220.f, 700.f), true, ESarkoSurface::Structure) },
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

			// A boulder, 210 x 168 x 130 uu. Was a sphere; the mesh's own
			// proportions are 1.14 x 0.91 x 0.71 m, which at the pinned 65 uu
			// half-height gives 105 x 84 — six units off the sphere's 105 x 90, so
			// the footprint barely moved and the silhouette stopped being a ball.
			// 130 uu is still 0.74x the pawn — chest-high: cover you shoot over
			// standing, and the tallest thing in the shoot-over set except the wreck.
			{ TEXT("rock"),             Box(MeshRock, FVector(105.f, 84.f, 65.f),   true,  ESarkoSurface::Structure) },
			// The one thing the player walks through, and it SHRANK: 122 x 96 x 90
			// uu against the sphere's 270 x 260 x 90. The bush mesh is 1.69 x 1.33
			// x 1.24 m, a shrub rather than a thicket, and at the pinned 45 uu
			// half-height its own proportions come out this small. Taking it is the
			// honest option — the alternative is a 2.7 m bush, which is what a
			// sphere stretched to the old extent would now visibly be.
			// Still LOW on purpose: at 90 uu (0.51x the pawn, thigh-high) it is
			// visibly shorter than the 110 uu floor of real cover, so nobody
			// crouches behind it and discovers by walking through that it was
			// never there. The mesh is the foliage half of Bush_1 — the pack ships
			// no woody part for it, which is why this kind is a canopy mesh used
			// as a ground prop rather than a split like the trees.
			{ TEXT("bush"),             Box(MeshBush, FVector(61.f, 48.f, 45.f),    false, ESarkoSurface::Vegetation) },
			// A fallen log, now 392 uu long against the box's 660 and 110 uu thick
			// — 0.63x the pawn, waist-high cover, unchanged. The mesh is 2.67 m
			// long; at the pinned 55 uu half-height its own proportions give 196,
			// and stretching a 2.7 m log to 6.6 m would make the bark a smear.
			// Scripts/prepare-assets.py turns it so its length is X, which is what
			// the fourteen authored yaws were chosen against.
			{ TEXT("log"),              Box(MeshLog,  FVector(196.f, 47.f, 55.f),   true,  ESarkoSurface::Timber) },
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

			// Every one of the four is now a real mesh, and the SPLIT is the whole
			// reason Scripts/prepare-assets.py runs Blender at all: Quaternius
			// ships a tree as ONE mesh with a Wood slot and two green ones, and
			// this table needs the green half as its own part so it can be
			// non-colliding and fadeable. The two halves are exported from one
			// source in one pass, so a trunk and its canopy are cut from the same
			// tree and their offsets below are that tree's own geometry measured
			// and multiplied by a single scale — not two shapes hand-fitted to
			// each other.
			//
			// Each kind's scale was chosen by the CANOPY FLOOR, not by taste. A
			// Quaternius tree at its modelled size hangs its leaves 1.4 m up, which
			// is below the 264 uu (1.5x pawn) headroom the canopy contract requires
			// — so the tree is scaled until its own underside clears that, and the
			// rest of its dimensions follow. That is why these are 5 to 7 m trees
			// rather than 3 m ones, and why the number is not round.

			// The default deciduous tree, scaled 2.2x. Trunk 178 x 141 uu across
			// running 0..660 uu (3.8x the pawn); canopy 343 x 282 uu hanging
			// 327..761, so its underside is 1.8x the pawn's height and there is a
			// clear storey to walk and fight in beneath it. Trunk and canopy
			// overlap by 334 uu, so when the canopy fades the trunk is not left
			// with a gap over it — the overlap is the branches, which belong to
			// the wood half and stay.
			{ TEXT("tree"),             Composite({
				Part(MeshTree, FVector(89.f, 70.f, 330.f),         FVector(0.f, 0.f, 330.f), true, ESarkoSurface::Bark),
				Canopy(MeshTreeCanopy, FVector(224.f, 182.f, 217.f), FVector(0.f, 0.f, 544.f)),
			}) },

			// The conifer, scaled 2.6x, and the slimmest trunk in the forest at
			// 107 x 112 uu — a pine really is a pole, and this is the one tree
			// whose modelled trunk is close to the 90 uu the old cylinder claimed.
			// It runs 0..644 uu; the canopy is 382 x 348 uu from 287 to 701. Its
			// canopy floor is the highest of the three, so a stand of these is the
			// most open one to fight in — which was the old cone kind's job too,
			// and is now the shape's own doing rather than a primitive's.
			{ TEXT("tree_tall"),        Composite({
				Part(MeshPine, FVector(54.f, 56.f, 322.f),         FVector(0.f, 0.f, 322.f), true, ESarkoSurface::Bark),
				Canopy(MeshPineCanopy, FVector(248.f, 226.f, 207.f), FVector(0.f, 0.f, 494.f)),
			}) },

			// The undergrowth tree, scaled 1.95x. Trunk 212 x 160 uu, 0..462 uu
			// (2.5x the pawn) — the widest trunk here, because this mesh's woody
			// half is mostly low branches rather than a bole, and the box that
			// contains them is what the player collides with. Canopy 351 x 308 uu
			// from 293 to 543: 293 uu is the tightest headroom in the table and it
			// is still 1.5x the pawn, which is what keeps the fade a purely visual
			// event.
			{ TEXT("tree_small"),       Composite({
				Part(MeshTreeSmall, FVector(106.f, 80.f, 231.f),         FVector(0.f, 0.f, 231.f), true, ESarkoSurface::Bark),
				Canopy(MeshTreeSmallCanopy, FVector(229.f, 200.f, 125.f), FVector(0.f, 0.f, 418.f)),
			}) },

			// The dead tree: the same species as `tree` with its leaves gone —
			// which is literally true, because Quaternius models its dead trees as
			// the living one's wood half, and CommonTree_Dead_3 and CommonTree_3
			// are the same 944 triangles. Scaled 1.9x rather than 2.2x, so a dead
			// one stands a little shorter than its living neighbours.
			//
			// NO CANOPY at all, which makes it the one tree that never fades — a
			// permanent vertical landmark inside a stand whose roof comes and goes,
			// and the thing that stops a faded clearing from looking like nothing
			// was ever there.
			//
			// It is a SINGLE box now, where it used to be two, so it follows the
			// single-box authoring convention: the sixteen placed dead trees carry
			// pos.z = 285 in bridge.json instead of the 0 they carried as a
			// composite. That is the one map edit in this change, it is sixteen z
			// values, and every one of them puts the tree back on the same ground
			// at the same x and y.
			{ TEXT("tree_dead"),        Box(MeshTreeDead, FVector(77.f, 60.f, 285.f), true, ESarkoSurface::Bark) },

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

TArray<FName> SarkoMap::AllPropKindNames()
{
	TArray<FName> Names;
	KindTable().GetKeys(Names);
	return Names;
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
