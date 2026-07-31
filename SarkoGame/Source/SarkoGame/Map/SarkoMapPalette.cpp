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

	/** JSON names, in enum order. Lower snake case, like every other key. */
	const TCHAR* const SurfaceNames[static_cast<int32>(ESarkoSurface::Count)] = {
		TEXT("ground"), TEXT("dirt"), TEXT("asphalt"), TEXT("concrete"), TEXT("structure"),
		TEXT("rust"), TEXT("timber"), TEXT("vegetation"), TEXT("water"), TEXT("shallow"),
		TEXT("ravine"), TEXT("extraction")
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
