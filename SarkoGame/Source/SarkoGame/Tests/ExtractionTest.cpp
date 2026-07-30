#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Loot/SarkoLootContainer.h"

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

#endif // WITH_AUTOMATION_TESTS
