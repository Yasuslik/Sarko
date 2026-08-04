#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SarkoPlayerController.generated.h"

namespace SarkoInput
{
	/** Left half drives movement, right half drives aim. Boundary is inclusive. */
	bool IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize);

	/**
	 * The part of the viewport iOS does not cover with a notch, a Dynamic Island
	 * or the home indicator, in viewport pixels.
	 *
	 * Landscape is the whole reason this exists. In portrait the unsafe strips run
	 * along the top and the bottom, and nothing this HUD anchors to a *side* edge
	 * is affected. Rotated, the island moves to the leading edge and iOS reports
	 * the inset on **both** sides — 59 pt each on a 14 Pro, so that turning the
	 * phone the other way up does not reflow the layout — plus 21 pt of home
	 * indicator along the bottom. Every element this HUD pins 24 px in from a side
	 * (the ammo count, the backpack count, the health bar, the interact button)
	 * lands inside that strip once the game is landscape.
	 *
	 * The numbers come from where UCanvas::SafeZonePad* gets them —
	 * FSlateApplicationBase::GetSafeZoneSize, which iOS fills from the window's
	 * own safeAreaInsets, already multiplied by the content scale, i.e. in the
	 * same pixels as the viewport. So this is the device's answer and not a
	 * guessed constant, and it stays right on a device this code has never seen.
	 *
	 * On Mac, and in any headless run where Slate is not initialised, the insets
	 * are zero and this returns the whole viewport — which is why nothing about
	 * the desktop layout or the existing tests changes.
	 */
	FBox2D SafeFrame(FVector2D ViewportSize);

	/**
	 * The thumb column, in points on the 844x390 landscape canvas.
	 *
	 * Sized in POINTS and not as a fraction of the frame, which is what the
	 * interact rect used to be: a fraction is unfalsifiable against a rule written
	 * in points (">= 44 pt"), and the old max(96 px, shorter axis * 0.14) gave 52
	 * pt on a phone and 32 pt in a small window while looking like one number.
	 */
	constexpr float ThumbColumnRightInsetPt = 16.f;
	constexpr float ReloadButtonSizePt = 56.f;
	constexpr float InteractButtonWidthPt = 96.f;
	constexpr float InteractButtonHeightPt = 48.f;

	/** The reload button's bottom edge, above the safe frame's. 96 pt is the room
	 *  a resting aim thumb and its ~45 pt of stick travel need underneath it. */
	constexpr float ReloadButtonBottomPt = 96.f;

	/** Between the two buttons. They must NEVER overlap (spec §5), and 12 pt is
	 *  also enough that a thumb aiming at one cannot clip the other. */
	constexpr float ThumbButtonGapPt = 12.f;

	/**
	 * The reload button: right thumb, above the aim stick, inside its arc.
	 *
	 * A dedicated button because reloading is a decision with a cost and the
	 * player must be able to make it BEFORE the magazine runs out —
	 * auto-reload-when-empty is the thing that gets you killed (spec §4.3).
	 *
	 * A pure function of the safe frame and the scale, and of NOTHING else. That
	 * is what makes "the interact button appearing must not shift the reload
	 * button" structural rather than a promise someone has to keep.
	 */
	FBox2D ReloadButtonRect(FBox2D Frame, float PointScale);

	/**
	 * The interact button: one 12 pt gap above the reload button, right-aligned to
	 * the same edge, contextual in its LABEL but never in its position.
	 *
	 * It used to shift left when a container panel covered its usual place. The
	 * panel is in the other half now (spec §4.5), so the shifted rect and the
	 * function that chose between the two are both gone — and with them the class
	 * of bug where the button is drawn in one place and pressed in another, which
	 * the owner experiences as "the button doesn't work".
	 *
	 * ONE authority: ASarkoHUD::DrawInteract draws this rect and
	 * ASarkoPlayerController::UpdateSticks hit-tests this rect. There is no second
	 * overload and no game-state argument, so they cannot disagree.
	 */
	FBox2D InteractButtonRect(FBox2D Frame, float PointScale);

	/**
	 * Where the aim thumb rests while working its stick. Documentary and
	 * test-facing: it is what Sarko.Input.ThumbControlsDoNotOverlap measures the
	 * two rects against, so "inside the thumb's arc" is a number rather than a
	 * claim.
	 */
	FVector2D RightThumbAnchor(FBox2D Frame, float PointScale);

	/**
	 * WHAT THE AIM THUMB IS DOING, in the only three answers there are.
	 *
	 * The aim stick is TWO CONTROLS sharing one thumb, and this names the split:
	 *
	 *   Rest — below MoveStickDeadZone. A thumb resting on the glass, a re-grip, a
	 *          finger steadying the phone. No direction was established, so nothing
	 *          happens at all.
	 *   Aim  — dead zone to fire threshold. The pawn turns and the aim cone follows
	 *          and NO ROUND IS FIRED. This band is the whole fix: before it existed
	 *          (fire threshold 0.35, i.e. 18 pt of a 52 pt stick) there was nowhere
	 *          to put a thumb that meant "look over there" without also meaning
	 *          "shoot", and the first phone playtest emptied a magazine into
	 *          nothing discovering that.
	 *   Fire — at or past USarkoRaidSettings::AimFireDeadZone. Firing, at the
	 *          weapon's own interval, for as long as the thumb stays out here.
	 *
	 * Pure, and the ONE authority: ASarkoPlayerController::PlayerTick asks it what
	 * to do and ASarkoHUD::DrawStick draws the Aim/Fire boundary at the same
	 * fraction, so the ring on the screen is a picture of this function rather than
	 * a second opinion about it.
	 */
	enum class ESarkoAimZone : uint8
	{
		Rest,
		Aim,
		Fire
	};
	ESarkoAimZone AimZoneFor(FVector2D AimValue, float MoveDeadZone, float FireThreshold);

	/** Whether the aim thumb is deflected far enough to be firing — AimZoneFor's
	 *  Fire zone, as the boolean the tick actually asks for. Pure, because it is
	 *  the difference between a weapon that shoots when you meant to aim and one
	 *  that does not. */
	bool ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone);

	/**
	 * THE STICK'S FULL-DEFLECTION TRAVEL, IN POINTS.
	 *
	 * It was 100 *pixels* — the one input constant in this project that was not
	 * point-scaled, and the reason four other numbers were wrong on a phone and
	 * right in the editor. On a 2556x1179 device the point scale is 3.02, so a
	 * "100 px" radius was 33 pt of thumb travel: less than the 44 pt minimum this
	 * project's own tests assert on both thumb buttons, roughly half of what
	 * shipped touch shooters use, and it dragged the fire threshold (0.35 of the
	 * radius) down to 11.6 pt and the quiet-walk band down to an 18 pt annulus.
	 * In a Mac editor window the scale is ~1.85, so the stick felt 63 % bigger
	 * than on the target device and nothing ever caught it.
	 *
	 * 52 pt puts the fire threshold at 18 pt (a deliberate push rather than a
	 * twitch), the walk/run noise boundary at 36 pt (a findable place on the
	 * ring), and full deflection at 52 pt — inside the ~45 pt arc a landscape
	 * thumb sweeps, which the reload button's placement already assumes.
	 */
	constexpr float StickRadiusPt = 52.f;

	/**
	 * That radius in pixels, for a viewport of this size. **The one authority.**
	 *
	 * Everything that consumes the radius — the deflection maths in
	 * FSarkoTouchStick::Value, the move dead zone and the fire threshold that are
	 * fractions of it, and the rings ASarkoHUD::DrawStick draws — reads the
	 * resolved value off the stick, which is set from here once, at the moment
	 * the thumb anchors. Resolving it per frame would be a multiply on a tick
	 * path for a number that cannot change while a finger is down; resolving it
	 * in two places would let the drawn ring and the rule it pictures disagree,
	 * which is the bug the old static constant already was.
	 */
	float StickRadiusPxForViewport(FVector2D ViewportSize);

	/**
	 * THERE IS NO ShouldFireOnRelease ANY MORE, and its absence is a rule.
	 *
	 * Lifting the aim thumb used to fire one round if the hold had gone anywhere
	 * at all — past MoveStickDeadZone, short of the fire threshold. That was a
	 * sound trade when the threshold was 0.35: the tap band was 8 pt to 18 pt, a
	 * gesture narrow enough that a player only landed in it on purpose, and it was
	 * the scheme's one precise single shot.
	 *
	 * At 0.70 the same rule inverts. The band becomes 8 pt to 36 pt — which is
	 * exactly the Aim zone, the place the player now spends every deliberate
	 * turn-and-look. "Release fires" would mean every aim ends in a gunshot: the
	 * complaint this change exists to fix, moved one gesture to the left. So the
	 * release does nothing, ever, and the rule is a single sentence a player can
	 * hold in their head — **past the ring you are shooting, inside it you are
	 * not.**
	 *
	 * The single aimed shot survives; it moved into the Fire zone where it is
	 * visible. Push past the ring and lift: the crossing frame fires once,
	 * immediately, and a second round needs MinFireIntervalSeconds (0.15 s) of
	 * holding out there. The two bugs the old release rule was hardened against —
	 * a directionless tap firing along the PREVIOUS hold's stale ray, and a
	 * drag-back-to-cancel that did not cancel — are not merely still fixed but
	 * unreachable: neither gesture ever enters the Fire zone.
	 * Sarko.Input.ReleasingTheAimStickNeverFires pins both.
	 */

	/**
	 * Whether the left thumb's stick must not be driven this frame.
	 *
	 * Today this is exactly "a container panel is open" — spec §4.5. The panel
	 * moved to the left half so that a thumb reaching for the AIM stick can never
	 * land on a cell, and the price of that is the move stick, which is the input
	 * looting can afford to lose: you are standing still to loot anyway. Shooting
	 * is not, and a player interrupted mid-loot must be able to fight back with
	 * the thumb that was already there. The aim stick, fire and the reload button
	 * all keep working untouched.
	 *
	 * **This is the ONE place.** Spec §5 names the fallback if the suppression
	 * reads as a bug in play — shrink the panel, do NOT restore movement under it
	 * — and that fallback is a one-line change here precisely because nothing
	 * else in the project decides this.
	 */
	bool IsMoveStickSuppressed(bool bContainerPanelOpen);
}

