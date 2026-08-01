#pragma once

#include "CoreMinimal.h"

/**
 * The one factor that turns this game's point-authored UI into pixels.
 *
 * Two screens need it and neither gets it for free. The shelter is Slate added
 * to the viewport overlay, which is 1:1 with physical pixels; the in-raid HUD is
 * AHUD::DrawHUD, whose UCanvas is *also* 1:1 with physical pixels — the game
 * viewport client answers ShouldDPIScaleSceneCanvas() with false, so the scene
 * canvas FCanvas is built with a DPI scale of exactly 1 (Engine's
 * UnrealClient.cpp / GameplayViewportClient.cpp), and UCanvas::Init is handed
 * the view's UnscaledViewRect. Nothing between a size written in this project
 * and a pixel on the glass multiplies by anything. On a 1179x2556 iPhone that
 * made every readout about a third the height it claimed.
 *
 * Shared rather than duplicated so the menu and the HUD are the same size on the
 * same phone: a player who can read "В РЕЙД" and then cannot read the ammo count
 * is looking at a bug, not at two screens.
 */
namespace SarkoUI
{
	/**
	 * The canvas every UI size in this project is authored against: a landscape
	 * phone in logical points, iPhone 14/15 sized. The game is landscape-only, so
	 * the long edge is the width.
	 *
	 * Points and not pixels on purpose — the touch rule this project has to
	 * satisfy is written in points (">= 44 pt"), so authoring in the same unit
	 * makes the rule checkable by reading the code instead of by guessing at a
	 * device's pixel density.
	 */
	constexpr float DesignWidthPt = 844.f;
	constexpr float DesignHeightPt = 390.f;

	/**
	 * Pixels per point for a viewport of this size.
	 *
	 * min, not max: taking the larger ratio would overflow the design canvas along
	 * the other axis, which on a 16:9 desktop window means the top row of the HUD
	 * running off the side of the screen.
	 *
	 * The ratio doubles as a density estimate, which is what the HUD actually
	 * wants, and it is a good one on the hardware that matters: 1179x2556 gives
	 * min(3.03, 3.02) = 3.02 against a real 3x, and 720x1560 gives 1.85 against a
	 * real 2x — the second is 8% conservative, which costs a little size on a
	 * small phone and never overflows.
	 *
	 * Clamped because neither end is a screen anyone plays on: below 0.5 the text
	 * is unreadable anyway and above 6 a single readout would fill a wall.
	 */
	inline float PointScaleForViewport(FVector2D ViewportSize)
	{
		// A viewport smaller than a pixel happens mid-resize and during teardown.
		// Dividing by it produces a scale of zero and a HUD collapsed into a dot.
		if (ViewportSize.X < 1.f || ViewportSize.Y < 1.f)
		{
			return 1.f;
		}

		return FMath::Clamp(
			FMath::Min(static_cast<float>(ViewportSize.X) / DesignWidthPt,
				static_cast<float>(ViewportSize.Y) / DesignHeightPt),
			0.5f, 6.f);
	}
}
