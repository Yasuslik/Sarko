#include "AI/SarkoAIController.h"

#include "AI/SarkoNoise.h"
#include "Combat/SarkoWeapon.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

ESarkoAIState SarkoAI::DecideState(
	ESarkoAIState Current,
	bool bHasTarget,
	float DistanceToTarget,
	bool bHasLineOfSight,
	float SightRangeUU,
	float FiringRange,
	float ShootHysteresisRangeUU,
	bool bInvestigationActive)
{
	// THE LINE-OF-SIGHT GATE, now the only perception this function has. A bot
	// that cannot see its target reacts to exactly one thing: a noise the caller
	// heard for it (spec §7). It does not chase — a bot closing on a target it
	// cannot see is a bot aggroing through a wall (ТЗ §11) — and it certainly
	// does not shoot.
	//
	// What is deliberately NOT here any more: a hearing radius. This used to read
	// `DistanceToTarget > HearingRadius` and return Patrol, which made being near
	// a bot the same thing as being heard by it, whether the player was sprinting
	// or standing perfectly still. Proximity is not a sound.
	const bool bSeen = bHasTarget && bHasLineOfSight && DistanceToTarget <= SightRangeUU;
	if (!bSeen)
	{
		return bInvestigationActive ? ESarkoAIState::Investigate : ESarkoAIState::Patrol;
	}

	// Seen and close enough to hit: shoot. Already shooting tolerates
	// drifting up to ShootHysteresisRangeUU past FiringRange before giving
	// it up — the hysteresis band that stops the state chattering
	// Chase<->Shoot every tick right at the boundary. Entering Shoot from
	// any other state still requires the plain FiringRange, unwidened.
	const float EffectiveFiringRange = (Current == ESarkoAIState::Shoot)
		? FiringRange + ShootHysteresisRangeUU
		: FiringRange;

	if (DistanceToTarget <= EffectiveFiringRange)
	{
		return ESarkoAIState::Shoot;
	}
	return ESarkoAIState::Chase;
}

FVector SarkoAI::PatrolPointInLeash(const FVector& PostPos, float LeashUU, float AngleRand01, float RadiusRand01)
{
	// A non-positive leash is a bot pinned to its post rather than a bot with
	// the run of the map — the old behaviour on a bad number was the latter.
	const float Leash = FMath::Max(0.f, LeashUU);
	const float Angle = FMath::Clamp(AngleRand01, 0.f, 1.f) * 2.f * PI;
	const float Radius = Leash * FMath::Sqrt(FMath::Clamp(RadiusRand01, 0.f, 1.f));
	return FVector(
		PostPos.X + FMath::Cos(Angle) * Radius,
		PostPos.Y + FMath::Sin(Angle) * Radius,
		PostPos.Z);
}

FVector2D SarkoAI::ComputeSteerDirection(
	FVector2D DesiredDirection,
	bool bForwardBlocked,
	float AvoidanceSteerDegrees)
{
	if (!bForwardBlocked || DesiredDirection.IsNearlyZero())
	{
		return DesiredDirection;
	}

	// Rotate the desired direction to the side by a fixed angle so the enemy
	// curves around whatever the forward trace hit instead of pushing
	// straight into it and stalling in place — a single fixed rotation is
	// enough to clear one box of cover on a flat plane.
	const float Radians = FMath::DegreesToRadians(AvoidanceSteerDegrees);
	const float CosA = FMath::Cos(Radians);
	const float SinA = FMath::Sin(Radians);
	const FVector2D Rotated(
		DesiredDirection.X * CosA - DesiredDirection.Y * SinA,
		DesiredDirection.X * SinA + DesiredDirection.Y * CosA);
	return Rotated.GetSafeNormal();
}

