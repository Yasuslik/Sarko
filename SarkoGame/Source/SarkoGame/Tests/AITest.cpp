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
	const bool bNoInvestigation = false;

	TestEqual(TEXT("no target at all means patrol"),
		static_cast<int32>(DecideState(ESarkoAIState::Idle, /*bHasTarget*/ false, 0.f, false, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Patrol));

	// THE LINE-OF-SIGHT GATE, and the one assertion in this file that changed
	// meaning with the realism stage. This used to read "a heard but unseen
	// target is chased" — which is aggro through a wall (ТЗ §11 forbids it), and
	// with a 2000 uu firing range it was aggro through a wall from off the
	// player's screen. Heard and unseen now means Investigate: walk to the noise.
	TestEqual(TEXT("a heard but unseen target is investigated, not chased"),
		static_cast<int32>(DecideState(ESarkoAIState::Patrol, true, 2000.f, /*bHasLineOfSight*/ false, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Investigate));

	// And the discriminating half: at a distance that WOULD be inside the firing
	// range, no sight still means no shot. Without this, "gated on LOS" could be
	// satisfied by a function that only gates the far case.
	TestEqual(TEXT("an unseen target inside the firing range is still not shot at"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, 400.f, /*bHasLineOfSight*/ false, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Investigate));

	TestEqual(TEXT("a visible target in range is shot at"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 900.f, true, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("a visible target beyond firing range is closed on"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, 2200.f, true, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a target outside hearing range is forgotten"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 9000.f, false, 2500.f, 1200.f, HysteresisRangeUU, bNoInvestigation)),
		static_cast<int32>(ESarkoAIState::Patrol));

	// An investigation in progress survives the target leaving hearing — the bot
	// is walking to a REMEMBERED position, not following a live one. This is the
	// only reason the flag is a parameter: without it, a player who breaks
	// contact makes the bot forget mid-stride and the noise never gets looked at.
	TestEqual(TEXT("a bot already walking to a noise keeps walking after the target is out of earshot"),
		static_cast<int32>(DecideState(ESarkoAIState::Investigate, true, 9000.f, false, 2500.f, 1200.f, HysteresisRangeUU, /*bInvestigationActive*/ true)),
		static_cast<int32>(ESarkoAIState::Investigate));

	TestEqual(TEXT("and drops to patrol once that investigation is over"),
		static_cast<int32>(DecideState(ESarkoAIState::Investigate, true, 9000.f, false, 2500.f, 1200.f, HysteresisRangeUU, /*bInvestigationActive*/ false)),
		static_cast<int32>(ESarkoAIState::Patrol));

	// Sight beats a live investigation: the memory is moot the moment the thing
	// it remembers is visible.
	TestEqual(TEXT("seeing the target ends the investigation immediately"),
		static_cast<int32>(DecideState(ESarkoAIState::Investigate, true, 800.f, /*bHasLineOfSight*/ true, 2500.f, 1200.f, HysteresisRangeUU, /*bInvestigationActive*/ true)),
		static_cast<int32>(ESarkoAIState::Shoot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAIPatrolLeash,
	"Sarko.AI.PatrolLeash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAIPatrolLeash::RunTest(const FString& Parameters)
{
	// The bug this replaces, stated as numbers: PatrolTarget initialised to
	// FVector::ZeroVector (the world origin, which on the shipped map is the
	// closed bridge) and RerollPatrolTarget picked uniformly inside
	// +/-MapExtent*0.8 = +/-16,000 uu. With the stuck detector re-rolling every
	// 2 s, every hand-authored bot position on the map was fiction within about
	// 90 seconds. The one property worth asserting is therefore the one that was
	// missing entirely: a patrol target is never further from the post than the
	// leash.
	using namespace SarkoAI;

	const FVector Post(-14600.f, -10700.f, 150.f);
	const float LeashUU = 1400.f;

	// A sweep rather than a handful of points: the failure mode is a formula
	// that is right in the middle of its range and wrong at the corners.
	float Furthest = 0.f;
	for (int32 A = 0; A <= 16; ++A)
	{
		for (int32 R = 0; R <= 16; ++R)
		{
			const FVector Point = PatrolPointInLeash(Post, LeashUU, A / 16.f, R / 16.f);
			const float Distance = FVector::Dist2D(Point, Post);
			Furthest = FMath::Max(Furthest, Distance);
			TestTrue(FString::Printf(TEXT("a patrol point is inside the leash (%.1f uu of %.0f)"), Distance, LeashUU),
				Distance <= LeashUU + KINDA_SMALL_NUMBER);
			TestTrue(TEXT("a patrol point keeps the post's height"),
				FMath::IsNearlyEqual(static_cast<float>(Point.Z), static_cast<float>(Post.Z)));
		}
	}

	// A guard against the guard: a function that returned the post every time
	// would pass every assertion above and leave the bot standing still forever.
	TestTrue(FString::Printf(TEXT("the leash is actually used (furthest point %.0f uu)"), Furthest),
		Furthest > LeashUU * 0.9f);

	// Degenerate leashes must pin the bot to its post rather than free it — the
	// old code's behaviour on a bad number was the opposite.
	TestTrue(TEXT("a zero leash pins the bot to its post"),
		PatrolPointInLeash(Post, 0.f, 0.3f, 0.9f).Equals(Post, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("a negative leash pins the bot to its post"),
		PatrolPointInLeash(Post, -500.f, 0.7f, 0.4f).Equals(Post, KINDA_SMALL_NUMBER));

	// And the initial target, which is the other half of the bug: the fix is
	// that a bot's first patrol target is its own post, so a leash of any size
	// around a post is still a point at the post when the raid starts.
	TestTrue(TEXT("the post itself is inside its own leash"),
		FVector::Dist2D(PatrolPointInLeash(Post, LeashUU, 0.f, 0.f), Post) <= KINDA_SMALL_NUMBER);
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
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, FiringRange, true, HearingRadius, FiringRange, HysteresisRangeUU, /*bInvestigationActive*/ false)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("chasing at exactly the firing range enters Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, FiringRange, true, HearingRadius, FiringRange, HysteresisRangeUU, /*bInvestigationActive*/ false)),
		static_cast<int32>(ESarkoAIState::Shoot));

	// The discriminating case: identical distance, inside the hysteresis
	// band past FiringRange but past the plain entry threshold too. Only
	// Current differs between these two assertions, and the outcome must
	// differ with it, or the parameter is decorative.
	const float DistanceInHysteresisBand = FiringRange + HysteresisRangeUU * 0.5f;
	TestEqual(TEXT("already shooting, just past the firing range but inside the hysteresis band, stays in Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, DistanceInHysteresisBand, true, HearingRadius, FiringRange, HysteresisRangeUU, /*bInvestigationActive*/ false)),
		static_cast<int32>(ESarkoAIState::Shoot));
	TestEqual(TEXT("chasing at that same distance stays in Chase instead of entering Shoot"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, DistanceInHysteresisBand, true, HearingRadius, FiringRange, HysteresisRangeUU, /*bInvestigationActive*/ false)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("already shooting beyond the hysteresis band drops back to Chase"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, FiringRange + HysteresisRangeUU * 2.f, true, HearingRadius, FiringRange, HysteresisRangeUU, /*bInvestigationActive*/ false)),
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
