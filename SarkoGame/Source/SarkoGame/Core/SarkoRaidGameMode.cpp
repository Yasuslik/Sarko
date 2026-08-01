#include "Core/SarkoRaidGameMode.h"

#include "AI/SarkoEnemyCharacter.h"
#include "Core/SarkoGameInstance.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Core/SarkoTravel.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Map/SarkoMapBuilder.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "TimerManager.h"
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
		// Atoi64 then SeedToInt32, not FCString::Atoi: the seeds worth typing here
		// are the ones copied out of a sarko-api log line, and StartRaid produces
		// them as int64(rand.Uint32()) — so about half of them exceed INT32_MAX.
		// Atoi saturates at 2147483647, which silently rolls a *different* set of
		// crates than the server logged under that seed and makes the one
		// reproduction tool in the project lie. SeedToInt32 wraps the same bits the
		// online path wraps, so `?Seed=3402905197` reproduces that raid exactly.
		Seed = SarkoBackend::SeedToInt32(FCString::Atoi64(*SeedOption));
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
			// Seed is deliberately *not* assigned here any more, and the clock is
			// deliberately not started: both happen in ActivateRaid, once
			// sarko-api's raid/start has handed back the authoritative seed (or the
			// offline fallback has chosen the local one). Assigning the placeholder
			// here would let a container roll against a seed that is about to change
			// — two different hauls for one crate — and would replicate a "the raid
			// has begun" signal a round trip too early.
			//
			// The geometry below does not wait, because none of it depends on the
			// seed: every machine reduces the same shipped data file, and
			// RestartPlayer has already run by now and needs the layout.
			// The server already has the layout and definition from InitGame,
			// so it hands them over directly instead of reloading. The
			// server never receives its own OnRep notify, so it must trigger
			// the spawn explicitly.
			//
			// A client has no game mode instance to hand it a precomputed layout, so
			// it loads the same file itself — but *not* from here and not yet. Its
			// trigger is OnRep_SessionReady, i.e. ActivateRaid one HTTP round trip
			// below, because that is the honest "the raid has begun" edge. OnRep_Seed
			// calls the same idempotent builder, but off the authority that build is
			// gated by SarkoRaid::ShouldSpawnClientLayout, so on a client it does
			// nothing at all unless bSessionReady is already set.
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

	// Last, so the world is fully built before a completion callback can land in
	// it: the raid becomes live in ActivateRaid, one HTTP round trip from here.
	if (!GetDefault<USarkoRaidSettings>()->bBackendEnabled)
	{
		FallBackToOfflineRaid(TEXT("the backend is disabled in settings"));
		return;
	}
	BeginBackendSession();
}

void ASarkoRaidGameMode::BeginBackendSession()
{
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!GameInstance)
	{
		// Loud rather than fatal: the raid still plays offline. This means
		// GameInstanceClass is missing from DefaultEngine.ini (or is in
		// DefaultGame.ini, where UGameMapsSettings does not read it), which is
		// exactly the silent-default failure that once left the raid game mode
		// itself unloaded.
		FallBackToOfflineRaid(TEXT("no USarkoGameInstance — check GameInstanceClass in DefaultEngine.ini"));
		return;
	}
	Backend = GameInstance->EnsureBackend();

	// TWeakObjectPtr through every hop: an HTTP completion can land after the
	// level has been torn down, and this game mode is the first thing to go.
	TWeakObjectPtr<ASarkoRaidGameMode> WeakThis(this);

	// Already authenticated when the player came from the shelter — the client and
	// its JWT ride the game instance across the travel, so this saves a round trip
	// on every raid after the first of a launch.
	if (Backend->IsAuthenticated())
	{
		OnAuthenticated();
		return;
	}

	Backend->Authenticate([WeakThis](bool bAuthenticated, const FString& AuthError)
	{
		ASarkoRaidGameMode* Self = WeakThis.Get();
		if (!Self || !Self->Backend)
		{
			return;
		}
		if (!bAuthenticated)
		{
			Self->FallBackToOfflineRaid(AuthError);
			return;
		}
		Self->OnAuthenticated();
	});
}

