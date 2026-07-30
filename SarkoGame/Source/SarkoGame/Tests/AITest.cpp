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

	const float HysteresisRangeUU = 150.f;

	TestEqual(TEXT("no target at all means patrol"),
		static_cast<int32>(DecideState(ESarkoAIState::Idle, /*bHasTarget*/ false, 0.f, false, 2500.f, 1200.f, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Patrol));

	TestEqual(TEXT("a heard but unseen target is chased"),
		static_cast<int32>(DecideState(ESarkoAIState::Patrol, true, 2000.f, /*bHasLineOfSight*/ false, 2500.f, 1200.f, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a visible target in range is shot at"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 900.f, true, 2500.f, 1200.f, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("a visible target beyond firing range is closed on"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, 2200.f, true, 2500.f, 1200.f, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a target outside hearing range is forgotten"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 9000.f, false, 2500.f, 1200.f, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Patrol));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAIStateHysteresis,
	"Sarko.AI.StateHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAIStateHysteresis::RunTest(const FString& Parameters)
{
	// Current was previously read by nothing: at exactly the firing range the
	// state would chatter Chase<->Shoot every tick as floating-point distance
	// drifted by fractions of a uu around the boundary. These cases hold
	// DistanceToTarget fixed and vary only Current, which is the one thing
	// that proves the parameter is actually read — the old
	// FSarkoAIStateTransitions test above passes four different Current
	// values and would pass identically even if DecideState ignored it
	// entirely (as it used to).
	using namespace SarkoAI;

	const float HearingRadius = 2500.f;
	const float FiringRange = 1200.f;
	const float HysteresisRangeUU = 150.f;

	TestEqual(TEXT("already shooting exactly at the firing range stays in Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, FiringRange, true, HearingRadius, FiringRange, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("chasing at exactly the firing range enters Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, FiringRange, true, HearingRadius, FiringRange, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Shoot));

	// The discriminating case: identical distance, inside the hysteresis
	// band past FiringRange but past the plain entry threshold too. Only
	// Current differs between these two assertions, and the outcome must
	// differ with it, or the parameter is decorative.
	const float DistanceInHysteresisBand = FiringRange + HysteresisRangeUU * 0.5f;
	TestEqual(TEXT("already shooting, just past the firing range but inside the hysteresis band, stays in Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, DistanceInHysteresisBand, true, HearingRadius, FiringRange, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Shoot));
	TestEqual(TEXT("chasing at that same distance stays in Chase instead of entering Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, DistanceInHysteresisBand, true, HearingRadius, FiringRange, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("already shooting beyond the hysteresis band drops back to Chase"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, FiringRange + HysteresisRangeUU * 2.f, true, HearingRadius, FiringRange, HysteresisRangeUU)),
		static_cast<int32>(ESarkoAIState::Chase));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAISteerSideChoice,
	"Sarko.AI.SteerSideChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAISteerSideChoice::RunTest(const FString& Parameters)
{
	// Pure side-choice: the whole point of item 2 is that this must depend on
	// the impact normal instead of always picking the same side, or the enemy
	// can rotate straight into more geometry in a concave pocket exactly the
	// way the old fixed-CCW rotation did.
	using namespace SarkoAI;

	const FVector2D Forward(1.f, 0.f);

	const float SignA = ChooseSteerSign(Forward, FVector2D(0.f, 1.f));
	const float SignB = ChooseSteerSign(Forward, FVector2D(0.f, -1.f));
	TestTrue(TEXT("a normal on one side and its mirror choose opposite sides"), SignA * SignB < 0.f);
	TestTrue(TEXT("the chosen sign is always +1 or -1"), FMath::IsNearlyEqual(FMath::Abs(SignA), 1.f) && FMath::IsNearlyEqual(FMath::Abs(SignB), 1.f));

	// A straight-on hit (normal opposes the desired direction exactly) is a
	// degenerate tie: it must still return a deterministic, well-formed sign
	// rather than zero or NaN.
	const float SignHeadOn = ChooseSteerSign(Forward, FVector2D(-1.f, 0.f));
	TestTrue(TEXT("a head-on hit still resolves to +1 or -1, not zero"), FMath::IsNearlyEqual(FMath::Abs(SignHeadOn), 1.f));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
