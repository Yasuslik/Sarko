#include "UI/SarkoHUD.h"

#include "CanvasItem.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoGameInstance.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/Canvas.h"
#include "EngineFontServices.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "GlobalRenderResources.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Misc/ScopeExit.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "Pawn/SarkoSurvival.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"
#include "UI/SarkoVision.h"

namespace
{
	/**
	 * Every size this HUD is drawn at, in **points** on the 844x390 landscape
	 * canvas described in UI/SarkoUiScale.h — i.e. what they measure on the glass
	 * of a phone, at any pixel density.
	 *
	 * The type sizes are chosen against the shelter menu, which was validated at
	 * real phone resolutions first: its title is 26 pt and its body 15 pt. The
	 * clock and the ammo count are the two things a player glances at mid-fight
	 * without stopping, so they get title-sized type; everything the player only
	 * reads while standing still is nearer body size.
	 */
	constexpr float ClockPt = 26.f;
	constexpr float AmmoPt = 26.f;
	constexpr float BackpackPt = 20.f;
	constexpr float ConnectingPt = 15.f;
	constexpr float PromptPt = 17.f;
	constexpr float ExtractPt = 20.f;
	constexpr float DiedPt = 40.f;
	constexpr float OutcomeTitlePt = 46.f;
	constexpr float ReturningPt = 17.f;
	/**
	 * The interact button's word at 14 pt, and it is no longer inside the button.
	 *
	 * It was 12 pt because ОБШУКАТИ is eight Cyrillic capitals and had to fit
	 * across a 96 pt rectangle. The button is a 64 pt CIRCLE now, which no
	 * Ukrainian verb in this game fits inside at any size worth reading — so the
	 * circle took a glyph and the word moved out beside it, where it is bounded by
	 * nothing and can be set at a size a player reads rather than deciphers. 14 pt
	 * is the smallest type anywhere on this HUD that is still a caption and not a
	 * footnote; the first-raid hints are 15.
	 *
	 * The reload count is 16 pt, centred, and it was 20 when the button was a 56 pt
	 * SQUARE. What constrains it is not the button's 64 pt but the inside of the
	 * glyph's arc — 52 pt of clear width across the middle. Four characters at
	 * 20 pt measure 50 pt on the glass, which grazed the arc, and the realistic
	 * five ("2|120") would have run past the button's own edge. At 16 pt the five-
	 * character worst case is 40 pt and has a point of margin all round.
	 */
	constexpr float InteractLabelPt = 14.f;
	constexpr float ReloadLabelPt = 16.f;

	/** In from the safe frame's side edges, and down from its top. */
	constexpr float SideInsetPt = 16.f;
	constexpr float TopRowPt = 10.f;

	/**
	 * The top rows, stacked down the safe frame in points.
	 *
	 * The order is the old one — clock and readouts, then the connection warning,
	 * then the loot prompt, then the extraction banner — and the gaps are sized so
	 * that any two of them showing at once (holding a crate inside an extraction
	 * zone is a real state) still read as separate lines.
	 */
	constexpr float ConnectingTopPt = 46.f;
	constexpr float PromptTopPt = 72.f;
	constexpr float ExtractTopPt = 118.f;

	/** The health bar, top-right, vertically inside the clock's row. */
	constexpr float HealthBarWidthPt = 150.f;
	constexpr float HealthBarHeightPt = 11.f;
	constexpr float HealthBarTopPt = 16.f;

	/**
	 * Hunger and thirst, stacked directly under the health bar (spec §4).
	 *
	 * Same column, same width, HALF the height, because they move by a percent
	 * every twenty seconds and only need to be glanceable — where health is the
	 * thing you check mid-fight. Not the bottom (both corners are thumbs) and not
	 * the centre (the clock is there, with three caption slots beneath it).
	 */
	constexpr float SurvivalBarHeightPt = 5.f;
	constexpr float SurvivalBarGapPt = 3.f;

	/**
	 * Wheat for food, cyan for water — the two hues left over once the seven
	 * inventory categories have theirs (SarkoUI::CategoryColour: weapon 6 deg,
	 * ammo 41, gear 78, med 168, vehicle 210, valuable 275, consumable 340).
	 * Wheat is desaturated well clear of ammo's brass and cyan sits between med's
	 * teal and vehicle's blue at a luminance neither reaches, so a bar can never
	 * be mistaken for a cell. Both are far brighter than the map's olive ground,
	 * and each sits on the same dark plate the health bar uses, so neither
	 * depends on what is underneath it.
	 */
	const FLinearColor FoodBarColour(0.680f, 0.360f, 0.100f);   // #D8A25A
	const FLinearColor WaterBarColour(0.075f, 0.545f, 0.795f);  // #4FC3E8

	/** A meter at or below the penalty threshold pulses, with the reload button's
	 *  existing curve — one animation vocabulary for "this needs attention". */
	constexpr float SurvivalLowMinAlpha = 0.35f;

	/** The loot channel's progress bar, under the prompt it belongs to. */
	constexpr float LootBarWidthPt = 170.f;
	constexpr float LootBarHeightPt = 8.f;
	constexpr float LootBarGapPt = 8.f;

	/** Padding inside the dark plate behind a piece of text. */
	constexpr float PlatePadXPt = 10.f;
	constexpr float PlatePadYPt = 4.f;

	/** Stroke weights. Hairlines at 1:1 disappear on a phone. */
	constexpr float StickStrokePt = 1.5f;
	constexpr float AimConeStrokePt = 1.f;
	constexpr float StickDotPt = 11.f;

	/** The quiet/audible ring is the same stroke, dimmer: it is a boundary inside
	 *  the stick, not a second stick. */
	constexpr float QuietRingAlphaScale = 0.55f;

	/**
	 * THE HOME RING'S WEIGHT, against its own stick's.
	 *
	 * At rest it is drawn HALF AGAIN as opaque as the live stick's outline. The
	 * stick's 0.35 and 0.45 were chosen for a ring that appears under a thumb the
	 * player is already looking at; the home has to be found by a player who is
	 * looking at the middle of the screen and has touched nothing, over whatever
	 * ground the map put there.
	 *
	 * Once the thumb IS down, the live stick is the thing being read and the home
	 * drops well back — it has already done its job, and a mark this close to the
	 * anchor must not read as a third boundary. The stick has real boundaries
	 * (dead zone, fire threshold) and the home is not one of them.
	 */
	constexpr float HomeRingRestAlphaScale = 1.5f;
	constexpr float HomeRingActiveAlphaScale = 0.35f;

	/**
	 * The home ring's drop shadow, offset like every readout's.
	 *
	 * NEVER MORE OPAQUE THAN THE RING IT SHADES — its alpha is taken from the ring
	 * at draw time and this value only fixes the hue. Black at a fixed 0.55 behind
	 * a 0.2 amber was not a shadow, it was the mark: a translucent hue over pale
	 * ground loses to black every time, so what a resting thumb saw was a black
	 * ring with a yellow ghost beside it.
	 */
	const FLinearColor HomeRingShadow(0.f, 0.f, 0.f, 1.f);

	/**
	 * THE RELOAD GLYPH, in points on the 64 pt button.
	 *
	 * 26 pt of radius puts the arc 6 pt inside the rim and clear of the
	 * magazine|reserve pair in the middle: the pair is 20 pt type, about 39 pt
	 * wide at a realistic "8|24" and 50 pt at an absurd "8|720", against 52 pt of
	 * clear width across the arc's diameter.
	 *
	 * A 70 degree gap centred on straight up, with the arrowhead arriving at the
	 * far side of it: a broken ring with a head is a circular arrow, and an
	 * unbroken one is just an outline. 20 segments over 290 degrees is smooth at
	 * 3x. The stroke is heavier than the sticks' 1.5 pt because this one is a
	 * glyph read at a glance rather than a boundary read against a thumb.
	 */
	constexpr float ReloadIconRadiusPt = 26.f;
	constexpr float ReloadIconStrokePt = 2.5f;
	constexpr float ReloadIconGapDegrees = 70.f;
	constexpr float ReloadIconHeadPt = 9.f;
	constexpr int32 ReloadIconSegments = 20;

	/**
	 * THE INTERACT GLYPHS, in points on the same 64 pt button.
	 *
	 * A 16 pt half-extent is a 32 pt picture inside a 64 pt circle, leaving 16 pt
	 * of margin all round. It can be bigger than the reload glyph's clear width
	 * because it shares the middle with nothing: the reload button's centre belongs
	 * to the magazine|reserve pair, and this button's word is outside it.
	 *
	 * Same 2.5 pt stroke as the reload arrow, so the two read as one drawn family
	 * rather than as an icon set someone assembled.
	 */
	constexpr float InteractIconExtentPt = 16.f;
	constexpr float InteractIconStrokePt = 2.5f;

	/**
	 * The word, set OUTSIDE the button, inward of it, on the button's own centre
	 * line.
	 *
	 * To the SIDE and not underneath, and that is geometry rather than taste. The
	 * interact button sits at the far end of the thumb's arc, level with the aim
	 * home and 32 pt above the safe frame's bottom edge: a caption under it would
	 * fall off the frame, and one above it would land in the 40 pt of daylight
	 * between this button and the reload button — the one piece of empty screen
	 * that is doing a job. Inward along its own centre line is the direction with
	 * a clear lane in it.
	 *
	 * Right-aligned to the button, so the word grows away from the control and its
	 * right edge is always exactly this far from the rim. A left-aligned caption
	 * would push ЕВАКУАЦІЯ two characters closer to the glyph than ЗАКРИТИ.
	 */
	constexpr float InteractLabelGapPt = 12.f;

	/**
	 * THE FIRE RING, on the aim stick, at USarkoRaidSettings::AimFireDeadZone of
	 * its travel: cross this and you are shooting.
	 *
	 * Not the dimming the move stick's boundary gets, because the two rings answer
	 * different questions. The walk/run line is information you consult — it can
	 * afford to recede. This one is the difference between aiming at a bot and
	 * emptying eight rounds at it, so it has to be legible in the half-second a
	 * thumb takes to get there.
	 *
	 * A hot orange-red against the stick's amber (1, 0.85, 0.2): the same family,
	 * so it reads as part of this control and not as an alert pasted over it, but
	 * far enough round the hue wheel that "the inner ring is the hot one" survives
	 * a glance, a thumb half-covering it, and daylight on a phone. It carries the
	 * aim ring's own alpha rather than more — a peer of the outline it sits inside,
	 * which is what keeps it from shouting. Hue does the talking, not brightness.
	 */
	const FLinearColor FireRingColour(1.f, 0.22f, 0.10f, 0.45f);

	/** The snap-target bracket: four corner ticks on a 12 pt box, at the same
	 *  weight as the aim cone it belongs to. */
	constexpr float BracketSizePt = 12.f;
	constexpr float BracketArmPt = 4.f;
	constexpr float BracketStrokePt = 1.5f;

	/**
	 * The first-raid hint band: one line, top-centre, below the loot prompt's row
	 * (clock 10 pt, connecting 46, prompt 72) — because the hint about the
	 * interact button shows, by definition, while that prompt is up and must not
	 * land on the thing it is pointing at.
	 *
	 * The SAME row as the extraction banner, deliberately: the two are mutually
	 * exclusive (DrawFirstRaidHints yields to it), and any row further down
	 * covers the player's own character, which sits near the middle of a
	 * world-locked top-down frame.
	 */
	constexpr float HintTopPt = 118.f;
	constexpr float HintPt = 15.f;

	/** How long a hint stays up whether or not the player obeys it, and the tail
	 *  of that it spends fading. A hint that never leaves is an obstruction. */
	constexpr float HintLifetimeSeconds = 6.f;
	constexpr float HintFadeSeconds = 1.f;