void ASarkoRaidGameMode::OnAuthenticated()
{
	// The profile decides the loot mode, so it is fetched before the session
	// opens rather than alongside it: a container opened against the wrong mode
	// cannot be un-opened, because CompleteLootChannel marks it.
	//
	// Fetched here rather than trusted from the game instance's cache, even though
	// the shelter just fetched one: a raid can be entered directly from the
	// command line with no shelter visit at all (which is how every headless
	// verification in this plan runs), and the authority must not depend on a
	// screen it may never have shown.
	TWeakObjectPtr<ASarkoRaidGameMode> WeakThis(this);
	Backend->FetchProfile([WeakThis](bool bSuccess, const FSarkoProfile& Profile, const FString& Error)
	{
		ASarkoRaidGameMode* Self = WeakThis.Get();
		if (!Self || !Self->Backend)
		{
			return;
		}
		if (!bSuccess)
		{
			// A failed profile is not a failed raid: spec §4.6's offline
			// degradation says the raid still plays and nothing persists. The
			// offline fallback picks the loot mode from settings.
			Self->FallBackToOfflineRaid(Error);
			return;
		}

		if (USarkoGameInstance* Instance = Self->GetGameInstance<USarkoGameInstance>())
		{
			// Cached so the shelter draws the post-raid stash without waiting, and
			// so the two screens agree about the tier.
			Instance->RecordProfile(Profile);
		}

		Self->SetTutorialLoot(!Profile.bTutorialCompleted);
		Self->BeginRaidSession();
	});
}

void ASarkoRaidGameMode::SetTutorialLoot(bool bEnabled)
{
	bTutorialLoot = bEnabled;
	if (!bEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: normal loot — containers roll against the raid seed"));
		return;
	}

	// Counted once, at activation, not per container: this is the one line that
	// tells a reader whether the tutorial actually has a layout yet. Stage C
	// authors it; until then the count is 0 and every container falls back to a
	// roll (see SarkoLoot::RollContainerFor).
	int32 WithFixedItems = 0;
	for (const FSarkoLootContainerSpot& Spot : CachedDefinition.Containers)
	{
		if (Spot.FixedItems.Num() > 0)
		{
			++WithFixedItems;
		}
	}

	if (WithFixedItems == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoRaidGameMode: TUTORIAL loot requested but none of %d containers in '%s' carries fixedItems — every container will roll instead. Authoring the static layout is Stage C's job (spec §6.5)."),
			CachedDefinition.Containers.Num(), *CachedDefinition.Id);
		return;
	}
	UE_LOG(LogTemp, Display,
		TEXT("SarkoRaidGameMode: TUTORIAL loot — %d of %d containers carry fixedItems, the rest roll"),
		WithFixedItems, CachedDefinition.Containers.Num());
}

