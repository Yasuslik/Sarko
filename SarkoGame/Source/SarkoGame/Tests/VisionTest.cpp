#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "UI/SarkoVision.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoVisionConePredicate,
	"Sarko.Vision.ConePredicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoVisionConePredicate::RunTest(const FString& Parameters)
{
	// The angle half of "can I see that". Its failure mode is silent and
	// symmetrical: a cone built on an acos of the dot product loses the sign and
	// therefore reads a bot standing dead astern as standing dead ahead, which is
	// right half the time and is the worst kind of wrong — the player would see
	// exactly the thing they turned away from.
	using namespace SarkoVision;

	const float Half = ConeHalfAngleDegrees(120.f);
	TestEqual(TEXT("a 120 degree cone is 60 degrees either side"), Half, 60.f);

	const FVector2D North(0.f, 1.f);

	// Straight ahead, and the two edges, and just past them. The boundary is
	// INCLUSIVE, so a target exactly on the edge is seen — a strict comparison
	// would put a one-float-wide invisible seam down the middle of the cone's rim.
	TestTrue(TEXT("dead ahead is inside"), IsInsideCone(North, North, Half));
	for (int32 Sign = -1; Sign <= 1; Sign += 2)
	{
		const FVector2D Edge = North.GetRotated(static_cast<float>(Sign) * 59.5f);
		const FVector2D Past = North.GetRotated(static_cast<float>(Sign) * 60.5f);
		TestTrue(FString::Printf(TEXT("%.1f deg off axis is inside"), Sign * 59.5f),
			IsInsideCone(North, Edge, Half));
		TestFalse(FString::Printf(TEXT("%.1f deg off axis is outside"), Sign * 60.5f),
			IsInsideCone(North, Past, Half));
	}

	// THE SIGN, stated on its own. Dead astern must not be dead ahead.
	TestFalse(TEXT("directly behind is outside"), IsInsideCone(North, -North, Half));
	TestEqual(TEXT("directly behind measures 180 degrees"),
		FMath::Abs(SignedAngleDegrees(North, -North)), 180.f, 0.01f);

	// And the two sides are distinguishable, which is what the drawing needs: a
	// predicate that returned only magnitudes could still be right while the fan
	// drawn from it pointed the wrong way.
	TestTrue(TEXT("a target to the left reads positive"), SignedAngleDegrees(North, North.GetRotated(30.f)) > 0.f);
	TestTrue(TEXT("a target to the right reads negative"), SignedAngleDegrees(North, North.GetRotated(-30.f)) < 0.f);

	// Symmetry about the axis, at every angle: the cone has no favourite side.
	for (float Degrees = 0.f; Degrees <= 180.f; Degrees += 7.5f)
	{
		const bool bLeft = IsInsideCone(North, North.GetRotated(Degrees), Half);
		const bool bRight = IsInsideCone(North, North.GetRotated(-Degrees), Half);
		TestEqual(FString::Printf(TEXT("%.1f deg reads the same on both sides"), Degrees), bLeft, bRight);
	}

	// The facing is a replicated FVector_NetQuantizeNormal on the wire and is only
	// approximately unit when it lands, and the direction to a target is a raw
	// subtraction of two world positions measured in hundreds of uu. Neither is
	// normalised by the caller, so the predicate must not care.
	TestTrue(TEXT("an unnormalised facing and a 1200 uu offset still read ahead"),
		IsInsideCone(FVector2D(0.f, 0.997f), FVector2D(0.f, 1200.f), Half));

	// Degenerate input is "in front of me", not a NaN and not a hidden target: a
	// bot standing exactly on the player is touching them, and hiding the thing
	// the player is touching is the one failure worth ruling out by construction.
	TestTrue(TEXT("a zero direction is treated as straight ahead"),
		IsInsideCone(North, FVector2D::ZeroVector, Half));
	TestTrue(TEXT("a zero facing does not hide anything"),
		IsInsideCone(FVector2D::ZeroVector, North, Half));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoVisionVisibilityRule,
	"Sarko.Vision.VisibilityRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoVisionVisibilityRule::RunTest(const FString& Parameters)
{
	// The whole rule, in one statement: inside the cone AND in line of sight AND
	// within range. It exists as one function so no call site can remember two of
	// the three — and the server is the only caller, because a client that draws
	// an enemy it should not see is a cheat surface even in a single-player raid.
	using namespace SarkoVision;

	const float Half = ConeHalfAngleDegrees(120.f);
	const FVector2D North(0.f, 1.f);
	const FVector2D Ahead(0.f, 800.f);
	const FVector2D Behind(0.f, -800.f);

	TestTrue(TEXT("ahead, seen, in range"), IsVisible(North, Ahead, Half, true, 0.f));
	TestFalse(TEXT("ahead but behind cover"), IsVisible(North, Ahead, Half, false, 0.f));
	TestFalse(TEXT("in the open but behind me"), IsVisible(North, Behind, Half, true, 0.f));
	TestFalse(TEXT("behind me AND behind cover"), IsVisible(North, Behind, Half, false, 0.f));

	// Range: non-positive is unlimited, which is the shipped default because a
	// world-locked overhead camera already bounds what is on screen.
	TestTrue(TEXT("zero range means unlimited"), IsVisible(North, FVector2D(0.f, 50000.f), Half, true, 0.f));
	TestTrue(TEXT("just inside a 1000 uu range"), IsVisible(North, FVector2D(0.f, 999.f), Half, true, 1000.f));
	TestFalse(TEXT("just outside a 1000 uu range"), IsVisible(North, FVector2D(0.f, 1001.f), Half, true, 1000.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoVisionEdgeRamp,
	"Sarko.Vision.EdgeRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoVisionEdgeRamp::RunTest(const FString& Parameters)
{
	// The soft edge, which is the difference between "sight falling off" and "a
	// triangle somebody drew over the world" (vision spec §2). The property that
	// matters is MONOTONICITY: a ramp with a dip in it paints a bright ring
	// outside the cone, and a bright ring outside the cone is a second cone the
	// player will try to use.
	using namespace SarkoVision;

	const float Half = 60.f;
	const float Soft = 12.f;
	const float MaxAlpha = 0.55f;

	TestEqual(TEXT("the axis itself is undimmed"), DimAlphaForAngle(0.f, Half, Soft, MaxAlpha), 0.f);
	TestEqual(TEXT("just inside the core is undimmed"), DimAlphaForAngle(59.9f, Half, Soft, MaxAlpha), 0.f);
	TestEqual(TEXT("the core's edge is still undimmed"), DimAlphaForAngle(60.f, Half, Soft, MaxAlpha), 0.f);
	TestEqual(TEXT("past the ramp is the full dim"), DimAlphaForAngle(72.1f, Half, Soft, MaxAlpha), MaxAlpha, 0.001f);
	TestEqual(TEXT("dead astern is the full dim"), DimAlphaForAngle(180.f, Half, Soft, MaxAlpha), MaxAlpha, 0.001f);

	float Previous = -1.f;
	int32 DistinctPlateaus = 0;
	float LastValue = -1.f;
	for (float Degrees = 0.f; Degrees <= 180.f; Degrees += 0.25f)
	{
		const float Alpha = DimAlphaForAngle(Degrees, Half, Soft, MaxAlpha);
		TestTrue(FString::Printf(TEXT("%.2f deg does not brighten as the angle grows (%.3f after %.3f)"),
			Degrees, Alpha, Previous), Alpha >= Previous - KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("%.2f deg never exceeds the ceiling"), Degrees), Alpha <= MaxAlpha + KINDA_SMALL_NUMBER);
		if (!FMath::IsNearlyEqual(Alpha, LastValue))
		{
			++DistinctPlateaus;
			LastValue = Alpha;
		}
		Previous = Alpha;
	}

	// STEPPED, as the spec asks: the lit core, then EdgeSteps plateaus. The fan
	// samples this at a handful of angles and interpolates between the samples, so
	// a plateau either side of every sample is what makes the drawn gradient land
	// on this ramp rather than on a straight line through it.
	TestEqual(TEXT("the ramp is the core plus EdgeSteps plateaus"), DistinctPlateaus, EdgeSteps + 1);

	// The sign is discarded — the cone is symmetric and nothing in the game
	// distinguishes its left edge from its right.
	TestEqual(TEXT("the ramp is symmetric about the axis"),
		DimAlphaForAngle(-66.f, Half, Soft, MaxAlpha), DimAlphaForAngle(66.f, Half, Soft, MaxAlpha));

	// A zero soft edge is a hard wedge and must not divide by it.
	TestEqual(TEXT("no soft edge means a hard boundary"), DimAlphaForAngle(60.1f, Half, 0.f, MaxAlpha), MaxAlpha, 0.001f);
	TestEqual(TEXT("no soft edge still leaves the core lit"), DimAlphaForAngle(59.9f, Half, 0.f, MaxAlpha), 0.f);

	// THE NEAR HALO. The apex of a triangle fan has one colour, so without this
	// the player's own feet are drawn at the full dim on three sides.
	TestEqual(TEXT("the pawn's own position is fully lit"), NearHaloScale(0.f, 350.f), 0.f);
	TestEqual(TEXT("the halo's rim is the full dim"), NearHaloScale(350.f, 350.f), 1.f);
	TestEqual(TEXT("beyond the halo is still the full dim"), NearHaloScale(5000.f, 350.f), 1.f);
	TestEqual(TEXT("half way out is half the dim"), NearHaloScale(175.f, 350.f), 0.5f, 0.001f);
	TestEqual(TEXT("no halo dims from the apex"), NearHaloScale(0.f, 0.f), 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoVisionSettingBounds,
	"Sarko.Vision.SettingBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoVisionSettingBounds::RunTest(const FString& Parameters)
{
	// These are config values, so every one of them can be typed into an .ini by
	// somebody who is tuning the dim level on a phone at two in the morning. The
	// bounds exist so that the worst outcome of that is a game that looks wrong,
	// never one that cannot be played at all — which is the spec's first named
	// risk, "too dark is unplayable".
	using namespace SarkoVision;

	TestEqual(TEXT("a keyhole is widened to the floor"), ClampConeDegrees(5.f), MinConeDegrees);
	TestEqual(TEXT("a negative cone is widened to the floor"), ClampConeDegrees(-90.f), MinConeDegrees);
	TestEqual(TEXT("a full circle is pulled back below 360"), ClampConeDegrees(360.f), MaxConeDegrees);
	TestEqual(TEXT("120 is passed through untouched"), ClampConeDegrees(120.f), 120.f);

	// THE CEILING ON THE DIM IS THE FEATURE'S CONTRACT: geometry is never hidden.
	// An .ini cannot black the screen out, whatever it says.
	TestEqual(TEXT("a blackout is clamped to the ceiling"), ClampDimAlpha(1.f), MaxDimAlpha);
	TestEqual(TEXT("a value past 1 is clamped to the ceiling"), ClampDimAlpha(4.f), MaxDimAlpha);
	TestEqual(TEXT("a negative dim is no dim"), ClampDimAlpha(-1.f), 0.f);
	TestTrue(TEXT("the ceiling still lets the world through"), MaxDimAlpha < 1.f);

	// The ramp can never eat the cone it hangs off.
	TestEqual(TEXT("a ramp wider than the cone is cut to it"), ClampSoftEdgeDegrees(200.f, 25.f), 25.f);
	TestEqual(TEXT("a ramp is capped at the maximum"), ClampSoftEdgeDegrees(200.f, 175.f), MaxSoftEdgeDegrees);
	TestEqual(TEXT("a negative ramp is a hard edge"), ClampSoftEdgeDegrees(-5.f, 60.f), 0.f);

	// AND THE SHIPPED DEFAULTS LAND INSIDE ALL OF IT, unclamped. A default that
	// only works because it is clamped is a default nobody chose.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	TestEqual(TEXT("the shipped cone needs no clamping"),
		ClampConeDegrees(Settings.VisionConeDegrees), Settings.VisionConeDegrees);
	TestEqual(TEXT("the shipped dim needs no clamping"),
		ClampDimAlpha(Settings.VisionDimAlpha), Settings.VisionDimAlpha);
	TestEqual(TEXT("the shipped soft edge needs no clamping"),
		ClampSoftEdgeDegrees(Settings.VisionConeSoftEdgeDegrees,
			ConeHalfAngleDegrees(Settings.VisionConeDegrees)), Settings.VisionConeSoftEdgeDegrees);

	// The spec's own number: start generous, 110-120, and tune DOWN only if play
	// says it is too easy. A cone narrower than 110 on a phone reads as a bug.
	TestTrue(FString::Printf(TEXT("the shipped cone is at least 110 degrees (%.0f)"), Settings.VisionConeDegrees),
		Settings.VisionConeDegrees >= 110.f);

	// And the dim is a tint rather than a curtain, with room left to go darker if
	// a playtest asks for it.
	TestTrue(FString::Printf(TEXT("the shipped dim leaves the world readable (%.2f)"), Settings.VisionDimAlpha),
		Settings.VisionDimAlpha > 0.f && Settings.VisionDimAlpha <= 0.7f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoVisionBotsAreAnswerable,
	"Sarko.Vision.BotsAreAnswerable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoVisionBotsAreAnswerable::RunTest(const FString& Parameters)
{
	// Vision spec §5: "bots must not gain an advantage the player cannot answer.
	// Verify the player's cone is not narrower than a bot's effective awareness,
	// or the fight is unfair in a way that reads as cheating."
	//
	// Before this stage a bot's awareness was 360 degrees by omission —
	// AController::LineOfSightTo is a trace and nothing else. That was invisible
	// while the player could see the whole screen and stops being invisible the
	// instant the player's own sight is cut to a cone. The bot's arc stays WIDER
	// than the player's (it has no HUD, no damage arc and no intent), but it is
	// now finite, which is what makes "come at it from behind" a move the player
	// can make and see the result of.
	using namespace SarkoVision;
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	TestTrue(FString::Printf(TEXT("a bot no longer sees through the back of its head (%.0f deg)"),
		Settings.EnemyVisionConeDegrees), Settings.EnemyVisionConeDegrees < 360.f);

	// The one approach the player must be able to make. Directly behind a bot is
	// outside its cone at any setting below 360.
	const float BotHalf = ConeHalfAngleDegrees(Settings.EnemyVisionConeDegrees);
	const FVector2D BotFacing(1.f, 0.f);
	TestFalse(TEXT("a player directly behind a bot is not seen"),
		IsInsideCone(BotFacing, -BotFacing, BotHalf));
	TestTrue(TEXT("a player directly in front of a bot is seen"),
		IsInsideCone(BotFacing, BotFacing, BotHalf));

	// The asymmetry is bounded and deliberate: wider than the player, not
	// unlimited. If someone widens the bots past a full circle's worth of
	// advantage this fails and says so.
	TestTrue(FString::Printf(TEXT("the bot's arc is at most twice the player's (%.0f vs %.0f)"),
		Settings.EnemyVisionConeDegrees, Settings.VisionConeDegrees),
		Settings.EnemyVisionConeDegrees <= Settings.VisionConeDegrees * 2.f);

	// And a bot still cannot see further than it could before: this stage narrows
	// awareness, it does not extend it.
	TestEqual(TEXT("the bot's sight range is unchanged"), Settings.EnemySightRangeUU, 1600.f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
