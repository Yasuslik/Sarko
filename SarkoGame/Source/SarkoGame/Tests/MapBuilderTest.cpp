#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapBuilder.h"

#if WITH_AUTOMATION_TESTS

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
	TestTrue(TEXT("enemy hearing radius is positive"), Settings->EnemyHearingRadiusUU > 0.f);
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

#endif // WITH_AUTOMATION_TESTS
