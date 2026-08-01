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
	 * Where the on-screen interact button lives, in viewport pixels.
	 *
	 * Right-hand side, vertically centred: the bottom corners are covered by
	 * the thumbs driving the sticks (spec §9), and the very top cannot be
	 * reached without letting go of one. Computed from the frame rather than
	 * fixed, so it stays on screen on a phone and in a small desktop window.
	 */
	FBox2D InteractButtonRect(FBox2D Frame);

	/** As above, on a screen with no cutouts: the frame is the whole viewport. */
	FBox2D InteractButtonRect(FVector2D ViewportSize);

	/**
	 * Where the interact button sits while a container panel covers its usual
	 * place. Same size and same vertical band, moved left of the panel — a button
	 * drawn in one place and pressed in another is the one thing about this
	 * control that must never happen.
	 *
	 * The gap is a fraction of the button rather than a point constant because
	 * this function is handed pixels and has no scale to convert with; 0.3 of a
	 * 52 pt button is ~16 pt, which is the number the Visual design asks for and
	 * stays that on every density, since the button itself is density-derived.
	 *
	 * Callers use SarkoUI::InteractButtonRectFor rather than this directly: that
	 * is the one place that decides WHICH of the two rects is live.
	 */
	FBox2D InteractButtonRectBesidePanel(FBox2D Frame, FBox2D PanelRect);
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

	UFUNCTION(Exec)
	void SarkoTapContainerCell(int32 SlotIndex);

	UFUNCTION(Exec)
	void SarkoInventoryShot(float Delay);

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

	/** Rebinds when the possessed pawn changes. Called once per tick; it compares
	 *  two pointers and does nothing on all but the first frame. */
	void UpdatePanelBinding();

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
	void TickDebugOpen();
	void TickDebugTap();
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

	/** True on the frame the aim thumb lifts — that is when the shot goes off. */
	bool bAimReleasedThisFrame = false;
};
