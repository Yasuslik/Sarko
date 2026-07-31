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
	 * yard from above, and worse, it makes a forest the brightest thing in the
	 * north when the forest should be where the frame goes quiet. Bark is the
	 * same hue family, duller and darker, so a trunk still separates from the
	 * ground (1.4x its luminance) without competing with the buildings.
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

		/** Cover and props: neutral grey, deliberately much lighter than the ground. */
		const FLinearColor Structure(0.150f, 0.150f, 0.155f);

		/** Ground roughness. Near-matte, so a 400 m plane cannot catch a specular sheet. */
		constexpr float GroundRoughness = 0.92f;

		/** Cover roughness. Slightly glossier than the ground, which helps the edges catch light. */
		constexpr float StructureRoughness = 0.75f;

		/** The colour of one surface. Never a default — every value is listed. */
		const FLinearColor& ColourFor(ESarkoSurface Surface);

		/** The roughness of one surface. */
		float RoughnessFor(ESarkoSurface Surface);
	}

	/** Maps a JSON surface name to the enum. False for anything unlisted. */
	bool ParseSurfaceName(const FString& Name, ESarkoSurface& Out);

	/** The JSON name of a surface — the inverse of ParseSurfaceName. */
	FString SurfaceName(ESarkoSurface Surface);
}
