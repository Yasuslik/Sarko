#include "Map/SarkoMapBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/TextureCube.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"
#include "Map/SarkoPropField.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * The sun, plus an ambient term.
	 *
	 * Exactly one directional light. Not two: the mobile forward shading path
	 * supports a single directional light, and a second one makes the engine warn
	 * on screen that lights are "competing to be the single one used for forward
	 * shading" and then pick one by brightness. A fill light from the opposite
	 * side is the obvious way to stop cover's shadowed faces going black, and it
	 * is exactly what this renderer cannot have.
	 *
	 * The ambient is therefore a SkyLight, which contributes spherical-harmonic
	 * irradiance rather than a second shaded light. Its source is the engine's
	 * own map-template cubemap referenced by path — NOT SLS_CapturedScene, which
	 * would capture this level (no sky, no atmosphere, nothing beyond the floor)
	 * and light the scene with the black it found. That was the original reason
	 * for having no ambient at all, and a shipped cubemap is the way around it
	 * without authoring an asset.
	 *
	 * Movable mobility matters for both: these are spawned after BeginPlay, so
	 * there is no baked lighting for a Static light to have contributed to, and
	 * a Static light created at runtime lights nothing at all.
	 */
	void SpawnLighting(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(
			FVector(0.f, 0.f, 5000.f), SarkoMap::Lighting::SunRotation, Params);
		if (!Sun)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the sun; the raid will render black"));
			return;
		}

		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunComponent->SetIntensity(SarkoMap::Lighting::SunIntensityLux);
			SunComponent->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f));
			SunComponent->SetCastShadows(true);
			// Lifts cast shadows off black without removing them. A shader
			// constant, so this line is free.
			SunComponent->SetShadowAmount(SarkoMap::Lighting::ShadowAmount);
		}

		ASkyLight* Sky = World.SpawnActor<ASkyLight>(FVector(0.f, 0.f, 5000.f), FRotator::ZeroRotator, Params);
		if (!Sky)
		{
			// Not fatal: the sun still lights the scene, the shadowed sides just
			// go dark again.
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the sky light; shadowed faces will read as black"));
			return;
		}
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, SarkoMap::Lighting::AmbientCubemapPath);
			if (!Cubemap)
			{
				// A sky light with SLS_SpecifiedCubemap and a null cubemap is
				// invalid and contributes nothing, so say so loudly rather than
				// leaving a light in the scene that does not light.
				UE_LOG(LogTemp, Error, TEXT("SarkoMap: ambient cubemap '%s' failed to load; there will be no ambient light"),
					SarkoMap::Lighting::AmbientCubemapPath);
				return;
			}
			SkyComponent->SetMobility(EComponentMobility::Movable);
			SkyComponent->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
			SkyComponent->CubemapResolution = SarkoMap::Lighting::AmbientCubemapResolution;
			SkyComponent->bLowerHemisphereIsBlack = false;
			SkyComponent->LowerHemisphereColor = SarkoMap::Lighting::GroundBounceColour;
			SkyComponent->SetCubemap(Cubemap);
			SkyComponent->SetIntensity(SarkoMap::Lighting::AmbientIntensity);
			SkyComponent->SetLightColor(SarkoMap::Lighting::AmbientColour);
			// Once. There is no time of day and nothing in the sky moves, so a
			// real-time capture would re-render a cubemap every frame on a phone
			// to produce the same numbers.
			SkyComponent->RecaptureSky();
		}
	}

	/**
	 * The one material instance for a surface, made once and handed to every
	 * component that wants it.
	 *
	 * This material exists because of a rendering bug, not for art's sake.
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
	 * TEXTURE, SINCE. A surface with a generated detail map
	 * (Palette::DetailTexturePath) is built on /Game/Generated/Materials/M_SarkoSurface
	 * instead — the same two parameters plus one greyscale sample. That is not a
	 * retreat from the paragraph above: the aliasing there was a texture with no
	 * mip small enough, stretched 400x on mesh UVs. This one is world-aligned
	 * (uu per tile, not repeats per mesh), band-limited when it is generated, and
	 * mipped, so its texel size is a fixed number of world units at every scale
	 * the sector contains.
	 *
	 * A surface with NO map keeps BasicShapeMaterial, byte for byte as before.
	 * That is deliberate rather than incidental: water, the ford, the extraction
	 * pads and the three skirt bands are the surfaces whose flatness is the
	 * design, and giving them the new material would have cost them a sample to
	 * multiply by one.
	 *
	 * If M_SarkoSurface fails to load — a fresh clone that has not run
	 * Scripts/generate-textures.sh, a cook that dropped it — every surface falls
	 * back to BasicShapeMaterial and the sector renders exactly as it did before
	 * textures existed. Colour is never lost; only detail is.
	 *
	 * The parameter is called "Color" in current engine versions and "BaseColor"
	 * in older copies, so both are set — the same belt-and-braces the loot
	 * container and the extraction pad already use against this material.
	 *
	 * The colour of a block is a pure function of its surface — eleven surfaces,
	 * eleven looks — so a unique UMaterialInstanceDynamic per component per slot
	 * was 344 distinct material proxies on the shipped map describing eleven
	 * distinct appearances, and would be around 550 once Stage C's ledger is
	 * authored. That is not a memory argument: a unique material is a unique
	 * shader binding, which is exactly what stops the renderer batching identical
	 * static meshes, and it is the thing an instanced-static-mesh migration would
	 * have to undo first. Sharing costs nothing — nothing here ever animates a
	 * parameter, and if anything ever needs to, it needs its OWN instance and
	 * should say so rather than quietly relying on every block having one.
	 *
	 * Rooted rather than owned by a component. A MID created by
	 * CreateAndSetMaterialInstanceDynamicFromMaterial is outered to the component
	 * that made it, which is fine while that component is the only user and a
	 * dangling pointer in this cache the moment the level is torn down and the
	 * next raid asks for the same surface — and this game travels between the
	 * shelter and a raid repeatedly. Eleven permanently-rooted objects is the
	 * whole cost of never having to reason about that.
	 *
	 * Game thread only, like everything else in this file.
	 */
	UMaterialInstanceDynamic* SharedFlatMaterialInternal(ESarkoSurface Surface)
	{
		static const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
		static TMap<uint8, UMaterialInstanceDynamic*> Cache;

		const uint8 Key = static_cast<uint8>(Surface);
		if (UMaterialInstanceDynamic** Existing = Cache.Find(Key))
		{
			if (IsValid(*Existing))
			{
				return *Existing;
			}
			Cache.Remove(Key);
		}

		// The detail map decides which base material this surface wants. A
		// texture that is listed but missing on disk is a pipeline failure, not
		// a reason to lose the surface: it falls through to the flat material
		// with a log line, and the sector keeps its colours.
		const TCHAR* DetailPath = SarkoMap::Palette::DetailTexturePath(Surface);
		UTexture* Detail = DetailPath ? LoadObject<UTexture>(nullptr, DetailPath) : nullptr;
		if (DetailPath && !Detail)
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoMap: detail map '%s' for surface '%s' failed to load; the surface stays flat. Run Scripts/generate-textures.sh."),
				DetailPath, *SarkoMap::SurfaceName(Surface));
		}

		UMaterialInterface* Base = Detail
			? LoadObject<UMaterialInterface>(nullptr, SarkoMap::SurfaceMaterialPath)
			: nullptr;
		if (Detail && !Base)
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoMap: '%s' failed to load; every surface stays flat. Run Scripts/generate-textures.sh."),
				SarkoMap::SurfaceMaterialPath);
			Detail = nullptr;
		}
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
		}
		if (!Base)
		{
			// Not fatal: the geometry is still there and still blocks bullets, it
			// just keeps the engine's speckled placeholder material.
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: '%s' failed to load; geometry keeps the engine grid material"), BasicShapeMaterialPath);
			return nullptr;
		}

		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, nullptr);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: could not create a material instance for surface '%s'"),
				*SarkoMap::SurfaceName(Surface));
			return nullptr;
		}

		const FLinearColor Tint = SarkoMap::Palette::ColourFor(Surface);
		Material->SetVectorParameterValue(TEXT("Color"), Tint);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Tint);
		Material->SetScalarParameterValue(TEXT("Roughness"), SarkoMap::Palette::RoughnessFor(Surface));
		if (Detail)
		{
			// Set together, and only together. M_SarkoSurface's strengths default
			// to zero, so a run that set the texture and not the strengths would
			// sample a map and multiply the colour by exactly one — a texture
			// fetch per pixel buying nothing, and no visible symptom to notice it
			// by.
			Material->SetTextureParameterValue(TEXT("Detail"), Detail);
			Material->SetScalarParameterValue(TEXT("DetailTileUU"), SarkoMap::Palette::DetailTileUU(Surface));
			Material->SetScalarParameterValue(TEXT("DetailStrength"), SarkoMap::Palette::DetailStrength(Surface));
			Material->SetScalarParameterValue(TEXT("DetailRoughness"), SarkoMap::Palette::DetailRoughnessSwing(Surface));
		}
		Material->AddToRoot();

		Cache.Add(Key, Material);
		return Material;
	}

	/** Assigns that shared instance to every slot the component has. */
	void PaintFlat(UStaticMeshComponent& Component, ESarkoSurface Surface)
	{
		UMaterialInterface* Material = SarkoMap::SharedSurfaceMaterial(Surface);
		if (!Material)
		{
			return;
		}
		const int32 SlotCount = FMath::Max(Component.GetNumMaterials(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			Component.SetMaterial(Slot, Material);
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
		ESarkoSurface Surface)
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
			PaintFlat(*Component, Surface);
			Component->SetMobility(EComponentMobility::Static);
		}
	}
}

