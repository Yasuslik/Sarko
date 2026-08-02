#include "Core/SarkoRaidGameMode.h"

#include "AI/SarkoEnemyCharacter.h"
#include "CollisionQueryParams.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoGameInstance.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Core/SarkoTravel.h"
#include "Kismet/GameplayStatics.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootTable.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
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

	// ?EncounterBudget=N — the verification knob described on the member. Read on
	// the authority, kept on the authority: unlike Seed it is never replicated
	// and never echoed, so a joining client learns nothing from it. -1 (the
	// default) means the map's own budget stands.
	const FString BudgetOption = UGameplayStatics::ParseOption(Options, TEXT("EncounterBudget"));
	if (!BudgetOption.IsEmpty())
	{
		EncounterBudgetOverride = FMath::Max(0, FCString::Atoi(*BudgetOption));
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

	// THE SAFETY NET UNDER THE BORDER. The world's floor is a 40000 uu plane
	// whose top is z = 0, and the engine's default KillZ is -1000000: a pawn that
	// got past the border walls, or was pushed through the floor by a physics
	// resolve, fell for something like four minutes before anything noticed. To
	// the player that is not a death, it is a frozen raid with the haul still in
	// the bag. SarkoMap::WorldKillZ puts the trigger ten pawn heights under the
	// floor, and ASarkoRaidGameMode::RecoverFallenPawn turns crossing it into a
	// lost second rather than a lost raid.
	//
	// Set here rather than in the map asset because the raid map IS
	// /Engine/Maps/Entry with everything spawned into it — there is no level
	// designer's WorldSettings to author, and a number in a shared engine map
	// would apply to the shelter too.
	if (AWorldSettings* Settings = GetWorldSettings())
	{
		Settings->KillZ = SarkoMap::WorldKillZ;
	}

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
		//
		// These are the map's STATICALLY POSTED bots — the pre-encounter shape,
		// still supported because non-tutorial content uses it. The shipped
		// tutorial map authors none of them: since the realism stage every enemy
		// on `bridge` arrives from an encounter, which means the first ninety
		// seconds of a raid replicate zero enemies instead of six.
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
	// cannot be un-opened, because OpenContainerAt stores the roll for the rest
	// of the raid.
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

TArray<FSarkoItemStack>* ASarkoRaidGameMode::FindContainerInventory(int32 ContainerIndex)
{
	return ContainerInventories.Find(ContainerIndex);
}

TArray<FSarkoItemStack>* ASarkoRaidGameMode::OpenContainerAt(int32 ContainerIndex)
{
	if (TArray<FSarkoItemStack>* Existing = ContainerInventories.Find(ContainerIndex))
	{
		// Already rolled. Returning the stored array rather than re-rolling is the
		// whole point: the roll is deterministic, so a re-roll would resurrect
		// everything the player already took.
		return Existing;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoRaidGameMode: open of out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		return nullptr;
	}

	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(Spots[ContainerIndex].Tier);
	if (!RaidState || !Table)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameMode: container %d has tier '%s' with no loot table"),
			ContainerIndex, *Spots[ContainerIndex].Tier.ToString());
		if (RaidState)
		{
			// Emptied, not Opened: there is nothing to come back for, and leaving
			// it openable would re-run this failure every time the player walked past.
			RaidState->SetContainerState(ContainerIndex, ESarkoContainerState::Emptied);
		}
		return nullptr;
	}

	// LootSalt is why a client cannot precompute this: Seed is replicated and the
	// tables ship in the build, so without the salt the two remaining inputs are
	// already in the client's hands. Unchanged from the code this replaces.
	FRandomStream Stream(SarkoLoot::ContainerSeed(RaidState->Seed, ContainerIndex, LootSalt));
	// RollContainerFor, not RollContainer: the tutorial's static loot (spec §6.5)
	// is a per-container branch, and bTutorialLoot is server-only for the same
	// reason LootSalt is.
	TArray<FSarkoItemStack> Rolled =
		SarkoLoot::RollContainerFor(Spots[ContainerIndex], *Table, Stream, bTutorialLoot);

	if (Rolled.Num() > SarkoLoot::ContainerCells)
	{
		// Unreachable with the shipped tables (Sarko.Loot.EveryTierFitsTheContainerGrid
		// proves it) and loud rather than silent if that ever stops being true —
		// a truncated roll is the vanishing-loot defect wearing a new hat.
		UE_LOG(LogTemp, Error, TEXT("SarkoRaidGameMode: container %d rolled %d stacks but the grid holds %d; truncating"),
			ContainerIndex, Rolled.Num(), SarkoLoot::ContainerCells);
		Rolled.SetNum(SarkoLoot::ContainerCells);
	}

	// Opened, NOT Emptied. Marking a container emptied happens in exactly one
	// place — ASarkoCharacter::FinishTransfer, and only when the inventory is
	// actually empty. That single line is the vanishing-loot fix.
	RaidState->SetContainerState(ContainerIndex, ESarkoContainerState::Opened);
	return &ContainerInventories.Add(ContainerIndex, MoveTemp(Rolled));
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
	AnnouncedClosedZones.Reset();

	// Last of the three: it is what unlocks looting and the dwell, so it must not
	// be observable before the seed and the clock it belongs with.
	RaidState->bSessionReady = true;

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: raid live — seed %d, clock %.0fs, session '%s'"),
		AuthoritativeSeed, ClockSeconds, Session.SessionId.IsEmpty() ? TEXT("(offline)") : *Session.SessionId);

	// Last of all: enemies may not arrive before the raid the player is in has
	// begun, and the budget cannot be chosen before bTutorialLoot is known.
	InitialiseEncounters();
}

