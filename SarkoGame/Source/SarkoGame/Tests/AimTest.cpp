#include "Misc/AutomationTest.h"

#include "Pawn/SarkoCharacter.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickMapsToWorldDirection,
	"Sarko.Aim.StickMapsToWorldDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickMapsToWorldDirection::RunTest(const FString& Parameters)
{
	// With the camera unrotated, pushing the stick "up" must move the character
	// away from the viewer, i.e. along +X. Getting this wrong is the classic
	// top-down bug where the character walks sideways relative to your thumb.
	const FVector2D Up = SarkoAim::StickToWorldDirection(FVector2D(0.f, 1.f), 0.f);
	TestTrue(TEXT("stick up maps to +X"), Up.X > 0.9f);
	TestTrue(TEXT("stick up has no sideways component"), FMath::Abs(Up.Y) < 0.01f);

	const FVector2D Right = SarkoAim::StickToWorldDirection(FVector2D(1.f, 0.f), 0.f);
	TestTrue(TEXT("stick right maps to +Y"), Right.Y > 0.9f);

	// Rotating the camera 90 degrees must rotate the mapping with it.
	const FVector2D UpRotated = SarkoAim::StickToWorldDirection(FVector2D(0.f, 1.f), 90.f);
	TestTrue(TEXT("camera yaw rotates the mapping"), UpRotated.Y > 0.9f);

	// A dead stick must not produce a direction, or the character spins.
	const FVector2D Dead = SarkoAim::StickToWorldDirection(FVector2D::ZeroVector, 0.f);
	TestTrue(TEXT("a centred stick yields no direction"), Dead.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMoveIntentScale,
	"Sarko.Aim.MoveIntentScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMoveIntentScale::RunTest(const FString& Parameters)
{
	const float DeadZone = 0.15f;

	// A resting thumb inside the dead zone must not creep the character.
	TestEqual(TEXT("inside the dead zone yields zero scale"),
		SarkoAim::MoveIntentScale(FVector2D(0.05f, 0.05f), DeadZone), 0.f);
	TestEqual(TEXT("exactly centred yields zero scale"),
		SarkoAim::MoveIntentScale(FVector2D::ZeroVector, DeadZone), 0.f);

	// A partial push should give partial speed, not full speed — that is what
	// makes a floating stick feel analog instead of like a d-pad.
	const float PartialScale = SarkoAim::MoveIntentScale(FVector2D(0.5f, 0.f), DeadZone);
	TestTrue(TEXT("partial deflection yields partial scale"), PartialScale > 0.4f && PartialScale < 0.6f);

	// Full deflection must reach exactly full speed.
	TestEqual(TEXT("full deflection yields a scale of 1"),
		SarkoAim::MoveIntentScale(FVector2D(1.f, 0.f), DeadZone), 1.f);

	// An over-dragged stick must clamp so it can never exceed WalkSpeed.
	TestEqual(TEXT("an over-dragged stick clamps to 1"),
		SarkoAim::MoveIntentScale(FVector2D(2.f, 2.f), DeadZone), 1.f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
