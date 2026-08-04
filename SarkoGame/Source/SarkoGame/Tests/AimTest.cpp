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
		// they inherit the fix. 0.70 of 52 pt is 36 pt — past halfway out, a
		// distance a thumb has to mean to travel. It read 0.35 here (18 pt) until
		// the first phone playtest, where 18 pt turned out to be the ordinary
		// sweep of a thumb that was only trying to turn, and the magazine went
		// into the scenery.
		const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
		TestTrue(*FString::Printf(TEXT("%s: the fire threshold is a deliberate push"), *Where),
			(RadiusPx * Settings.AimFireDeadZone) / Scale >= 30.f);
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
	FSarkoReleasingTheAimStickNeverFires,
	"Sarko.Input.ReleasingTheAimStickNeverFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReleasingTheAimStickNeverFires::RunTest(const FString& Parameters)
{
	// This test used to be Sarko.Input.ADirectionlessReleaseDoesNotFire, and it
	// pinned SarkoInput::ShouldFireOnRelease: lifting the thumb fired one round if
	// the hold had gone anywhere past the move dead zone. That function is gone,
	// and the two bugs it defended against are now unreachable rather than merely
	// handled — so the same three gestures are asked of the rule that replaced it,
	// SarkoInput::AimZoneFor, and the answer for every one of them is the same.
	//
	// WHY THE RELEASE STOPPED FIRING. The flick was a fair trade at a 0.35 fire
	// threshold: its band was 8 pt to 18 pt of a 52 pt stick, narrow enough that
	// you only landed in it deliberately. At 0.70 that band is 8 pt to 36 pt — the
	// whole AIM zone, where every considered turn-and-look now ends. "Release
	// fires" would have meant every aim ending in a gunshot, which is the phone
	// playtest's complaint moved one gesture to the left.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Move = Settings.MoveStickDeadZone;
	const float Fire = Settings.AimFireDeadZone;

	using ESarkoAimZone = SarkoInput::ESarkoAimZone;
	const auto Zone = [Move, Fire](FVector2D Value)
	{
		return SarkoInput::AimZoneFor(Value, Move, Fire);
	};

	// THE ORIGINAL BUG, still pinned. A zero-deflection tap on the right half once
	// fired a round along a STALE ray, because ASarkoCharacter::SetAimIntent
	// leaves AimDirection alone when the stick is centred. A stray touch, a
	// re-grip or a thumb steadying the phone was a gunshot — the loudest event
	// this game models, 2600 uu against a 450 uu walk. It is not in the Fire zone,
	// and nothing but the Fire zone fires.
	TestTrue(TEXT("a touch that never moved is at rest, not firing"),
		Zone(FVector2D::ZeroVector) == ESarkoAimZone::Rest);

	// THE CANCEL, still pinned. Dragged out, then back onto the anchor, then
	// released: no shot. It is now the same statement as the one above — the
	// thumb is back at rest, and rest fires nothing.
	TestTrue(TEXT("dragging back to the anchor lands back at rest"),
		Zone(FVector2D(0.05f, 0.05f)) == ESarkoAimZone::Rest);
	TestTrue(TEXT("just inside the move dead zone is still rest"),
		Zone(FVector2D(Move * 0.99f, 0.f)) == ESarkoAimZone::Rest);

	// AND THE FLICK NO LONGER FIRES — the honest change. A quick drag to a quarter
	// or a half of the travel is AIMING. The pawn turns, the cone follows, the
	// magazine is untouched, and lifting the thumb there does nothing at all.
	TestTrue(TEXT("a flick to a quarter of the travel aims and does not fire"),
		Zone(FVector2D(0.25f, 0.f)) == ESarkoAimZone::Aim);
	TestTrue(TEXT("...and so does a much larger one, right up to the ring"),
		Zone(FVector2D(0.f, -(Fire - 0.01f))) == ESarkoAimZone::Aim);

	// THE SINGLE AIMED SHOT SURVIVES, in the one place the player can see. Push
	// past the ring and lift: the crossing frame fires once and the next round is
	// MinFireIntervalSeconds away, so a push-and-lift spends exactly one.
	TestTrue(TEXT("a push past the ring is the deliberate single shot"),
		Zone(FVector2D(0.f, -1.f)) == ESarkoAimZone::Fire);
	TestTrue(TEXT("one push cannot spend two rounds inside the fire interval"),
		Settings.MinFireIntervalSeconds > 0.f);

	// The zones have to nest in this order or one of them has no width, and the
	// one that would vanish is the AIM zone this whole change exists to create.
	TestTrue(TEXT("rest, then aim, then fire — in that order and all non-empty"),
		Move > 0.f && Move < Fire && Fire < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheAimStickHasTwoZones,
	"Sarko.Input.TheAimStickHasTwoZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheAimStickHasTwoZones::RunTest(const FString& Parameters)
{
	// THE FINDING THIS EXISTS FOR. The first time this game was played on a real
	// iPhone the owner emptied an eight-round magazine into nothing on his way to
	// a target: "стрельба сделана плохо, я выстрелял сразу все патроны в никуда —
	// возможно нужно стик прицела разделить на 2 части, типа когда я довожу до
	// 70% то он стреляет". He was right, and 0.70 is his number.
	//
	// The pure rule, at the boundaries, independent of any screen.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	using ESarkoAimZone = SarkoInput::ESarkoAimZone;

	// THE SHIPPED THRESHOLD. It was 0.35, which is why aiming without shooting was
	// not a thing this control scheme offered.
	TestTrue(TEXT("the fire boundary ships at 0.70"),
		FMath::IsNearlyEqual(Settings.AimFireDeadZone, 0.70f, 0.001f));

	// EXACT BOUNDARIES, with the settings' own numbers rather than literals, so
	// this keeps testing the rule if the tuning moves.
	const float Move = Settings.MoveStickDeadZone;
	const float Fire = Settings.AimFireDeadZone;

	TestTrue(TEXT("dead centre is rest"),
		SarkoInput::AimZoneFor(FVector2D::ZeroVector, Move, Fire) == ESarkoAimZone::Rest);
	TestTrue(TEXT("exactly on the move dead zone, aiming begins"),
		SarkoInput::AimZoneFor(FVector2D(Move, 0.f), Move, Fire) == ESarkoAimZone::Aim);
	TestTrue(TEXT("a hair short of the fire ring is still only aiming"),
		SarkoInput::AimZoneFor(FVector2D(Fire * 0.999f, 0.f), Move, Fire) == ESarkoAimZone::Aim);
	TestTrue(TEXT("exactly on the fire ring, it fires"),
		SarkoInput::AimZoneFor(FVector2D(Fire, 0.f), Move, Fire) == ESarkoAimZone::Fire);
	TestTrue(TEXT("full deflection fires"),
		SarkoInput::AimZoneFor(FVector2D(0.f, -1.f), Move, Fire) == ESarkoAimZone::Fire);

	// The zone is a function of the DISTANCE and not of either axis, or a diagonal
	// thumb would be judged by a different rule than a vertical one.
	const float Diagonal = Fire / FMath::Sqrt(2.f);
	TestTrue(TEXT("a diagonal at the ring's distance fires"),
		SarkoInput::AimZoneFor(FVector2D(Diagonal, Diagonal), Move, Fire) == ESarkoAimZone::Fire);
	TestTrue(TEXT("...and one just inside it does not"),
		SarkoInput::AimZoneFor(FVector2D(Diagonal * 0.99f, Diagonal * 0.99f), Move, Fire)
			== ESarkoAimZone::Aim);

	// A ZERO SETTING MUST NOT MAKE A RESTING THUMB SHOOT. Both bounds are floored
	// at KINDA_SMALL_NUMBER, so the worst a mistyped .ini can do is remove the aim
	// band — not fire a weapon nobody touched.
	TestTrue(TEXT("a zero threshold still does not fire an untouched stick"),
		SarkoInput::AimZoneFor(FVector2D::ZeroVector, 0.f, 0.f) == ESarkoAimZone::Rest);

	// ShouldFireWhileHeld is the same rule and not a second one — it is what the
	// tick asks, and it must agree with the classifier the ring is drawn from.
	TestEqual(TEXT("the boolean the tick asks agrees with the zone, inside"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(Fire * 0.99f, 0.f), Fire), false);
	TestEqual(TEXT("...and outside"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(Fire, 0.f), Fire), true);

	// AND THE RING IS THE RULE, DRAWN. ASarkoHUD::DrawStick puts the aim stick's
	// inner ring at exactly this fraction of the stick's own resolved radius, the
	// same way the move stick's sits on NoiseRunSpeedFraction. Neither number is
	// written in the HUD. What the geometry has to deliver is a boundary a thumb
	// can find and then STOP at, on every screen.
	const FVector2D Screens[] = { FVector2D(2556.f, 1179.f), FVector2D(1560.f, 720.f) };
	for (const FVector2D& Screen : Screens)
	{
		const float Scale = SarkoUI::PointScaleForViewport(Screen);
		const float RadiusPx = SarkoInput::StickRadiusPxForViewport(Screen);
		const float FireRingPt = (RadiusPx * Fire) / Scale;
		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Screen.X, Screen.Y);

		// 0.70 of 52 pt = 36.4 pt, the same distance on every screen — which is
		// what makes it a place a thumb can learn rather than a place it discovers.
		TestTrue(*FString::Printf(TEXT("%s: the fire ring is 36.4 pt out"), *Where),
			FMath::IsNearlyEqual(FireRingPt, SarkoInput::StickRadiusPt * Fire, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: it is drawn strictly inside the stick's ring"), *Where),
			FireRingPt < RadiusPx / Scale);

		// THE AIM ZONE IS A PLACE YOU CAN REST. 8 pt to 36 pt is a 28 pt band —
		// wider than a thumb's own contact patch, which is what "aiming without
		// shooting" needs in order to be a pose rather than a balancing act. At the
		// old 0.35 it was 8 pt to 18 pt: a 10 pt band, narrower than the thumb
		// holding it, and that is the bug in one number.
		const float AimBandPt = FireRingPt - (RadiusPx * Move) / Scale;
		TestTrue(*FString::Printf(TEXT("%s: the aim band is %.1f pt, wide enough to hold"), *Where, AimBandPt),
			AimBandPt >= 25.f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
