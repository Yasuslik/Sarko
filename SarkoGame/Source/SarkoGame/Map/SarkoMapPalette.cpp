#include "Map/SarkoMapPalette.h"

namespace
{
	/**
	 * Every surface's linear base colour and roughness, in enum order.
	 *
	 * The numbers are chosen against each other, not in isolation. Read
	 * Sarko.Config.SurfacePaletteIsReadable alongside this table: it encodes
	 * every relation that matters (dirt above ground, asphalt below it, deck
	 * far above asphalt, water blue and below ground, treeline green and below
	 * ground, ravine below water, nothing but the extraction saturated).
	 *
	 * AND, since the separation pass, one relation that is about the table as a
	 * whole rather than about any row of it: every pair of surfaces that can
	 * appear in the same frame must be at least 10 dE00 apart at the shipping
	 * camera. That number is not a preference either. The detail maps modulate
	 * each surface by +-0.4 x Strength around its palette colour, which is up to
	 * 6.5 dE00 of variation WITHIN one surface — so two surfaces closer together
	 * than that are inside each other's texture noise, and 10 is the nearest
	 * round number that clears it by half again.
	 *
	 * WHAT THE RULE CAUGHT. Eight pairs failed it, and the worst of them is the
	 * one a frame shows immediately: at the rail depot, Rust and Timber measured
	 * 5.90 apart, and a real 1600x900 frame put a crate top at (212,187,161)
	 * against a wagon top at (211,172,139). Wagons, crates, pallets, spools and
	 * barrels were one brown mass distinguished by silhouette alone. Bark sat on
	 * top of both (6.17 from Timber), the two greys sat on top of each other
	 * (4.96), and the ford's water was 9.27 from the asphalt of the road that
	 * crosses it.
	 *
	 * WHAT THE FIX IS. The warm end of this palette had four surfaces — Rust,
	 * Bark, Timber, Dirt — packed into about 30 degrees of hue and 10 units of
	 * lightness. There is no arrangement of four colours in that box that works,
	 * so the pass SPREAD them along both axes instead of nudging them along one:
	 * Rust went DOWN, Timber went up and yellow, Bark took the gap between them
	 * at low chroma, and Dirt left the warm family altogether for the olive one
	 * it actually belongs to — a track is the sector's own soil, walked pale, not
	 * a plank. The two greys separated by lightness because lightness is the only
	 * thing two greys have.
	 *
	 * Rust went down rather than sideways on the SECOND attempt, and the Rust row
	 * below is where that argument is written out. The first attempt separated it
	 * from Timber by hue, kept it bright, passed every number here, and rendered
	 * the whole industrial half of the sector pink. Hue is also the axis the enemy
	 * tint occupies, which is the second reason not to spend it: the surfaces are
	 * told apart by how dark they are, and the characters keep the colour.
	 *
	 * The world is no louder for it: the loudest world surface is Vegetation at
	 * chroma 25.6 against the enemy tint's 27.4 (it was 26.2 before), and the
	 * nearest any surface comes to a character is Bark at 13.3 from the red and
	 * Concrete at 14.5 from the blue. A scav still pops — and after the Rust fix
	 * the second-nearest to the red is Structure at 20.9, where it used to be
	 * Rust itself at 13.8.
	 */
	struct FSurfaceStyle
	{
		FLinearColor Colour;
		float Roughness;
	};