float SarkoAI::ChooseSteerSign(FVector2D DesiredDirection, FVector2D ImpactNormal2D)
{
	// The 2D perp-dot product's sign tells us which side of the desired
	// direction the impact normal leans toward; steering toward the opposite
	// side is steering toward the side with more room. A fixed rotation
	// (the old behaviour) ignores this entirely and can just as easily turn
	// into more geometry as away from it.
	const float Cross = DesiredDirection.X * ImpactNormal2D.Y - DesiredDirection.Y * ImpactNormal2D.X;
	return Cross >= 0.f ? 1.f : -1.f;
}

ASarkoAIController::ASarkoAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASarkoAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// A bot whose map data authored no post takes the place it was put down as
	// its post. That is the whole fix for "every bot walks at the world origin
	// on frame one": there is now always a post, and the initial patrol target
	// is it. SetPost may already have run (the encounter director calls it right
	// after SpawnActor, which is before possession in the placed-or-spawned auto
	// possess path is *not* guaranteed) — so only fill in what nobody authored.
	if (InPawn && AuthoredLeashUU < 0.f && PostPos.IsNearlyZero())
	{
		PostPos = InPawn->GetActorLocation();
		PatrolTarget = PostPos;
	}
}

void ASarkoAIController::SetPost(const FVector& InPostPos, float InLeashUU)
{
	PostPos = InPostPos;
	AuthoredLeashUU = InLeashUU;
	// The initial target IS the post. Not FVector::ZeroVector, which is the
	// world origin and, on the shipped map, the closed bridge.
	PatrolTarget = PostPos;
}

float ASarkoAIController::GetLeashUU() const
{
	return AuthoredLeashUU >= 0.f ? AuthoredLeashUU : GetDefault<USarkoRaidSettings>()->AIPatrolLeashUU;
}

void ASarkoAIController::SetPerception(float InHearingSensitivity, float InFiringRangeUU, float InFireIntervalSeconds)
{
	HearingSensitivityOverride = InHearingSensitivity;
	FiringRangeOverrideUU = InFiringRangeUU;
	FireIntervalOverrideSeconds = InFireIntervalSeconds;
}

APawn* ASarkoAIController::FindNearestLivingPlayer() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Candidate = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (!Candidate)
		{
			continue;
		}
		const USarkoHealthComponent* Health = Candidate->FindComponentByClass<USarkoHealthComponent>();
		if (!Health || Health->IsDead())
		{
			continue;
		}
		if (const APawn* Self = GetPawn())
		{
			const float DistSq = FVector::DistSquared(Self->GetActorLocation(), Candidate->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Candidate;
			}
		}
	}
	return Nearest;
}