	namespace SarkoHint
	{
		/**
		 * The four hints, in priority order — only ever one on screen, because a
		 * wall of text at raid start is the thing every game that gets this right
		 * refuses to draw.
		 *
		 * FIRE first: it is the single non-obvious rule in this control scheme.
		 * Every player arriving from a mobile shooter is looking for a fire button
		 * and there is not one — pointing IS shooting.
		 *
		 * Static FStrings and not Printf: DrawHUD is a tick path, these never
		 * change, and their widths are measured once per scale beside them.
		 */
		enum EHint : int32 { Fire = 0, Move = 1, Reload = 2, Interact = 3, Count = 4 };

		const FString Text[Count] = {
			FString(TEXT("ТРИМАЙ ПРАВИЙ СТІК — ВОГОНЬ")),
			FString(TEXT("ЛІВИЙ СТІК — РУХ · МАЛЕ ВІДХИЛЕННЯ = ТИХО")),
			FString(TEXT("КНОПКА СПРАВА — ПЕРЕЗАРЯДКА")),
			FString(TEXT("ТРИМАЙ ОБШУКАТИ, ЩОБ ВІДКРИТИ"))
		};
	}

	/** The drop shadow every readout gets. The HUD is drawn over an arbitrary
	 *  world, and white-on-white is the one failure that no size fixes. */
	const FLinearColor TextShadow(0.f, 0.f, 0.f, 0.75f);

	/** The backing under the two thumb-column buttons, for the same reason: a
	 *  translucent tint alone disappears against pale ground, and a control the
	 *  player cannot find is worse than one they do not like the look of. */
	const FLinearColor ThumbButtonPlate(0.f, 0.f, 0.f, 0.45f);

	/**
	 * THE DAMAGE ARC (spec §4.1), in points on the same 844x390 canvas.
	 *
	 * 96 pt of radius puts the arc clear of the pawn's own silhouette (the
	 * character reads about 40 pt tall from this camera) and well inside the safe
	 * frame's short edge, so an arc pointing straight up is still on screen. 44
	 * degrees of span is wide enough to read as "that way" at a glance and narrow
	 * enough that two arcs 90 degrees apart are unmistakably two.
	 */
	constexpr float DamageArcRadiusPt = 96.f;
	constexpr float DamageArcSpanDegrees = 44.f;
	constexpr float DamageArcStrokePt = 5.f;
	/** Segments across the span. Nine chords over 44 degrees is smooth at 3x. */
	constexpr int32 DamageArcSegments = 9;
	/** Arterial red, and nothing else on this HUD is this colour. */
	const FLinearColor DamageArcColour(0.95f, 0.12f, 0.10f);

	/** THE HIT MARKER (spec §4.2): four ticks around the victim, leaving a gap in
	 *  the middle so the body itself is never covered by its own confirmation. */
	constexpr float HitMarkerInnerPt = 7.f;
	constexpr float HitMarkerOuterPt = 16.f;
	constexpr float HitMarkerStrokePt = 2.5f;
	const FLinearColor HitMarkerColour(1.f, 1.f, 1.f, 0.92f);

	/**
	 * THE LOW-HEALTH VIGNETTE (spec §4.4).
	 *
	 * Six bands of 9 pt each pulled in from every edge of the safe frame: 54 pt of
	 * a 390 pt short edge, so the middle two thirds of the screen — where the
	 * player and everything they are shooting at live — is never touched. The
	 * alpha ramps to zero inward, which is what makes six flat rectangles read as
	 * a gradient.
	 *
	 * MaxAlpha is deliberately low. This is drawn UNDER every readout (see
	 * DrawHUD's call order), but the health bar and the two survival meters sit
	 * 16 pt from the top-right corner, inside the band region — so the vignette
	 * has to be a tint the meters win against, not a wash they have to fight.
	 */
	constexpr int32 VignetteBands = 6;
	constexpr float VignetteBandPt = 9.f;
	constexpr float VignetteMaxAlpha = 0.42f;
	const FLinearColor VignetteColour(0.55f, 0.03f, 0.02f);

	/**
	 * THE DIMMING LAYER'S COLOUR (vision spec §1).
	 *
	 * Not black. A NAVY-BLACK, because dusk is blue and soot is not: a pure black
	 * wash over the map's olive ground and grey concrete kills the hue difference
	 * between them and leaves the player navigating a greyscale photograph, which
	 * is precisely the disorientation the spec's second risk names. Pulling the
	 * blue channel up a little keeps ground, road and building distinguishable at
	 * the full dim while still reading as "the light is off over there".
	 *
	 * The alpha is NOT here: it comes from USarkoRaidSettings::VisionDimAlpha, is
	 * clamped by SarkoVision::ClampDimAlpha, and varies per vertex across the
	 * edge ramp and the near halo.
	 */
	const FLinearColor VisionDimColour(0.016f, 0.020f, 0.043f);

	/**
	 * How finely the fan is sampled in ANGLE, in degrees, away from the edges.
	 *
	 * The bulk of the dark region is a flat colour, so its only requirement is
	 * that a straight chord between two samples does not visibly cut a corner off
	 * the outer radius — and the outer radius is off screen by construction (it
	 * is the distance to the furthest canvas corner plus a margin), so nothing
	 * about the chord is ever seen. Twelve degrees is thirty-odd slices for the
	 * whole sweep, which costs a hundred-odd triangles.
	 */
	constexpr float VisionBulkSliceDegrees = 12.f;

	/**
	 * How many samples the soft edge gets, PER STEP of the ramp.
	 *
	 * Two, and two is the minimum that draws a step as a step: one at the
	 * plateau's start and one at its end, so the interpolation between them is
	 * flat and the change happens at the boundary. One sample per step would
	 * interpolate straight through the middle of every plateau and turn the
	 * stepped ramp SarkoVision::DimAlphaForAngle describes into a plain linear
	 * gradient — which is not worse to look at, but is not what the pure function
	 * says, and a drawing that disagrees with its own tested arithmetic is a
	 * drawing nobody can reason about.
	 */
	constexpr int32 VisionEdgeSamplesPerStep = 2;

	/** The fan's outer radius: past the furthest canvas corner, so the dark
	 *  region always runs off screen rather than ending in an arc. */
	constexpr float VisionOuterMarginPx = 8.f;
}

void ASarkoHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// UCanvas applies the platform's safe zone itself before PostRender: on a
	// device that reports insets it translates the canvas origin inward and
	// shrinks SizeX/SizeY to match (Engine's UCanvas::ApplySafeZoneTransform).
	// That is the same job SarkoInput::SafeFrame does, and doing both insets the
	// HUD twice — while ASarkoPlayerController::UpdateSticks hit-tests the
	// interact button in raw viewport pixels, having done it once. The button
	// would then be drawn in one place and pressed in another, which is precisely
	// the bug this HUD had before and the one the player experiences as "it does
	// not work". So the engine's transform is lifted for the duration of our own
	// drawing and put back afterwards, leaving exactly one authority on the safe
	// area — SafeFrame, the one the input path also uses.
	//
	// Both calls return immediately when the platform reports no insets, which is
	// every desktop and every headless run, so nothing about the Mac changes.
	Canvas->PopSafeZoneTransform();
	ON_SCOPE_EXIT{ Canvas->ApplySafeZoneTransform(); };

	// Once per frame, before anything is placed. See the members' comments.
	const FVector2D ViewportSize(Canvas->SizeX, Canvas->SizeY);
	Safe = SarkoInput::SafeFrame(ViewportSize);

	const float NewScale = SarkoUI::PointScaleForViewport(ViewportSize);
	if (NewScale != MeasuredAtScale)
	{
		PointScale = NewScale;
		InvalidateMeasurements();

		// Logged on the scale changing rather than per frame — so once, on the
		// first frame of a raid, and again only if the window is resized. The whole
		// layout is expressed in points and this factor is the only thing turning
		// it into pixels; a wrong one is an unreadable HUD and nothing else says so.
		const FVector2D Digits = MeasurePt(TEXT("00:00"), ClockPt);
		UE_LOG(LogTemp, Display,
			TEXT("SarkoHUD: viewport %.0fx%.0f px, scale %.3f px/pt (canvas %.0fx%.0f pt); ")
			TEXT("clock %.0f px = %.1f pt tall, interact button %.0f px = %.1f pt"),
			ViewportSize.X, ViewportSize.Y, PointScale,
			ViewportSize.X / PointScale, ViewportSize.Y / PointScale,
			Digits.Y, Digits.Y / PointScale,
			SarkoInput::InteractButtonRect(Safe, PointScale).GetSize().X,
			SarkoInput::InteractButtonRect(Safe, PointScale).GetSize().X / PointScale);

		MeasuredAtScale = NewScale;
	}

	// THE VISION CONE'S DIMMING IS THE BOTTOM LAYER OF THE WHOLE HUD, and that
	// ordering is the answer to "the dimming is a world effect, not a screen
	// effect over the interface". Everything below this line — the vignette, the
	// arcs, both sticks and their homes, every readout, the two round buttons,
	// the loot prompt and the outcome screen — is painted OVER it and keeps its
	// own contrast. The container panel is Slate and sits above the canvas
	// entirely, so it never darkens at all.
	DrawVisionDimming();

	// COMBAT FEEDBACK FIRST, and the order is the answer to spec §4.4's
	// requirement that the vignette must not fight the survival meters: it is
	// painted before every readout on this HUD, so the health bar, the two meters
	// and the clock are all drawn OVER it rather than under it. The arcs and the
	// marker follow for the same reason — they are world-anchored and can land
	// anywhere, including under the top row.
	DrawLowHealthVignette();
	DrawDamageArcs();
	DrawHitMarker();

	// EACH STICK GETS ITS OWN BOUNDARY DRAWN, and only its own. The move stick's
	// is the walk/run noise line; the aim stick's is where aiming becomes
	// shooting. Both fractions are read from the settings here and handed to a
	// drawing function that knows nothing about either rule, so a change to
	// either setting moves the ring with it.
	//
	// The aim stick used to get no inner ring at all — the reasoning being that
	// drawing the move stick's fraction on it would teach the wrong rule twice.
	// True, and beside the point: it needed its OWN line. Without it the fire
	// threshold was a number in an .ini that the player could only locate by
	// crossing it, which on a phone, with an eight-round magazine and a manual
	// reload, costs the magazine and gives away the position.
	//
	// BOTH STICKS ALSO GET THEIR HOME DRAWN, which is what makes them findable at
	// all: they float, so before this there was nothing on screen at raid start to
	// say a control lived under either thumb. See DrawStick.
	const USarkoRaidSettings& StickSettings = *GetDefault<USarkoRaidSettings>();

	// The move stick's home is hidden while the stick itself is suppressed, which
	// today is exactly "a container panel is open" (spec §4.5). The panel sits over
	// the bottom-left, i.e. over that very home, and a mark inviting a thumb onto a
	// control that is switched off is worse than no mark at all. Asked of the same
	// pure function ASarkoPlayerController::UpdateSticks asks, from the pawn's own
	// mirror of what is open, so the two cannot disagree about it.
	const ASarkoCharacter* StickPawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const bool bMoveHomeVisible = !SarkoInput::IsMoveStickSuppressed(
		StickPawn && StickPawn->GetOpenContainerIndex() != INDEX_NONE);

	const FLinearColor MoveColour(1.f, 1.f, 1.f, 0.35f);
	FLinearColor QuietColour = MoveColour;
	QuietColour.A *= QuietRingAlphaScale;
	DrawStick(PC->GetMoveStick(), SarkoInput::MoveStickHome(Safe, PointScale), bMoveHomeVisible,
		MoveColour, StickSettings.NoiseRunSpeedFraction, QuietColour);

	DrawStick(PC->GetAimStick(), SarkoInput::AimStickHome(Safe, PointScale), /*bShowHome*/ true,
		FLinearColor(1.f, 0.85f, 0.2f, 0.45f), StickSettings.AimFireDeadZone, FireRingColour);
	DrawAimCone();
	DrawTopBar();
	DrawHealth();
	DrawAmmo();
	DrawBackpack();
	DrawInteract();
	DrawReload();
	DrawExtraction();
	DrawFirstRaidHints(*PC);
	// Last, so the final screen is over everything else rather than under it.
	DrawOutcomeSummary();
}