void ASarkoRaidGameMode::InitialiseEncounters()
{
	if (!HasAuthority())
	{
		return;
	}

	EncounterRuntimes.Reset();
	EncounterOrder.Reset();
	EncountersFired = 0;

	const FSarkoEncounterBudget& Budget = CachedDefinition.EncounterBudget;
	if (CachedDefinition.Encounters.Num() == 0 || !Budget.bAuthored)
	{
		// A map with no encounters is not an error — the pre-encounter
		// `botSpawns` shape still works and non-tutorial content uses it.
		EncounterBudgetRemaining = 0;
		return;
	}

	EncounterBudgetRemaining = bTutorialLoot ? Budget.Tutorial : Budget.Normal;
	if (EncounterBudgetOverride >= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoEncounter: budget overridden from the URL — %d instead of the map's %d"),
			EncounterBudgetOverride, EncounterBudgetRemaining);
		EncounterBudgetRemaining = EncounterBudgetOverride;
	}

	EncounterRuntimes.SetNum(CachedDefinition.Encounters.Num());

	// THE ROTATION (spec §5). Built once, here, because it depends on the raid
	// seed and on bTutorialLoot and neither is known before ActivateRaid.
	//
	// A tutorial raid gets the authored teaching rows in authored order and
	// nothing else. A normal raid gets every row, shuffled against the seed, so
	// which POIs spend the budget is a different answer each raid. The decision
	// itself is a pure function in SarkoEncounter so "the same seed is the same
	// raid" is a testable statement rather than a belief about this loop.
	const TArray<FSarkoEncounter>& Encounters = CachedDefinition.Encounters;
	TArray<int32> AuthoredOrders;
	TArray<bool> Optional;
	AuthoredOrders.Reserve(Encounters.Num());
	Optional.Reserve(Encounters.Num());
	for (const FSarkoEncounter& Encounter : Encounters)
	{
		AuthoredOrders.Add(Encounter.Order);
		Optional.Add(Encounter.bOptional);
	}
	EncounterOrder = SarkoEncounter::BuildActivationOrder(AuthoredOrders, Optional, bTutorialLoot, Seed);

	FString OrderText;
	for (const int32 Index : EncounterOrder)
	{
		OrderText += FString::Printf(TEXT("%s%s"), OrderText.IsEmpty() ? TEXT("") : TEXT(" -> "), *Encounters[Index].Id);
	}
	UE_LOG(LogTemp, Display,
		TEXT("SarkoEncounter: %d of %d encounter(s) armed for a %s raid (seed %d) — budget %d, first fight at most %d alive"),
		EncounterOrder.Num(), Encounters.Num(), bTutorialLoot ? TEXT("TUTORIAL") : TEXT("normal"),
		Seed, EncounterBudgetRemaining, Budget.FirstFightMaxAlive);
	UE_LOG(LogTemp, Display, TEXT("SarkoEncounter: activation order — %s"), *OrderText);

	const float Interval = FMath::Max(0.05f, GetDefault<USarkoRaidSettings>()->EncounterEvaluationIntervalSeconds);
	GetWorldTimerManager().SetTimer(EncounterTimerHandle, this, &ASarkoRaidGameMode::EvaluateEncounters, Interval, true);
}

