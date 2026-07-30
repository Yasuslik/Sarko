#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SarkoPlayerController.generated.h"

namespace SarkoInput
{
	/** Left half drives movement, right half drives aim. Boundary is inclusive. */
	bool IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize);

	/**
	 * Where the on-screen interact button lives, in viewport pixels.
	 *
	 * Right-hand side, vertically centred: the bottom corners are covered by
	 * the thumbs driving the sticks (spec §9), and the very top cannot be
	 * reached without letting go of one. Computed from the viewport rather than
	 * fixed, so it stays on screen on a phone and in a small desktop window.
	 */
	FBox2D InteractButtonRect(FVector2D ViewportSize);
}

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

	virtual void PlayerTick(float DeltaTime) override;

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

private:
	void UpdateSticks();

	/** Finds the nearest openable container and turns held input into channel start/stop. */
	void UpdateInteract();

	/**
	 * Cached once the containers exist; they never move and never change count
	 * during a raid.
	 *
	 * The cache is only sealed once at least one container has been found: on a
	 * client the containers spawn from OnRep_Seed, which can land after the
	 * first PlayerTick, and a cache sealed empty on frame one would leave that
	 * client unable to loot anything for the whole raid.
	 */
	TArray<TWeakObjectPtr<class ASarkoLootContainer>> CachedContainers;
	bool bContainersCached = false;

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