void ASarkoAIController::SteerToward(const FVector& TargetLocation, const USarkoRaidSettings& Settings, bool bLogThisTick)
{
	APawn* Self = GetPawn();
	UWorld* World = GetWorld();
	if (!Self || !World)
	{
		return;
	}

	const FVector ToTarget = TargetLocation - Self->GetActorLocation();
	const FVector2D DesiredDirection = FVector2D(ToTarget.X, ToTarget.Y).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		return;
	}

	// There is no navmesh in this project — the map's actors are spawned from a
	// data file at runtime rather than authored into a level, so nothing is
	// baked, and nothing configures runtime generation either — so
	// MoveToLocation/MoveToActor always fail and would
	// leave the enemy standing still forever. Steer straight at the target
	// instead, and probe ahead with a swept capsule rather than a line from
	// the capsule centre — a line reports "clear" even when geometry clips
	// the pawn's shoulder, since the pawn's own collision capsule is roughly
	// AIAvoidanceProbeRadiusUU wide.
	float ProbeHalfHeight = Settings.AIAvoidanceProbeRadiusUU;
	if (const ACharacter* Character = Cast<ACharacter>(Self))
	{
		ProbeHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	const FCollisionShape ProbeShape = FCollisionShape::MakeCapsule(Settings.AIAvoidanceProbeRadiusUU, ProbeHalfHeight);
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SarkoAIAvoidance), /*bTraceComplex*/ false, Self);
	const FVector TraceStart = Self->GetActorLocation();

	const auto ProbeDirection = [&](const FVector2D& Direction, FHitResult& OutHit) -> bool
	{
		const FVector TraceEnd = TraceStart + FVector(Direction, 0.f) * Settings.AIAvoidanceTraceDistanceUU;
		return World->SweepSingleByChannel(OutHit, TraceStart, TraceEnd, FQuat::Identity, ECC_WorldStatic, ProbeShape, QueryParams);
	};

	FHitResult Hit;
	const bool bForwardBlocked = ProbeDirection(DesiredDirection, Hit);

	FVector2D SteerDirection = DesiredDirection;
	bool bFoundClearDirection = !bForwardBlocked;

	if (bForwardBlocked)
	{
		// Re-trace the chosen direction instead of trusting one fixed
		// rotation blindly: use the impact normal to pick a side, try that
		// side, then the other, then both again at a wider angle. Bounded to
		// four extra sweeps (two angles x two sides) so this can never loop.
		const FVector2D ImpactNormal2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y);
		const float ChosenSign = SarkoAI::ChooseSteerSign(DesiredDirection, ImpactNormal2D);

		const float CandidateAngles[] = { Settings.AIAvoidanceSteerDegrees, Settings.AIAvoidanceWideSteerDegrees };
		for (const float Angle : CandidateAngles)
		{
			for (const float SideSign : { ChosenSign, -ChosenSign })
			{
				const FVector2D Candidate = SarkoAI::ComputeSteerDirection(DesiredDirection, true, SideSign * Angle);
				FHitResult SideHit;
				if (!ProbeDirection(Candidate, SideHit))
				{
					SteerDirection = Candidate;
					bFoundClearDirection = true;
					break;
				}
			}
			if (bFoundClearDirection)
			{
				break;
			}
		}
	}

	if (bLogThisTick)
	{
		UE_LOG(LogTemp, Log, TEXT("SarkoAI: loc=%s target=%s blocked=%d clear=%d steer=%s"),
			*Self->GetActorLocation().ToString(), *TargetLocation.ToString(), bForwardBlocked, bFoundClearDirection, *SteerDirection.ToString());
	}

	// Every bounded attempt this tick was blocked: do not push against
	// geometry. The stuck detector in Tick is the backstop that guarantees
	// this can never be a permanent freeze even so.
	if (bFoundClearDirection)
	{
		Self->AddMovementInput(FVector(SteerDirection, 0.f), 1.f);
	}
}

void ASarkoAIController::RerollPatrolTarget(const APawn& Self, const USarkoRaidSettings& Settings)
{
	// Inside the leash, around the post. This used to pick uniformly inside
	// +/-MapExtent*0.8 — a 320x320 m square covering the closed two-thirds, the
	// player's spawn shelf and the extraction — which, with the stuck detector
	// re-rolling every 2 s, is why no authored bot position survived 90 seconds.
	const FVector Post(PostPos.X, PostPos.Y, Self.GetActorLocation().Z);
	PatrolTarget = SarkoAI::PatrolPointInLeash(Post, GetLeashUU(), FMath::FRand(), FMath::FRand());

	if (Settings.bLogAIDiagnostics)
	{
		UE_LOG(LogTemp, Log, TEXT("SarkoAI: patrol target %s, %.0f uu from post %s (leash %.0f)"),
			*PatrolTarget.ToString(), FVector::Dist2D(PatrolTarget, Post), *Post.ToString(), GetLeashUU());
	}
}

void ASarkoAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Self = GetPawn();
	if (!Self || !Self->HasAuthority())
	{
		return;
	}

	const USarkoHealthComponent* SelfHealth = Self->FindComponentByClass<USarkoHealthComponent>();
	if (SelfHealth && SelfHealth->IsDead())
	{
		return;
	}

	// The bots stand down the moment the raid has an outcome, and visibly so —
	// no steering, no state changes, no fire. USarkoHealthComponent::ApplyDamage
	// already refuses the hits, but a squad still tracking and shooting a player
	// frozen on the extraction pad under a summary screen reads as a bug even when
	// nothing takes damage, and a bot that keeps aiming is one refactor away from
	// one that keeps hurting.
	UWorld* World = GetWorld();
	const ASarkoRaidGameState* RaidState = World ? World->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	FireCooldown = FMath::Max(0.f, FireCooldown - DeltaSeconds);

	APawn* Target = FindNearestLivingPlayer();
	const float Distance = Target ? FVector::Dist(Self->GetActorLocation(), Target->GetActorLocation()) : 0.f;
	const bool bLineOfSight = Target ? LineOfSightTo(Target) : false;

	const float HearingSensitivity = HearingSensitivityOverride > 0.f ? HearingSensitivityOverride : Settings.EnemyHearingSensitivity;
	const float FiringRangeUU = FiringRangeOverrideUU > 0.f ? FiringRangeOverrideUU : Settings.EnemyFiringRangeUU;
	const float FireIntervalSeconds = FireIntervalOverrideSeconds > 0.f ? FireIntervalOverrideSeconds : Settings.EnemyFireIntervalSeconds;

	// HEARING (spec §7). A noise event, not a proximity test — and the position
	// investigated is the EVENT's, never the target's live one. That is the
	// difference between hunting a sound and wallhacking: a player who fires and
	// then moves is walked to where they fired from, and a player who walks past
	// at 900 uu emits a 450 uu event that nobody hears at all.
	//
	// Only while the bot cannot see the target: the moment it can, Chase and
	// Shoot take over and the investigation is moot. Note that the listener is
	// this bot's PAWN, so its own gunshots are filtered out of its own hearing.
	if (!bLineOfSight)
	{
		if (USarkoNoiseSubsystem* Noise = World ? World->GetSubsystem<USarkoNoiseSubsystem>() : nullptr)
		{
			SarkoNoise::FNoiseEvent Heard;
			if (Noise->Hear(Self->GetActorLocation(), HearingSensitivity, Self, Heard))
			{
				// A fresh event restarts the clock as well as moving the target: a
				// player who keeps making audible noise keeps being followed, and one
				// who goes quiet has AIInvestigateTimeoutSeconds to be somewhere else.
				// The bound is the noise radius, not the bot's patience alone.
				InvestigateTarget = Heard.Location;
				InvestigateSeconds = 0.f;
				if (!bInvestigating)
				{
					bInvestigating = true;
					if (Settings.bLogAIDiagnostics)
					{
						UE_LOG(LogTemp, Log, TEXT("SarkoAI: heard something at %s (radius %.0f uu, sensitivity %.2f) — investigating the EVENT position, not the player"),
							*Heard.Location.ToString(), Heard.RadiusUU, HearingSensitivity);
					}
				}
			}
		}
	}

	State = SarkoAI::DecideState(State, Target != nullptr, Distance, bLineOfSight,
		Settings.EnemySightRangeUU, FiringRangeUU, Settings.AIShootHysteresisRangeUU, bInvestigating);

	DebugLogAccum += DeltaSeconds;
	const bool bLogThisTick = Settings.bLogAIDiagnostics && DebugLogAccum > 1.f;
	if (bLogThisTick)
	{
		DebugLogAccum = 0.f;
	}

	// Stuck detector: a backstop against any steering failure, independent of
	// *why* the pawn has not moved. Patrol's own re-roll only fires once the
	// pawn gets within 200uu of PatrolTarget, so an enemy wedged 3000uu away
	// from an unreachable target would otherwise never pick a new one and
	// freeze for the rest of the raid.
	if (!bStuckReferenceInitialised)
	{
		StuckReferenceLocation = Self->GetActorLocation();
		bStuckReferenceInitialised = true;
	}

	bool bForceFallbackToPatrol = false;
	const float DisplacementFromReference = FVector::Dist2D(Self->GetActorLocation(), StuckReferenceLocation);
	if (DisplacementFromReference > Settings.AIStuckDisplacementThresholdUU)
	{
		StuckReferenceLocation = Self->GetActorLocation();
		StuckSeconds = 0.f;
	}
	else
	{
		StuckSeconds += DeltaSeconds;
		if (StuckSeconds >= Settings.AIStuckTimeThresholdSeconds)
		{
			RerollPatrolTarget(*Self, Settings);
			bForceFallbackToPatrol = true;
			StuckReferenceLocation = Self->GetActorLocation();
			StuckSeconds = 0.f;

			if (bLogThisTick || Settings.bLogAIDiagnostics)
			{
				UE_LOG(LogTemp, Log, TEXT("SarkoAI: stuck detector fired at loc=%s, new PatrolTarget=%s"),
					*Self->GetActorLocation().ToString(), *PatrolTarget.ToString());
			}
		}
	}

	// A stuck Chase or Investigate falls back to Patrol for this tick so it
	// steers toward the fresh wander point instead of straight at the same
	// blocked geometry it was already failing to get around.
	const ESarkoAIState EffectiveState =
		(bForceFallbackToPatrol && (State == ESarkoAIState::Chase || State == ESarkoAIState::Investigate))
			? ESarkoAIState::Patrol
			: State;

	switch (EffectiveState)
	{
	case ESarkoAIState::Patrol:
		if (FVector::Dist2D(Self->GetActorLocation(), PatrolTarget) < 200.f)
		{
			RerollPatrolTarget(*Self, Settings);
		}
		SteerToward(PatrolTarget, Settings, bLogThisTick);
		break;

	case ESarkoAIState::Investigate:
	{
		// Walk to the noise, once. Arriving and finding nothing, or running out
		// of patience, both end the same way: the investigation is dropped and
		// the patrol target is reset to the post, so the next tick walks home.
		InvestigateSeconds += DeltaSeconds;
		const bool bArrived = FVector::Dist2D(Self->GetActorLocation(), InvestigateTarget) <= Settings.AIInvestigateArriveRadiusUU;
		const bool bGaveUp = InvestigateSeconds >= Settings.AIInvestigateTimeoutSeconds;
		if (bArrived || bGaveUp)
		{
			bInvestigating = false;
			InvestigateSeconds = 0.f;
			PatrolTarget = FVector(PostPos.X, PostPos.Y, Self->GetActorLocation().Z);
			if (Settings.bLogAIDiagnostics)
			{
				UE_LOG(LogTemp, Log, TEXT("SarkoAI: investigation over (%s), returning to post %s"),
					bArrived ? TEXT("arrived, nothing there") : TEXT("gave up"), *PostPos.ToString());
			}
			SteerToward(PatrolTarget, Settings, bLogThisTick);
			break;
		}
		SteerToward(InvestigateTarget, Settings, bLogThisTick);
		break;
	}

	case ESarkoAIState::Chase:
		if (Target)
		{
			SteerToward(Target->GetActorLocation(), Settings, bLogThisTick);
		}
		break;

	case ESarkoAIState::Shoot:
		// No movement input this tick — direct steering has no path-following
		// component to stop, so standing still while shooting just means not
		// calling AddMovementInput; the character's own braking deceleration
		// handles the rest.
		//
		// bLineOfSight is re-asserted here rather than trusted from DecideState:
		// the firing path is the one place where being wrong means shooting
		// through a wall from off the player's screen, and one gate in one pure
		// function is one refactor away from not being a gate at all.
		if (Target && bLineOfSight && Distance <= FiringRangeUU + Settings.AIShootHysteresisRangeUU && FireCooldown <= 0.f)
		{
			if (USarkoWeaponComponent* Weapon = Self->FindComponentByClass<USarkoWeaponComponent>())
			{
				const FVector Origin = Self->GetActorLocation() + FVector(0.f, 0.f, 40.f);
				const FVector Direction = (Target->GetActorLocation() - Origin).GetSafeNormal();
				if (!Weapon->CanFire())
				{
					Weapon->StartReload();
				}
				else
				{
					Weapon->ServerFire(Origin, Direction);
					FireCooldown = FireIntervalSeconds;
				}
			}
		}
		break;

	default:
		break;
	}
}