void ASarkoRaidGameMode::BeginRaidSession()
{
	// Weak again, and for a second reason now: the client is owned by the game
	// instance and therefore outlives every world, so a completion is guaranteed
	// to run — on a live client, with a dead game mode — after the player has
	// travelled back to the shelter. The weak check is what turns that into a
	// dropped reply instead of a write into a torn-down world.
	TWeakObjectPtr<ASarkoRaidGameMode> WeakThis(this);

	// The loadout goes out **empty**, and it must stay empty until in-raid
	// weapons and ammo are real items.
	//
	// /v1/raid/start debits the loadout from the stash; nothing credits it back
	// except the raid result, and the result submits the backpack alone. So the
	// pistol and 60 rounds this used to send were a one-way withdrawal: the first
	// raid spent the starter kit and every raid after it got 409
	// insufficient_items, which fell through to the offline path permanently —
	// the online loop worked exactly once per install. Nothing in the raid even
	// reads the loadout: the weapon is abstract with infinite reloads, so the
	// debit was risk with no matching stake, which is dishonest rather than hard.
	//
	// An empty loadout means a PvE raid risks only the loot it finds, which is
	// the intended economy for the tutorial sector. Restore the debit — together
	// with crediting a survivor's kit back in the result — when losing a weapon
	// on death is a real consequence.
	const FString MapId = GetDefault<USarkoRaidSettings>()->BackendMapId;
	Backend->StartRaid(MapId, SarkoBackend::WireLoadout(),
		[WeakThis](bool bStarted, const FSarkoRaidSession& NewSession, const FString& StartError)
		{
			ASarkoRaidGameMode* Inner = WeakThis.Get();
			if (!Inner || !Inner->Backend)
			{
				return;
			}
			if (!bStarted)
			{
				Inner->FallBackToOfflineRaid(StartError);
				return;
			}
			Inner->Session = NewSession;

			// Confirm immediately. Until it lands the session is `pending`
			// and the sweeper hands the loadout back after PENDING_TTL (60 s
			// on the deployed service), which would leave a raid running
			// against a session that no longer accepts a result.
			Inner->Backend->ConfirmRaid(NewSession,
				[WeakThis](bool bConfirmed, const FDateTime& ExpiresAt, const FString& ConfirmError)
				{
					ASarkoRaidGameMode* Confirmed = WeakThis.Get();
					if (!Confirmed)
					{
						return;
					}
					if (!bConfirmed)
					{
						Confirmed->FallBackToOfflineRaid(ConfirmError);
						return;
					}

					const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
					const double SecondsLeft = (ExpiresAt - FDateTime::UtcNow()).GetTotalSeconds();
					const float Clock = SarkoBackend::ClockSecondsFromDeadline(
						Confirmed->CachedDefinition.RaidDurationSeconds, SecondsLeft,
						Settings.BackendGraceMarginSeconds);

					if (Confirmed->MapClockSeconds() > Clock + 1.f)
					{
						// Loud, because it is a configuration mismatch a
						// player would experience as "the raid was shorter
						// than the map says". RAID_TTL is 20m on the deployed
						// service against a 15-minute map, so this line
						// should never appear — if it does, RAID_TTL was
						// lowered and raising it is the fix.
						UE_LOG(LogTemp, Warning,
							TEXT("SarkoRaidGameMode: the map asks for %.0fs but the server's deadline allows %.0fs — raising RAID_TTL is the fix"),
							Confirmed->MapClockSeconds(), Clock);
					}

					Confirmed->ActivateRaid(Confirmed->Session.Seed, Clock);
				});
		});
}

void ASarkoRaidGameMode::FallBackToOfflineRaid(const FString& Reason)
{
	if (bOfflineDegraded)
	{
		// One line per raid, not one per failed hop: the first failure is the one
		// that explains the rest, and a repeated Error reads like a loop.
		return;
	}
	bOfflineDegraded = true;

	// Spec §4.6: any HTTP failure logs loudly, the raid still plays, nothing
	// persists. The game must never hard-lock on the network — the alternative is
	// a black screen in a lift, and a developer who cannot iterate on a plane.
	UE_LOG(LogTemp, Error,
		TEXT("SarkoRaidGameMode: playing OFFLINE — nothing will be saved. Reason: %s"), *Reason);

	// Cleared so FinishRaid can tell "no session" from "a session that failed
	// halfway": an empty session id is the one signal it reads.
	Session = FSarkoRaidSession();

	// No profile, so no flag. Settings decide, defaulting to the tutorial's static
	// layout: an offline raid persists nothing, so replaying the tutorial costs the
	// player nothing and gives a deterministic raid to iterate against.
	SetTutorialLoot(GetDefault<USarkoRaidSettings>()->bOfflineTutorialLoot);

	// The Seed the game mode already holds (URL option or its default) becomes
	// authoritative for this raid.
	ActivateRaid(Seed, MapClockSeconds());
}

