#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Net/Core/PushModel/PushModel.h"

#include "SarkoCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;

namespace SarkoAim
{
	/**
	 * Converts a thumbstick vector into a world-space planar direction, taking
	 * the camera yaw into account. Pure and world-free so the mapping — the
	 * thing a player feels most immediately — is unit tested.
	 */
	FVector2D StickToWorldDirection(FVector2D Stick, float CameraYaw);

	/**
	 * Maps a raw stick deflection to a movement scale in [0,1]: zero inside
	 * DeadZone (so a resting thumb does not creep the character), otherwise
	 * the deflection magnitude clamped to 1 (so an over-dragged stick cannot
	 * exceed WalkSpeed). Pure so the arithmetic is unit tested without a
	 * world or an actor.
	 */
	float MoveIntentScale(FVector2D Stick, float DeadZone);
}

/** The player pawn. Top-down camera, server-authoritative aim. */
UCLASS()
class ASarkoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASarkoCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called every frame by the controller from the left stick. */
	void SetMoveIntent(FVector2D Intent);

	/** Called by the controller from the right stick. */
	void SetAimIntent(FVector2D Intent, bool bInIsAiming);

	bool IsAiming() const { return bIsAiming; }

	/** Muzzle position for traces and effects. */
	FVector GetMuzzleLocation() const;

	/** Where this pawn is aiming, replicated so others see the facing. */
	UPROPERTY(ReplicatedUsing = OnRep_AimDirection, BlueprintReadOnly, Category = "Combat")
	FVector_NetQuantizeNormal AimDirection = FVector::ForwardVector;

protected:
	UFUNCTION()
	void OnRep_AimDirection() {}

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

private:
	/**
	 * Server RPC: the client's aim is validated and republished by the
	 * server. Unreliable — this fires every frame the stick is deflected, so
	 * a dropped packet is corrected by the next one and is not worth the
	 * cost of guaranteed delivery.
	 */
	UFUNCTION(Server, Unreliable)
	void ServerSetAim(FVector_NetQuantizeNormal NewAim, bool bInIsAiming);

	/**
	 * Server RPC: tells the server the aiming state changed (most notably,
	 * that the stick centred and aiming stopped). Reliable — unlike the
	 * continuous updates above, this fires once per transition, so it must
	 * not be dropped or the server's bIsAiming can stay pinned forever.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetAimState(bool bInIsAiming);

	FVector2D MoveIntent = FVector2D::ZeroVector;
	float MoveScale = 0.f;
	bool bIsAiming = false;
};