	const FSurfaceStyle& StyleFor(ESarkoSurface Surface)
	{
		static const FSurfaceStyle Styles[static_cast<int32>(ESarkoSurface::Count)] = {
			/* Ground     */ { SarkoMap::Palette::Ground,               SarkoMap::Palette::GroundRoughness },
			// OLIVE, not tan, and that is the separation pass's one change of
			// family. A dirt track is the sector's own soil with the grass walked
			// off it, so it belongs to the ground's hue and not to the village
			// timber's — which is where it used to sit, 8.94 from Timber and 5.23
			// from Bark, close enough that a road through a wood read as more wood.
			// Now it is the ground one stop paler: same hue to within two degrees,
			// 2.08x the luminance. Sandbags wear this too (see SarkoMapKinds.cpp),
			// which is what raised its co-occurrence with Structure from 36 to 205
			// and made the move worth making rather than merely defensible.
			/* Dirt       */ { FLinearColor(0.094f, 0.106f, 0.062f),    0.95f },
			// 0.90 rather than a glossier 0.80: measured in the Task 8 overview,
			// glossy asphalt caught the same broad specular sheet the ground
			// roughness exists to avoid, and the highway came out 123/255 against
			// a 141/255 ground — a ribbon you had to look for. Matte asphalt is
			// lit by its base colour instead, which is what §14's "dark" means.
			/* Asphalt    */ { FLinearColor(0.022f, 0.022f, 0.025f),    0.90f },
			// Up from 0.235, and only as far as it can go: this is the surface the
			// Structure comment in the header is arguing with, and it is the one
			// that could not move much. The deck, the barriers and the depot pad
			// already read near-white in a frame, so the gap between the two greys
			// had to be bought mostly at the other end.
			/* Concrete   */ { FLinearColor(0.276f, 0.281f, 0.246f),    0.78f },
			/* Structure  */ { SarkoMap::Palette::Structure,            SarkoMap::Palette::StructureRoughness },
			// OXIDISED IRON: DARK, WARM, LOW CHROMA. Separated from Timber by
			// VALUE, which is the honest axis — rust is dark and weathered timber
			// is pale — and not by hue, which is the axis the previous attempt used
			// and the axis the enemy tint lives on.
			//
			// That attempt (0.143/0.055/0.044, hue 38, 1.51x the ground) cleared
			// every number in the test below and still shipped a PINK sector. Two
			// separate reasons, both visible in a frame and neither visible in the
			// table:
			//
			//  * A PROP IS LIT BRIGHTER THAN THE MODEL PREDICTS. ScreenLab is
			//    calibrated against the GROUND, where it is accurate to one 255th.
			//    A prop is not the ground: in the depot frame a wagon top predicted
			//    at (172,127,117) measured (208,163,150), about 1.4x in linear
			//    display terms, because a raised horizontal face is neither
			//    ambient-occluded by the field around it nor partly shadowed by the
			//    treeline. Every palette entry therefore reads a stop or so lighter
			//    on a prop than in the table, and a colour chosen at the edge of
			//    "warm brown" lands well inside "salmon".
			//  * AT L* 58 THERE IS NO SUCH THING AS BROWN. Brown is a dark orange;
			//    at that lightness, hue 38 and chroma 20 is the colour of red-lead
			//    primer only in the tin. On screen it was the wagons, the eight
			//    industrial houses and the depot walls in pale warm pink — a
			//    surface family that does not exist in an east-European wasteland,
			//    and that shared a hue with the one thing allowed to shout. In a
			//    frame with three scavs in it a lit scav torso measured (239,186,175)
			//    against a lit wagon top at (210,166,153): the loudest thing on
			//    screen and the largest thing on screen, ten units apart.
			//
			// So: L* 45 rather than 58, hue 50, chroma 21.8 (DOWN from the 20.1 the
			// tin said, once the lightness came out of it), rendering to about
			// (163,116,92) on a lit face. Rust/Timber is 29.4 dE00 — better than the
			// 22.6 the hue split bought — and 27.8 of that is lightness. Rust is
			// now 26.1 dE00 from the enemy red instead of 13.8, which is the whole
			// point: the reddest thing in the sector must be a person.
			//
			// THE ONE THING GIVEN UP is §14's "rust is brighter than the ground"
			// (it was 1.51x, it is 0.72x). That clause was a proxy for "a wagon
			// separates from the field", and the pairwise sweep now measures that
			// directly and answers 23.6 dE00 — up from 26.1's worth of a colour
			// nobody believed. The floor that replaces it is the one the clause was
			// really protecting: a Rust face standing UP is lit at roughly a
			// quarter of a face lying flat (49/208 in the same frame), so this may
			// not be dropped much further without the depot walls going black. See
			// the Bark comment for the same lesson learned the expensive way.
			/* Rust       */ { FLinearColor(0.067f, 0.027f, 0.017f),    0.85f },
			// LIGHTER AND YELLOWER, which is what sawn softwood actually is. It
			// went the opposite way to Rust on purpose — separating a pair by
			// moving both is cheaper in aesthetic damage than dragging one of them
			// twice as far — and it is now the palest chromatic surface in the
			// sector at 3.42x the ground, hue 92 against Rust's 38.
			/* Timber     */ { FLinearColor(0.225f, 0.157f, 0.068f),    0.80f },
			// Well above the ground's luminance, and greyer than the village timber
			// rather than darker — which is the second attempt. The first was
			// 1.4x and a warm brown, chosen on the numbers, and in a frame it
			// produced black posts with pale lit caps: a trunk is VERTICAL, and
			// this sector's sun sits 55 degrees up, so a trunk's sides receive a
			// fraction of the light the flat ground does. Two surfaces that
			// separate on paper do not separate when one of them is a wall.
			//
			// Greyer rather than simply brighter because a brighter warm brown
			// lands on top of Timber, and "the fence tone" and "the tree tone"
			// being the same colour is exactly the distinction this surface was
			// added to make.
			//
			// The separation pass moved it UP, from 2.35x the ground to 2.75x, and
			// kept the greyness. Timber climbing to 0.225 would otherwise have
			// closed on it from above (they were already 6.17 apart, which is why
			// a fence in a wood read as a fallen trunk), and the argument above
			// says which way this one is allowed to go: a vertical surface may be
			// made brighter and may not be made darker.
			/* Bark       */ { FLinearColor(0.198f, 0.118f, 0.087f),    0.88f },
			// A shade darker and a shade cooler. The treeline masses stand ON
			// SkirtNear, and at the shipped values the two were 9.22 apart — the
			// wall and the ground beyond the border were the same tone, which is
			// exactly the flattening the skirt bands were added to prevent. It is
			// still green-dominant and still darker than the ground it borders.
			/* Vegetation */ { FLinearColor(0.018f, 0.037f, 0.014f),    0.95f },
			// Opaque, and that is a shipped limitation rather than a choice: a
			// translucent material cannot exist here without authoring an asset
			// (spec §5.2). Dark and blue is enough for the read from above.
			//
			// NEAR-MATTE (0.90), which is not what water looks like and is the
			// point. At 0.55 the 40000 uu water slab behaved exactly like the
			// specular sheet Palette::GroundRoughness exists to prevent: in the
			// Task 8 overview it measured (178,182,188) at x=+10000 — the
			// BRIGHTEST large surface in the frame and no longer blue — while
			// reading correctly at (105,119,138) in the west, i.e. the ravine's
			// tone depended on where the sun happened to be. A gloss you cannot
			// control across 400 m is worse than no gloss.
			//
			// Bluer since the separation pass (0.013/0.028/0.050 rather than
			// 0.018/0.028/0.046): the bridge crosses the ravine, so the deep water
			// and the highway's asphalt are in the same frame, and two dark
			// near-greys 9.27 apart is a gorge that reads as more road.
			/* Water      */ { FLinearColor(0.013f, 0.028f, 0.050f),    0.90f },
			// Lighter than the deep water and lighter than the GROUND — a shallow
			// over pale stones is the one water tone that should read bright, which
			// is what makes a ford legible from a top-down camera 20000 uu up.
			// Near-matte for the same reason Water is: a gloss you cannot control
			// across 400 m is worse than no gloss.
			/* Shallow    */ { FLinearColor(0.048f, 0.060f, 0.087f),    0.90f },
			// DEEPER, and neutral. The gorge bed and the highway were 6.80 apart
			// and both of them are near-grey, so nothing but lightness could ever
			// have told them apart — and there were only eight units of it. At
			// 0.008 luminance it is still lit rather than black (the floor is
			// 0.005) and it is now 1.7x below the outermost skirt band instead of
			// 1.06x, which finally makes "the darkest thing in the sector" a
			// statement about the picture and not just about the table.
			/* Ravine     */ { FLinearColor(0.009f, 0.008f, 0.008f),    0.93f },
			// THE EDGE SKIRT, near to far. 0.70x, 0.47x and 0.27x the ground's
			// luminance — an even-looking fall-off, not an even numeric one.
			// Matte throughout and getting matter: the outermost band must never
			// catch a specular highlight, because a bright edge is the opposite of
			// the thing being built. All three stay lighter than Ravine.
			// The ratios are untouched (0.700 / 0.492 / 0.287 of the ground); only
			// the hue moved, a little grey and a little cool, so the treeline
			// standing on the near band separates from it.
			/* SkirtNear  */ { FLinearColor(0.033f, 0.035f, 0.024f),    0.93f },
			/* SkirtMid   */ { FLinearColor(0.023f, 0.025f, 0.014f),    0.95f },
			/* SkirtFar   */ { FLinearColor(0.009f, 0.016f, 0.007f),    0.97f },
			// Mirrors ASarkoExtractionZone's pad tint so the two cannot drift.
			/* Extraction */ { FLinearColor(0.160f, 0.620f, 0.240f),    0.70f },
		};

		const int32 Index = static_cast<int32>(Surface);
		// Count (or anything cast in from outside) falls back to the neutral
		// rather than reading past the array: a wrong colour is a bug you can
		// see, an out-of-bounds read is one you cannot.
		if (Index < 0 || Index >= static_cast<int32>(ESarkoSurface::Count))
		{
			return Styles[static_cast<int32>(ESarkoSurface::Structure)];
		}
		return Styles[Index];
	}