void ASarkoRaidGameMode::ActivateRaid(int32 AuthoritativeSeed, float ClockSeconds)
{
	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	if (!RaidState)
	{
		return;
	}
	// Both refusals live in SarkoRaid::CanActivateRaid, pure and unit tested. The
	// settled-outcome half is the one that bites: the damage gate opens on
	// IsRaidFinished() alone, so a player can be shot dead during the
	// auth→start→confirm round trip, and the confirm landing afterwards would
	// otherwise re-arm the raid under its own KIA summary — a fresh seed that
	// re-rolls every container, and a fresh full clock on a corpse whose result was
	// already decided.
	if (!SarkoRaid::CanActivateRaid(RaidState->bSessionReady, RaidState->Outcome))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoRaidGameMode: refused to activate a raid that is already %s (session ready: %s) — seed %d and a %.0fs clock were discarded"),
			*UEnum::GetValueAsString(RaidState->Outcome),
			RaidState->bSessionReady ? TEXT("yes") : TEXT("no"),
			AuthoritativeSeed, ClockSeconds);
		return;
	}

	Seed = AuthoritativeSeed;
	RaidState->Seed = AuthoritativeSeed;
	RaidState->StartRaidClock(ClockSeconds);

	// Activation is the dwell's epoch. Cleared here rather than relied upon to be
	// empty: the Tick's IsLootable() guard means nothing has accrued yet *today*,
	// but that is a property of a guard somewhere else, and the rule "five seconds
	// from entering this zone, and the raid going live counts as entering" should
	// hold because this line exists. Pinned by Sarko.Extract.ActivationIsTheDwellEpoch.
	Dwells.Reset();

	// Last of the three: it is what unlocks looting and the dwell, so it must not
	// be observable before the seed and the clock it belongs with.
	RaidState->bSessionReady = true;

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: raid live — seed %d, clock %.0fs, session '%s'"),
		AuthoritativeSeed, ClockSeconds, Session.SessionId.IsEmpty() ? TEXT("(offline)") : *Session.SessionId);
}

