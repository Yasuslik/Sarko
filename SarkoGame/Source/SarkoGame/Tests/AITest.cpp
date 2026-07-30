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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAISteerDirection,
	"Sarko.AI.SteerDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAISteerDirection::RunTest(const FString& Parameters)
{
	// Pure steering math: no navmesh in this project, so the enemy steers
	// straight at its target and only deviates when a forward trace (cast by
	// the controller, not this function) reports the way is blocked. No world
	// access here either — same reasoning as DecideState above.
	using namespace SarkoAI;

	const FVector2D Forward(1.f, 0.f);

	{
		const FVector2D Result = ComputeSteerDirection(Forward, /*bForwardBlocked*/ false, 60.f);
		TestTrue(TEXT("clear path keeps the desired direction unchanged"), Result.Equals(Forward, KINDA_SMALL_NUMBER));
	}

	{
		const FVector2D Result = ComputeSteerDirection(Forward, /*bForwardBlocked*/ true, 90.f);
		TestTrue(TEXT("a 90-degree avoidance turn rotates (1,0) to (0,1)"), Result.Equals(FVector2D(0.f, 1.f), KINDA_SMALL_NUMBER));
	}

	{
		const FVector2D Result = ComputeSteerDirection(Forward, /*bForwardBlocked*/ true, 45.f);
		TestTrue(TEXT("a blocked steer direction stays unit length"), FMath::IsNearlyEqual(Result.Size(), 1.f, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("a positive avoidance angle steers away from straight-ahead"), !Result.Equals(Forward, KINDA_SMALL_NUMBER));
	}

	{
		const FVector2D Result = ComputeSteerDirection(FVector2D::ZeroVector, /*bForwardBlocked*/ true, 60.f);
		TestTrue(TEXT("no desired direction means no steer direction, blocked or not"), Result.IsNearlyZero());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
