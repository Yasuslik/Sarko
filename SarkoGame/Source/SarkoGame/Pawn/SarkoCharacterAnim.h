#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoCharacterAnim.generated.h"

class UAnimSequence;
class USarkoWeaponComponent;
class USkeletalMeshComponent;

/**
 * The full-body poses this game can show.
 *
 * One plain UAnimSequence each, because that is all a project which authors no
 * binary assets can play: a blend space or a montage needs an AnimInstance and
 * an Anim Blueprint to host it, and both are .uasset files. Single-node mode
 * (SetAnimationMode(AnimationSingleNode) + PlayAnimation) plays one sequence at
 * a time with no asset of our own, so the states below are mutually exclusive
 * and switch rather than blend.
 */
enum class ESarkoAnimState : uint8
{
	/** Standing, weapon down. */
	Idle,
	/** Standing, weapon up — arms forward, which is what makes facing legible from directly above. */
	IdleAiming,
	/** Running. Eight directional variants, so strafing while aiming does not look like sliding. */
	Jog,
	/** The 2.2 s reload the weapon component actually runs. */
	Reload,
	/** A one-shot on the frame a round leaves the magazine. */
	Fire,
	/** Played once and held on its last frame. A corpse that stands back up idle is worse than no animation at all. */
	Death
};

namespace SarkoAnimation
{
	/**
	 * Picks the pose from game state, in priority order: death beats everything,
	 * a reload beats a shot, a shot only interrupts a standing pawn, and
	 * otherwise the pawn either runs or stands.
	 *
	 * Pure, so the priority order — the thing that decides whether a corpse can
	 * be seen reloading — is unit tested without a world, a mesh or a network.
	 *
	 * Fire deliberately loses to movement: with no montage there is no upper-body
	 * layer to put a shot on, so a firing pose would have to *replace* the run
	 * cycle for its whole length. At the ~100 px a pawn occupies on a phone, a
	 * stalled run reads far worse than an unanimated shot.
	 */
	ESarkoAnimState ChooseState(bool bDead, bool bReloading, bool bFiringWindow, bool bMoving, bool bAiming);

	/**
	 * Signed yaw of a planar velocity relative to a facing, in [-180, 180].
	 * Zero when the pawn is moving straight ahead. Returns zero for a velocity
	 * too small to have a direction, so a stationary pawn cannot pick a
	 * direction out of floating-point noise.
	 */
	float RelativeYaw(FVector PlanarVelocity, float FacingYawDegrees);

	/**
	 * Bucket for an eight-way directional set: 0 = forward, then clockwise in 45
	 * degree steps (1 = forward-right … 7 = forward-left). Clockwise because a
	 * positive yaw is a turn to the right in Unreal's left-handed world.
	 */
	int32 EightWayIndex(float RelativeYawDegrees);

	/** Number of directional jog variants. */
	constexpr int32 EightWayCount = 8;

	/**
	 * Play rate that stretches or squashes a sequence to fill TargetSeconds, so
	 * the reload animation lasts exactly as long as the reload the weapon
	 * component is actually running rather than finishing early and freezing.
	 * Clamped, so a zero-length sequence or a zero target cannot produce an
	 * infinite or negative rate.
	 */
	float PlayRateForDuration(float AnimLength, float TargetSeconds);

	/** Which of the three front-death sequences a pawn falls with. Deterministic per pawn, so a corpse never re-rolls its pose. */
	int32 DeathVariantForPawn(uint32 PawnUniqueId);

	/**
	 * Every asset path the pawns load, meshes included.
	 *
	 * Exposed for one reason: these assets were copied into the project from
	 * outside the editor, so nothing but a load proves the paths resolve. A
	 * headless test walks this list, which is a real regression guard —
	 * unlike the rendering, which -nullrhi cannot see at all.
	 */
	TArray<FString> AllAssetPaths();
}

/**
 * Drives one pawn's skeletal mesh from the states the game already has.
 *
 * Reads health, the weapon and the pawn's own velocity every frame and switches
 * the mesh's single-node animation when the resulting state changes. Nothing
 * here is authoritative: bDead, bReloading and the magazine count are all
 * replicated, so every machine reaches the same pose from the same facts and no
 * animation RPC is needed.
 */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoCharacterAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoCharacterAnimComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** What is on screen right now. Read by tests and worth having in a log. */
	ESarkoAnimState GetState() const { return CurrentState; }

private:
	/** Loads the sequence set. Called from BeginPlay, before any mesh exists. */
	void LoadSequences();

	/** Switches the mesh to a state, or does nothing if it is already showing it. */
	void Apply(ESarkoAnimState State, int32 DirectionIndex);

	UAnimSequence* SequenceFor(ESarkoAnimState State, int32 DirectionIndex) const;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAimingSequence;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> JogSequences;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReloadSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FireSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> DeathSequence;

	/**
	 * Found by class rather than handed over, so one component serves both pawn
	 * classes: the player and the enemy declare their health and weapon
	 * components as their own protected members, and this needs neither to know
	 * which class it is attached to.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class USarkoHealthComponent> Health;

	UPROPERTY(Transient)
	TObjectPtr<class USarkoWeaponComponent> Weapon;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** Nothing has been applied yet, so the first tick always applies. */
	ESarkoAnimState CurrentState = ESarkoAnimState::Idle;
	int32 CurrentDirection = INDEX_NONE;
	bool bApplied = false;

	/**
	 * A shot is discovered rather than signalled: the magazine count is already
	 * replicated, so every machine sees it drop on the same frame. A dedicated
	 * multicast would be one more RPC carrying information the client already
	 * has. INDEX_NONE until the first tick, so the magazine being filled at
	 * BeginPlay is not mistaken for a shot.
	 */
	int32 PreviousAmmo = INDEX_NONE;

	/** World seconds the fire pose stops. Zero when not firing. */
	float FireWindowEndSeconds = 0.f;
};
