#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoLootContainer.h"
#include "Loot/SarkoLootTable.h"
#include "Map/SarkoMapDefinition.h"
#include "Pawn/SarkoHealthComponent.h"

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

	// The looted bit is where the partial-loot rule lands, but CanInteract only
	// consumes it — what sets it, and in what order, is the loot path's own
	// invariant. It is pinned by Sarko.Loot.CompletedChannelCreditsThenMarksOnce
	// below, not by repeating the assertion above with a different message.
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
	FSarkoCompletedChannelCreditsThenMarksOnce,
	"Sarko.Loot.CompletedChannelCreditsThenMarksOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCompletedChannelCreditsThenMarksOnce::RunTest(const FString& Parameters)
{
	// The rule this pins is the one that protects the economy: a completed channel
	// credits the haul, *then* marks the container, marks it whether or not the
	// roll fitted, and never credits an index that is already marked. The full path
	// through ASarkoCharacter::TickLootChannel needs a world, a game mode, a game
	// state and a replicated component, so the rule itself lives in the pure
	// SarkoLoot::CompleteLootChannel that path calls — this exercises that.
	FSarkoItemCatalog Catalog;
	FString Error;
	const FString CatalogJson = TEXT(R"({
		"items": [
			{ "id": "pistol", "name": "Пістолет", "stackSize": 1, "category": "weapon" },
			{ "id": "medkit", "name": "Аптечка",  "stackSize": 3, "category": "med" }
		]
	})");
	if (!SarkoLoot::ParseItemCatalog(CatalogJson, Catalog, Error))
	{
		AddError(FString::Printf(TEXT("the fixture catalog failed to parse: %s"), *Error));
		return false;
	}

	// The same shape the game state replicates: one byte per container index.
	TArray<uint8> Looted;
	Looted.SetNumZeroed(3);

	// Deliberately more than fits: five pistols (stackSize 1) into two slots. This
	// is the partial-loot case, and the one where marking is easiest to get wrong.
	const TArray<FSarkoItemStack> Roll = { FSarkoItemStack{ TEXT("pistol"), 5 } };
	constexpr int32 SlotLimit = 2;
	constexpr int32 Index = 1;

	TArray<FSarkoItemStack> Slots;
	TArray<FName> Steps;

	// The two effects, recorded as they happen: the order is half the invariant, so
	// it has to be observed rather than assumed.
	auto Credit = [&Steps, &Slots, &Catalog](FName Item, int32 Quantity)
	{
		Steps.Add(TEXT("credit"));
		return SarkoLoot::AddToBackpack(Slots, Catalog, SlotLimit, Item, Quantity);
	};
	auto Mark = [&Steps, &Looted]()
	{
		Steps.Add(TEXT("mark"));
		Looted[Index] = 1;
	};

	const SarkoLoot::FSarkoLootPayout First =
		SarkoLoot::CompleteLootChannel(Roll, Looted[Index] != 0, Credit, Mark);

	TestTrue(TEXT("an unlooted container pays out"), First.bCredited);
	TestEqual(TEXT("only what fitted is credited"), First.Taken, 2);
	TestEqual(TEXT("the overflow is reported, not silently dropped"), First.LeftBehind, 3);
	TestEqual(TEXT("what was taken plus what was left equals what was rolled"),
		First.Taken + First.LeftBehind, 5);

	// Credit strictly before mark. The other order is not a style preference: the
	// mark is what makes a container ineligible, so marking first would make the
	// credit unreachable and every crate would open onto nothing.
	if (Steps.Num() != 2)
	{
		AddError(FString::Printf(TEXT("expected exactly one credit and one mark, saw %d steps"), Steps.Num()));
		return false;
	}
	TestEqual(TEXT("the haul is credited first"), Steps[0], FName(TEXT("credit")));
	TestEqual(TEXT("and the container is marked after"), Steps[1], FName(TEXT("mark")));

	// Marked even though three of the five pistols never made it into the backpack.
	// This is spec §4.3's partial loot, and it is what makes a full backpack cost
	// the player something real.
	TestTrue(TEXT("a partly-emptied container is marked looted anyway"), Looted[Index] != 0);
	TestTrue(TEXT("and no other container was touched"), Looted[0] == 0 && Looted[2] == 0);

	// The no-double-credit rule. A second completion on an accepted index must do
	// nothing at all: the roll is deterministic, so paying out again would credit
	// the very same items a second time out of thin air.
	Steps.Reset();
	const SarkoLoot::FSarkoLootPayout Second =
		SarkoLoot::CompleteLootChannel(Roll, Looted[Index] != 0, Credit, Mark);

	TestFalse(TEXT("an already-emptied container does not pay out again"), Second.bCredited);
	TestEqual(TEXT("nothing is credited the second time"), Second.Taken, 0);
	TestEqual(TEXT("and nothing is reported as left behind either"), Second.LeftBehind, 0);
	TestEqual(TEXT("neither effect ran at all"), Steps.Num(), 0);
	TestEqual(TEXT("the backpack was not topped up a second time"), Slots.Num(), 2);

	// An empty roll still marks. An empty crate that stayed openable would let a
	// player re-channel it forever, and on a tier with an emptyChance that is a
	// real outcome rather than a hypothetical.
	Steps.Reset();
	constexpr int32 EmptyIndex = 2;
	TArray<FSarkoItemStack> UntouchedSlots;
	auto CreditNothing = [&Steps, &UntouchedSlots, &Catalog](FName Item, int32 Quantity)
	{
		Steps.Add(TEXT("credit"));
		return SarkoLoot::AddToBackpack(UntouchedSlots, Catalog, SlotLimit, Item, Quantity);
	};
	auto MarkEmpty = [&Steps, &Looted]()
	{
		Steps.Add(TEXT("mark"));
		Looted[EmptyIndex] = 1;
	};
	const SarkoLoot::FSarkoLootPayout Empty = SarkoLoot::CompleteLootChannel(
		TArray<FSarkoItemStack>(), Looted[EmptyIndex] != 0, CreditNothing, MarkEmpty);

	TestTrue(TEXT("an empty container still counts as completed"), Empty.bCredited);
	TestEqual(TEXT("with nothing taken"), Empty.Taken, 0);
	TestTrue(TEXT("an empty container is marked looted, so it cannot be re-channelled"), Looted[EmptyIndex] != 0);
	TestEqual(TEXT("and the mark was the only effect"), Steps.Num(), 1);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSettledOutcomeTakesNoMoreDamage,
	"Sarko.Extract.SettledOutcomeTakesNoMoreDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSettledOutcomeTakesNoMoreDamage::RunTest(const FString& Parameters)
{
	// Exclusivity on the enum (FSarkoRaidOutcomeIsDecidedOnce above) is not
	// exclusivity on the *effects*. An extracted player stands frozen on the pad
	// with the summary up, and the bots were still shooting at them last frame: a
	// hit that lands is refused a second outcome by CanFinishRaid, but it still runs
	// death's side-effects, and one of those clears the backpack. The summary then
	// reads EXTRACTED over an empty haul and the submitted result credits nothing.
	//
	// The gate that stops it is the earliest return in
	// USarkoHealthComponent::ApplyDamage. The full path — bots, weapon, HandleDeath,
	// FinishRaid — needs a world and a network, so this exercises the layer that is
	// headless-testable, through the same NewObject seam
	// Sarko.Combat.HealthDamageAndDeath uses.
	USarkoHealthComponent* Health = NewObject<USarkoHealthComponent>();
	Health->ResetForTest(100.f);

	int32 DeathCount = 0;
	Health->OnDied.AddLambda([&DeathCount](AActor*) { ++DeathCount; });

	// Baseline: with the raid in progress the same damage lands, so what the
	// assertions below observe is the gate and not some unrelated refusal.
	Health->ApplyDamage(10.f, nullptr);
	TestEqual(TEXT("damage lands while the raid is in progress"), Health->GetHealth(), 90.f);

	// The raid settles. Everything after this must be inert.
	Health->SetRaidFinishedForTest(true);

	Health->ApplyDamage(40.f, nullptr);
	TestEqual(TEXT("a hit after the outcome is settled changes nothing"), Health->GetHealth(), 90.f);
	TestFalse(TEXT("and cannot kill an extracted player"), Health->IsDead());

	// Overkill specifically, because that is the real case: the bot's burst was
	// already in the air when the dwell completed, and it is more than enough.
	Health->ApplyDamage(10000.f, nullptr);
	TestEqual(TEXT("overkill after a settled outcome changes nothing either"), Health->GetHealth(), 90.f);
	TestFalse(TEXT("still alive"), Health->IsDead());
	TestEqual(TEXT("and death never fires, so nothing clears the haul"), DeathCount, 0);

	// The gate is the raid's state, not a latch on the component: a component that
	// stopped taking damage for good would be a different bug.
	Health->SetRaidFinishedForTest(false);
	Health->ApplyDamage(10.f, nullptr);
	TestEqual(TEXT("the gate is the raid's state, not a permanent immunity"), Health->GetHealth(), 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLosingOutcomesLoseTheHaul,
	"Sarko.Extract.LosingOutcomesLoseTheHaul",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLosingOutcomesLoseTheHaul::RunTest(const FString& Parameters)
{
	using ESarkoOutcome = ESarkoRaidOutcome;

	// Spec §4.5: MIA is death, so MIA loses the loot. KIA loses it on the pawn's own
	// death path, but the clock running out has no death path — so before FinishRaid
	// consulted this rule the MIA summary itemised, line by line, loot the player
	// had just lost.
	TestTrue(TEXT("MIA loses the haul"), SarkoRaid::OutcomeLosesHaul(ESarkoOutcome::MIA));
	TestTrue(TEXT("KIA loses the haul"), SarkoRaid::OutcomeLosesHaul(ESarkoOutcome::Died));
	TestFalse(TEXT("extraction is the one outcome that keeps it"),
		SarkoRaid::OutcomeLosesHaul(ESarkoOutcome::Extracted));
	TestFalse(TEXT("a raid still in progress has lost nothing yet"),
		SarkoRaid::OutcomeLosesHaul(ESarkoOutcome::InProgress));

	// And the effect, on the real component: what FinishRaid does to each pawn it
	// finds, in the order it does it — clear first, outcome second, which is what
	// makes the HUD's "the server emptied the backpack before the outcome was set"
	// true rather than aspirational.
	const TArray<FSarkoItemStack> Haul = {
		FSarkoItemStack{ TEXT("pistol"), 1 },
		FSarkoItemStack{ TEXT("medkit"), 3 },
	};

	for (const ESarkoOutcome Outcome : { ESarkoOutcome::MIA, ESarkoOutcome::Died, ESarkoOutcome::Extracted })
	{
		USarkoBackpackComponent* Backpack = NewObject<USarkoBackpackComponent>();
		Backpack->SetSlots(Haul);
		TestEqual(TEXT("the fixture haul is in the backpack to begin with"), Backpack->GetUsedSlots(), 2);

		if (SarkoRaid::OutcomeLosesHaul(Outcome))
		{
			Backpack->ClearOnDeath();
		}

		const int32 Expected = Outcome == ESarkoOutcome::Extracted ? 2 : 0;
		TestEqual(*FString::Printf(TEXT("%s leaves %d slots"), *UEnum::GetValueAsString(Outcome), Expected),
			Backpack->GetUsedSlots(), Expected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellIsMeasuredFromEnteringThisZone,
	"Sarko.Extract.DwellIsMeasuredFromEnteringThisZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellIsMeasuredFromEnteringThisZone::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// Entering starts the clock at this frame's delta, not at zero-plus-a-frame
	// later: the frame the pawn is first seen inside is a frame it spent inside.
	FSarkoDwell Dwell = SarkoExtract::AdvanceDwellInZone(FSarkoDwell(), /*ZoneIndex*/ 0, 0.1f);
	TestEqual(TEXT("entering records which zone"), Dwell.ZoneIndex, 0);
	TestTrue(TEXT("entering starts at one frame"), FMath::IsNearlyEqual(Dwell.Seconds, 0.1f, 0.001f));

	for (int32 Frame = 0; Frame < 9; ++Frame)
	{
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, 0, 0.1f);
	}
	TestTrue(TEXT("a second in one zone accrues about a second"),
		FMath::IsNearlyEqual(Dwell.Seconds, 1.f, 0.001f));

	// THE BUG. Crossing straight from zone 0 into zone 1 without passing through
	// open ground must NOT carry zone 0's seconds across. Before this, a pawn that
	// had stood 4.9 s in one zone extracted from a different one on its first frame
	// inside — nine tenths of the dwell paid somewhere else entirely.
	const FSarkoDwell Crossed = SarkoExtract::AdvanceDwellInZone(Dwell, /*ZoneIndex*/ 1, 0.1f);
	TestEqual(TEXT("crossing into a different zone re-keys the dwell"), Crossed.ZoneIndex, 1);
	TestTrue(TEXT("crossing into a different zone restarts the count"),
		FMath::IsNearlyEqual(Crossed.Seconds, 0.1f, 0.001f));

	// Leaving resets to zero and forgets the zone, so re-entering the same one
	// starts over rather than resuming (spec §4.5: leaving resets it, and a pause
	// would let a player stitch five seconds out of safe fragments).
	const FSarkoDwell Left = SarkoExtract::AdvanceDwellInZone(Dwell, INDEX_NONE, 0.1f);
	TestEqual(TEXT("leaving forgets the zone"), Left.ZoneIndex, INDEX_NONE);
	TestEqual(TEXT("leaving resets to zero"), Left.Seconds, 0.f);

	const FSarkoDwell Reentered = SarkoExtract::AdvanceDwellInZone(Left, 0, 0.5f);
	TestTrue(TEXT("re-entering starts from zero"), FMath::IsNearlyEqual(Reentered.Seconds, 0.5f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellClampsHitchesAndIgnoresDeadFrames,
	"Sarko.Extract.DwellClampsHitchesAndIgnoresDeadFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellClampsHitchesAndIgnoresDeadFrames::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// A loading stall or a breakpoint produces one enormous delta, and without a
	// clamp that single frame completes most of an extraction the player never
	// stood through. The clamp has to survive being moved behind the new entry
	// point, including on the *entry* frame — a hitch on the frame the pawn
	// arrives is the easiest way to lose it.
	const FSarkoDwell AfterHitch = SarkoExtract::AdvanceDwellInZone(FSarkoDwell(), 0, 3.f);
	TestTrue(TEXT("a huge delta is clamped even on the entry frame"),
		AfterHitch.Seconds <= SarkoExtract::MaxDwellStepSeconds + KINDA_SMALL_NUMBER);

	FSarkoDwell Standing;
	Standing.ZoneIndex = 0;
	Standing.Seconds = 2.f;
	TestEqual(TEXT("a zero delta changes nothing"),
		SarkoExtract::AdvanceDwellInZone(Standing, 0, 0.f).Seconds, 2.f);
	TestEqual(TEXT("a negative delta changes nothing"),
		SarkoExtract::AdvanceDwellInZone(Standing, 0, -1.f).Seconds, 2.f);
	// ...but a zero delta on a *different* zone still re-keys, because the pawn is
	// somewhere else and its old progress is not transferable at any delta.
	const FSarkoDwell ZeroDeltaCross = SarkoExtract::AdvanceDwellInZone(Standing, 1, 0.f);
	TestEqual(TEXT("a zero delta still re-keys across zones"), ZeroDeltaCross.ZoneIndex, 1);
	TestEqual(TEXT("and drops the old zone's progress"), ZeroDeltaCross.Seconds, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoActivationIsTheDwellEpoch,
	"Sarko.Extract.ActivationIsTheDwellEpoch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoActivationIsTheDwellEpoch::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// The rule the live e2e exposed, now written down: a pawn standing in a zone
	// before the raid went live owes the FULL dwell from the activation frame, not
	// a fraction of it and not zero. ASarkoRaidGameMode::ActivateRaid clears its
	// dwell map, so the next tick is an entry frame — which is what this default
	// state models. The alternative reading of "measured from entering the zone"
	// would hand an instant extraction to anyone who spawned on a pad.
	const FSarkoDwell AtActivation;
	TestEqual(TEXT("a cleared dwell knows no zone"), AtActivation.ZoneIndex, INDEX_NONE);
	TestEqual(TEXT("a cleared dwell has no seconds"), AtActivation.Seconds, 0.f);

	// Ten frames of 0.5 s reaches the 5 s dwell and not a frame sooner.
	FSarkoDwell Dwell = AtActivation;
	constexpr float Required = 5.f;
	int32 Frames = 0;
	while (Dwell.Seconds < Required && Frames < 100)
	{
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, 0, 0.5f);
		++Frames;
	}
	TestEqual(TEXT("the full dwell is owed from the activation frame"), Frames, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoClientLayoutTriggersOnSessionReadyNotOnSeed,
	"Sarko.Extract.ClientLayoutTriggersOnSessionReadyNotOnSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoClientLayoutTriggersOnSessionReadyNotOnSeed::RunTest(const FString& Parameters)
{
	// The other open note. A client spawned its geometry from OnRep_Seed, and
	// replication sends no change when a property equals its default — so an
	// authoritative seed of exactly 0 (one in 2^32 of the backend's uint32, and
	// the *default* of the ?Seed= path) never fired the notify and left a joining
	// client standing in an empty world with no error anywhere.
	//
	// bSessionReady is the honest trigger: it is false until the raid is live and
	// its flip false->true always replicates. The rule is pure so the seed value is
	// provably irrelevant to it.
	TestFalse(TEXT("nothing spawns before the session is ready"),
		SarkoRaid::ShouldSpawnClientLayout(/*bLayoutBuilt*/ false, /*bSessionReady*/ false));
	TestTrue(TEXT("a ready session spawns the layout"),
		SarkoRaid::ShouldSpawnClientLayout(false, true));
	TestFalse(TEXT("an already-built layout is never rebuilt"),
		SarkoRaid::ShouldSpawnClientLayout(/*bLayoutBuilt*/ true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDeathLosesThePocketsAndTheBag,
	"Sarko.Extract.DeathLosesThePocketsAndTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDeathLosesThePocketsAndTheBag::RunTest(const FString& Parameters)
{
	// Spec §5: "everything carried is lost, per the game's core rule; only what
	// is in the shelter stash is safe." Pockets are NOT a safe pocket, and the
	// worn bag is not gear you keep — both go. This is the explicit decision the
	// spec asked to be made explicit, and it is the only place it is enforced.
	USarkoBackpackComponent* Backpack = NewObject<USarkoBackpackComponent>();
	Backpack->SetSlots({ FSarkoItemStack{ TEXT("medkit"), 2 } });
	Backpack->EquipBackpack(SarkoLoot::BackpackItemId);
	TestTrue(TEXT("the bag is on before death"), Backpack->IsWearingBackpack());

	Backpack->ClearOnDeath();

	TestEqual(TEXT("pockets are emptied"), Backpack->GetSlots().Num(), 0);
	TestFalse(TEXT("the worn bag is lost too"), Backpack->IsWearingBackpack());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHaulCarriesTheWornBagHome,
	"Sarko.Extract.HaulCarriesTheWornBagHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHaulCarriesTheWornBagHome::RunTest(const FString& Parameters)
{
	// A bag you extracted with is loot: it is submitted as one stack and lands
	// in the stash. Without this it would be the one thing a player carries out
	// of a raid and is never credited for, which reads as the game eating it —
	// the exact complaint this whole plan exists to end.
	USarkoBackpackComponent* Backpack = NewObject<USarkoBackpackComponent>();
	Backpack->SetSlots({ FSarkoItemStack{ TEXT("medkit"), 2 } });

	TestEqual(TEXT("no bag worn, no extra stack"), Backpack->GetHaulForSubmission().Num(), 1);

	Backpack->EquipBackpack(SarkoLoot::BackpackItemId);
	const TArray<FSarkoItemStack> Haul = Backpack->GetHaulForSubmission();
	TestEqual(TEXT("the worn bag is appended as its own stack"), Haul.Num(), 2);
	TestTrue(TEXT("and it is one backpack"),
		Haul.Last().Item == SarkoLoot::BackpackItemId && Haul.Last().Quantity == 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
