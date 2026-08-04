#include "Misc/AutomationTest.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Loot/SarkoItemCatalog.h"
#include "Pawn/SarkoBody.h"
#include "Pawn/SarkoWeaponVisuals.h"
#include "ReferenceSkeleton.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoWeaponMeshesResolve,
	"Sarko.Visuals.WeaponMeshesResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every held weapon mesh loads, is the size the real gun is, and is NOT the
 * shape a prop is.
 *
 * The last part is the one worth having. A weapon comes out of the same
 * Scripts/import-assets.sh as the trees but through its `weapon` mode, which
 * scales uniformly to a real length instead of stretching into the -50..50 box.
 * Nothing in C++ can tell whether Blender did that — and the failure would be
 * silent and absurd rather than loud: the pistol would arrive as a one-metre
 * cube in a scav's fist. So this asserts the shape of the ASSET, in exactly the
 * spirit of Sarko.Config.PropMeshBoundsAreNormalised asserting the opposite
 * shape for the props.
 */
bool FSarkoWeaponMeshesResolve::RunTest(const FString& Parameters)
{
	const TArray<FName> Armed = SarkoWeaponVisuals::ArmedItemIds();
	TestTrue(TEXT("there are weapons with meshes at all"), Armed.Num() >= 2);

	// The pawn is 176 uu tall (a capsule of half-height 88 — see SarkoBody), and
	// that is the ruler every one of these is measured against: a weapon longer
	// than the pawn is tall is a bug you would see from the camera, and one
	// shorter than a hand is a weapon nobody can see at all.
	constexpr float PawnHeightUU = 176.f;

	FSarkoItemCatalog Catalog;
	FString Error;
	const bool bCatalogLoaded = SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error);
	TestTrue(TEXT("the shipped catalog loads"), bCatalogLoaded);

	for (const FName& ItemId : Armed)
	{
		const TCHAR* Path = SarkoWeaponVisuals::MeshPathForItem(ItemId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a mesh path"), *ItemId.ToString()), Path))
		{
			continue;
		}

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh)
		{
			AddError(FString::Printf(TEXT("'%s' does not load — run Scripts/import-assets.sh UltimateGuns"), Path));
			continue;
		}

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float LengthUU = Bounds.BoxExtent.X * 2.f;

		// NOT the prop contract. If a future re-import ran these through the prop
		// normaliser they would all come back at exactly 100 uu on every axis,
		// and every one of the assertions below would still be a plausible
		// number for something — so the shape is checked directly.
		TestFalse(*FString::Printf(TEXT("'%s' was NOT stretched into the prop box (%s)"),
				*ItemId.ToString(), *Bounds.BoxExtent.ToString()),
			Bounds.BoxExtent.Equals(FVector(50.f), 0.5f));

		TestTrue(*FString::Printf(TEXT("'%s' is %.0f uu long — shorter than the 176 uu pawn is tall"),
				*ItemId.ToString(), LengthUU),
			LengthUU > 10.f && LengthUU < PawnHeightUU);

		// A gun is long and NARROW: longest along the barrel, thinnest across it.
		// Asserting the proportions rather than the axes catches the other half
		// of a normalisation accident, since a mesh scaled per axis comes back
		// roughly cubic.
		//
		// Against Y and not against Z, and that is the correction a first draft
		// of this test needed. A rifle is six times deeper than tall as well as
		// long, but a PISTOL is 8.1 x 1.4 x 5.1 — its grip makes it two thirds as
		// tall as it is long, so "longer than twice its height" is false for a
		// real pistol and the assertion was wrong rather than the mesh.
		TestTrue(*FString::Printf(TEXT("'%s' is longer than it is tall (%s)"),
				*ItemId.ToString(), *Bounds.BoxExtent.ToString()),
			Bounds.BoxExtent.X > Bounds.BoxExtent.Z);
		TestTrue(*FString::Printf(TEXT("'%s' is far longer than it is thick (%s)"),
				*ItemId.ToString(), *Bounds.BoxExtent.ToString()),
			Bounds.BoxExtent.X > Bounds.BoxExtent.Y * 3.f);

		// The import convention: X and Y centred, Z standing on zero, so a
		// dropped weapon rests on the floor rather than half through it and the
		// hand offsets in SarkoWeaponVisuals are written against a known datum.
		TestTrue(*FString::Printf(TEXT("'%s' stands on Z=0 (origin %s)"),
				*ItemId.ToString(), *Bounds.Origin.ToString()),
			FMath::IsNearlyEqual(Bounds.Origin.Z, Bounds.BoxExtent.Z, 0.5f)
				&& FMath::Abs(Bounds.Origin.X) < 0.5f);

		// An unresolved material renders as the default checkerboard, which from
		// 1400 uu is a bright white smear in a fist.
		TestTrue(*FString::Printf(TEXT("'%s' has materials"), *ItemId.ToString()),
			Mesh->GetStaticMaterials().Num() > 0);
		for (const FStaticMaterial& Material : Mesh->GetStaticMaterials())
		{
			TestNotNull(*FString::Printf(TEXT("every material slot on '%s' resolved"), *ItemId.ToString()),
				Material.MaterialInterface.Get());
		}

		// The mesh and the catalog have to be talking about the same object. A
		// mesh keyed to an id no catalog row has is a gun that can never appear.
		if (bCatalogLoaded)
		{
			const FSarkoItemDef* Def = Catalog.Find(ItemId);
			if (!TestNotNull(*FString::Printf(TEXT("'%s' is a real catalog item"), *ItemId.ToString()), Def))
			{
				continue;
			}
			// And the cell it occupies has to be in proportion to the gun: the
			// ПМ is 2 wide and 16 cm, the AKM 3 wide and 88. A 3-wide cell for
			// something shorter than a 2-wide one would make the grid lie about
			// what it is holding.
			TestTrue(*FString::Printf(TEXT("'%s' is %.0f uu long in a %dx%d cell"),
					*ItemId.ToString(), LengthUU, Def->Width, Def->Height),
				(Def->Width >= 3) == (LengthUU > 50.f));
			TestEqual(*FString::Printf(TEXT("'%s' is a weapon"), *ItemId.ToString()),
				static_cast<int32>(Def->Category), static_cast<int32>(ESarkoItemCategory::Weapon));
		}
	}

	// The hand. Recorded as a test rather than as a comment because the answer
	// was not obvious and cost time to find: SKM_Manny_Simple authors NO sockets
	// at all, so the attachment names the `hand_r` BONE and carries its own
	// relative transform. If a later mesh does author a socket by that name the
	// attach path is unchanged; if the bone is ever renamed, this fails here
	// instead of leaving a rifle floating at a pawn's feet.
	if (const USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, SarkoBody::MeshPathForSide(SarkoBody::ESide::Player)))
	{
		const FName Bone = SarkoWeaponVisuals::HandBoneName();
		TestTrue(*FString::Printf(TEXT("the mannequin has a '%s' bone to hang a weapon from"), *Bone.ToString()),
			Body->GetRefSkeleton().FindBoneIndex(Bone) != INDEX_NONE);
	}

	// An id nothing knows about is empty hands, not a crash and not a default
	// gun: a profile with an empty weapon slot is legal and the raid start path
	// already says so.
	TestNull(TEXT("an unknown item holds nothing"), SarkoWeaponVisuals::MeshPathForItem(FName(TEXT("medkit"))));
	TestNull(TEXT("no item at all holds nothing"), SarkoWeaponVisuals::MeshPathForItem(NAME_None));

	// Two weapons that are the same length are two weapons nobody can tell apart
	// from the camera. This is the whole claim the frames were taken to check,
	// stated as a number so a later mesh swap cannot quietly undo it.
	const UStaticMesh* Pistol = LoadObject<UStaticMesh>(nullptr, SarkoWeaponVisuals::MeshPathForItem(FName(TEXT("pistol"))));
	const UStaticMesh* Rifle = LoadObject<UStaticMesh>(nullptr, SarkoWeaponVisuals::MeshPathForItem(FName(TEXT("rifle"))));
	if (Pistol && Rifle)
	{
		TestTrue(TEXT("the rifle is at least three times the pistol's length"),
			Rifle->GetBounds().BoxExtent.X > Pistol->GetBounds().BoxExtent.X * 3.f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
