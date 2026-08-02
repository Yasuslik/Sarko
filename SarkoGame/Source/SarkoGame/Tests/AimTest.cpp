#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidSettings.h"
#include "Pawn/SarkoCharacter.h"
#include "UI/SarkoUiScale.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTouchZonesSplitTheScreen,
	"Sarko.Input.TouchZonesSplitTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTouchZonesSplitTheScreen::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(2400.f, 1080.f);

	TestTrue(TEXT("a touch on the left is the move stick"), SarkoInput::IsLeftHalf(FVector2D(300.f, 900.f), Viewport));
	TestFalse(TEXT("a touch on the right is the aim stick"), SarkoInput::IsLeftHalf(FVector2D(2100.f, 900.f), Viewport));
	TestTrue(TEXT("the boundary belongs to the left"), SarkoInput::IsLeftHalf(FVector2D(1199.f, 500.f), Viewport));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickIsFloatingAndNormalised,
	"Sarko.Input.StickIsFloatingAndNormalised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickIsFloatingAndNormalised::RunTest(const FString& Parameters)
{
	// The radius is a POINT size resolved per viewport now, not a fixed 100 px,
	// so the drag this test makes is expressed the same way the game expresses it
	// — otherwise the test pins a pixel count the code no longer has.
	const FVector2D Viewport(2556.f, 1179.f);
	const float Radius = SarkoInput::StickRadiusPxForViewport(Viewport);

	FSarkoTouchStick Stick;
	Stick.bActive = true;
	Stick.RadiusPx = Radius;
	// The origin is wherever the thumb landed — a fixed rosette is the known
	// failure mode on phones, so the stick must be relative to its own origin.
	Stick.Origin = FVector2D(700.f, 800.f);
	Stick.Current = FVector2D(700.f, 800.f - Radius); // dragged up to full deflection

	const FVector2D Value = Stick.Value();
	TestTrue(TEXT("dragging up gives a positive Y"), Value.Y > 0.9f);
	TestTrue(TEXT("the value is clamped to unit length"), Value.Size() <= 1.001f);
	TestTrue(TEXT("a drag of exactly the radius IS full deflection"),
		FMath::IsNearlyEqual(Value.Size(), 1.f, 0.01f));

	// Half the radius is half the deflection — that is what makes the stick
	// analog rather than a d-pad, and it is now half of a POINT distance.
	Stick.Current = FVector2D(700.f, 800.f - Radius * 0.5f);
	TestTrue(TEXT("half the radius is half the deflection"),
		FMath::IsNearlyEqual(Stick.Value().Size(), 0.5f, 0.01f));

	// Beyond the stick radius the value saturates instead of growing.
	Stick.Current = FVector2D(700.f, 800.f - Radius * 4.f);
	TestTrue(TEXT("a long drag saturates at 1"), FMath::IsNearlyEqual(Stick.Value().Size(), 1.f, 0.01f));

	// A thumb that has not moved must not steer.
	Stick.Current = Stick.Origin;
	TestTrue(TEXT("no drag means no input"), Stick.Value().IsNearlyZero());

	// A stick that was never anchored must not divide by zero and read full
	// deflection off a single pixel of travel.
	FSarkoTouchStick Fresh;
	Fresh.bActive = true;
	Fresh.Origin = FVector2D(100.f, 100.f);
	Fresh.Current = FVector2D(100.f, 99.f);
	TestTrue(TEXT("an unanchored stick still reads a small drag as small"),
		Fresh.Value().Size() < 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickRadiusIsTheSameSizeOnEveryScreen,
	"Sarko.Input.StickRadiusIsTheSameSizeOnEveryScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickRadiusIsTheSameSizeOnEveryScreen::RunTest(const FString& Parameters)
{
	// THE BUG THIS PINS. The radius was a literal 100 px, the one input constant
	// in the project that was not point-scaled. On a 2556x1179 phone (3.02 px/pt)
	// that was 33 pt of thumb travel to full deflection — below the 44 pt minimum
	// this project's own tests assert on both thumb buttons — while in a Mac
	// editor window (1.85 px/pt) it was 54 pt and felt fine. That gap is why it
	// survived: every screen a developer looked at was the forgiving one.
	const FVector2D Screens[] = {
		FVector2D(2556.f, 1179.f),   // iPhone 14/15 Pro landscape, ~3.02 px/pt
		FVector2D(1560.f, 720.f),    // a cheap phone at 2x, ~1.85 px/pt
		FVector2D(1280.f, 720.f),    // a small desktop window
	};

	for (const FVector2D& Screen : Screens)
	{
		const float Scale = SarkoUI::PointScaleForViewport(Screen);
		const float RadiusPx = SarkoInput::StickRadiusPxForViewport(Screen);
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f (%.2f px/pt)"), Screen.X, Screen.Y, Scale);

		// The whole point: the same physical distance on the glass, everywhere.
		TestTrue(*FString::Printf(TEXT("%s: full deflection is 52 pt"), *Where),
			FMath::IsNearlyEqual(RadiusPx / Scale, SarkoInput::StickRadiusPt, 0.01f));

		// And it clears the tap-target floor the buttons are held to, which the
		// old 33 pt did not.
		TestTrue(*FString::Printf(TEXT("%s: full deflection clears 44 pt"), *Where),
			RadiusPx / Scale >= 44.f);

		// The fire threshold and the walk/run boundary are fractions of it, so
		// they inherit the fix. 0.35 of 52 pt is 18 pt — a deliberate push, where
		// 11.6 pt was inside a thumb's own contact patch.
		const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
		TestTrue(*FString::Printf(TEXT("%s: the fire threshold is a real push"), *Where),
			(RadiusPx * Settings.AimFireDeadZone) / Scale >= 15.f);
		TestTrue(*FString::Printf(TEXT("%s: the quiet-walk band is a holdable annulus"), *Where),
			(RadiusPx * (Settings.NoiseRunSpeedFraction - Settings.MoveStickDeadZone)) / Scale >= 25.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoQuietRingSitsOnTheNoiseBoundary,
	"Sarko.Input.QuietRingSitsOnTheNoiseBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoQuietRingSitsOnTheNoiseBoundary::RunTest(const FString& Parameters)
{
	// ASarkoHUD::DrawStick draws the move stick's second, dimmer ring at
	// NoiseRunSpeedFraction of the stick's own resolved radius. Neither number is
	// written in the HUD: the fraction is read from the settings the SERVER
	// splits quiet from audible with (ASarkoCharacter::ReportMovementNoise), and
	// the radius is the stick's. This pins the composition — that the ring lands
	// on the boundary, strictly inside the stick and strictly outside the dead
	// zone — so a ring that stopped meaning the rule would fail here rather than
	// quietly teach the wrong thumb position.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Fraction = Settings.NoiseRunSpeedFraction;

	TestTrue(TEXT("the boundary is inside the ring, not on it"), Fraction < 1.f);
	TestTrue(TEXT("the boundary is outside the move dead zone"), Fraction > Settings.MoveStickDeadZone);

	const FVector2D Screens[] = { FVector2D(2556.f, 1179.f), FVector2D(1560.f, 720.f) };
	for (const FVector2D& Screen : Screens)
	{
		const float Scale = SarkoUI::PointScaleForViewport(Screen);
		const float RadiusPx = SarkoInput::StickRadiusPxForViewport(Screen);
		const float QuietRingPt = (RadiusPx * Fraction) / Scale;
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Screen.X, Screen.Y);

		// 0.7 of 52 pt = 36.4 pt, the same distance on every screen — which is
		// what makes it a place a thumb can learn. At the old pixel radius it was
		// 23 pt on a phone and 39 pt in the editor, i.e. two different rules.
		TestTrue(*FString::Printf(TEXT("%s: the quiet ring is 36.4 pt"), *Where),
			FMath::IsNearlyEqual(QuietRingPt, SarkoInput::StickRadiusPt * Fraction, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: it is drawn strictly inside the stick's ring"), *Where),
			QuietRingPt < RadiusPx / Scale);
		TestTrue(*FString::Printf(TEXT("%s: and clear of the dead zone by a holdable margin"), *Where),
			QuietRingPt - (RadiusPx * Settings.MoveStickDeadZone) / Scale >= 25.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoADirectionlessReleaseDoesNotFire,
	"Sarko.Input.ADirectionlessReleaseDoesNotFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoADirectionlessReleaseDoesNotFire::RunTest(const FString& Parameters)
{
	const float DeadZone = GetDefault<USarkoRaidSettings>()->MoveStickDeadZone;

	// THE BUG. A zero-deflection tap on the right half used to fire a round —
	// and along a STALE ray, because ASarkoCharacter::SetAimIntent leaves
	// AimDirection alone when the stick is centred. A stray touch, a re-grip or a
	// thumb steadying the phone was a gunshot, and a gunshot is the loudest event
	// this game models: 2600 uu against a 450 uu walk.
	TestFalse(TEXT("a touch that never moved does not fire"),
		SarkoInput::ShouldFireOnRelease(FVector2D::ZeroVector, DeadZone));

	// THE CANCEL. Dragged out, then back onto the anchor, then released: no
	// shot. The genre's abort gesture, and the only one this scheme has.
	TestFalse(TEXT("dragging back to the anchor cancels the shot"),
		SarkoInput::ShouldFireOnRelease(FVector2D(0.05f, 0.05f), DeadZone));
	TestFalse(TEXT("just inside the dead zone is still a cancel"),
		SarkoInput::ShouldFireOnRelease(FVector2D(DeadZone * 0.99f, 0.f), DeadZone));

	// AND THE FLICK STILL FIRES. A real tap — past the move dead zone, short of
	// the 0.35 fire threshold — is the aimed single shot, and it must survive
	// both rules above or the scheme loses its only precise attack.
	TestTrue(TEXT("a flick with a direction still fires"),
		SarkoInput::ShouldFireOnRelease(FVector2D(0.25f, 0.f), DeadZone));
	TestTrue(TEXT("a full-deflection release fires"),
		SarkoInput::ShouldFireOnRelease(FVector2D(0.f, -1.f), DeadZone));

	// The cancel band is the MOVE dead zone and not the fire one, or the tap
	// gesture would have nowhere to live: everything below 0.35 would cancel and
	// everything above it would already have fired on the hold.
	TestTrue(TEXT("the cancel band is below the fire threshold"),
		GetDefault<USarkoRaidSettings>()->MoveStickDeadZone
			< GetDefault<USarkoRaidSettings>()->AimFireDeadZone);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
