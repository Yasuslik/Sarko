#include "Loot/SarkoExtractionZone.h"

#include "Components/StaticMeshComponent.h"
#include "Core/SarkoRaidGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
	const FLinearColor PadOpenTint(0.16f, 0.62f, 0.24f);

	/**
	 * A closed pad (spec §4.5). The same grey the HUD's closed banner uses, so
	 * the ground and the label agree — and deliberately not a dark green, which
	 * from a top-down camera at 1400 uu reads as green.
	 */
	const FLinearColor PadClosedTint(0.30f, 0.31f, 0.30f);

	/** 4 uu thin, so the pawn walks over it rather than onto it. */
	constexpr float PadHalfHeight = 2.f;

	/**
	 * How often a closed pad asks whether it has opened yet.
	 *
	 * Twice a second, against a countdown displayed in whole seconds: the pad
	 * cannot turn green visibly later than the banner it sits under. Cheap
	 * regardless — one float comparison, on at most a handful of actors, and only
	 * while any of them is still closed.
	 */
	constexpr float ExtractionStateTickSeconds = 0.5f;
}

ASarkoExtractionZone::ASarkoExtractionZone()
{
	// Ticking is decided in BeginPlay, once the zone knows whether it has
	// anything to wait for. A pad that is open from the first frame never ticks.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = ExtractionStateTickSeconds;
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

	// One dynamic instance per pad, made here and repainted through
	// ApplyStateTint afterwards — the pads share a material asset, so a tint
	// applied to anything but a per-actor instance would recolour every zone on
	// the map together.
	Pad->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMaterial);

	// The opening state at spawn. A zone with no `opensAfterSeconds` — every
	// extraction on this map but the west cordon — is open from the first frame
	// and is painted green once, here, and never looked at again.
	const bool bOpenNow = SarkoExtract::IsZoneOpen(OpensAfterSeconds, 0.f);
	ApplyStateTint(bOpenNow);
	SetActorTickEnabled(!bOpenNow);
}

void ASarkoExtractionZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Presentation, never authority: ASarkoRaidGameMode::ExtractTick decides
	// whether a dwell accrues, from its own clock. This derives the same elapsed
	// time the HUD's banner derives, from the same replicated RemainingSeconds and
	// the same map duration, so the ground and the label cannot say different
	// things about the same second.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState)
	{
		return;
	}

	const float Elapsed = FMath::Max(0.f, RaidDurationSeconds - RaidState->RemainingSeconds);
	if (SarkoExtract::IsZoneOpen(OpensAfterSeconds, Elapsed))
	{
		ApplyStateTint(true);
		// Nothing closes again, so there is nothing left to watch for.
		SetActorTickEnabled(false);
	}
}

void ASarkoExtractionZone::ApplyStateTint(bool bOpen)
{
	if (!Pad)
	{
		return;
	}
	// Idempotent by the flag rather than by the material: SetVectorParameterValue
	// on an unchanged value still dirties the instance's uniform buffer, and this
	// runs from a tick.
	if (bPaintedOpen == bOpen && bOpen)
	{
		return;
	}
	bPaintedOpen = bOpen;

	const FLinearColor Tint = bOpen ? PadOpenTint : PadClosedTint;
	if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Pad->GetMaterial(0)))
	{
		// Both names, as before: /Engine/BasicShapes' material exposes one of them
		// and which one has moved between engine versions.
		Material->SetVectorParameterValue(TEXT("Color"), Tint);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Tint);
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoExtractionZone %d ('%s'): pad drawn %s (opens after %.0f s)"),
		ZoneIndex, *ZoneName, bOpen ? TEXT("GREEN — open") : TEXT("GREY — closed"), OpensAfterSeconds);
}

void ASarkoExtractionZone::SetupFromSpot(int32 InIndex, const FString& InName, float InRadiusUU,
	float InOpensAfterSeconds, float InRaidDurationSeconds)
{
	ZoneIndex = InIndex;
	ZoneName = InName;
	RadiusUU = FMath::Max(50.f, InRadiusUU);
	OpensAfterSeconds = FMath::Max(0.f, InOpensAfterSeconds);
	RaidDurationSeconds = FMath::Max(0.f, InRaidDurationSeconds);
}