bool SarkoMap::NearestPoint(const FVector& From, const TArray<FVector>& Candidates, FVector& OutPoint)
{
	// Squared distance, and XY rather than 3D: a pawn that has left the world is
	// a long way DOWN, and z would then be a large term every candidate shares —
	// it cannot change which is nearest, and it can hide the difference between
	// two spawns 2000 uu apart under a 100000 uu fall. "Nearest" here means
	// nearest on the ground the pawn fell off.
	double Best = TNumericLimits<double>::Max();
	bool bFound = false;
	for (const FVector& Candidate : Candidates)
	{
		const double Squared = FVector::DistSquaredXY(From, Candidate);
		if (Squared < Best)
		{
			Best = Squared;
			OutPoint = Candidate;
			bFound = true;
		}
	}
	return bFound;
}

UMaterialInterface* SarkoMap::SharedSurfaceMaterial(ESarkoSurface Surface)
{
	return SharedFlatMaterialInternal(Surface);
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
		FVector(Layout.Extent, Layout.Extent, 25.f), true, ESarkoSurface::Ground);

	// One loop, one spawn path, for cover *and* for the flat surfaces (roads,
	// water, the ravine bed) that ТЗ §14 wants — the only difference between
	// them is a colour lookup and a collision flag, both carried on the block.
	// A block authored before either field existed is Structure and colliding,
	// so this is byte-identical to what it replaced for every block on the
	// shipped map today.
	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		SpawnMeshBox(World, CubeMesh, Block.Location, Block.Rotation, Block.Extent, Block.bBlocksMovement,
			Block.Surface);
	}
}