void ASarkoHUD::InvalidateMeasurements()
{
	CachedClockSeconds = -1;
	CachedReloadingWidth = -1.f;
	CachedInteractLabelWidth = -1.f;
	CachedInteractLabel.Reset();
	CachedReloadLabel.Reset();
	bPromptCached = false;
	for (FVector2D& Size : CachedHintSize)
	{
		Size = FVector2D(-1.f, 0.f);
	}
}

FSlateFontInfo ASarkoHUD::FontPt(float PointSize) const
{
	// The engine's large font, asked for at a size rather than magnified from one.
	// With no LargeFontName configured it resolves to a transient UFont whose
	// RuntimeFontSource is CoreStyleDefault — so this is FCoreStyle's face, the
	// same one the shelter menu draws, and still no font asset.
	//
	// Not FCoreStyle::GetDefaultFontStyle() directly, which is what the shelter
	// calls: the style hands back an FSlateFontInfo with a null FontObject, and
	// FCanvasSimpleTextItem::HasValidText() tests exactly that pointer and then
	// silently draws nothing. A HUD that renders every bar and no text at all is
	// how that reads from the outside.
	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Large)
	{
		return FSlateFontInfo();
	}
	// Runtime-cached, so overriding the size asks the font cache to rasterise
	// glyphs at that size instead of scaling up the 10-pixel ones the legacy size
	// would give — which is the difference between a readable clock and a blurry
	// one at the 3x a phone needs.
	FSlateFontInfo Info = Large->GetLegacySlateFontInfo();
	Info.Size = FMath::Max(1.f, PointSize * PointScale);
	return Info;
}

FVector2D ASarkoHUD::MeasurePt(const FString& Text, float PointSize) const
{
	if (!FEngineFontServices::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::Get().GetFontMeasure();
	if (!Measure.IsValid())
	{
		return FVector2D::ZeroVector;
	}
	// FontScale 1, matching the canvas: the size asked for in FontPt is already in
	// pixels, and the canvas the HUD draws into has a DPI scale of exactly 1.
	return Measure->Measure(Text, FontPt(PointSize), 1.f);
}

void ASarkoHUD::DrawTextPt(const FString& Text, const FLinearColor& Colour, float X, float Y, float PointSize)
{
	// FStringView and not FText: DrawHUD is a tick path and the strings handed to
	// this are the cached ones the class already keeps, so nothing here allocates.
	FCanvasTextStringViewItem Item(FVector2D(X, Y), Text, FontPt(PointSize), Colour);
	Item.EnableShadow(TextShadow, FVector2D(FMath::Max(1.f, Px(1.f)), FMath::Max(1.f, Px(1.f))));
	Canvas->DrawItem(Item);
}

void ASarkoHUD::DrawArc(FVector2D Centre, float Radius, float StartRadians, float EndRadians,
	int32 Segments, const FLinearColor& Colour, float Thickness)
{
	const int32 Count = FMath::Max(1, Segments);
	for (int32 i = 0; i < Count; ++i)
	{
		const float A0 = FMath::Lerp(StartRadians, EndRadians, static_cast<float>(i) / Count);
		const float A1 = FMath::Lerp(StartRadians, EndRadians, static_cast<float>(i + 1) / Count);
		DrawLine(
			Centre.X + FMath::Cos(A0) * Radius,
			Centre.Y + FMath::Sin(A0) * Radius,
			Centre.X + FMath::Cos(A1) * Radius,
			Centre.Y + FMath::Sin(A1) * Radius,
			Colour, Thickness);
	}
}

void ASarkoHUD::DrawRing(FVector2D Centre, float Radius, const FLinearColor& Colour)
{
	DrawArc(Centre, Radius, 0.f, 2.f * PI, 24, Colour, Px(StickStrokePt));
}

void ASarkoHUD::DrawDisc(FVector2D Centre, float Radius, const FLinearColor& Colour)
{
	// Bands sampled in ANGLE, not in Y — see the header for why that is what makes
	// a stack of rectangles read as a circle.
	//
	// The COUNT is chosen from the radius in pixels rather than fixed, so the step
	// in the silhouette is about the same size on every screen instead of getting
	// coarser the denser the phone: a band per 1.5 px of radius holds the worst
	// stair (which is at the diagonals, where the outline moves fastest against
	// both axes at once) near a point across at any scale. A fixed two dozen looked
	// right in an editor window and showed a 9-pixel staircase at 3x.
	const int32 Bands = FMath::Clamp(FMath::CeilToInt(Radius / 1.5f), 16, 64);
	for (int32 i = 0; i < Bands; ++i)
	{
		const float T0 = (PI * i) / static_cast<float>(Bands);
		const float T1 = (PI * (i + 1)) / static_cast<float>(Bands);
		const float Top = -Radius * FMath::Cos(T0);
		const float Bottom = -Radius * FMath::Cos(T1);
		// The chord at the band's mid-angle: half the error above the band's centre
		// and half below it, rather than all of it on one side.
		const float HalfWidth = Radius * FMath::Sin((T0 + T1) * 0.5f);
		DrawRect(Colour, Centre.X - HalfWidth, Centre.Y + Top, HalfWidth * 2.f, Bottom - Top);
	}
}

void ASarkoHUD::DrawReloadIcon(FVector2D Centre, const FLinearColor& Colour)
{
	const float Radius = Px(ReloadIconRadiusPt);
	const float Stroke = Px(ReloadIconStrokePt);

	// Canvas angles, with Y growing DOWNWARD: increasing angle sweeps clockwise on
	// the glass, which is the direction every reload/refresh glyph a player has
	// ever seen turns. The gap is centred on straight up so the arrowhead arrives
	// at the top, where the eye lands first.
	const float GapHalf = FMath::DegreesToRadians(ReloadIconGapDegrees * 0.5f);
	const float StraightUp = -HALF_PI;
	const float Start = StraightUp + GapHalf;
	const float End = StraightUp + 2.f * PI - GapHalf;
	DrawArc(Centre, Radius, Start, End, ReloadIconSegments, Colour, Stroke);

	// THE ARROWHEAD: two barbs raked back along the direction of travel. Two lines
	// and not a filled triangle, because a filled triangle on a UCanvas means an
	// FCanvasTriangleItem and its TArray — a heap allocation per frame on a tick
	// path — and because a V at this stroke reads as an arrow at every size this
	// button is drawn at.
	const FVector2D Tip(Centre.X + FMath::Cos(End) * Radius, Centre.Y + FMath::Sin(End) * Radius);
	const FVector2D Travel(-FMath::Sin(End), FMath::Cos(End));
	const FVector2D Outward(FMath::Cos(End), FMath::Sin(End));
	const float Head = Px(ReloadIconHeadPt);

	// 30 degrees either side of the shaft, which is the rake an arrow is expected
	// to have; wider reads as a chevron and narrower disappears at 2 pt of stroke.
	const FVector2D Back = -Travel * (Head * 0.866f);
	const FVector2D Side = Outward * (Head * 0.5f);
	DrawLine(Tip.X, Tip.Y, Tip.X + Back.X + Side.X, Tip.Y + Back.Y + Side.Y, Colour, Stroke);
	DrawLine(Tip.X, Tip.Y, Tip.X + Back.X - Side.X, Tip.Y + Back.Y - Side.Y, Colour, Stroke);
}

void ASarkoHUD::DrawInteractIcon(FVector2D Centre, SarkoUI::EInteractAction Action, const FLinearColor& Colour)
{
	const float E = Px(InteractIconExtentPt);
	const float Stroke = Px(InteractIconStrokePt);

	// A local so the six or eight strokes below read as coordinates on a 32 pt
	// square rather than as arithmetic. Nothing allocates: this is a lambda over
	// three floats on a tick path.
	auto Stroke2 = [this, Centre, E, Stroke, &Colour](float X0, float Y0, float X1, float Y1)
	{
		DrawLine(Centre.X + X0 * E, Centre.Y + Y0 * E,
			Centre.X + X1 * E, Centre.Y + Y1 * E, Colour, Stroke);
	};

	switch (Action)
	{
	case SarkoUI::EInteractAction::Search:
	{
		// AN OPEN CRATE. A box across the lower two thirds, with a flap splayed up
		// and out from each of its top corners — the silhouette of a container
		// someone has just opened, which is exactly the verb (ОБШУКАТИ, "search
		// it"). Angular on purpose: the button beside it is a circle carrying a
		// circular arrow, and a straight-edged picture is the fastest difference
		// there is.
		Stroke2(-0.80f, -0.10f, 0.80f, -0.10f);   // the box: top, then round it
		Stroke2(0.80f, -0.10f, 0.80f, 0.85f);
		Stroke2(0.80f, 0.85f, -0.80f, 0.85f);
		Stroke2(-0.80f, 0.85f, -0.80f, -0.10f);
		Stroke2(-0.80f, -0.10f, -1.15f, -0.75f);  // the two flaps, hinged open
		Stroke2(0.80f, -0.10f, 1.15f, -0.75f);
		break;
	}
	case SarkoUI::EInteractAction::Extract:
	{
		// A DOORWAY WITH AN ARROW LEAVING THROUGH IT — the exit sign, minus its
		// running figure. The frame is open on the side the arrow leaves by, so the
		// arrow reads as passing through the opening rather than as pointing at a
		// wall.
		Stroke2(-0.95f, -0.90f, -0.95f, 0.90f);   // the jamb
		Stroke2(-0.95f, -0.90f, -0.35f, -0.90f);  // lintel and threshold, part way
		Stroke2(-0.95f, 0.90f, -0.35f, 0.90f);
		Stroke2(-0.40f, 0.f, 0.95f, 0.f);         // the shaft, out through the gap
		Stroke2(0.95f, 0.f, 0.45f, -0.40f);       // and a two-barb head, raked back
		Stroke2(0.95f, 0.f, 0.45f, 0.40f);
		break;
	}
	case SarkoUI::EInteractAction::Close:
	{
		// A CROSS. Nothing else on this HUD is two straight strokes through a
		// centre, and "close the thing that is open" is the one action here that
		// every player already knows the picture for.
		Stroke2(-0.75f, -0.75f, 0.75f, 0.75f);
		Stroke2(-0.75f, 0.75f, 0.75f, -0.75f);
		break;
	}
	default:
		// None: nothing in reach, so nothing is drawn. A picture here would be the
		// button claiming an action it does not have.
		break;
	}
}

void ASarkoHUD::DrawStick(const FSarkoTouchStick& Stick, FVector2D Home, bool bShowHome,
	const FLinearColor& Colour, float BoundaryFraction, const FLinearColor& BoundaryColour)
{
	// THE HOME, FIRST AND WHETHER OR NOT A FINGER IS DOWN. This function used to
	// return here on an inactive stick, and that early return is the whole of the
	// owner's "the sticks don't work" report: a floating control that is invisible
	// until touched is a control nobody knows to touch.
	//
	// Drawn at SarkoInput::StickHomeRingPt — half the travel — because the stick
	// FLOATS and its real extent appears under the thumb, wherever that lands. A
	// full-radius ring here would be a picture of a boundary that does not exist.
	if (bShowHome)
	{
		// Heavier than the live stick's outline at rest, much lighter than it while
		// a thumb is down. See the two constants.
		FLinearColor HomeColour = Colour;
		HomeColour.A = FMath::Min(1.f, Colour.A * HomeRingRestAlphaScale)
			* (Stick.bActive ? HomeRingActiveAlphaScale : 1.f);

		// The shadow takes the ring's own alpha, so it can never out-draw it — see
		// HomeRingShadow. One PIXEL of offset and not one point: a point is three
		// pixels at 3x, which on a stroke four pixels wide separates into a second
		// ring rather than reading as an edge.
		FLinearColor ShadowColour = HomeRingShadow;
		ShadowColour.A = HomeColour.A;

		const float HomeRadius = Px(SarkoInput::StickHomeRingPt);
		DrawRing(Home + FVector2D(1.f, 1.f), HomeRadius, ShadowColour);
		DrawRing(Home, HomeRadius, HomeColour);
	}

	if (!Stick.bActive)
	{
		return;
	}

	// Ring at the thumb's landing point, dot at the current position.
	//
	// The radius is read off the STICK and not from a constant here. It used to be
	// FSarkoTouchStick's static 100 px with a comment claiming the ring was
	// deliberately unscaled — which was true, and was the bug: the rule itself was
	// in pixels, so on a 3.02x phone full deflection was 33 pt of thumb travel,
	// less than the 44 pt minimum this project asserts on its own buttons. The
	// rule is in POINTS now (SarkoInput::StickRadiusPt), resolved to pixels once
	// when the thumb anchors, and the ring is still an exact picture of it —
	// because it is drawn from the same number the input maths divides by.
	DrawRing(Stick.Origin, Stick.RadiusPx, Colour);

	// THE BOUNDARY THIS STICK CHANGES BEHAVIOUR AT — the walk/run line on the move
	// stick, the aim/fire line on the aim stick. One ring, drawn from the fraction
	// the caller read out of the settings, so the rule and its picture are the same
	// number in both cases.
	//
	// Inside the outer ring, always: it reads as a band within the stick rather
	// than as a second control. Whether it is dimmer or merely different is the
	// caller's to say — the walk/run line is information, the fire line is a
	// warning, and they should not look alike.
	//
	// Guarded rather than clamped. A fraction of 0 or 1 would put the ring exactly
	// on the dot or exactly on the stick's own outline, where it says nothing and
	// only muddies what is there; drawing nothing is the honest picture of a
	// boundary that has no width to sit in.
	const float Fraction = BoundaryFraction;
	if (Fraction > KINDA_SMALL_NUMBER && Fraction < 1.f)
	{
		DrawRing(Stick.Origin, Stick.RadiusPx * Fraction, BoundaryColour);
	}

	const float Dot = Px(StickDotPt);
	DrawRect(Colour, Stick.Current.X - Dot * 0.5f, Stick.Current.Y - Dot * 0.5f, Dot, Dot);
}

bool ASarkoHUD::ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreen) const
{
	// AHUD::Project's Z is the distance in front of the camera; a non-positive
	// one is a point behind it, and its X/Y are then mirrored garbage. The
	// world-locked top-down camera makes that nearly unreachable, but "nearly"
	// is not a reason to draw a marker at a coordinate that means nothing.
	const FVector Projected = Project(WorldLocation);
	if (Projected.Z <= 0.f)
	{
		return false;
	}
	OutScreen = FVector2D(Projected.X, Projected.Y);
	return true;
}

