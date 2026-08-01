#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Pawn/SarkoHealthComponent.h"
// FSarkoItemStack by value, not forward-declared: it is the payload of the
// client RPC below, and UHT needs the complete USTRUCT to generate its NetSerialize.
#include "Loot/SarkoItemCatalog.h"

#include "SarkoCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class USarkoWeaponComponent;
class USarkoBackpackComponent;
class USarkoCharacterAnimComponent;

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

/** Why a take did nothing. Sent to exactly one client so the panel can say which. */
UENUM()
enum class ESarkoTakeRefusal : uint8
{
	NoSpace,
	TooFar,
	NotOpen,
	Gone,
	RaidOver
};

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

	/** Drives the mesh's pose from health, the weapon and this pawn's own velocity. Purely cosmetic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visuals")
	TObjectPtr<USarkoCharacterAnimComponent> AnimComponent;

	/** Client intent: begin opening the container at this index. Validated server-side. */
	void RequestBeginLoot(int32 ContainerIndex);

	/** Client intent: stop opening. Also called automatically when the pawn walks away or dies. */
	void RequestCancelLoot();

	/** INDEX_NONE when not channelling. Server truth; the client keeps its own cosmetic copy. */
	int32 GetLootChannelIndex() const { return LootChannelIndex; }

	/** Seconds the channel has been running, or 0. Used by the HUD for the progress bar. */
	float GetLootChannelElapsed() const;

	/** Client intent: take one container slot. Every field is validated server-side. */
	void RequestTakeItem(int32 ContainerIndex, int32 SlotIndex);
	void RequestTakeAll(int32 ContainerIndex);
	void RequestCloseContainer();

	/** INDEX_NONE when no panel should be up. The client's own mirror, filled by RPC. */
	int32 GetOpenContainerIndex() const { return LocalOpenContainerIndex; }
	const TArray<FSarkoItemStack>& GetOpenContainerSlots() const { return LocalOpenContainerSlots; }

	/** Fires on the owning client whenever the open container's contents change,
	 *  including the first time they arrive. The panel subscribes to this. */
	DECLARE_MULTICAST_DELEGATE(FSarkoContainerViewChanged);
	FSarkoContainerViewChanged OnContainerViewChanged;

	/** Fires on the owning client when a take was refused, with the reason. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FSarkoTakeRefused, int32 /*SlotIndex*/, ESarkoTakeRefusal);
	FSarkoTakeRefused OnTakeRefused;

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

	UFUNCTION(Server, Reliable) void ServerTakeItem(int32 ContainerIndex, int32 SlotIndex);
	UFUNCTION(Server, Reliable) void ServerTakeAll(int32 ContainerIndex);
	UFUNCTION(Server, Reliable) void ServerCloseContainer();

	/**
	 * The one channel through which container contents reach a client, and only
	 * for a container that client has successfully opened (spec §3). A client RPC
	 * rather than replicated state because the container has no net identity — it
	 * is spawned locally on every machine from the map file — and because the
	 * fact is per-client, per-moment, per-container, which is exactly an RPC's
	 * shape. Reliable: a dropped update leaves the panel lying about a crate.
	 */
	UFUNCTION(Client, Reliable) void ClientContainerContents(int32 ContainerIndex, const TArray<FSarkoItemStack>& Slots);
	UFUNCTION(Client, Reliable) void ClientContainerClosed(int32 ContainerIndex);
	UFUNCTION(Client, Reliable) void ClientTransferRefused(int32 ContainerIndex, int32 SlotIndex, ESarkoTakeRefusal Reason);

	/** Server: the channel is complete, so the container opens. Called from Tick. */
	void TickLootChannel();

	/**
	 * Server. Rolls the container if this is its first open, remembers that this
	 * pawn has it open, and pushes the contents to the owning client.
	 *
	 * Shared by the completed channel and by ServerBeginLoot's instant re-open,
	 * so "what opening does" exists once rather than twice.
	 */
	void OpenContainerFor(int32 ContainerIndex);

	/**
	 * Server. Moves one container slot into this pawn's cells, or equips it if it
	 * is the first backpack. Returns false when nothing moved, which is the only
	 * thing a refusal means.
	 *
	 * Bag is the caller's working copy of the cells; the caller writes it back
	 * once. Both take paths go through here, so the "a bag is worn, not carried"
	 * rule cannot hold on one of them and not the other.
	 */
	bool TakeSlotInto(TArray<FSarkoItemStack>& Inventory, int32 SlotIndex, TArray<FSarkoItemStack>& Bag);

	/**
	 * Server. Drains a container into this pawn until nothing more fits.
	 *
	 * Assumes the §3 validation chain has already run once — it does not re-run
	 * it per item, because that would be four validations and four RPCs for one
	 * button press.
	 */
	void TakeAllFrom(int32 ContainerIndex);

	/**
	 * Server. Settles a container after a transfer: marks it emptied **only if it
	 * is actually empty**, then pushes the new contents to the owning client.
	 *
	 * The `SetContainerState(Emptied)` inside this function is the ONLY place in
	 * the project a container is marked emptied by a take. That is the
	 * vanishing-loot fix: the code this replaces marked unconditionally at
	 * channel completion, so whatever did not fit was destroyed with the crate.
	 */
	void FinishTransfer(int32 ContainerIndex, const TArray<FSarkoItemStack>& Inventory);

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

	/** Server truth: which container this pawn has open, or INDEX_NONE. */
	int32 OpenContainerIndex = INDEX_NONE;

	/** The client's mirror, and the panel's only data source. Never a guess: it is
	 *  written by ClientContainerContents and by nothing else. */
	int32 LocalOpenContainerIndex = INDEX_NONE;
	TArray<FSarkoItemStack> LocalOpenContainerSlots;
};
