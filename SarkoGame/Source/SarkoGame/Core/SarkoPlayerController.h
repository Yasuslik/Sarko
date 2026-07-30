#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SarkoPlayerController.generated.h"

namespace SarkoInput
{
	/** Left half drives movement, right half drives aim. Boundary is inclusive. */
	bool IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize);
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

private:
	void UpdateSticks();

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
