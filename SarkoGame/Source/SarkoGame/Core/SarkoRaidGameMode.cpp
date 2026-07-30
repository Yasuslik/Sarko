#include "Core/SarkoRaidGameMode.h"

#include "AI/SarkoEnemyCharacter.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Map/SarkoMapBuilder.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "UI/SarkoHUD.h"

ASarkoRaidGameMode::ASarkoRaidGameMode()
{
	// AGameModeBase does not tick by default. Without this the dwell never
	// advances and the clock never expires into MIA — a silent failure where
	// standing in the zone simply does nothing.
	PrimaryActorTick.bCanEverTick = true;

	GameStateClass = ASarkoRaidGameState::StaticClass();
	DefaultPawnClass = ASarkoCharacter::StaticClass();
	PlayerControllerClass = ASarkoPlayerController::StaticClass();
	HUDClass = ASarkoHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ASarkoRaidGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// ?Seed=12345 on the travel URL. Defaults to 1 so a bare PIE session rolls
	// the same way every launch rather than being accidentally random. It does
	// not affect the map — that comes from the file loaded below.
	const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("Seed"));
	if (!SeedOption.IsEmpty())
	{
		Seed = FCString::Atoi(*SeedOption);
	}

	// The salt, generated here on the authority and never replicated. Unlike Seed
	// this is deliberately *not* readable from the travel URL: a URL option is
	// visible to a joining client, which would hand back exactly the loot map this
	// salt exists to withhold. It is also not derived from Seed or from the clock,
	// because a client knows both. A raid GUID rather than FMath::Rand(), which is
	// seeded per process and would repeat across raids in one session.
	//
	// Two of the GUID's words rather than one hash of it: 32 bits of salt is
	// offline-sweepable from a single observed roll (the other two inputs are
	// known, so one crate pins the one salt that explains it, and that reopens the
	// whole raid). Assembled in uint64 and reinterpreted once, because shifting a
	// word with its top bit set into a signed 64-bit value overflows.
	const FGuid SaltSource = FGuid::NewGuid();
	LootSalt = static_cast<int64>((static_cast<uint64>(SaltSource.A) << 32) | static_cast<uint64>(SaltSource.B));

	// Load the layout here, not in StartPlay: UEngine::LoadMap spawns every
	// local player's pawn (which calls RestartPlayer) before it calls
	// UWorld::BeginPlay, which is what invokes StartPlay. Waiting for
	// StartPlay to populate CachedLayout means the very first RestartPlayer
	// of a standalone or PIE launch always finds it empty and falls back to
	// the world origin. InitGame runs ahead of player spawning in the same
	// LoadMap sequence, and loading a definition from disk needs no world or
	// game state, so it can safely run this early. The game state may not
	// exist yet at this point, so this only loads the definition; StartPlay
	// below hands it to the game state to spawn once a world and game state
	// both exist.
	FSarkoMapDefinition Definition;
	FString Error;
	if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId.ToString(), Definition, Error))
	{
		CachedDefinition = Definition;
		CachedLayout = SarkoMap::ToLayout(Definition);
	}
	else
	{
		// Loud, and empty. A hand-edited map file will break eventually, and a
		// silently empty raid is the confusing outcome: the game starts, the
		// player falls through nothing, and nothing says why.
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameMode: %s"), *Error);
	}
}

void ASarkoRaidGameMode::StartPlay()
{
	Super::StartPlay();

	// The map itself never crosses the network. Every machine — server
	// included — ends up with identical geometry because every machine reduces
	// the same shipped data file with the same pure ToLayout, so the layouts
	// cannot disagree, and the server never pays to replicate or simulate
	// hundreds of static scenery actors. Seed replicates through
	// ASarkoRaidGameState for loot rolls, not for geometry.
	if (HasAuthority())
	{
		if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
		{
			RaidState->Seed = Seed;
			// The server already has the layout and definition from InitGame,
			// so it hands them over directly instead of reloading. The
			// server never receives its own OnRep notify, so it must trigger
			// the spawn explicitly; clients build and spawn their own copy
			// via OnRep_Seed once the replicated value arrives, since they
			// have no game mode instance to hand them a precomputed layout.
			RaidState->SpawnPrebuiltLayout(CachedLayout, CachedDefinition);
			CachedLayout = RaidState->GetLayout();
		}

		// Enemies are real replicated actors (unlike the map's static geometry),
		// so only the server spawns them; replication hands them to clients.
		if (UWorld* World = GetWorld())
		{
			for (const FVector& Spawn : CachedLayout.EnemySpawns)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				World->SpawnActor<ASarkoEnemyCharacter>(ASarkoEnemyCharacter::StaticClass(), Spawn, FRotator::ZeroRotator, Params);
			}
		}
	}

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		// Prefer the map's own duration when it set one — per-map duration
		// (15 minutes here, 30 on real maps) — and fall back to the settings
		// default otherwise, e.g. when the map failed to load.
		const float DurationSeconds = CachedDefinition.RaidDurationSeconds > 0.f
			? CachedDefinition.RaidDurationSeconds
			: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;
		RaidState->StartRaidClock(DurationSeconds);
	}
}