	/**
	 * THE DETAIL MAPS, in enum order, and a separate table from the one above on
	 * purpose.
	 *
	 * The colours and roughnesses are the palette's original claim — the thing
	 * Sarko.Config.SurfacePaletteIsReadable measures and the thing every
	 * readability argument in this file rests on. Detail is a MODULATION of that
	 * claim and nothing more: the maps are normalised to a mean of 0.5, which
	 * makes the mean multiplier 1.0, which makes the average of a textured
	 * surface exactly the colour on the line above. Folding four more fields
	 * into that table would have buried thirty lines of reasoning about
	 * luminance ratios under tiling numbers that have nothing to do with them.
	 *
	 * Six of the sixteen surfaces have no map, and each of those is a decision
	 * argued in the header. They keep /Engine/BasicShapes/BasicShapeMaterial
	 * exactly as before, so they pay no texture sample and cannot have changed.
	 *
	 * The tiling numbers are world units per tile, chosen against the top-down
	 * camera's ~2.5 uu per pixel: the surfaces that cover ground area tile
	 * slowly (a 1600 uu ground tile is most of a screen, so no repetition is
	 * visible in a frame) and the surfaces that clothe objects a few metres
	 * across tile fast enough that two crates sample different parts of the map.
	 */
	struct FSurfaceDetail
	{
		/** nullptr means "flat, and meant to be". */
		const TCHAR* TexturePath;
		/** World units per tile. */
		float TileUU;
		/** Base-colour swing, as a fraction of the palette colour. */
		float Strength;
		/**
		 * Roughness swing across the map's full range; the actual deviation is
		 * half this. Every value here is chosen so that Roughness +- half the
		 * swing stays inside [0.5, 1.0] — matte at the glossiest end, legal at
		 * the rough end. Sarko.Config.SurfaceDetailIsBounded checks the
		 * arithmetic; it caught dirt at 1.01, which the material would have
		 * silently clamped and nobody would ever have seen.
		 */
		float RoughnessSwing;
	};