enum class ESarkoTakeRefusal : uint8;

/** One floating virtual stick, anchored wherever the thumb first touched. */
USTRUCT()
struct FSarkoTouchStick
{
	GENERATED_BODY()

	/**
	 * Screen distance at which this stick reads full deflection, in pixels.
	 *
	 * An instance member and not a static constant, because the answer depends on
	 * the screen: it is SarkoInput::StickRadiusPt resolved through the viewport's
	 * point scale, written once by ASarkoPlayerController::UpdateSticks at the
	 * moment the thumb anchors. The default is the unscaled point value so that a
	 * stick that somehow reads before it anchors divides by something sane rather
	 * than by zero.
	 */
	UPROPERTY()
	float RadiusPx = SarkoInput::StickRadiusPt;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D Current = FVector2D::ZeroVector;

	/** Deflection in the range [-1, 1] per axis, Y up. */
	FVector2D Value() const
	{
		if (!bActive)
		{
			return FVector2D::ZeroVector;
		}
		// Screen Y grows downward; flip it so "up" is positive.
		const FVector2D Delta(Current.X - Origin.X, Origin.Y - Current.Y);
		const float Length = Delta.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		// Guarded: a stick whose radius was never resolved would divide by zero
		// and read full deflection from a single pixel of travel.
		const float Radius = FMath::Max(KINDA_SMALL_NUMBER, RadiusPx);
		return (Delta / Length) * FMath::Min(1.f, Length / Radius);
	}
};

