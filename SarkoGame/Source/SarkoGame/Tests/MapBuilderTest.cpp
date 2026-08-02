#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

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
	const TArray<FString> Paths = {
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		// The conifer canopy, and the only thing that uses it.
		TEXT("/Engine/BasicShapes/Cone.Cone"),
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
