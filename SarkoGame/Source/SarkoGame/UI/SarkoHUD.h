#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/HUD.h"
#include "UI/SarkoCombatFeedback.h"

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

	/**
	 * A floating stick: the ring at the thumb's anchor, the dot at its current
	 * position, and — on the MOVE stick only — a second, dimmer ring at the
	 * walk/run boundary.
	 *
	 * That second ring is the noise model's only interface. The server splits
	 * quiet from audible at USarkoRaidSettings::NoiseRunSpeedFraction of the
	 * stick's deflection (450 uu heard against 1100 uu heard), which makes
	 * "choose to be quiet" the most interesting verb in the game — and until now
	 * the boundary existed nowhere but the diagnostic log. The fraction is READ
	 * FROM THE SETTINGS and never written here, so the drawn ring cannot drift
	 * from the rule the server applies.
	 *
	 * Both radii come off the stick itself (FSarkoTouchStick::RadiusPx, resolved
	 * from points at anchor), so the picture and the rule are one number.
	 */
	void DrawStick(const struct FSarkoTouchStick& Stick, const FLinearColor& Colour, bool bDrawQuietRing);

	/** One circle, in segments. Both stick rings and nothing else — a second
	 *  hand-rolled loop is a second chance to draw a ring the input rule does not
	 *  have. */
	void DrawRing(FVector2D Centre, float Radius, const FLinearColor& Colour);

	void DrawAimCone();

	/**
	 * THE SNAP-TARGET BRACKET. Four corner ticks on the enemy a shot fired right
	 * now would be nudged onto.
	 *
	 * The aim assist is generous — a 6 deg half-angle is +/-115 uu of tolerance at
	 * 1100 uu against a ~34 uu capsule — and completely invisible: the player has
	 * no way to learn what the cone is worth, so they guess. This draws the
	 * answer, using SarkoCombat::BestAimAssistTarget, the same selection the
	 * server runs at fire time, against the same foes-only candidate set.
	 *
	 * DISPLAY ONLY. It changes nothing about the assist and nothing about
	 * authority: the server still re-runs the selection from its own copy when the
	 * shot is taken, exactly as it does for the hit marker.
	 */
	void DrawSnapTargetBracket(const class ASarkoCharacter& Pawn, const FVector& Muzzle, const FVector& Aim);

	/**
	 * Candidate target locations, gathered for the bracket above.
	 *
	 * A member and Reset() rather than a local TArray: DrawHUD is a tick path and
	 * a heap allocation per frame is what this project forbids. Reset keeps the
	 * capacity, so after the first aimed frame the gather allocates nothing.
	 */
	TArray<FVector> AimAssistCandidates;

	/**
	 * FIRST-RAID CONTROL HINTS (audit §7). One line, top-centre, teaching the one
	 * rule this scheme does not share with every other mobile shooter — that the
	 * right stick fires, because there is no fire button — plus the quiet-walk
	 * band, the reload button and the interact button.
	 *
	 * Gated on the profile the client already holds
	 * (USarkoGameInstance::CachedProfile.bTutorialCompleted): in the standalone
	 * raid this game ships the client IS the server, so nothing new replicates.
	 * Each hint is dismissed permanently — for this raid — the first time the
	 * player performs its verb, which the controller already knows; and each also
	 * fades on its own after HintLifetimeSeconds whether it was obeyed or not, so
	 * a hint can never become a permanent obstruction.
	 */
	void DrawFirstRaidHints(const class ASarkoPlayerController& PC);

	/** When each hint was first drawn, in world seconds; negative until it has
	 *  been. Parallel to SarkoHint::Text by index. */
	float HintFirstShownSeconds[4] = { -1.f, -1.f, -1.f, -1.f };

	/** Each hint's measured size in pixels, taken once per scale. A negative X is
	 *  "not measured yet"; dropped by InvalidateMeasurements with every other
	 *  cache, because DrawHUD is a tick path and these strings never change. */
	FVector2D CachedHintSize[4] = {
		FVector2D(-1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(-1.f, 0.f) };
	void DrawTopBar();
	void DrawHealth();

	/**
	 * THE DIRECTIONAL DAMAGE ARC (spec §4.1). A short arc on a circle around the
	 * pawn, pointing at whoever hit it, fading over DamageArcSeconds.
	 *
	 * The most valuable of the four feedback items because the camera is
	 * world-locked: there is no way to turn and look, so "where did that come
	 * from" is a question the player literally cannot answer otherwise. The world
	 * direction arrives on the health component in one byte
	 * (USarkoHealthComponent::GetLastDamageYawDegrees), riding along with the
	 * damage rather than on a channel of its own.
	 *
	 * Overlapping arcs are allowed and bounded (SarkoFeedback::FDamageArcRing):
	 * being shot from two sides at once is exactly when this matters most, and a
	 * TArray on a draw path would allocate mid-firefight.
	 */
	void DrawDamageArcs();

	/**
	 * THE HIT MARKER (spec §4.2). A four-tick cross over the VICTIM, for
	 * HitMarkerSeconds.
	 *
	 * At the victim rather than at screen centre, because there is no crosshair
	 * on a twin-stick touch game and screen centre is not where the player is
	 * looking — the aim cone points somewhere else entirely. The server confirms
	 * the hit (it is the side that traces and applies the damage) and notifies
	 * this client alone.
	 */
	void DrawHitMarker();

	/**
	 * THE LOW-HEALTH VIGNETTE (spec §4.4). Alpha bands pulled in from the safe
	 * frame's edges below LowHealthVignetteHealth.
	 *
	 * DrawRect bands rather than a material, because a post-process material is a
	 * binary asset and this project authors none. Drawn EARLY in DrawHUD, before
	 * every readout, which is the whole answer to "it must not fight the survival
	 * meters' legibility": the meters are painted over it, not under it.
	 */
	void DrawLowHealthVignette();

	/** Screen position of a world point, or false when it is behind the camera —
	 *  which for a world-locked top-down camera means off the map, but the arcs
	 *  and the marker both project and neither may draw a garbage pixel. */
	bool ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreen) const;

	/**
	 * Hunger and thirst: two 150 x 5 pt bars stacked under the health bar.
	 *
	 * Takes the health bar's own geometry rather than recomputing it, so the
	 * three bars cannot drift into a ragged column when one of the numbers above
	 * moves. Drawn from DrawHealth for the same reason.
	 */
	void DrawSurvival(float BarX, float HealthBarBottomY, float BarWidth);
	void DrawAmmo();
	void DrawBackpack();

	/** The interact button, the "search this crate" prompt and the channel's progress bar. */
	void DrawInteract();

	/**
	 * The reload button: a rounded plate carrying the magazine count, amber below
	 * a third and pulsing at empty (spec §4.3).
	 *
	 * A dedicated control because reloading is a decision with a cost and the
	 * player must be able to make it BEFORE the magazine runs out. Its rect is
	 * SarkoInput::ReloadButtonRect and takes no game state, so it can never move —
	 * a control that moves is a control you mis-press.
	 */
	void DrawReload();

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

	/**
	 * Each zone's `opensAfterSeconds`, read from the same file at the same time
	 * and for the same reason as the names above: it never changes, every machine
	 * already has it, and re-reading a map definition on a tick path is exactly
	 * the per-frame allocation this project forbids. Parallel to CachedZoneNames
	 * by index.
	 *
	 * Presentation only. The server gates the dwell itself
	 * (ASarkoRaidGameMode::ExtractTick); this is what lets the HUD say WHY
	 * nothing is happening.
	 */
	TArray<float> CachedZoneOpensAfter;

	/** The raid's full length, so elapsed time can be derived from the replicated
	 *  RemainingSeconds. Same map-then-settings fallback the game mode uses. */
	float CachedRaidDuration = 0.f;

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
	 * The interact button's label is contextual now (ОБШУКАТИ / ЗАКРИТИ), so the
	 * measurement cache is keyed on the STRING and not on the action: two actions
	 * can share a width and none can share a string.
	 */
	FString CachedInteractLabel;

	/**
	 * The reload button's "magazine|reserve" pair, built and measured when either
	 * number changes rather than every frame. DrawHUD is a tick path.
	 *
	 * Keyed on the two numbers rather than compared as a string, because the string
	 * is now a Printf: comparing it would mean building it every frame to find out
	 * that it had not changed, which is the allocation the key exists to avoid.
	 * The ammo half carries INDEX_NONE for "reloading", exactly as CachedAmmoKey
	 * does; -2 for both means nothing is cached yet.
	 */
	FString CachedReloadLabel;
	float CachedReloadLabelWidth = 0.f;
	float CachedReloadLabelHeight = 0.f;
	int32 CachedReloadAmmoKey = -2;
	int32 CachedReloadReserveKey = -2;

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
	 *
	 * CachedReserveKey is the second half since the scarcity stage: the readout
	 * says "8 | 24" now, and picking ammo up out of a crate moves the reserve
	 * without moving the magazine — keyed on the count alone, the reserve figure
	 * would have stayed stale until the next shot.
	 */
	int32 CachedAmmoKey = -2;
	int32 CachedReserveKey = -2;
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

	/**
	 * The live damage arcs, fixed capacity, no allocation. See DrawDamageArcs.
	 */
	SarkoFeedback::FDamageArcRing DamageArcs;

	/**
	 * The damage serial this HUD has already turned into an arc.
	 *
	 * INDEX_NONE until the first draw, so a HUD that comes up on a pawn which has
	 * already been hit — a client joining, or a possession change — records what
	 * it finds rather than drawing an arc for a bullet that landed before it was
	 * watching. Polled rather than delegated: see USarkoHealthComponent::
	 * GetDamageSerial for why a counter is the one signal that behaves the same on
	 * a client and on a listen server.
	 */
	int32 SeenDamageSerial = INDEX_NONE;
};
