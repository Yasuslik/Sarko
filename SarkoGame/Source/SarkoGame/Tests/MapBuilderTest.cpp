#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/StaticMesh.h"
#include "Map/SarkoMapBuilder.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/SoftObjectPath.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/**
	 * THE PALETTE, AS THE PLAYER ACTUALLY RECEIVES IT.
	 *
	 * A palette entry is a LINEAR base colour, and comparing two of those tells
	 * you almost nothing: linear values are not perceptually spaced, and the
	 * frame the player sees has been through the exposure and the tonemapper
	 * first. Ground and Dirt differ by 0.069 of linear green and by 13 units of
	 * L*; Concrete and Structure differ by 0.085 of linear and by 15. The same
	 * arithmetic distance, two very different pictures.
	 *
	 * So the checks below measure the SCREEN colour: exposure, a compressive
	 * tonemap, sRGB encoding, then CIELAB. The exposure is not a guess — at 5.0
	 * this model puts the ground at (119,124,98) and a real 1600x900 frame from
	 * the shipping camera measures the ground at (118,124,98). Rust and Timber
	 * are predicted 5.90 apart and a wagon and a crate in that same frame measure
	 * 5.03. The model tracks the renderer to about 1.5 dE00, which is the margin
	 * the threshold below is chosen to survive.
	 */
	constexpr float PaletteExposure = 5.0f;

	FVector ScreenLab(const FLinearColor& Linear)
	{
		const auto Encode = [](float Channel)
		{
			const float Exposed = PaletteExposure * Channel;
			const float Tonemapped = Exposed / (1.f + Exposed);   // compressive shoulder
			const float Clamped = FMath::Clamp(Tonemapped, 0.f, 1.f);
			return Clamped <= 0.0031308f ? 12.92f * Clamped
										 : 1.055f * FMath::Pow(Clamped, 1.f / 2.4f) - 0.055f;
		};
		// Encode to sRGB and decode again: the round trip is what applies the
		// display transfer function, which is the thing CIELAB is defined on.
		const auto Decode = [](float Channel)
		{
			return Channel <= 0.04045f ? Channel / 12.92f
									   : FMath::Pow((Channel + 0.055f) / 1.055f, 2.4f);
		};
		const float R = Decode(Encode(Linear.R));
		const float G = Decode(Encode(Linear.G));
		const float B = Decode(Encode(Linear.B));

		const float X = (0.4124f * R + 0.3576f * G + 0.1805f * B) / 0.95047f;
		const float Y = (0.2126f * R + 0.7152f * G + 0.0722f * B);
		const float Z = (0.0193f * R + 0.1192f * G + 0.9505f * B) / 1.08883f;
		const auto F = [](float T)
		{
			return T > 216.f / 24389.f ? FMath::Pow(T, 1.f / 3.f)
									   : (841.f / 108.f) * T + 4.f / 29.f;
		};
		const float Fx = F(X), Fy = F(Y), Fz = F(Z);
		return FVector(116.f * Fy - 16.f, 500.f * (Fx - Fy), 200.f * (Fy - Fz));
	}

	float LabChroma(const FVector& Lab)
	{
		return FMath::Sqrt(Lab.Y * Lab.Y + Lab.Z * Lab.Z);
	}

	/** CIEDE2000, verbatim from the standard. */
	float DeltaE2000(const FVector& A, const FVector& B)
	{
		const float L1 = A.X, a1 = A.Y, b1 = A.Z;
		const float L2 = B.X, a2 = B.Y, b2 = B.Z;
		const float C1 = FMath::Sqrt(a1 * a1 + b1 * b1);
		const float C2 = FMath::Sqrt(a2 * a2 + b2 * b2);
		const float CBar = 0.5f * (C1 + C2);
		const float CBar7 = FMath::Pow(CBar, 7.f);
		const float G = 0.5f * (1.f - FMath::Sqrt(CBar7 / (CBar7 + FMath::Pow(25.f, 7.f))));
		const float a1p = (1.f + G) * a1, a2p = (1.f + G) * a2;
		const float C1p = FMath::Sqrt(a1p * a1p + b1 * b1);
		const float C2p = FMath::Sqrt(a2p * a2p + b2 * b2);
		const auto Hue = [](float Ap, float Bp)
		{
			if (FMath::IsNearlyZero(Ap) && FMath::IsNearlyZero(Bp))
			{
				return 0.f;
			}
			const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Bp, Ap));
			return Deg < 0.f ? Deg + 360.f : Deg;
		};
		const float h1p = Hue(a1p, b1), h2p = Hue(a2p, b2);
		const float dLp = L2 - L1;
		const float dCp = C2p - C1p;
		float dhp = 0.f;
		if (C1p * C2p != 0.f)
		{
			dhp = h2p - h1p;
			if (dhp > 180.f)       { dhp -= 360.f; }
			else if (dhp < -180.f) { dhp += 360.f; }
		}
		const float dHp = 2.f * FMath::Sqrt(C1p * C2p) * FMath::Sin(FMath::DegreesToRadians(dhp) * 0.5f);
		const float LBarP = 0.5f * (L1 + L2);
		const float CBarP = 0.5f * (C1p + C2p);
		float hBarP = h1p + h2p;
		if (C1p * C2p != 0.f)
		{
			if (FMath::Abs(h1p - h2p) <= 180.f)  { hBarP = 0.5f * (h1p + h2p); }
			else if (h1p + h2p < 360.f)          { hBarP = 0.5f * (h1p + h2p + 360.f); }
			else                                 { hBarP = 0.5f * (h1p + h2p - 360.f); }
		}
		const auto Cos = [](float Deg) { return FMath::Cos(FMath::DegreesToRadians(Deg)); };
		const float T = 1.f - 0.17f * Cos(hBarP - 30.f) + 0.24f * Cos(2.f * hBarP)
			+ 0.32f * Cos(3.f * hBarP + 6.f) - 0.20f * Cos(4.f * hBarP - 63.f);
		const float dTheta = 30.f * FMath::Exp(-FMath::Square((hBarP - 275.f) / 25.f));
		const float CBarP7 = FMath::Pow(CBarP, 7.f);
		const float Rc = 2.f * FMath::Sqrt(CBarP7 / (CBarP7 + FMath::Pow(25.f, 7.f)));
		const float Sl = 1.f + (0.015f * FMath::Square(LBarP - 50.f))
			/ FMath::Sqrt(20.f + FMath::Square(LBarP - 50.f));
		const float Sc = 1.f + 0.045f * CBarP;
		const float Sh = 1.f + 0.015f * CBarP * T;
		const float Rt = -FMath::Sin(FMath::DegreesToRadians(2.f * dTheta)) * Rc;
		return FMath::Sqrt(FMath::Square(dLp / Sl) + FMath::Square(dCp / Sc) + FMath::Square(dHp / Sh)
			+ Rt * (dCp / Sc) * (dHp / Sh));
	}

	/**
	 * The separation metric, and the one place it is not plain CIEDE2000.
	 *
	 * Between two NEAR-NEUTRAL colours only the lightness term counts. CIEDE2000
	 * inflates the chroma axes at low chroma (that is what its G factor is for),
	 * so two greys whose hues happen to be 150 degrees apart at chroma 2 score
	 * several units of "hue difference" that no player has ever seen. Structure
	 * and Concrete are both neutral by definition, so the only honest thing they
	 * can separate on is lightness, and this makes the metric say so.
	 */
	constexpr float NeutralChroma = 6.f;

	float SurfaceSeparation(const FVector& A, const FVector& B)
	{
		if (LabChroma(A) < NeutralChroma && LabChroma(B) < NeutralChroma)
		{
			return DeltaE2000(FVector(A.X, 0.f, 0.f), FVector(B.X, 0.f, 0.f));
		}
		return DeltaE2000(A, B);
	}

	/**
	 * THE THRESHOLD, and why it is this number.
	 *
	 * Every textured surface is modulated by its detail map by up to
	 * +-0.4 x DetailStrength around its palette colour (see the DetailStrength
	 * comment in SarkoMapPalette.h). Run that swing through the model above and a
	 * single surface varies by up to 6.5 dE00 WITHIN ITSELF — the ground by 6.5,
	 * bark by 6.4, rust by 6.3. Two surfaces closer together than that are inside
	 * each other's texture noise, which is not a figure of speech: it is what the
	 * rail depot looked like, wagons and crates and pallets one brown mass told
	 * apart by silhouette alone.
	 *
	 * 10 is that noise floor with half again on top. It is also, conveniently,
	 * about where two colours stop being shades of one thing and start having
	 * different names — which is the read a 40-to-120 pixel prop on a phone, in
	 * motion, under a shadow, actually gets.
	 */
	constexpr float MinSurfaceSeparation = 10.f;

	/** Where a surface appears in the sector, for the co-occurrence sweep below. */
	struct FSurfacePlacement
	{
		FVector2D Where = FVector2D::ZeroVector;
		ESarkoSurface Surface = ESarkoSurface::Ground;
	};

	/**
	 * How far apart two things can be and still share a frame.
	 *
	 * The spring arm is 1400 uu at -70 degrees, which frames roughly 3000 uu of
	 * ground across the long axis. Two surfaces within that of each other are a
	 * pair the player compares side by side; beyond it they are never seen
	 * together and their colours are free to collide.
	 */
	constexpr double FrameFootprintUU = 3000.0;

	/**
	 * The pairs that are a DELIBERATE RAMP rather than two things to tell apart.
	 *
	 * The edge skirt is a monotone luminance fall-off standing in for distance
	 * (see ESarkoSurface's comment on the bands). Consecutive steps of a gradient
	 * are supposed to be close; separating them would replace "further away" with
	 * "another field", which is the exact failure the bands exist to prevent.
	 * Nothing else is exempt.
	 */
	bool IsTheEdgeGradient(ESarkoSurface A, ESarkoSurface B)
	{
		const auto InRamp = [](ESarkoSurface S)
		{
			return S == ESarkoSurface::Ground || S == ESarkoSurface::SkirtNear
				|| S == ESarkoSurface::SkirtMid || S == ESarkoSurface::SkirtFar;
		};
		return InRamp(A) && InRamp(B);
	}

	/** Every surface the sector actually places, with where it places it. */
	void CollectPlacements(const FSarkoMapDefinition& Map, TArray<FSurfacePlacement>& Out)
	{
		const auto Add = [&Out](const FVector& Location, ESarkoSurface Surface)
		{
			Out.Add({ FVector2D(Location.X, Location.Y), Surface });
		};
		for (const FSarkoCoverBlock& Block : Map.Blocks)
		{
			Add(Block.Location, Block.Surface);
		}
		for (const FSarkoBuilding& Building : Map.Buildings)
		{
			Add(Building.Location, Building.Surface);
		}
		for (const FSarkoMapProp& Prop : Map.Props)
		{
			FSarkoPropKind Kind;
			if (!SarkoMap::FindPropKind(Prop.Kind, Kind))
			{
				continue;
			}
			for (const FSarkoPropPart& Part : Kind.Parts)
			{
				Add(Prop.Location, Part.Surface);
			}
		}
		for (const FSarkoExtractionSpot& Spot : Map.Extractions)
		{
			Add(Spot.Location, ESarkoSurface::Extraction);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSettingsHaveSaneDefaults,
	"Sarko.Config.SettingsHaveSaneDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSettingsHaveSaneDefaults::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings singleton resolves"), Settings);

	TestTrue(TEXT("raid lasts a positive number of seconds"), Settings->RaidDurationSeconds > 0.f);
	TestTrue(TEXT("map has a positive extent"), Settings->MapExtent > 0.f);
	TestTrue(TEXT("walk speed is positive"), Settings->WalkSpeed > 0.f);
	TestTrue(TEXT("weapon range is shorter than the map"), Settings->WeaponRangeUU < Settings->MapExtent);
	TestTrue(TEXT("weapon damage is positive and not instant death"), Settings->WeaponDamage > 0.f && Settings->WeaponDamage < 100.f);
	TestTrue(TEXT("aim assist is a nudge, not an aimbot"), Settings->AimConeHalfAngleDegrees > 0.f && Settings->AimConeHalfAngleDegrees <= 10.f);
	TestTrue(TEXT("magazine holds at least one round"), Settings->MagazineSize > 0);
	TestTrue(TEXT("reload time is positive and tactical"), Settings->ReloadSeconds > 0.f && Settings->ReloadSeconds < 10.f);
	// The forest is only walk-in-able because of this number. Zero disables the
	// fade (which is a legitimate way to see the problem it solves, and not a
	// legitimate thing to ship), and anything approaching the weapon's range
	// would cut a clearing bigger than a firefight and stop the stand reading as
	// a stand at all.
	TestTrue(TEXT("canopies fade around the player, and not across the whole map"),
		Settings->CanopyFadeRadiusUU > 0.f && Settings->CanopyFadeRadiusUU < Settings->WeaponRangeUU);
	// THE NOISE MODEL'S ORDER (spec §7). Firing louder than running louder than
	// walking is not a taste question — it is the entire mechanic. A config that
	// made a walk carry as far as a shot would delete stealth without failing
	// anything else in this file.
	TestTrue(TEXT("firing is louder than running, which is louder than walking, which is louder than nothing"),
		Settings->NoiseLoudRadiusUU > Settings->NoiseAudibleRadiusUU
			&& Settings->NoiseAudibleRadiusUU > Settings->NoiseQuietRadiusUU
			&& Settings->NoiseQuietRadiusUU > 0.f);
	TestTrue(TEXT("running is a real threshold on the stick, not the whole range or none of it"),
		Settings->NoiseRunSpeedFraction > Settings->NoiseMoveSpeedFraction
			&& Settings->NoiseRunSpeedFraction < 1.f
			&& Settings->NoiseMoveSpeedFraction > 0.f);
	// A movement event must outlive the gap between reports, or a walking pawn
	// blinks in and out of earshot instead of being continuously audible.
	TestTrue(TEXT("a noise event outlives the interval between reports"),
		Settings->NoiseEventLifetimeSeconds > Settings->NoiseMovementIntervalSeconds
			&& Settings->NoiseMovementIntervalSeconds > 0.f);
	TestTrue(TEXT("the fallback listener can hear"), Settings->EnemyHearingSensitivity > 0.f);
	// Sight bounds what hearing used to bound by accident, and a bot must never
	// be able to shoot something it cannot see (ТЗ §11).
	TestTrue(TEXT("a bot sees at least as far as it shoots"),
		Settings->EnemySightRangeUU >= Settings->EnemyFiringRangeUU);
	TestTrue(TEXT("enemy fire interval is positive"), Settings->EnemyFireIntervalSeconds > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPaletteSeparatesGroundFromCover,
	"Sarko.Config.PaletteSeparatesGroundFromCover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The palette is the readability contract, so it is asserted rather than left to
 * whoever next edits the constants. A top-down game where cover and ground are
 * the same value is unplayable, and that is exactly what the engine's default
 * grey-on-grey placeholder was.
 */
bool FSarkoPaletteSeparatesGroundFromCover::RunTest(const FString& Parameters)
{
	using namespace SarkoMap::Palette;

	// Perceived brightness, not a channel-by-channel compare: two colours can
	// differ in every channel and still read as the same shade from above.
	const auto Luminance = [](const FLinearColor& Colour)
	{
		return 0.2126f * Colour.R + 0.7152f * Colour.G + 0.0722f * Colour.B;
	};

	const float GroundLuminance = Luminance(Ground);
	const float StructureLuminance = Luminance(Structure);

	TestTrue(TEXT("cover is clearly brighter than the ground it stands on"),
		StructureLuminance > GroundLuminance * 2.f);

	// Both have to survive a fixed exposure: pure black hides shadows, and a
	// blown-out value hides everything drawn on top of it.
	TestTrue(TEXT("ground is lit, not black"), GroundLuminance > 0.01f);
	TestTrue(TEXT("cover is not blown out"), StructureLuminance < 0.6f);

	// Green-brown per the map design: green is the strongest channel and blue
	// the weakest. A neutral grey ground would pass every check above.
	TestTrue(TEXT("ground leans green-brown, not grey"), Ground.G > Ground.R && Ground.R > Ground.B);

	// Cover is the neutral in the frame — the colour budget belongs to the
	// blue player and the red enemy.
	const float StructureSpread = FMath::Max3(Structure.R, Structure.G, Structure.B)
		- FMath::Min3(Structure.R, Structure.G, Structure.B);
	TestTrue(TEXT("cover is neutral grey"), StructureSpread < 0.02f);

	// A big flat plane with a low roughness becomes a mirror for the sun.
	TestTrue(TEXT("ground is matte"), GroundRoughness > 0.8f && GroundRoughness <= 1.f);
	TestTrue(TEXT("cover roughness is sane"), StructureRoughness > 0.f && StructureRoughness <= 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSurfacePaletteIsReadable,
	"Sarko.Config.SurfacePaletteIsReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole readability argument of ТЗ §14, written down as assertions. These
 * are not style preferences: a top-down player reads the map by luminance
 * first, and every relation below was chosen because its opposite made
 * something unreadable in a real frame.
 */
bool FSarkoSurfacePaletteIsReadable::RunTest(const FString& Parameters)
{
	using namespace SarkoMap;
	using namespace SarkoMap::Palette;

	const auto Lum = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	const auto Spread = [](const FLinearColor& C)
	{
		return FMath::Max3(C.R, C.G, C.B) - FMath::Min3(C.R, C.G, C.B);
	};

	// Every enum value must have a colour, a roughness and a name. The Count
	// sentinel makes this loop exhaustive: adding a twelfth surface and
	// forgetting a switch case fails here instead of shipping black geometry.
	for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Raw);
		const FString Name = SurfaceName(Surface);
		TestFalse(FString::Printf(TEXT("surface %d has a name"), Raw), Name.IsEmpty());

		ESarkoSurface RoundTripped = ESarkoSurface::Count;
		TestTrue(FString::Printf(TEXT("'%s' parses back"), *Name), ParseSurfaceName(Name, RoundTripped));
		TestEqual(FString::Printf(TEXT("'%s' round-trips"), *Name),
			static_cast<uint8>(RoundTripped), Raw);

		const FLinearColor Colour = ColourFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' is in gamut"), *Name),
			Colour.R >= 0.f && Colour.G >= 0.f && Colour.B >= 0.f &&
			Colour.R <= 1.f && Colour.G <= 1.f && Colour.B <= 1.f);
		TestTrue(FString::Printf(TEXT("'%s' is lit, not black"), *Name), Lum(Colour) > 0.005f);
		const float Roughness = RoughnessFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' has a sane roughness"), *Name),
			Roughness > 0.f && Roughness <= 1.f);
	}

	// No two surfaces may be the same colour — two names for one look is a
	// palette that silently lost a distinction.
	for (uint8 A = 0; A < static_cast<uint8>(ESarkoSurface::Count); ++A)
	{
		for (uint8 B = A + 1; B < static_cast<uint8>(ESarkoSurface::Count); ++B)
		{
			const FLinearColor First = ColourFor(static_cast<ESarkoSurface>(A));
			const FLinearColor Second = ColourFor(static_cast<ESarkoSurface>(B));
			TestFalse(FString::Printf(TEXT("'%s' and '%s' are not the same colour"),
				*SurfaceName(static_cast<ESarkoSurface>(A)), *SurfaceName(static_cast<ESarkoSurface>(B))),
				First.Equals(Second, 0.004f));
		}
	}

	const float GroundLum = Lum(ColourFor(ESarkoSurface::Ground));

	// ТЗ §14, clause by clause.
	TestTrue(TEXT("a dirt road is lighter than the ground it cuts through"),
		Lum(ColourFor(ESarkoSurface::Dirt)) > GroundLum * 1.6f);
	TestTrue(TEXT("asphalt is darker than the ground"),
		Lum(ColourFor(ESarkoSurface::Asphalt)) < GroundLum);
	TestTrue(TEXT("the bridge deck contrasts hard against its own asphalt"),
		Lum(ColourFor(ESarkoSurface::Concrete)) > Lum(ColourFor(ESarkoSurface::Asphalt)) * 4.f);
	{
		const FLinearColor Water = ColourFor(ESarkoSurface::Water);
		TestTrue(TEXT("water is blue-grey: blue leads, red trails"), Water.B > Water.G && Water.G > Water.R);
		TestTrue(TEXT("water is darker than the ground, so the ravine reads as depth"),
			Lum(Water) < GroundLum);
	}
	{
		// ТЗ §6's «мелкая вода» at the ford. The deep water had no lighter tone to
		// be shallow against, which is why the ford read as a gap in the cliff and
		// nothing else. Still opaque — a translucent material needs an authored
		// asset and this project authors none (spec §5.2).
		const FLinearColor Shallow = ColourFor(ESarkoSurface::Shallow);
		const FLinearColor Water = ColourFor(ESarkoSurface::Water);
		TestTrue(TEXT("shallow water is still blue-grey: blue leads, red trails"),
			Shallow.B > Shallow.G && Shallow.G > Shallow.R);
		TestTrue(TEXT("shallow water is visibly lighter than deep water"),
			Lum(Shallow) > Lum(Water) * 1.8f);
		TestTrue(TEXT("shallow water is lighter than the ground it interrupts, so a ford reads as a bright band"),
			Lum(Shallow) > GroundLum);
	}
	{
		const FLinearColor Rust = ColourFor(ESarkoSurface::Rust);
		TestTrue(TEXT("rust is red-dominant"), Rust.R > Rust.G && Rust.G > Rust.B);
		TestTrue(TEXT("rust separates from the ground by brightness too"),
			Lum(Rust) > GroundLum * 1.4f);
	}
	{
		const FLinearColor Timber = ColourFor(ESarkoSurface::Timber);
		TestTrue(TEXT("the village tone is warm"), Timber.R > Timber.B * 2.f);
		TestTrue(TEXT("the village tone is brighter than the ground"), Lum(Timber) > GroundLum * 1.8f);
	}
	{
		const FLinearColor Veg = ColourFor(ESarkoSurface::Vegetation);
		TestTrue(TEXT("vegetation is green-dominant"), Veg.G > Veg.R && Veg.G > Veg.B);
		TestTrue(TEXT("a treeline is darker than the ground it borders, so it reads as a wall"),
			Lum(Veg) < GroundLum);
	}
	TestTrue(TEXT("the ravine bed is the darkest thing in the sector"),
		Lum(ColourFor(ESarkoSurface::Ravine)) < Lum(ColourFor(ESarkoSurface::Water)));
	{
		const FLinearColor Green = ColourFor(ESarkoSurface::Extraction);
		TestTrue(TEXT("the extraction is unmistakably green"), Green.G > Green.R * 2.f && Green.G > Green.B * 2.f);
		TestTrue(TEXT("the extraction is the brightest surface in the sector"), Lum(Green) > 0.3f);
	}

	// The colour budget belongs to the characters. Every *world* surface stays
	// muted; the three gameplay tints do not. This is the constraint that lets
	// §14's palette exist without competing with friend/foe reading.
	for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Raw);
		if (Surface == ESarkoSurface::Extraction)
		{
			continue; // deliberately loud: it is a gameplay marker, not scenery
		}
		const FLinearColor Colour = ColourFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' is muted enough not to fight the characters"),
			*SurfaceName(Surface)),
			FMath::Max3(Colour.R, Colour.G, Colour.B) < 0.35f && Spread(Colour) < 0.20f);
	}

	// ---- PAIRWISE SEPARATION, over the surfaces that share a frame ----
	//
	// Everything above is a relation between two named surfaces, written down one
	// clause at a time because someone noticed it in a picture. This is the rule
	// that does not need noticing first: it derives its own list of pairs from
	// the map, so a prop kind that starts appearing beside a wagon next year is
	// checked without anybody remembering to check it.
	//
	// It is derived rather than tabulated for a specific reason. The sandbag kind
	// moved from Structure to Dirt in this same pass, and that one edit took
	// Dirt-beside-Structure from 36 co-occurrences to 205 — a hard-coded pair
	// list would have gone quietly stale at exactly the moment it mattered.
	{
		FSarkoMapDefinition Map;
		FString MapError;
		const bool bLoaded = SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, MapError);
		TestTrue(FString::Printf(TEXT("the sector loads, so its surfaces can be paired up: %s"), *MapError), bLoaded);

		if (bLoaded)
		{
			TArray<FSurfacePlacement> Placements;
			CollectPlacements(Map, Placements);
			TestTrue(TEXT("the sector places surfaces at all"), Placements.Num() > 100);

			// Which pairs actually share a frame. The ground is under every one of
			// them, so it pairs with everything by definition rather than by sweep.
			TSet<TPair<uint8, uint8>> Together;
			for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
			{
				if (Raw != static_cast<uint8>(ESarkoSurface::Ground))
				{
					Together.Add({ static_cast<uint8>(ESarkoSurface::Ground), Raw });
				}
			}
			for (int32 I = 0; I < Placements.Num(); ++I)
			{
				for (int32 J = I + 1; J < Placements.Num(); ++J)
				{
					if (Placements[I].Surface == Placements[J].Surface)
					{
						continue;
					}
					if (FVector2D::DistSquared(Placements[I].Where, Placements[J].Where)
						> FrameFootprintUU * FrameFootprintUU)
					{
						continue;
					}
					const uint8 A = static_cast<uint8>(Placements[I].Surface);
					const uint8 B = static_cast<uint8>(Placements[J].Surface);
					Together.Add({ FMath::Min(A, B), FMath::Max(A, B) });
				}
			}

			TArray<FVector> Labs;
			for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
			{
				Labs.Add(ScreenLab(ColourFor(static_cast<ESarkoSurface>(Raw))));
			}

			int32 Checked = 0;
			for (const TPair<uint8, uint8>& Pair : Together)
			{
				const ESarkoSurface A = static_cast<ESarkoSurface>(Pair.Key);
				const ESarkoSurface B = static_cast<ESarkoSurface>(Pair.Value);
				if (IsTheEdgeGradient(A, B))
				{
					continue;
				}
				++Checked;
				const float Apart = SurfaceSeparation(Labs[Pair.Key], Labs[Pair.Value]);
				TestTrue(FString::Printf(
					TEXT("'%s' and '%s' share a frame and are %.2f dE00 apart (needs %.0f)"),
					*SurfaceName(A), *SurfaceName(B), Apart, MinSurfaceSeparation),
					Apart >= MinSurfaceSeparation);
			}
			TestTrue(FString::Printf(TEXT("the sweep found pairs to check (%d)"), Checked), Checked > 40);
		}
	}

	// ---- AND THE CHARACTERS STAY THE LOUDEST THING IN THE FRAME ----
	//
	// The palette's whole licence to exist is that it does not compete with
	// friend/foe reading, and "muted" above is the input to that claim rather
	// than the claim itself. These are the output: no scenery may be as saturated
	// as the enemy tint, and no scenery may come as close to a character as two
	// surfaces are allowed to come to each other.
	{
		// SarkoBody's two tints. Duplicated as literals on purpose: if someone
		// repaints a scav, this test should fail and force the question, not
		// silently re-measure against the new colour and keep passing.
		const FVector PlayerLab = ScreenLab(FLinearColor(0.16f, 0.34f, 0.85f));
		const FVector EnemyLab = ScreenLab(FLinearColor(0.80f, 0.12f, 0.10f));
		const float EnemyChroma = LabChroma(EnemyLab);

		for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
		{
			const ESarkoSurface Surface = static_cast<ESarkoSurface>(Raw);
			const FVector Lab = ScreenLab(ColourFor(Surface));
			TestTrue(FString::Printf(TEXT("'%s' is not louder than the enemy red (%.1f vs %.1f)"),
				*SurfaceName(Surface), LabChroma(Lab), EnemyChroma),
				LabChroma(Lab) < EnemyChroma);
			TestTrue(FString::Printf(TEXT("a scav in blue stands out against '%s' (%.2f dE00)"),
				*SurfaceName(Surface), SurfaceSeparation(PlayerLab, Lab)),
				SurfaceSeparation(PlayerLab, Lab) >= MinSurfaceSeparation);
			TestTrue(FString::Printf(TEXT("a scav in red stands out against '%s' (%.2f dE00)"),
				*SurfaceName(Surface), SurfaceSeparation(EnemyLab, Lab)),
				SurfaceSeparation(EnemyLab, Lab) >= MinSurfaceSeparation);
		}
	}

	// The two original constants still mean what the previous palette test says
	// they mean, and the lookup agrees with them.
	TestTrue(TEXT("ColourFor(Ground) is the Ground constant"), ColourFor(ESarkoSurface::Ground).Equals(Ground, 0.0001f));
	TestTrue(TEXT("ColourFor(Structure) is the Structure constant"), ColourFor(ESarkoSurface::Structure).Equals(Structure, 0.0001f));
	TestEqual(TEXT("RoughnessFor(Ground) is GroundRoughness"), RoughnessFor(ESarkoSurface::Ground), GroundRoughness);
	TestEqual(TEXT("RoughnessFor(Structure) is StructureRoughness"), RoughnessFor(ESarkoSurface::Structure), StructureRoughness);

	ESarkoSurface Unknown = ESarkoSurface::Count;
	TestFalse(TEXT("an unknown surface name does not parse"), ParseSurfaceName(TEXT("chartreuse"), Unknown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLightingHasAnAmbientTerm,
	"Sarko.Config.LightingHasAnAmbientTerm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Automation runs under -nullrhi and can see nothing, so this cannot assert that
 * the frame looks right — the offscreen screenshot does that. What it CAN pin is
 * every way the ambient silently does not exist: a cubemap path that does not
 * resolve (a sky light with SLS_SpecifiedCubemap and no cubemap is invalid and
 * contributes nothing at all), an intensity of zero, a shadow lift that lifts
 * nothing, or an ambient bright enough to flatten the sun out of the frame.
 */
bool FSarkoLightingHasAnAmbientTerm::RunTest(const FString& Parameters)
{
	using namespace SarkoMap::Lighting;

	// The one failure mode that produces no log, no warning and no ambient: a
	// typo'd or moved engine asset path. Checked as a package rather than a
	// LoadObject so it is safe with no RHI.
	const FString Package = FSoftObjectPath(FString(AmbientCubemapPath)).GetLongPackageName();
	TestFalse(TEXT("the ambient cubemap path names a package"), Package.IsEmpty());
	TestTrue(FString::Printf(TEXT("the ambient cubemap package '%s' exists"), *Package),
		FPackageName::DoesPackageExist(Package));
	TestTrue(TEXT("the ambient cubemap is an engine asset, not one we authored"),
		Package.StartsWith(TEXT("/Engine/")));

	TestTrue(TEXT("the ambient actually contributes"), AmbientIntensity > 0.f);
	TestTrue(TEXT("the ambient does not flatten the sun out of the frame"), AmbientIntensity < SunIntensityLux);
	TestTrue(TEXT("the ambient cubemap is small enough for a phone"),
		AmbientCubemapResolution > 0 && AmbientCubemapResolution <= 64);

	// Cool sky against a warm sun: the shadowed side of a wall should read as a
	// different colour temperature, not merely a darker grey.
	TestTrue(TEXT("the sky fill is cool"), AmbientColour.B > AmbientColour.R);
	// The ground bounce exists but is dim — bLowerHemisphereIsBlack false with a
	// bright lower colour lights the undersides of everything and looks like a
	// missing shadow.
	const auto Lum = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	TestTrue(TEXT("the ground bounce is present"), Lum(GroundBounceColour) > 0.f);
	TestTrue(TEXT("the ground bounce is dimmer than the sky"),
		Lum(GroundBounceColour) < Lum(AmbientColour) * 0.5f);

	// A shadow lift of 1.0 is what the engine already does, and 0.0 removes
	// shadows entirely — which would undo the reason virtual shadow maps were
	// deliberately re-enabled in DefaultEngine.ini.
	TestTrue(TEXT("shadows are lifted but not removed"), ShadowAmount > 0.2f && ShadowAmount < 1.f);

	// The sun still comes from above, or a top-down camera sees mostly shadow.
	TestTrue(TEXT("the sun is steep, not horizontal"), SunRotation.Pitch < -30.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEngineMeshPathsResolve,
	"Sarko.Config.EngineMeshPathsResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEngineMeshPathsResolve::RunTest(const FString& Parameters)
{
	// Every engine asset this project reaches by literal string, in one place.
	// A moved or renamed engine asset produces a log line at runtime and nothing
	// else — geometry simply does not appear, or keeps the grid material.
	// Sphere and Cone are gone from this list because they are gone from the
	// project: they were the rock, the bush and the two round canopies, and all
	// four of those are imported meshes now. A path listed here that nothing
	// reaches for is a claim about the engine rather than about this game.
	const TArray<FString> Paths = {
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
		TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"),
	};
	for (const FString& Path : Paths)
	{
		const FString Package = FSoftObjectPath(Path).GetLongPackageName();
		TestTrue(FString::Printf(TEXT("'%s' exists"), *Path), FPackageName::DoesPackageExist(Package));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropMeshBoundsAreNormalised,
	"Sarko.Config.PropMeshBoundsAreNormalised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every non-primitive mesh is the same shape as the engine cube: bounds
 * -50..50 uu on all three axes, centred on its own origin.
 *
 * BOTH ROOTS, which is the reason this test lost the word "ThirdParty" from its
 * name. /Game/ThirdParty is the downloaded packs; /Game/Generated/Props is the
 * eleven meshes this project builds itself with Scripts/generate-props.sh. They
 * come out of the SAME Blender normaliser and they are trusted by the same line
 * of arithmetic, so covering one and not the other would leave the newer half
 * of the prop table — the whole АЗС, the rolling stock and the yard — resting on
 * an assumption nothing checks. The prefix test is a whitelist rather than a
 * "not /Engine/" check on purpose: an engine primitive is 100 uu by
 * construction and does not need asserting, and a path in neither root is
 * something nobody has thought about yet.
 *
 * This is the load-bearing assumption of the whole prop system and it lives
 * outside the code — Scripts/prepare-assets.py stretches each mesh into that box
 * in Blender, and nothing in C++ can tell whether it did. ASarkoPropField::AddPart
 * scales an instance by `Extent / 50` on the strength of it, the box simple
 * collision added at import is cut to those bounds, and every extent in the kind
 * table (and every extent assertion in this suite) means "the prop's half-extent
 * in world units" only because of it.
 *
 * If a re-import lost the normalisation, the failure would be silent and total:
 * a mesh that arrived at its modelled 2.4 m would come out at a scale of
 * Extent/50 anyway, so a tree 7 m tall would spawn 3 m tall, its collision box
 * would no longer match the number the spawn-clearance test checked, and every
 * test in the suite would still pass. Hence: assert the shape of the ASSET.
 */
bool FSarkoPropMeshBoundsAreNormalised::RunTest(const FString& Parameters)
{
	// Gathered from the kind table rather than listed, so a mesh added to a kind
	// tomorrow is covered without anyone remembering this test exists.
	TSet<FString> MeshPaths;
	for (const FName& Kind : SarkoMap::AllPropKindNames())
	{
		FSarkoPropKind Resolved;
		if (!SarkoMap::FindPropKind(Kind, Resolved))
		{
			continue;
		}
		for (const FSarkoPropPart& Part : Resolved.Parts)
		{
			const FString Path = Part.Mesh.ToString();
			if (Path.StartsWith(TEXT("/Game/ThirdParty/")) || Path.StartsWith(TEXT("/Game/Generated/Props/")))
			{
				MeshPaths.Add(Path);
			}
		}
	}
	TestTrue(TEXT("the kind table actually uses imported meshes"), MeshPaths.Num() >= 8);
	// Both halves are actually present. Without this the test would still pass
	// with a green tick on the day somebody deleted every generated mesh from the
	// table, because a set that lost eleven entries still has fifteen.
	int32 Generated = 0;
	for (const FString& Path : MeshPaths)
	{
		Generated += Path.StartsWith(TEXT("/Game/Generated/Props/")) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("the kind table uses this project's own meshes (%d)"), Generated),
		Generated >= 11);

	for (const FString& Path : MeshPaths)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(FSoftObjectPath(Path).TryLoad());
		if (!Mesh)
		{
			AddError(FString::Printf(TEXT("'%s' does not load — run Scripts/import-assets.sh"), *Path));
			continue;
		}

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		TestTrue(FString::Printf(TEXT("'%s' is 100 uu on every axis (%s)"), *Path, *Bounds.BoxExtent.ToString()),
			Bounds.BoxExtent.Equals(FVector(50.f), 0.5f));
		TestTrue(FString::Printf(TEXT("'%s' is centred on its origin (%s)"), *Path, *Bounds.Origin.ToString()),
			Bounds.Origin.IsNearlyZero(0.5f));

		// A HISM set to QueryAndPhysics with no simple collision does not stop a
		// character capsule — sweeps use simple collision and complex collision
		// only answers line traces. Every one of these meshes is something the
		// player walks into or hides behind, so SOMETHING simple has to be there.
		//
		// Any element, not specifically a box: Interchange fits one convex hull
		// per mesh on import, and a hull is both sufficient and better than the
		// box this project first tried to add by hand — a box around a branching
		// tree stops the player a metre out from the trunk. What matters is that
		// the hull is inside the mesh's bounds, which the extent assertions above
		// have just pinned, so a kind's Extent remains an upper bound on what the
		// player can touch.
		const UBodySetup* Body = Mesh->GetBodySetup();
		const int32 SimpleElements = Body
			? Body->AggGeom.BoxElems.Num() + Body->AggGeom.ConvexElems.Num()
				+ Body->AggGeom.SphereElems.Num() + Body->AggGeom.SphylElems.Num()
			: 0;
		TestTrue(FString::Printf(TEXT("'%s' has simple collision (%d elements)"), *Path, SimpleElements),
			SimpleElements >= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPackagingSettingsLiveInTheGameIni,
	"Sarko.Config.PackagingSettingsLiveInTheGameIni",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three packaging lines are in DefaultGame.ini and NOT in DefaultEngine.ini.
 *
 * This is a test about which FILE a section is in, which is why it reads the files
 * off disk instead of asking GConfig or the settings object: in the wrong file
 * every line is silently inert, and a test that queries the merged config sees the
 * same "nothing configured" whether the section is misplaced or simply absent.
 *
 * UProjectPackagingSettings is UCLASS(config=Game), so the cooker and UAT read it
 * from the Game ini hierarchy and never look in the Engine one. The block lived in
 * DefaultEngine.ini until 2026-08-03 and did nothing there — no error, no warning,
 * and an editor that behaves perfectly, because the editor loads Data/ off the
 * filesystem and hard-loads nothing from the cook lines. The cost was a device
 * build with no bridge.json staged (an empty raid), an uncooked mannequin (an
 * invisible character) and no ambient cubemap. All three appear only on hardware,
 * and none of them says why.
 *
 * The negative half matters as much as the positive: putting the section back in
 * DefaultEngine.ini "as well, to be safe" would look like belt and braces and be
 * two copies of one setting, one of which is dead and would drift.
 */
bool FSarkoPackagingSettingsLiveInTheGameIni::RunTest(const FString& Parameters)
{
	const FString ConfigDir = FPaths::ProjectConfigDir();

	FString GameIni;
	const bool bReadGame = FFileHelper::LoadFileToString(GameIni, *(ConfigDir / TEXT("DefaultGame.ini")));
	TestTrue(TEXT("DefaultGame.ini is readable"), bReadGame);

	FString EngineIni;
	const bool bReadEngine = FFileHelper::LoadFileToString(EngineIni, *(ConfigDir / TEXT("DefaultEngine.ini")));
	TestTrue(TEXT("DefaultEngine.ini is readable"), bReadEngine);
	if (!bReadGame || !bReadEngine)
	{
		return false;
	}

	// The section header, because a key in the right file under no section (or
	// under the previous one) is just as dead as a key in the wrong file.
	TestTrue(TEXT("DefaultGame.ini declares the packaging section"),
		GameIni.Contains(TEXT("[/Script/UnrealEd.ProjectPackagingSettings]")));

	// Data/ staged as non-UFS: bridge.json is read with FFileHelper at runtime, not
	// cooked as a UAsset, so without this line the map file is simply absent from a
	// packaged build and the raid comes up empty.
	TestTrue(TEXT("Data is staged as non-UFS"),
		GameIni.Contains(TEXT("DirectoriesToAlwaysStageAsNonUFS=(Path=\"Data\")")));
	// Both LoadObject-by-literal-path assets: nothing hard-references them, so the
	// cooker has no reason to include them and the editor cannot tell the
	// difference.
	TestTrue(TEXT("the mannequin directory is force-cooked"),
		GameIni.Contains(TEXT("DirectoriesToAlwaysCook=(Path=\"/Game/Mannequins\")")));
	TestTrue(TEXT("the ambient cubemap directory is force-cooked"),
		GameIni.Contains(TEXT("DirectoriesToAlwaysCook=(Path=\"/Engine/MapTemplates/Sky\")")));

	// And nothing packaging-related in the Engine ini, where it would be inert.
	TestFalse(TEXT("DefaultEngine.ini does not claim the packaging section"),
		EngineIni.Contains(TEXT("ProjectPackagingSettings")));
	TestFalse(TEXT("DefaultEngine.ini does not stage directories"),
		EngineIni.Contains(TEXT("DirectoriesToAlwaysStageAsNonUFS")));
	TestFalse(TEXT("DefaultEngine.ini does not force-cook directories"),
		EngineIni.Contains(TEXT("DirectoriesToAlwaysCook")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
