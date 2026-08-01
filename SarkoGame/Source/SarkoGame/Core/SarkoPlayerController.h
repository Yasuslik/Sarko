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

	/** Whether the aim thumb is deflected far enough to be firing. Pure, because
	 *  it is the difference between a weapon that shoots when you meant to aim and
	 *  one that does not. */
	bool ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone);

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

	/** Screen distance at which the stick reads full deflection. */
	static constexpr float RadiusPx = 100.f;

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
		return (Delta / Length) * FMath::Min(1.f, Length / RadiusPx);
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

	/** True on the frame the aim thumb lifts — that is when the flick's shot goes off. */
	bool bAimReleasedThisFrame = false;

	/** Which touch slot is holding the reload button, or INDEX_NONE. Claimed
	 *  before stick classification, exactly as InteractTouchIndex is — without it
	 *  a press on the button would also start an aim drag, and with hold-to-fire
	 *  that means the reload button shoots. */
	int32 ReloadTouchIndex = INDEX_NONE;

	/** True once this hold of the aim stick has fired at least once. What makes a
	 *  flick fire exactly once on release, and a hold not fire a bonus shot when
	 *  the thumb finally lifts. Reset when the stick is next pressed. */
	bool bAimFiredThisHold = false;

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