void ASarkoHUD::DrawDamageArcs()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const USarkoHealthComponent* Health = Pawn ? Pawn->HealthComponent : nullptr;
	if (!Health)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// A new hit is a new arc. Polled once per frame — one byte compared, and the
	// push happens a few times a fight rather than a few times a second.
	const int32 Serial = static_cast<int32>(Health->GetDamageSerial());
	if (Serial != SeenDamageSerial)
	{
		const bool bFirstObservation = (SeenDamageSerial == INDEX_NONE);
		SeenDamageSerial = Serial;
		if (!bFirstObservation)
		{
			DamageArcs.Add(Health->GetLastDamageYawDegrees(), Now);
		}
	}

	const float Lifetime = GetDefault<USarkoRaidSettings>()->DamageArcSeconds;
	const float Radius = Px(DamageArcRadiusPt);
	const float Stroke = Px(DamageArcStrokePt);

	// The pawn's own screen position, not the safe frame's centre: the top-down
	// camera trails the pawn, so the two are not the same point and an arc drawn
	// around the wrong one points at the wrong place by exactly that offset.
	FVector2D Centre;
	if (!ProjectToScreen(Pawn->GetActorLocation(), Centre))
	{
		return;
	}

	// WORLD YAW TO SCREEN. The camera is world-locked but its yaw is not
	// guaranteed to be zero, so the mapping is measured rather than assumed: one
	// extra projection of a point 500 uu along the reported direction gives the
	// screen-space vector that direction corresponds to, exactly. Two projections
	// per arc, at most four arcs, allocating nothing.
	for (int32 Index = 0; Index < DamageArcs.Num(); ++Index)
	{
		const SarkoFeedback::FDamageArc& Arc = DamageArcs.Get(Index);
		const float Alpha = SarkoFeedback::FadeAlpha(Now - Arc.StartSeconds, Lifetime);
		if (Alpha <= 0.f)
		{
			continue;
		}

		const FVector WorldDirection = FRotator(0.f, Arc.YawDegrees, 0.f).Vector();
		FVector2D Probe;
		if (!ProjectToScreen(Pawn->GetActorLocation() + WorldDirection * 500.f, Probe))
		{
			continue;
		}
		const FVector2D ScreenDirection = (Probe - Centre).GetSafeNormal();
		if (ScreenDirection.IsNearlyZero())
		{
			continue;
		}

		// Atan2 with Y first and NEGATED, because screen Y grows downward while
		// the angle sweep below is drawn with a +Y-down sin: without the flip the
		// arc mirrors about the horizontal and points at the reflection of the
		// shooter, which is right half the time and therefore the worst kind of
		// wrong.
		const float CentreRadians = FMath::Atan2(ScreenDirection.Y, ScreenDirection.X);
		const float Span = FMath::DegreesToRadians(DamageArcSpanDegrees);

		FLinearColor Colour = DamageArcColour;
		Colour.A = Alpha;

		for (int32 Segment = 0; Segment < DamageArcSegments; ++Segment)
		{
			const float A0 = SarkoFeedback::ArcSegmentAngle(CentreRadians, Span, Segment, DamageArcSegments);
			const float A1 = SarkoFeedback::ArcSegmentAngle(CentreRadians, Span, Segment + 1, DamageArcSegments);
			DrawLine(
				Centre.X + FMath::Cos(A0) * Radius, Centre.Y + FMath::Sin(A0) * Radius,
				Centre.X + FMath::Cos(A1) * Radius, Centre.Y + FMath::Sin(A1) * Radius,
				Colour, Stroke);
		}
	}
}

void ASarkoHUD::DrawHitMarker()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	FVector VictimLocation;
	float Age = 0.f;
	if (!Pawn->GetHitMarker(VictimLocation, Age))
	{
		return;
	}

	FVector2D Screen;
	if (!ProjectToScreen(VictimLocation, Screen))
	{
		return;
	}

	// Four ticks with a hole in the middle, drawn AT THE VICTIM. A cross at screen
	// centre would be the convention from games with a crosshair; this one has an
	// aim cone pointing somewhere else entirely, and a marker in the middle of the
	// screen would be confirming a hit next to a piece of geometry the player was
	// not shooting at.
	const float Inner = Px(HitMarkerInnerPt);
	const float Outer = Px(HitMarkerOuterPt);
	const float Stroke = Px(HitMarkerStrokePt);

	// Full opacity for the whole 0.15 s rather than a fade: this is a yes/no
	// answer, and a fading yes reads as a maybe.
	const FLinearColor Colour = HitMarkerColour;

	DrawLine(Screen.X - Outer, Screen.Y, Screen.X - Inner, Screen.Y, Colour, Stroke);
	DrawLine(Screen.X + Inner, Screen.Y, Screen.X + Outer, Screen.Y, Colour, Stroke);
	DrawLine(Screen.X, Screen.Y - Outer, Screen.X, Screen.Y - Inner, Colour, Stroke);
	DrawLine(Screen.X, Screen.Y + Inner, Screen.X, Screen.Y + Outer, Colour, Stroke);
}

void ASarkoHUD::DrawLowHealthVignette()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const USarkoHealthComponent* Health = Pawn ? Pawn->HealthComponent : nullptr;
	if (!Health)
	{
		return;
	}

	const float Intensity = SarkoFeedback::VignetteIntensity(
		Health->GetHealth(), GetDefault<USarkoRaidSettings>()->LowHealthVignetteHealth);
	if (Intensity <= 0.f)
	{
		return;
	}

	// The reload button's curve, so this HUD has ONE animation vocabulary for
	// "this needs attention" — the survival meters already borrow it. Bounded away
	// from zero for the same reason they bound theirs: an effect that vanishes on
	// the trough reads as a rendering fault rather than as urgency.
	const float Pulse = FMath::Max(0.45f, SarkoUI::ReloadPulseAlpha(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f));
	const float PeakAlpha = VignetteMaxAlpha * Intensity * Pulse;

	const float Band = Px(VignetteBandPt);
	const FVector2D Size = Safe.GetSize();

	// Bands from the edge inward, alpha ramping to nothing. Twenty-four DrawRects
	// on the frames the player is nearly dead, and none on any other frame — and
	// they are computed rather than cached because the arithmetic is four
	// multiplies and a cache keyed on the safe frame would be more state than the
	// thing it saves.
	for (int32 Index = 0; Index < VignetteBands; ++Index)
	{
		FLinearColor Colour = VignetteColour;
		Colour.A = PeakAlpha * (1.f - static_cast<float>(Index) / static_cast<float>(VignetteBands));
		const float Offset = Band * static_cast<float>(Index);

		// Top and bottom span the full width; the sides are inset by the bands
		// already drawn above and below them, so the corners are not painted twice
		// (which would stack alpha into a dark blob exactly where the eye is least
		// able to tell a vignette from a bug).
		DrawRect(Colour, Safe.Min.X, Safe.Min.Y + Offset, Size.X, Band);
		DrawRect(Colour, Safe.Min.X, Safe.Max.Y - Offset - Band, Size.X, Band);

		const float SideY = Safe.Min.Y + Band * static_cast<float>(VignetteBands);
		const float SideHeight = FMath::Max(0.f, Size.Y - Band * static_cast<float>(VignetteBands) * 2.f);
		DrawRect(Colour, Safe.Min.X + Offset, SideY, Band, SideHeight);
		DrawRect(Colour, Safe.Max.X - Offset - Band, SideY, Band, SideHeight);
	}
}

