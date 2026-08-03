#pragma once

#include "CoreMinimal.h"

#include "SarkoMapPalette.generated.h"

/**
 * What a piece of geometry is made of, for colour purposes only. Not a physical
 * material: nothing queries this for friction or footstep sounds, and the
 * project ships one material (BasicShapeMaterial) with a colour parameter.
 *
 * The list is ТЗ §14's palette plus the two surfaces the ravine needs. Keep
 * Count last: Sarko.Config.SurfacePaletteIsReadable loops to it, so a new
 * surface with no colour, no roughness or no name fails a test instead of
 * shipping as black.
 */
UENUM()
enum class ESarkoSurface : uint8
{
	/** Bare earth. The one thing everything else must be distinguishable from. */
	Ground,
	/** Dirt track — lighter than the ground, per §14. */
	Dirt,
	/** Highway and yards — dark, per §14. */
	Asphalt,
	/** Bridge deck, kerbs, pipe bases, sign plates — the pale contrast tone. */
	Concrete,
	/** Generic built grey: walls, wrecks, crates. The neutral of the frame. */
	Structure,
	/** Industry (§14 "промзона ржавая"): tanks, freight cars, trailers, pylon legs. */
	Rust,
	/** §14's warm village tone. Doubles as timber: roofs, fences, logs, sheds. */
	Timber,
	/**
	 * Tree trunks, and only tree trunks.
	 *
	 * Timber was the obvious candidate and is the wrong one: it is the *village*
	 * tone — a bright sawn-plank orange-brown that reads, correctly, as something
	 * a person built. A stand of two hundred trunks in it looks like a timber
	 * yard from above. Bark is the same family, desaturated toward grey, so a
	 * stand reads as wood without reading as lumber.
	 *
	 * It is 2.75x the ground's luminance and that is not generosity. A trunk is
	 * VERTICAL and the sun here is 55 degrees up, so a trunk's sides catch a
	 * fraction of what the flat ground catches; the first version of this colour
	 * cleared every luminance rule on paper at 1.4x and rendered as black posts
	 * with pale lit caps. Surfaces that stand up need more contrast than
	 * surfaces that lie down.
	 */
	Bark,
	/** Bushes and the treeline boundary. Dark green, deliberately darker than the ground. */
	Vegetation,
	/** The ravine's water. Dark blue-grey and OPAQUE — see the Water constant. */
	Water,
	/**
	 * The ford's «мелкая вода» (ТЗ §6). Lighter than Water and laid on top of it,
	 * so a crossing reads as a bright band in a dark gorge instead of as a gap in
	 * a cliff. Also opaque — the limitation Water documents applies here too, and
	 * this surface is what makes "shallow" expressible at all without a material.
	 */
	Shallow,
	/** The ravine bed. The visual stand-in for depth the map does not physically dig. */
	Ravine,

	/**
	 * THE EDGE SKIRT'S THREE BANDS, near to far. They exist because the sector's
	 * floor is 40000 x 40000 uu and then simply stops: from a pawn standing at
	 * the north border the frame was ground, a treeline, and BLACK — not the end
	 * of a world, the end of a data file.
	 *
	 * Three tones rather than one because a single darker slab beyond the border
	 * reads as a second field, and a gradient reads as distance. Each is about
	 * 0.70x, 0.47x and 0.27x the ground's luminance, so the fall-off is roughly
	 * even in perceived brightness rather than in linear value.
	 *
	 * They stay ABOVE Ravine, which keeps "the ravine bed is the darkest thing in
	 * the sector" true — the gorge must not be out-darkened by scenery a player
	 * can never walk on.
	 *
	 * All three are Vegetation's hue family (olive going grey), not Vegetation
	 * itself: the treeline masses standing ON the skirt are Vegetation, and
	 * ground and trees sharing one tone is exactly the flattening the bands are
	 * there to avoid.
	 */
	SkirtNear,
	SkirtMid,
	SkirtFar,

	/** Extraction pads. The one saturated world colour, and it is a gameplay marker. */
	Extraction,

	Count UMETA(Hidden)
};

namespace SarkoMap
{
	/**
	 * The sector palette, ТЗ §14: ground muted green-brown, everything built on
	 * it a tone that reads as a different material from above.
	 *
	 * These are linear-space base colours, not sRGB swatches — they are fed
	 * straight into a material's BaseColor, which is linear. An sRGB value
	 * pasted here comes out roughly twice as bright as intended.
	 *
	 * The named constants are kept (rather than folded into the lookup) because
	 * Sarko.Config.PaletteSeparatesGroundFromCover asserts against them by
	 * name, and because "which two colours is the whole readability argument
	 * about" deserves to be answerable without reading a table.
	 */
	namespace Palette
	{
		/**
		 * Ground: desaturated olive/khaki. Dark enough that grey cover sits on
		 * top of it rather than dissolving into it — which is exactly what
		 * happened before, when floor and cover both wore the engine grid
		 * material and measured (156,155,151) against (158,157,153) in the same
		 * frame. Three levels apart is not cover.
		 */
		const FLinearColor Ground(0.046f, 0.051f, 0.028f);

