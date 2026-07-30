#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace SarkoDebug
{
	/**
	 * Camera height that fits a square sector of the given half-extent into the
	 * frame, with a margin so the outermost geometry is not flush against the
	 * screen edge. Pure trigonometry, no world — which is why it can be tested.
	 */
	float HeightToFitSector(float ExtentUU, float VerticalFOVDegrees);

	/**
	 * Points the view straight down from above the sector's centre, high enough
	 * to see all of it. Used only by the overview screenshot: this is a design
	 * tool, not gameplay.
	 */
	void FrameWholeSector(APlayerController& Controller, float ExtentUU);
}