void ASarkoHUD::DrawVisionDimming()
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	if (!Settings.bVisionConeEnabled)
	{
		return;
	}

	const float MaxAlpha = SarkoVision::ClampDimAlpha(Settings.VisionDimAlpha);
	if (MaxAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// A corpse has no facing worth drawing a cone around: ASarkoCharacter::Tick
	// stops rotating the mesh at death, so the wedge would freeze pointing
	// wherever the player happened to be looking when they were killed and sit
	// there under the summary screen. The raid is over; the map is not a secret
	// any more.
	if (Pawn->HealthComponent && Pawn->HealthComponent->IsDead())
	{
		return;
	}

	// The apex is the PAWN'S projected position, not the middle of the frame: the
	// camera trails the pawn, so the two differ by a few dozen pixels and a fan
	// built on the wrong one lights a wedge that starts beside the player.
	const FVector PawnLocation = Pawn->GetActorLocation();
	FVector2D Apex;
	if (!ProjectToScreen(PawnLocation, Apex))
	{
		return;
	}

	// WORLD FACING TO SCREEN, MEASURED RATHER THAN ASSUMED — the same probe
	// DrawDamageArcs uses, and for the same reason: the camera is world-locked
	// but its yaw is not guaranteed to be zero, and a fan drawn on an assumed
	// mapping points somewhere the character is not.
	//
	// GetActorForwardVector and not AimDirection: the character interpolates
	// toward its aim (or toward its travel when the aim stick is centred), so the
	// forward vector is the direction the player can SEE the body pointing.
	// Lighting a wedge the body has not turned into yet is a cone that leads the
	// character, and the owner's ask was «куда кручусь, туда и видно» — where it
	// has turned, not where it is turning.
	constexpr float ProbeUU = 500.f;
	FVector2D Probe;
	if (!ProjectToScreen(PawnLocation + Pawn->GetActorForwardVector() * ProbeUU, Probe))
	{
		return;
	}
	const FVector2D ScreenForward = Probe - Apex;
	const float ProbePx = ScreenForward.Size();
	if (ProbePx <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// The same probe gives the world-to-screen scale for free, which is what puts
	// the near halo at a distance in METRES rather than in pixels — a halo sized
	// in pixels would be a different amount of ground on every screen.
	const float PixelsPerUU = ProbePx / ProbeUU;
	const float HaloPx = FMath::Max(1.f, Settings.VisionNearHaloUU * PixelsPerUU);

	// Screen Y grows DOWNWARD, so a positive angle sweeps clockwise on the glass
	// and every angle below is in that space. This is the same convention DrawArc
	// documents and DrawDamageArcs relies on.
	const float CentreRadians = FMath::Atan2(ScreenForward.Y, ScreenForward.X);

	// Out past the furthest CANVAS corner, not the safe frame's: the world is
	// drawn under the cutouts too, and a dark region that stopped at the safe
	// frame would leave a bright band down both edges of a phone.
	const FVector2D Corners[4] = {
		FVector2D(0.f, 0.f),
		FVector2D(Canvas->SizeX, 0.f),
		FVector2D(0.f, Canvas->SizeY),
		FVector2D(Canvas->SizeX, Canvas->SizeY) };
	float OuterPx = 0.f;
	for (const FVector2D& Corner : Corners)
	{
		OuterPx = FMath::Max(OuterPx, (Corner - Apex).Size());
	}
	OuterPx += VisionOuterMarginPx;
	if (OuterPx <= HaloPx)
	{
		return;
	}

	const float Half = SarkoVision::ConeHalfAngleDegrees(Settings.VisionConeDegrees);
	const float Soft = SarkoVision::ClampSoftEdgeDegrees(Settings.VisionConeSoftEdgeDegrees, Half);

	// THE SWEEP IS THE COMPLEMENT OF THE CONE, walked once in increasing angle
	// from the cone's near edge round to its far one. Offsets are degrees from
	// the axis, so the angular distance from the axis at offset O is
	// min(O, 360 - O) and the ramp at both ends falls out of that one expression
	// rather than out of two mirrored loops.
	const float SweepStart = Half;
	const float SweepEnd = 360.f - Half;

	// Sample boundaries: fine across each ramp, coarse across the bulk between
	// them. Built into a fixed-size stack array — DrawHUD is a tick path, and the
	// count is bounded by construction (the bulk is at most 360/12 slices and each
	// ramp is at most EdgeSteps * VisionEdgeSamplesPerStep).
	constexpr int32 MaxSamples = 128;
	float Offsets[MaxSamples];
	int32 SampleCount = 0;
	const auto PushSample = [&Offsets, &SampleCount](float Offset)
	{
		// Strictly increasing: a duplicate boundary is a zero-area slice, which is
		// three wasted vertices and a degenerate triangle in the batch.
		if (SampleCount < MaxSamples &&
			(SampleCount == 0 || Offset > Offsets[SampleCount - 1] + KINDA_SMALL_NUMBER))
		{
			Offsets[SampleCount++] = Offset;
		}
	};

	const int32 EdgeSamples = SarkoVision::EdgeSteps * VisionEdgeSamplesPerStep;
	const float RampEnd = FMath::Min(SweepStart + Soft, SweepEnd);
	for (int32 Index = 0; Index <= EdgeSamples; ++Index)
	{
		PushSample(FMath::Lerp(SweepStart, RampEnd, static_cast<float>(Index) / EdgeSamples));
	}

	const float MirrorStart = FMath::Max(SweepEnd - Soft, RampEnd);
	for (float Offset = RampEnd + VisionBulkSliceDegrees; Offset < MirrorStart; Offset += VisionBulkSliceDegrees)
	{
		PushSample(Offset);
	}

	for (int32 Index = 0; Index <= EdgeSamples; ++Index)
	{
		PushSample(FMath::Lerp(MirrorStart, SweepEnd, static_cast<float>(Index) / EdgeSamples));
	}

	if (SampleCount < 2)
	{
		return;
	}

	if (!VisionFan.IsValid())
	{
		// Built ONCE, on the first frame there is a cone to draw. The one triangle
		// it insists on is overwritten immediately below; from here on the list is
		// Reset and refilled in place and nothing allocates.
		FCanvasUVTri Seed;
		Seed.V0_Pos = Seed.V1_Pos = Seed.V2_Pos = Apex;
		Seed.V0_UV = Seed.V1_UV = Seed.V2_UV = FVector2D::ZeroVector;
		Seed.V0_Color = Seed.V1_Color = Seed.V2_Color = FLinearColor::Transparent;
		VisionFan = MakeUnique<FCanvasTriangleItem>(Seed, GWhiteTexture);
		VisionFan->BlendMode = SE_BLEND_Translucent;
	}
	VisionFan->TriangleList.Reset();

	// THE RADIAL HALF OF THE ALPHA, per ring, from the same tested function the
	// header describes: nothing at the pawn's own feet, everything past the
	// halo's rim. Three rings, three scales, computed once for the whole fan
	// because they depend on the radius alone.
	const float ApexHalo = SarkoVision::NearHaloScale(0.f, Settings.VisionNearHaloUU);
	const float InnerHalo = SarkoVision::NearHaloScale(HaloPx / PixelsPerUU, Settings.VisionNearHaloUU);
	const float OuterHalo = SarkoVision::NearHaloScale(OuterPx / PixelsPerUU, Settings.VisionNearHaloUU);

	FVector2D PreviousInner = FVector2D::ZeroVector;
	FVector2D PreviousOuter = FVector2D::ZeroVector;
	float PreviousAlpha = 0.f;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Offset = Offsets[Index];
		const float Radians = CentreRadians + FMath::DegreesToRadians(Offset);
		const FVector2D Unit(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D Inner = Apex + Unit * HaloPx;
		const FVector2D Outer = Apex + Unit * OuterPx;

		// The angular distance from the AXIS, which is what the ramp is a function
		// of — the sweep runs to 360 - Half, so the far half of it has to be
		// folded back or the second edge would draw at full dim with no ramp.
		const float FromAxis = FMath::Min(Offset, 360.f - Offset);
		const float Alpha = SarkoVision::DimAlphaForAngle(FromAxis, Half, Soft, MaxAlpha);

		if (Index > 0)
		{
			FCanvasUVTri Tri;
			Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;

			// Each vertex's alpha is the ANGULAR ramp times the RADIAL halo, which
			// is what makes the wedge's apex soften into the ground under the pawn
			// instead of ending in a hard point at their feet.
			FLinearColor ApexColour = VisionDimColour;
			FLinearColor InnerA = VisionDimColour;
			FLinearColor InnerB = VisionDimColour;
			FLinearColor OuterA = VisionDimColour;
			FLinearColor OuterB = VisionDimColour;
			ApexColour.A = PreviousAlpha * ApexHalo;
			InnerA.A = PreviousAlpha * InnerHalo;
			InnerB.A = Alpha * InnerHalo;
			OuterA.A = PreviousAlpha * OuterHalo;
			OuterB.A = Alpha * OuterHalo;

			Tri.V0_Pos = Apex;
			Tri.V1_Pos = PreviousInner;
			Tri.V2_Pos = Inner;
			Tri.V0_Color = ApexColour;
			Tri.V1_Color = InnerA;
			Tri.V2_Color = InnerB;
			VisionFan->TriangleList.Add(Tri);

			// And the body of the slice, from the halo's rim out past the corner of
			// the screen. Two triangles, because a quad with two different alphas
			// down one pair of edges cannot be one.
			Tri.V0_Pos = PreviousInner;
			Tri.V1_Pos = PreviousOuter;
			Tri.V2_Pos = Outer;
			Tri.V0_Color = InnerA;
			Tri.V1_Color = OuterA;
			Tri.V2_Color = OuterB;
			VisionFan->TriangleList.Add(Tri);

			Tri.V0_Pos = PreviousInner;
			Tri.V1_Pos = Outer;
			Tri.V2_Pos = Inner;
			Tri.V0_Color = InnerA;
			Tri.V1_Color = OuterB;
			Tri.V2_Color = InnerB;
			VisionFan->TriangleList.Add(Tri);
		}

		PreviousInner = Inner;
		PreviousOuter = Outer;
		PreviousAlpha = Alpha;
	}

	if (VisionFan->TriangleList.Num() > 0)
	{
		Canvas->DrawItem(*VisionFan);
	}
}

void ASarkoHUD::DrawAimCone()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->IsAiming())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector Muzzle = Pawn->GetMuzzleLocation();
	const FVector Aim = FVector(Pawn->AimDirection);

	// Two edges of the cone, projected to screen. On a touchscreen there is no
	// cursor, so without this the player is shooting blind.
	const auto ProjectAndDraw = [this, &Muzzle](const FVector& End, const FLinearColor& Colour)
	{
		const FVector Start2D = Project(Muzzle);
		const FVector End2D = Project(End);
		DrawLine(Start2D.X, Start2D.Y, End2D.X, End2D.Y, Colour, Px(AimConeStrokePt));
	};

	const FVector Left = Muzzle + Aim.RotateAngleAxis(Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;
	const FVector Right = Muzzle + Aim.RotateAngleAxis(-Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;

	const FLinearColor Colour(1.f, 0.85f, 0.2f, 0.5f);
	ProjectAndDraw(Left, Colour);
	ProjectAndDraw(Right, Colour);

	// And, if the assist has someone, say so.
	DrawSnapTargetBracket(*Pawn, Muzzle, Aim);
}

void ASarkoHUD::DrawSnapTargetBracket(const ASarkoCharacter& Pawn, const FVector& Muzzle, const FVector& Aim)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// The SAME candidate rule USarkoWeaponComponent::ServerFire uses: living
	// FOES only. Restricted to foes there so a shot at the player cannot be
	// nudged onto a nearer enemy; restricted to foes here so the bracket names
	// the actor the shot would actually reach.
	const USarkoHealthComponent* OwnerHealth = Pawn.HealthComponent;
	const ESarkoTeam OwnerTeam = OwnerHealth ? OwnerHealth->GetTeam() : ESarkoTeam::Player;

	// Reset, not Empty: the capacity survives, so after the first aimed frame
	// this gathers without allocating.
	AimAssistCandidates.Reset();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Other = *It;
		if (!Other || Other == &Pawn)
		{
			continue;
		}
		// A HIDDEN ENEMY GETS NO BRACKET. The server decides who may be drawn
		// (ASarkoRaidGameMode::UpdateEnemyVisibility) and pushes it out as
		// bHidden; four corner ticks floating over an invisible body would hand
		// the player back exactly the position the cone exists to withhold, and
		// would do it in the one place the eye is already looking. The assist
		// itself is NOT changed — the server still re-runs its own selection at
		// fire time over every foe, so what a shot does is unaffected and only
		// what the player is TOLD about it narrows.
		if (Other->IsHidden())
		{
			continue;
		}
		if (const USarkoHealthComponent* Health = Other->FindComponentByClass<USarkoHealthComponent>())
		{
			if (!Health->IsDead() && SarkoCombat::IsFoe(OwnerTeam, Health->GetTeam()))
			{
				AimAssistCandidates.Add(Other->GetActorLocation());
			}
		}
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const int32 Best = SarkoCombat::BestAimAssistTarget(
		Muzzle, Aim, Settings.AimConeHalfAngleDegrees, AimAssistCandidates);
	if (!AimAssistCandidates.IsValidIndex(Best))
	{
		return;
	}

	FVector2D Screen;
	if (!ProjectToScreen(AimAssistCandidates[Best], Screen))
	{
		return;
	}

	// Four corners and no box: a closed rectangle over an enemy hides the enemy,
	// and the thing being communicated is "this one", not "look at this area".
	const float Half = Px(BracketSizePt) * 0.5f;
	const float Arm = Px(BracketArmPt);
	const float Stroke = Px(BracketStrokePt);
	const FLinearColor Colour(1.f, 0.85f, 0.2f, 0.85f);
	for (int32 SignX = -1; SignX <= 1; SignX += 2)
	{
		for (int32 SignY = -1; SignY <= 1; SignY += 2)
		{
			const float CornerX = Screen.X + Half * SignX;
			const float CornerY = Screen.Y + Half * SignY;
			DrawLine(CornerX, CornerY, CornerX - Arm * SignX, CornerY, Colour, Stroke);
			DrawLine(CornerX, CornerY, CornerX, CornerY - Arm * SignY, Colour, Stroke);
		}
	}
}

