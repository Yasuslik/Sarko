#include "Pawn/SarkoBody.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/**
	 * Manny for the player, Quinn for the enemy. Two different meshes rather than
	 * one mesh twice: they are different builds with different textures, so the
	 * silhouettes differ even before any tint is applied — and if the tint below
	 * turns out to be a no-op on some material variant, friend/foe still reads.
	 */
	const TCHAR* PlayerMeshPath = TEXT("/Game/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* EnemyMeshPath = TEXT("/Game/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");

	/**
	 * M_Mannequin's body-colour vector parameter. Set through a dynamic instance
	 * of whatever material the mesh already carries, so the mannequin's own
	 * textures are kept and only the paint colour changes — the alternative,
	 * assigning a flat coloured material, would throw away the textures that are
	 * the entire point of using these assets.
	 */
	const TCHAR* PaintTintParameter = TEXT("Paint Tint");

	/** Blue reads as "me" and red as "them" from directly above; the same pairing the placeholder bodies used. */
	const FLinearColor PlayerTint(0.16f, 0.34f, 0.85f);
	const FLinearColor EnemyTint(0.80f, 0.12f, 0.10f);

	/**
	 * The mannequin faces +Y in mesh space, so a -90 degree yaw is what puts its
	 * nose down the actor's +X — the axis ASarkoCharacter::Tick rotates to face
	 * aim or travel. Without this the character runs permanently sideways, which
	 * from a top-down camera is the single most obvious way to look broken.
	 */
	constexpr float MeshYawCorrection = -90.f;
}

const TCHAR* SarkoBody::MeshPathForSide(ESide Side)
{
	return Side == ESide::Enemy ? EnemyMeshPath : PlayerMeshPath;
}

FLinearColor SarkoBody::TintForSide(ESide Side)
{
	return Side == ESide::Enemy ? EnemyTint : PlayerTint;
}

FLinearColor SarkoBody::FlashTint()
{
	// Not FLinearColor::White but past it: the Paint Tint parameter multiplies
	// the mannequin's own texture, so plain white leaves a dark body dark. Above
	// one it blows the albedo out, which from a top-down camera at 1400 uu is the
	// difference between "did something happen" and an unmistakable blink.
	return FLinearColor(3.f, 3.f, 3.f);
}

void SarkoBody::SetPaintTint(ACharacter& Character, const FLinearColor& Tint)
{
	USkeletalMeshComponent* MeshComponent = Character.GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	for (int32 Slot = 0; Slot < MaterialCount; ++Slot)
	{
		// The dynamic instance AttachCharacterMesh already made for THIS pawn's
		// mesh component. A failed cast means the slot was never given one (a
		// material that would not instance), and skipping it is right: creating
		// one here would be creating it on a draw-adjacent path, once per hit,
		// for a body that has already shown it cannot be tinted.
		if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(Slot)))
		{
			Dynamic->SetVectorParameterValue(PaintTintParameter, Tint);
		}
	}
}

void SarkoBody::AttachCharacterMesh(ACharacter& Character, ESide Side)
{
	USkeletalMeshComponent* MeshComponent = Character.GetMesh();
	if (!MeshComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoBody: character has no mesh component; the pawn will be invisible"));
		return;
	}

	const TCHAR* Path = MeshPathForSide(Side);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoBody: '%s' failed to load; the pawn will be invisible"), Path);
		return;
	}

	MeshComponent->SetSkeletalMeshAsset(Mesh);

	// The mannequin's origin is between its feet, so it has to be dropped by a
	// full capsule half-height to stand *on* the capsule's floor. Read from the
	// capsule rather than hard-coded, so a later capsule change cannot silently
	// sink the character into the ground or float it above it.
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
	MeshComponent->SetRelativeRotation(FRotator(0.f, MeshYawCorrection, 0.f));

	// Cosmetic only: the capsule owns collision, and a mesh that blocked traces
	// would let a shot hit the body instead of the pawn — which is not the same
	// hit, because the server re-traces against the capsule.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// A dynamic instance per material slot, so the tint is per-pawn and the
	// shared material instance on disk is never touched. This is also what makes
	// the hit flash per-instance (SetPaintTint above): every scav on the map
	// shares one archetype and one material asset, so without a MID of its own a
	// flash on one bot would blink all four.
	const FLinearColor Tint = TintForSide(Side);
	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	for (int32 Slot = 0; Slot < MaterialCount; ++Slot)
	{
		UMaterialInterface* Base = MeshComponent->GetMaterial(Slot);
		if (!Base)
		{
			continue;
		}
		if (UMaterialInstanceDynamic* Dynamic = MeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(Slot, Base))
		{
			// An unknown parameter name is ignored rather than fatal, so this is
			// safe against a material variant that does not expose the tint —
			// the two different meshes carry the friend/foe distinction in that
			// case.
			Dynamic->SetVectorParameterValue(PaintTintParameter, Tint);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoBody: %s wearing '%s', %d material slot(s), dropped %.0f uu onto the capsule floor"),
		Side == ESide::Enemy ? TEXT("enemy") : TEXT("player"), *Mesh->GetName(), MaterialCount, HalfHeight);
}
