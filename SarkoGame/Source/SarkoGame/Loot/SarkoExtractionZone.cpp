#include "Loot/SarkoExtractionZone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Map/SarkoMapDefinition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

float SarkoExtract::AdvanceDwell(float CurrentSeconds, bool bInsideZone, float DeltaSeconds)
{
	if (!bInsideZone)
	{
		return 0.f;
	}
	if (DeltaSeconds <= 0.f)
	{
		return CurrentSeconds;
	}
	return CurrentSeconds + FMath::Min(DeltaSeconds, MaxDwellStepSeconds);
}

SarkoExtract::FSarkoDwell SarkoExtract::AdvanceDwellInZone(const FSarkoDwell& Current, int32 ZoneIndex,
	float DeltaSeconds)
{
	FSarkoDwell Next;

	if (ZoneIndex == INDEX_NONE)
	{
		// Outside: forget the zone and the seconds both. Returning a default is the
		// whole rule, so there is nothing to reset elsewhere.
		return Next;
	}

	Next.ZoneIndex = ZoneIndex;

	if (ZoneIndex != Current.ZoneIndex)
	{
		// An entry frame. AdvanceDwell from zero rather than assigning the delta
		// directly, so the per-frame clamp applies here too — a hitch on the frame
		// the pawn arrives is the easiest way to lose it.
		Next.Seconds = AdvanceDwell(0.f, /*bInsideZone*/ true, DeltaSeconds);
		return Next;
	}

	Next.Seconds = AdvanceDwell(Current.Seconds, /*bInsideZone*/ true, DeltaSeconds);
	return Next;
}

int32 SarkoExtract::FindZoneContaining(const FVector& PawnLocation, const TArray<FSarkoExtractionSpot>& Zones)
{
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
	{
		const FSarkoExtractionSpot& Zone = Zones[Index];
		const FVector2D Flat(PawnLocation.X - Zone.Location.X, PawnLocation.Y - Zone.Location.Y);
		if (Flat.SizeSquared() <= Zone.RadiusUU * Zone.RadiusUU)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool SarkoExtract::IsZoneOpen(float OpensAfterSeconds, float ElapsedSeconds)
{
	// The boundary belongs to open. Also true for the ordinary case: zero is the
	// default and every elapsed time is >= it, including the very first frame.
	return ElapsedSeconds >= OpensAfterSeconds;
}

float SarkoExtract::SecondsUntilOpen(float OpensAfterSeconds, float ElapsedSeconds)
{
	return FMath::Max(0.f, OpensAfterSeconds - ElapsedSeconds);
}

namespace
{
	/** Extraction green, per the ТЗ §14 palette. Flat and unmistakable from above. */
	const FLinearColor PadTint(0.16f, 0.62f, 0.24f);

	/** 4 uu thin, so the pawn walks over it rather than onto it. */
	constexpr float PadHalfHeight = 2.f;
}

ASarkoExtractionZone::ASarkoExtractionZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
	SetRootComponent(Pad);
}

void ASarkoExtractionZone::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cylinder || !BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoExtractionZone %d: engine cylinder or basic material missing; the zone is invisible"),
			ZoneIndex);
		return;
	}

	// Movable -> assign -> Static. See SpawnMeshBox in SarkoMapBuilder.cpp:
	// SetStaticMesh silently no-ops on a Static component after BeginPlay.
	Pad->SetMobility(EComponentMobility::Movable);
	Pad->SetStaticMesh(Cylinder);
	Pad->SetWorldScale3D(FVector(RadiusUU / 50.f, RadiusUU / 50.f, PadHalfHeight / 50.f));
	// No collision at all: the dwell is decided by the game mode against the map
	// definition, and a collision volume here would be a second source of truth
	// that could disagree with it.
	Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pad->SetMobility(EComponentMobility::Static);

	if (UMaterialInstanceDynamic* Material = Pad->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMaterial))
	{
		Material->SetVectorParameterValue(TEXT("Color"), PadTint);
		Material->SetVectorParameterValue(TEXT("BaseColor"), PadTint);
	}
}

void ASarkoExtractionZone::SetupFromSpot(int32 InIndex, const FString& InName, float InRadiusUU)
{
	ZoneIndex = InIndex;
	ZoneName = InName;
	RadiusUU = FMath::Max(50.f, InRadiusUU);
}
