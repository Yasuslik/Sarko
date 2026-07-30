#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"

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
	TestTrue(TEXT("weapon range is shorter than the map"), Settings->WeaponRangeUU < Settings->MapExtent);
	TestTrue(TEXT("aim assist is a nudge, not an aimbot"), Settings->AimConeHalfAngleDegrees > 0.f && Settings->AimConeHalfAngleDegrees <= 10.f);
	TestTrue(TEXT("magazine holds at least one round"), Settings->MagazineSize > 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
