#include "Map/SarkoMapBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Sun angle. Steep rather than horizontal so a top-down camera sees lit
	 * surfaces rather than long shadows across everything.
	 */
	const FRotator SunRotation(-55.f, 30.f, 0.f);

	/** Bright enough to read grey boxes on a phone screen in daylight. */
	constexpr float SunIntensityLux = 6.f;

	/**
	 * Exactly one directional light — the sun.
	 *
	 * Not two. The mobile forward shading path supports a single directional
	 * light, and a second one makes the engine warn on screen that lights are
	 * "competing to be the single one used for forward shading" and then pick
	 * one by brightness. A fill light from the opposite side is the obvious way
	 * to stop cover's shadowed faces going black, and it is exactly what this
	 * renderer cannot have.
	 *
	 * Not a SkyLight for the ambient either: a sky light needs a cubemap or a
	 * scene to capture, and this level is empty and this project authors no
	 * assets, so it would light the scene with the black it found. The shadowed
	 * sides instead read via the sun's shadow softness and the material's own
	 * base colour, which is enough for grey boxes.
	 *
	 * Movable mobility matters: this is spawned after BeginPlay, so there is no
	 * baked lighting for a Static light to have contributed to, and a Static
	 * light created at runtime lights nothing at all.
	 */
	void SpawnLighting(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 5000.f), SunRotation, Params);
		if (!Sun)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the sun; the raid will render black"));
			return;
		}

		Sun->SetMobility(EComponentMobility::Movable);
		if (ULightComponent* Component = Sun->GetLightComponent())
		{
			Component->SetIntensity(SunIntensityLux);
			Component->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f));
			Component->SetCastShadows(true);
		}
	}

	/**
	 * Spawns one box-shaped actor with a given mesh, transform and extent. The
	 * one spawn path SpawnLayout (floor, cover) and SpawnProps (props,
	 * container markers) both use.
	 *
	 * The Movable -> assign -> Static ordering is load-bearing:
	 * AStaticMeshActor's mesh component defaults to Static mobility, and
	 * UStaticMeshComponent::SetStaticMesh silently refuses to change the mesh
	 * on a Static component once the world has begun play — which it already
	 * has by the time this runs. Without this, the mesh assignment below is a
	 * no-op: the actor ends up with no mesh and no collision, so anything
	 * standing on it just falls through forever. Go Movable just long enough
	 * to assign the mesh, scale and collision, then lock it back to Static.
	 */
	void SpawnMeshBox(UWorld& World, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Extent, bool bCollides)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation, Params);
		if (!Actor)
		{
			return;
		}
		if (UStaticMeshComponent* Component = Actor->GetStaticMeshComponent())
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Mesh);
			// The engine cube (and the engine cylinder, built to the same
			// bounding-box convention) is 100 uu across, so scale is extent/50
			// per axis.
			Component->SetWorldScale3D(Extent / 50.f);
			Component->SetCollisionEnabled(bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			Component->SetMobility(EComponentMobility::Static);
		}
	}
}

void SarkoMap::SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout)
{
	// Light first, or the whole raid renders black. /Engine/Maps/Entry is an
	// empty container with no lights in it, and this project authors no level,
	// so nothing lights the scene unless we spawn it here. The HUD still draws
	// (it is 2D primitives over the frame), which makes the symptom confusing:
	// sticks and timer visible, world pitch black.
	SpawnLighting(World);

	// Engine primitives, referenced by path. Nothing is authored — this is how
	// the slice gets geometry without a single .uasset of our own.
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoMap: engine cube mesh missing; map will be empty"));
		return;
	}

	// Floor: one flattened cube covering the play area.
	SpawnMeshBox(World, CubeMesh, FVector(0.f, 0.f, -25.f), FRotator::ZeroRotator, FVector(Layout.Extent, Layout.Extent, 25.f), true);

	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		SpawnMeshBox(World, CubeMesh, Block.Location, Block.Rotation, Block.Extent, true);
	}
}

void SarkoMap::SpawnProps(UWorld& World, const FSarkoMapDefinition& Definition)
{
	int32 Skipped = 0;

	for (const FSarkoMapProp& Prop : Definition.Props)
	{
		FSarkoPropKind Kind;
		if (!FindPropKind(Prop.Kind, Kind))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: unknown prop kind '%s' at %s — skipped"),
				*Prop.Kind.ToString(), *Prop.Location.ToString());
			++Skipped;
			continue;
		}

		UStaticMesh* Mesh = Cast<UStaticMesh>(Kind.Mesh.TryLoad());
		if (!Mesh)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: mesh missing for kind '%s'"), *Prop.Kind.ToString());
			++Skipped;
			continue;
		}

		SpawnMeshBox(World, Mesh, Prop.Location, FRotator(0.f, Prop.Yaw, 0.f), Kind.Extent, Kind.bBlocksMovement);
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d props, skipped %d"),
		Definition.Props.Num() - Skipped, Skipped);
}

void SarkoMap::SpawnLootContainers(UWorld& World, const FSarkoMapDefinition& Definition)
{
	for (int32 Index = 0; Index < Definition.Containers.Num(); ++Index)
	{
		const FSarkoLootContainerSpot& Spot = Definition.Containers[Index];

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ASarkoLootContainer* Container = World.SpawnActor<ASarkoLootContainer>(
			ASarkoLootContainer::StaticClass(), Spot.Location, FRotator::ZeroRotator, Params);
		if (!Container)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn container %d at %s"), Index, *Spot.Location.ToString());
			continue;
		}
		// Set before BeginPlay would have been nicer, but SpawnActor runs it;
		// RefreshVisualState is called again from the game state's OnRep, so a
		// container that learns its index a moment late still ends up correct.
		Container->SetupFromSpot(Index, Spot.Tier);
		Container->RefreshVisualState();
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d loot containers"), Definition.Containers.Num());
}
