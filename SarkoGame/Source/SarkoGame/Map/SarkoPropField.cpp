#include "Map/SarkoPropField.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/SarkoRaidGameState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Map/SarkoMapBuilder.h"
#include "Map/SarkoMapKinds.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/**
	 * How far the viewer must move before the fade is recomputed.
	 *
	 * This is the whole per-frame budget of the effect. At the player's 400 uu/s
	 * walk speed it means about three passes a second instead of sixty, and the
	 * worst staleness it can produce is a canopy that hangs on for 100 uu past
	 * where it should have gone — a quarter of a second, over a fade radius ten
	 * times that size, i.e. invisible.
	 */
	constexpr float FadeRecomputeStepUU = 100.f;

	/**
	 * How much further than the fade radius a canopy must get before it comes
	 * back. Without it a canopy sitting exactly on the boundary flickers on and
	 * off as the player strafes, which is far more distracting than the canopy
	 * ever was — and it flickers at the recompute rate, so it looks like a bug in
	 * the renderer rather than a threshold.
	 */
	constexpr float FadeHysteresisUU = 200.f;

	/**
	 * The scale a hidden canopy is squashed to.
	 *
	 * Not zero: a degenerate transform has an undefined inverse and several
	 * render paths take one. This is 1/10000th of the mesh, so the largest canopy
	 * in the table becomes 0.06 uu across — smaller than a pixel from any camera
	 * this game has, and it costs nothing to draw because it culls out.
	 *
	 * Scale rather than a visibility flag because there IS no per-instance
	 * visibility flag on an instanced component; the alternatives are removing
	 * the instance (which renumbers every index after it) or a material that
	 * reads per-instance custom data (which needs an authored asset, and this
	 * project authors none).
	 */
	constexpr float HiddenScale = 0.0001f;
}

ASarkoPropField::ASarkoPropField()
{
	// Nothing here ticks. The fade is driven from ASarkoRaidGameState::Tick,
	// which already ticks for the clock, so this actor adds no tick of its own.
	PrimaryActorTick.bCanEverTick = false;

	// A bare scene root. The instanced components attach to it, and it is what
	// puts the whole field at the world origin so an instance's component-space
	// transform is its world transform.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ASarkoPropField::BeginPlay()
{
	Super::BeginPlay();

	// Registering rather than being found: the same pattern the loot containers
	// use, and for the same reason. The game state drives the fade from its Tick
	// and must not run a TActorIterator to discover who to drive.
	if (ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr)
	{
		RaidState->RegisterPropField(this);
	}
}

UInstancedStaticMeshComponent* ASarkoPropField::FindOrCreateComponent(UStaticMesh* Mesh, const FSarkoPropPart& Part)
{
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"),
		*Mesh->GetPathName(), static_cast<int32>(Part.Surface),
		Part.bBlocksMovement ? 1 : 0, Part.bCanopy ? 1 : 0);

	if (TObjectPtr<UInstancedStaticMeshComponent>* Existing = ComponentsByKey.Find(Key))
	{
		return Existing->Get();
	}

	// HISM for the static bulk, plain ISM for canopies.
	//
	// The difference is which one is cheaper to CHANGE. HISM's cluster tree buys
	// per-instance culling and LOD, which is what a few hundred rocks and trunks
	// spread over 400 m want; but every transform written into a HISM marks that
	// tree out of date and schedules a rebuild. Canopies are the only instances
	// that ever move, they are few, and they all sit within a screen of the
	// player when they matter — so the tree would cost them something and buy
	// them nothing. A flat ISM writes the transform and stops.
	UInstancedStaticMeshComponent* Component = Part.bCanopy
		? NewObject<UInstancedStaticMeshComponent>(this)
		: NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	if (!Component)
	{
		return nullptr;
	}

	// Movable, and it stays Movable — unlike SpawnMeshBox, which flips back to
	// Static once the mesh is assigned. Instances are added after BeginPlay and
	// canopy transforms are written during play, and a Static primitive is
	// entitled to assume neither ever happens. Nothing is lost: every light in
	// this map is Movable too (SpawnLighting), so there is no baked lighting for
	// a Static primitive to have been participating in.
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(Part.bBlocksMovement
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
	Component->SetCollisionProfileName(Part.bBlocksMovement
		? UCollisionProfile::BlockAll_ProfileName
		: UCollisionProfile::NoCollision_ProfileName);
	// Scenery. Nothing overlaps it and there is no navmesh in this project (the
	// map is spawned from a data file at runtime, so nothing is baked), so both
	// of these are pure savings.
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);

	// The one shared instance for this surface — the same object every cover
	// block and every other component with this surface is using. Sharing it is
	// what makes instancing work at all: UE batches primitives that agree on
	// mesh AND material, so a unique material here would put every component
	// back into its own draw call.
	if (UMaterialInterface* Material = SarkoMap::SharedSurfaceMaterial(Part.Surface))
	{
		const int32 SlotCount = FMath::Max(Component->GetNumMaterials(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			Component->SetMaterial(Slot, Material);
		}
	}

	// SetupAttachment before RegisterComponent: the attachment API for an
	// unregistered component is SetupAttachment, and registering an unattached
	// component leaves it at the world origin with no parent.
	Component->SetupAttachment(GetRootComponent());
	Component->RegisterComponent();
	AddInstanceComponent(Component);

	ComponentsByKey.Add(Key, Component);
	return Component;
}

void ASarkoPropField::AddPart(UStaticMesh* Mesh, const FSarkoPropPart& Part, const FVector& WorldLocation,
	const FRotator& WorldRotation)
{
	if (!Mesh)
	{
		return;
	}

	UInstancedStaticMeshComponent* Component = FindOrCreateComponent(Mesh, Part);
	if (!Component)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoPropField: could not build a component for '%s'"), *Mesh->GetPathName());
		return;
	}

	// extent / 50: every engine primitive is built to a 100 uu bounding box, so
	// this is the identical arithmetic SpawnMeshBox used per actor.
	const FTransform InstanceTransform(WorldRotation.Quaternion(), WorldLocation, Part.Extent / 50.f);
	const int32 Index = Component->AddInstance(InstanceTransform, /*bWorldSpace*/ true);
	if (Index == INDEX_NONE)
	{
		return;
	}

	if (Part.bCanopy)
	{
		FSarkoCanopyInstance& Canopy = Canopies.AddDefaulted_GetRef();
		Canopy.Component = Component;
		Canopy.InstanceIndex = Index;
		Canopy.Location = WorldLocation;
		Canopy.Shown = InstanceTransform;
	}
}

