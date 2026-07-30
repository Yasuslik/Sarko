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
	 * The deterministic lattice fallback (see LatticeClearPoint) samples at
	 * this fraction of the largest cover block's extent — fine enough that a
	 * grid point can land in a gap between two blocks whose footprints are
	 * roughly that size.
	 */
	constexpr float LatticeStrideFraction = 0.5f;

	/**
	 * Hard cap on lattice steps per axis, independent of MapExtent, so a huge
	 * map with small cover cannot make the fallback scan pathological. Total
	 * samples are bounded by the square of this.
	 */
	constexpr int32 MaxLatticeStepsPerAxis = 64;

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
	 * The actual guarantee PickClearPoint must deliver: true only if Candidate
	 * clears every block's footprint plus the safety margin. Extracted so the
	 * post-loop verification below and the lattice fallback share one
	 * definition of "clear" instead of two copies of the same comparison.
	 */
	bool ClearsAllCover(const FVector& Candidate, const TArray<FSarkoCoverBlock>& Cover)
	{
		return Cover.Num() == 0 || NearestCoverClearance(Candidate, Cover) > 0.f;
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
	 * Deterministic last resort for when displacement fails to converge (it
	 * can oscillate if it keeps bouncing between two overlapping exclusion
	 * zones instead of escaping them). Walks a fixed grid over
	 * [-Extent, Extent]^2 and returns the first point that clears every
	 * block. The stride is derived from the largest cover extent so the
	 * lattice is fine enough to land in a gap between blocks when one
	 * exists, and the step count per axis is capped at MaxLatticeStepsPerAxis
	 * so a large MapExtent cannot make the scan unbounded. Consumes no
	 * FRandomStream draws — it only reads Cover, which was already sampled —
	 * so it cannot perturb the seeded sequence that makes layouts
	 * reproducible. If no lattice point clears every block either, returns
	 * the least-bad point seen, the same contract PickClearPoint's own
	 * sampling loop follows.
	 */
	FVector LatticeClearPoint(float Extent, const TArray<FSarkoCoverBlock>& Cover)
	{
		float LargestCoverExtent = 0.f;
		for (const FSarkoCoverBlock& Block : Cover)
		{
			LargestCoverExtent = FMath::Max(LargestCoverExtent, Block.Extent.GetMax());
		}

		const float DesiredStride = FMath::Max(LargestCoverExtent * LatticeStrideFraction, KINDA_SMALL_NUMBER);
		const int64 DesiredStepsPerAxis = FMath::CeilToInt64((2.0 * Extent) / DesiredStride) + 1;
		const int32 StepsPerAxis = static_cast<int32>(FMath::Clamp<int64>(DesiredStepsPerAxis, 2, MaxLatticeStepsPerAxis));
		const float Stride = (2.f * Extent) / static_cast<float>(StepsPerAxis - 1);

		FVector BestCandidate(0.f, 0.f, 100.f);
		float BestClearance = -TNumericLimits<float>::Max();

		for (int32 XStep = 0; XStep < StepsPerAxis; ++XStep)
		{
			const float X = -Extent + Stride * XStep;
			for (int32 YStep = 0; YStep < StepsPerAxis; ++YStep)
			{
				const float Y = -Extent + Stride * YStep;
				const FVector Candidate(X, Y, 100.f);

				const float Clearance = NearestCoverClearance(Candidate, Cover);
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
		}
		return BestCandidate;
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
	 *
	 * A clear point cannot be guaranteed to exist — if cover is dense enough
	 * to saturate the whole sampling square, there is no point that clears
	 * everything. So the loop's outcome is always verified against
	 * ClearsAllCover: if displacement itself did not converge (it can
	 * oscillate between overlapping exclusion zones), a deterministic lattice
	 * scan of the sampling square is tried before giving up. If even that
	 * finds nothing, the configuration is genuinely unsatisfiable, which is
	 * logged at Error level (naming the seed, cover count and map extent) so
	 * a designer who pushed CoverCount too high is told, not silently handed
	 * a map with a spawn point inside geometry.
	 */
	FVector PickClearPoint(FRandomStream& Stream, float SampleExtent, const TArray<FSarkoCoverBlock>& Cover, int32 Seed, float MapExtent)
	{
		FVector BestCandidate(0.f, 0.f, 100.f);
		float BestClearance = -TNumericLimits<float>::Max();

		for (int32 Attempt = 0; Attempt < ClearPointAttempts; ++Attempt)
		{
			const FVector Candidate(
				Stream.FRandRange(-SampleExtent, SampleExtent),
				Stream.FRandRange(-SampleExtent, SampleExtent),
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

		// Sampling exhausted. Push the least-bad candidate clear of whichever
		// block it violates, repeating against the next violator, until it is
		// clear of all of them or the step budget runs out.
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

		if (ClearsAllCover(Candidate, Cover))
		{
			UE_LOG(LogTemp, Log,
				TEXT("SarkoMap: PickClearPoint exhausted %d sampling attempts for seed %d; displacement converged and the spawn point clears all %d cover blocks"),
				ClearPointAttempts, Seed, Cover.Num());
			return Candidate;
		}

		// Displacement did not converge on its own. Fall back to a
		// deterministic lattice scan of the same sampling square before
		// giving up — this consumes no random draws, so it cannot affect
		// reproducibility.
		const FVector LatticePoint = LatticeClearPoint(SampleExtent, Cover);
		if (ClearsAllCover(LatticePoint, Cover))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SarkoMap: PickClearPoint displacement did not converge for seed %d; the deterministic lattice scan found a clear spawn point instead"),
				Seed);
			return LatticePoint;
		}

		// Neither displacement nor the lattice scan found a point clear of
		// every block: this cover configuration genuinely leaves no clear
		// spawn point. That is a map-design problem, not a bug to hide, so
		// surface it loudly and hand back whichever candidate is least bad.
		const FVector BestOfBoth =
			NearestCoverClearance(LatticePoint, Cover) > NearestCoverClearance(Candidate, Cover) ? LatticePoint : Candidate;

		UE_LOG(LogTemp, Error,
			TEXT("SarkoMap: PickClearPoint found no point clear of cover for seed %d — %d cover blocks in a %.0fuu map extent leave no clear spawn point; returning the best available candidate, which may overlap cover geometry"),
			Seed, Cover.Num(), MapExtent);
		return BestOfBoth;
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
		Layout.PlayerStarts.Add(PickClearPoint(Stream, Settings.MapExtent * 0.8f, Layout.Cover, Seed, Settings.MapExtent));
	}
	for (int32 Index = 0; Index < EnemySpawnCount; ++Index)
	{
		Layout.EnemySpawns.Add(PickClearPoint(Stream, Settings.MapExtent * 0.9f, Layout.Cover, Seed, Settings.MapExtent));
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
