#include "Map/SarkoMapBuilder.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Spawn points are pushed this far from any cover so nobody starts in a wall. */
	constexpr float SpawnClearanceUU = 600.f;

	constexpr int32 PlayerStartCount = 4;
	constexpr int32 EnemySpawnCount = 8;
	constexpr int32 ClearPointAttempts = 64;

	/**
	 * Positive when Candidate clears every block's footprint plus the safety
	 * margin; the more positive, the further from the nearest cover.
	 */
	float NearestCoverClearance(const FVector& Candidate, const TArray<FSarkoCoverBlock>& Cover)
	{
		float MinClearance = TNumericLimits<float>::Max();
		for (const FSarkoCoverBlock& Block : Cover)
		{
			const float Clearance = FVector::Dist2D(Candidate, Block.Location) - (Block.Extent.GetMax() + SpawnClearanceUU);
			MinClearance = FMath::Min(MinClearance, Clearance);
		}
		return MinClearance;
	}

	/**
	 * Rejection-samples a point clear of cover. If every attempt lands inside a
	 * block's safety margin, falls back to the least-bad candidate seen instead
	 * of a fixed point — a fixed fallback could itself land inside a cover box,
	 * which is exactly the "spawn inside a wall" bug this generator must avoid.
	 */
	FVector PickClearPoint(FRandomStream& Stream, float Extent, const TArray<FSarkoCoverBlock>& Cover)
	{
		FVector BestCandidate(0.f, 0.f, 100.f);
		float BestClearance = -TNumericLimits<float>::Max();

		for (int32 Attempt = 0; Attempt < ClearPointAttempts; ++Attempt)
		{
			const FVector Candidate(
				Stream.FRandRange(-Extent, Extent),
				Stream.FRandRange(-Extent, Extent),
				100.f);

			const float Clearance = Cover.Num() > 0 ? NearestCoverClearance(Candidate, Cover) : TNumericLimits<float>::Max();
			if (Clearance > 0.f)
			{
				return Candidate;
			}
			if (Clearance > BestClearance)
			{
				BestClearance = Clearance;
				BestCandidate = Candidate;
			}
		}
		return BestCandidate;
	}
}

FSarkoMapLayout SarkoMap::BuildLayout(int32 Seed, const USarkoRaidSettings& Settings)
{
	// FRandomStream, not FMath::Rand: it is explicitly seeded and reproducible
	// across platforms, which the global RNG is not.
	FRandomStream Stream(Seed);

	FSarkoMapLayout Layout;
	Layout.Extent = Settings.MapExtent;
	Layout.Cover.Reserve(Settings.CoverCount);

	for (int32 Index = 0; Index < Settings.CoverCount; ++Index)
	{
		FSarkoCoverBlock Block;
		Block.Location = FVector(
			Stream.FRandRange(-Settings.MapExtent, Settings.MapExtent),
			Stream.FRandRange(-Settings.MapExtent, Settings.MapExtent),
			Stream.FRandRange(100.f, 200.f));
		Block.Rotation = FRotator(0.f, Stream.FRandRange(0.f, 90.f), 0.f);
		Block.Extent = FVector(
			Stream.FRandRange(150.f, 500.f),
			Stream.FRandRange(150.f, 500.f),
			Stream.FRandRange(120.f, 260.f));
		Layout.Cover.Add(Block);
	}

	for (int32 Index = 0; Index < PlayerStartCount; ++Index)
	{
		Layout.PlayerStarts.Add(PickClearPoint(Stream, Settings.MapExtent * 0.8f, Layout.Cover));
	}
	for (int32 Index = 0; Index < EnemySpawnCount; ++Index)
	{
		Layout.EnemySpawns.Add(PickClearPoint(Stream, Settings.MapExtent * 0.9f, Layout.Cover));
	}

	return Layout;
}

void SarkoMap::SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout)
{
	// Engine primitives, referenced by path. Nothing is authored — this is how
	// the slice gets geometry without a single .uasset of our own.
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoMap: engine cube mesh missing; map will be empty"));
		return;
	}

	const auto SpawnBox = [&World, CubeMesh](const FVector& Location, const FRotator& Rotation, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation, Params);
		if (!Actor)
		{
			return;
		}
		Actor->SetMobility(EComponentMobility::Static);
		if (UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent())
		{
			Mesh->SetStaticMesh(CubeMesh);
			// The engine cube is 100 uu across, so scale is extent/50 per axis.
			Mesh->SetWorldScale3D(Extent / 50.f);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	};

	// Floor: one flattened cube covering the play area.
	SpawnBox(FVector(0.f, 0.f, -25.f), FRotator::ZeroRotator, FVector(Layout.Extent, Layout.Extent, 25.f));

	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		SpawnBox(Block.Location, Block.Rotation, Block.Extent);
	}
}
