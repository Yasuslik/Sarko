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
	 * Sarko.Config.PropMeshBoundsAreNormalised asserts it rather
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
	// Three of the five meshes the asset pass imported and never placed. They were
	// staged for exactly this: yard clutter with no kind to belong to.
	const FString MeshBarrel = TEXT("/Game/ThirdParty/ZombieApocalypse/Barrel.Barrel");
	const FString MeshPallet = TEXT("/Game/ThirdParty/ZombieApocalypse/Pallet.Pallet");
	const FString MeshPipes = TEXT("/Game/ThirdParty/ZombieApocalypse/Pipes.Pipes");

	/**
	 * The meshes this project builds itself, by Scripts/generate-props.sh.
	 *
	 * They go through the SAME Blender normaliser the downloaded packs do, so
	 * everything the block above says about the -50..50 box is true of these too
	 * — and Sarko.Config.PropMeshBoundsAreNormalised asserts it over both roots
	 * rather than over one.
	 *
	 * The difference is what they cost to author. A downloaded mesh's proportions
	 * are a fact you work around; these were chosen against the extent they were
	 * always going to be given, so ELEVEN OF THE THIRTEEN EXTENTS BELOW ARE
	 * EXACT — the mesh is 14.00 x 3.00 x 4.00 m and the extent is 700 x 150 x 200
	 * uu, and nothing is stretched by so much as a percent. That is the real
	 * argument for generating props rather than only finding them: not that the
	 * shapes are better, but that "distorted prop" stops being a category.
	 */
	const FString MeshCanopyRoof = TEXT("/Game/Generated/Props/GasCanopyRoof.GasCanopyRoof");
	const FString MeshCanopyPillar = TEXT("/Game/Generated/Props/GasCanopyPillar.GasCanopyPillar");
	const FString MeshFuelPump = TEXT("/Game/Generated/Props/FuelPump.FuelPump");
	const FString MeshFreightWagon = TEXT("/Game/Generated/Props/FreightWagon.FreightWagon");
	const FString MeshTankWagon = TEXT("/Game/Generated/Props/TankWagon.TankWagon");
	const FString MeshJerseyBarrier = TEXT("/Game/Generated/Props/JerseyBarrier.JerseyBarrier");
	const FString MeshCrate = TEXT("/Game/Generated/Props/Crate.Crate");
	const FString MeshFenceBroken = TEXT("/Game/Generated/Props/FenceBroken.FenceBroken");
	const FString MeshSandbagStack = TEXT("/Game/Generated/Props/SandbagStack.SandbagStack");
	const FString MeshBarrelFallen = TEXT("/Game/Generated/Props/BarrelFallen.BarrelFallen");
	const FString MeshSpool = TEXT("/Game/Generated/Props/CableSpool.CableSpool");

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
	 * The leafy part of a tree. One of the two places FSarkoPropPart::bCanopy is
	 * set (see Fading below for the other, which is the АЗС roof and is not
	 * foliage), and it hard-wires the two properties a canopy must have:
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

	/**
	 * A part that fades over the player's head but is NOT foliage.
	 *
	 * The АЗС canopy roof is the only user, and it is the reason this exists
	 * beside Canopy() rather than inside it. Both hard-wire the half of the
	 * canopy contract that is a safety property — a fading part never collides,
	 * because the fade changes visibility and NOTHING else — and they differ on
	 * the half that is a fiction: Canopy() forces Vegetation because a canopy
	 * that is not green is not a canopy, and a filling-station roof that IS green
	 * is not a roof.
	 *
	 * Splitting them rather than adding a surface parameter to Canopy() keeps
	 * that guarantee where it was: there is still exactly one way to make a tree
	 * canopy and it still cannot be the wrong colour.
	 */
	FSarkoPropPart Fading(const FString& Mesh, const FVector& Extent, const FVector& Offset,
		ESarkoSurface Surface)
	{
		FSarkoPropPart Result;
		Result.Mesh = FSoftObjectPath(Mesh);
		Result.Extent = Extent;
		Result.Offset = Offset;
		Result.bBlocksMovement = false;
		Result.Surface = Surface;
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
			// A dispenser with a boom arm. 220 uu tall (1.25x the pawn) exactly as
			// before, so the three pumps on the forecourt keep their pos.z of 110
			// and do not move; the footprint went from 120 x 80 to 130 x 68,
			// because the mesh's own proportions put the extra length in the arm
			// that hangs out over where the car stands. That arm is the whole
			// reason this is a mesh: from 1400 uu up a pump body is a rectangle
			// and so is a bin.
			{ TEXT("fuel_pump"),   Box(MeshFuelPump, FVector(65.f, 34.f, 110.f), true, ESarkoSurface::Structure) },
			// The tutorial's climax was parked on twelve grey boxes. It is now an
			// open gondola — the Soviet полувагон, and open because an open wagon
			// gives this camera a rim, ribs and a dark interior where a boxcar
			// gives it a roof. Extent UNCHANGED to the unit (the mesh is 14.00 x
			// 3.00 x 4.00 m against 1400 x 300 x 400 uu, undistorted), so not one
			// of the twelve moved. Rust rather than Structure: rolling stock
			// belongs to the industrial vocabulary, and the siding read as twelve
			// concrete blocks in a field.
			{ TEXT("freight_car"), Box(MeshFreightWagon, FVector(700.f, 150.f, 200.f), true, ESarkoSurface::Rust) },
			// Its opposite number, and the reason the depot now reads as rolling
			// stock rather than as repeated cover: a barrel on saddles with a
			// walkway and a manway dome, 1300 x 280 x 400 uu, undistorted. Three
			// gondolas in a row are one object seen three times; a cylinder parked
			// among them is a train.
			{ TEXT("tank_wagon"),  Box(MeshTankWagon, FVector(650.f, 140.f, 200.f), true, ESarkoSurface::Rust) },
			// The sector's one landmark that the brief names by itself, and it was
			// a grey cylinder. The mesh is a legged tower with a tank on top; the
			// extent is untouched (the tower is 5.4 x 5.3 x 9.4 m and this is
			// 4.4 x 4.4 x 14.0, so it is stretched 8% tall against its width) and
			// the single placed instance did not move.
			{ TEXT("water_tower"), Box(MeshWaterTower, FVector(220.f, 220.f, 700.f), true, ESarkoSurface::Structure) },
			// A solid core with its TOP COURSE modelled as eight separate bags and
			// three more along the front face. Extents byte-identical (360 x 140 x
			// 110 uu, and the mesh is 3.80 x 1.48 x 1.16 m, undistorted), so the
			// twenty-four placed emplacements do not move. Everything below the
			// top course is a box, because everything below the top course is a
			// surface this game's camera never sees.
			{ TEXT("sandbag"),     Box(MeshSandbagStack, FVector(180.f, 70.f, 55.f), true, ESarkoSurface::Structure) },
			// The single biggest count in the table — forty-three of them — and it
			// was a cube. It is now a boarded crate with corner battens, a lid
			// frame and one diagonal brace ACROSS THE LID, which is the only
			// detail of a crate this camera can resolve. Extent unchanged and the
			// mesh is a true 1.4 m cube, so nothing moved and nothing is
			// stretched. Timber rather than Structure: it is made of wood, it
			// stands beside houses and pallets, and forty-three grey boxes were
			// the same grey as the walls they leant on.
			{ TEXT("crate"),       Box(MeshCrate, FVector(70.f, 70.f, 70.f), true, ESarkoSurface::Timber) },
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
			// Now five posts, a top rail, a bottom rail with a hole in it, six
			// surviving boards and one hanging off. Extent unchanged (800 x 24 x
			// 184 uu against a mesh of 8.00 x 0.24 x 1.84 m, undistorted), so the
			// thirteen sections do not move, and it still BLOCKS completely —
			// collision is the mesh's hull. The gaps are for the eye and not for
			// bullets, deliberately: a fence you can shoot through but not walk
			// through is a rule nobody can see.
			{ TEXT("fence_section"),    Box(MeshFenceBroken, FVector(400.f, 12.f, 92.f), true, ESarkoSurface::Timber) },
			// Jersey barrier, 300 uu long and 110 uu tall (0.63x the pawn,
			// waist-high — the real thing is about a metre). Low, heavy, and the
			// pale concrete tone, which is what makes a row of them read as a
			// deliberate closure rather than as scattered junk.
			// Modelled as its real extruded section — wide foot, a hard kick at
			// 34 cm, a gentle taper to a narrow top — plus two lifting lugs. The
			// half-height and half-length are untouched (110 uu tall, 300 long) so
			// the eleven placed barriers neither move nor break the butted rows
			// they were authored into; the half-WIDTH drops 45 -> 28, which is the
			// real thing's 60 cm instead of a 90 cm block, and narrowing a prop
			// can only make the clearance assertions easier.
			{ TEXT("concrete_barrier"), Box(MeshJerseyBarrier, FVector(150.f, 28.f, 55.f), true, ESarkoSurface::Concrete) },

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

			// ---- ТЗ §9's АЗС, as a filling station rather than as a pale pad.
			//
			// THE NOTE ON bridge_gas_canopy_pad SAID THIS COULD NOT EXIST: "A
			// canopy roof cannot exist here: the camera is above and ТЗ §13 says
			// the roof must not hide the player, so the shelter is expressed as
			// its own footprint plus the four pillars in props." That was true
			// when the choices were "solid" and "absent". It stopped being true
			// when the forest landed — FSarkoPropPart::bCanopy hides a part while
			// the local pawn is under it, which is precisely and only what a
			// forecourt roof needs. So the roof is a canopy in the engine's sense
			// of the word, and ТЗ §13's clause is satisfied by the same machinery
			// that satisfies it for a tree.
			//
			// THE ROOF IS 1400 uu SQUARE FOR ONE REASON, and it is arithmetic
			// rather than taste: its half-diagonal is 990 uu, just inside
			// USarkoRaidSettings::CanopyFadeRadiusUU (1000). The fade is measured
			// from the roof's own centre, so a roof any bigger than this has
			// CORNERS A PLAYER CAN STAND UNDER WHILE IT IS STILL DRAWN — which is
			// the one failure mode ТЗ §13 names. The 1800 uu pad it sits on stays
			// as it is and reads as the apron around the canopy.
			//
			// Four pillars carry it from 0 to 500 uu, so there is 2.8x the pawn's
			// height of headroom to walk and fight in; the pillars collide (you
			// cannot walk through a stanchion) and are 90 uu square, which is
			// cover for exactly one shoulder — a filling station is meant to be a
			// dangerous place to stand still. The island is the pump kerb: 24 uu
			// of Concrete, non-colliding, because a 24 uu step that stops a pawn
			// is a snag and not a kerb.
			{ TEXT("gas_canopy"),       Composite({
				Part(MeshCanopyPillar, FVector(45.f, 45.f, 250.f), FVector(-600.f, -600.f, 250.f), true, ESarkoSurface::Concrete),
				Part(MeshCanopyPillar, FVector(45.f, 45.f, 250.f), FVector(600.f, -600.f, 250.f),  true, ESarkoSurface::Concrete),
				Part(MeshCanopyPillar, FVector(45.f, 45.f, 250.f), FVector(-600.f, 600.f, 250.f),  true, ESarkoSurface::Concrete),
				Part(MeshCanopyPillar, FVector(45.f, 45.f, 250.f), FVector(600.f, 600.f, 250.f),   true, ESarkoSurface::Concrete),
				Part(Cube, FVector(75.f, 500.f, 12.f), FVector(0.f, 0.f, 12.f), false, ESarkoSurface::Concrete),
				Fading(MeshCanopyRoof, FVector(700.f, 700.f, 80.f), FVector(0.f, 0.f, 580.f), ESarkoSurface::Concrete),
			}) },

			// ТЗ §14's ВЫВЕСКА, which bridge.json has been carrying as a FLAGGED
			// omission: "a landmark-scale plate needs a composite kind this stage
			// does not add. Flagged, not silently dropped." This is the kind. A
			// 700 uu mast with a 280 x 360 uu board on top of it, reading from 680
			// to 1040 uu — nearly six times the pawn, so it is visible over the
			// forecourt from the road, which is the entire job of a station sign.
			//
			// Built from Cube parts and NOT from a new mesh, on purpose: a flat
			// board seen from a camera looking down is a flat board, there is no
			// silhouette to win, and the three parts below reuse (mesh, surface,
			// collision) combinations the table already draws — so this landmark
			// costs zero additional draw calls. Only the mast collides; the boards
			// hang clear of the pawn's head and must never snag anyone.
			{ TEXT("station_sign"),     Composite({
				Part(Cube, FVector(18.f, 18.f, 350.f),  FVector(0.f, 0.f, 350.f), true,  ESarkoSurface::Structure),
				Part(Cube, FVector(110.f, 16.f, 55.f),  FVector(0.f, 0.f, 620.f), false, ESarkoSurface::Structure),
				Part(Cube, FVector(140.f, 20.f, 180.f), FVector(0.f, 0.f, 860.f), false, ESarkoSurface::Concrete),
			}) },

			// ---- THE YARD. Clutter that dresses rather than clutters: five kinds
			// on three meshes, three of which were imported by the asset pass and
			// have been sitting in Content/ThirdParty unplaced ever since.
			//
			// Two of the five staged meshes are NOT here and that is a decision:
			// Wheels_Stack is 1824 triangles for a 92 uu prop that resolves to a
			// dark blob from 1400 uu, and CinderBlock is 47 cm long, which is
			// about two pixels. Neither is worth a draw call. They stay staged.

			// A 200-litre drum, 114 uu tall — 0.65x the pawn, waist-high cover you
			// shoot over, and the single most legible industrial object there is.
			{ TEXT("barrel"),           Box(MeshBarrel, FVector(35.f, 35.f, 57.f), true, ESarkoSurface::Rust) },
			// The same drum on its side, and it is a separate MESH rather than a
			// rotated part because FSarkoPropPart carries a yaw and nothing else:
			// a barrel cannot be tipped by authoring, so a fallen one has to be
			// modelled fallen or not exist. 90 uu — knee-high, so it breaks up a
			// row of standing drums without pretending to be cover.
			{ TEXT("barrel_fallen"),    Box(MeshBarrelFallen, FVector(58.f, 47.f, 45.f), true, ESarkoSurface::Rust) },
			// One pallet, flat on the ground, and the ONE kind here that does not
			// block: at 14 uu tall a colliding pallet is an invisible trip hazard,
			// and a pallet you walk over is what a pallet is. Pure texture for the
			// dock and the warehouse floor.
			{ TEXT("pallet"),           Box(MeshPallet, FVector(61.f, 45.f, 7.f), false, ESarkoSurface::Timber) },
			// A run of pipe, scaled 1.5x off the imported mesh to 504 uu long and
			// 100 uu tall — 0.57x the pawn. Long, low and horizontal, which is the
			// shape the yard vocabulary was missing: everything else in it is
			// either a box or a drum.
			{ TEXT("pipe_run"),         Box(MeshPipes, FVector(250.f, 56.f, 50.f), true, ESarkoSurface::Rust) },
			// A cable drum standing on its flanges, 220 uu tall (1.25x the pawn —
			// a sight blocker). The one ROUND thing in a vocabulary of rectangles,
			// which is exactly what earns it a draw call: in a yard of wagons,
			// crates and barriers a pair of curved rims is legible at a glance and
			// at any yaw, and yaw is the one thing scattered clutter varies.
			{ TEXT("spool"),            Box(MeshSpool, FVector(105.f, 60.f, 110.f), true, ESarkoSurface::Timber) },

			// Two stacks, and both are composites of a mesh the table already
			// draws — so they add silhouettes without adding a single draw call.
			// That is the whole reason they are composites rather than meshes: a
			// stack of crates is not a new SHAPE, it is the same shape three
			// times, and modelling it again would be paying for repetition.

			// Two crates squared up and a third knocked off to the side. 264 uu at
			// the top, which is 1.5x the pawn: a stack you cannot see over, unlike
			// the single crate, which is why one of these does a different job in
			// a yard than three loose ones.
			{ TEXT("crate_stack"),      Composite({
				Part(MeshCrate, FVector(70.f, 70.f, 70.f), FVector(0.f, 0.f, 70.f),      true, ESarkoSurface::Timber),
				Part(MeshCrate, FVector(62.f, 62.f, 62.f), FVector(12.f, -10.f, 202.f),  true, ESarkoSurface::Timber),
				Part(MeshCrate, FVector(50.f, 50.f, 50.f), FVector(-125.f, 55.f, 50.f),  true, ESarkoSurface::Timber),
			}) },
			// Four pallets, each nudged off the one below, 56 uu at the top. Never
			// blocks, for the same reason a single pallet never does. ТЗ §9's
			// «паллеты» at the rail depot are this.
			{ TEXT("pallet_stack"),     Composite({
				Part(MeshPallet, FVector(61.f, 45.f, 7.f), FVector(0.f, 0.f, 7.f),     false, ESarkoSurface::Timber),
				Part(MeshPallet, FVector(61.f, 45.f, 7.f), FVector(-8.f, 6.f, 21.f),   false, ESarkoSurface::Timber),
				Part(MeshPallet, FVector(61.f, 45.f, 7.f), FVector(5.f, -9.f, 35.f),   false, ESarkoSurface::Timber),
				Part(MeshPallet, FVector(61.f, 45.f, 7.f), FVector(-3.f, 4.f, 49.f),   false, ESarkoSurface::Timber),
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