void ASarkoRaidGameMode::RestartPlayer(AController* NewPlayer)
{
	if (CachedLayout.PlayerStarts.Num() > 0)
	{
		const int32 Index = NextPlayerStartIndex % CachedLayout.PlayerStarts.Num();
		++NextPlayerStartIndex;

		const FTransform SpawnTransform(FRotator::ZeroRotator, CachedLayout.PlayerStarts[Index]);
		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
		return;
	}

	Super::RestartPlayer(NewPlayer);
}

void ASarkoRaidGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A game mode only ever exists on the server, so everything below is
	// authority-side by construction — including every location it measures.
	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	UWorld* World = GetWorld();
	if (!RaidState || !World || RaidState->IsRaidFinished())
	{
		return;
	}

	// The clock is the ceiling. Spec §4.5: time out is MIA, which is death.
	// Checked before the dwell, and FinishRaid is idempotent, so the frame a
	// dwell completes on an expired clock still resolves to exactly one outcome.
	if (RaidState->IsRaidOver())
	{
		FinishRaid(ESarkoRaidOutcome::MIA);
		return;
	}

	const TArray<FSarkoExtractionSpot>& Zones = CachedDefinition.Extractions;
	if (Zones.Num() == 0)
	{
		return;
	}
	const float RequiredDwell = FMath::Max(0.1f, GetDefault<USarkoRaidSettings>()->ExtractDwellSeconds);

	// Prune pawns that are gone instead of letting the map grow for the whole
	// raid. Cheap: it is one entry per living player, not per actor.
	for (auto Entry = DwellSeconds.CreateIterator(); Entry; ++Entry)
	{
		if (!Entry.Key().IsValid())
		{
			Entry.RemoveCurrent();
		}
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
		if (!Pawn || (Pawn->HealthComponent && Pawn->HealthComponent->IsDead()))
		{
			continue;
		}

		// The server's own copy of the pawn, never a client-supplied position —
		// the same rule the loot channel follows.
		const int32 ZoneIndex = SarkoExtract::FindZoneContaining(Pawn->GetActorLocation(), Zones);
		float& Dwell = DwellSeconds.FindOrAdd(Pawn);
		Dwell = SarkoExtract::AdvanceDwell(Dwell, ZoneIndex != INDEX_NONE, DeltaSeconds);

		// Replicated to the owner only, so that pawn's HUD can draw a countdown
		// without anyone else learning that somebody is extracting.
		Pawn->SetExtractProgress(ZoneIndex, Dwell);

		if (Dwell >= RequiredDwell && Zones.IsValidIndex(ZoneIndex))
		{
			UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: extracted at zone %d ('%s') with %d backpack slots used"),
				ZoneIndex, *Zones[ZoneIndex].Name,
				Pawn->BackpackComponent ? Pawn->BackpackComponent->GetUsedSlots() : 0);
			FinishRaid(ESarkoRaidOutcome::Extracted);
			return;
		}
	}
}

void ASarkoRaidGameMode::HandlePlayerDied(ASarkoCharacter* Pawn)
{
	// The pawn has already emptied its own backpack (see ASarkoCharacter::
	// HandleDeath, which owns the whole death path — this only reports it), so
	// by the time a result is submitted there is nothing to credit. The order
	// matters and is deliberate.
	FinishRaid(ESarkoRaidOutcome::Died);
}

void ASarkoRaidGameMode::FinishRaid(ESarkoRaidOutcome NewOutcome)
{
	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	// CanFinishRaid is the whole idempotency and mutual-exclusion rule, pure and
	// unit tested: first real outcome wins, and InProgress is not an outcome.
	if (!RaidState || !SarkoRaid::CanFinishRaid(RaidState->Outcome, NewOutcome))
	{
		return;
	}
	// Both per-pawn effects run *before* Outcome is written, and on the server's
	// own copy of every player pawn.
	//
	// The order is the point for the haul: the HUD's summary reads the backpack and
	// says "the server emptied the backpack before the outcome was set", so that
	// has to be true for every losing outcome rather than only for the one that
	// happens to run through a death path. Input freeze does not care about the
	// order, and is here so there is one loop rather than two.
	const bool bLosesHaul = SarkoRaid::OutcomeLosesHaul(NewOutcome);
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
			if (!Pawn)
			{
				continue;
			}

			// Spec §4.5: MIA is death, so MIA loses the haul too. KIA already
			// cleared it on the pawn's own death path (ASarkoCharacter::
			// HandleDeath) and clearing an empty backpack again is a no-op, so
			// this is the single place the rule holds for all of them.
			if (bLosesHaul && Pawn->BackpackComponent)
			{
				Pawn->BackpackComponent->ClearOnDeath();
			}

			// Input freeze, on the server. The owning client's controller also
			// stops sending (ASarkoPlayerController::PlayerTick), but a client
			// that keeps sending is exactly the case worth defending against:
			// movement is disabled on the server's own copy of every player pawn,
			// and the aim/fire/loot RPCs check the outcome for themselves, so
			// ignoring the freeze buys nothing.
			Pawn->FreezeForRaidEnd();
		}
	}

	RaidState->Outcome = NewOutcome;

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: raid finished as %s, %.1f s left on the clock"),
		*UEnum::GetValueAsString(NewOutcome), RaidState->RemainingSeconds);

	// Task 8 adds the backend submission here. Deliberately after the state is
	// set: the HUD must show the outcome immediately, whether or not the network
	// cooperates (spec §4.6 — the game never hard-locks on network).
}
