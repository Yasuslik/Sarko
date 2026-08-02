#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "UI/SarkoCombatFeedback.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFeedbackDamageDirectionSurvivesTheWire,
	"Sarko.Feedback.DamageDirectionSurvivesTheWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFeedbackDamageDirectionSurvivesTheWire::RunTest(const FString& Parameters)
{
	// The damage direction rides along with health in ONE BYTE (spec §4.1), so
	// the whole correctness of the arc rests on this round trip. The failure it
	// guards is silent and systematic: an arc that points a few degrees off, or
	// that mirrors at the 0/360 wrap, is wrong in a way nobody files and everybody
	// feels under a world-locked camera.
	using namespace SarkoFeedback;

	// 360/256 = 1.40625 degrees per bucket, so no honest round trip can be worse
	// than half of that plus the bucket-centre offset. One bucket is the bound.
	const float BucketDegrees = 360.f / 256.f;

	for (int32 Degrees = -720; Degrees <= 720; Degrees += 3)
	{
		const float Input = static_cast<float>(Degrees);
		const float Decoded = ByteToYaw(YawToByte(Input));

		// Compared as a WRAPPED difference: 359.6 decoding to 0.7 is a 1.1 degree
		// error, not a 358.9 degree one, and a naive subtraction would call the
		// single most common case in the game a catastrophic failure.
		const float Wrapped = FMath::Fmod(FMath::Fmod(Input, 360.f) + 360.f, 360.f);
		const float Delta = FMath::Abs(FMath::UnwindDegrees(Decoded - Wrapped));
		TestTrue(FString::Printf(TEXT("%.0f deg survives the byte (decoded %.2f, off by %.2f deg)"),
			Input, Decoded, Delta), Delta <= BucketDegrees);
	}

	// The wrap itself, stated: a shooter just clockwise of due north and one just
	// anticlockwise of it must not end up on opposite sides of the pawn.
	TestEqual(TEXT("359.9 and -0.1 quantise to the same byte"),
		static_cast<int32>(YawToByte(359.9f)), static_cast<int32>(YawToByte(-0.1f)));
	TestEqual(TEXT("0 and 360 quantise to the same byte"),
		static_cast<int32>(YawToByte(0.f)), static_cast<int32>(YawToByte(360.f)));

	// And a guard against the whole thing collapsing to one value, which would
	// pass every tolerance above by pointing every arc the same way.
	TSet<int32> Distinct;
	for (int32 Degrees = 0; Degrees < 360; Degrees += 10)
	{
		Distinct.Add(static_cast<int32>(YawToByte(static_cast<float>(Degrees))));
	}
	TestEqual(TEXT("thirty-six different directions produce thirty-six different bytes"), Distinct.Num(), 36);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFeedbackArcGeometry,
	"Sarko.Feedback.ArcGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFeedbackArcGeometry::RunTest(const FString& Parameters)
{
	// The arc is a short polyline around a circle, and the one property that
	// matters is that it is CENTRED on the direction it reports. An off-by-one in
	// the boundary count draws it slightly to one side, which is a marker that
	// lies about where the shot came from.
	using namespace SarkoFeedback;

	const float Centre = 1.f;              // radians, deliberately not zero
	const float Span = FMath::DegreesToRadians(44.f);
	const int32 Segments = 9;

	TestTrue(TEXT("the first boundary is half a span anticlockwise of centre"),
		FMath::IsNearlyEqual(ArcSegmentAngle(Centre, Span, 0, Segments), Centre - Span * 0.5f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("the last boundary is half a span clockwise of centre"),
		FMath::IsNearlyEqual(ArcSegmentAngle(Centre, Span, Segments, Segments), Centre + Span * 0.5f, KINDA_SMALL_NUMBER));

	// The midpoint IS the reported direction. With an odd segment count this
	// lands between two boundaries, so it is asserted as the average of the pair
	// that straddle it — which is exactly what "centred" means and exactly what an
	// off-by-one breaks.
	const float BelowCentre = ArcSegmentAngle(Centre, Span, Segments / 2, Segments);
	const float AboveCentre = ArcSegmentAngle(Centre, Span, Segments / 2 + 1, Segments);
	TestTrue(TEXT("the arc is centred on the direction it reports"),
		FMath::IsNearlyEqual((BelowCentre + AboveCentre) * 0.5f, Centre, 0.001f));

	// Monotone, so the polyline sweeps one way rather than folding back on itself.
	float Previous = ArcSegmentAngle(Centre, Span, 0, Segments);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const float Angle = ArcSegmentAngle(Centre, Span, Index, Segments);
		TestTrue(FString::Printf(TEXT("boundary %d sweeps past boundary %d"), Index, Index - 1), Angle > Previous);
		Previous = Angle;
	}

	// Degenerate inputs must not produce NaNs on a draw path.
	TestTrue(TEXT("no segments at all collapses to the centre rather than dividing by zero"),
		FMath::IsNearlyEqual(ArcSegmentAngle(Centre, Span, 0, 0), Centre, KINDA_SMALL_NUMBER));

	// THE FADE. Full at the moment of the hit, gone at the end, and clamped at
	// both ends so a clock that jumps (a travel, a hitch) cannot draw a stale arc
	// or an over-bright one.
	const float Lifetime = GetDefault<USarkoRaidSettings>()->DamageArcSeconds;
	TestTrue(TEXT("an arc is full strength on the frame the hit lands"),
		FMath::IsNearlyEqual(FadeAlpha(0.f, Lifetime), 1.f));
	TestTrue(TEXT("and gone exactly at its lifetime"), FadeAlpha(Lifetime, Lifetime) <= 0.f);
	TestTrue(TEXT("a stale arc never draws"), FadeAlpha(Lifetime * 10.f, Lifetime) == 0.f);
	TestTrue(TEXT("a clock that ran backwards does not draw brighter than full"),
		FadeAlpha(-5.f, Lifetime) <= 1.f);
	TestTrue(TEXT("a zero lifetime draws nothing rather than dividing by zero"),
		FadeAlpha(0.f, 0.f) == 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFeedbackArcRingIsBounded,
	"Sarko.Feedback.ArcRingIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFeedbackArcRingIsBounded::RunTest(const FString& Parameters)
{
	// Overlapping arcs are allowed — being shot from two sides at once is exactly
	// when the player most needs to be told — but this is written from DrawHUD,
	// which is a tick path, so the capacity is fixed and nothing here may ever
	// allocate.
	using namespace SarkoFeedback;

	FDamageArcRing Ring;
	TestEqual(TEXT("a fresh ring holds no arcs"), Ring.Num(), 0);

	for (int32 Index = 0; Index < MaxDamageArcs * 5; ++Index)
	{
		Ring.Add(static_cast<float>(Index) * 11.f, static_cast<float>(Index));
		TestTrue(FString::Printf(TEXT("the ring never grows past %d (holds %d after %d hits)"),
			MaxDamageArcs, Ring.Num(), Index + 1), Ring.Num() <= MaxDamageArcs);
	}
	TestEqual(TEXT("and fills to exactly its bound"), Ring.Num(), MaxDamageArcs);

	// The NEWEST arcs are the ones kept: a ring that dropped new entries once full
	// would stop telling the player where they are being shot from at precisely
	// the moment they are being shot from four directions.
	float Newest = -1000.f;
	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		Newest = FMath::Max(Newest, Ring.Get(Index).StartSeconds);
	}
	TestEqual(TEXT("the most recent hit is still in the ring after five wraps"),
		Newest, static_cast<float>(MaxDamageArcs * 5 - 1));

	// Four distinct directions survive together, which is the whole reason the
	// ring is not a single slot.
	TSet<float> Directions;
	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		Directions.Add(Ring.Get(Index).YawDegrees);
	}
	TestEqual(TEXT("four overlapping arcs are four directions, not one"), Directions.Num(), MaxDamageArcs);

	Ring.Reset();
	TestEqual(TEXT("and it can be emptied"), Ring.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFeedbackVignetteOnlyAtLowHealth,
	"Sarko.Feedback.VignetteOnlyAtLowHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFeedbackVignetteOnlyAtLowHealth::RunTest(const FString& Parameters)
{
	// The vignette must be ABSENT above the threshold, not faint. An effect that
	// is always slightly on is an effect the player stops seeing, and the whole
	// job of this one is to be noticed at the moment it appears.
	using namespace SarkoFeedback;

	const float Threshold = GetDefault<USarkoRaidSettings>()->LowHealthVignetteHealth;
	TestTrue(TEXT("the threshold is a real fraction of a health pool"), Threshold > 0.f && Threshold < 100.f);

	TestEqual(TEXT("a healthy player sees nothing"), VignetteIntensity(100.f, Threshold), 0.f);
	TestEqual(TEXT("and nothing at exactly the threshold — the boundary belongs to fine"),
		VignetteIntensity(Threshold, Threshold), 0.f);
	TestTrue(TEXT("one point below it, it appears"), VignetteIntensity(Threshold - 1.f, Threshold) > 0.f);
	TestTrue(TEXT("and it is loudest at death"),
		FMath::IsNearlyEqual(VignetteIntensity(0.f, Threshold), 1.f));

	// Monotone, so "getting worse" always looks like getting worse.
	float Previous = 0.f;
	for (float Health = Threshold; Health >= 0.f; Health -= 1.f)
	{
		const float Intensity = VignetteIntensity(Health, Threshold);
		TestTrue(FString::Printf(TEXT("%.0f hp is at least as loud as %.0f hp"), Health, Health + 1.f),
			Intensity >= Previous - KINDA_SMALL_NUMBER);
		Previous = Intensity;
	}

	// Degenerate configuration disables the effect rather than dividing by zero.
	TestEqual(TEXT("a zero threshold disables the vignette"), VignetteIntensity(1.f, 0.f), 0.f);
	TestEqual(TEXT("negative health is still clamped to fully dead, not past it"),
		VignetteIntensity(-50.f, Threshold), 1.f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