/**
 * Polls raw touch state instead of using Enhanced Input, because input actions
 * and mapping contexts are binary assets this project cannot author.
 */
UCLASS()
class ASarkoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASarkoPlayerController();

	/**
	 * Asserts FInputModeGameOnly, because nothing else in the raid does.
	 *
	 * UGameViewportClient::SetIgnoreInput is **not** world state: the viewport
	 * client belongs to the ULocalPlayer, which UEngine::LoadMap keeps while it
	 * destroys every actor. So the shelter's FInputModeUIOnly (which sets
	 * bIgnoreInput = true) survives the travel into the raid, and
	 * UGameViewportClient::InputKey/InputAxis/InputTouch all early-return while it
	 * is set — the raid spawns, the clock runs, and WASD, touch, fire, loot and
	 * extract are every one of them dead, so the raid can only ever end MIA.
	 *
	 * Asserted here rather than only reset by the shelter on the way out, and the
	 * shelter resets it too: each alone leaves the hole open (a future screen that
	 * forgets to reset, or a raid entered from some other UI-only state), and the
	 * pair is idempotent — applying game-only input in a world that is already
	 * game-only changes nothing.
	 */
	virtual void BeginPlay() override;

	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * Removes the container panel, before Super and unconditionally.
	 *
	 * A viewport widget is not an actor and is not destroyed with the level —
	 * the same reason ASarkoShelterPlayerController::EndPlay exists. Left added,
	 * the panel would still be on screen after the raid, over the shelter menu.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	const FSarkoTouchStick& GetMoveStick() const { return MoveStick; }
	const FSarkoTouchStick& GetAimStick() const { return AimStick; }

	/** The container the pawn could open right now, or nullptr. The HUD reads this to draw the prompt. */
	class ASarkoLootContainer* GetInteractTarget() const { return InteractTarget.Get(); }

	/** True while the player is holding interact. The HUD reads this to draw the progress bar. */
	bool IsInteractHeld() const { return bInteractHeld; }

	/** Which of the four taught verbs the player has already performed this raid.
	 *  Read only by ASarkoHUD::DrawFirstRaidHints; see the members for why they
	 *  live here and are not replicated. */
	bool HasEverFired() const { return bEverFired; }
	bool HasEverMoved() const { return bEverMoved; }
	bool HasEverReloaded() const { return bEverReloaded; }
	bool HasEverLooted() const { return bEverLooted; }

	/**
	 * TEMPORARY manual-verification aid for the rc-task-6 fix wave: a
	 * headless -game run has no touch input, so there is no other way to
	 * make the player pawn actually fire during that run. Fires the
	 * possessed pawn's weapon repeatedly (well past a full magazine) and
	 * logs ammo/reloading state before and after, so the log proves the
	 * player's weapon starts reloading on its own instead of going
	 * permanently dead. Not part of any of the six review items — remove
	 * once that manual pass is done.
	 */
	UFUNCTION(Exec)
	void CheatEmptyMagazine();

	/**
	 * Presses the reload button, Cycles times, IntervalSeconds apart, emptying the
	 * magazine before each press.
	 *
	 * A headless run has no fingers, and reload is manual-only since spec §3 — so
	 * without this there is no way at all to exercise the path that spec §1 just
	 * made the centre of the game, in a running raid, against a real grid. Each
	 * cycle logs nothing itself: USarkoWeaponComponent::FinishReload and
	 * ::StartReload already say what happened, and a harness that narrated over
	 * them would be quoting itself rather than the code.
	 *
	 * IntervalSeconds must exceed USarkoRaidSettings::ReloadSeconds or the next
	 * press lands while the previous reload is still in flight and StartReload
	 * drops it — which is correct behaviour and a useless observation.
	 */
	UFUNCTION(Exec)
	void CheatDrainAndReload(int32 Cycles, float IntervalSeconds);

	/**
	 * One reload press, after DelaySeconds. Separate from the cycle above because
	 * the interesting press is often the one on a magazine that is NOT empty — an
	 * empty bag with rounds still in the gun is the dry-click-on-reload path, and
	 * emptying the magazine first would hide it behind the other dry click.
	 */
	UFUNCTION(Exec)
	void CheatReload(float DelaySeconds);

	/**
	 * Frames the whole sector from above and takes a screenshot. This is the
	 * design loop for a hand-authored map: edit the data file, run offscreen,
	 * look at the frame, adjust. Without it the layout is written blind.
	 *
	 * UFUNCTION(Exec) cannot itself live inside an #if block (UHT rejects any
	 * UFUNCTION/UPROPERTY inside a preprocessor block other than
	 * WITH_EDITORONLY_DATA), so the declaration is unconditional here, same as
	 * CheatEmptyMagazine above; the shipping guard is applied to the body in
	 * the .cpp instead, so the tool's actual effect compiles out of shipping.
	 */
	UFUNCTION(Exec)
	void SarkoOverview();

	/**
	 * The headless verification set for the container panel. A -RenderOffscreen
	 * run has no fingers: it cannot hold the interact button for a channel, tap a
	 * Slate cell, or fill a backpack by playing the game — and every visual claim
	 * this panel makes has to be settled by a frame someone reads, because
	 * automation runs -nullrhi and can see nothing.
	 *
	 * Bodies are `#if !UE_BUILD_SHIPPING` in the .cpp; the declarations cannot be,
	 * because UHT rejects a UFUNCTION inside a preprocessor block. Same shape as
	 * SarkoOverview above.
	 */
	UFUNCTION(Exec)
	void SarkoDebugLoot(int32 Count);

	UFUNCTION(Exec)
	void SarkoOpenNearestContainer();

	/**
	 * Taps a container cell and, if ShotDelay is positive, takes the screenshot
	 * ShotDelay seconds AFTER the tap actually lands rather than at a fixed time
	 * from boot.
	 *
	 * That is the only way a still frame can catch the 240 ms refusal pulse: the
	 * tap itself happens whenever the loot channel finishes, which moves run to
	 * run, so a shutter timed from engine start is guessing. Chaining it off the
	 * tap makes the transient photographable instead of lucky.
	 */
	UFUNCTION(Exec)
	void SarkoTapContainerCell(int32 SlotIndex, float ShotDelay);

	UFUNCTION(Exec)
	void SarkoInventoryShot(float Delay);

	/**
	 * Debug only: sets the magazine to Rounds so the reload button's three states
	 * can be photographed.
	 *
	 * A headless run cannot earn them: Ready is the boot state, Low needs
	 * twenty-odd shots the run has no finger to fire, and Empty is a state
	 * auto-reload leaves almost immediately by design. This writes the count
	 * directly through the weapon's existing test seam and nothing else —
	 * ASarkoPlayerController::SarkoDebugLoot is the precedent.
	 */
	UFUNCTION(Exec)
	void SarkoDebugAmmo(int32 Rounds);

	/**
	 * Debug only: puts a named weapon in the possessed pawn's hand, so the one
	 * question this art pass exists to answer — can you tell a rifle from a
	 * pistol from 1400 uu straight up — can be photographed.
	 *
	 * A headless run cannot reach it otherwise: the equipped weapon comes from a
	 * profile fetched over HTTP, and the shipped catalog gives every profile the
	 * same ПМ. Cosmetic only, like everything else in SarkoWeaponVisuals: it
	 * changes nothing about what the weapon does.
	 */
	UFUNCTION(Exec)
	void SarkoDebugEquipWeapon(FString ItemId, float DelaySeconds, float ShotDelay);

	/**
	 * Debug only: puts the survival meters at a chosen reading and optionally
	 * applies damage, so a headless run can reach a state that would otherwise
	 * take a quarter of an hour and a firefight.
	 *
	 * Hunger and thirst move by 2.5 and 3.3 PER MINUTE, and regeneration only
	 * starts eight seconds after the last damage taken or dealt — so "thirst
	 * below thirty, wounded, then quiet" is a state no offscreen run can play its
	 * way into inside a reasonable timeout. Same precedent and same shipping
	 * guard as SarkoDebugAmmo above.
	 */
	UFUNCTION(Exec)
	void SarkoDebugSurvival(float Food, float Water, float Damage, float DelaySeconds);

	/**
	 * Debug only: taps a cell of the player's OWN grid, through the panel's real
	 * button, exactly as a finger would.
	 *
	 * The carry grid's consumable cells are the one interactive thing in this
	 * project a -RenderOffscreen run cannot reach any other way: the container
	 * cells already have SarkoTapContainerCell, and this is its mirror on the
	 * other half of the panel. It goes through SButton::SimulateClick rather than
	 * calling RequestConsumeItem, so what it proves includes the button existing,
	 * being enabled, and the rebuild being deferred out of ExecuteOnClick.
	 */
	UFUNCTION(Exec)
	void SarkoTapCarryCell(int32 SlotIndex, float DelaySeconds);

	/**
	 * Debug only: teleports the pawn onto extraction zone ZoneIndex, DelaySeconds
	 * from now.
	 *
	 * BugItGo would do it if a headless run could issue a command mid-run, but
	 * -ExecCmds executes its whole list at engine init — so every step of a
	 * verification run that needs time to pass has to carry its own delay, which
	 * is why the three seams above take one. Reading the pad's centre out of the
	 * map file also means the coordinate cannot go stale when the map moves.
	 */
	UFUNCTION(Exec)
	void SarkoDebugStandInZone(int32 ZoneIndex, float DelaySeconds);

	/**
	 * THE NOISE MODEL'S VERIFICATION SEAMS (spec §7). Three, and none of them
	 * fakes an outcome — they arrange a situation and let the production code
	 * log what it does.
	 *
	 * A headless raid cannot reach a firefight on its own: encounters spawn only
	 * when the player walks into a trigger AND every authored door is far enough
	 * away and out of sight, which is a walk no -ExecCmds line can take. So a bot
	 * is placed directly, through the same ApplyArchetypeAndPost the encounter
	 * director calls, and then everything that matters — what a shot emits, what
	 * a walk emits, what the bot hears, where it walks — runs through
	 * USarkoNoiseSubsystem and ASarkoAIController unchanged and says so in the log.
	 *
	 * SarkoDebugMove drives ASarkoCharacter::SetMoveIntent, which is the exact
	 * function the move stick drives, at a chosen deflection: that is what makes
	 * "walking is quiet, running is audible" observable at all in a run with no
	 * fingers. Scale is the stick's deflection, 0..1.
	 *
	 * Bodies are `#if !UE_BUILD_SHIPPING` in the .cpp; the declarations cannot be,
	 * because UHT rejects a UFUNCTION inside a preprocessor block.
	 */
	UFUNCTION(Exec)
	void SarkoDebugSpawnBot(FString ArchetypeId, float X, float Y, float DelaySeconds);

	UFUNCTION(Exec)
	void SarkoDebugMove(float DirX, float DirY, float Scale, float HoldSeconds, float DelaySeconds);

	UFUNCTION(Exec)
	void SarkoDebugFire(float DirX, float DirY, float DelaySeconds);

	/**
	 * THE TOUCH SEAMS. A headless run has no fingers, and every rule this
	 * controller owns — which half a touch claims, how far full deflection is,
	 * whether a release fires — lives BELOW the point SarkoDebugMove and
	 * SarkoDebugFire enter at. Those two write an intent straight onto the pawn,
	 * so they can prove what the pawn does with an intent and nothing at all
	 * about the stick that produced it. The sticks are also the only things that
	 * make the HUD draw its rings, so without this there is no frame that shows
	 * the radius this wave exists to fix.
	 *
	 * Both inject through UPlayerInput::InputTouch, which is the same call the
	 * iOS layer makes: UpdateSticks then classifies, anchors, resolves the radius
	 * and fires exactly as it would under a thumb. Nothing is faked past the
	 * glass.
	 *
	 * SarkoDebugTouchStick holds one stick at a fixed deflection: Half 0 is the
	 * left (move) stick and 1 the right (aim), Fraction is 0..1 of the resolved
	 * radius, and the touch is released after HoldSeconds.
	 */
	UFUNCTION(Exec)
	void SarkoDebugTouchStick(int32 Half, float DirX, float DirY, float Fraction, float HoldSeconds, float DelaySeconds);

	/**
	 * One scripted use of the aim thumb, and the four answers that matter. Three
	 * of them are the same answer, which is the point:
	 *
	 *   tap    — pressed and lifted without ever moving. Must NOT fire: it never
	 *            had a direction, so the round would have gone out along the
	 *            previous hold's ray.
	 *   cancel — dragged out into the AIM zone, back onto the anchor, lifted.
	 *            Must NOT fire.
	 *   flick  — dragged out into the AIM zone and lifted there. Must NOT fire,
	 *            and this one CHANGED: it used to be the scheme's single aimed
	 *            shot, back when the fire boundary was 0.35 and the band below it
	 *            was too narrow to land in by accident. At 0.70 that band is where
	 *            every deliberate aim now lives, so firing on release would mean
	 *            every look ends in a gunshot.
	 *   push   — dragged PAST the fire ring and lifted. MUST fire exactly once:
	 *            the deliberate single shot, moved to where the player can see it.
	 *
	 * Logs the magazine before and after, because "no shot" is only observable as
	 * a round that was not spent and a noise event that was not reported.
	 */
	UFUNCTION(Exec)
	void SarkoDebugAimGesture(FString Kind, float DelaySeconds);

