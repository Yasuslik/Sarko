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

#endif // WITH_AUTOMATION_TESTS
