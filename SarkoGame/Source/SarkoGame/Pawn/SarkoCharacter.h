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

	/** Client intent: begin opening the container at this index. Validated server-side. */
	void RequestBeginLoot(int32 ContainerIndex);

	/** Client intent: stop opening. Also called automatically when the pawn walks away or dies. */
	void RequestCancelLoot();

	/** INDEX_NONE when not channelling. Server truth; the client keeps its own cosmetic copy. */
	int32 GetLootChannelIndex() const { return LootChannelIndex; }

	/** Seconds the channel has been running, or 0. Used by the HUD for the progress bar. */
	float GetLootChannelElapsed() const;

	/** Server only: pushes the dwell state the owning client's HUD draws. */
	void SetExtractProgress(int32 ZoneIndex, float DwellSeconds);

	/** INDEX_NONE when not in a zone. Owner-only replicated. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Extraction")
	int32 ExtractZoneIndex = INDEX_NONE;

	/** Seconds stood in the current zone. Owner-only replicated. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Extraction")
	float ExtractDwellSeconds = 0.f;

	/**
	 * Server only: the raid is over for this pawn, whatever the outcome.
	 *
	 * Movement is disabled on the *server's* copy, which is what makes the
	 * freeze real: a client that keeps sending ServerMove gets MOVE_None applied
	 * to it. Deliberately not an unpossess — the HUD still has to read the
	 * backpack it is about to list on the summary screen.
	 */
	void FreezeForRaidEnd();

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

	/**
	 * Server RPC. Reliable: a dropped begin would leave a player holding the
	 * button with nothing happening, which reads as a broken container.
	 *
	 * ContainerIndex is hostile input — the server bounds-checks it against its
	 * own map definition and re-measures the distance from its own copy of this
	 * pawn, exactly as ServerRequestFire re-derives the muzzle origin.
	 */
	UFUNCTION(Server, Reliable)
	void ServerBeginLoot(int32 ContainerIndex);

	UFUNCTION(Server, Reliable)
	void ServerCancelLoot();

	/**
	 * The server refused a begin request, so the client must drop the optimistic
	 * channel it started (see RequestBeginLoot).
	 *
	 * Without this the bar sits at 100% for as long as the button is held on any
	 * refused request — out of range on the server's copy of the pawn, an index
	 * that is already emptied, a raid that finished mid-flight — and reads as "the
	 * container is broken" rather than "that did not work".
	 *
	 * A client notify rather than a replicated rejection counter: the refusal
	 * concerns exactly one client and carries which request it refused, so nothing
	 * about it belongs in per-pawn replicated state that then also has to be
	 * conditioned to the owner. Reliable, because a dropped refusal is the pinned
	 * bar this exists to prevent.
	 */
	UFUNCTION(Client, Reliable)
	void ClientLootRejected(int32 ContainerIndex);

	/** Server: completes the channel, rolls and transfers. Called from Tick. */
	void TickLootChannel();

	/**
	 * True once the game state carries a real outcome. Every server RPC on this
	 * pawn consults it, so the freeze does not depend on the client politely
	 * stopping — spec §4.5's "input is frozen" has to hold against a client that
	 * does not cooperate.
	 */
	bool IsRaidFinishedNow() const;

	FVector2D MoveIntent = FVector2D::ZeroVector;
	float MoveScale = 0.f;
	bool bIsAiming = false;

	/** Server-side channel state. Not replicated: the HUD's bar is local and cosmetic. */
	int32 LootChannelIndex = INDEX_NONE;
	float LootChannelStartSeconds = 0.f;

	/** The client's own copy, so the bar moves without waiting for a round trip. */
	int32 LocalChannelIndex = INDEX_NONE;
	float LocalChannelStartSeconds = 0.f;
};