int32 ASarkoPropField::GetInstanceCount() const
{
	int32 Total = 0;
	for (const TPair<FString, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : ComponentsByKey)
	{
		if (Pair.Value)
		{
			Total += Pair.Value->GetInstanceCount();
		}
	}
	return Total;
}

void ASarkoPropField::UpdateCanopyFade(const FVector& ViewerLocation, float RadiusUU)
{
	if (Canopies.Num() == 0 || RadiusUU <= 0.f)
	{
		return;
	}

	// The standing-still case, which is most frames. One squared distance and a
	// float compare, no allocation, no engine call.
	if (bFadeHasRun
		&& FMath::IsNearlyEqual(RadiusUU, LastFadeRadiusUU)
		&& FVector::DistSquared2D(ViewerLocation, LastFadeLocation) < FMath::Square(FadeRecomputeStepUU))
	{
		return;
	}
	LastFadeLocation = ViewerLocation;
	LastFadeRadiusUU = RadiusUU;
	bFadeHasRun = true;

	const float HideRadiusSq = FMath::Square(RadiusUU);
	const float ShowRadiusSq = FMath::Square(RadiusUU + FadeHysteresisUU);

	for (FSarkoCanopyInstance& Canopy : Canopies)
	{
		// Planar: the camera is overhead, so what decides whether a canopy is
		// between it and the pawn is how far away the canopy is on the ground,
		// not how high it hangs.
		const float DistanceSq = FVector::DistSquared2D(ViewerLocation, Canopy.Location);
		// Asymmetric thresholds: a hidden canopy has to get further out than the
		// one that hid it before it comes back. See FadeHysteresisUU.
		const bool bWantHidden = Canopy.bHidden
			? DistanceSq < ShowRadiusSq
			: DistanceSq < HideRadiusSq;
		if (bWantHidden == Canopy.bHidden)
		{
			continue;
		}

		UInstancedStaticMeshComponent* Component = Canopy.Component.Get();
		if (!Component)
		{
			continue;
		}

		const FTransform Target = bWantHidden
			? FTransform(Canopy.Shown.GetRotation(), Canopy.Shown.GetTranslation(), FVector(HiddenScale))
			: Canopy.Shown;
		Component->UpdateInstanceTransform(Canopy.InstanceIndex, Target,
			/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		Canopy.bHidden = bWantHidden;
	}
}
