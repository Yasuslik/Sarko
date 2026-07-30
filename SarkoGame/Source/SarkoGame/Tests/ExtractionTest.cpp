#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteractGateIsServerSide,
	"Sarko.Loot.InteractGateIsServerSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteractGateIsServerSide::RunTest(const FString& Parameters)
{
	const FVector Pawn(0.f, 0.f, 100.f);
	const FVector Near(200.f, 0.f, 35.f);
	const FVector Far(900.f, 0.f, 35.f);
	constexpr float Radius = 250.f;

	TestTrue(TEXT("a live pawn beside an unlooted container may interact"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, /*bAlive*/ true, /*bLooted*/ false));

	// Every one of these is a thing a hostile client will claim.
	TestFalse(TEXT("distance is enforced"),
		SarkoLoot::CanInteract(Pawn, Far, Radius, true, false));
	TestFalse(TEXT("a looted container cannot be looted twice"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, true, true));

	// This is also the partial-loot rule, and it is the reason a full backpack
	// costs the player something real. A container is marked looted the moment
	// its channel completes, whether or not the whole roll fitted: whatever did
	// not fit stays behind and is gone, because the alternative — re-opening it
	// for the remainder — re-runs the same deterministic roll and would credit
	// the part already taken a second time.
	TestFalse(TEXT("a partly-emptied container is still looted, so the remainder is unrecoverable"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, /*bAlive*/ true, /*bLooted*/ true));
	TestFalse(TEXT("a corpse cannot loot"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, false, false));

	// Height is ignored: the container sits on the ground and the pawn's origin
	// is at capsule centre, so a 3D distance check would make a crate at the
	// player's feet unreachable at the edge of the radius. Planar distance is
	// what the player sees on a top-down camera.
	const FVector Above(200.f, 0.f, 900.f);
	TestTrue(TEXT("vertical separation does not block interaction"),
		SarkoLoot::CanInteract(Pawn, Above, Radius, true, false));

	// Exactly at the radius counts as inside, so a value tuned to 250 does not
	// behave like 249.99 on one machine and 250.01 on another.
	const FVector Exactly(Radius, 0.f, 35.f);
	TestTrue(TEXT("the radius boundary is inclusive"),
		SarkoLoot::CanInteract(Pawn, Exactly, Radius, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteractButtonAvoidsTheThumbs,
	"Sarko.Input.InteractButtonAvoidsTheThumbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteractButtonAvoidsTheThumbs::RunTest(const FString& Parameters)
{
	// Landscape iPhone, and a small window, because the rect is computed from
	// the viewport and a fixed pixel offset would leave the screen on one of them.
	for (const FVector2D Viewport : { FVector2D(2532.f, 1170.f), FVector2D(1280.f, 720.f) })
	{
		const FBox2D Rect = SarkoInput::InteractButtonRect(Viewport);

		TestTrue(TEXT("the button is on screen"),
			Rect.Min.X >= 0.f && Rect.Min.Y >= 0.f && Rect.Max.X <= Viewport.X && Rect.Max.Y <= Viewport.Y);
		TestTrue(TEXT("the button is big enough for a thumb (>= 88 px)"),
			Rect.GetSize().X >= 88.f && Rect.GetSize().Y >= 88.f);

		// Spec §9: the bottom corners are covered by the thumbs that drive the
		// sticks. A button there is a button that fights the controls.
		const float BottomBandY = Viewport.Y * 0.75f;
		const float LeftBandX = Viewport.X * 0.25f;
		const float RightBandX = Viewport.X * 0.75f;
		const bool bInBottomLeft = Rect.Min.Y > BottomBandY && Rect.Min.X < LeftBandX;
		const bool bInBottomRight = Rect.Min.Y > BottomBandY && Rect.Max.X > RightBandX;
		TestFalse(TEXT("the button is not in the bottom-left thumb zone"), bInBottomLeft);
		TestFalse(TEXT("the button is not in the bottom-right thumb zone"), bInBottomRight);

		// Reachable: a button pinned to the very top edge cannot be pressed
		// without letting go of a stick, which is the whole problem it solves.
		TestTrue(TEXT("the button sits in the vertical centre band, not against an edge"),
			Rect.GetCenter().Y > Viewport.Y * 0.3f && Rect.GetCenter().Y < Viewport.Y * 0.7f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellAccumulatesAndResets,
	"Sarko.Extract.DwellAccumulatesAndResets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellAccumulatesAndResets::RunTest(const FString& Parameters)
{
	// Inside: time accrues.
	float Dwell = 0.f;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Dwell = SarkoExtract::AdvanceDwell(Dwell, /*bInside*/ true, 0.1f);
	}
	TestTrue(TEXT("a second inside the zone accrues about a second"), FMath::IsNearlyEqual(Dwell, 1.f, 0.001f));

	// Leaving resets to zero, not "pauses". Spec §4.5: leaving resets it.
	// A pause would let a player hide behind cover, step in for a moment, step
	// out, and stitch five seconds together out of safe fragments — which is
	// exactly the risk the dwell exists to create.
	Dwell = SarkoExtract::AdvanceDwell(Dwell, /*bInside*/ false, 0.1f);
	TestEqual(TEXT("stepping out resets the dwell to zero"), Dwell, 0.f);

	// Re-entering starts from zero.
	Dwell = SarkoExtract::AdvanceDwell(Dwell, true, 0.5f);
	TestEqual(TEXT("re-entering starts from zero"), Dwell, 0.5f);

	// A frame hitch must not skip the dwell: a 3-second delta on a loading
	// stall would otherwise complete most of an extraction the player never
	// stood through. Clamped to a sane per-frame maximum.
	const float AfterHitch = SarkoExtract::AdvanceDwell(0.f, true, 3.f);
	TestTrue(TEXT("a huge frame delta is clamped"), AfterHitch <= 0.5f);

	// Negative or zero delta changes nothing.
	TestEqual(TEXT("a zero delta changes nothing"), SarkoExtract::AdvanceDwell(2.f, true, 0.f), 2.f);
	TestEqual(TEXT("a negative delta changes nothing"), SarkoExtract::AdvanceDwell(2.f, true, -1.f), 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoZoneLookupIsPlanarAndBounded,
	"Sarko.Extract.ZoneLookupIsPlanarAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoZoneLookupIsPlanarAndBounded::RunTest(const FString& Parameters)
{
	TArray<FSarkoExtractionSpot> Zones;
	FSarkoExtractionSpot First;
	First.Location = FVector(-14500.f, 18600.f, 0.f);
	First.RadiusUU = 500.f;
	First.Name = TEXT("Північна стежка");
	Zones.Add(First);

	FSarkoExtractionSpot Second;
	Second.Location = FVector(-1500.f, 18700.f, 0.f);
	Second.RadiusUU = 500.f;
	Second.Name = TEXT("Шосе на північ");
	Zones.Add(Second);

	TestEqual(TEXT("dead centre of the first zone"),
		SarkoExtract::FindZoneContaining(FVector(-14500.f, 18600.f, 150.f), Zones), 0);
	TestEqual(TEXT("inside the second zone"),
		SarkoExtract::FindZoneContaining(FVector(-1400.f, 18650.f, 150.f), Zones), 1);
	TestEqual(TEXT("between the zones is no zone"),
		SarkoExtract::FindZoneContaining(FVector(-8000.f, 18600.f, 150.f), Zones), INDEX_NONE);

	// Height is ignored: the zone is a circle on the ground, and the pawn's
	// origin is at capsule centre 150 uu up.
	TestEqual(TEXT("standing height does not matter"),
		SarkoExtract::FindZoneContaining(FVector(-14500.f, 18600.f, 900.f), Zones), 0);

	// The radius boundary is inclusive, for the same reason CanInteract's is: a
	// value tuned to 500 must not behave like 499.99 on one machine.
	TestEqual(TEXT("the radius boundary is inclusive"),
		SarkoExtract::FindZoneContaining(FVector(-14000.f, 18600.f, 150.f), Zones), 0);

	// An empty list is not a crash and not zone zero.
	const TArray<FSarkoExtractionSpot> NoZones;
	TestEqual(TEXT("no zones means no zone"),
		SarkoExtract::FindZoneContaining(FVector::ZeroVector, NoZones), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeExtractionsAreReachableAndDistinct,
	"Sarko.Extract.BridgeExtractionsAreReachableAndDistinct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeExtractionsAreReachableAndDistinct::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("there is somewhere to extract"), Map.Extractions.Num() >= 1);

	// Overlapping zones would make the dwell ambiguous: the pawn would be in two
	// at once and FindZoneContaining would silently pick the earlier one, so the
	// HUD could name a zone the player is not aiming for.
	for (int32 A = 0; A < Map.Extractions.Num(); ++A)
	{
		for (int32 B = A + 1; B < Map.Extractions.Num(); ++B)
		{
			const float Distance = FVector2D(
				Map.Extractions[A].Location.X - Map.Extractions[B].Location.X,
				Map.Extractions[A].Location.Y - Map.Extractions[B].Location.Y).Size();
			TestTrue(*FString::Printf(TEXT("extractions '%s' and '%s' do not overlap"),
				*Map.Extractions[A].Name, *Map.Extractions[B].Name),
				Distance > Map.Extractions[A].RadiusUU + Map.Extractions[B].RadiusUU);
		}
	}

	// No spawn inside an extraction: the player must not be able to extract at
	// second zero with the loadout they walked in with.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestEqual(TEXT("no player spawn sits inside an extraction zone"),
			SarkoExtract::FindZoneContaining(Spawn.GetLocation(), Map.Extractions), INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRaidOutcomeIsDecidedOnce,
	"Sarko.Extract.RaidOutcomeIsDecidedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRaidOutcomeIsDecidedOnce::RunTest(const FString& Parameters)
{
	using ESarkoOutcome = ESarkoRaidOutcome;

	// The three real outcomes are all reachable from a raid in progress.
	for (const ESarkoOutcome Requested : { ESarkoOutcome::Extracted, ESarkoOutcome::Died, ESarkoOutcome::MIA })
	{
		TestTrue(TEXT("a raid in progress can reach any real outcome"),
			SarkoRaid::CanFinishRaid(ESarkoOutcome::InProgress, Requested));
	}

	// The mutual exclusion that matters. The clock reaching zero on the same
	// frame a dwell completes must not turn an extraction into an MIA, and a
	// bullet landing on the extraction frame must not turn it into a KIA — nor
	// the reverse. Whichever outcome the server saw first is the raid's outcome,
	// and it is submitted to the backend exactly once (Task 8).
	for (const ESarkoOutcome Settled : { ESarkoOutcome::Extracted, ESarkoOutcome::Died, ESarkoOutcome::MIA })
	{
		for (const ESarkoOutcome Requested : { ESarkoOutcome::Extracted, ESarkoOutcome::Died, ESarkoOutcome::MIA })
		{
			TestFalse(TEXT("a settled raid cannot be re-decided"),
				SarkoRaid::CanFinishRaid(Settled, Requested));
		}
	}

	// InProgress is not an outcome, so nothing may "finish" a raid into it —
	// that would silently un-freeze input and reopen a submitted raid.
	TestFalse(TEXT("a raid cannot be finished into InProgress"),
		SarkoRaid::CanFinishRaid(ESarkoOutcome::InProgress, ESarkoOutcome::InProgress));
	TestFalse(TEXT("a settled raid cannot be reopened"),
		SarkoRaid::CanFinishRaid(ESarkoOutcome::Extracted, ESarkoOutcome::InProgress));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