		/**
		 * Cover and props: neutral grey, deliberately much lighter than the ground.
		 *
		 * 0.106 rather than the 0.150 this shipped at, and the reason is arithmetic
		 * rather than taste. Structure and Concrete are BOTH near-neutral by
		 * definition — one is "the neutral of the frame" and the other is "the pale
		 * contrast tone" — so the only thing that can separate them is lightness,
		 * and Concrete cannot climb: the muted ceiling caps it and at its old value
		 * the depot pad already measured 223/255 in a real frame. That put the two
		 * greys 4.96 dE00 apart, inside the wobble a single surface's own detail
		 * map produces, and the pad and the pale boxes standing on it measured
		 * (223,223,221) against (228,230,214). Dropping the neutral is what buys
		 * the gap; it is still 2.19x the ground's luminance, so cover still stands
		 * off the field it sits on.
		 */
		const FLinearColor Structure(0.106f, 0.105f, 0.111f);

		/** Ground roughness. Near-matte, so a 400 m plane cannot catch a specular sheet. */
		constexpr float GroundRoughness = 0.92f;

		/** Cover roughness. Slightly glossier than the ground, which helps the edges catch light. */
		constexpr float StructureRoughness = 0.75f;

		/** The colour of one surface. Never a default — every value is listed. */
		const FLinearColor& ColourFor(ESarkoSurface Surface);

		/** The roughness of one surface. */
		float RoughnessFor(ESarkoSurface Surface);

		/**
		 * THE DETAIL MAP OF A SURFACE, or nullptr for a surface that is
		 * deliberately flat.
		 *
		 * These are greyscale tiling maps built by Scripts/generate-textures.sh
		 * and they are NOT base colour: the colour above is still the only
		 * source of a surface's colour, and the map modulates it around that
		 * value. Every generated image is normalised to a mean of exactly 0.5,
		 * which the material turns into a mean multiplier of exactly 1.0 — so
		 * the AVERAGE of any patch of a textured surface is the palette colour
		 * to within a rounding error, and re-running the generator with a
		 * different seed cannot move a surface's identity.
		 *
		 * nullptr is a decision, not an omission. Water and Shallow are the
		 * ford's read from 20000 uu up and a graphic band is what makes it
		 * legible; Extraction is a gameplay marker and the one saturated colour
		 * in the sector; the three skirt bands are an even luminance fall-off
		 * standing in for distance, and putting texture on them replaces
		 * "further away" with "another field". A flat surface keeps the exact
		 * material it had before textures existed — /Engine/BasicShapes — so it
		 * pays no sample and cannot have regressed.
		 */
		const TCHAR* DetailTexturePath(ESarkoSurface Surface);

		/**
		 * How many world units one tile of that map covers. World units, not UV
		 * repeats: the material derives its UVs from world position (see
		 * Scripts/import-textures.py) precisely so texel density is a property
		 * of the sector rather than of whichever mesh a surface landed on — the
		 * floor and a crate are the same engine cube at scales 400 apart.
		 *
		 * Chosen against the judging camera, not against a close-up: the spring
		 * arm is 1400 uu at -70 degrees, which puts about 2.5 uu under a pixel,
		 * so a tile between 200 and 1600 uu puts its features in the 5-to-150
		 * pixel band where they read as material instead of as noise or as a
		 * stain. Zero for a flat surface.
		 */
		float DetailTileUU(ESarkoSurface Surface);

		/**
		 * How far the map may push base colour, as a fraction: the multiplier is
		 * 1 - Strength at a black texel and 1 + Strength at a white one. The maps
		 * have a standard deviation near 0.2, so a typical texel lands within
		 * about 0.4 x Strength of the palette colour and the extremes are rare.
		 * Zero for a flat surface.
		 */
		float DetailStrength(ESarkoSurface Surface);

		/**
		 * How far the map may push roughness, in absolute roughness units across
		 * the map's full range — so the swing is +-half of this.
		 *
		 * Deliberately small everywhere. RoughnessFor's numbers are not taste:
		 * they are what stops a 40000 uu plane catching a single specular sheet
		 * across the whole sector (see the Water and Asphalt comments in the
		 * .cpp), and a detail map is not a licence to undo that. Zero for a flat
		 * surface.
		 */
		float DetailRoughnessSwing(ESarkoSurface Surface);
	}

	/**
	 * The generated surface material, built node-by-node by
	 * Scripts/import-textures.py because this project has no editor session in
	 * which to author one by hand.
	 *
	 * It is a superset of /Engine/BasicShapes/BasicShapeMaterial: same "Color"
	 * and "Roughness" parameters, plus a detail sample whose strengths default
	 * to zero. A missing asset is survivable — SharedSurfaceMaterial falls back
	 * to the engine material and the sector renders exactly as it did before —
	 * which is why every call site below still speaks in colours.
	 */
	extern const TCHAR* const SurfaceMaterialPath;

	/** Maps a JSON surface name to the enum. False for anything unlisted. */
	bool ParseSurfaceName(const FString& Name, ESarkoSurface& Out);

	/** The JSON name of a surface — the inverse of ParseSurfaceName. */
	FString SurfaceName(ESarkoSurface Surface);
}
