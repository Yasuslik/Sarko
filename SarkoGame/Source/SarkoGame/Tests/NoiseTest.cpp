#include "Misc/AutomationTest.h"

#include "AI/SarkoBotArchetypes.h"
#include "AI/SarkoNoise.h"
#include "Core/SarkoRaidSettings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNoiseRadiusForMovement,
	"Sarko.Noise.RadiusForMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNoiseRadiusForMovement::RunTest(const FString& Parameters)
{
	// THE VERB, as arithmetic (spec §7). Before the noise model there was no way
	// to be quiet: hearing was a radius around the LISTENER, so the only variable
	// was distance and the player could not spend it. These four cases are the
	// whole of what the player can now choose between.
	using namespace SarkoNoise;

	const float MaxSpeed = 400.f;
	const float MoveFraction = 0.15f;
	const float RunFraction = 0.7f;

	TestEqual(TEXT("standing still is silent"),
		static_cast<int32>(KindForSpeed(0.f, MaxSpeed, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Silent));

	// A pawn braking to a stop, or sliding a little on a slope, must not go on
	// announcing itself for the second the deceleration takes.
	TestEqual(TEXT("a drift below the move threshold is still silent"),
		static_cast<int32>(KindForSpeed(MaxSpeed * 0.1f, MaxSpeed, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Silent));

	TestEqual(TEXT("a half-deflected stick is walking, and walking is quiet"),
		static_cast<int32>(KindForSpeed(MaxSpeed * 0.5f, MaxSpeed, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Quiet));

	// The boundary belongs to running, for the same reason SarkoExtract::
	// IsZoneOpen's belongs to open: a threshold the player is standing exactly on
	// and that refuses is a bug report.
	TestEqual(TEXT("exactly at the run threshold is running"),
		static_cast<int32>(KindForSpeed(MaxSpeed * RunFraction, MaxSpeed, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Audible));

	TestEqual(TEXT("a fully deflected stick is running, and running is audible"),
		static_cast<int32>(KindForSpeed(MaxSpeed, MaxSpeed, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Audible));

	// A pawn with no speed of its own cannot be classified against a fraction of
	// it, and an unknown must never resolve to the noisy answer.
	TestEqual(TEXT("a broken MaxWalkSpeed is silent rather than loud"),
		static_cast<int32>(KindForSpeed(300.f, 0.f, MoveFraction, RunFraction)),
		static_cast<int32>(EKind::Silent));

	// The radii themselves, in the order that makes the mechanic: a shot must be
	// the loudest thing in the game or shooting costs nothing.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Quiet = RadiusForKind(EKind::Quiet, Settings.NoiseQuietRadiusUU, Settings.NoiseAudibleRadiusUU, Settings.NoiseLoudRadiusUU);
	const float Audible = RadiusForKind(EKind::Audible, Settings.NoiseQuietRadiusUU, Settings.NoiseAudibleRadiusUU, Settings.NoiseLoudRadiusUU);
	const float Loud = RadiusForKind(EKind::Loud, Settings.NoiseQuietRadiusUU, Settings.NoiseAudibleRadiusUU, Settings.NoiseLoudRadiusUU);

	TestEqual(TEXT("silence has no radius at all — it is the absence of an event, not a small one"),
		RadiusForKind(EKind::Silent, Settings.NoiseQuietRadiusUU, Settings.NoiseAudibleRadiusUU, Settings.NoiseLoudRadiusUU), 0.f);
	TestTrue(FString::Printf(TEXT("firing (%.0f) > running (%.0f) > walking (%.0f) > nothing"), Loud, Audible, Quiet),
		Loud > Audible && Audible > Quiet && Quiet > 0.f);

	// The one relationship that is not a taste question: a shot must reach past
	// the distance a bot will shoot from, or being shot at is not information.
	TestTrue(FString::Printf(TEXT("a shot (%.0f uu) carries further than a bot fires (%.0f uu)"),
		Loud, Settings.EnemyFiringRangeUU), Loud > Settings.EnemyFiringRangeUU);

	// Audibility is the listener's half, and it must not be able to hear silence.
	TestTrue(TEXT("a run is heard at its own radius"), IsAudible(Audible, Audible, 1.f));
	TestFalse(TEXT("a run is not heard one uu past it"), IsAudible(Audible + 1.f, Audible, 1.f));
	TestTrue(TEXT("a sharper listener hears further"), IsAudible(Audible * 1.2f, Audible, 1.25f));
	TestFalse(TEXT("nothing hears an event with no radius"), IsAudible(0.f, 0.f, 10.f));
	TestFalse(TEXT("a deaf listener hears nothing"), IsAudible(1.f, Audible, 0.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNoiseRingIsBounded,
	"Sarko.Noise.RingIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNoiseRingIsBounded::RunTest(const FString& Parameters)
{
	// The ring is written from a tick path and read from every bot's tick, so the
	// property worth asserting is the one this project forbids breaking: it never
	// grows. A TArray here would allocate during a firefight.
	using namespace SarkoNoise;

	FNoiseRing Ring;
	TestEqual(TEXT("a fresh ring holds nothing"), Ring.Num(), 0);

	const float Lifetime = 1.f;
	const FVector Origin = FVector::ZeroVector;

	for (int32 Index = 0; Index < MaxLiveEvents * 3; ++Index)
	{
		FNoiseEvent Event;
		Event.Location = FVector(static_cast<float>(Index) * 10.f, 0.f, 0.f);
		Event.RadiusUU = 1000.f;
		Event.TimeSeconds = static_cast<float>(Index);
		Ring.Add(Event);
		TestTrue(FString::Printf(TEXT("the ring never grows past %d (holds %d after %d adds)"),
			MaxLiveEvents, Ring.Num(), Index + 1), Ring.Num() <= MaxLiveEvents);
	}
	TestEqual(TEXT("and it fills to exactly its bound"), Ring.Num(), MaxLiveEvents);

	// Overwriting the OLDEST is the half that matters: a ring that dropped new
	// events once full would go deaf for the rest of the raid after 16 shots.
	{
		const float Now = static_cast<float>(MaxLiveEvents * 3 - 1);
		FNoiseEvent Heard;
		TestTrue(TEXT("the newest event survived a full wrap"),
			Ring.FindAudible(FVector(static_cast<float>(MaxLiveEvents * 3 - 1) * 10.f, 0.f, 0.f),
				1.f, Now, Lifetime, nullptr, Heard));
		TestEqual(TEXT("and it is the one that was added last"), Heard.TimeSeconds, Now);
	}

	// Newest wins, and a tie goes to the louder event: a shot and a footstep can
	// land in the same frame, and the shot is the one to walk to.
	{
		FNoiseRing Tie;
		FNoiseEvent Footstep;
		Footstep.Location = FVector(100.f, 0.f, 0.f);
		Footstep.RadiusUU = 450.f;
		Footstep.TimeSeconds = 5.f;
		Tie.Add(Footstep);

		FNoiseEvent Shot;
		Shot.Location = FVector(-100.f, 0.f, 0.f);
		Shot.RadiusUU = 2600.f;
		Shot.TimeSeconds = 5.f;
		Tie.Add(Shot);

		FNoiseEvent Heard;
		TestTrue(TEXT("something is heard"), Tie.FindAudible(Origin, 1.f, 5.f, Lifetime, nullptr, Heard));
		TestEqual(TEXT("a shot beats a footstep from the same frame"), Heard.RadiusUU, 2600.f);
	}

	// Expiry is by age against the clock, not by anything ticking the buffer.
	{
		FNoiseRing Aged;
		FNoiseEvent Old;
		Old.Location = Origin;
		Old.RadiusUU = 2600.f;
		Old.TimeSeconds = 0.f;
		Aged.Add(Old);

		FNoiseEvent Heard;
		TestTrue(TEXT("a fresh event is heard"), Aged.FindAudible(Origin, 1.f, 0.5f, Lifetime, nullptr, Heard));
		TestFalse(TEXT("the same event past its lifetime is not"), Aged.FindAudible(Origin, 1.f, 2.f, Lifetime, nullptr, Heard));
	}

	// Out of earshot is out of earshot however loud the event was.
	{
		FNoiseRing Far;
		FNoiseEvent Shot;
		Shot.Location = FVector(9000.f, 0.f, 0.f);
		Shot.RadiusUU = 2600.f;
		Shot.TimeSeconds = 1.f;
		Far.Add(Shot);

		FNoiseEvent Heard;
		TestFalse(TEXT("a shot 9000 uu away is not heard at 2600"),
			Far.FindAudible(Origin, 1.f, 1.f, Lifetime, nullptr, Heard));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNoiseArchetypeSensitivity,
	"Sarko.Noise.ArchetypeSensitivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNoiseArchetypeSensitivity::RunTest(const FString& Parameters)
{
	// Hearing survived §7 as a property of the BOT rather than of the world: the
	// scout hears further because it listens better, not because sound travels
	// differently near it. These are the properties that keep that meaningful.
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	TSet<float> Distinct;
	for (const FSarkoBotArchetype& Row : SarkoAI::GetBotArchetypes())
	{
		Distinct.Add(Row.HearingSensitivity);
		TestTrue(FString::Printf(TEXT("'%s' can hear at all (%.2fx)"), *Row.Id.ToString(), Row.HearingSensitivity),
			Row.HearingSensitivity > 0.f);

		// The old table asserted "hears at least as far as it shoots" against a
		// hearing radius. The same sentence under the noise model is about the
		// loudest event there is: being shot at must always be information.
		const float ShotReach = Settings.NoiseLoudRadiusUU * Row.HearingSensitivity;
		TestTrue(FString::Printf(TEXT("'%s' hears a gunshot (%.0f uu) at least as far as it shoots (%.0f uu)"),
			*Row.Id.ToString(), ShotReach, Row.FiringRangeUU), ShotReach >= Row.FiringRangeUU);
	}

	// A guard against the table collapsing into one listener, which would pass
	// everything above and make the archetype's hearing column decorative.
	TestTrue(TEXT("the archetypes do not all listen identically"), Distinct.Num() > 1);

	// The scout is the row where hearing and shooting disagree on purpose.
	FSarkoBotArchetype Scout;
	FSarkoBotArchetype Pistol;
	TestTrue(TEXT("scout resolves"), SarkoAI::FindBotArchetype(TEXT("scout"), Scout));
	TestTrue(TEXT("scav_pistol resolves"), SarkoAI::FindBotArchetype(TEXT("scav_pistol"), Pistol));
	TestTrue(TEXT("the scout listens better than the tutorial's teacher"),
		Scout.HearingSensitivity > Pistol.HearingSensitivity);
	TestTrue(TEXT("and still shoots from closer in"), Scout.FiringRangeUU < Pistol.FiringRangeUU);

	// Walking past the tutorial's teacher is genuinely quiet: a walk reaches
	// 450 * 0.9 = 405 uu, less than the distance a pawn covers in a second.
	TestTrue(FString::Printf(TEXT("a walk reaches scav_pistol from only %.0f uu"),
		Settings.NoiseQuietRadiusUU * Pistol.HearingSensitivity),
		Settings.NoiseQuietRadiusUU * Pistol.HearingSensitivity < Settings.EnemyFiringRangeUU);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