void ASarkoHUD::DrawFirstRaidHints(const ASarkoPlayerController& PC)
{
	const UWorld* World = GetWorld();
	const USarkoGameInstance* Instance = World ? World->GetGameInstance<USarkoGameInstance>() : nullptr;
	// A player who has finished a raid is not being taught the controls again.
	// The profile is the client's own cached copy — the raid fetches it before it
	// goes live, and in the standalone raid this game ships the client is the
	// server, so there is nothing to replicate and nothing to wait for.
	if (!Instance || Instance->CachedProfile.bTutorialCompleted)
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// The extraction banner owns this row and it is the more urgent message: a
	// countdown the player is standing in beats a hint about a control. Yielding
	// rather than choosing another row, because every row further down covers the
	// player's own character in a top-down frame.
	if (Pawn->ExtractZoneIndex != INDEX_NONE)
	{
		return;
	}

	// Whether each hint still has anything to teach. Two are due from the first
	// frame — they are the scheme itself — and two wait for the beat where the
	// verb is first needed, which is the rule every game that teaches well
	// follows: at the empty magazine, and at the crate.
	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const bool bMagazineEmpty = Weapon && !Weapon->IsReloading() && Weapon->GetAmmoInMagazine() <= 0;

	bool bDue[SarkoHint::Count] = { false, false, false, false };
	bDue[SarkoHint::Fire] = !PC.HasEverFired();
	bDue[SarkoHint::Move] = !PC.HasEverMoved();
	bDue[SarkoHint::Reload] = !PC.HasEverReloaded() && bMagazineEmpty;
	bDue[SarkoHint::Interact] = !PC.HasEverLooted() && PC.GetInteractTarget() != nullptr;

	const float Now = World->GetTimeSeconds();

	// One line at a time, in priority order, and each keeps its own clock: a hint
	// that has already had its six seconds stays down even while it is still due,
	// so the next one gets the band instead of queueing behind a permanent one.
	int32 Chosen = INDEX_NONE;
	for (int32 Index = 0; Index < SarkoHint::Count; ++Index)
	{
		if (!bDue[Index])
		{
			continue;
		}
		if (HintFirstShownSeconds[Index] < 0.f)
		{
			HintFirstShownSeconds[Index] = Now;
		}
		if (Now - HintFirstShownSeconds[Index] < HintLifetimeSeconds)
		{
			Chosen = Index;
			break;
		}
	}

	if (Chosen == INDEX_NONE)
	{
		return;
	}

	const float Age = Now - HintFirstShownSeconds[Chosen];
	const float Alpha = FMath::Clamp((HintLifetimeSeconds - Age) / FMath::Max(KINDA_SMALL_NUMBER, HintFadeSeconds), 0.f, 1.f);

	const FString& Text = SarkoHint::Text[Chosen];
	if (CachedHintSize[Chosen].X < 0.f)
	{
		CachedHintSize[Chosen] = MeasurePt(Text, HintPt);
	}
	const FVector2D Size = CachedHintSize[Chosen];

	// Top-centre, below every other top row, on the same dark plate every readout
	// on this HUD sits on — the world underneath is arbitrary and white-on-white
	// is the one failure no size fixes.
	const float X = Safe.GetCenter().X - Size.X * 0.5f;
	const float Y = Safe.Min.Y + Px(HintTopPt);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha),
		X - Px(PlatePadXPt), Y - Px(PlatePadYPt),
		Size.X + Px(PlatePadXPt * 2.f), Size.Y + Px(PlatePadYPt * 2.f));
	DrawTextPt(Text, FLinearColor(1.f, 0.92f, 0.6f, Alpha), X, Y, HintPt);
}

void ASarkoHUD::DrawTopBar()
{
	// Top only: the lower corners are dead zones under the player's thumbs.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState)
	{
		return;
	}

	// Rebuilt on the second, not on the frame: the string only ever changes when
	// the integer second does, and DrawHUD is a tick path where a Printf and a
	// GetTextSize per frame is 59 of every 60 rebuilds discarded.
	const int32 Total = FMath::CeilToInt(RaidState->RemainingSeconds);
	if (Total != CachedClockSeconds)
	{
		CachedClockSeconds = Total;
		CachedClock = FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
		CachedClockWidth = MeasurePt(CachedClock, ClockPt).X;
	}

	DrawTextPt(CachedClock, FLinearColor::White,
		Safe.GetCenter().X - CachedClockWidth * 0.5f, Safe.Min.Y + Px(TopRowPt), ClockPt);

	// The player must be able to tell "the raid has not started yet" from "the
	// crates are broken". Spec §4.6's loud degradation is a log line for the
	// developer; this is the same fact for the player. Rebuilt per frame rather
	// than cached, unlike the clock: it is on screen for one HTTP round trip, so
	// there is no steady state worth optimising.
	if (!RaidState->bSessionReady)
	{
		static const FString Connecting(TEXT("З'ЄДНАННЯ..."));
		const float Width = MeasurePt(Connecting, ConnectingPt).X;
		DrawTextPt(Connecting, FLinearColor(1.f, 0.75f, 0.2f),
			Safe.GetCenter().X - Width * 0.5f, Safe.Min.Y + Px(ConnectingTopPt), ConnectingPt);
	}
}

void ASarkoHUD::DrawHealth()
{
	// Health was missing entirely, and its absence produced the worst kind of
	// bug report: "the character walks, then stops walking when enemies show
	// up". The character had died — death disables movement — and nothing on
	// screen said so. A player must always be able to see that they are dying,
	// and that they are dead.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->HealthComponent)
	{
		return;
	}

	const USarkoHealthComponent* Health = Pawn->HealthComponent;
	const float Fraction = Health->GetMaxHealth() > 0.f
		? FMath::Clamp(Health->GetHealth() / Health->GetMaxHealth(), 0.f, 1.f)
		: 0.f;

	const float BarWidth = Px(HealthBarWidthPt);
	const float BarHeight = Px(HealthBarHeightPt);
	// Measured from the safe frame's right edge, not the canvas': in landscape
	// that edge is 59 pt in from the glass on a notched phone.
	const float BarX = Safe.Max.X - BarWidth - Px(SideInsetPt);
	const float BarY = Safe.Min.Y + Px(HealthBarTopPt);
	const float Border = Px(2.f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
		BarX - Border, BarY - Border, BarWidth + Border * 2.f, BarHeight + Border * 2.f);

	// Green through red, so falling health is readable without reading a number.
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.85f, 0.15f, 0.1f), FLinearColor(0.3f, 0.85f, 0.25f), Fraction);
	DrawRect(Fill, BarX, BarY, BarWidth * Fraction, BarHeight);

	DrawSurvival(BarX, BarY + BarHeight, BarWidth);

	if (!Health->IsDead())
	{
		return;
	}

	// Once the raid has an outcome, DrawOutcomeSummary says KIA in the same place
	// and says it better. Two centred death banners stacked on each other is
	// worse than either alone.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		return;
	}

	// Unmissable, centred: this is the state the tester could not previously see.
	static const FString DeadText(TEXT("YOU DIED"));
	const float TextWidth = MeasurePt(DeadText, DiedPt).X;
	DrawTextPt(DeadText, FLinearColor(1.f, 0.2f, 0.15f),
		Safe.GetCenter().X - TextWidth * 0.5f, Safe.Min.Y + Safe.GetSize().Y * 0.42f, DiedPt);
}

void ASarkoHUD::DrawAmmo()
{
	// Top-left, alongside the clock: still along the top per spec §9, clear of
	// both bottom corners so the sticks never cover it. Reloading is drawn as
	// distinct text rather than a number so the tester can tell "reloading"
	// from "empty and stuck" at a glance — the whole point of this readout.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		return;
	}

	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const bool bReloading = Weapon->IsReloading();

	// THE RESERVE (spec §1). Read straight off the pawn's own grid rather than
	// replicated separately: Slots is already COND_OwnerOnly, so the owning client
	// has the stacks this sums and nobody else has either. A loop over at most
	// thirteen stacks, allocating nothing — GetSlots hands back a const reference.
	const int32 Reserve = Pawn->BackpackComponent
		? Pawn->BackpackComponent->CountItem(SarkoLoot::AmmoItemId)
		: 0;

	// Rebuilt when the readout changes, which is on a shot, a reload or a pickup
	// rather than on a frame. FromInt allocates, and so does turning the
	// TEXT("RELOADING") literal into the FString DrawText takes — both were paid
	// every frame for a string with two digits' worth of variation.
	//
	// BOTH halves are the key now. Keying on the magazine alone would have frozen
	// the reserve figure on screen: picking ammo up out of a crate does not change
	// the magazine, so the readout would have gone on claiming the old reserve
	// until the next shot — a stale number is worse than no number.
	const int32 AmmoKey = bReloading ? INDEX_NONE : Weapon->GetAmmoInMagazine();
	if (AmmoKey != CachedAmmoKey || Reserve != CachedReserveKey)
	{
		CachedAmmoKey = AmmoKey;
		CachedReserveKey = Reserve;
		// "8 | 24": what is in the gun, then what is left to put in it. Spaced,
		// unlike the reload button's compact pairing, because there is room along
		// the top row and this is the readout the player reads deliberately.
		CachedAmmoText = bReloading
			? FString(TEXT("RELOADING"))
			: FString::Printf(TEXT("%d | %d"), AmmoKey, Reserve);
	}

	const FLinearColor Colour = bReloading ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;

	DrawTextPt(CachedAmmoText, Colour, Safe.Min.X + Px(SideInsetPt), Safe.Min.Y + Px(TopRowPt), AmmoPt);
}

void ASarkoHUD::DrawBackpack()
{
	// Top-left, immediately right of the ammo readout: spec §9 puts every
	// number along the top, and the bottom corners belong to the thumbs.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->BackpackComponent)
	{
		return;
	}

	const USarkoBackpackComponent* Backpack = Pawn->BackpackComponent;
	// Cells, not stacks: with sizes a toolbox costs two, and a readout that said
	// 1/12 for half a full pocket row would be lying about the only number the
	// player uses to decide whether to keep looting.
	const int32 Used = Backpack->GetUsedCells();
	const int32 Limit = Backpack->GetCellCount();

	// Rebuilt when a slot is taken or the limit changes — a few times a raid —
	// rather than every frame. Both halves are the key because the limit comes from
	// a setting, not a constant.
	if (Used != CachedBackpackUsed || Limit != CachedBackpackLimit)
	{
		CachedBackpackUsed = Used;
		CachedBackpackLimit = Limit;
		CachedBackpackText = FString::Printf(TEXT("%d/%d"), Used, Limit);
	}

	// The x offset is measured from the widest string DrawAmmo can produce
	// rather than guessed: "RELOADING" in the large font is far wider than the
	// two digits of a magazine count, and a fixed offset that clears "30"
	// overlaps it the moment the player reloads.
	//
	// Measured once per scale and kept. DrawHUD is a tick path, and measuring
	// inline allocated and freed a string every frame to re-derive a constant.
	if (CachedReloadingWidth < 0.f)
	{
		static const FString Reloading(TEXT("RELOADING"));
		CachedReloadingWidth = MeasurePt(Reloading, AmmoPt).X;
	}
	const float X = Safe.Min.X + Px(SideInsetPt) + CachedReloadingWidth + Px(SideInsetPt);

	// Amber when full, so "the crate had more in it" is legible at a glance
	// rather than being discovered by counting.
	const FLinearColor Colour = Used >= Limit ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;
	// Baseline-ish alignment with the taller ammo readout beside it: sitting both
	// at the same top edge would leave the smaller number floating.
	DrawTextPt(CachedBackpackText, Colour, X, Safe.Min.Y + Px(TopRowPt + (AmmoPt - BackpackPt) * 0.75f), BackpackPt);
}

