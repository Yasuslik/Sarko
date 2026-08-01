#include "Core/SarkoPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Shelter/SarkoShelterWidget.h"
#include "UI/SarkoUiScale.h"

/**
 * The one number that makes every point-authored size in this project real.
 *
 * Automated because it is the only part of the UI work that *can* be: the rest of
 * "is the ammo count readable" is a screenshot and a pair of eyes. What a test can
 * hold is that the factor those eyes were satisfied by is still the factor being
 * applied — on a phone, on a desktop window, and to both screens at once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPointScaleIsOneRuleForEveryScreen,
	"Sarko.UI.PointScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPointScaleIsOneRuleForEveryScreen::RunTest(const FString& Parameters)
{
	// A 14 Pro on its side. The device is 3x, and the rule is expected to recover
	// that from the pixel size alone — this is the resolution the HUD's sizes were
	// judged legible at, so a change that moves this number changes what the owner
	// actually looked at.
	const float Phone = SarkoUI::PointScaleForViewport(FVector2D(2556.f, 1179.f));
	TestTrue(TEXT("a 2556x1179 phone comes out at very nearly its real 3x"),
		Phone > 2.95f && Phone < 3.1f);

	// A smaller 19.5:9 phone, which is a 2x device.
	const float SmallPhone = SarkoUI::PointScaleForViewport(FVector2D(1560.f, 720.f));
	TestTrue(TEXT("a 1560x720 phone comes out near its real 2x, and never above it"),
		SmallPhone > 1.75f && SmallPhone <= 2.f);

	// min and not max, which is the whole reason the rule is written this way: a
	// 16:9 window is taller in points than the design canvas, and taking the
	// height ratio there would scale everything until the top row ran off the side.
	const float Desktop = SarkoUI::PointScaleForViewport(FVector2D(1920.f, 1080.f));
	TestEqual(TEXT("a 16:9 desktop window is limited by its width, not its height"),
		Desktop, 1920.f / SarkoUI::DesignWidthPt);
	TestTrue(TEXT("so the design canvas always fits across"),
		1920.f / Desktop >= SarkoUI::DesignWidthPt - KINDA_SMALL_NUMBER &&
		1080.f / Desktop >= SarkoUI::DesignHeightPt - KINDA_SMALL_NUMBER);

	// A viewport of zero happens during teardown and mid-resize. Scaling by the
	// result would collapse the whole HUD into the top-left corner.
	TestEqual(TEXT("a degenerate viewport falls back to 1:1 rather than to zero"),
		SarkoUI::PointScaleForViewport(FVector2D::ZeroVector), 1.f);

	// The HUD and the shelter menu must be the same size on the same phone. They
	// are separate rendering paths — DrawHUD primitives and Slate — and the only
	// thing keeping them agreeing is that they divide by the same two constants.
	TestEqual(TEXT("the shelter menu scales by exactly the shared rule"),
		SSarkoShelterWidget::UiScaleForViewport(FVector2D(2556.f, 1179.f)), Phone);

	// The touch rule, stated in the unit it is written in. The interact button is
	// drawn and hit-tested against this same rect, so this is the tap target
	// itself and not a proxy for it.
	for (const FVector2D Viewport : { FVector2D(2556.f, 1179.f), FVector2D(1560.f, 720.f), FVector2D(1280.f, 720.f) })
	{
		const float Scale = SarkoUI::PointScaleForViewport(Viewport);
		const FBox2D Rect = SarkoInput::InteractButtonRect(SarkoInput::SafeFrame(Viewport));
		TestTrue(FString::Printf(TEXT("the interact button is at least 44 pt on %.0fx%.0f"), Viewport.X, Viewport.Y),
			Rect.GetSize().X / Scale >= 44.f && Rect.GetSize().Y / Scale >= 44.f);
	}

	return true;
}