private:
	void UpdateSticks();

	/**
	 * The container panel, owned here because a Slate widget belongs to a
	 * viewport and the HUD is not one.
	 *
	 * **Never with FInputModeUIOnly.** That mode sets
	 * UGameViewportClient::SetIgnoreInput(true) on a viewport client that belongs
	 * to the ULocalPlayer and outlives the level, which is the scar this class's
	 * BeginPlay already carries from the shelter. The panel routes taps by Slate
	 * hit-testing instead, and its root is SelfHitTestInvisible so everything that
	 * is not a cell falls through to the sticks.
	 */
	TSharedPtr<class SSarkoInventoryPanel> InventoryPanel;

	/** True between PlayExit and the widget actually being removed. A reopen
	 *  during that window rebuilds rather than reviving a fading widget. */
	bool bPanelExiting = false;

	FTimerHandle PanelExitTimer;

	/** Which pawn's delegates are currently bound, and the handles to undo it.
	 *  Possession can change mid-raid, and a binding left on a dead pawn is a
	 *  panel that never refreshes again. */
	TWeakObjectPtr<class ASarkoCharacter> BoundPawn;
	FDelegateHandle ContainerViewHandle;
	FDelegateHandle TakeRefusedHandle;

	/**
	 * Set by HandleContainerViewChanged, acted on by UpdateInventoryPanel one
	 * tick later. The indirection is not tidiness — see HandleContainerViewChanged
	 * for the crash it exists to prevent.
	 */
	bool bPanelDirty = false;

	/** Rebinds when the possessed pawn changes. Called once per tick; it compares
	 *  two pointers and does nothing on all but the first frame. */
	void UpdatePanelBinding();

	/** Creates, refreshes or dismisses the panel, from the tick rather than from
	 *  inside a Slate event. */
	void UpdateInventoryPanel();

	void HandleContainerViewChanged();
	void HandleTakeRefused(int32 SlotIndex, ESarkoTakeRefusal Reason);
	void RemoveInventoryPanel();