float ASarkoRaidGameMode::MapClockSeconds() const
{
	// Prefer the map's own duration when it set one — per-map duration (15 minutes
	// here, 30 on real maps) — and fall back to the settings default otherwise,
	// e.g. when the map failed to load. Zero would start a clock that is already
	// expired, which the Tick below reads as MIA on the first frame.
	return CachedDefinition.RaidDurationSeconds > 0.f
		? CachedDefinition.RaidDurationSeconds
		: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;
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
	// IsLootable() is "the raid is live": no outcome yet, and the session has
	// settled. The second half matters as much as the first — a dwell completing
	// during the auth→start→confirm round trip would finish the raid before there
	// is a session to submit it to, which is the one path that silently throws a
	// haul away. The clock is not running yet either (ActivateRaid starts it), so
	// the round trip costs no raid time.
	if (!RaidState || !World || !RaidState->IsLootable())
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
	for (auto Entry = Dwells.CreateIterator(); Entry; ++Entry)
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
		SarkoExtract::FSarkoDwell& Dwell = Dwells.FindOrAdd(Pawn);
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, ZoneIndex, DeltaSeconds);

		// Replicated to the owner only, so that pawn's HUD can draw a countdown
		// without anyone else learning that somebody is extracting.
		Pawn->SetExtractProgress(Dwell.ZoneIndex, Dwell.Seconds);

		// Dwell.ZoneIndex rather than the local ZoneIndex: they agree, and reading
		// the state that was actually counted is what stops the two drifting if
		// AdvanceDwellInZone ever grows a rule that refuses a zone.
		if (Dwell.Seconds >= RequiredDwell && Zones.IsValidIndex(Dwell.ZoneIndex))
		{
			UE_LOG(LogTemp, Display,
				TEXT("SarkoRaidGameMode: extracted at zone %d ('%s') after %.2fs of dwell, with %d backpack slots used"),
				Dwell.ZoneIndex, *Zones[Dwell.ZoneIndex].Name, Dwell.Seconds,
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
	// The order is the point for the haul. Its consumers are no longer the HUD —
	// that summary moved to the shelter — but two things downstream of this very
	// function: the Haul array assembled below (submitted to sarko-api and handed
	// to USarkoGameInstance::RecordRaidOutcome) and SarkoShelter::BuildHaulLines,
	// which itemises that recorded array on the shelter screen. Both read the
	// backpack as it stood when Outcome was written, so "the server emptied the
	// backpack before the outcome was set" has to be true for every losing outcome
	// rather than only for the one that happens to run through a death path.
	// Input freeze does not care about the order, and is here so there is one loop
	// rather than two.
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

	// The backend comes last, deliberately: the HUD must show the outcome
	// immediately, whether or not the network cooperates (spec §4.6 — the game
	// never hard-locks on network).
	//
	// Submitted once. The backend is idempotent by session id, which is a safety
	// net for a dropped connection — not a licence to send twice. CanFinishRaid
	// above already makes this unreachable a second time; the flag is the belt to
	// that braces, because the cost of being wrong is a double-credited haul.
	//
	// This is also the one return below the outcome write that does *not* schedule
	// a trip back to the shelter, and it is safe: reaching it means an earlier
	// FinishRaid already submitted and already scheduled that trip, so the player
	// is not stranded on the banner. CanFinishRaid makes it unreachable anyway.
	if (bSessionSubmitted)
	{
		return;
	}
	bSessionSubmitted = true;

	// Only an extraction carries anything out, and this reads the backpack on the
	// same tick the outcome settled: the loop above cleared it for every losing
	// outcome *before* Outcome was written, so a died/MIA haul is empty by
	// construction and an extracted one is intact. Sending an explicitly empty
	// array for the losing outcomes makes the intent independent of that ordering.
	//
	// Assembled *before* the offline branch, not after it as it used to be: the
	// shelter now needs the haul on the offline path too, to show it under a "НЕ
	// ЗБЕРЕЖЕНО" label rather than showing nothing and letting the player conclude
	// the raid was pointless.
	//
	// SINGLE-PLAYER-SLICE ASSUMPTION, recorded because it is now load-bearing in
	// two more places than it used to be. The loop below takes the *first* player
	// controller with a live pawn and breaks — it is not the pawn that extracted,
	// and nothing here knows which one that was (ExtractTick calls FinishRaid
	// without naming the pawn, and Outcome is one value for the whole raid). With
	// one player the two are always the same pawn, so this is correct today.
	//
	// With a second player it stops being correct in three ways at once: the wrong
	// backpack can be submitted to sarko-api under this session, the wrong haul can
	// be credited to USarkoGameInstance::LastRaid and itemised in the shelter, and
	// one player's extraction ends the raid for everyone. The fix is not a better
	// loop — it is FinishRaid taking the extracting pawn (or a per-player outcome),
	// which is a design decision, not a tidy-up. Whoever adds the second player
	// owns it.
	TArray<FSarkoItemStack> Haul;
	if (NewOutcome == ESarkoRaidOutcome::Extracted)
	{
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
				if (Pawn && Pawn->BackpackComponent)
				{
					// Not GetSlots(): a worn backpack is not in the cells, and
					// submitting the cells alone would silently drop the one item
					// the player most obviously carried out.
					Haul = Pawn->BackpackComponent->GetHaulForSubmission();
					break;
				}
			}
		}
	}

	if (!Backend || Session.SessionId.IsEmpty())
	{
		// The offline path: no token, so no result. Logged at Error once, never per
		// tick, and the raid has already ended locally with its banner on screen.
		UE_LOG(LogTemp, Error,
			TEXT("SarkoRaidGameMode: raid ended '%s' with no backend session — nothing was saved"),
			SarkoBackend::OutcomeToWire(NewOutcome));
		// bPersisted = false, so the shelter says "НЕ ЗБЕРЕЖЕНО" over the haul
		// instead of showing it above a stash that does not contain it.
		ReturnToShelter(NewOutcome, Haul);
		return;
	}

	// The client is captured by *strong* reference on purpose, unlike every other
	// hop in this file. A raid can end moments before the world goes away — and
	// after this task, it always does: ReturnToShelter travels away a few seconds
	// later. The game instance is now a second owner, so this capture is belt to
	// those braces; it stays because the lambda must survive even engine shutdown,
	// where the game instance goes too. Nothing here touches the game mode.
	Backend->SubmitResult(Session, SarkoBackend::OutcomeToWire(NewOutcome), Haul,
		[Keep = Backend, Wire = FString(SarkoBackend::OutcomeToWire(NewOutcome)),
		 WeakInstance = TWeakObjectPtr<USarkoGameInstance>(GetGameInstance<USarkoGameInstance>())]
		(bool bSuccess, const FString& Error)
		{
			if (bSuccess)
			{
				UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: result '%s' submitted"), *Wire);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("SarkoRaidGameMode: the raid result '%s' was NOT saved: %s. The session expires on the server and closes as died."),
					*Wire, *Error);
			}
			// The shelter's "НЕ ЗБЕРЕЖЕНО" label is corrected here if the submission
			// beat the travel, which it normally does. The game instance survives
			// the travel, so this lands whichever world is current.
			if (USarkoGameInstance* Instance = WeakInstance.Get())
			{
				Instance->LastRaid.bPersisted = bSuccess;
			}
		});

	// Recorded as not-yet-persisted and corrected by the completion above. The
	// pessimistic direction on purpose: a label that says "saved" about a haul
	// that was lost is the one mistake this screen must not make.
	ReturnToShelter(NewOutcome, Haul);
}

