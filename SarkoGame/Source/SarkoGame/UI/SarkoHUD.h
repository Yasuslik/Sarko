#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SarkoHUD.generated.h"

/**
 * Drawn with primitives rather than UMG, because widget blueprints are binary
 * assets. Layout follows spec §9: all information along the top, because the
 * bottom corners are physically covered by the player's thumbs.
 */
UCLASS()
class ASarkoHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawStick(const struct FSarkoTouchStick& Stick, const FLinearColor& Colour);
	void DrawAimCone();
	void DrawTopBar();
	void DrawHealth();
	void DrawAmmo();
	void DrawBackpack();

	/** The interact button, the "search this crate" prompt and the channel's progress bar. */
	void DrawInteract();

	/** Zone name and the dwell countdown, top-centre, while the owning pawn is in a zone. */
	void DrawExtraction();

	/** The final screen: EXTRACTED and the haul, or KIA/MIA and nothing. Drawn last, over everything. */
	void DrawOutcomeSummary();

	/**
	 * Extraction zone names, read from the map file once.
	 *
	 * Resolved locally rather than replicated: an FString on the wire for a value
	 * that never changes would be pure waste, and every machine already has the
	 * file. Cached rather than re-read per frame because DrawHUD is a tick path
	 * and a disk read plus a whole parsed definition per frame is exactly the
	 * per-tick allocation this project forbids.
	 */
	TArray<FString> CachedZoneNames;
	bool bZoneNamesCached = false;

	/** The zone's name for the HUD, or a generic label when the file cannot supply one. */
	const FString& ZoneNameFor(int32 ZoneIndex);
};