	const FSurfaceDetail& DetailFor(ESarkoSurface Surface)
	{
		static const FSurfaceDetail None{ nullptr, 0.f, 0.f, 0.f };
		static const FSurfaceDetail Details[static_cast<int32>(ESarkoSurface::Count)] = {
			// Dirt clumps and sparse grass tufts over 400 m of field. 1600 uu is
			// the largest tile here because this is the surface a player sees
			// most and for longest, and a tile smaller than a screen is a
			// wallpaper pattern.
			/* Ground     */ { TEXT("/Game/Generated/Textures/T_Surface_Ground.T_Surface_Ground"),         1600.f, 0.45f, 0.10f },
			/* Dirt       */ { TEXT("/Game/Generated/Textures/T_Surface_Dirt.T_Surface_Dirt"),             1200.f, 0.40f, 0.08f },
			// The strongest base-colour swing in the table, because asphalt has
			// the least colour to lose: at a linear 0.022 it is nearly black, so
			// the only thing that can distinguish worn from unworn tarmac is a
			// ratio.
			/* Asphalt    */ { TEXT("/Game/Generated/Textures/T_Surface_Asphalt.T_Surface_Asphalt"),       1000.f, 0.55f, 0.18f },
			// And the mirror case: concrete is the palest surface in the sector,
			// so the same fraction is a much larger absolute step. Held back.
			/* Concrete   */ { TEXT("/Game/Generated/Textures/T_Surface_Concrete.T_Surface_Concrete"),      800.f, 0.35f, 0.20f },
			/* Structure  */ { TEXT("/Game/Generated/Textures/T_Surface_Structure.T_Surface_Structure"),    600.f, 0.30f, 0.24f },
			/* Rust       */ { TEXT("/Game/Generated/Textures/T_Surface_Rust.T_Surface_Rust"),              500.f, 0.50f, 0.24f },
			/* Timber     */ { TEXT("/Game/Generated/Textures/T_Surface_Timber.T_Surface_Timber"),          400.f, 0.40f, 0.16f },
			// 220 uu, the fastest tile in the table: a trunk is about 60 uu
			// across, so this wraps roughly three times around one — which is
			// what puts a countable number of fibres on it rather than one
			// smeared band. The strength is the highest here for the reason the
			// Bark colour comment gives at length: a vertical surface under a
			// 55-degree sun receives a fraction of the light a flat one does, so
			// the same modulation reads as less.
			/* Bark       */ { TEXT("/Game/Generated/Textures/T_Surface_Bark.T_Surface_Bark"),              220.f, 0.55f, 0.12f },
			/* Vegetation */ { TEXT("/Game/Generated/Textures/T_Surface_Vegetation.T_Surface_Vegetation"),  700.f, 0.45f, 0.08f },
			// Water and Shallow: flat, and that is the ford's whole read. See
			// the header.
			/* Water      */ None,
			/* Shallow    */ None,
			// Ravine: flat, and this one was DECIDED BY A FRAME rather than argued
			// in advance. It had a map, it shipped through the whole pipeline, and
			// the gorge bed measured 6.45 units of local detail before it and 6.55
			// after — nothing. At a linear 0.013 it is the darkest surface in the
			// sector on purpose, so a 35% swing is 0.005 of linear brightness. The
			// texture is gone and the picture is identical.
			/* Ravine     */ None,
			// The three skirt bands: flat. They are a luminance gradient doing
			// the job of distance, and texture on them is a second field.
			/* SkirtNear  */ None,
			/* SkirtMid   */ None,
			/* SkirtFar   */ None,
			// The one saturated colour in the sector, and it is UI. Flat.
			/* Extraction */ None,
		};

		const int32 Index = static_cast<int32>(Surface);
		if (Index < 0 || Index >= static_cast<int32>(ESarkoSurface::Count))
		{
			return None;
		}
		return Details[Index];
	}