void ASarkoRaidGameMode::ReturnToShelter(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul)
{
	if (USarkoGameInstance* Instance = GetGameInstance<USarkoGameInstance>())
	{
		// bPersisted starts false and is raised by SubmitResult's completion. An
		// offline raid never raises it, which is exactly right.
		Instance->RecordRaidOutcome(Outcome, Haul, /*bPersisted*/ false);
	}
	else
	{
		// Without a game instance there is nowhere to put the outcome and no
		// shelter to travel to. Loud, and the raid simply stops here.
		UE_LOG(LogTemp, Error,
			TEXT("SarkoRaidGameMode: no USarkoGameInstance, so no return to the shelter — check GameInstanceClass in DefaultEngine.ini"));
		return;
	}

	const float Delay = FMath::Max(0.5f, GetDefault<USarkoRaidSettings>()->PostRaidReturnSeconds);
	FTimerHandle Handle;
	// CreateWeakLambda: this game mode is destroyed by the travel it is about to
	// start, and a strong delegate on a timer that the world outlives is how a
	// travel turns into a crash.
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// ShelterOptions() is empty on purpose and the travel is ABSOLUTE: a
		// relative return would inherit `game=…SarkoRaidGameMode` from the outbound
		// URL (FURL copies Base->Op for TRAVEL_Relative) and start another raid,
		// forever, with nothing in any log to explain it. SarkoTravel::TravelTo is
		// where bAbsolute=true lives.
		SarkoTravel::TravelTo(this, SarkoTravel::ShelterOptions());
	}), Delay, /*bLoop*/ false);

	// Only now, with a timer that exists, does the HUD get to promise a return.
	// Deliberately not written next to Outcome: the early return above leaves a
	// finished raid with no trip scheduled, and a banner keyed on the outcome would
	// promise one anyway.
	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		RaidState->bReturningToShelter = true;
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: returning to the shelter in %.1fs"), Delay);
}