void ASarkoHUD::DrawInteract()
{
	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// The button is always drawn, so the player learns where it is before they
	// need it; it dims when there is nothing in reach.
	//
	// The rect comes from SarkoInput::InteractButtonRect and is *not* scaled here:
	// it is already derived from the safe frame. Scaling it again would make the
	// drawn button a different rectangle from the one
	// ASarkoPlayerController::UpdateSticks hit-tests, which is the one thing about
	// this button that must never happen — the owner experiences that as "the
	// button doesn't work".
	//
	// It never shifts. There used to be a second rect, beside an open container
	// panel, and a shared function to choose between them; the panel moved to the
	// left half (spec §4.5), so the thing the shift was avoiding is not there any
	// more and both the shifted rect and the chooser are deleted. This note is
	// deliberately not deleted with them: a stale comment about the branch is how
	// the branch comes back.
	const ASarkoCharacter* OwningPawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const FBox2D Rect = SarkoInput::InteractButtonRect(Safe, PointScale);

	// The glyph and the word say what the button will DO (spec §4.4). A generic
	// picture in a game with two verbs is a guess, and the button is always drawn
	// — dim and BLANK when there is nothing in reach — so the player learns where
	// it is before they need it and the empty state is honest about there being
	// nothing to do.
	const ASarkoLootContainer* Target = PC->GetInteractTarget();
	const bool bPanelOpen = OwningPawn && OwningPawn->GetOpenContainerIndex() != INDEX_NONE;
	SarkoUI::EInteractAction Action = bPanelOpen
		? SarkoUI::EInteractAction::Close
		: (Target ? SarkoUI::EInteractAction::Search : SarkoUI::EInteractAction::None);

#if !UE_BUILD_SHIPPING
	// The photographic seam, and nothing else: SarkoDebugInteract forces the state
	// a -RenderOffscreen run cannot reach with no fingers. Extract in particular is
	// unreachable by playing, because it is deliberately NOT wired (see
	// SarkoUI::EInteractAction — an extraction is a dwell, not a press), and its
	// glyph still has to be looked at by someone before it ships.
	const int32 Forced = PC->GetDebugInteractAction();
	if (Forced >= 0 && Forced <= static_cast<int32>(SarkoUI::EInteractAction::Extract))
	{
		Action = static_cast<SarkoUI::EInteractAction>(Forced);
	}
#endif

	// ROUND, and the same circle as the reload button: the rect is the square the
	// circle is inscribed in, so the four corners are slop in the player's favour
	// and every clearance in the cluster is measured on the square.
	const FVector2D Centre = Rect.GetCenter();
	const float Radius = Rect.GetSize().X * 0.5f;

	// A dark plate under the tint, exactly as every readout on this HUD has one:
	// the button is drawn over an arbitrary world, and a 0.15-alpha white plate on
	// pale ground is a control the player cannot find. See TextShadow's comment —
	// white-on-white is the one failure that no size fixes.
	DrawDisc(Centre, Radius, ThumbButtonPlate);
	const bool bLive = Action != SarkoUI::EInteractAction::None;
	// Amber when it means something, which is the loot family's colour — the same
	// 0.95/0.8/0.25 the prompt's channel bar is drawn in, so the button, the prompt
	// and the progress that joins them are one colour and one action.
	const FLinearColor Fill = bLive
		? FLinearColor(0.95f, 0.8f, 0.25f, 0.55f)
		: FLinearColor(1.f, 1.f, 1.f, 0.15f);
	// The idle rim is DIM, and the first frames of this are why: at full white it
	// came out as bright as the move stick's home ring and much the same size —
	// two white circles along the bottom of the screen, one a control and one a
	// place to put a thumb, told apart by nothing. This project has already paid
	// once for a player reading a button as a stick.
	//
	// Dimmed in the RGB and NOT in the alpha, which is the thing worth knowing
	// here: AHUD::DrawLine's alpha does not survive to the glass — a 0.22-alpha
	// white stroke measures 255,255,255 in the frame — so every stroke on this HUD
	// is opaque whatever its colour says, and the only lever a drawn line has is
	// its brightness. 0.26 linear is a mid grey on the dark plate: visibly a
	// reserved slot, no longer competing with a stick home.
	const FLinearColor Ink = bLive ? FLinearColor::White : FLinearColor(0.26f, 0.26f, 0.25f, 1.f);
	DrawDisc(Centre, Radius, Fill);

	// A DRAWN RIM, which the reload button does not have. Two circles the same size
	// under one thumb need something that separates them before either picture is
	// read, and the reload button's own glyph is a broken ring out near ITS rim —
	// so a solid outline here and an interrupted one there is a difference visible
	// at a glance and at arm's length. Inset by a pixel so the stroke lands on the
	// disc rather than half off it.
	DrawRing(Centre, Radius - FMath::Max(1.f, Px(1.f)), Ink);

	DrawInteractIcon(Centre, Action, Ink);

	const FString Label = SarkoUI::InteractLabelFor(Action);
	if (!Label.IsEmpty())
	{
		// Keyed on the STRING, not on the action: two actions can share a width
		// and none can share a string. Measured on a change rather than per frame,
		// because DrawHUD is a tick path.
		if (Label != CachedInteractLabel || CachedInteractLabelWidth < 0.f)
		{
			CachedInteractLabel = Label;
			const FVector2D LabelSize = MeasurePt(Label, InteractLabelPt);
			CachedInteractLabelWidth = LabelSize.X;
			CachedInteractLabelHeight = LabelSize.Y;
		}

		// OUTSIDE the button, inward of it, right-aligned to its rim and centred on
		// its row — see InteractLabelGapPt for why the side and not underneath. The
		// word is a caption and not a target: the tap rect is the square above and
		// does not grow to cover the text, so a longer verb can never quietly widen
		// the control.
		const float PlateRight = Rect.Min.X - Px(InteractLabelGapPt);
		const float TextRight = PlateRight - Px(PlatePadXPt);
		const float TextX = TextRight - CachedInteractLabelWidth;
		const float TextY = Centre.Y - CachedInteractLabelHeight * 0.5f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
			TextX - Px(PlatePadXPt), TextY - Px(PlatePadYPt),
			CachedInteractLabelWidth + Px(PlatePadXPt * 2.f),
			CachedInteractLabelHeight + Px(PlatePadYPt * 2.f));
		DrawTextPt(CachedInteractLabel, FLinearColor::White, TextX, TextY, InteractLabelPt);
	}

	// A container panel is up: this control is the CLOSE button now, and the
	// prompt and the channel bar are skipped outright — neither means anything
	// while a panel is open, and the prompt would sit under the clock saying
	// "search" about a crate the player is already inside.
	if (bPanelOpen)
	{
		return;
	}

	if (!Target)
	{
		return;
	}

	// Prompt: top-centre, under the clock. Never a bottom corner (spec §9).
	//
	// Built and measured only when the tier changes, which is the only thing the
	// text depends on. Doing it inline cost a Printf, an FName::ToString and a
	// GetTextSize every frame the player stood near a crate.
	if (!bPromptCached || Target->Tier != CachedPromptTier)
	{
		bPromptCached = true;
		CachedPromptTier = Target->Tier;
		CachedPrompt = FString::Printf(TEXT("ОБШУКАТИ (%s)"), *CachedPromptTier.ToString());
		const FVector2D PromptSize = MeasurePt(CachedPrompt, PromptPt);
		CachedPromptWidth = PromptSize.X;
		CachedPromptHeight = PromptSize.Y;
	}

	const float PromptWidth = CachedPromptWidth;
	const float PromptHeight = CachedPromptHeight;
	const float PromptX = Safe.GetCenter().X - PromptWidth * 0.5f;
	const float PromptY = Safe.Min.Y + Px(PromptTopPt);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
		PromptX - Px(PlatePadXPt), PromptY - Px(PlatePadYPt),
		PromptWidth + Px(PlatePadXPt * 2.f), PromptHeight + Px(PlatePadYPt * 2.f));
	DrawTextPt(CachedPrompt, FLinearColor::White, PromptX, PromptY, PromptPt);

	if (!PC->IsInteractHeld())
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// Progress: local and cosmetic. The server owns whether the channel
	// completes; this bar only has to stop the player wondering whether the
	// hold is doing anything.
	const float Duration = FMath::Max(0.01f, GetDefault<USarkoRaidSettings>()->LootChannelSeconds);
	const float Fraction = FMath::Clamp(Pawn->GetLootChannelElapsed() / Duration, 0.f, 1.f);

	const float BarWidth = Px(LootBarWidthPt);
	const float BarHeight = Px(LootBarHeightPt);
	const float BarX = Safe.GetCenter().X - BarWidth * 0.5f;
	const float BarY = PromptY + PromptHeight + Px(LootBarGapPt);
	const float Border = Px(2.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f),
		BarX - Border, BarY - Border, BarWidth + Border * 2.f, BarHeight + Border * 2.f);
	DrawRect(FLinearColor(0.95f, 0.8f, 0.25f, 0.9f), BarX, BarY, BarWidth * Fraction, BarHeight);
}