	/** JSON names, in enum order. Lower snake case, like every other key. */
	const TCHAR* const SurfaceNames[static_cast<int32>(ESarkoSurface::Count)] = {
		TEXT("ground"), TEXT("dirt"), TEXT("asphalt"), TEXT("concrete"), TEXT("structure"),
		TEXT("rust"), TEXT("timber"), TEXT("bark"), TEXT("vegetation"), TEXT("water"),
		TEXT("shallow"), TEXT("ravine"),
		TEXT("skirt_near"), TEXT("skirt_mid"), TEXT("skirt_far"),
		TEXT("extraction")
	};
}

const FLinearColor& SarkoMap::Palette::ColourFor(ESarkoSurface Surface)
{
	return StyleFor(Surface).Colour;
}

float SarkoMap::Palette::RoughnessFor(ESarkoSurface Surface)
{
	return StyleFor(Surface).Roughness;
}

const TCHAR* SarkoMap::Palette::DetailTexturePath(ESarkoSurface Surface)
{
	return DetailFor(Surface).TexturePath;
}

float SarkoMap::Palette::DetailTileUU(ESarkoSurface Surface)
{
	return DetailFor(Surface).TileUU;
}

float SarkoMap::Palette::DetailStrength(ESarkoSurface Surface)
{
	return DetailFor(Surface).Strength;
}

float SarkoMap::Palette::DetailRoughnessSwing(ESarkoSurface Surface)
{
	return DetailFor(Surface).RoughnessSwing;
}

const TCHAR* const SarkoMap::SurfaceMaterialPath =
	TEXT("/Game/Generated/Materials/M_SarkoSurface.M_SarkoSurface");

bool SarkoMap::ParseSurfaceName(const FString& Name, ESarkoSurface& Out)
{
	for (int32 Index = 0; Index < static_cast<int32>(ESarkoSurface::Count); ++Index)
	{
		if (Name.Equals(SurfaceNames[Index], ESearchCase::IgnoreCase))
		{
			Out = static_cast<ESarkoSurface>(Index);
			return true;
		}
	}
	return false;
}

FString SarkoMap::SurfaceName(ESarkoSurface Surface)
{
	const int32 Index = static_cast<int32>(Surface);
	if (Index < 0 || Index >= static_cast<int32>(ESarkoSurface::Count))
	{
		return FString();
	}
	return SurfaceNames[Index];
}