void SarkoMap::SpawnProps(UWorld& World, const FSarkoMapDefinition& Definition)
{
	// ONE actor for every prop in the sector, instead of one per part.
	//
	// Until the forest this was an actor per part: 401 of them against ТЗ §16's
	// ceiling of 420, which is why "add trees" and "instance the props" were
	// always the same task — a stand of any size at all blows that budget on its
	// own. The trigger written into Sarko.Map.BridgeStaysInsideTheActorBudget
	// named this the response, in this order, and this is it being taken.
	//
	// It only became possible when the per-actor UMaterialInstanceDynamic went
	// away: UE batches primitives that share a mesh AND a material, so 400 unique
	// materials describing eleven appearances defeated instancing outright. With
	// one shared instance per surface the whole props section collapses into a
	// handful of components — see ASarkoPropField.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASarkoPropField* Field = World.SpawnActor<ASarkoPropField>(
		ASarkoPropField::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Field)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the prop field; the sector will have no props"));
		return;
	}

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
			// Unchanged by instancing — an instance transform is the transform
			// the actor used to have.
			const FVector PartLocation = PartWorldLocation(Prop.Location, Prop.Yaw, Part);
			// Colour still comes from the part's surface, and it is now also what
			// decides which component the part lands in: same surface, same mesh,
			// same draw call.
			Field->AddPart(Mesh, Part, PartLocation, Rotation);
		}
	}

	// Not optional, and not a tidy-up: a HISM draws nothing until its cluster
	// tree is built, and AddInstance only schedules that asynchronously. Without
	// this call every prop in the sector is missing for the first frames of the
	// raid. See ASarkoPropField::FinishBuild.
	Field->FinishBuild();

	// Skipped counts *parts* that did not appear (plus one per unknown kind,
	// which is a deliberate approximation of "at least one thing is missing").
	UE_LOG(LogTemp, Display,
		TEXT("SarkoMap: %d prop instances from %d authored props in %d instanced components (%d canopies), skipped %d parts"),
		Field->GetInstanceCount(), Definition.Props.Num(), Field->GetInstancedComponentCount(),
		Field->GetCanopyCount(), Skipped);
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
		// OpensAfterSeconds and the raid's length both travel with the zone now
		// (spec §4.5): a pad that says ЗАЧИНЕНО on the HUD while drawing
		// extraction green on the ground is two answers to one question. The
		// duration is the map's own, with the settings fallback the game mode and
		// the HUD both use, so all three agree about what "four minutes in" means.
		const float RaidDuration = Definition.RaidDurationSeconds > 0.f
			? Definition.RaidDurationSeconds
			: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;
		Zone->SetupFromSpot(Index, Spot.Name, Spot.RadiusUU, Spot.OpensAfterSeconds, RaidDuration);
		Zone->FinishSpawning(SpawnTransform);
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d extraction zones"), Definition.Extractions.Num());
}