void ASarkoHUD::DrawReload()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		return;
	}

	// Always drawn, in a rect that takes no game state: the interact button
	// appearing above it cannot shift it, because both are computed from the safe
	// frame alone. A control that moves is a control you mis-press (spec §5).
	const FBox2D Rect = SarkoInput::ReloadButtonRect(Safe, PointScale);

	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const int32 Magazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
	// The same grid sum DrawAmmo takes, and for the same reason it is not
	// replicated: it is the owning client's own bag.
	const int32 Reserve = Pawn->BackpackComponent
		? Pawn->BackpackComponent->CountItem(SarkoLoot::AmmoItemId)
		: 0;
	const SarkoUI::ESarkoReloadState State =
		SarkoUI::ReloadStateFor(Weapon->GetAmmoInMagazine(), Magazine, Weapon->IsReloading(), Reserve);

	// Spec §4.3: "the magazine count lives on it, it goes amber below a third, and
	// it pulses when empty. The player should never have to look at two places to
	// know they need to reload." The scarcity stage makes the reserve part of that
	// sentence — knowing you need to reload is worth nothing if the bag is empty —
	// so the label is the PAIR, compactly: "3|43", no spaces, because this is a
	// 64 pt round thumb button and the top-left readout is where the spaced version
	// lives — at 26 pt, which is the readout a player actually glances at mid-fight.
	// This one is the answer to "is pressing this worth anything", read while
	// deciding to press it, and it is sized to fit inside the glyph's arc at its
	// five-character worst case ("8|720", a bag of nothing but ammo).
	//
	// Built ONLY when one of the two numbers moves, not every frame. The pair made
	// this matter: the old label was one FString::FromInt per frame, which was
	// already an allocation on a tick path, and a Printf of two numbers is not the
	// place to double it. The key is both halves plus the reloading flag, exactly
	// as DrawAmmo's is, so a pickup that changes only the reserve still rebuilds.
	const int32 LabelAmmoKey = Weapon->IsReloading() ? INDEX_NONE : Weapon->GetAmmoInMagazine();
	if (LabelAmmoKey != CachedReloadAmmoKey || Reserve != CachedReloadReserveKey)
	{
		CachedReloadAmmoKey = LabelAmmoKey;
		CachedReloadReserveKey = Reserve;
		CachedReloadLabel = Weapon->IsReloading()
			? FString(TEXT("…"))
			: FString::Printf(TEXT("%d|%d"), LabelAmmoKey, Reserve);
		const FVector2D Size = MeasurePt(CachedReloadLabel, ReloadLabelPt);
		CachedReloadLabelWidth = Size.X;
		CachedReloadLabelHeight = Size.Y;
	}

	FLinearColor Fill;
	FLinearColor Ink;
	switch (State)
	{
	case SarkoUI::ESarkoReloadState::Low:
		Fill = FLinearColor(0.95f, 0.55f, 0.06f, 0.35f);
		Ink = FLinearColor(1.f, 0.6f, 0.1f, 1.f);
		break;
	case SarkoUI::ESarkoReloadState::Empty:
		// The pulse is the one animated thing on this HUD, and it is bounded away
		// from zero: a button that vanishes on the trough reads as absent, not as
		// urgent. It survives the scarcity stage unchanged — but it now means
		// exactly one thing, "press me, there are rounds in the bag", because Dry
		// below took the other meaning away from it.
		Fill = FLinearColor(0.95f, 0.55f, 0.06f,
			SarkoUI::ReloadPulseAlpha(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f));
		Ink = FLinearColor(1.f, 0.6f, 0.1f, 1.f);
		break;
	case SarkoUI::ESarkoReloadState::Dry:
		// STATIC RED, and deliberately not the pulse. Empty magazine, empty bag:
		// the press does nothing, so the button stops asking for one and reports a
		// state instead. Red because this is the fact that changes the raid — the
		// player's remaining verbs are avoid, loot and leave — and static because
		// an animation here would be the HUD nagging for an action that does not
		// exist.
		Fill = FLinearColor(0.8f, 0.12f, 0.10f, 0.40f);
		Ink = FLinearColor(1.f, 0.35f, 0.3f, 1.f);
		break;
	case SarkoUI::ESarkoReloadState::Reloading:
		Fill = FLinearColor(1.f, 1.f, 1.f, 0.10f);
		Ink = FLinearColor(0.7f, 0.7f, 0.7f, 1.f);
		break;
	default:
		Fill = FLinearColor(1.f, 1.f, 1.f, 0.15f);
		Ink = FLinearColor::White;
		break;
	}

	// ROUND. The rect is the square the circle is inscribed in — the hit target
	// stays the square (SarkoInput::ReloadButtonRect and UpdateSticks both use it),
	// so the four corners are slop in the player's favour, and every gap in the
	// thumb column is measured on the square rather than on the circle so that slop
	// can never reach a neighbouring control.
	//
	// A circle and not a rounded rectangle because the interact button 12 pt above
	// it is a rectangle: two controls under one thumb, told apart by shape before
	// either is read. That matters most in the moment the button exists for — a
	// magazine running out while something is shooting at you.
	const FVector2D Centre = Rect.GetCenter();
	const float Radius = Rect.GetSize().X * 0.5f;

	// Dark plate first, state tint over it: see DrawInteract for why.
	DrawDisc(Centre, Radius, ThumbButtonPlate);
	DrawDisc(Centre, Radius, Fill);

	// The glyph on the rim, in the state's own ink, so a dry button is a red icon
	// as well as a red number. Drawn and not imported: this project authors no
	// binary assets, so there is no texture and no icon font to reach for.
	DrawReloadIcon(Centre, Ink);

	// THE NUMBERS STAY IN THE MIDDLE. Spec §4.3 puts the magazine count on this
	// button so the player never has to look at two places to know they need to
	// reload, and the middle of a round button is where the eye goes — so the icon
	// went out to the rim rather than the pair moving off centre to share the
	// middle with it. A round button has one place a number belongs.
	//
	// Both the string and its measurement were built above, on the frame one of the
	// two numbers moved. DrawHUD is a tick path and an uncached MeasurePt per frame
	// is the allocation this HUD spent two commits removing.
	DrawTextPt(CachedReloadLabel, Ink,
		Centre.X - CachedReloadLabelWidth * 0.5f,
		Centre.Y - CachedReloadLabelHeight * 0.5f,
		ReloadLabelPt);
}

const FString& ASarkoHUD::ZoneNameFor(int32 ZoneIndex)
{
	// Fallback, not a label: a generic word here means the map file and the
	// server's zone list have drifted, which should be visible rather than
	// crash the HUD.
	static const FString Generic(TEXT("ЕВАКУАЦІЯ"));

	if (!bZoneNamesCached)
	{
		// Once per HUD, not once per frame. DrawHUD is a tick path.
		bZoneNamesCached = true;
		FSarkoMapDefinition Definition;
		FString Error;
		if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId, Definition, Error))
		{
			CachedZoneNames.Reserve(Definition.Extractions.Num());
			CachedZoneOpensAfter.Reserve(Definition.Extractions.Num());
			for (const FSarkoExtractionSpot& Spot : Definition.Extractions)
			{
				CachedZoneNames.Add(Spot.Name.IsEmpty() ? Generic : Spot.Name);
				CachedZoneOpensAfter.Add(Spot.OpensAfterSeconds);
			}
			// The same fallback the game mode uses (MapClockSeconds), so the two
			// cannot disagree about what "ten minutes in" means.
			CachedRaidDuration = Definition.RaidDurationSeconds > 0.f
				? Definition.RaidDurationSeconds
				: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoHUD: extraction zone names unavailable: %s"), *Error);
		}
	}

	return CachedZoneNames.IsValidIndex(ZoneIndex) ? CachedZoneNames[ZoneIndex] : Generic;
}

void ASarkoHUD::DrawSurvival(float BarX, float HealthBarBottomY, float BarWidth)
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const USarkoSurvivalComponent* Survival = Pawn ? Pawn->SurvivalComponent : nullptr;
	if (!Survival)
	{
		return;
	}

	const float Height = Px(SurvivalBarHeightPt);
	const float Gap = Px(SurvivalBarGapPt);
	const float Border = Px(2.f);
	const float Pulse = SarkoUI::ReloadPulseAlpha(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);

	// Two rows under the health bar, in the order they were introduced in and in
	// the order they run out in: food first because it is the slower one, so the
	// bar that moves is always the bottom one.
	const struct { float Fraction; bool bLow; FLinearColor Colour; } Rows[] = {
		{ Survival->GetFoodPercent() / SarkoSurvival::MeterMax, Survival->IsFoodLow(), FoodBarColour },
		{ Survival->GetWaterPercent() / SarkoSurvival::MeterMax, Survival->IsWaterLow(), WaterBarColour },
	};

	float Y = HealthBarBottomY + Gap;
	for (const auto& Row : Rows)
	{
		// The same dark plate the health bar sits on, so the bar's own colour is
		// never asked to fight the olive ground directly.
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
			BarX - Border, Y - Border, BarWidth + Border * 2.f, Height + Border * 2.f);

		FLinearColor Fill = Row.Colour;
		if (Row.bLow)
		{
			// Pulsed rather than recoloured: red would say "you are dying", and
			// hunger and thirst are never lethal in this slice. Bounded away from
			// zero for the reason the reload button's pulse is — a bar that
			// vanishes on the trough reads as absent, not as urgent.
			Fill.A = FMath::Max(SurvivalLowMinAlpha, Pulse);
		}
		DrawRect(Fill, BarX, Y, BarWidth * FMath::Clamp(Row.Fraction, 0.f, 1.f), Height);
		Y += Height + Gap;
	}
}

void ASarkoHUD::DrawExtraction()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || Pawn->ExtractZoneIndex == INDEX_NONE)
	{
		return;
	}

	// Names the zone first, which also fills the caches the closed check below
	// reads. One disk read per HUD, never per frame.
	const FString& Name = ZoneNameFor(Pawn->ExtractZoneIndex);

	// A zone that has not opened yet is INERT: no dwell is accruing on the server
	// (ASarkoRaidGameMode::ExtractTick gates it), so drawing a countdown that
	// never moves would be the HUD lying about the one thing it exists to say.
	// It says how long the wait is instead. The clock is the same one the server
	// measures against, re-derived here from the map file and the replicated
	// RemainingSeconds — presentation, never authority.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	const float OpensAfter = CachedZoneOpensAfter.IsValidIndex(Pawn->ExtractZoneIndex)
		? CachedZoneOpensAfter[Pawn->ExtractZoneIndex] : 0.f;
	const float Elapsed = RaidState ? FMath::Max(0.f, CachedRaidDuration - RaidState->RemainingSeconds) : 0.f;

	FString Text;
	FLinearColor Plate(0.f, 0.25f, 0.05f, 0.55f);
	FLinearColor Ink(0.55f, 1.f, 0.6f);
	if (!SarkoExtract::IsZoneOpen(OpensAfter, Elapsed))
	{
		const int32 Wait = FMath::CeilToInt(SarkoExtract::SecondsUntilOpen(OpensAfter, Elapsed));
		Text = FString::Printf(TEXT("%s — ЗАЧИНЕНО ЩЕ %d:%02d"), *Name, Wait / 60, Wait % 60);
		// Grey rather than green, because green is the colour of a dwell that is
		// running and nothing is running here.
		Plate = FLinearColor(0.f, 0.f, 0.f, 0.55f);
		Ink = FLinearColor(0.72f, 0.72f, 0.70f);
	}
	else
	{
		const float Required = FMath::Max(0.1f, GetDefault<USarkoRaidSettings>()->ExtractDwellSeconds);
		const float Left = FMath::Max(0.f, Required - Pawn->ExtractDwellSeconds);
		Text = FString::Printf(TEXT("%s — %.1f"), *Name, Left);
	}

	// Top-centre, below the clock and the loot prompt's slot: everything
	// informational lives along the top (spec §9), and never a bottom corner.
	const FVector2D Size = MeasurePt(Text, ExtractPt);
	const float X = Safe.GetCenter().X - Size.X * 0.5f;
	const float Y = Safe.Min.Y + Px(ExtractTopPt);
	DrawRect(Plate,
		X - Px(PlatePadXPt * 1.4f), Y - Px(PlatePadYPt * 1.5f),
		Size.X + Px(PlatePadXPt * 2.8f), Size.Y + Px(PlatePadYPt * 3.f));
	DrawTextPt(Text, Ink, X, Y, ExtractPt);
}

void ASarkoHUD::DrawOutcomeSummary()
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsRaidFinished())
	{
		return;
	}

	FString Title;
	FLinearColor Colour;
	switch (RaidState->Outcome)
	{
	case ESarkoRaidOutcome::Extracted: Title = TEXT("EXTRACTED"); Colour = FLinearColor(0.4f, 1.f, 0.45f); break;
	case ESarkoRaidOutcome::MIA:       Title = TEXT("MIA");       Colour = FLinearColor(1.f, 0.65f, 0.2f);  break;
	default:                           Title = TEXT("KIA");       Colour = FLinearColor(1.f, 0.25f, 0.2f);  break;
	}

	// Dim the world so the summary is unmistakably a final screen rather than
	// another HUD element. The whole canvas, deliberately: the dim is the only
	// thing here that should reach under a notch, because a lit strip along the
	// edge of a dimmed screen reads as a rendering fault.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const FVector2D TitleSize = MeasurePt(Title, OutcomeTitlePt);
	float Y = Safe.Min.Y + Safe.GetSize().Y * 0.22f;
	DrawTextPt(Title, Colour, Safe.GetCenter().X - TitleSize.X * 0.5f, Y, OutcomeTitlePt);
	Y += TitleSize.Y + Px(16.f);

	// The itemised haul used to be drawn here and now lives in the shelter (spec
	// §6.5: "вынесено: …" moves there, where it sits above the stash it was just
	// credited into and can be compared with it). What stays is the outcome itself,
	// in the place the raid ended, plus a line saying where the player is about to
	// go — without it, PostRaidReturnSeconds of a dimmed frozen world reads as a
	// hang rather than as a beat.
	//
	// Keyed on bReturningToShelter, not on the outcome: ASarkoRaidGameMode::
	// ReturnToShelter returns without scheduling anything when there is no
	// USarkoGameInstance, and then the dimmed world really is where the player
	// stays. Promising a return that is not coming is the one thing this line must
	// not do, so the stuck case says so instead — the Error log explains it, and
	// this tells the player to go look.
	const FString Returning = RaidState->bReturningToShelter
		? TEXT("ПОВЕРНЕННЯ ДО УКРИТТЯ...")
		: TEXT("ПОВЕРНЕННЯ НЕДОСТУПНЕ — ДИВ. ЛОГ");
	const float Width = MeasurePt(Returning, ReturningPt).X;
	const FLinearColor ReturningColour = RaidState->bReturningToShelter
		? FLinearColor(0.8f, 0.8f, 0.8f)
		: FLinearColor(1.f, 0.65f, 0.2f);
	DrawTextPt(Returning, ReturningColour, Safe.GetCenter().X - Width * 0.5f, Y, ReturningPt);
}