#if !UE_BUILD_SHIPPING
	/** Retry pumps for the headless execs above: the raid's authoritative seed,
	 *  the loot channel and the panel's construction all land on later frames
	 *  than the -ExecCmds line that asked for them. */
	FTimerHandle DebugOpenTimer;
	FTimerHandle DebugTapTimer;
	FTimerHandle DebugShotTimer;
	int32 DebugTapSlot = 0;
	float DebugTapShotDelay = 0.f;
	void TickDebugOpen();
	void TickDebugTap();
	void TakeDebugShot();
#endif

	/** Finds the nearest openable container and turns held input into channel start/stop. */
	void UpdateInteract();

	TWeakObjectPtr<class ASarkoLootContainer> InteractTarget;
	bool bInteractHeld = false;

	/** Which container the held input is currently channelling, or INDEX_NONE. */
	int32 HeldContainerIndex = INDEX_NONE;

	/** Which touch slot is holding the interact button, or INDEX_NONE. Claimed before stick classification. */
	int32 InteractTouchIndex = INDEX_NONE;

#if !UE_BUILD_SHIPPING
	/**
	 * Keyboard fallback so the game can be tested on a desktop, where a mouse
	 * emulates a single finger and cannot move and aim at the same time.
	 * WASD moves, space fires. Compiled out of shipping builds: the real game
	 * is touch-only and must never gain a second input path by accident.
	 *
	 * @return true if the keyboard supplied a movement intent this frame
	 */
	bool ApplyDesktopTestInput(class ASarkoCharacter& Pawn, float CameraYaw);

	/**
	 * The headless stick, held for as long as SarkoDebugMove asked for.
	 *
	 * Applied from PlayerTick beside ApplyDesktopTestInput and for the same
	 * reason: this class reassigns the move intent from MoveStick every frame, so
	 * an intent written once from a console command is gone before the pawn has
	 * accelerated. A run that verifies "walking is quiet" against a pawn that
	 * never moved would be verifying nothing.
	 *
	 * @return true if it supplied a movement intent this frame
	 */
	bool ApplyDebugMoveInput(class ASarkoCharacter& Pawn);

	/** The held deflection, and the world time it is released at. Zero intent
	 *  means the seam is idle and the real stick has the pawn. */
	FVector2D DebugMoveIntent = FVector2D::ZeroVector;
	float DebugMoveUntilSeconds = -1.f;

	/** One touch event, AfterSeconds from now, through the engine's own input
	 *  path. See SarkoDebugTouchStick for why the seam is here and not higher. */
	void ScheduleTouch(int32 FingerIndex, uint8 TouchType, FVector2D Position, float AfterSeconds);

	/** Where a thumb of that hand rests, in viewport pixels — the right one is
	 *  SarkoInput::RightThumbAnchor, the same point the button layout is measured
	 *  against, and the left is its mirror. */
	FVector2D DebugThumbAnchor(bool bLeftHalf) const;

	/** This viewport's resolved stick radius, so a debug drag is expressed in the
	 *  same units the rule is. */
	float DebugStickRadiusPx() const;

	/** Magazine and reserve, labelled. The whole proof of "no shot" is that this
	 *  reads the same before and after. */
	void LogGestureAmmo(const TCHAR* Stage, const FString& Kind);
