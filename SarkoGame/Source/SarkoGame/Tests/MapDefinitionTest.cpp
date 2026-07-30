#include "Misc/AutomationTest.h"

#include "Debug/SarkoOverviewShot.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOverviewHeightFitsSector,
	"Sarko.Debug.OverviewHeightFitsSector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOverviewHeightFitsSector::RunTest(const FString& Parameters)
{
	// A 90-degree vertical FOV sees exactly as far across as it is high, so a
	// sector of half-extent E needs at least E of height, and more for a
	// narrower FOV. Getting this wrong means the overview crops the map, which
	// is worse than useless: it looks like the map ends there.
	const float Height90 = SarkoDebug::HeightToFitSector(/*ExtentUU*/ 20000.f, /*VerticalFOVDegrees*/ 90.f);
	TestTrue(TEXT("a 90 degree FOV needs at least the sector's half-extent in height"), Height90 >= 20000.f);

	const float Height60 = SarkoDebug::HeightToFitSector(20000.f, 60.f);
	TestTrue(TEXT("a narrower FOV must pull the camera further back"), Height60 > Height90);

	// Margin: the frame should not end exactly at the sector edge, or the
	// outermost cover touches the screen border and cannot be judged.
	TestTrue(TEXT("there is headroom beyond the sector edge"), Height90 > 20000.f * 1.05f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