APawn* ASarkoRaidGameMode::FindNearestLivingPlayerPawn() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Candidate = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (!Candidate)
		{
			continue;
		}
		const USarkoHealthComponent* Health = Candidate->FindComponentByClass<USarkoHealthComponent>();
		if (Health && Health->IsDead())
		{
			continue;
		}
		return Candidate;
	}
	return nullptr;
}

bool ASarkoRaidGameMode::HasLineOfSightBetween(const FVector& From, const FVector& To, const AActor* IgnoreActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// No world is not "the point is safe": with nothing to trace against,
		// the honest answer is that the spawn point cannot be cleared.
		return true;
	}

	// Eye height on both ends, not floor to floor: a floor-level trace is
	// blocked by the ground's own slope and would clear points that stand in
	// plain view. 80 uu is roughly chest height on a ~176 uu pawn.
	const FVector Eye(0.f, 0.f, 80.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SarkoEncounterSpawnLOS), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(IgnoreActor);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, From + Eye, To + Eye, ECC_Visibility, Params);
	return !bBlocked;
}

void ASarkoRaidGameMode::EvaluateEncounters()
{
	// Authority only, and that is structural rather than defensive: a game mode
	// instance exists nowhere else. The check stays because the cost is nothing
	// and the rule is worth stating where it is relied on.
	if (!HasAuthority())
	{
		return;
	}
	const ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	if (!RaidState || !RaidState->bSessionReady || RaidState->IsRaidFinished())
	{
		return;
	}
	UWorld* World = GetWorld();
	APawn* Player = FindNearestLivingPlayerPawn();
	if (!World || !Player)
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Interval = FMath::Max(0.05f, Settings.EncounterEvaluationIntervalSeconds);
	const FVector PlayerLocation = Player->GetActorLocation();

	for (const int32 Index : EncounterOrder)
	{
		if (!CachedDefinition.Encounters.IsValidIndex(Index) || !EncounterRuntimes.IsValidIndex(Index))
		{
			continue;
		}
		const FSarkoEncounter& Encounter = CachedDefinition.Encounters[Index];
		SarkoEncounter::FEncounterRuntime& Runtime = EncounterRuntimes[Index];

		// Weak pointers, so a corpse that has expired (SetLifeSpan) stops
		// counting against maxAlive without anyone having to tell us.
		Runtime.Spawned.RemoveAll([](const TWeakObjectPtr<AActor>& Actor)
		{
			if (!Actor.IsValid())
			{
				return true;
			}
			const USarkoHealthComponent* Health = Actor->FindComponentByClass<USarkoHealthComponent>();
			return Health != nullptr && Health->IsDead();
		});
		const int32 AliveNow = Runtime.Spawned.Num();

		const FVector TriggerCentre(Encounter.Trigger.Location.X, Encounter.Trigger.Location.Y, PlayerLocation.Z);
		const float Distance = FVector::Dist2D(TriggerCentre, PlayerLocation);
		Runtime.bBeyondRearm = SarkoEncounter::UpdateBeyondRearm(
			Runtime.bBeyondRearm, Distance, Encounter.Trigger.ArmAfterUU);

		if (!Runtime.bArmed)
		{
			if (!SarkoEncounter::ShouldArm(Runtime.bFired, Encounter.bOneShot, Runtime.bBeyondRearm,
					Distance, Encounter.Trigger.RadiusUU))
			{
				continue;
			}
			Runtime.bArmed = true;
			Runtime.DeferredSeconds = 0.f;
			UE_LOG(LogTemp, Display,
				TEXT("SarkoEncounter: '%s' ARMED — player %.0f uu from the trigger (radius %.0f, order %d, cost %d)"),
				*Encounter.Id, Distance, Encounter.Trigger.RadiusUU, Encounter.Order, Encounter.BudgetCost);
		}

		const bool bFirstFight = (EncountersFired == 0);
		const int32 Allowed = SarkoEncounter::AllowedSpawnCount(
			EncounterBudgetRemaining, Encounter.BudgetCost, Encounter.MaxAlive, AliveNow,
			Encounter.Spawns.Num(), bFirstFight, CachedDefinition.EncounterBudget.FirstFightMaxAlive);

		if (Allowed <= 0)
		{
			// Refused. Disarmed and the hysteresis latch closed, so this does not
			// re-evaluate (and re-log) four times a second for the rest of the
			// raid: the player has to leave and come back for another attempt.
			UE_LOG(LogTemp, Display,
				TEXT("SarkoEncounter: '%s' REFUSED — costs %d, %d left in the raid budget (%d alive of maxAlive %d)"),
				*Encounter.Id, Encounter.BudgetCost, EncounterBudgetRemaining, AliveNow, Encounter.MaxAlive);
			Runtime.bArmed = false;
			Runtime.bBeyondRearm = false;
			Runtime.DeferredSeconds = 0.f;
			continue;
		}

		// Authored points, in author order, that are far enough away AND cannot
		// see the player. Author order is the preference order: the notes on
		// each `spawns[]` row record which cover it was chosen behind, so the
		// first usable one is the one the designer would have picked.
		TArray<int32> Usable;
		Usable.Reserve(Encounter.Spawns.Num());
		for (int32 SpawnIndex = 0; SpawnIndex < Encounter.Spawns.Num(); ++SpawnIndex)
		{
			const FVector& Point = Encounter.Spawns[SpawnIndex].Location;
			const float SpawnDistance = FVector::Dist(Point, PlayerLocation);
			const bool bSees = HasLineOfSightBetween(Point, PlayerLocation, Player);
			if (SarkoEncounter::SpawnPointQualifies(SpawnDistance, bSees, Settings.EncounterMinSpawnDistanceUU))
			{
				Usable.Add(SpawnIndex);
			}
		}

		if (Usable.Num() < Allowed)
		{
			// Defer. Never relocate toward the player, never spawn in view — the
			// only honest move is to wait for the player to move.
			Runtime.DeferredSeconds += Interval;
			if (SarkoEncounter::ShouldAbandonDeferral(Runtime.DeferredSeconds, Settings.EncounterSpawnDeferSeconds))
			{
				UE_LOG(LogTemp, Display,
					TEXT("SarkoEncounter: '%s' gave up after %.2fs — only %d of %d authored point(s) were >= %.0f uu away and out of sight; it will re-arm on the next approach"),
					*Encounter.Id, Runtime.DeferredSeconds, Usable.Num(), Encounter.Spawns.Num(),
					Settings.EncounterMinSpawnDistanceUU);
				Runtime.bArmed = false;
				Runtime.bBeyondRearm = false;
				Runtime.DeferredSeconds = 0.f;
			}
			continue;
		}

		int32 SpawnedNow = 0;
		for (int32 Slot = 0; Slot < Allowed; ++Slot)
		{
			const FSarkoEncounterSpawn& Spawn = Encounter.Spawns[Usable[Slot]];

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			// Facing the player, so the first thing the player ever sees of this
			// enemy is its front and not its back.
			const FRotator Facing = (PlayerLocation - Spawn.Location).Rotation();
			ASarkoEnemyCharacter* Enemy = World->SpawnActor<ASarkoEnemyCharacter>(
				ASarkoEnemyCharacter::StaticClass(), Spawn.Location, FRotator(0.f, Facing.Yaw, 0.f), Params);
			if (!Enemy)
			{
				continue;
			}
			Enemy->ApplyArchetypeAndPost(
				Spawn.Archetype, FVector(Spawn.PostPos.X, Spawn.PostPos.Y, Spawn.Location.Z), Spawn.LeashUU);
			Runtime.Spawned.Add(Enemy);
			++SpawnedNow;

			UE_LOG(LogTemp, Display,
				TEXT("SarkoEncounter: '%s' spawned '%s' (%s) at %s — %.0f uu from the player, no line of sight, holding post (%.0f, %.0f) on a %.0f uu leash"),
				*Encounter.Id, *Spawn.Id, *Spawn.Archetype.ToString(), *Spawn.Location.ToString(),
				FVector::Dist(Spawn.Location, PlayerLocation), Spawn.PostPos.X, Spawn.PostPos.Y, Spawn.LeashUU);
		}

		if (SpawnedNow == 0)
		{
			// Every SpawnActor failed. Not a budget event: nothing is spent for
			// enemies that do not exist.
			Runtime.bArmed = false;
			Runtime.DeferredSeconds = 0.f;
			continue;
		}

		// Charged in full and never refunded, even when the first-fight cap made
		// the wave smaller than its cost. Deducting at least what was spawned is
		// what makes "at most N enemies in a tutorial raid" true by arithmetic
		// rather than by authoring.
		EncounterBudgetRemaining -= Encounter.BudgetCost;
		++EncountersFired;
		Runtime.bFired = true;
		Runtime.bArmed = false;
		Runtime.bBeyondRearm = false;
		Runtime.DeferredSeconds = 0.f;

		UE_LOG(LogTemp, Display,
			TEXT("SarkoEncounter: '%s' spent %d of the raid budget for %d enemy(ies) — %d left"),
			*Encounter.Id, Encounter.BudgetCost, SpawnedNow, EncounterBudgetRemaining);
	}
}

