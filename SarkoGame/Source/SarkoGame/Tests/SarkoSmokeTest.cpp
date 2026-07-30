#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// Proves the headless automation path works: this test is written, compiled and
// run entirely from the command line, with no editor UI. Everything the agent
// verifies about game logic rides on this loop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHeadlessSmokeTest,
	"Sarko.Infrastructure.HeadlessSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHeadlessSmokeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the automation harness reports arithmetic correctly"), 2 + 2, 4);
	TestTrue(TEXT("the SarkoGame module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("SarkoGame")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
