#include "Misc/AutomationTest.h"

#include "AI/SarkoAIController.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAIStateTransitions,
	"Sarko.AI.StateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAIStateTransitions::RunTest(const FString& Parameters)
{
	// Pure decision function: distance and visibility in, next state out. This
	// is why the AI is a C++ state machine and not a Behavior Tree — a tree is a
	// binary asset that cannot be written or tested here at all.
	using namespace SarkoAI;

	TestEqual(TEXT("no target at all means patrol"),
		static_cast<int32>(DecideState(ESarkoAIState::Idle, /*bHasTarget*/ false, 0.f, false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Patrol));

	TestEqual(TEXT("a heard but unseen target is chased"),
		static_cast<int32>(DecideState(ESarkoAIState::Patrol, true, 2000.f, /*bHasLineOfSight*/ false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a visible target in range is shot at"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 900.f, true, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("a visible target beyond firing range is closed on"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, 2200.f, true, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a target outside hearing range is forgotten"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 9000.f, false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Patrol));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
