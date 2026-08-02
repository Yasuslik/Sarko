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
			/* Dirt       */ { FLinearColor(0.115f, 0.098f, 0.062f),    0.95f },
			// 0.90 rather than a glossier 0.80: measured in the Task 8 overview,
			// glossy asphalt caught the same broad specular sheet the ground
			// roughness exists to avoid, and the highway came out 123/255 against
			// a 141/255 ground — a ribbon you had to look for. Matte asphalt is
			// lit by its base colour instead, which is what §14's "dark" means.
			/* Asphalt    */ { FLinearColor(0.022f, 0.022f, 0.025f),    0.90f },
			/* Concrete   */ { FLinearColor(0.235f, 0.232f, 0.222f),    0.78f },
			/* Structure  */ { SarkoMap::Palette::Structure,            SarkoMap::Palette::StructureRoughness },
			/* Rust       */ { FLinearColor(0.160f, 0.070f, 0.036f),    0.85f },
			/* Timber     */ { FLinearColor(0.185f, 0.100f, 0.055f),    0.80f },
			// 2.4x the ground's luminance, and greyer than the village timber
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
			/* Bark       */ { FLinearColor(0.145f, 0.108f, 0.078f),    0.88f },
			/* Vegetation */ { FLinearColor(0.020f, 0.042f, 0.016f),    0.95f },
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
			/* Water      */ { FLinearColor(0.018f, 0.028f, 0.046f),    0.90f },
			// Lighter than the deep water and lighter than the GROUND — a shallow
			// over pale stones is the one water tone that should read bright, which
			// is what makes a ford legible from a top-down camera 20000 uu up.
			// Near-matte for the same reason Water is: a gloss you cannot control
			// across 400 m is worse than no gloss.
			/* Shallow    */ { FLinearColor(0.045f, 0.062f, 0.085f),    0.90f },
			/* Ravine     */ { FLinearColor(0.013f, 0.013f, 0.010f),    0.93f },
			// THE EDGE SKIRT, near to far. 0.70x, 0.47x and 0.27x the ground's
			// luminance — an even-looking fall-off, not an even numeric one.
			// Matte throughout and getting matter: the outermost band must never
			// catch a specular highlight, because a bright edge is the opposite of
			// the thing being built. All three stay lighter than Ravine.
			/* SkirtNear  */ { FLinearColor(0.032f, 0.036f, 0.020f),    0.93f },
			/* SkirtMid   */ { FLinearColor(0.021f, 0.024f, 0.013f),    0.95f },
			/* SkirtFar   */ { FLinearColor(0.008f, 0.016f, 0.006f),    0.97f },
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
			/* Structure  */ { TEXT("/Game/Generated/Textures/T_Surface_Structure.T_Surface_Structure"),    600.f, 0.40f, 0.24f },
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
			/* Ravine     */ { TEXT("/Game/Generated/Textures/T_Surface_Ravine.T_Surface_Ravine"),         1400.f, 0.35f, 0.10f },
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
