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
	 * Extra push past the required clearance distance so the result is
	 * strictly clear (> 0), not merely touching the boundary (== 0).
	 */
	constexpr float ClearanceEpsilonUU = 1.f;

	/** Bounds the displacement loop below. Pure geometry, no sampling, so this is cheap. */
	constexpr int32 MaxDisplacementSteps = 64;

	/**
	 * Index of the cover block nearest to violating Candidate's clearance
	 * (the most negative value), plus that clearance. INDEX_NONE if Cover is
	 * empty. Positive when Candidate clears every block's footprint plus the
	 * safety margin; the more positive, the further from the nearest cover.
	 */
	int32 NearestBlockIndex(const FVector& Candidate, const TArray<FSarkoCoverBlock>& Cover, float& OutClearance)
	{
		int32 BestIndex = INDEX_NONE;
		OutClearance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Cover.Num(); ++Index)
		{
			const float Clearance = FVector::Dist2D(Candidate, Cover[Index].Location) - (Cover[Index].Extent.GetMax() + SpawnClearanceUU);
			if (Clearance < OutClearance)
			{
				OutClearance = Clearance;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	float NearestCoverClearance(const FVector& Candidate, const TArray<FSarkoCoverBlock>& Cover)
	{
		float Clearance = TNumericLimits<float>::Max();
		NearestBlockIndex(Candidate, Cover, Clearance);
		return Clearance;
	}

	/**
	 * Pushes Candidate directly away from Block's center, along the 2D
	 * separating axis, to exactly the distance that clears Block's footprint
	 * plus the safety margin (with a small epsilon so it is strictly clear).
	 * Deterministic: pure geometry derived from Candidate and Block, no
	 * randomness. If Candidate sits exactly on Block's center the direction is
	 * undefined, so it pushes along +X — as good as any other axis when the
	 * starting distance is zero.
	 */
	FVector DisplaceClearOfBlock(const FVector& Candidate, const FSarkoCoverBlock& Block)
	{
		FVector Direction = Candidate - Block.Location;
		Direction.Z = 0.f;
		if (Direction.IsNearlyZero())
		{
			Direction = FVector(1.f, 0.f, 0.f);
		}
		Direction.Normalize();

		const float RequiredDistance = Block.Extent.GetMax() + SpawnClearanceUU + ClearanceEpsilonUU;
		FVector Result = Block.Location + Direction * RequiredDistance;
		Result.Z = Candidate.Z;
		return Result;
	}

	/**
	 * Rejection-samples a point clear of cover. If every attempt lands inside a
	 * block's safety margin, this must still return a point that clears every
	 * block — a fallback that only "improves the odds" (e.g. the least-bad
	 * candidate seen) can still sit inside a cover box, which is exactly the
	 * "spawn inside a wall" bug this generator must avoid. So instead it takes
	 * the least-bad candidate and deterministically displaces it away from
	 * whichever block it is currently violating, repeating against the next
	 * violator (if the push moved it into another block's margin) until it is
	 * clear of all of them or the step budget runs out.
	 */
	FVector PickClearPoint(FRandomStream& Stream, float Extent, const TArray<FSarkoCoverBlock>& Cover, int32 Seed)
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

		// Exhausted every attempt. A map dense enough to hit this path is
		// something the designer tuning CoverCount needs told about, not a
		// silent statistical near-miss.
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoMap: PickClearPoint exhausted all %d attempts for seed %d; displacing the fallback point clear of cover"),
			ClearPointAttempts, Seed);

		FVector Candidate = BestCandidate;
		for (int32 Step = 0; Step < MaxDisplacementSteps; ++Step)
		{
			float Clearance = TNumericLimits<float>::Max();
			const int32 BlockIndex = NearestBlockIndex(Candidate, Cover, Clearance);
			if (BlockIndex == INDEX_NONE || Clearance > 0.f)
			{
				break;
			}
			Candidate = DisplaceClearOfBlock(Candidate, Cover[BlockIndex]);
		}
		return Candidate;
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
		Layout.PlayerStarts.Add(PickClearPoint(Stream, Settings.MapExtent * 0.8f, Layout.Cover, Seed));
	}
	for (int32 Index = 0; Index < EnemySpawnCount; ++Index)
	{
		Layout.EnemySpawns.Add(PickClearPoint(Stream, Settings.MapExtent * 0.9f, Layout.Cover, Seed));
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
