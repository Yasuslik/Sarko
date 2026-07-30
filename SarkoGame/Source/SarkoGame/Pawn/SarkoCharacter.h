#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Pawn/SarkoHealthComponent.h"

#include "SarkoCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class USarkoWeaponComponent;
class USarkoBackpackComponent;

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
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called every frame by the controller from the left stick. */
	void SetMoveIntent(FVector2D Intent);

	/** Called by the controller from the right stick. */
	void SetAimIntent(FVector2D Intent, bool bInIsAiming);

	bool IsAiming() const { return bIsAiming; }

	/** Muzzle position for traces and effects. */
	FVector GetMuzzleLocation() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<USarkoWeaponComponent> WeaponComponent;

	/** What this pawn is carrying. Replicated owner-only by the component itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USarkoBackpackComponent> BackpackComponent;

	/** Called by the controller the moment the aim thumb lifts. */
	void RequestFire();

	/** Where this pawn is aiming, replicated so others see the facing. */
	UPROPERTY(ReplicatedUsing = OnRep_AimDirection, BlueprintReadOnly, Category = "Combat")
	FVector_NetQuantizeNormal AimDirection = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	TObjectPtr<USarkoHealthComponent> HealthComponent;

protected:
	UFUNCTION()
	void OnRep_AimDirection() {}

	/** Server only: stops movement and disables collision so the corpse does not block shots. */
	void HandleDeath(AActor* Killer);

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

	/**
	 * The server re-derives the muzzle origin from its own copy of the pawn,
	 * so a client-supplied origin would be pointless to send and pointless
	 * to trust; only Direction carries information the server doesn't
	 * already have.
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestFire(FVector Direction);

	FVector2D MoveIntent = FVector2D::ZeroVector;
	float MoveScale = 0.f;
	bool bIsAiming = false;
};
