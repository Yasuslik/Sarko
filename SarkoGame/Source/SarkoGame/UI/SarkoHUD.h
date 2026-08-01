#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/HUD.h"

#include "SarkoHUD.generated.h"

/**
 * Drawn with primitives rather than UMG, because widget blueprints are binary
 * assets. Layout follows spec §9: all information along the top, because the
 * bottom corners are physically covered by the player's thumbs.
 *
 * The game is landscape-only, so everything anchored to a side edge is measured
 * from Safe (below) rather than from the canvas: rotated, a phone puts its
 * Dynamic Island against the leading edge and reports an inset on both sides,
 * and the ammo count, the backpack count, the health bar and the interact button
 * are all pinned to a side.
 *
 * **Every size in the .cpp is in points, not pixels**, and PointScale turns them
 * into pixels. Nothing else does: a UCanvas handed to DrawHUD is 1:1 with
 * physical pixels (see UI/SarkoUiScale.h for how that was established), so the
 * fixed pixel sizes this HUD used to be written in came out around 5 pt tall on
 * a 1179x2556 phone — present, and unreadable. Points are also the unit the
 * project's touch rule is written in, which is what makes ">= 44 pt" something
 * you can check by reading the code.
 */
UCLASS()
class ASarkoHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	/**
	 * The drawable rectangle: the canvas minus whatever the device covers.
	 *
	 * Recomputed once per DrawHUD and read by every helper below, rather than each
	 * of them asking, so that one frame cannot be drawn against two different
	 * frames — and so the cost (a Slate call) is paid once on a tick path.
	 * Equal to the whole canvas on any screen without cutouts, which is every
	 * screen this project has been looked at on so far.
	 */
	FBox2D Safe = FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);

	/**
	 * Pixels per point, for the viewport this frame is being drawn into.
	 *
	 * One number for the whole HUD, recomputed once per DrawHUD alongside Safe and
	 * for the same reason: two elements drawn at two different scales in one frame
	 * is a layout that does not line up. See SarkoUI::PointScaleForViewport.
	 */
	float PointScale = 1.f;

	/**
	 * The PointScale the cached text measurements below were taken at.
	 *
	 * Glyph metrics are not linear in font size — hinting rounds each advance — so
	 * a width measured at one scale cannot simply be multiplied into another.
	 * Rather than re-measure per frame, every measurement cache is invalidated when
	 * this stops matching, which happens on a resize and otherwise never.
	 * Negative until the first draw.
	 */
	float MeasuredAtScale = -1.f;

	/** Drops every cached string measurement. Called only when PointScale moves. */
	void InvalidateMeasurements();

	/** FCoreStyle's default face at a size in points. No font asset is involved:
	 *  FCoreStyle is compiled into SlateCore. */
	FSlateFontInfo FontPt(float PointSize) const;

	/**
	 * Text, positioned in pixels, sized in points.
	 *
	 * Not AHUD::DrawText: that draws a UFont, and GEngine->GetLargeFont() on this
	 * project resolves to the engine's fallback face rasterised once at legacy size
	 * 10 — its Scale parameter magnifies that 10-pixel bitmap rather than asking
	 * for bigger glyphs, so the 3x a phone needs would have produced blur where the
	 * readout used to be. An FSlateFontInfo asks the font cache for the size it
	 * actually wants, and stays sharp at any scale.
	 */
	void DrawTextPt(const FString& Text, const FLinearColor& Colour, float X, float Y, float PointSize);

	/** Size of Text in pixels at PointSize. Zero when no font service exists, which
	 *  is every headless run — where nothing is drawn either. */
	FVector2D MeasurePt(const FString& Text, float PointSize) const;

	/** Points to pixels. Positions stay in pixels because Safe is in pixels; this is
	 *  what every offset, width and thickness in the .cpp is passed through. */
	float Px(float Points) const { return Points * PointScale; }

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
	 * Width of "RELOADING" in pixels, measured once per scale.
	 *
	 * DrawBackpack offsets itself past the widest string DrawAmmo can produce, so
	 * measuring it inline cost a string measurement every frame for a number that
	 * only changes when the viewport does. Negative until measured; reset by
	 * InvalidateMeasurements.
	 */
	float CachedReloadingWidth = -1.f;

	/**
	 * The interact button's label, measured once per scale.
	 *
	 * A one-character label still costs a string measurement every frame when it is
	 * written inline, and DrawHUD is a tick path. Hoisted for the same reason
	 * CachedReloadingWidth is. Negative until measured.
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
