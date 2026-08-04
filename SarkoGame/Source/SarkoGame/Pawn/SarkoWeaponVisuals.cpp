#include "Pawn/SarkoWeaponVisuals.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Map/SarkoMapBuilder.h"
#include "Map/SarkoMapPalette.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/**
	 * Item id -> mesh. Three entries, because three of the pack's forty guns
	 * passed the theme pass: a plain slab-sided service pistol with a wooden
	 * grip for the ПМ, the wood-furniture AKM, and the wood-stocked pump gun.
	 *
	 * Keyed by ITEM ID and not by any notion of weapon class, so the day a
	 * second rifle exists it is a row here and nothing else changes.
	 */
	struct FSarkoWeaponVisual
	{
		const TCHAR* ItemId;
		const TCHAR* MeshPath;
		/** See GripOffsetFor: where the grip is, in the gun's own frame. */
		FVector GripOffset;
	};

	/**
	 * Where each gun's grip is, measured from the mesh's own origin — which the
	 * import convention puts on the ground under the middle of the gun.
	 *
	 * X is how far FORWARD the mesh has to move so the grip lands in the fist,
	 * i.e. how far back from the middle the grip is: a Makarov's grip is 5 cm
	 * behind the centre of a 16 cm pistol, an AKM's is 18 cm behind the centre
	 * of an 88 cm rifle, a pump gun's is 22 cm behind the centre of a 105 cm
	 * one. Z drops it so the hand is around the middle of the grip rather than
	 * under the gun's belly.
	 *
	 * Three numbers per weapon and no angles: the ROTATION is computed from the
	 * skeleton rather than authored — see AlignmentToPawn().
	 */
	const FSarkoWeaponVisual Visuals[] = {
		{ TEXT("pistol"),  TEXT("/Game/ThirdParty/UltimateGuns/Pistol_1.Pistol_1"),
			FVector(5.f, 0.f, -5.f) },
		{ TEXT("rifle"),   TEXT("/Game/ThirdParty/UltimateGuns/AssaultRifle_2.AssaultRifle_2"),
			FVector(18.f, 0.f, -13.f) },
		{ TEXT("shotgun"), TEXT("/Game/ThirdParty/UltimateGuns/Shotgun_2.Shotgun_2"),
			FVector(22.f, 0.f, -9.f) },
	};

	/**
	 * Epic's mannequin bone. Named once so the test and the attachment cannot
	 * disagree about it, in the same spirit as SarkoBody::MeshPathForSide.
	 */
	const TCHAR* HandBone = TEXT("hand_r");

	/** The component's name, so a second call finds the first one's component. */
	const TCHAR* HeldWeaponComponentName = TEXT("SarkoHeldWeapon");

	const FSarkoWeaponVisual* Find(FName ItemId)
	{
		for (const FSarkoWeaponVisual& Visual : Visuals)
		{
			if (FName(Visual.ItemId) == ItemId)
			{
				return &Visual;
			}
		}
		return nullptr;
	}

	/**
	 * The rotation that turns the gun from "however `hand_r` happens to be
	 * oriented" into "pointing where the pawn points, sights up".
	 *
	 * COMPUTED, not authored, and that is the whole reason this function exists.
	 * The first fitting used hand-guessed Euler angles and produced a rifle that
	 * was genuinely attached, genuinely in the right place, and invisible —
	 * because it ended up pointing straight down the camera axis, which from
	 * directly above is a gun exactly one barrel wide. Guessing three angles
	 * against a skeleton nobody in this project authored is not a thing to do
	 * twice.
	 *
	 * The derivation, from the chain the engine actually walks. A weapon's world
	 * rotation is `Actor * MeshRelative * BoneComponentSpace * Relative`, so
	 * asking for a weapon aligned with the ACTOR — whose +X is the direction
	 * ASarkoCharacter::Tick turns to face aim or travel — gives
	 * `Relative = (MeshRelative * BoneComponentSpace)^-1`. Both factors are read
	 * from the assets: the mesh's own -90 yaw correction (SarkoBody) and the
	 * hand bone's rest orientation in the reference skeleton.
	 *
	 * The REST pose and not the animated one, so the answer is a property of the
	 * skeleton and identical on every machine and in every frame. The hand still
	 * moves the weapon after that, which is what a held weapon should do.
	 */
	FQuat AlignmentToPawn(const USkeletalMeshComponent& Body, FName Bone)
	{
		const USkeletalMesh* Asset = Body.GetSkeletalMeshAsset();
		if (!Asset)
		{
			return FQuat::Identity;
		}

		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		int32 Index = Ref.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			return FQuat::Identity;
		}

		FTransform ComponentSpace = FTransform::Identity;
		for (; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
		{
			ComponentSpace = ComponentSpace * Ref.GetRefBonePose()[Index];
		}

		return (Body.GetRelativeRotation().Quaternion() * ComponentSpace.GetRotation()).Inverse();
	}

	UStaticMeshComponent* FindHeld(ACharacter& Character)
	{
		TArray<UStaticMeshComponent*> Components;
		Character.GetComponents<UStaticMeshComponent>(Components);
		for (UStaticMeshComponent* Component : Components)
		{
			if (Component && Component->GetFName() == FName(HeldWeaponComponentName))
			{
				return Component;
			}
		}
		return nullptr;
	}
}

