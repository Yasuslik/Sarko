#include "Loot/SarkoLootContainer.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/SarkoRaidGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

bool SarkoLoot::CanInteract(const FVector& PawnLocation, const FVector& ContainerLocation,
	float RadiusUU, bool bPawnAlive, bool bAlreadyLooted)
{
	if (!bPawnAlive || bAlreadyLooted)
	{
		return false;
	}
	const FVector2D Flat(PawnLocation.X - ContainerLocation.X, PawnLocation.Y - ContainerLocation.Y);
	// <=, so a radius tuned to 250 does not behave like 249.99 on one machine.
	return Flat.SizeSquared() <= RadiusUU * RadiusUU;
}

namespace
{
	/** Closed: rusted olive. Emptied: washed out, so a cleared route reads at a glance. */
	const FLinearColor ClosedTint(0.28f, 0.30f, 0.16f);
	const FLinearColor LootedTint(0.42f, 0.42f, 0.44f);

	/** Chest-height crate: findable from above, low enough not to hide anything. */
	constexpr float BodyHalfWidth = 45.f;
	constexpr float BodyHalfHeight = 32.f;
	constexpr float LidHalfHeight = 8.f;
}

ASarkoLootContainer::ASarkoLootContainer()
{
	PrimaryActorTick.bCanEverTick = false;
	// Not replicated: every machine spawns its own from the map file. See the
	// class comment; making this true would create a second, competing copy on
	// every client.
	bReplicates = false;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Pivot);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(Pivot);
}

void ASarkoLootContainer::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cube || !BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoLootContainer %d: engine cube or basic material missing; container is invisible"),
			ContainerIndex);
		return;
	}

	// Movable -> assign -> Static, in that order. SetStaticMesh silently
	// no-ops on a Static component once the world has begun play, and this
	// actor is spawned well after BeginPlay; getting the order wrong leaves a
	// container with no mesh and no collision, which looks exactly like a
	// container that was never spawned.
	const auto Build = [Cube](UStaticMeshComponent& Component, const FVector& Extent, const FVector& Offset)
	{
		Component.SetMobility(EComponentMobility::Movable);
		Component.SetStaticMesh(Cube);
		Component.SetRelativeLocation(Offset);
		// The engine cube is 100 uu across, so scale is extent/50 per axis.
		Component.SetWorldScale3D(Extent / 50.f);
		Component.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component.SetMobility(EComponentMobility::Static);
	};

	Build(*Body, FVector(BodyHalfWidth, BodyHalfWidth, BodyHalfHeight), FVector::ZeroVector);
	Build(*Lid, FVector(BodyHalfWidth * 1.05f, BodyHalfWidth * 1.05f, LidHalfHeight),
		FVector(0.f, 0.f, BodyHalfHeight + LidHalfHeight));

	// The lid is the state indicator, so it gets the dynamic material. Same
	// BasicShapeMaterial trick SarkoBody uses; the parameter is called "Color"
	// in current engine versions and "BaseColor" in older copies, so both are set.
	LidMaterial = Lid->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMaterial);

	if (ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr)
	{
		// Registering rather than ticking: the state changes a few dozen times
		// per raid, so a per-frame poll on 42 actors would be pure waste.
		RaidState->RegisterContainer(this);
	}
	RefreshVisualState();
}

void ASarkoLootContainer::SetupFromSpot(int32 InIndex, FName InTier)
{
	ContainerIndex = InIndex;
	Tier = InTier;
}

bool ASarkoLootContainer::IsLooted() const
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	return RaidState && RaidState->IsContainerLooted(ContainerIndex);
}

void ASarkoLootContainer::RefreshVisualState()
{
	if (!LidMaterial)
	{
		return;
	}
	const FLinearColor Tint = IsLooted() ? LootedTint : ClosedTint;
	LidMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
	LidMaterial->SetVectorParameterValue(TEXT("BaseColor"), Tint);
}
