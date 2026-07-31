#include "Map/SarkoMapBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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
	 * Paints one primitive a flat colour.
	 *
	 * This exists because of a rendering bug, not for art's sake.
	 * /Engine/BasicShapes/Cube ships with /Engine/EngineMaterials/WorldGridMaterial
	 * in its material slot — the engine's grey checkerboard placeholder — and
	 * nothing here was overriding it. That material is a texture, and the floor
	 * is one cube scaled 400x on X and Y, so its 0..1 UVs are stretched across
	 * 400 m: every texel of the grid texture, including the fine dither in its
	 * flat areas, comes out roughly the size of the player. Magnified that far
	 * there is no coarser mip to fall back to (r.MipMapLODBias 8 changes the
	 * frame not at all), so the ground renders as dense black-and-white speckle
	 * that fights every character silhouette on it. It is base colour, not
	 * lighting: the identical pattern is there with showflag.Lighting 0.
	 *
	 * BasicShapeMaterial is the cure because it has no textures at all — one
	 * vector parameter into BaseColor, one scalar into Roughness. A constant
	 * cannot alias at any scale, and on a phone it is strictly cheaper than what
	 * it replaces: zero texture samples and zero texture memory where there were
	 * two sampled textures (grid colour and grid normal) plus a CameraDepthFade.
	 *
	 * The parameter is called "Color" in current engine versions and "BaseColor"
	 * in older copies, so both are set — the same belt-and-braces the loot
	 * container and the extraction pad already use against this material.
	 */
	void PaintFlat(UStaticMeshComponent& Component, const FLinearColor& Tint, float Roughness)
	{
		static const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
		if (!Base)
		{
			// Not fatal: the geometry is still there and still blocks bullets, it
			// just keeps the engine's speckled placeholder material.
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: '%s' failed to load; geometry keeps the engine grid material"), BasicShapeMaterialPath);
			return;
		}

		const int32 SlotCount = FMath::Max(Component.GetNumMaterials(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			UMaterialInstanceDynamic* Material = Component.CreateAndSetMaterialInstanceDynamicFromMaterial(Slot, Base);
			if (!Material)
			{
				continue;
			}
			Material->SetVectorParameterValue(TEXT("Color"), Tint);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Tint);
			Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
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
	void SpawnMeshBox(UWorld& World, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Extent, bool bCollides,
		const FLinearColor& Tint, float Roughness)
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
			// Painted inside the Movable window too, for the same reason the mesh
			// is: a Static component that has already begun play is the awkward
			// case, and there is no reason to find out the hard way which of its
			// setters tolerate it.
			PaintFlat(*Component, Tint, Roughness);
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
	SpawnMeshBox(World, CubeMesh, FVector(0.f, 0.f, -25.f), FRotator::ZeroRotator,
		FVector(Layout.Extent, Layout.Extent, 25.f), true,
		Palette::ColourFor(ESarkoSurface::Ground), Palette::RoughnessFor(ESarkoSurface::Ground));

	// One loop, one spawn path, for cover *and* for the flat surfaces (roads,
	// water, the ravine bed) that ТЗ §14 wants — the only difference between
	// them is a colour lookup and a collision flag, both carried on the block.
	// A block authored before either field existed is Structure and colliding,
	// so this is byte-identical to what it replaced for every block on the
	// shipped map today.
	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		SpawnMeshBox(World, CubeMesh, Block.Location, Block.Rotation, Block.Extent, Block.bBlocksMovement,
			Palette::ColourFor(Block.Surface), Palette::RoughnessFor(Block.Surface));
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

		// One actor per part. A single-box kind — which is all eleven kinds the
		// shipped map uses — has one part with a zero offset, so this is the same
		// single spawn it always was, at the same transform.
		const FRotator Rotation(0.f, Prop.Yaw, 0.f);
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Part.Mesh.TryLoad());
			if (!Mesh)
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoMap: mesh missing for kind '%s'"), *Prop.Kind.ToString());
				++Skipped;
				continue;
			}
			// The part's offset is authored in the prop's own frame, so it
			// rotates with the prop: a road sign's plate stays over its post at
			// any yaw. The arithmetic lives in PartWorldLocation so it can be
			// asserted under -nullrhi, where there is no world to spawn into.
			const FVector PartLocation = PartWorldLocation(Prop.Location, Prop.Yaw, Part);
			// Colour comes from the part's surface rather than a per-prop choice:
			// the read the player needs from above is "ground versus thing
			// standing on it", and every legacy kind is Structure, which is the
			// exact grey props were painted before surfaces existed.
			SpawnMeshBox(World, Mesh, PartLocation, Rotation, Part.Extent, Part.bBlocksMovement,
				Palette::ColourFor(Part.Surface), Palette::RoughnessFor(Part.Surface));
		}
	}

	// Skipped counts *parts* that did not appear (plus one per unknown kind,
	// which is a deliberate approximation of "at least one thing is missing").
	// Both totals are named so the line cannot mislead once composites exist.
	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d prop actors from %d authored props, skipped %d parts"),
		CountPropActors(Definition) - Skipped, Definition.Props.Num(), Skipped);
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

void SarkoMap::SpawnExtractionZones(UWorld& World, const FSarkoMapDefinition& Definition)
{
	for (int32 Index = 0; Index < Definition.Extractions.Num(); ++Index)
	{
		const FSarkoExtractionSpot& Spot = Definition.Extractions[Index];
		const FTransform SpawnTransform(FRotator::ZeroRotator, Spot.Location);

		// Deferred, unlike SpawnLootContainers: the pad's mesh is scaled to the
		// zone's radius in BeginPlay, so the radius has to be set *before*
		// BeginPlay runs or every pad comes out the class-default size.
		ASarkoExtractionZone* Zone = World.SpawnActorDeferred<ASarkoExtractionZone>(
			ASarkoExtractionZone::StaticClass(), SpawnTransform, /*Owner*/ nullptr, /*Instigator*/ nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Zone)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn extraction zone %d"), Index);
			continue;
		}
		Zone->SetupFromSpot(Index, Spot.Name, Spot.RadiusUU);
		Zone->FinishSpawning(SpawnTransform);
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d extraction zones"), Definition.Extractions.Num());
}