bool ASarkoRaidGameMode::RecoverFallenPawn(APawn& Pawn)
{
	// Server only, and the check is real rather than ceremonial: FellOutOfWorld
	// runs on every machine that simulates the actor, and a client that moved its
	// own copy would be corrected by the next replicated position anyway — after
	// having shown the player a teleport nobody authorised.
	if (!HasAuthority())
	{
		return false;
	}

	const FVector From = Pawn.GetActorLocation();
	FVector Recovery;
	if (!SarkoMap::NearestPoint(From, CachedLayout.PlayerStarts, Recovery))
	{
		// No layout means no honest answer, and inventing one (the origin) would
		// drop the pawn on the closed bridge in the middle of the map. Let the
		// engine's own FellOutOfWorld run instead: dying is worse than recovering
		// and better than falling for four minutes.
		UE_LOG(LogTemp, Error,
			TEXT("SarkoRecovery: '%s' left the world at %s and there is no player spawn to return it to — the map layout is empty"),
			*Pawn.GetName(), *From.ToString());
		return false;
	}

	// Above the point, not on it: the spawn transform is a floor position and a
	// capsule teleported to floor level can resolve downward into the very floor
	// it was rescued from.
	const FVector Target(Recovery.X, Recovery.Y, FMath::Max(Recovery.Z, 0.f) + 150.f);

	// Velocity first. A character that has been falling for a while carries a
	// large downward velocity, and teleporting without clearing it puts the pawn
	// back through the floor within a frame or two — the same fall, from higher up.
	if (UCharacterMovementComponent* Movement = Pawn.FindComponentByClass<UCharacterMovementComponent>())
	{
		Movement->StopMovementImmediately();
		Movement->Velocity = FVector::ZeroVector;
	}
	Pawn.SetActorLocation(Target, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);

	// LOUD, and on purpose. This is a net, not a feature: every line here is a
	// hole in the world that has not been found yet, and a net that catches
	// silently is a net that lets the hole live forever.
	UE_LOG(LogTemp, Warning,
		TEXT("SarkoRecovery: '%s' FELL OUT OF THE WORLD at (%.0f, %.0f, %.0f) — returned to the nearest player spawn (%.0f, %.0f, %.0f). ")
		TEXT("This is the safety net firing: the border is meant to make it impossible, so a line here means there is a hole in the map."),
		*Pawn.GetName(), From.X, From.Y, From.Z, Target.X, Target.Y, Target.Z);
	return true;
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

	// Raid-clock seconds since ActivateRaid, from the clock this game mode
	// started and the remaining time it owns — never from a wall clock and never
	// from anything a client sends. This is what a late-opening zone is measured
	// against (SarkoExtract::IsZoneOpen).
	const float ElapsedSeconds = FMath::Max(0.f, MapClockSeconds() - RaidState->RemainingSeconds);

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

		// A zone that has not opened yet accrues NOTHING. The gate is here, on the
		// authority, and not in the HUD that draws the zone as inert: the HUD is
		// telling the player something, this is deciding it. Standing on a closed
		// pad from second one and stepping off the instant it opens must not be a
		// free extraction, so the dwell is advanced as if the pawn were outside
		// every zone — which resets it, per AdvanceDwellInZone's first rule.
		const bool bOpen = Zones.IsValidIndex(ZoneIndex)
			&& SarkoExtract::IsZoneOpen(Zones[ZoneIndex].OpensAfterSeconds, ElapsedSeconds);
		const int32 CountingZone = bOpen ? ZoneIndex : INDEX_NONE;

		// Once per zone per raid, not per tick: a player can stand on a shut pad
		// for minutes, and this exists so a headless run can prove the gate holds.
		if (Zones.IsValidIndex(ZoneIndex) && !bOpen && !AnnouncedClosedZones.Contains(ZoneIndex))
		{
			AnnouncedClosedZones.Add(ZoneIndex);
			UE_LOG(LogTemp, Display,
				TEXT("SarkoRaidGameMode: zone %d ('%s') refuses the dwell — it opens at %.0fs and the raid is %.0fs old (%.0fs to go)"),
				ZoneIndex, *Zones[ZoneIndex].Name, Zones[ZoneIndex].OpensAfterSeconds, ElapsedSeconds,
				SarkoExtract::SecondsUntilOpen(Zones[ZoneIndex].OpensAfterSeconds, ElapsedSeconds));
		}

		SarkoExtract::FSarkoDwell& Dwell = Dwells.FindOrAdd(Pawn);
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, CountingZone, DeltaSeconds);

		// Replicated to the owner only, so that pawn's HUD can draw a countdown
		// without anyone else learning that somebody is extracting.
		//
		// The zone the pawn is PHYSICALLY in, even when it is shut: the HUD needs
		// to name the zone it is standing on to say how long it stays shut, and
		// the dwell it is handed is the counted one, which for a closed zone is
		// zero. Openness is re-derived on the client from the same map file, so
		// nothing extra crosses the wire for it.
		Pawn->SetExtractProgress(ZoneIndex, Dwell.Seconds);

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
				ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
				if (Pawn && Pawn->BackpackComponent)
				{
					// THE MAGAZINE COMES HOME TOO (spec §1). Now that a reload
					// spends `ammo_9mm` out of the grid, rounds sitting in the gun
					// are rounds the player found, carried and did not spend —
					// losing them for the crime of having reloaded before walking
					// to the pad would teach exactly the wrong lesson, and it would
					// make "reload before extracting" a strictly wrong move.
					//
					// Through AddItem rather than appended to the haul, and that is
					// the load-bearing choice: AddItem tops up an existing ammo
					// stack for free and only opens a new rectangle when there is
					// room, so the credit can never claim more than the bag could
					// physically hold. Appending straight to the submission would
					// have been simpler and would eventually have handed
					// domain.FitsCarryGrid a haul that needs a thirteenth cell —
					// answered 400 implausible_items, i.e. the player's whole raid
					// deleted over eight rounds. Rounds with genuinely nowhere to
					// go are dropped, which is the same rule every other overflow
					// on this path already follows.
					//
					// UnloadMagazine zeroes the magazine as it reports it, so this
					// is exactly-once by construction however the outcome path is
					// re-entered.
					if (Pawn->WeaponComponent)
					{
						const int32 Rounds = Pawn->WeaponComponent->UnloadMagazine();
						if (Rounds > 0)
						{
							const int32 Leftover =
								Pawn->BackpackComponent->AddItem(SarkoLoot::AmmoItemId, Rounds);
							UE_LOG(LogTemp, Display,
								TEXT("SarkoRaidGameMode: extraction credits %d round(s) from the magazine (%d had nowhere to go)"),
								Rounds - Leftover, Leftover);
						}
					}

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

	// THE SUBMISSION IS NOT THIS OBJECT'S ANY MORE, and that is the fix. It used
	// to be one attempt, started here, with the client captured strongly so it
	// could outlive the travel — but one attempt is all a timeout or an iOS
	// suspension needs to destroy a legitimate haul, and a retry SCHEDULE cannot
	// live in a game mode that is about to be destroyed by ReturnToShelter.
	//
	// The game instance owns it now: it writes the result to Saved/ before the
	// first attempt, retries with backoff on its own timer manager (which survives
	// the travel), and picks the file up again on the next launch if the app dies
	// mid-flight. The server dedups by session token, so a resubmission of one it
	// already took costs nothing.
	//
	// Nothing below waits for any of it. ReturnToShelter is called immediately and
	// the travel is never held hostage to the network.
	if (USarkoGameInstance* Instance = GetGameInstance<USarkoGameInstance>())
	{
		Instance->SubmitRaidResultWithRetry(Session, SarkoBackend::OutcomeToWire(NewOutcome), Haul);
	}

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
