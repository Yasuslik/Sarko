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

	/**
	 * Width of "RELOADING" in the large font, measured once.
	 *
	 * DrawBackpack offsets itself past the widest string DrawAmmo can produce, and
	 * GetTextSize takes an FString — so measuring it inline built and destroyed a
	 * string every frame for a number that cannot change. Negative until measured.
	 */
	float CachedReloadingWidth = -1.f;

	/**
	 * The interact button's label, measured once.
	 *
	 * A one-character label still costs an FString construction plus a GetTextSize
	 * every frame when it is written inline, and DrawHUD is a tick path. Hoisted
	 * for the same reason CachedReloadingWidth is. Negative until measured.
	 */
	float CachedInteractLabelWidth = -1.f;
	float CachedInteractLabelHeight = 0.f;

	/**
	 * The clock, rebuilt on the second rather than on the frame.
	 *
	 * A Printf and a GetTextSize per frame for a string that changes once a second
	 * — at 60 fps that is 59 of every 60 rebuilds thrown away. Negative until the
	 * first draw, so second zero is not mistaken for "already cached".
	 */
	int32 CachedClockSeconds = -1;
	FString CachedClock;
	float CachedClockWidth = 0.f;

	/**
	 * The ammo readout, rebuilt when the number changes.
	 *
	 * Keyed on the magazine count, with INDEX_NONE standing for "reloading" —
	 * which is the only other thing this readout can say, and the reason the key is
	 * not simply the count. Negative-one is not a reachable ammo value, so it also
	 * serves as "nothing cached yet".
	 */
	int32 CachedAmmoKey = -2;
	FString CachedAmmoText;

	/**
	 * The backpack readout, rebuilt when what it says changes.
	 *
	 * Both halves are the key: the limit is a setting rather than a constant, so a
	 * cache keyed on the used count alone would keep drawing "3/12" after a config
	 * change made it "3/16".
	 */
	int32 CachedBackpackUsed = -1;
	int32 CachedBackpackLimit = -1;
	FString CachedBackpackText;

	/**
	 * The loot prompt, built only when the target container's tier changes.
	 *
	 * Printf plus FName::ToString plus GetTextSize is three allocations per frame
	 * for a string that changes when the player walks up to a different kind of
	 * crate — a few times a raid. The tier is the cache key because it is the only
	 * thing the text depends on.
	 */
	FName CachedPromptTier;
	FString CachedPrompt;
	float CachedPromptWidth = 0.f;
	float CachedPromptHeight = 0.f;
	bool bPromptCached = false;
};