const TCHAR* SarkoWeaponVisuals::MeshPathForItem(FName ItemId)
{
	const FSarkoWeaponVisual* Visual = Find(ItemId);
	return Visual ? Visual->MeshPath : nullptr;
}

TArray<FName> SarkoWeaponVisuals::ArmedItemIds()
{
	TArray<FName> Ids;
	for (const FSarkoWeaponVisual& Visual : Visuals)
	{
		Ids.Add(FName(Visual.ItemId));
	}
	return Ids;
}

FName SarkoWeaponVisuals::HandBoneName()
{
	return FName(HandBone);
}

FVector SarkoWeaponVisuals::GripOffsetFor(FName ItemId)
{
	const FSarkoWeaponVisual* Visual = Find(ItemId);
	return Visual ? Visual->GripOffset : FVector::ZeroVector;
}

void SarkoWeaponVisuals::SetHeldWeapon(ACharacter& Character, FName ItemId)
{
	UStaticMeshComponent* Held = FindHeld(Character);
	const FSarkoWeaponVisual* Visual = Find(ItemId);

	if (!Visual)
	{
		// Unarmed. Destroying rather than hiding, because a hidden component is
		// still a component the pawn carries into every later frame and every
		// later GetComponents walk, and "holding nothing" should cost nothing.
		if (Held)
		{
			Held->DestroyComponent();
		}
		return;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Visual->MeshPath);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("SarkoWeaponVisuals: '%s' failed to load; '%s' will be held empty-handed — run Scripts/import-assets.sh"),
			Visual->MeshPath, *ItemId.ToString());
		return;
	}

	USkeletalMeshComponent* Body = Character.GetMesh();
	if (!Body)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoWeaponVisuals: character has no mesh component; nothing to attach a weapon to"));
		return;
	}

	if (!Held)
	{
		Held = NewObject<UStaticMeshComponent>(&Character, FName(HeldWeaponComponentName));
		// MOVABLE, and this line cost a frame to find. A UStaticMeshComponent
		// created at runtime defaults to Static mobility, and a Static component
		// parented to a moving skeletal mesh does not follow it — the engine
		// keeps its baked transform and the gun renders nowhere the pawn is. The
		// log said "holding 'rifle' at hand_r" and the frame showed empty hands,
		// which is exactly the shape of failure that makes it worth a comment.
		Held->SetMobility(EComponentMobility::Movable);
		Held->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName(HandBone));
		Held->RegisterComponent();
		// Cosmetic, exactly like the body: the capsule owns collision, and a
		// weapon that blocked traces would put a rifle barrel between a shot and
		// the pawn it was aimed at. It also casts no shadow of its own — at this
		// camera a 3 cm-thick barrel's shadow is noise, not information.
		Held->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Held->SetCastShadow(false);

	}

	Held->SetStaticMesh(Mesh);

	// PAINT IT WITH THE PROJECT'S OWN MATERIAL, slot by slot, and do not use the
	// pack's.
	//
	// This is not a style preference, it is the difference between a visible gun
	// and no gun at all. The materials Interchange builds from these FBX files
	// render as nothing in game: the same engine cube in the same component at
	// the same place draws, and the imported mesh does not. Every prop in the
	// sector has been sidestepping that since the first import pass —
	// ASarkoPropField::AddPart overwrites every slot with a shared surface
	// material and never notices — so a held weapon was simply the first asset
	// to try to use one and find out.
	//
	// Which surface, per slot, by the slot's own name: Quaternius names its
	// flat-colour slots after the material (Wood, DarkWood, Metal, DarkMetal,
	// Black), so wood takes Timber and everything else takes Structure. That
	// keeps the two-tone an AKM is READ by — pale wooden furniture against a
	// dark receiver — which is most of what survives the trip to 1400 uu, and it
	// keeps the weapon inside the sector's palette instead of beside it.
	//
	// The shared instances, not dynamic copies: they are the same objects every
	// wall and every barrel uses, so an armed pawn adds no material and no draw
	// call that the frame was not already paying for.
	const int32 SlotCount = Held->GetNumMaterials();
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const FName SlotName = Mesh->GetStaticMaterials().IsValidIndex(Slot)
			? Mesh->GetStaticMaterials()[Slot].MaterialSlotName
			: NAME_None;
		const bool bWooden = SlotName.ToString().Contains(TEXT("Wood"));
		if (UMaterialInterface* Painted = SarkoMap::SharedSurfaceMaterial(
				bWooden ? ESarkoSurface::Timber : ESarkoSurface::Structure))
		{
			Held->SetMaterial(Slot, Painted);
		}
	}

	// The bone is resolved by name through the same path a socket would be. If a
	// future mesh does author a real socket called hand_r this keeps working
	// unchanged; if the bone is ever renamed, the attach silently lands on the
	// component root, so the miss is reported rather than left to be noticed in a
	// frame.
	const FName Bone = FName(HandBone);
	if (!Body->DoesSocketExist(Bone))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoWeaponVisuals: '%s' has no '%s' bone or socket; the weapon will hang off the mesh root"),
			*Body->GetName(), *Bone.ToString());
	}

	Held->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);

	// The grip offset is authored in the GUN's frame, and the alignment above
	// makes the gun's frame the pawn's frame — so the same rotation carries the
	// offset across, and "18 cm back along the barrel" stays 18 cm back along
	// the barrel whatever the hand is doing.
	const FQuat Alignment = AlignmentToPawn(*Body, Bone);
	Held->SetRelativeTransform(FTransform(Alignment, Alignment.RotateVector(GripOffsetFor(ItemId))));

	// The weapon's position RELATIVE TO THE PAWN, not its world location: it is
	// the number a person fitting GripOffsetFor actually needs, and reading it
	// off a frame is guesswork. A gun that is somewhere absurd — inside the
	// chest, under the floor, a hundred uu behind — says so here.
	const FVector Offset = Character.GetActorTransform().InverseTransformPosition(Held->GetComponentLocation());
	UE_LOG(LogTemp, Display,
		TEXT("SarkoWeaponVisuals: '%s' holding '%s' (%.0f uu long) at '%s', %s from the pawn, scale %s, visible %d, bounds r=%.1f, mats %d"),
		*Character.GetName(), *ItemId.ToString(),
		Mesh->GetBounds().BoxExtent.X * 2.f, *Bone.ToString(), *Offset.ToCompactString(),
		*Held->GetComponentScale().ToCompactString(), Held->IsVisible() ? 1 : 0,
		Held->Bounds.SphereRadius, Held->GetNumMaterials());
}