#endif

	FSarkoTouchStick MoveStick;
	FSarkoTouchStick AimStick;

	/**
	 * Which physical touch slot (ETouchIndex) currently owns each stick, or
	 * INDEX_NONE. A stick is classified into left/right only once, at the
	 * moment its finger first touches down; after that this index — not the
	 * finger's current screen half — decides which stick it keeps driving.
	 * Without this, a thumb that drags across the midline mid-hold would be
	 * reclassified every frame and steal the other stick.
	 */
	int32 MoveTouchIndex = INDEX_NONE;
	int32 AimTouchIndex = INDEX_NONE;

	/** Which touch slot is holding the reload button, or INDEX_NONE. Claimed
	 *  before stick classification, exactly as InteractTouchIndex is — without it
	 *  a press on the button would also start an aim drag, and with hold-to-fire
	 *  that means the reload button shoots. */
	int32 ReloadTouchIndex = INDEX_NONE;

	/**
	 * The damage serial this controller has already reacted to, or INDEX_NONE
	 * before the first look.
	 *
	 * INDEX_NONE rather than zero for the same reason ASarkoHUD::SeenDamageSerial
	 * uses it: a controller that starts watching a pawn which has already been
	 * hit must record what it finds, not act on a bullet that landed before it
	 * was looking. See UpdateLootPanelUnderFire.
	 */
	int32 LastSeenDamageSerial = INDEX_NONE;

	/**
	 * Shuts the loot panel when the pawn takes a hit.
	 *
	 * The panel suppresses the move stick — deliberately, and that stays (spec
	 * §4.5: you are standing still to loot anyway, and aim, fire and reload all
	 * keep working). But the verb that gives movement back lives on the interact
	 * button, 104 pt straight up from the aim thumb's anchor, sized and placed
	 * for SEARCH — a decision you have already stopped to make. Under fire it is
	 * being used as a panic button, and it is in the wrong place for one.
	 *
	 * So the game closes the panel on exactly the event that makes movement
	 * matter again. Client-side and cosmetic: the panel is a client-side view of
	 * the container and this is a client-side input rule, so nothing new
	 * replicates — the damage serial already does, for the directional damage
	 * arcs the HUD polls the same way.
	 */
	void UpdateLootPanelUnderFire(class ASarkoCharacter& Pawn);

	/**
	 * FIRST-RAID TEACHING, dismissal half. Each is set the first time the player
	 * performs the verb, and read by ASarkoHUD::DrawFirstRaidHints so the hint
	 * for that verb never comes back this raid.
	 *
	 * Plain bools on the controller and NOT replicated: the whole hint system is
	 * one client drawing on its own screen, and in the standalone raid this game
	 * ships the client is the server anyway. Per-raid by construction — a
	 * controller does not outlive its level — which is exactly the lifetime
	 * "dismissed for this raid" wants.
	 */
	bool bEverFired = false;
	bool bEverMoved = false;
	bool bEverReloaded = false;
	bool bEverLooted = false;

	/**
	 * World time of the last fire request this client SENT.
	 *
	 * RequestFire is a reliable server RPC. Holding the stick would otherwise send
	 * one every frame — sixty reliable RPCs a second for a weapon that fires at
	 * most every MinFireIntervalSeconds — and the server would drop fifty-three of
	 * them after they had already cost the bandwidth. The server's own rate limit
	 * stays exactly as it is: this throttle is politeness, not authority.
	 */
	float LastLocalFireSeconds = -1000.f;
};
