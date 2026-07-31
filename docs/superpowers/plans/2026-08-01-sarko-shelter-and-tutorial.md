# Shelter and One-Time Tutorial Raid Implementation Plan (Stage A.5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the game from "boots straight into a raid, forever" into a loop: boot → **the Shelter** (stash, garage progress, shop stub, "В РЕЙД") → raid → outcome → back to the Shelter with a freshly fetched profile — and make the *first* raid a one-time tutorial whose containers hold authored `fixedItems` instead of a seeded roll, until the backend records the player's first successful extraction.

**Architecture:** One level, two game modes. The project authors no `.umap`, so travel is `/Engine/Maps/Entry` → `/Engine/Maps/Entry` with the game mode chosen by the travel URL's `game=` option; `GlobalDefaultGameMode` becomes the Shelter, so boot and the return trip land there for free. Everything that must survive a level travel (the JWT, the fetched profile, the raid's outcome and haul) moves onto a `USarkoGameInstance`, which is the one object the engine keeps across `LoadMap`. The Shelter's UI is a **Slate widget built in C++** and added to the viewport; every string it shows is produced by a pure `SarkoShelter::Build*` function so the whole menu is unit-tested with no world and no widget. Tutorial loot is a per-container branch in the existing server-side roll path, selected by one server-only bool that the raid reads from `GET /v1/profile`.

**Tech Stack:** UE 5.8, C++ only, `Build.sh` + `UnrealEditor-Cmd`, automation tests under `-nullrhi`. Slate/SlateCore (no UMG, no Blueprints, no assets). Backend: Go stdlib mux, pgx, goose embedded migrations, Postgres.

## Global Constraints

- Specs: Stage A.5 is **§6.5 of `docs/superpowers/specs/2026-07-30-sarko-production-raid-loop-design.md`** (owner decisions of 2026-07-31) and it is **normative**. §4 is what Stage A already built and is the context, not the work.
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`.
- **`DefaultBuildSettings` stays `BuildSettingsVersion.V7`.** `SarkoGame.Build.cs` keeps `PrivateIncludePaths.Add(ModuleDirectory)` and every existing dependency (`Core`, `CoreUObject`, `Engine`, `InputCore`, `AIModule`, `NavigationSystem`, `DeveloperSettings`, `Json`, `JsonUtilities`, `HTTP`). This plan adds **`"Slate"` and `"SlateCore"`** and nothing else. It does **not** add `"UMG"`.
  - Verified: `Engine.Build.cs` lists `SlateCore` and `Slate` in `PublicDependencyModuleNames`, so both are already reachable transitively — the explicit entries are hygiene, so a future `Engine` change cannot silently break the Shelter. `UMG` is only a **private** dependency of `Engine` and is therefore *not* reachable, which is one more reason the Shelter is Slate.
- **Create no binary assets. Ever.** No `.uasset`, `.umap`, Blueprint, UMG widget blueprint, Enhanced Input action, DataTable, Behavior Tree, Slate style asset, font asset. C++, `.ini`, `.json`, `.sh`, `.sql` only. Referencing an engine asset by path (`/Engine/BasicShapes/Cube.Cube`, `/Engine/Maps/Entry`) is fine. Files written under `Saved/` at runtime are fine.
  - Slate widgets written with `SNew`/`SLATE_BEGIN_ARGS` in C++ are **not assets** — they are compiled code. Their styling comes from `FCoreStyle::Get()`, which is compiled into SlateCore. Verified: `SButton`'s `FArguments` default `_ButtonStyle` to `&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button")` and `_TextStyle` to `"ButtonText"`, and `FCoreStyle::GetDefaultFontStyle(FName, float)` exists in `SlateCore/Public/Styling/CoreStyle.h`. Nothing in the Shelter needs `FAppStyle`, which is editor-slanted.
- **The in-raid HUD stays `AHUD::DrawHUD` primitives only** and input stays the existing polling in `ASarkoPlayerController` — no input assets. The *Shelter* is Slate; the *raid* is unchanged.
- **Touch layout:** the bottom corners are the thumbs' dead zone during a raid. The Shelter has no sticks, so it may use the whole screen — but the "В РЕЙД" button must sit in the **lower-middle third**, reachable by either thumb, and never within 8% of a screen edge.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code** — `UnrealEditor-Cmd` exits 0 having run zero tests, and the script takes its verdict from the `Automation Test Queue Empty N tests performed` line. **The suite is at 56 tests before this plan and must be at 73 after it.** Every verify step names the expected total.
- The automation-test flag spelling that compiles is `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`.
- **`USarkoRaidSettings` is `config = Game` → `Config/DefaultGame.ini`.** `UGameMapsSettings` is `config = Engine` → `Config/DefaultEngine.ini`, section `[/Script/EngineSettings.GameMapsSettings]`. **Both `GlobalDefaultGameMode` and `GameInstanceClass` are `UGameMapsSettings` properties and therefore both live in `DefaultEngine.ini`.** A setting in the wrong file silently loads the C++ default and nothing warns — this exact mistake once left `SarkoRaidGameMode` unloaded for a whole session.
- **ini string values containing `//` must be quoted** or `SwallowDoubleSlashComments` truncates them at the scheme (`BackendBaseUrl` carries the comment explaining it). Class paths like `/Script/SarkoGame.SarkoShelterGameMode` contain no `//` and need no quoting.
- **`SetMobility(Static)` before `SetStaticMesh` silently no-ops after BeginPlay.** No new geometry in this plan, but if any is added: Movable → assign mesh/scale/collision → Static, via `SpawnMeshBox` in `Map/SarkoMapBuilder.cpp`.
- **`UFUNCTION(Exec)` cannot sit inside `#if !UE_BUILD_SHIPPING`** — UHT rejects it. Declare unconditionally, guard the *body* in the `.cpp` (`SarkoOverview` is the precedent).
- **`FRotator`/`FVector` members are doubles in 5.8** — a bare float literal in `TestEqual` is ambiguous. Compare with `.Equals(..., Tolerance)` and suffix scalar literals with `f`.
- **Forward-declare structs at global scope**, never as an elaborated type specifier inside a namespace (`const struct FFoo&` inside `namespace Bar {}` declares a *second*, permanently incomplete `Bar::FFoo`). `Core/SarkoRaidGameState.h` and `Loot/SarkoExtractionZone.h` both carry the comment; follow it.
- **HTTP callbacks fire after world teardown, and now also after a level travel.** Every completion handler binds through `TWeakObjectPtr` / `CreateWeakLambda` and returns early when the owner is gone. `FinishRaid`'s `SubmitResult` deliberately captures the client by **strong** reference so the result is still logged after teardown — keep that, and note that after this plan the game instance is a second owner, so the client cannot die mid-request at all.
- **RPC and profile inputs are hostile.** Every client-supplied container index is bounds-checked against `Definition.Containers.Num()`; every distance is re-measured from the server's own copy of the pawn; every field of a parsed HTTP response is validated before use. `ServerRequestFire` in `Combat/SarkoWeapon.cpp` is the precedent.
- **No per-tick HTTP and no per-tick allocation.** HTTP happens exactly **five** times per raid after this plan (auth, profile, start, confirm, result) plus once per Shelter visit (profile). The Shelter widget rebuilds its strings only when the model changes, never per paint.
- **Level travel destroys every actor.** Anything the Shelter needs must live on `USarkoGameInstance` or be re-fetched. The profile is re-fetched on every Shelter entry; the JWT and the last raid's outcome ride the game instance.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything that discards working-tree changes — a subagent on this project already destroyed an uncommitted file that way.
- Backend: all SQL stays in `internal/store`. Migrations are goose-embedded under `internal/db/migrations/`. Tests run `go test ./... -count=1 -p 1` with `-race` where invoked directly; test DB is Postgres on **port 5455** via `make test-db`. **The backend suite is at 89 tests before this plan and must be at 94 after it.**

### Contract of record (verified against the real handlers in `sarko-api/internal/api/` on 2026-07-31)

Base URL: `https://sarko-api-production.up.railway.app`. `RAID_TTL=20m` and `GRACE_BUFFER=2m` on the deployed service, so `confirm` returns `now + 22m` and the 15-minute `bridge.json` clock is never clamped. Field names are exactly these — do not invent, rename or add.

| Call | Request body | 200 response |
|---|---|---|
| `POST /v1/auth/anonymous` (no auth) | `{"device_id":"<≤128 chars>"}` | `{"player_id":"<uuid>","token":"<jwt>"}` |
| `GET /v1/profile` (Bearer) | — | `{"player_id","schema_version","stash":[{"item_id","quantity"}],"vehicle_tier","unlocked_maps":[...],"tutorial_completed":bool}` |
| `POST /v1/raid/start` (Bearer) | `{"map_id":"bridge","loadout":[]}` | `{"session_id","session_token","seed","expires_at"}` |
| `POST /v1/raid/confirm` (Bearer) | `{"session_id","session_token"}` | `{"expires_at":"<RFC3339>"}` |
| `POST /v1/raid/result` (Bearer) | `{"session_id","session_token","outcome":"extracted"\|"died","items":[{"item_id","quantity"}]}` | `{"session_id","outcome","credited_items":[...],"already_closed":bool}` |

`tutorial_completed` is the **only** new field in this plan and Task 1 adds it. Everything else is already live and already parsed.

Errors are always `{"error":{"code":"<snake_case>","message":"..."}}`. Codes this plan must handle by name: `map_locked` (403), `raid_in_progress` (409), `insufficient_items` (409), `session_not_open` (409), `bad_session_token` (401), `safe_pocket_overflow` (400), `implausible_items` (400), `unauthorized` (401), `not_found` (404).

Carried forward unchanged from Stage A and still true:

- **The wire loadout is EMPTY.** `SarkoBackend::WireLoadout()` returns an empty array on purpose: `/v1/raid/start` debits the loadout and only the *result* credits anything back, so a non-empty loadout is a one-way withdrawal that makes raid 2 answer `409 insufficient_items` forever. Nothing in this plan refills it.
- **The map id is `bridge`** on the wire and on disk. `internal/domain/garage.go` maps `TierNone → "bridge"`, so any other `map_id` gets `403 map_locked`. `USarkoRaidSettings::BackendMapId` keeps the indirection as configuration hygiene and defaults to `bridge`.
- **`seed` overflows `int32`.** `StartRaid` does `int64(rand.Uint32())`; `SarkoBackend::SeedToInt32` does the bit-preserving `uint32` reinterpretation and is pinned by `Sarko.Backend.SeedFromTheUrlSurvivesTheFullUint32Range`. Nothing in this plan touches it.
- **`items.json` carries a `category` field** beyond spec §4.1's `{id,name,stackSize}`, and loot state replicates as `TArray<uint8>` on the game state rather than a per-container `bLooted`. Both are deliberate Stage A extensions, both documented in the Stage A plan, both unchanged here.

## Two decisions this plan makes, with the reasoning and the cost

### Decision 1 — the Shelter is a Slate widget in C++, not a second `AHUD`

**Chosen:** `SSarkoShelterWidget`, constructed with `SNew` in `ASarkoShelterPlayerController::BeginPlay` and handed to `UGameViewportClient::AddViewportWidgetContent`.

**Why not a second `AHUD` on a menu level:**

1. `AHUD::DrawHUD` gives `DrawRect`, `DrawText`, `DrawLine` and nothing else. A menu needs **hit-testable buttons with press states** and a **scrollable list of unbounded length** (a stash grows every raid). With `DrawHUD` both must be hand-rolled: hit-testing off `GetInputTouchState`/`GetMousePosition` against manually computed rects — which is exactly the code `SarkoInput::InteractButtonRect` + `UpdateInteract` already is, and that is ~90 lines for *one* button with no scrolling, no focus and no press feedback. `SButton` + `SScrollBox` + `SVerticalBox` are that for free, and cost zero assets.
2. An `AHUD` requires a live world with a game mode whose `HUDClass` is set **and a local player controller with a view**; `DrawHUD` is driven off the player's view target. The Shelter has no pawn and no view target worth having. A viewport widget needs neither.
3. There is no second level to put a second `AHUD` on. The project authors no `.umap`, so "a menu level" would mean loading `/Engine/Maps/Entry` again anyway — i.e. the same travel this plan already does, plus an `AHUD` we would then have to hand-roll a menu into.

**The cost, stated plainly:**

- Slate is the engine's *editor/tooling* UI layer. UMG is the shipping game UI layer, and every UMG widget is a binary asset. So this Shelter is **throwaway UI**: when the project is allowed binary assets, it gets rebuilt in UMG. What survives is `namespace SarkoShelter`'s pure builders — they produce strings, and a UMG widget will consume exactly the same strings.
- No designer can edit it and it cannot be previewed in the editor. Every layout change is a recompile.
- `FCoreStyle` is deliberately plain: grey buttons, one font family, no theming. It will look like a debug menu, because it is one. That is what "MVP shelter, zero binary assets" buys.
- Slate routes touch as pointer input, so buttons work on device — but there is no thumb-zone discipline for free, hence the Global Constraint pinning where "В РЕЙД" sits.

### Decision 2 — one level, game mode selected by the travel URL

`/Engine/Maps/Entry` stays the only map, and `GameMapsSettings` stays in `DefaultEngine.ini` (`config = Engine`). What changes there is two lines:

```ini
GlobalDefaultGameMode=/Script/SarkoGame.SarkoShelterGameMode
GameInstanceClass=/Script/SarkoGame.SarkoGameInstance
```

Flow:

- **Boot** → engine loads `GameDefaultMap` = `/Engine/Maps/Entry` with no `game=` option → `UGameInstance::CreateGameModeForURL` falls through to `GlobalDefaultGameMode` → **the Shelter**.
- **Shelter → raid** → `UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), /*bAbsolute*/ true, TEXT("game=/Script/SarkoGame.SarkoRaidGameMode"))`.
- **Raid → Shelter** → `UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), /*bAbsolute*/ true, FString())` → no `game=` → `GlobalDefaultGameMode` → the Shelter again.

Verified against engine source, because all three of these are the kind of thing that fails silently:

- `UGameInstance::CreateGameModeForURL` (`Runtime/Engine/Private/GameInstance.cpp:1514`) reads `GAME=` out of `InURL.Op`, resolves it through `UGameMapsSettings::GetGameModeForName` (which returns the string unchanged when it matches no alias — `Runtime/EngineSettings/Private/EngineSettingsModule.cpp:120`), `LoadClass<AGameModeBase>` on it, and **overrides `WorldSettings->DefaultGameMode` with it**. `GlobalDefaultGameMode` is only the final fallback. So `game=` wins, and an absent `game=` yields the Shelter.
- `UGameplayStatics::OpenLevel` (`Runtime/Engine/Private/GameplayStatics.cpp:981`) appends `?` + options and calls `GEngine->SetClientTravel(World, *Cmd, bAbsolute ? TRAVEL_Absolute : TRAVEL_Relative)`.
- **`bAbsolute` must be `true` on both trips.** `FURL::FURL(FURL* Base, ...)` copies `Base->Op` for `TRAVEL_Relative` and `TRAVEL_Partial` (`Runtime/Engine/Private/URL.cpp:168-175`). A *relative* return trip would therefore inherit `game=/Script/SarkoGame.SarkoRaidGameMode` from the outbound URL and boot the raid again — an infinite raid loop with nothing in any log to explain it. `TRAVEL_Absolute` does not copy `Op`.

`ServerTravel` is deliberately **not** used: this slice is standalone single-player (`bBackendEnabled` aside, there is no listen server and nothing joins), and `ServerTravel` on a standalone world is a no-op path with extra ceremony. The seam is one function — `SarkoTravel::TravelTo` — so the day a dedicated server exists, `ServerTravel` replaces `OpenLevel` in one place, and the `game=` option is honoured identically on that path because it is the same `CreateGameModeForURL`.

## Three things where the spec and the real code disagree — flagged, not silently resolved

1. **§6.5 says the tutorial's static layout teaches "junk at spawn → medkit at the pipes → ammo + first valuable at the gas station → military at the rail depot → extract at E1". None of those places exist yet.** `Data/Maps/bridge.json` is the *full* Bridge sector from the bridge-map plan (42 containers, 3 extractions, no `pipes`/`gas station`/`rail depot` ids at all), and Bridge_West is Stage C. **This plan therefore builds the mechanism and pins it with tests; it authors no `fixedItems` anywhere, and `bridge.json` is not edited.** Stage C authors the layout against Bridge_West's final geometry, exactly as §6.5's own "Ordering" paragraph says. Task 6 makes that split explicit in code: tutorial mode with no authored `fixedItems` falls back to the seeded roll and logs one Warning per raid naming the count, and *Stage C's acceptance bar is that the Warning stops appearing.*
2. **The e2e report calls the dwell timing "correct and deliberate"; the owner calls it a bug.** `.superpowers/sdd/task-9-report.md` §6.4 records the live observation (pawn on the pad at `23:40:45.785`, raid live at `46.407`, extraction at `51.253` — five seconds billed from the activation frame, not from the entry) and classifies it as intended, because `Tick`'s `IsLootable()` guard is what produces it. The owner's OPEN NOTE classifies the same fact as a real bug. Task 7 resolves it by **keeping the deliberate half and fixing the accidental half**: no dwell may accrue before the raid is live (a dwell completing during the round trip would finish a raid with no session to submit it to — the one path that silently throws a haul away), *and* the dwell now carries the identity of the zone it belongs to and is reset at the activation frame, so "five seconds from entering **this** zone" is a rule that is written down and tested instead of an accident of a boolean. The genuine defect the old shape hid: `AdvanceDwell` takes only `bInsideZone`, so a pawn crossing from one zone straight into an overlapping second zone **kept its accumulated dwell** and could extract from a zone it had stood in for one frame.
3. **§6.5 says the Shelter shows "garage progress (bicycle 0/3 parts)", but no endpoint exposes a recipe.** `GET /v1/profile` returns `vehicle_tier` and `stash`; the bicycle recipe (`bike_frame` ×1, `wheel_small` ×2, `chain` ×1) exists only in `sarko-api/internal/domain/garage.go`'s unexported `recipes` map. The client therefore **mirrors** the recipe in `SarkoShelter::BicycleRecipe()`, with a comment naming `garage.go` as the source of truth. The three ids are already pinned to `garage.go` by the existing `Sarko.Loot.RealItemCatalogIsUsable` test, so a rename on the backend breaks a test rather than the menu. Adding `GET /v1/garage/recipe` is the right long-term fix and is **out of scope** — flag it in the Task 8 report.

## File Structure

```
SarkoGame/
├── Config/
│   ├── DefaultEngine.ini                       # + GlobalDefaultGameMode=Shelter, GameInstanceClass  (config=Engine!)
│   └── DefaultGame.ini                         # + PostRaidReturnSeconds, bOfflineTutorialLoot
└── Source/SarkoGame/
    ├── SarkoGame.Build.cs                      # + "Slate", "SlateCore"
    ├── Core/
    │   ├── SarkoGameInstance.h/.cpp            # NEW: backend client + profile + last raid, survives travel
    │   ├── SarkoTravel.h/.cpp                  # NEW: pure travel URLs + the one TravelTo seam
    │   ├── SarkoRaidSettings.h                 # + PostRaidReturnSeconds, bOfflineTutorialLoot
    │   ├── SarkoRaidGameMode.h/.cpp            # + profile hop, tutorial flag, dwell struct, return travel
    │   └── SarkoRaidGameState.h/.cpp           # bSessionReady triggers the client layout (Seed==0 fix)
    ├── Net/
    │   └── SarkoBackendClient.h/.cpp           # + verb on Send, FSarkoProfile, ParseProfileResponse, FetchProfile
    ├── Loot/
    │   ├── SarkoExtractionZone.h/.cpp          # + FSarkoDwell, AdvanceDwellInZone
    │   └── SarkoLootTable.h/.cpp               # + RollContainerFor (fixedItems branch)
    ├── Map/
    │   └── SarkoMapDefinition.h/.cpp           # + FSarkoLootContainerSpot::FixedItems
    ├── Shelter/
    │   ├── SarkoShelterView.h/.cpp             # NEW: pure view model + Build* string builders
    │   ├── SarkoShelterWidget.h/.cpp           # NEW: SSarkoShelterWidget (Slate, C++)
    │   ├── SarkoShelterGameMode.h/.cpp         # NEW: no pawn, no HUD, spectator start
    │   └── SarkoShelterPlayerController.h/.cpp # NEW: owns the widget, fetches the profile, travels
    ├── UI/SarkoHUD.h/.cpp                      # summary loses the haul list; gains "returning" line
    └── Tests/
        ├── BackendClientTest.cpp               # + 3 profile-parsing tests
        ├── ExtractionTest.cpp                  # + 3 dwell-from-entry tests, + 1 layout-trigger test
        ├── LootTest.cpp                        # + 4 fixedItems tests
        └── ShelterTest.cpp                     # NEW: 3 view tests + 2 outcome tests + 1 travel test

sarko-api/
├── internal/db/migrations/0003_tutorial_flag.sql   # NEW
├── internal/store/players.go                       # Profile reads tutorial_completed
├── internal/store/raids.go                         # SubmitResult sets it on an extracted result
├── internal/store/raids_result_test.go             # + 3 flag tests
├── internal/store/players_test.go                  # + 1 default-false test
└── internal/api/endpoints_test.go                  # + 1 end-to-end flag test
```

`namespace SarkoShelter` holds free functions that take values and return values, separate from the widget that draws them — the same split the map parser has from the map spawner, and for the same reason: `run-tests.sh` runs under `-nullrhi`, where there is no viewport and no Slate application to build a widget into.

---

### Task 1: Backend — `tutorial_completed`, set on the first successful raid

First, because the flag's **absence** is what puts a client into tutorial mode (Task 2 parses a missing `tutorial_completed` as `false`). If the client shipped first, every player would be in tutorial mode until the backend caught up — the safe direction, but only by luck. Shipping the backend first makes it correct by construction.

**Files:**
- Create: `sarko-api/internal/db/migrations/0003_tutorial_flag.sql`
- Modify: `sarko-api/internal/store/players.go` (`Profile` struct + query)
- Modify: `sarko-api/internal/store/raids.go` (`SubmitResult` sets the flag)
- Modify: `sarko-api/internal/store/raids_result_test.go` (3 new tests)
- Modify: `sarko-api/internal/store/players_test.go` (1 new test)
- Modify: `sarko-api/internal/api/endpoints_test.go` (1 new test)

**Interfaces:**
- Consumes: `store.Store`, `store.Profile`, `store.SubmitResultParams`, `domain.OutcomeExtracted`, `addItemsTx`, `testutil.Pool`.
- Produces:
  - `store.Profile.TutorialCompleted bool` with JSON tag **`tutorial_completed`** — this is the wire name Task 2 parses.
  - No new exported function. The flag is set inside the existing `SubmitResult` transaction.

- [ ] **Step 1: Write the failing store tests**

Append to `sarko-api/internal/store/raids_result_test.go`. Read the top of that file first and reuse whatever helper it already has for "start + confirm a raid"; if it has none, the three tests below each do it inline as shown.

```go
func TestExtractedResultSetsTheTutorialFlag(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-tutorial-extract")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	// Before the first raid the flag must be false, because false is what puts a
	// client into tutorial mode. A default of true would silently skip the
	// tutorial for every player who ever existed.
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Fatal("a brand-new player is already marked as having completed the tutorial")
	}

	started, err := s.StartRaid(ctx, store.StartRaidParams{
		PlayerID:   playerID,
		MapID:      "bridge",
		Loadout:    nil,
		PendingTTL: time.Minute,
	})
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}

	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeExtracted,
		Items:        []domain.ItemStack{{ItemID: "scrap_metal", Quantity: 2}},
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	profile, err = s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile after extraction: %v", err)
	}
	if !profile.TutorialCompleted {
		t.Error("a successful extraction did not complete the tutorial")
	}
}

func TestDiedResultLeavesTheTutorialFlagAlone(t *testing.T) {
	// Spec §6.5: "dying replays the tutorial with the same static layout". So
	// death must not set the flag — that is the whole difference between the two
	// outcomes as far as the tutorial is concerned.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-tutorial-death")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	started, err := s.StartRaid(ctx, store.StartRaidParams{
		PlayerID:   playerID,
		MapID:      "bridge",
		PendingTTL: time.Minute,
	})
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour); err != nil {
		t.Fatalf("ConfirmRaid: %v", err)
	}
	if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
		PlayerID:     playerID,
		SessionID:    started.SessionID,
		SessionToken: started.SessionToken,
		Outcome:      domain.OutcomeDied,
		Items:        nil,
	}); err != nil {
		t.Fatalf("SubmitResult: %v", err)
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Error("dying completed the tutorial; it must replay instead")
	}
}

func TestTutorialFlagNeverUnsetsAndSurvivesALaterDeath(t *testing.T) {
	// One-way latch. The flag decides whether containers roll or read a fixed
	// list, so a flag that could go back to false would put a veteran back into
	// the tutorial's static loot — and, worse, make the loot a player sees depend
	// on the order of their last two raids.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-tutorial-latch")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	raid := func(outcome domain.RaidOutcome) {
		t.Helper()
		started, err := s.StartRaid(ctx, store.StartRaidParams{
			PlayerID:   playerID,
			MapID:      "bridge",
			PendingTTL: time.Minute,
		})
		if err != nil {
			t.Fatalf("StartRaid(%s): %v", outcome, err)
		}
		if _, err := s.ConfirmRaid(ctx, playerID, started.SessionID, started.SessionToken, time.Hour); err != nil {
			t.Fatalf("ConfirmRaid(%s): %v", outcome, err)
		}
		if _, err := s.SubmitResult(ctx, store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    started.SessionID,
			SessionToken: started.SessionToken,
			Outcome:      outcome,
		}); err != nil {
			t.Fatalf("SubmitResult(%s): %v", outcome, err)
		}
	}

	raid(domain.OutcomeExtracted)
	raid(domain.OutcomeDied)
	raid(domain.OutcomeExtracted)

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if !profile.TutorialCompleted {
		t.Error("the tutorial flag came back off after a death following a successful raid")
	}
}
```

Append to `sarko-api/internal/store/players_test.go`:

```go
func TestProfileReportsTutorialNotCompletedForANewPlayer(t *testing.T) {
	// Read through the same path the client reads: /v1/profile serialises this
	// struct verbatim, so a field that is never populated by Profile() is a field
	// the client receives as false no matter what the column says.
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-tutorial-fresh")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}
	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	if profile.TutorialCompleted {
		t.Error("a new player must start with tutorial_completed = false")
	}
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd sarko-api && make test-db && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./internal/store/ -race -count=1 -p 1 -run 'Tutorial'`
Expected: compile failure — `profile.TutorialCompleted undefined (type store.Profile has no field or method TutorialCompleted)`.

- [ ] **Step 3: Add the migration**

Create `sarko-api/internal/db/migrations/0003_tutorial_flag.sql`:

```sql
-- +goose Up
-- The tutorial raid happens once (spec §6.5). A flag on the player row rather
-- than "has this player ever closed a raid as extracted": the raid_sessions
-- table is swept and closed rows are the historical record, so inferring the
-- answer would mean a scan per profile read on the hottest endpoint the client
-- has. It is also a one-way latch, which a derived answer would not be.
--
-- DEFAULT false, so every player who predates this migration is put back into
-- the tutorial exactly once. That is the deliberate choice: the alternative
-- (DEFAULT true) would mean nobody ever sees the tutorial, and the only players
-- that exist today are the developer's own test devices.
ALTER TABLE players ADD COLUMN tutorial_completed BOOLEAN NOT NULL DEFAULT false;

-- +goose Down
ALTER TABLE players DROP COLUMN tutorial_completed;
```

- [ ] **Step 4: Expose it on the profile**

In `sarko-api/internal/store/players.go`, add the field to `Profile`:

```go
// Profile is everything the client needs to render the shelter.
type Profile struct {
	PlayerID      string             `json:"player_id"`
	SchemaVersion int                `json:"schema_version"`
	Stash         []domain.ItemStack `json:"stash"`
	Tier          domain.Tier        `json:"vehicle_tier"`
	UnlockedMaps  []string           `json:"unlocked_maps"`
	// TutorialCompleted is false until the player's first *successful* raid
	// (spec §6.5). While it is false the client uses the map's authored
	// fixedItems instead of a seeded roll, so this field decides what is in
	// every container of the next raid — which is why it is set inside
	// SubmitResult's transaction and never anywhere else.
	TutorialCompleted bool `json:"tutorial_completed"`
}
```

and extend the single-row query in `Profile` — it already reads `schema_version` and the tier in one statement, so this is one more column on the same `SELECT`:

```go
	err = tx.QueryRow(ctx,
		`SELECT p.schema_version, COALESCE(g.vehicle_tier, 'none'), p.tutorial_completed
		 FROM players p
		 LEFT JOIN garage_progress g ON g.player_id = p.id
		 WHERE p.id = $1`, playerID).Scan(&p.SchemaVersion, &p.Tier, &p.TutorialCompleted)
```

- [ ] **Step 5: Set it on a successful result**

In `sarko-api/internal/store/raids.go`, inside `SubmitResult`, immediately after the `addItemsTx` credit and **before** the `UPDATE raid_sessions` that closes the session:

```go
	// Spec §6.5: the tutorial completes on the first *successful* raid. Set in
	// this transaction, so "the haul was credited" and "the tutorial is over"
	// can never disagree — a separate call could be interrupted between them and
	// leave a player who has banked tutorial loot still reading fixed lists.
	//
	// `AND NOT tutorial_completed` makes it a one-way latch and makes the
	// statement a no-op for every raid after the first, so this costs one indexed
	// row lookup per extraction and nothing else.
	//
	// Deliberately not reached on the two early-return paths above: a replay of
	// an already-closed session must not re-latch anything (it credits nothing),
	// and an expired session is closed as `died` regardless of what the client
	// claimed, so it is not a successful raid.
	if p.Outcome == domain.OutcomeExtracted {
		if _, err := tx.Exec(ctx,
			`UPDATE players SET tutorial_completed = true
			 WHERE id = $1 AND NOT tutorial_completed`, playerID); err != nil {
			return RaidResult{}, fmt.Errorf("complete tutorial: %w", err)
		}
	}
```

Note `playerID` (the value loaded from the session row), not `p.PlayerID`: the two are already proven equal by the token check above, and using the loaded one keeps every write in this function keyed off the row it locked.

- [ ] **Step 6: Run the store tests and confirm they pass**

Run: `cd sarko-api && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./internal/store/ -race -count=1 -p 1 -v -run 'Tutorial'`
Expected: four `--- PASS` lines — `TestExtractedResultSetsTheTutorialFlag`, `TestDiedResultLeavesTheTutorialFlagAlone`, `TestTutorialFlagNeverUnsetsAndSurvivesALaterDeath`, `TestProfileReportsTutorialNotCompletedForANewPlayer` — then `ok`.

- [ ] **Step 7: Prove the flag reaches the wire**

The store tests read the struct in-process. The client reads JSON, and a missing JSON tag is invisible to every test above. Append to `sarko-api/internal/api/endpoints_test.go`:

```go
func TestProfileExposesTutorialCompletedOverTheWire(t *testing.T) {
	// The client's tutorial branch reads this exact field name off the HTTP
	// response. A struct field with no tag, or a tag with the wrong name,
	// serialises to something the client parses as absent — and absent means
	// tutorial mode, so the bug is "every veteran gets static loot forever" and
	// nothing in the store tests can see it.
	c := newClient(t)
	c.authenticate()

	var raw map[string]any
	if code := c.do(http.MethodGet, "/v1/profile", nil, &raw); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	done, present := raw["tutorial_completed"]
	if !present {
		t.Fatalf("profile response has no tutorial_completed field: %v", raw)
	}
	if done != false {
		t.Errorf("tutorial_completed = %v for a new player, want false", done)
	}

	started := c.startRaid("bridge", nil)
	c.confirmRaid(started)
	c.submitResult(started, "extracted", []domain.ItemStack{{ItemID: "chain", Quantity: 1}})

	if code := c.do(http.MethodGet, "/v1/profile", nil, &raw); code != http.StatusOK {
		t.Fatalf("profile status after extraction = %d, want 200", code)
	}
	if raw["tutorial_completed"] != true {
		t.Errorf("tutorial_completed = %v after a successful raid, want true", raw["tutorial_completed"])
	}
}
```

`newClient`, `authenticate`, `startRaid`, `confirmRaid` and `submitResult` are whatever this file already calls them — read the top of `endpoints_test.go` and use the existing helpers verbatim rather than adding new ones. If a helper does not exist for one of these hops, inline the `c.do(...)` call in the same shape the neighbouring tests use.

- [ ] **Step 8: Run the whole backend suite**

Run: `cd sarko-api && make test`
Expected: `ok` for all seven packages, and **94 tests** total (89 before + 3 raid-result + 1 players + 1 endpoints). Confirm the count with:

Run: `cd sarko-api && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./... -count=1 -p 1 -v 2>&1 | grep '^=== RUN' | grep -vc '/'`
(the `grep -vc '/'` matters: a bare `grep -c '^=== RUN'` counts subtests too and reports 131, not the 94 top-level tests this plan's numbers refer to.)
Expected: `94`.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add sarko-api && git commit -m "feat(api): tutorial_completed on the profile, latched by the first extraction"
```

---

### Task 2: Client — `GET /v1/profile`

Second, because the Shelter, the raid's tutorial branch and the garage readout all read the same parsed profile, and nothing can be built against it until it exists. `FSarkoBackendClient::Send` is POST-only today, so this task also teaches it a verb.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp`

**Interfaces:**
- Consumes: `FSarkoItemStack` (`Loot/SarkoItemCatalog.h`), `USarkoRaidSettings`, `FHttpModule`, the existing `ReadRoot` helper in `SarkoBackendClient.cpp`.
- Produces:
  - `FSarkoProfile { FString PlayerId; int32 SchemaVersion; TArray<FSarkoItemStack> Stash; FString VehicleTier; TArray<FString> UnlockedMaps; bool bTutorialCompleted; }`
  - `bool SarkoBackend::ParseProfileResponse(const FString& Json, FSarkoProfile& Out, FString& OutError)`
  - `FSarkoBackendClient::FOnProfile = TFunction<void(bool, const FSarkoProfile&, const FString&)>`
  - `void FSarkoBackendClient::FetchProfile(FOnProfile OnDone)`
  - `FSarkoBackendClient::Send` gains a leading `const TCHAR* Verb` parameter.

- [ ] **Step 1: Write the failing parser tests**

Append to `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp`, inside the existing `#if WITH_AUTOMATION_TESTS` block:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoParsesTheRealProfileResponse,
	"Sarko.Backend.ParsesTheRealProfileResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoParsesTheRealProfileResponse::RunTest(const FString& Parameters)
{
	// Copied from the deployed service, field for field. `stash` is ordered by
	// item_id server-side (store.Profile's ORDER BY), which is why the test can
	// assert positions.
	const FString Body = TEXT(R"({
		"player_id": "a9451008-9665-44d6-aeec-1305d61e53dd",
		"schema_version": 1,
		"stash": [
			{ "item_id": "ammo_9mm", "quantity": 60 },
			{ "item_id": "medkit",   "quantity": 1 },
			{ "item_id": "pistol",   "quantity": 1 }
		],
		"vehicle_tier": "none",
		"unlocked_maps": ["bridge"],
		"tutorial_completed": true
	})");

	FSarkoProfile Profile;
	FString Error;
	TestTrue(TEXT("the live profile shape parses"), SarkoBackend::ParseProfileResponse(Body, Profile, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("player id survives"), Profile.PlayerId, FString(TEXT("a9451008-9665-44d6-aeec-1305d61e53dd")));
	TestEqual(TEXT("schema version survives"), Profile.SchemaVersion, 1);
	TestEqual(TEXT("every stash row is read"), Profile.Stash.Num(), 3);
	TestEqual(TEXT("stash order is the server's"), Profile.Stash[0].Item, FName(TEXT("ammo_9mm")));
	TestEqual(TEXT("stash quantity survives"), Profile.Stash[0].Quantity, 60);
	TestEqual(TEXT("vehicle tier survives"), Profile.VehicleTier, FString(TEXT("none")));
	TestEqual(TEXT("unlocked maps are read"), Profile.UnlockedMaps.Num(), 1);
	TestEqual(TEXT("the only unlocked map is the one this build ships"),
		Profile.UnlockedMaps[0], FString(TEXT("bridge")));
	TestTrue(TEXT("tutorial_completed is read, not defaulted"), Profile.bTutorialCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAbsentTutorialFlagMeansTutorialMode,
	"Sarko.Backend.AbsentTutorialFlagMeansTutorialMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAbsentTutorialFlagMeansTutorialMode::RunTest(const FString& Parameters)
{
	// Spec §6.5: "no flag → tutorial mode". That has to hold for a *missing*
	// field too, not only for `false`, because a client running against a backend
	// that predates Task 1 receives no field at all. Defaulting the other way
	// would hand a brand-new player the full seeded loot economy on raid one and
	// skip the tutorial permanently, with nothing logging it.
	const FString Body = TEXT(R"({
		"player_id": "11111111-2222-3333-4444-555555555555",
		"schema_version": 1,
		"stash": [],
		"vehicle_tier": "none",
		"unlocked_maps": ["bridge"]
	})");

	FSarkoProfile Profile;
	FString Error;
	TestTrue(TEXT("a profile without the flag still parses"),
		SarkoBackend::ParseProfileResponse(Body, Profile, Error));
	TestFalse(TEXT("an absent flag reads as not completed"), Profile.bTutorialCompleted);
	TestEqual(TEXT("an empty stash is legitimate, not an error"), Profile.Stash.Num(), 0);

	// And the default on a freshly constructed struct must agree, because the
	// offline path never parses anything at all.
	const FSarkoProfile Fresh;
	TestFalse(TEXT("a default-constructed profile is in tutorial mode"), Fresh.bTutorialCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoProfileRejectsBadInput,
	"Sarko.Backend.ProfileRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoProfileRejectsBadInput::RunTest(const FString& Parameters)
{
	// The shelter draws this and the raid branches on it. A half-parsed profile
	// would show a player an empty stash they actually own, or silently pick the
	// wrong loot mode — both indistinguishable from a backend fault.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"),            TEXT("{{{") },
		{ TEXT("an error envelope"),   TEXT(R"({"error":{"code":"unauthorized","message":"no player in context"}})") },
		{ TEXT("no player_id"),        TEXT(R"({"schema_version":1,"vehicle_tier":"none"})") },
		{ TEXT("empty player_id"),     TEXT(R"({"player_id":"","schema_version":1,"vehicle_tier":"none"})") },
		{ TEXT("no vehicle_tier"),     TEXT(R"({"player_id":"p","schema_version":1})") },
		{ TEXT("stash not an array"),  TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":7})") },
		{ TEXT("stash row has no id"), TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[{"quantity":3}]})") },
		{ TEXT("stash row qty zero"),  TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[{"item_id":"chain","quantity":0}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoProfile Profile;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoBackend::ParseProfileResponse(Case.Value, Profile, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
		// A failed parse leaves nothing behind: a caller that ignores the return
		// value must not find a plausible-looking half-profile.
		TestTrue(FString::Printf(TEXT("nothing survives a failure: %s"), *Case.Key),
			Profile.PlayerId.IsEmpty() && Profile.Stash.Num() == 0);
	}
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `BUILD FAILED` — `unknown type name 'FSarkoProfile'` and `no member named 'ParseProfileResponse' in namespace 'SarkoBackend'`.

- [ ] **Step 3: Declare the profile and the call**

In `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h`, after `FSarkoRaidSession`:

```cpp
/**
 * What GET /v1/profile hands back. Field names below are the parser's business;
 * the wire names are exactly `player_id`, `schema_version`, `stash`,
 * `vehicle_tier`, `unlocked_maps`, `tutorial_completed` (store.Profile's JSON
 * tags in sarko-api/internal/store/players.go).
 *
 * Re-fetched on every shelter entry rather than cached across a raid: a level
 * travel destroys every actor, and the whole point of coming back to the shelter
 * is to see what the raid just credited.
 */
USTRUCT()
struct FSarkoProfile
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerId;

	UPROPERTY()
	int32 SchemaVersion = 0;

	/** Ordered by item id, server-side. The shelter draws it in that order. */
	UPROPERTY()
	TArray<FSarkoItemStack> Stash;

	/** `none|bicycle|motorcycle|car|helicopter` — domain.Tier. A string, not an
	 *  enum: the client only displays it, and an unknown future tier must not
	 *  fail a parse. */
	UPROPERTY()
	FString VehicleTier;

	UPROPERTY()
	TArray<FString> UnlockedMaps;

	/**
	 * False until the player's first *successful* raid (spec §6.5). While false,
	 * containers read the map's authored `fixedItems` instead of rolling.
	 *
	 * **Defaults to false, and an absent wire field parses as false**, so both
	 * "brand-new player" and "backend older than the flag" land in tutorial mode
	 * — the direction that shows static loot rather than skipping the tutorial
	 * forever.
	 */
	UPROPERTY()
	bool bTutorialCompleted = false;
};
```

In the same header, inside `namespace SarkoBackend`, alongside the other parsers:

```cpp
	/**
	 * Reads the profile. Every field is validated: a stash row with no id or a
	 * non-positive quantity fails the whole parse rather than being skipped,
	 * because a silently-shortened stash is a player being shown items they do
	 * not have — or not being shown items they do.
	 *
	 * `tutorial_completed` is the one optional field: absent means false.
	 */
	bool ParseProfileResponse(const FString& Json, FSarkoProfile& OutProfile, FString& OutError);
```

and on the class, next to the other calls:

```cpp
	using FOnProfile = TFunction<void(bool bSuccess, const FSarkoProfile& Profile, const FString& Error)>;

	/** GET /v1/profile. The only GET this client makes. */
	void FetchProfile(FOnProfile OnDone);
```

and change the private transport to carry a verb:

```cpp
private:
	/**
	 * One place that builds, sends and unwraps a request.
	 *
	 * Verb is explicit rather than inferred from "is the body empty": GET
	 * /v1/profile has no body, and a POST with an empty body is a legitimate
	 * shape too, so inferring it would make the two indistinguishable.
	 */
	void Send(const TCHAR* Verb, const FString& Path, const FString& Body, bool bAuthenticated,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& Error)> OnComplete);
```

- [ ] **Step 4: Implement the parser**

In `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.cpp`, after `ParseExpiresAtResponse`:

```cpp
bool SarkoBackend::ParseProfileResponse(const FString& Json, FSarkoProfile& OutProfile, FString& OutError)
{
	// Reset first, and again on every failure below: a caller that ignores the
	// return value must find an empty profile rather than a plausible half of one.
	OutProfile = FSarkoProfile();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}

	// An error envelope is valid JSON with none of these fields, and it arrives
	// on a 401 the moment the JWT expires. Named explicitly so the log says
	// "unauthorized" instead of "profile response has no 'player_id'".
	FSarkoBackendError Envelope;
	if (ParseErrorResponse(Json, Envelope))
	{
		OutError = FString::Printf(TEXT("profile request failed: %s — %s"), *Envelope.Code, *Envelope.Message);
		return false;
	}

	if (!Root->TryGetStringField(TEXT("player_id"), OutProfile.PlayerId) || OutProfile.PlayerId.IsEmpty())
	{
		OutError = TEXT("profile response has no 'player_id'");
		OutProfile = FSarkoProfile();
		return false;
	}
	if (!Root->TryGetStringField(TEXT("vehicle_tier"), OutProfile.VehicleTier) || OutProfile.VehicleTier.IsEmpty())
	{
		OutError = TEXT("profile response has no 'vehicle_tier'");
		OutProfile = FSarkoProfile();
		return false;
	}

	// Optional, and absence is not an error: schema_version is informational and
	// a future response may drop it.
	double SchemaVersion = 0.0;
	if (Root->TryGetNumberField(TEXT("schema_version"), SchemaVersion))
	{
		OutProfile.SchemaVersion = static_cast<int32>(SchemaVersion);
	}

	// `stash` may be absent (a brand-new player before the starter kit lands) or
	// empty. Present-but-not-an-array is a fault, and must not read as "empty".
	if (Root->HasField(TEXT("stash")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Stash = nullptr;
		if (!Root->TryGetArrayField(TEXT("stash"), Stash) || !Stash)
		{
			OutError = TEXT("profile response has a 'stash' that is not an array");
			OutProfile = FSarkoProfile();
			return false;
		}
		OutProfile.Stash.Reserve(Stash->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Stash)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'stash' contains a non-object entry");
				OutProfile = FSarkoProfile();
				return false;
			}
			FString ItemId;
			double Quantity = 0.0;
			if (!(*Object)->TryGetStringField(TEXT("item_id"), ItemId) || ItemId.IsEmpty())
			{
				OutError = TEXT("a stash row has no 'item_id'");
				OutProfile = FSarkoProfile();
				return false;
			}
			if (!(*Object)->TryGetNumberField(TEXT("quantity"), Quantity) || Quantity < 1.0)
			{
				OutError = FString::Printf(TEXT("stash row '%s' has no positive 'quantity'"), *ItemId);
				OutProfile = FSarkoProfile();
				return false;
			}
			OutProfile.Stash.Add(FSarkoItemStack{ FName(*ItemId), static_cast<int32>(Quantity) });
		}
	}

	if (Root->HasField(TEXT("unlocked_maps")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
		if (!Root->TryGetArrayField(TEXT("unlocked_maps"), Maps) || !Maps)
		{
			OutError = TEXT("profile response has an 'unlocked_maps' that is not an array");
			OutProfile = FSarkoProfile();
			return false;
		}
		OutProfile.UnlockedMaps.Reserve(Maps->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Maps)
		{
			FString MapId;
			if (Value->TryGetString(MapId) && !MapId.IsEmpty())
			{
				OutProfile.UnlockedMaps.Add(MapId);
			}
		}
	}

	// The one field whose absence is meaningful rather than merely tolerated:
	// absent == false == tutorial mode (spec §6.5).
	Root->TryGetBoolField(TEXT("tutorial_completed"), OutProfile.bTutorialCompleted);
	return true;
}
```

- [ ] **Step 5: Teach the transport a verb and add the call**

In the same `.cpp`, change `Send`'s definition and the four existing call sites. The signature:

```cpp
void FSarkoBackendClient::Send(const TCHAR* Verb, const FString& Path, const FString& Body, bool bAuthenticated,
	TFunction<void(bool, const FString&, const FString&)> OnComplete)
```

and inside it, replace the three lines that set the verb and the body with:

```cpp
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.BackendBaseUrl + Path);
	Request->SetVerb(Verb);
	if (!Body.IsEmpty())
	{
		// No Content-Type and no body on a GET: some proxies reject a GET that
		// declares a JSON body, and Go's http.Server will happily read one and
		// then ignore it, which makes a mistake here invisible.
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Body);
	}
	if (bAuthenticated)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Jwt);
	}
	Request->SetTimeout(Settings.BackendTimeoutSeconds);
```

Everything else in `Send` — the weak-self capture, the `bConnected`/response checks, the error-envelope unwrap, the `Error`-level log, and the deliberate *absence* of a second `OnComplete` call when `ProcessRequest()` returns false — stays exactly as it is.

Then update the four existing call sites to pass `TEXT("POST")` as the first argument:

```cpp
	Send(TEXT("POST"), TEXT("/v1/auth/anonymous"), SarkoBackend::MakeAnonymousBody(SarkoBackend::EnsureDeviceId()), /*bAuthenticated*/ false, ...
	Send(TEXT("POST"), TEXT("/v1/raid/start"), SarkoBackend::MakeRaidStartBody(MapId, Loadout), /*bAuthenticated*/ true, ...
	Send(TEXT("POST"), TEXT("/v1/raid/confirm"), SarkoBackend::MakeSessionBody(Session.SessionId, Session.SessionToken), /*bAuthenticated*/ true, ...
	Send(TEXT("POST"), TEXT("/v1/raid/result"), SarkoBackend::MakeRaidResultBody(...), /*bAuthenticated*/ true, ...
```

and add the new call at the end of the file:

```cpp
void FSarkoBackendClient::FetchProfile(FOnProfile OnDone)
{
	Send(TEXT("GET"), TEXT("/v1/profile"), FString(), /*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FSarkoProfile(), Error);
				return;
			}
			FSarkoProfile Profile;
			FString ParseError;
			if (!SarkoBackend::ParseProfileResponse(Body, Profile, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FSarkoProfile(), ParseError);
				return;
			}
			// The stash is logged by size, not by contents: it is the player's own
			// inventory and there is no reason for it to be in a log file, and on a
			// long-lived stash it would be dozens of lines every shelter entry.
			UE_LOG(LogTemp, Display,
				TEXT("SarkoBackend: profile for %s — %d stash rows, tier '%s', tutorial %s"),
				*Profile.PlayerId, Profile.Stash.Num(), *Profile.VehicleTier,
				Profile.bTutorialCompleted ? TEXT("completed") : TEXT("PENDING"));
			OnDone(true, Profile, FString());
		});
}
```

- [ ] **Step 6: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `13 test(s) performed, 0 failed`, `ALL GREEN` (10 before + the 3 added here).

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `59 test(s) performed, 0 failed` (56 before this plan + 3).

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): GET /v1/profile with a validated parser and tutorial_completed"
```

---

### Task 3: `USarkoGameInstance` and the travel seam

Third, because it is the only thing that survives `LoadMap`. Without it the Shelter and the raid are two islands: the JWT would be re-negotiated on every trip, and the raid's outcome would be destroyed with the game mode that produced it — so the Shelter could never say "вынесено: …" about a raid that has already ended.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Core/SarkoGameInstance.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Core/SarkoTravel.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp`
- Modify: `SarkoGame/Source/SarkoGame/SarkoGame.Build.cs` (+ `"Slate"`, `"SlateCore"`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (borrow the shared client)
- Modify: `SarkoGame/Config/DefaultEngine.ini` (`GameInstanceClass`)

**Interfaces:**
- Consumes: `FSarkoBackendClient` and `FSarkoProfile` (Task 2), `ESarkoRaidOutcome` and `FSarkoItemStack`.
- Produces:
  - `FSarkoLastRaid { ESarkoRaidOutcome Outcome; TArray<FSarkoItemStack> Haul; bool bPersisted; }`
  - `USarkoGameInstance` with `TSharedPtr<FSarkoBackendClient> EnsureBackend()`, `FSarkoProfile CachedProfile`, `bool bProfileLoaded`, `FSarkoLastRaid LastRaid`, `void RecordRaidOutcome(ESarkoRaidOutcome, const TArray<FSarkoItemStack>&, bool bPersisted)`, `bool HasFinishedARaid() const`
  - `SarkoTravel::ShelterMapName()`, `SarkoTravel::RaidOptions(int32 SeedOverride)`, `SarkoTravel::ShelterOptions()`, `SarkoTravel::TravelTo(UObject* WorldContext, const FString& Options)`

- [ ] **Step 1: Write the failing travel test**

Create `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoTravel.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTravelUrlsSelectTheRightGameMode,
	"Sarko.Shelter.TravelUrlsSelectTheRightGameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTravelUrlsSelectTheRightGameMode::RunTest(const FString& Parameters)
{
	// The raid is reached by a `game=` option on the travel URL and nothing else,
	// so a renamed or moved ASarkoRaidGameMode would silently send the player
	// into the shelter again — an infinite menu with no error anywhere.
	// UGameInstance::CreateGameModeForURL does LoadClass<AGameModeBase> on this
	// exact string; comparing it against the class's own path is what makes a
	// rename a red test instead of a broken game.
	const FString Expected = FString::Printf(TEXT("game=%s"),
		*ASarkoRaidGameMode::StaticClass()->GetPathName());
	TestEqual(TEXT("the raid option names the real raid game mode class"),
		SarkoTravel::RaidOptions(/*SeedOverride*/ 0), Expected);

	// A seed override is appended, not substituted: the game mode reads ?Seed=
	// in InitGame and it is the only reproduction tool this project has.
	const FString WithSeed = SarkoTravel::RaidOptions(3402905197);
	TestTrue(TEXT("a seed override keeps the game mode option"), WithSeed.Contains(Expected));
	TestTrue(TEXT("a seed override is appended as a URL option"), WithSeed.Contains(TEXT("?Seed=3402905197")));

	// The shelter is reached by the ABSENCE of a game= option, falling through to
	// GlobalDefaultGameMode. Anything non-empty here would be a second way to
	// pick a game mode and would eventually disagree with the ini.
	TestTrue(TEXT("the shelter carries no options at all"), SarkoTravel::ShelterOptions().IsEmpty());

	// The map is the engine's empty level, by full package path. A bare "Entry"
	// resolves against the project's content directory, which has no Entry, and
	// MakeSureMapNameIsValid only *warns* — the travel then silently does nothing.
	TestEqual(TEXT("both trips load the engine's empty level by full path"),
		SarkoTravel::ShelterMapName(), FString(TEXT("/Engine/Maps/Entry")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `BUILD FAILED` — `'Core/SarkoTravel.h' file not found`.

- [ ] **Step 3: Write the travel seam**

Create `SarkoGame/Source/SarkoGame/Core/SarkoTravel.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * Level travel, in one place.
 *
 * This project ships exactly one map (/Engine/Maps/Entry) because it authors no
 * .umap, so "going to the shelter" and "going on a raid" are the same level
 * loaded twice with a different game mode. The game mode is chosen by the travel
 * URL's `game=` option, which UGameInstance::CreateGameModeForURL resolves ahead
 * of GlobalDefaultGameMode — so the raid names its class and the shelter names
 * nothing and falls through to the ini.
 *
 * The URL builders are pure so the one string that decides which game the player
 * gets is unit tested; TravelTo is the single impure line, which is also the seam
 * to change to ServerTravel the day a dedicated server exists.
 */
namespace SarkoTravel
{
	/** Full package path, never a bare name — a bare name resolves against the
	 *  project's own content directory and only *warns* when it is missing. */
	FString ShelterMapName();

	/**
	 * `game=<raid game mode>`, plus `?Seed=<n>` when SeedOverride is non-zero.
	 *
	 * The class path comes from ASarkoRaidGameMode::StaticClass() rather than a
	 * literal, so renaming the class cannot silently route every raid back into
	 * the shelter.
	 */
	FString RaidOptions(int32 SeedOverride);

	/** Empty, on purpose: the absence of `game=` is what selects the shelter. */
	FString ShelterOptions();

	/**
	 * Absolute travel to ShelterMapName() with the given options.
	 *
	 * **Absolute, always.** FURL's relative constructor copies the base URL's
	 * option list, so a relative return trip would inherit `game=…RaidGameMode`
	 * from the outbound URL and start another raid — an infinite raid loop with
	 * nothing in any log to explain it.
	 */
	void TravelTo(UObject* WorldContext, const FString& Options);
}
```

Create `SarkoGame/Source/SarkoGame/Core/SarkoTravel.cpp`:

```cpp
#include "Core/SarkoTravel.h"

#include "Core/SarkoRaidGameMode.h"
#include "Kismet/GameplayStatics.h"

FString SarkoTravel::ShelterMapName()
{
	return TEXT("/Engine/Maps/Entry");
}

FString SarkoTravel::RaidOptions(int32 SeedOverride)
{
	FString Options = FString::Printf(TEXT("game=%s"), *ASarkoRaidGameMode::StaticClass()->GetPathName());
	if (SeedOverride != 0)
	{
		// Appended with its own '?', because UGameplayStatics::OpenLevel prefixes
		// the whole options string with exactly one '?' and every further option
		// needs its own separator.
		Options += FString::Printf(TEXT("?Seed=%d"), SeedOverride);
	}
	return Options;
}

FString SarkoTravel::ShelterOptions()
{
	return FString();
}

void SarkoTravel::TravelTo(UObject* WorldContext, const FString& Options)
{
	UE_LOG(LogTemp, Display, TEXT("SarkoTravel: travelling to %s with options '%s'"),
		*ShelterMapName(), Options.IsEmpty() ? TEXT("(none — the shelter)") : *Options);

	// bAbsolute = true. See the header: relative travel inherits the previous
	// URL's options, and inheriting `game=` here is an infinite raid loop.
	UGameplayStatics::OpenLevel(WorldContext, FName(*ShelterMapName()), /*bAbsolute*/ true, Options);
}
```

- [ ] **Step 4: Write the game instance**

Create `SarkoGame/Source/SarkoGame/Core/SarkoGameInstance.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
// Included, not forward-declared: FSarkoProfile is a USTRUCT held by value below.
#include "Net/SarkoBackendClient.h"
// For ESarkoRaidOutcome, which is declared with the game state.
#include "Core/SarkoRaidGameState.h"

#include "SarkoGameInstance.generated.h"

/**
 * What the shelter says about the raid the player just finished.
 *
 * Spec §6.5 moves the EXTRACTED summary out of the raid HUD and into the
 * shelter, and a level travel destroys the game mode that knew the outcome — so
 * the outcome and the haul have to be handed to something that outlives the
 * travel. This is that something.
 */
USTRUCT()
struct FSarkoLastRaid
{
	GENERATED_BODY()

	UPROPERTY()
	ESarkoRaidOutcome Outcome = ESarkoRaidOutcome::InProgress;

	/** What was carried out. Empty for every losing outcome, by construction:
	 *  ASarkoRaidGameMode::FinishRaid clears the backpack before it writes the
	 *  outcome (SarkoRaid::OutcomeLosesHaul is the rule). */
	UPROPERTY()
	TArray<FSarkoItemStack> Haul;

	/**
	 * False when the raid ran offline or the result submission failed, so the
	 * shelter can say "не збережено" instead of showing a haul that the stash
	 * below it does not contain. Without this the two halves of the shelter
	 * screen contradict each other and the player is told the network is fine.
	 */
	UPROPERTY()
	bool bPersisted = false;
};

/**
 * The one object that survives a level travel.
 *
 * Registered through GameInstanceClass in DefaultEngine.ini (UGameMapsSettings
 * is config=Engine — putting it in DefaultGame.ini silently loads
 * /Script/Engine.GameInstance and everything below quietly stops existing).
 *
 * It owns three things, all of them things that must not be re-derived per level:
 *  - the backend client, and with it the JWT: one anonymous auth per app launch
 *    instead of one per shelter/raid trip;
 *  - the last fetched profile, so the shelter can draw immediately and refresh
 *    behind the drawing;
 *  - the last raid's outcome and haul.
 */
UCLASS()
class USarkoGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/**
	 * The shared backend client, created on first use.
	 *
	 * Shared rather than owned outright by whoever asks: an HTTP completion can
	 * land after the world that started it is gone, and this object is what keeps
	 * the client alive across that boundary. ASarkoRaidGameMode::FinishRaid still
	 * captures it strongly in its own completion lambda — that is now belt and
	 * braces rather than the only thing holding it, and it stays because the
	 * lambda must outlive even this object during shutdown.
	 */
	TSharedPtr<class FSarkoBackendClient> EnsureBackend();

	/** Null when nothing has needed the backend yet. Never creates one. */
	TSharedPtr<class FSarkoBackendClient> GetBackend() const { return Backend; }

	/**
	 * The last profile fetched from /v1/profile. Read by the shelter (to draw)
	 * and by the raid (for bTutorialCompleted) — but the raid re-fetches rather
	 * than trusting this, because a raid may be entered directly from the command
	 * line with no shelter visit at all.
	 */
	UPROPERTY()
	FSarkoProfile CachedProfile;

	/** False until a profile has actually been fetched. Distinguishes "empty
	 *  stash" from "no idea yet", which the shelter draws differently. */
	UPROPERTY()
	bool bProfileLoaded = false;

	UPROPERTY()
	FSarkoLastRaid LastRaid;

	/** True once any raid has ended, so the shelter knows whether to draw a
	 *  summary block at all. InProgress is the "never raided" value. */
	bool HasFinishedARaid() const { return LastRaid.Outcome != ESarkoRaidOutcome::InProgress; }

	/** Called by ASarkoRaidGameMode as the raid settles, before it travels. */
	void RecordRaidOutcome(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul, bool bPersisted);

	/** Called by the shelter when a fresh profile lands. */
	void RecordProfile(const FSarkoProfile& Profile);

private:
	TSharedPtr<class FSarkoBackendClient> Backend;
};
```

Create `SarkoGame/Source/SarkoGame/Core/SarkoGameInstance.cpp`:

```cpp
#include "Core/SarkoGameInstance.h"

#include "Net/SarkoBackendClient.h"

TSharedPtr<FSarkoBackendClient> USarkoGameInstance::EnsureBackend()
{
	if (!Backend.IsValid())
	{
		Backend = MakeShared<FSarkoBackendClient>();
	}
	return Backend;
}

void USarkoGameInstance::RecordRaidOutcome(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul,
	bool bPersisted)
{
	LastRaid.Outcome = Outcome;
	LastRaid.Haul = Haul;
	LastRaid.bPersisted = bPersisted;

	// The profile is now stale by definition — the raid just changed the stash —
	// so the cached copy is marked unloaded rather than left to be drawn as if it
	// were current. The shelter re-fetches on entry; this is what stops it drawing
	// yesterday's stash for the one frame before that lands.
	bProfileLoaded = false;

	UE_LOG(LogTemp, Display, TEXT("SarkoGameInstance: raid ended %s with %d stacks, persisted: %s"),
		*UEnum::GetValueAsString(Outcome), Haul.Num(), bPersisted ? TEXT("yes") : TEXT("NO"));
}

void USarkoGameInstance::RecordProfile(const FSarkoProfile& Profile)
{
	CachedProfile = Profile;
	bProfileLoaded = true;
}
```

- [ ] **Step 5: Link Slate and register the game instance**

`SarkoGame/Source/SarkoGame/SarkoGame.Build.cs` — append two entries to the existing list. Do **not** reorder or remove anything, and do not add `"UMG"`:

```csharp
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NavigationSystem",
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"HTTP",
			// The shelter menu is Slate built in C++ (SNew), which is code rather
			// than an asset — UMG widgets are .uasset and this project ships none.
			// Both are already reachable transitively through Engine's public
			// dependencies; listing them means an Engine change cannot silently
			// take them away.
			"Slate",
			"SlateCore"
		});
```

`SarkoGame/Config/DefaultEngine.ini` — add one line to the **existing** `[/Script/EngineSettings.GameMapsSettings]` section. Do not create a second section, and do not touch `GlobalDefaultGameMode` yet (Task 4 does that, once a Shelter game mode exists to point at):

```ini
; UGameInstance survives LoadMap, and it is the only thing that does. The JWT,
; the fetched profile and the last raid's outcome all live here, because a level
; travel destroys every actor including the game mode that knew them.
; UGameMapsSettings is config=Engine, which is why this line is in this file.
GameInstanceClass=/Script/SarkoGame.SarkoGameInstance
```

- [ ] **Step 6: Borrow the shared client in the raid**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.cpp`, add `#include "Core/SarkoGameInstance.h"` and replace the first two lines of `BeginBackendSession`:

```cpp
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
	// on every raid after the first of a launch. Task 6 replaces this lambda's
	// body with the profile hop; the auth skip stays.
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
```

Move the existing `StartRaid` → `ConfirmRaid` → `ActivateRaid` chain — verbatim, including every comment, in particular the long one explaining why the wire loadout is empty — into a new private method `void ASarkoRaidGameMode::OnAuthenticated()`, declared in `SarkoRaidGameMode.h` next to `BeginBackendSession`:

```cpp
	/**
	 * Everything after auth: (from Task 6) the profile hop, then start, confirm
	 * and ActivateRaid. Split out of BeginBackendSession because auth is now
	 * skipped whenever the player arrived from the shelter with a live JWT, so the
	 * rest of the chain has two entry points.
	 */
	void OnAuthenticated();
```

- [ ] **Step 7: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `1 test(s) performed, 0 failed`, `ALL GREEN`.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `60 test(s) performed, 0 failed`.

The raid must still run end to end after the refactor — the auth split is the kind of change that compiles and then does nothing:

```bash
cd SarkoGame
echo "a5-t3-$(date +%s)" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="t.MaxFPS 5" 2>/dev/null &
sleep 25 && kill %1 2>/dev/null
grep -E "authenticated as player|raid session .* opened|raid confirmed|raid live" \
  ~/Library/Logs/SarkoGame/SarkoGame.log | tail -5
```

Expected: all four lines present, in that order. `playing OFFLINE` in that log means the refactor broke the chain — fix it before committing. (The raid still boots directly here because `GlobalDefaultGameMode` is untouched until Task 4.)

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): USarkoGameInstance owns the backend client, and a tested travel seam"
```

---

### Task 4: The Shelter — pure view model, Slate widget, game mode, boot flow

Fourth, and the deliverable of the plan's first half. Every string on screen is produced by a pure function tested with no world; the widget is a dumb consumer of a struct; the game mode has no pawn and no HUD.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterGameMode.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp` (3 new tests)
- Modify: `SarkoGame/Config/DefaultEngine.ini` (`GlobalDefaultGameMode`)

**Interfaces:**
- Consumes: `FSarkoProfile`, `USarkoGameInstance`, `FSarkoLastRaid`, `SarkoTravel`, `SarkoLoot::GetItemCatalog()`, `FSarkoItemCatalog::Find`.
- Produces:
  - `FSarkoShelterView { FString Title; FString OutcomeTitle; TArray<FString> HaulLines; FString GarageLine; TArray<FString> StashLines; FString StatusLine; bool bRaidEnabled; }`
  - `SarkoShelter::BicycleRecipe() -> TArray<FSarkoItemStack>`
  - `SarkoShelter::BuildStashLines(const FSarkoProfile&, const FSarkoItemCatalog&) -> TArray<FString>`
  - `SarkoShelter::BuildGarageLine(const FSarkoProfile&) -> FString`
  - `SarkoShelter::BuildView(const FSarkoLastRaid&, const FSarkoProfile&, bool bProfileLoaded, const FString& Error, const FSarkoItemCatalog&) -> FSarkoShelterView`
  - `SSarkoShelterWidget` with `SLATE_EVENT(FSimpleDelegate, OnEnterRaid)` and `void SetView(const FSarkoShelterView&)`
  - `ASarkoShelterGameMode`, `ASarkoShelterPlayerController`
- `SarkoShelter::BuildOutcomeTitle` / `BuildHaulLines` are **Task 5's** additions; `BuildView` gains them there. In this task `BuildView` leaves `OutcomeTitle` empty and `HaulLines` empty.

- [ ] **Step 1: Write the failing view tests**

Append to `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp` (and add `#include "Shelter/SarkoShelterView.h"` and `#include "Loot/SarkoItemCatalog.h"` at the top):

```cpp
namespace
{
	/** A catalog built in the test rather than read from disk, so these tests pin
	 *  the *formatting* rules and do not fail when items.json gains an entry. */
	FSarkoItemCatalog MakeTestCatalog()
	{
		FSarkoItemCatalog Catalog;
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("scrap_metal")), TEXT("Металолом"), 10, ESarkoItemCategory::Junk });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("medkit")),      TEXT("Аптечка"),   3,  ESarkoItemCategory::Med });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("bike_frame")),  TEXT("Рама"),      1,  ESarkoItemCategory::VehiclePart });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("chain")),       TEXT("Ланцюг"),    1,  ESarkoItemCategory::VehiclePart });
		Catalog.Items.Add(FSarkoItemDef{ FName(TEXT("wheel_small")), TEXT("Колесо"),    2,  ESarkoItemCategory::VehiclePart });
		return Catalog;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStashLinesUseUkrainianNames,
	"Sarko.Shelter.StashLinesUseUkrainianNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStashLinesUseUkrainianNames::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = MakeTestCatalog();

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	Profile.Stash = {
		FSarkoItemStack{ FName(TEXT("scrap_metal")), 14 },
		FSarkoItemStack{ FName(TEXT("medkit")), 2 },
		// An id the catalog does not know. It must still be shown: a stash row the
		// player owns and cannot see is worse than an ugly line, and an id on
		// screen is the visible symptom of items.json drifting from the backend.
		FSarkoItemStack{ FName(TEXT("mystery_cog")), 1 },
	};

	const TArray<FString> Lines = SarkoShelter::BuildStashLines(Profile, Catalog);
	TestEqual(TEXT("one line per stash row, none dropped"), Lines.Num(), 3);
	TestEqual(TEXT("the UA display name is used, never the id"), Lines[0], FString(TEXT("Металолом  x14")));
	TestEqual(TEXT("server order is preserved"), Lines[1], FString(TEXT("Аптечка  x2")));
	TestEqual(TEXT("an unknown id falls back to the id itself"), Lines[2], FString(TEXT("mystery_cog  x1")));

	FSarkoProfile Empty;
	Empty.PlayerId = TEXT("p");
	Empty.VehicleTier = TEXT("none");
	const TArray<FString> EmptyLines = SarkoShelter::BuildStashLines(Empty, Catalog);
	TestEqual(TEXT("an empty stash says so rather than drawing nothing"), EmptyLines.Num(), 1);
	TestEqual(TEXT("and says it in Ukrainian"), EmptyLines[0], FString(TEXT("СХОВОК ПОРОЖНІЙ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoGarageLineCountsBicycleParts,
	"Sarko.Shelter.GarageLineCountsBicycleParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoGarageLineCountsBicycleParts::RunTest(const FString& Parameters)
{
	// Spec §6.5 asks for "bicycle 0/3 parts". Three is the number of recipe
	// ENTRIES, not the number of units (the recipe is 1 frame + 2 wheels + 1
	// chain = 4 units), and a part counts only when the stash holds the full
	// required quantity — two wheels, not one.
	TestEqual(TEXT("the mirrored recipe has three entries"), SarkoShelter::BicycleRecipe().Num(), 3);

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	TestEqual(TEXT("an empty stash is 0/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 0/3")));

	Profile.Stash = { FSarkoItemStack{ FName(TEXT("bike_frame")), 1 } };
	TestEqual(TEXT("one satisfied entry is 1/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 1/3")));

	// One wheel of the two required does not count: the craft call would be
	// rejected, and a shelter that says 2/3 while /v1/garage/craft says no is
	// lying to the player.
	Profile.Stash.Add(FSarkoItemStack{ FName(TEXT("wheel_small")), 1 });
	TestEqual(TEXT("a partially-held entry does not count"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 1/3")));

	Profile.Stash.Last().Quantity = 2;
	Profile.Stash.Add(FSarkoItemStack{ FName(TEXT("chain")), 1 });
	TestEqual(TEXT("all three entries held is 3/3"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 3/3")));

	// Past the bicycle, the line names the tier the player has rather than
	// counting parts they no longer need.
	Profile.VehicleTier = TEXT("bicycle");
	TestEqual(TEXT("an owned bicycle is reported as owned"), SarkoShelter::BuildGarageLine(Profile),
		FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShelterViewSeparatesUnknownFromEmpty,
	"Sarko.Shelter.ViewSeparatesUnknownFromEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShelterViewSeparatesUnknownFromEmpty::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = MakeTestCatalog();
	const FSarkoLastRaid NoRaidYet;

	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");

	// Not fetched yet: the stash is unknown, not empty. Drawing "СХОВОК
	// ПОРОЖНІЙ" here would tell a player their raid credited nothing.
	const FSarkoShelterView Loading = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ false,
		FString(), Catalog);
	TestEqual(TEXT("the title is the shelter's name"), Loading.Title, FString(TEXT("УКРИТТЯ")));
	TestEqual(TEXT("an unfetched profile draws no stash lines at all"), Loading.StashLines.Num(), 0);
	TestEqual(TEXT("and says it is connecting"), Loading.StatusLine, FString(TEXT("З'ЄДНАННЯ...")));
	TestFalse(TEXT("the raid button is disabled until the profile lands"), Loading.bRaidEnabled);

	// Fetched and genuinely empty.
	const FSarkoShelterView Loaded = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ true,
		FString(), Catalog);
	TestEqual(TEXT("a fetched empty stash says so"), Loaded.StashLines.Num(), 1);
	TestEqual(TEXT("no status line once the profile is in"), Loaded.StatusLine, FString());
	TestTrue(TEXT("the raid button is live once the profile is in"), Loaded.bRaidEnabled);
	TestEqual(TEXT("the garage line is present"), Loaded.GarageLine, FString(TEXT("ГАРАЖ: ВЕЛОСИПЕД 0/3")));

	// Failed: the error is shown verbatim and the raid is still allowed, because
	// spec §4.6's offline degradation says the game never hard-locks on network —
	// an offline raid plays and persists nothing.
	const FSarkoShelterView Failed = SarkoShelter::BuildView(NoRaidYet, Profile, /*bProfileLoaded*/ false,
		TEXT("/v1/profile: HTTP 401 unauthorized"), Catalog);
	TestEqual(TEXT("the error is shown, not swallowed"), Failed.StatusLine,
		FString(TEXT("ОФЛАЙН: /v1/profile: HTTP 401 unauthorized")));
	TestTrue(TEXT("an offline shelter can still start a raid"), Failed.bRaidEnabled);
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `BUILD FAILED` — `'Shelter/SarkoShelterView.h' file not found`.

- [ ] **Step 3: Write the view model**

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Core/SarkoGameInstance.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"

/**
 * Everything the shelter shows, as plain strings.
 *
 * The whole menu is decided here and merely *drawn* by SSarkoShelterWidget, for
 * the same reason the map parser is separate from the map spawner: a function
 * taking values and returning values is testable under -nullrhi, where there is
 * no Slate application to build a widget into. It is also what will survive when
 * this Slate menu is eventually rebuilt in UMG — a UMG widget consumes exactly
 * these strings.
 */
struct FSarkoShelterView
{
	/** "УКРИТТЯ". */
	FString Title;

	/** "ВИНЕСЕНО" / "ЗАГИНУВ" / "ЗНИК БЕЗВІСТИ", or empty before the first raid. Task 5. */
	FString OutcomeTitle;

	/** The haul, one line per stack. Task 5. */
	TArray<FString> HaulLines;

	/** "ГАРАЖ: ВЕЛОСИПЕД 1/3". */
	FString GarageLine;

	/** One line per stash row, or a single "СХОВОК ПОРОЖНІЙ". **Empty** — not the
	 *  porozhniy line — while the profile has not been fetched. */
	TArray<FString> StashLines;

	/** "З'ЄДНАННЯ..." while fetching, "ОФЛАЙН: <reason>" on failure, empty when
	 *  everything is current. */
	FString StatusLine;

	/** False only while the very first profile fetch is still in flight. An
	 *  offline shelter still lets the player raid (spec §4.6). */
	bool bRaidEnabled = false;
};

namespace SarkoShelter
{
	/**
	 * The bicycle recipe, mirrored from sarko-api/internal/domain/garage.go's
	 * unexported `recipes[TierBicycle]`.
	 *
	 * Mirrored because no endpoint exposes a recipe: /v1/profile returns
	 * `vehicle_tier` and `stash`, and /v1/garage/craft only accepts or refuses.
	 * The three ids are already pinned to garage.go by the existing
	 * Sarko.Loot.RealItemCatalogIsUsable test, so a rename on the backend turns a
	 * test red rather than quietly making this readout wrong. Adding
	 * GET /v1/garage/recipe is the proper fix and is out of this stage's scope.
	 */
	TArray<FSarkoItemStack> BicycleRecipe();

	/**
	 * One "<UA name>  x<qty>" line per stash row, in the server's order, with the
	 * item id as the fallback label for an id the catalog does not know — an id on
	 * screen is the visible symptom of items.json drifting from the backend, and
	 * hiding the row instead would hide items the player actually owns.
	 *
	 * A fetched-but-empty stash yields exactly one line, "СХОВОК ПОРОЖНІЙ".
	 */
	TArray<FString> BuildStashLines(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog);

	/**
	 * "ГАРАЖ: ВЕЛОСИПЕД n/3", counting recipe *entries* whose full required
	 * quantity is in the stash — one wheel of two does not count, because the
	 * craft call would refuse and a shelter that disagrees with the backend is
	 * worse than one that says less. Past TierNone it reports the tier as built.
	 */
	FString BuildGarageLine(const FSarkoProfile& Profile);

	/** Assembles the whole screen. Pure. */
	FSarkoShelterView BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
		bool bProfileLoaded, const FString& Error, const FSarkoItemCatalog& Catalog);
}
```

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.cpp`:

```cpp
#include "Shelter/SarkoShelterView.h"

TArray<FSarkoItemStack> SarkoShelter::BicycleRecipe()
{
	// Verbatim from garage.go's recipes[TierBicycle]. Quantities included: the
	// readout is wrong without them (two wheels, not one).
	return {
		FSarkoItemStack{ FName(TEXT("bike_frame")), 1 },
		FSarkoItemStack{ FName(TEXT("wheel_small")), 2 },
		FSarkoItemStack{ FName(TEXT("chain")), 1 },
	};
}

TArray<FString> SarkoShelter::BuildStashLines(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog)
{
	TArray<FString> Lines;
	if (Profile.Stash.Num() == 0)
	{
		Lines.Add(TEXT("СХОВОК ПОРОЖНІЙ"));
		return Lines;
	}

	Lines.Reserve(Profile.Stash.Num());
	for (const FSarkoItemStack& Stack : Profile.Stash)
	{
		const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
		Lines.Add(FString::Printf(TEXT("%s  x%d"),
			Def ? *Def->Name : *Stack.Item.ToString(), Stack.Quantity));
	}
	return Lines;
}

FString SarkoShelter::BuildGarageLine(const FSarkoProfile& Profile)
{
	// Anything past the starting tier already owns the bicycle: the garage ladder
	// is cumulative (domain.UnlockedMaps walks tierOrder), so "not none" means
	// built. Compared against the literal "none" rather than an enum because
	// vehicle_tier is a string on the wire and an unknown future tier must not
	// crash this readout.
	if (!Profile.VehicleTier.IsEmpty() && Profile.VehicleTier != TEXT("none"))
	{
		return TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ");
	}

	const TArray<FSarkoItemStack> Recipe = BicycleRecipe();
	int32 Met = 0;
	for (const FSarkoItemStack& Part : Recipe)
	{
		// Linear over both lists: three entries against a stash of at most a few
		// dozen rows, computed once per profile fetch rather than per frame.
		const FSarkoItemStack* Held = Profile.Stash.FindByPredicate(
			[&Part](const FSarkoItemStack& Stack) { return Stack.Item == Part.Item; });
		if (Held && Held->Quantity >= Part.Quantity)
		{
			++Met;
		}
	}
	return FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД %d/%d"), Met, Recipe.Num());
}

FSarkoShelterView SarkoShelter::BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
	bool bProfileLoaded, const FString& Error, const FSarkoItemCatalog& Catalog)
{
	FSarkoShelterView View;
	View.Title = TEXT("УКРИТТЯ");

	// Task 5 fills OutcomeTitle and HaulLines from LastRaid. Referenced here so
	// the signature is stable and the parameter is not "unused" in a warning.
	(void)LastRaid;

	View.GarageLine = BuildGarageLine(Profile);

	// The stash is drawn only when it is known. An unfetched profile has an empty
	// Stash array, which is indistinguishable from a genuinely empty stash unless
	// this branch exists — and telling a player their haul vanished is the single
	// worst thing this screen can do.
	if (bProfileLoaded)
	{
		View.StashLines = BuildStashLines(Profile, Catalog);
	}

	if (!Error.IsEmpty())
	{
		// Verbatim, including the endpoint and the HTTP code: this is the player's
		// only view of spec §4.6's loud degradation, and a friendly paraphrase
		// would cost the one piece of information that identifies the fault.
		View.StatusLine = FString::Printf(TEXT("ОФЛАЙН: %s"), *Error);
	}
	else if (!bProfileLoaded)
	{
		View.StatusLine = TEXT("З'ЄДНАННЯ...");
	}

	// Disabled only while the first fetch is genuinely still in flight. A failed
	// fetch still allows a raid: the raid degrades to offline on its own and the
	// game must never hard-lock on the network (spec §4.6).
	View.bRaidEnabled = bProfileLoaded || !Error.IsEmpty();
	return View;
}
```

- [ ] **Step 4: Run the view tests and confirm they pass**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `4 test(s) performed, 0 failed`, `ALL GREEN` — `TravelUrlsSelectTheRightGameMode`, `StashLinesUseUkrainianNames`, `GarageLineCountsBicycleParts`, `ViewSeparatesUnknownFromEmpty`.

- [ ] **Step 5: Write the Slate widget**

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Shelter/SarkoShelterView.h"

class STextBlock;
class SVerticalBox;

/**
 * The shelter menu.
 *
 * Slate built in C++, not UMG: a UMG widget is a .uasset and this project ships
 * no binary assets. Every style comes from FCoreStyle, which is compiled into
 * SlateCore — so this looks like a debug menu, and that is the honest cost of
 * "MVP shelter, zero assets" (see the plan's Decision 1).
 *
 * It owns no state and decides nothing: SetView hands it an already-built
 * FSarkoShelterView and it writes those strings into its text blocks. That is
 * what keeps every rule in this screen testable under -nullrhi, where no Slate
 * application exists.
 */
class SSarkoShelterWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSarkoShelterWidget) {}
		/** Fired when "В РЕЙД" is pressed. The controller owns the travel. */
		SLATE_EVENT(FSimpleDelegate, OnEnterRaid)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the changing text. Called once per profile fetch, never per frame. */
	void SetView(const FSarkoShelterView& View);

private:
	FReply HandleEnterRaid();

	FSimpleDelegate OnEnterRaid;

	/** True while the first profile fetch is in flight; the raid button reads it
	 *  through an attribute, so no per-frame work is needed to keep it in sync. */
	bool bRaidEnabled = false;

	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> OutcomeText;
	TSharedPtr<STextBlock> GarageText;
	TSharedPtr<STextBlock> StatusText;

	/** Rebuilt wholesale on SetView. A few dozen rows, a few times per session. */
	TSharedPtr<SVerticalBox> HaulBox;
	TSharedPtr<SVerticalBox> StashBox;
};
```

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.cpp`:

```cpp
#include "Shelter/SarkoShelterWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** FCoreStyle is compiled into SlateCore, so no font asset is involved. */
	FSlateFontInfo ShelterFont(float Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	const FSlateColor Dim(FLinearColor(0.72f, 0.72f, 0.74f));
	const FSlateColor Warn(FLinearColor(1.f, 0.75f, 0.2f));
}

void SSarkoShelterWidget::Construct(const FArguments& InArgs)
{
	OnEnterRaid = InArgs._OnEnterRaid;

	ChildSlot
	[
		// Opaque, full-screen: /Engine/Maps/Entry behind this is an empty void and
		// a translucent menu over it reads as a rendering fault.
		SNew(SBorder)
		.BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.07f, 1.f))
		.Padding(FMargin(48.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SAssignNew(TitleText, STextBlock)
				.Font(ShelterFont(36.f))
				.Text(FText::FromString(TEXT("УКРИТТЯ")))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SAssignNew(OutcomeText, STextBlock)
				.Font(ShelterFont(20.f))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SAssignNew(HaulBox, SVerticalBox)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SAssignNew(GarageText, STextBlock)
				.Font(ShelterFont(20.f))
			]

			// The stash can be any length, which is the reason this screen is Slate
			// and not DrawHUD primitives: SScrollBox is the whole feature.
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(StashBox, SVerticalBox)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 8.f)
			[
				SAssignNew(StatusText, STextBlock)
				.Font(ShelterFont(16.f))
				.ColorAndOpacity(Warn)
			]

			// Buttons last, so they sit in the lower-middle third — reachable by
			// either thumb, and clear of the edges (see the plan's touch constraint).
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(48.f, 20.f))
					// An attribute, not a one-shot value: SetView flips the flag and
					// Slate re-reads it, so nothing has to tick to keep the button
					// honest.
					.IsEnabled_Lambda([this]() { return bRaidEnabled; })
					.OnClicked(this, &SSarkoShelterWidget::HandleEnterRaid)
					[
						SNew(STextBlock).Font(ShelterFont(28.f)).Text(FText::FromString(TEXT("В РЕЙД")))
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f)
				[
					// Stub, and disabled rather than absent: spec §6.5 wants the shop
					// visible so the shape of the shelter is right, and it says
					// "subscription later, no P2W" — a button that did anything now
					// would be a design decision this stage has not made.
					SNew(SButton)
					.ContentPadding(FMargin(24.f, 20.f))
					.IsEnabled(false)
					[
						SNew(STextBlock)
						.Font(ShelterFont(18.f))
						.ColorAndOpacity(Dim)
						.Text(FText::FromString(TEXT("МАГАЗИН — НЕЗАБАРОМ")))
					]
				]
			]
		]
	];
}

void SSarkoShelterWidget::SetView(const FSarkoShelterView& View)
{
	bRaidEnabled = View.bRaidEnabled;

	TitleText->SetText(FText::FromString(View.Title));
	OutcomeText->SetText(FText::FromString(View.OutcomeTitle));
	GarageText->SetText(FText::FromString(View.GarageLine));
	StatusText->SetText(FText::FromString(View.StatusLine));

	const auto Fill = [](const TSharedPtr<SVerticalBox>& Box, const TArray<FString>& Lines, float Size)
	{
		Box->ClearChildren();
		for (const FString& Line : Lines)
		{
			Box->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Font(ShelterFont(Size)).Text(FText::FromString(Line))
			];
		}
	};

	Fill(HaulBox, View.HaulLines, 18.f);
	Fill(StashBox, View.StashLines, 18.f);
}

FReply SSarkoShelterWidget::HandleEnterRaid()
{
	OnEnterRaid.ExecuteIfBound();
	return FReply::Handled();
}
```

- [ ] **Step 6: Write the game mode and the controller**

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterGameMode.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "SarkoShelterGameMode.generated.h"

/**
 * The main menu, as a game mode.
 *
 * No pawn, no HUD, no map: /Engine/Maps/Entry is an empty level and the whole
 * screen is a Slate widget the player controller puts in the viewport. It exists
 * as a game mode rather than as a widget on the raid game mode because the two
 * must not share a world — a shelter that ran alongside ASarkoRaidGameMode would
 * inherit its InitGame (which loads bridge.json and spawns 42 containers) and its
 * StartPlay (which opens a backend raid session).
 */
UCLASS()
class ASarkoShelterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASarkoShelterGameMode();
};
```

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterGameMode.cpp`:

```cpp
#include "Shelter/SarkoShelterGameMode.h"

#include "Shelter/SarkoShelterPlayerController.h"

ASarkoShelterGameMode::ASarkoShelterGameMode()
{
	// Nothing ticks and nothing spawns. The default AGameModeBase behaviour is
	// already this; every line below is an explicit refusal of something the raid
	// game mode does, so a future edit has to argue with a name rather than a
	// silence.
	PrimaryActorTick.bCanEverTick = false;

	PlayerControllerClass = ASarkoShelterPlayerController::StaticClass();

	// No pawn: there is nothing to walk around in. bStartPlayersAsSpectators
	// keeps RestartPlayer from running at all, so a null DefaultPawnClass cannot
	// produce the "failed to spawn pawn" warning on every boot.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;

	// No AHUD. The menu is Slate in the viewport, and an AHUD here would draw
	// underneath it for no reason.
	HUDClass = nullptr;
}
```

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SarkoShelterPlayerController.generated.h"

/**
 * Owns the shelter widget, fetches the profile, and starts the raid.
 *
 * The widget lives here rather than on the game mode because a viewport widget
 * belongs to a local player, and because EndPlay is the one place guaranteed to
 * run before a level travel — a widget that is not removed there survives into
 * the raid and covers the HUD, with the raid still fully playable underneath it.
 */
UCLASS()
class ASarkoShelterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASarkoShelterPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Rebuilds the view from the game instance's state and hands it to the widget. */
	void RefreshWidget();

	/** Authenticates if needed, then GETs /v1/profile. One round trip per entry. */
	void FetchProfile();

	void EnterRaid();

	TSharedPtr<class SSarkoShelterWidget> Widget;

	/** The last failure, shown verbatim. Empty when everything is current. */
	FString LastError;
};
```

Create `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.cpp`:

```cpp
#include "Shelter/SarkoShelterPlayerController.h"

#include "Core/SarkoGameInstance.h"
#include "Core/SarkoTravel.h"
#include "Engine/GameViewportClient.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"
#include "Shelter/SarkoShelterView.h"
#include "Shelter/SarkoShelterWidget.h"

ASarkoShelterPlayerController::ASarkoShelterPlayerController()
{
	// A menu wants a cursor on desktop; on a phone Slate routes touches as
	// pointer events and this changes nothing.
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ASarkoShelterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Local only: a widget belongs to a viewport, and a remote controller has
	// none. Also the guard that keeps this doing nothing in a headless automation
	// run, where GetGameViewport() is null.
	if (!IsLocalController())
	{
		return;
	}
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoShelter: no game viewport, so no menu. Headless run?"));
		return;
	}

	Widget = SNew(SSarkoShelterWidget)
		.OnEnterRaid(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::EnterRaid));

	Viewport->AddViewportWidgetContent(Widget.ToSharedRef());

	// UI only: there is no pawn and nothing to steer, and leaving game input live
	// would let the desktop test keys (WASD/E from ASarkoPlayerController's
	// #if !UE_BUILD_SHIPPING path) reach a world that has no pawn in it.
	SetInputMode(FInputModeUIOnly().SetWidgetToFocus(Widget));

	// Draw immediately with whatever the game instance already knows — the raid's
	// outcome, and the previous profile if there is one — then refresh behind it.
	RefreshWidget();
	FetchProfile();
}

void ASarkoShelterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Before Super, and unconditionally. A viewport widget is not an actor and is
	// not destroyed with the level: leaving it added means the shelter menu is
	// still on screen during the raid, on top of the HUD, with the raid playing
	// underneath it.
	if (Widget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
		Widget.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ASarkoShelterPlayerController::RefreshWidget()
{
	const USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!Widget.IsValid() || !GameInstance)
	{
		return;
	}
	Widget->SetView(SarkoShelter::BuildView(
		GameInstance->LastRaid, GameInstance->CachedProfile, GameInstance->bProfileLoaded,
		LastError, SarkoLoot::GetItemCatalog()));
}

void ASarkoShelterPlayerController::FetchProfile()
{
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!GameInstance)
	{
		LastError = TEXT("no USarkoGameInstance — check GameInstanceClass in DefaultEngine.ini");
		RefreshWidget();
		return;
	}

	const TSharedPtr<FSarkoBackendClient> Backend = GameInstance->EnsureBackend();

	// Weak through both hops: these completions routinely land after the player
	// has pressed В РЕЙД and this controller has been destroyed by the travel.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);

	const auto Fetch = [WeakThis, Backend]()
	{
		Backend->FetchProfile([WeakThis](bool bSuccess, const FSarkoProfile& Profile, const FString& Error)
		{
			ASarkoShelterPlayerController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			USarkoGameInstance* Instance = Self->GetGameInstance<USarkoGameInstance>();
			if (bSuccess && Instance)
			{
				Instance->RecordProfile(Profile);
				Self->LastError.Reset();
			}
			else
			{
				Self->LastError = Error;
			}
			Self->RefreshWidget();
		});
	};

	if (Backend->IsAuthenticated())
	{
		Fetch();
		return;
	}

	Backend->Authenticate([WeakThis, Fetch](bool bAuthenticated, const FString& Error)
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (!bAuthenticated)
		{
			Self->LastError = Error;
			Self->RefreshWidget();
			return;
		}
		Fetch();
	});
}

void ASarkoShelterPlayerController::EnterRaid()
{
	UE_LOG(LogTemp, Display, TEXT("SarkoShelter: entering the raid"));
	// Seed 0 means "no override", so the raid uses the backend's seed (or its own
	// default offline). ?Seed= stays a command-line tool for reproducing a raid.
	SarkoTravel::TravelTo(this, SarkoTravel::RaidOptions(/*SeedOverride*/ 0));
}
```

- [ ] **Step 7: Make boot land in the Shelter**

`SarkoGame/Config/DefaultEngine.ini` — change **one existing line** in `[/Script/EngineSettings.GameMapsSettings]`. Leave `GameDefaultMap`, `ServerDefaultMap`, `EditorStartupMap` and the `GameInstanceClass` line from Task 3 exactly as they are:

```ini
; Boot lands in the shelter (spec §6.5: boot -> shelter -> "В РЕЙД" -> raid ->
; outcome -> shelter). The raid is reached by a `game=` option on the travel URL,
; which UGameInstance::CreateGameModeForURL resolves ahead of this line; the
; return trip carries no options and therefore falls back to it.
GlobalDefaultGameMode=/Script/SarkoGame.SarkoShelterGameMode
```

- [ ] **Step 8: Prove the suite still runs, then prove the flow by hand**

The ini change is exactly the kind of edit that breaks the automation harness silently, so check the count first:

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `63 test(s) performed, 0 failed` (60 after Task 3 + 3). If the number is 0, the editor failed to start — read `Saved/Logs/agent-build.log` and the engine log; a `GlobalDefaultGameMode` that fails to load is a Warning, not an error, so the symptom will be further down.

Then the flow, headlessly. The Shelter draws nothing useful offscreen (Slate under `-RenderOffscreen` renders, but there is nobody to click), so this pass verifies the **routing** — which game mode each trip gets:

```bash
cd SarkoGame
echo "a5-t4-$(date +%s)" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="t.MaxFPS 5" 2>/dev/null &
sleep 20 && kill %1 2>/dev/null
grep -E "SarkoShelter|profile for |SarkoRaidGameMode:" ~/Library/Logs/SarkoGame/SarkoGame.log | head -10
```

Expected: `SarkoBackend: authenticated as player <uuid>` then `SarkoBackend: profile for <uuid> — N stash rows, tier 'none', tutorial PENDING`, and **no** `SarkoRaidGameMode:` line at all — boot must not start a raid any more. `SarkoShelter: no game viewport` is acceptable under `-RenderOffscreen` on some configurations; the profile line is the proof that matters.

Then the same, forced into the raid, which is the route "В РЕЙД" takes:

```bash
cd SarkoGame
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="t.MaxFPS 5" 2>/dev/null &
sleep 25 && kill %1 2>/dev/null
grep -E "spawned 42 loot containers|raid live" ~/Library/Logs/SarkoGame/SarkoGame.log | tail -3
```

Expected: both lines present — the `game=` option really does override `GlobalDefaultGameMode`.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): the Shelter — Slate menu, stash, garage, and boot lands there"
```

---

### Task 5: Raid → Shelter, and the summary moves

Fifth: the loop closes. The raid hands its outcome and haul to the game instance, travels back after a beat, and the "вынесено: …" list that the HUD used to draw becomes the Shelter's.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`, `.cpp` (outcome builders)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h` (+ `PostRaidReturnSeconds`)
- Modify: `SarkoGame/Config/DefaultGame.ini` (+ the value)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp` (record + travel)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp` (summary loses the haul list)
- Modify: `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp` (2 new tests)

**Interfaces:**
- Consumes: `FSarkoLastRaid`, `USarkoGameInstance::RecordRaidOutcome`, `SarkoTravel::TravelTo`, `SarkoRaid::OutcomeLosesHaul`.
- Produces:
  - `SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome, bool bPersisted) -> FString`
  - `SarkoShelter::BuildHaulLines(const FSarkoLastRaid&, const FSarkoItemCatalog&) -> TArray<FString>`
  - `BuildView` now fills `OutcomeTitle` and `HaulLines`
  - `USarkoRaidSettings::PostRaidReturnSeconds` (default `5.f`)

- [ ] **Step 1: Write the failing outcome tests**

Append to `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShelterNamesEveryOutcome,
	"Sarko.Shelter.NamesEveryOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShelterNamesEveryOutcome::RunTest(const FString& Parameters)
{
	// Every enumerator, including InProgress — which is the "never raided" value
	// and must draw nothing rather than a blank banner.
	TestEqual(TEXT("no raid yet draws no outcome at all"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::InProgress, /*bPersisted*/ true), FString());
	TestEqual(TEXT("an extraction is named"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::Extracted, true), FString(TEXT("ВИНЕСЕНО")));
	TestEqual(TEXT("a death is named"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::Died, true), FString(TEXT("ЗАГИНУВ")));
	TestEqual(TEXT("a timeout is named as its own thing, not as a death"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::MIA, true), FString(TEXT("ЗНИК БЕЗВІСТИ")));

	// An unpersisted raid must say so. Without this the shelter shows a haul above
	// a stash that does not contain it, and the player concludes the stash is
	// broken rather than that the network was.
	TestEqual(TEXT("an unsaved extraction says it was not saved"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::Extracted, /*bPersisted*/ false),
		FString(TEXT("ВИНЕСЕНО — НЕ ЗБЕРЕЖЕНО")));
	// A lost haul was nothing to save, so the warning would be noise.
	TestEqual(TEXT("an unsaved death needs no warning"),
		SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome::Died, false), FString(TEXT("ЗАГИНУВ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHaulLinesOnlySurviveAnExtraction,
	"Sarko.Shelter.HaulLinesOnlySurviveAnExtraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHaulLinesOnlySurviveAnExtraction::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = MakeTestCatalog();

	FSarkoLastRaid Won;
	Won.Outcome = ESarkoRaidOutcome::Extracted;
	Won.bPersisted = true;
	Won.Haul = {
		FSarkoItemStack{ FName(TEXT("scrap_metal")), 4 },
		FSarkoItemStack{ FName(TEXT("medkit")), 1 },
	};

	const TArray<FString> WonLines = SarkoShelter::BuildHaulLines(Won, Catalog);
	TestEqual(TEXT("one line per carried stack"), WonLines.Num(), 2);
	TestEqual(TEXT("UA names, matching the stash list's format"), WonLines[0], FString(TEXT("Металолом  x4")));

	// Spec §4.5: MIA is death and loses the haul. ASarkoRaidGameMode::FinishRaid
	// clears the backpack before it writes the outcome, so a losing raid arrives
	// here with an empty Haul — but this must not *depend* on that, because the
	// cost of it being wrong is loot shown as banked that was actually destroyed.
	// A losing outcome refuses to list anything even when handed a full haul.
	FSarkoLastRaid Lost = Won;
	Lost.Outcome = ESarkoRaidOutcome::MIA;
	const TArray<FString> LostLines = SarkoShelter::BuildHaulLines(Lost, Catalog);
	TestEqual(TEXT("a losing outcome lists exactly one line"), LostLines.Num(), 1);
	TestEqual(TEXT("and that line is the loss"), LostLines[0], FString(TEXT("НІЧОГО НЕ ВИНЕСЕНО")));

	Lost.Outcome = ESarkoRaidOutcome::Died;
	TestEqual(TEXT("death is the same"), SarkoShelter::BuildHaulLines(Lost, Catalog)[0],
		FString(TEXT("НІЧОГО НЕ ВИНЕСЕНО")));

	// An extraction that genuinely carried nothing says the same thing.
	FSarkoLastRaid EmptyHanded;
	EmptyHanded.Outcome = ESarkoRaidOutcome::Extracted;
	EmptyHanded.bPersisted = true;
	TestEqual(TEXT("an empty extraction says nothing was carried"),
		SarkoShelter::BuildHaulLines(EmptyHanded, Catalog)[0], FString(TEXT("НІЧОГО НЕ ВИНЕСЕНО")));

	// And before any raid there is no block at all — not an empty-haul line.
	const FSarkoLastRaid NoRaidYet;
	TestEqual(TEXT("no raid yet draws no haul block"), SarkoShelter::BuildHaulLines(NoRaidYet, Catalog).Num(), 0);

	// End to end through BuildView, because that is what the widget calls.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	const FSarkoShelterView View = SarkoShelter::BuildView(Won, Profile, true, FString(), Catalog);
	TestEqual(TEXT("BuildView carries the outcome title"), View.OutcomeTitle, FString(TEXT("ВИНЕСЕНО")));
	TestEqual(TEXT("BuildView carries the haul"), View.HaulLines.Num(), 2);
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `BUILD FAILED` — `no member named 'BuildOutcomeTitle' in namespace 'SarkoShelter'`.

- [ ] **Step 3: Add the outcome builders**

In `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`, inside `namespace SarkoShelter`:

```cpp
	/**
	 * The banner for the raid that just ended, or empty before the first raid.
	 *
	 * MIA is named separately from death even though spec §4.5 makes them the
	 * same *mechanically* (both lose the haul, both submit as `died`): the player
	 * needs to know whether they were shot or ran out of clock, because those are
	 * different mistakes.
	 *
	 * An extraction that was not persisted is labelled, so a haul shown above a
	 * stash that does not contain it is explained rather than mysterious. A lost
	 * haul had nothing to persist, so it carries no such label.
	 */
	FString BuildOutcomeTitle(ESarkoRaidOutcome Outcome, bool bPersisted);

	/**
	 * The haul, one "<UA name>  x<qty>" line per stack — the list that spec §6.5
	 * moves out of the raid HUD and into the shelter.
	 *
	 * Empty (no block at all) before the first raid. Exactly one line, "НІЧОГО НЕ
	 * ВИНЕСЕНО", for every losing outcome **and** for an extraction that carried
	 * nothing. Losing outcomes are refused by outcome rather than by an empty
	 * array, so a haul that reached this struct by some future path still cannot
	 * be shown as banked.
	 */
	TArray<FString> BuildHaulLines(const FSarkoLastRaid& LastRaid, const FSarkoItemCatalog& Catalog);
```

In `SarkoShelterView.cpp`, add `#include "Core/SarkoRaidGameState.h"` (for `SarkoRaid::OutcomeLosesHaul`) and:

```cpp
FString SarkoShelter::BuildOutcomeTitle(ESarkoRaidOutcome Outcome, bool bPersisted)
{
	switch (Outcome)
	{
	case ESarkoRaidOutcome::Extracted:
		return bPersisted ? FString(TEXT("ВИНЕСЕНО")) : FString(TEXT("ВИНЕСЕНО — НЕ ЗБЕРЕЖЕНО"));
	case ESarkoRaidOutcome::Died:
		return TEXT("ЗАГИНУВ");
	case ESarkoRaidOutcome::MIA:
		return TEXT("ЗНИК БЕЗВІСТИ");
	default:
		// InProgress is the "no raid yet" value: nothing is drawn.
		return FString();
	}
}

TArray<FString> SarkoShelter::BuildHaulLines(const FSarkoLastRaid& LastRaid, const FSarkoItemCatalog& Catalog)
{
	TArray<FString> Lines;
	if (LastRaid.Outcome == ESarkoRaidOutcome::InProgress)
	{
		return Lines;
	}

	// By outcome, not by "is the array empty". SarkoRaid::OutcomeLosesHaul is the
	// same rule FinishRaid consults before it clears the backpack, so the shelter
	// and the server agree on what a losing raid keeps by construction rather than
	// by both happening to be right.
	if (SarkoRaid::OutcomeLosesHaul(LastRaid.Outcome) || LastRaid.Haul.Num() == 0)
	{
		Lines.Add(TEXT("НІЧОГО НЕ ВИНЕСЕНО"));
		return Lines;
	}

	Lines.Reserve(LastRaid.Haul.Num());
	for (const FSarkoItemStack& Stack : LastRaid.Haul)
	{
		const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
		Lines.Add(FString::Printf(TEXT("%s  x%d"),
			Def ? *Def->Name : *Stack.Item.ToString(), Stack.Quantity));
	}
	return Lines;
}
```

and in `BuildView`, replace the `(void)LastRaid;` placeholder with:

```cpp
	View.OutcomeTitle = BuildOutcomeTitle(LastRaid.Outcome, LastRaid.bPersisted);
	View.HaulLines = BuildHaulLines(LastRaid, Catalog);
```

- [ ] **Step 4: Run the shelter tests**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Shelter`
Expected: `6 test(s) performed, 0 failed`, `ALL GREEN`.

- [ ] **Step 5: Add the return-delay setting**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, after `ExtractDwellSeconds`:

```cpp
	/**
	 * How long the raid's outcome banner stays on screen before the game travels
	 * back to the shelter (spec §6.5).
	 *
	 * Not zero: the player has to see EXTRACTED/KIA/MIA where it happened, in the
	 * place they died or extracted from, before the screen changes. Not long
	 * either — the itemised haul is in the shelter now, so there is nothing to
	 * read here.
	 *
	 * The raid result is submitted independently of this timer, and both the game
	 * instance and the submission lambda hold the backend client, so travelling
	 * mid-request cannot lose the result — only the log line about it moves worlds.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Raid")
	float PostRaidReturnSeconds = 5.f;
```

In `SarkoGame/Config/DefaultGame.ini`, under `[/Script/SarkoGame.SarkoRaidSettings]`, next to `ExtractDwellSeconds`:

```ini
PostRaidReturnSeconds=5.000000
```

`USarkoRaidSettings` is `config = Game`, so this belongs in **DefaultGame.ini** — putting it in DefaultEngine.ini silently loads the 5.0 C++ default and nothing warns.

- [ ] **Step 6: Record the outcome and travel back**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, add one private method:

```cpp
	/**
	 * Hands the outcome and haul to the game instance and schedules the trip back
	 * to the shelter. Called once, from the tail of FinishRaid.
	 *
	 * The travel is on a timer rather than immediate so the outcome banner is
	 * visible where the raid ended, and the timer is a weak-lambda delegate
	 * because it is scheduled on a game mode that a travel is about to destroy.
	 */
	void ReturnToShelter(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul);
```

In `SarkoRaidGameMode.cpp`, add `#include "Core/SarkoTravel.h"` and `#include "TimerManager.h"`, then restructure the tail of `FinishRaid`. Everything up to and including `bSessionSubmitted = true;` and the `Haul` assembly stays **exactly** as it is. Replace only the offline early-return and the `SubmitResult` call with:

```cpp
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
		SarkoTravel::TravelTo(this, SarkoTravel::ShelterOptions());
	}), Delay, /*bLoop*/ false);

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: returning to the shelter in %.1fs"), Delay);
}
```

`FinishRaid` must call `ReturnToShelter` on **every** path that writes an outcome, including the `bSessionSubmitted` early return — a second `FinishRaid` cannot happen (`CanFinishRaid` refuses it), so the only reachable paths are the three above. Verify by reading the function top to bottom after the edit: every `return` after `RaidState->Outcome = NewOutcome;` must be preceded by a `ReturnToShelter` call, or a player who ends a raid is stranded on the banner forever.

- [ ] **Step 7: Strip the haul list out of the HUD**

In `SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp`, replace the body of `DrawOutcomeSummary` below the title with a single "returning" line. Keep the dim-the-world rect and the title switch exactly as they are; delete the backpack lookup, the catalog lookup and the per-stack loop:

```cpp
	// The itemised haul used to be drawn here and now lives in the shelter (spec
	// §6.5: "вынесено: …" moves there). What stays is the outcome itself, in the
	// place the raid ended, plus a line saying where the player is about to go —
	// without it, five seconds of a dimmed frozen world reads as a hang.
	const FString Returning = TEXT("ПОВЕРНЕННЯ ДО УКРИТТЯ...");
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Returning, Width, Height, GEngine->GetLargeFont(), 1.f);
	DrawText(Returning, FLinearColor(0.8f, 0.8f, 0.8f), (Canvas->SizeX - Width) * 0.5f, Y,
		GEngine->GetLargeFont(), 1.f);
```

Then delete the now-unused `#include "Loot/SarkoItemCatalog.h"` from `SarkoHUD.cpp` **only if** nothing else in the file uses it — check with `grep -n "SarkoLoot::\|FSarkoItemDef\|GetItemCatalog" Source/SarkoGame/UI/SarkoHUD.cpp` and leave the include alone if there is a hit. Nothing in `SarkoHUD.h` changes.

- [ ] **Step 8: Run everything and verify the loop by hand**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `65 test(s) performed, 0 failed`.

Then the round trip, headlessly, using `BugItGo` to stand on extraction zone 0 (`-14500 18600` is its centre in `Data/Maps/bridge.json`, and the nearest bot spawn is over 4000 uu away):

```bash
cd SarkoGame
echo "a5-t5-$(date +%s)" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -14500 18600 200" 2>/dev/null &
sleep 45 && kill %1 2>/dev/null
grep -E "extracted at zone|raid finished as|result 'extracted' submitted|returning to the shelter|SarkoTravel: travelling|profile for " \
  ~/Library/Logs/SarkoGame/SarkoGame.log
```

Expected, in this order: `extracted at zone 0`, `raid finished as ESarkoRaidOutcome::Extracted`, `returning to the shelter in 5.0s`, `result 'extracted' submitted`, `SarkoTravel: travelling to /Engine/Maps/Entry with options '(none — the shelter)'`, and finally a second `profile for <uuid>` line — the Shelter re-fetching. That last line is the proof the loop closed.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): the raid returns to the Shelter, and the haul summary moves there"
```

---

### Task 6: `fixedItems` — the tutorial's static loot

Sixth. The map schema gains an optional per-container `fixedItems` list; the server-side roll path honours it while the profile says the tutorial is not done. **No layout is authored here** — `Data/Maps/bridge.json` is not edited, because §6.5's teaching route (spawn → pipes → gas station → rail depot → E1) is Bridge_West geometry that Stage C creates. This task builds the mechanism and pins it with tests over string literals.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, `.cpp` (`FixedItems`)
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`, `.cpp` (`RollContainerFor`)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp` (call the new roll)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp` (profile hop, `bTutorialLoot`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h` + `Config/DefaultGame.ini` (`bOfflineTutorialLoot`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (4 new tests)

**Interfaces:**
- Consumes: `FSarkoLootContainerSpot`, `FSarkoLootTable`, `FRandomStream`, `SarkoLoot::GetItemCatalog()`, `FSarkoProfile::bTutorialCompleted`, `FSarkoBackendClient::FetchProfile`.
- Produces:
  - `FSarkoLootContainerSpot::FixedItems` (`TArray<FSarkoItemStack>`, empty when absent)
  - `TArray<FSarkoItemStack> SarkoLoot::RollContainerFor(const FSarkoLootContainerSpot&, const FSarkoLootTable&, FRandomStream&, bool bTutorialLoot)`
  - `ASarkoRaidGameMode::bTutorialLoot` (server-only plain member, like `LootSalt`)
  - `USarkoRaidSettings::bOfflineTutorialLoot` (default `true`)

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (it already includes `Loot/SarkoLootTable.h`; add `#include "Map/SarkoMapDefinition.h"` if it is not there):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoContainersMayCarryFixedItems,
	"Sarko.Loot.ContainersMayCarryFixedItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoContainersMayCarryFixedItems::RunTest(const FString& Parameters)
{
	// The tutorial's static layout (spec §6.5) is authored in the map file, one
	// optional list per container. Stage C writes the real one; this pins the
	// schema so it cannot drift under it.
	const FString Json = TEXT(R"({
		"id": "t",
		"extentUU": 1000,
		"raidDurationSeconds": 600,
		"playerSpawns": [{ "pos": [0, 0, 100] }],
		"containers": [
			{ "pos": [100, 0, 0], "tier": "junk",
			  "fixedItems": [{ "item": "scrap_metal", "qty": 3 }, { "item": "duct_tape", "qty": 1 }] },
			{ "pos": [200, 0, 0], "tier": "common" }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("fixedItems parses"), SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("both containers are read"), Definition.Containers.Num(), 2);

	TestEqual(TEXT("the authored list is read in order"), Definition.Containers[0].FixedItems.Num(), 2);
	TestEqual(TEXT("the first fixed item's id survives"),
		Definition.Containers[0].FixedItems[0].Item, FName(TEXT("scrap_metal")));
	TestEqual(TEXT("the first fixed item's quantity survives"),
		Definition.Containers[0].FixedItems[0].Quantity, 3);

	// Absent is the normal case and means "roll this one" even in tutorial mode.
	TestEqual(TEXT("a container with no fixedItems has an empty list"),
		Definition.Containers[1].FixedItems.Num(), 0);

	// Every failure mode is a named error, never a silently shortened list: a
	// dropped entry is a teaching beat that quietly stops happening, and the
	// symptom is "the tutorial feels thin", which nobody can trace to a data file.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not an array"),        TEXT(R"("fixedItems": 7)") },
		{ TEXT("entry not an object"), TEXT(R"("fixedItems": ["scrap_metal"])") },
		{ TEXT("no item id"),          TEXT(R"("fixedItems": [{ "qty": 2 }])") },
		{ TEXT("empty item id"),       TEXT(R"("fixedItems": [{ "item": "", "qty": 2 }])") },
		{ TEXT("qty zero"),            TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": 0 }])") },
		{ TEXT("qty negative"),        TEXT(R"("fixedItems": [{ "item": "scrap_metal", "qty": -1 }])") },
		// An id the catalog does not know would be rejected by the backend's
		// domain.ValidateRaidItems at result time — fifteen minutes into a raid,
		// having already been shown to the player. Caught at load instead.
		{ TEXT("unknown item id"),     TEXT(R"("fixedItems": [{ "item": "unobtanium", "qty": 1 }])") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		const FString Bad = FString::Printf(TEXT(R"({
			"id": "t", "extentUU": 1000, "raidDurationSeconds": 600,
			"playerSpawns": [{ "pos": [0, 0, 100] }],
			"containers": [{ "pos": [100, 0, 0], "tier": "junk", %s }]
		})"), *Case.Value);

		FSarkoMapDefinition Rejected;
		FString BadError;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Bad, Rejected, BadError));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), BadError.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialLootIgnoresTheRandomStream,
	"Sarko.Loot.TutorialLootIgnoresTheRandomStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialLootIgnoresTheRandomStream::RunTest(const FString& Parameters)
{
	FSarkoLootContainerSpot Spot;
	Spot.Tier = SarkoLoot::TierJunk;
	Spot.FixedItems = {
		FSarkoItemStack{ FName(TEXT("scrap_metal")), 3 },
		FSarkoItemStack{ FName(TEXT("duct_tape")), 1 },
	};

	// A real table, so the test proves the fixed list *wins* rather than that
	// there was nothing to roll.
	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierJunk);
	if (!Table)
	{
		AddError(TEXT("the junk tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	// Two wildly different streams must produce byte-identical results: "static"
	// means the seed cannot reach it at all, which is what makes dying and
	// replaying the tutorial show the same layout (spec §6.5).
	FRandomStream StreamA(1);
	FRandomStream StreamB(0x7FFFFFFF);
	const TArray<FSarkoItemStack> FromA = SarkoLoot::RollContainerFor(Spot, *Table, StreamA, /*bTutorialLoot*/ true);
	const TArray<FSarkoItemStack> FromB = SarkoLoot::RollContainerFor(Spot, *Table, StreamB, /*bTutorialLoot*/ true);

	TestEqual(TEXT("the fixed list is returned whole"), FromA.Num(), 2);
	TestEqual(TEXT("two different streams give the same static loot"), FromA.Num(), FromB.Num());
	for (int32 Index = 0; Index < FromA.Num(); ++Index)
	{
		TestEqual(TEXT("same item at the same position"), FromA[Index].Item, FromB[Index].Item);
		TestEqual(TEXT("same quantity"), FromA[Index].Quantity, FromB[Index].Quantity);
	}
	TestEqual(TEXT("the authored order is preserved"), FromA[0].Item, FName(TEXT("scrap_metal")));
	TestEqual(TEXT("the authored quantity is preserved"), FromA[0].Quantity, 3);

	// Fixed lists must clear the backend's plausibility gate, which is the same
	// gate a rolled haul clears (spec §6.5: "fixed lists pass the same
	// plausibility gate"). The client-side half of that is the 12-slot backpack.
	TestTrue(TEXT("a fixed list fits a backpack"), FromA.Num() <= 12);
	for (const FSarkoItemStack& Stack : FromA)
	{
		TestNotNull(TEXT("every fixed item is in the catalog"),
			SarkoLoot::GetItemCatalog().Find(Stack.Item));
		TestTrue(TEXT("every fixed quantity is positive"), Stack.Quantity > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialModeFallsBackToRollingWhenNothingIsAuthored,
	"Sarko.Loot.TutorialModeFallsBackToRollingWhenNothingIsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialModeFallsBackToRollingWhenNothingIsAuthored::RunTest(const FString& Parameters)
{
	// The mechanism ships in Stage A.5; the authored layout is Stage C's. Between
	// the two, `bridge.json` carries no fixedItems at all, and a tutorial raid that
	// yielded nothing from all 42 containers would be a real regression in a build
	// that is meant to stay playable. So tutorial mode with nothing authored rolls
	// normally — and the game mode logs one Warning per raid naming the count, so
	// the gap is visible rather than assumed. Stage C's acceptance bar is that the
	// Warning stops appearing.
	FSarkoLootContainerSpot Unauthored;
	Unauthored.Tier = SarkoLoot::TierJunk;

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierJunk);
	if (!Table)
	{
		AddError(TEXT("the junk tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	FRandomStream Tutorial(4242);
	FRandomStream Normal(4242);
	const TArray<FSarkoItemStack> InTutorial =
		SarkoLoot::RollContainerFor(Unauthored, *Table, Tutorial, /*bTutorialLoot*/ true);
	const TArray<FSarkoItemStack> Rolled =
		SarkoLoot::RollContainerFor(Unauthored, *Table, Normal, /*bTutorialLoot*/ false);

	TestEqual(TEXT("an unauthored container rolls identically in either mode"), InTutorial.Num(), Rolled.Num());
	for (int32 Index = 0; Index < InTutorial.Num(); ++Index)
	{
		TestEqual(TEXT("same item"), InTutorial[Index].Item, Rolled[Index].Item);
		TestEqual(TEXT("same quantity"), InTutorial[Index].Quantity, Rolled[Index].Quantity);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNormalModeIgnoresAuthoredFixedItems,
	"Sarko.Loot.NormalModeIgnoresAuthoredFixedItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNormalModeIgnoresAuthoredFixedItems::RunTest(const FString& Parameters)
{
	// The other half of the branch, and the half that protects the economy: once
	// tutorial_completed is set, an authored fixedItems list must be dead data.
	// Otherwise the teaching layout — which contains a guaranteed military crate —
	// becomes a farmable route forever.
	FSarkoLootContainerSpot Spot;
	Spot.Tier = SarkoLoot::TierMilitary;
	Spot.FixedItems = { FSarkoItemStack{ FName(TEXT("bike_frame")), 1 } };

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(SarkoLoot::TierMilitary);
	if (!Table)
	{
		AddError(TEXT("the military tier has no loot table, so this test cannot mean anything"));
		return false;
	}

	// Ten different streams, because a single roll could coincidentally match the
	// fixed list and pass a weaker version of this test.
	int32 MatchedTheFixedList = 0;
	for (int32 Seed = 1; Seed <= 10; ++Seed)
	{
		FRandomStream Stream(Seed * 7919);
		const TArray<FSarkoItemStack> Out = SarkoLoot::RollContainerFor(Spot, *Table, Stream, /*bTutorialLoot*/ false);
		FRandomStream Same(Seed * 7919);
		const TArray<FSarkoItemStack> Reference = SarkoLoot::RollContainer(*Table, Same);

		TestEqual(TEXT("normal mode is exactly RollContainer"), Out.Num(), Reference.Num());
		for (int32 Index = 0; Index < Out.Num(); ++Index)
		{
			TestEqual(TEXT("same item as an unbranched roll"), Out[Index].Item, Reference[Index].Item);
			TestEqual(TEXT("same quantity as an unbranched roll"), Out[Index].Quantity, Reference[Index].Quantity);
		}
		if (Out.Num() == 1 && Out[0].Item == FName(TEXT("bike_frame")) && Out[0].Quantity == 1)
		{
			++MatchedTheFixedList;
		}
	}
	TestTrue(TEXT("at least one of ten rolls differs from the fixed list, so the branch is really off"),
		MatchedTheFixedList < 10);
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `BUILD FAILED` — `no member named 'FixedItems' in 'FSarkoLootContainerSpot'` and `no member named 'RollContainerFor' in namespace 'SarkoLoot'`.

- [ ] **Step 3: Add `fixedItems` to the schema**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, extend `FSarkoLootContainerSpot` (it must `#include "Loot/SarkoItemCatalog.h"` for `FSarkoItemStack` — add it next to the existing includes):

```cpp
/** Where a lootable container sits, and how good its contents are. */
USTRUCT()
struct FSarkoLootContainerSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Tier;

	/**
	 * Exact contents instead of a roll, for the one-time tutorial raid (spec
	 * §6.5). Empty for every normal container, which is all of them today —
	 * Stage C authors the teaching layout against Bridge_West's geometry.
	 *
	 * Only consulted while the player's profile says `tutorial_completed` is
	 * false, so once the tutorial is over this is dead data rather than a
	 * guaranteed drop a player could farm forever.
	 *
	 * Validated against the item catalog at parse time: an id the catalog does
	 * not know would be refused by the backend's domain.ValidateRaidItems at
	 * result time, fifteen minutes into a raid, after the player has already been
	 * shown the item.
	 */
	UPROPERTY()
	TArray<FSarkoItemStack> FixedItems;
};
```

In `SarkoMapDefinition.cpp`, add `#include "Loot/SarkoItemCatalog.h"` and, inside the `containers` loop immediately after the `tier` read, before `OutDefinition.Containers.Add(Spot)`:

```cpp
			// fixedItems — optional, and every failure is named. A silently
			// shortened list is a teaching beat that quietly stops happening.
			const TArray<TSharedPtr<FJsonValue>>* FixedItems = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("fixedItems"), FixedItems, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			if (FixedItems)
			{
				for (int32 ItemIndex = 0; ItemIndex < FixedItems->Num(); ++ItemIndex)
				{
					const TSharedPtr<FJsonObject>* ItemObject = nullptr;
					if (!(*FixedItems)[ItemIndex]->TryGetObject(ItemObject) || !ItemObject)
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: not an object"),
							Index, ItemIndex);
						return false;
					}
					FString ItemId;
					if (!(*ItemObject)->TryGetStringField(TEXT("item"), ItemId) || ItemId.IsEmpty())
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: 'item' is missing or empty"),
							Index, ItemIndex);
						return false;
					}
					double Quantity = 0.0;
					if (!(*ItemObject)->TryGetNumberField(TEXT("qty"), Quantity) || Quantity < 1.0)
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d] ('%s'): 'qty' is missing or less than 1"),
							Index, ItemIndex, *ItemId);
						return false;
					}
					// Checked here, at load, and not at loot time: the backend's
					// plausibility gate rejects an unknown id and would reject the
					// whole haul at the end of the raid instead.
					if (SarkoLoot::GetItemCatalog().Find(FName(*ItemId)) == nullptr)
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d]: '%s' is not in Data/Items/items.json"),
							Index, ItemIndex, *ItemId);
						return false;
					}
					Spot.FixedItems.Add(FSarkoItemStack{ FName(*ItemId), static_cast<int32>(Quantity) });
				}
			}
```

Note `TryGetOptionalArrayField` takes a `TSharedPtr<FJsonObject>` and the loop variable here is a `const TSharedPtr<FJsonObject>*` — hence `*Object`. That matches how the neighbouring `ReadOptionalString` calls in the same loop are written.

- [ ] **Step 4: Add the roll branch**

In `SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`, forward-declare the spot at **global scope** (never inside the namespace) and declare the function:

```cpp
// Forward-declared at global scope on purpose. `const struct
// FSarkoLootContainerSpot&` written inside `namespace SarkoLoot` declares a
// second, permanently-incomplete SarkoLoot::FSarkoLootContainerSpot that shadows
// the real one — that exact bug already happened once in SarkoMapBuilder.h.
struct FSarkoLootContainerSpot;

namespace SarkoLoot
{
	// ... existing declarations ...

	/**
	 * One container's contents, honouring the tutorial's static loot.
	 *
	 * The single entry point the loot channel calls, so the branch exists in
	 * exactly one place rather than at the call site:
	 *  - tutorial mode **and** an authored fixedItems list → that list, verbatim,
	 *    with the stream untouched. Static means the seed cannot reach it, which
	 *    is what makes dying and replaying the tutorial show the same layout
	 *    (spec §6.5);
	 *  - anything else → RollContainer, unchanged.
	 *
	 * Tutorial mode with **no** authored list deliberately rolls rather than
	 * yielding nothing: the mechanism ships in Stage A.5 and the layout in Stage
	 * C, and a first raid where all 42 containers are empty would be a real
	 * regression in between. ASarkoRaidGameMode logs one Warning per tutorial raid
	 * naming how many containers carry a list, so the gap is visible; Stage C's
	 * acceptance bar is that the Warning stops appearing.
	 */
	TArray<FSarkoItemStack> RollContainerFor(const FSarkoLootContainerSpot& Spot, const FSarkoLootTable& Table,
		FRandomStream& Stream, bool bTutorialLoot);
}
```

In `SarkoLootTable.cpp`, add `#include "Map/SarkoMapDefinition.h"` and:

```cpp
TArray<FSarkoItemStack> SarkoLoot::RollContainerFor(const FSarkoLootContainerSpot& Spot,
	const FSarkoLootTable& Table, FRandomStream& Stream, bool bTutorialLoot)
{
	if (bTutorialLoot && Spot.FixedItems.Num() > 0)
	{
		// A copy, and the stream is not advanced at all. Advancing it "for
		// consistency" would make the tutorial's own contents depend on which
		// containers the player opened first, which is the opposite of static.
		return Spot.FixedItems;
	}
	return RollContainer(Table, Stream);
}
```

- [ ] **Step 5: Route the loot channel through it**

In `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp`, in `TickLootChannel`, replace the single `RollContainer` line:

```cpp
	// GameMode->LootSalt is the reason a client cannot precompute this: Seed is
	// replicated and the tables ship in the build, so without the salt the two
	// remaining inputs are both already in the client's hands.
	FRandomStream Stream(SarkoLoot::ContainerSeed(RaidState->Seed, Index, GameMode->LootSalt));
	// RollContainerFor, not RollContainer: the tutorial's static loot (spec §6.5)
	// is a per-container branch, and bTutorialLoot is a server-only member of the
	// game mode — like LootSalt, it is never replicated, so a client cannot learn
	// which mode the raid is in and therefore cannot read the authored layout out
	// of its own copy of the map file plus the tables.
	const TArray<FSarkoItemStack> Rolled =
		SarkoLoot::RollContainerFor(Spots[Index], *Table, Stream, GameMode->bTutorialLoot);
```

Everything after it — `CompleteLootChannel`, the payout log, the `bCredited` warning — is unchanged.

- [ ] **Step 6: Add the profile hop and the mode flag**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, next to `LootSalt`:

```cpp
	/**
	 * Whether this raid uses the tutorial's static loot (spec §6.5).
	 *
	 * Set from GET /v1/profile's `tutorial_completed` before the raid goes live,
	 * or from USarkoRaidSettings::bOfflineTutorialLoot when there is no profile.
	 *
	 * A deliberately plain member and **not** a UPROPERTY, for exactly the reasons
	 * LootSalt is one: a game mode exists only on the server, so there is no
	 * replication path to forget to exclude, and a non-UPROPERTY also stays out of
	 * anything that walks reflected properties. It matters here because a client
	 * that knew the raid was in tutorial mode could read the authored layout
	 * straight out of its own copy of the map file — fixedItems is shipped data,
	 * unlike a roll, which needs the server-only salt.
	 */
	bool bTutorialLoot = false;
```

In `SarkoRaidSettings.h`, next to `bBackendEnabled`:

```cpp
	/**
	 * Which loot mode an offline raid uses, when there is no profile to ask.
	 *
	 * True (the default) means an offline raid replays the tutorial's static
	 * layout: nothing persists offline, so replaying it costs nothing and gives a
	 * deterministic raid to iterate against. Set it false to exercise the seeded
	 * roll path with the backend off.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	bool bOfflineTutorialLoot = true;
```

In `Config/DefaultGame.ini`, under `[/Script/SarkoGame.SarkoRaidSettings]`:

```ini
bOfflineTutorialLoot=True
```

In `SarkoRaidGameMode.cpp`, `OnAuthenticated` (created in Task 3) gains the profile hop ahead of `StartRaid`. Wrap the existing `StartRaid` → `ConfirmRaid` → `ActivateRaid` chain — verbatim, comments and all — in a new private `void ASarkoRaidGameMode::BeginRaidSession()`, declared in the header alongside `OnAuthenticated`, and make `OnAuthenticated` this:

```cpp
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
```

Add the setter, which is where the Warning lives:

```cpp
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
```

declared in the header as:

```cpp
	/** Sets bTutorialLoot and logs what the map can actually deliver in that mode. */
	void SetTutorialLoot(bool bEnabled);

	/** /v1/raid/start -> /v1/raid/confirm -> ActivateRaid. Split from OnAuthenticated
	 *  so the profile hop sits between auth and the session opening. */
	void BeginRaidSession();
```

Finally, in `FallBackToOfflineRaid`, set the mode before activating — insert immediately above the existing `ActivateRaid(Seed, MapClockSeconds());`:

```cpp
	// No profile, so no flag. Settings decide, defaulting to the tutorial's static
	// layout: an offline raid persists nothing, so replaying the tutorial costs the
	// player nothing and gives a deterministic raid to iterate against.
	SetTutorialLoot(GetDefault<USarkoRaidSettings>()->bOfflineTutorialLoot);
```

- [ ] **Step 7: Run everything**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `14 test(s) performed, 0 failed` (10 before + 4).

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `69 test(s) performed, 0 failed`.

The map file must still load — a new required-shape check in the container loop is exactly the kind of edit that rejects the real file:

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `8 test(s) performed, 0 failed` — `Sarko.Map.BridgeMapIsValid` in particular proves `Data/Maps/bridge.json` still parses with no `fixedItems` anywhere in it.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): optional fixedItems per container, honoured while the tutorial is pending"
```

---

### Task 7: The two open notes — dwell from zone entry, and `Seed == 0`

Seventh. Both are carried in `.superpowers/sdd/progress.md`'s OPEN NOTES and both belong to this stage: the dwell one was found during the live e2e, and the seed one is a comment describing a bug rather than a fix.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoExtractionZone.h`, `.cpp` (`FSarkoDwell`, `AdvanceDwellInZone`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp` (the dwell map, the activation epoch)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (`OnRep_SessionReady`, `ShouldSpawnClientLayout`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp` (4 new tests)

**Interfaces:**
- Consumes: `SarkoExtract::AdvanceDwell` (kept, and now the primitive the new function is built on), `SarkoExtract::FindZoneContaining`, `ASarkoCharacter::SetExtractProgress`.
- Produces:
  - `SarkoExtract::FSarkoDwell { int32 ZoneIndex; float Seconds; }`
  - `SarkoExtract::FSarkoDwell SarkoExtract::AdvanceDwellInZone(const FSarkoDwell&, int32 ZoneIndex, float DeltaSeconds)`
  - `SarkoRaid::ShouldSpawnClientLayout(bool bLayoutBuilt, bool bSessionReady) -> bool`
  - `ASarkoRaidGameState::OnRep_SessionReady()`

**What is actually wrong, and what is deliberate.** The live log (`.superpowers/sdd/task-9-report.md` §4.1) shows the pawn on the pad at `23:40:45.785`, the raid live at `46.407`, and the extraction at `51.253` — five seconds billed from the activation frame. The e2e report calls that correct; the owner calls it a bug. Both are half right, so this task keeps the deliberate half and fixes the accidental half:

- **Kept:** no dwell accrues before the raid is live. `Tick`'s `IsLootable()` guard is load-bearing — a dwell completing during the `auth → profile → start → confirm` round trip would call `FinishRaid` before there is a session to submit the result to, which is the one path that silently throws a haul away. The clock is not running during that window either, so the round trip costs the player no raid time.
- **Fixed:** the dwell carries no identity and has no epoch. `AdvanceDwell` takes only `bInsideZone`, so a pawn crossing from one zone straight into a second — two overlapping circles, or a zone list edited so two touch — **kept its accumulated seconds** and could extract from a zone it had stood in for one frame. And nothing reset the map at activation, so "the dwell starts when the raid goes live" was an accident of the guard rather than a rule. After this task the rule is written down and tested: **five seconds from entering *this* zone, with the activation frame counting as an entry.**

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellIsMeasuredFromEnteringThisZone,
	"Sarko.Extract.DwellIsMeasuredFromEnteringThisZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellIsMeasuredFromEnteringThisZone::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// Entering starts the clock at this frame's delta, not at zero-plus-a-frame
	// later: the frame the pawn is first seen inside is a frame it spent inside.
	FSarkoDwell Dwell = SarkoExtract::AdvanceDwellInZone(FSarkoDwell(), /*ZoneIndex*/ 0, 0.1f);
	TestEqual(TEXT("entering records which zone"), Dwell.ZoneIndex, 0);
	TestTrue(TEXT("entering starts at one frame"), FMath::IsNearlyEqual(Dwell.Seconds, 0.1f, 0.001f));

	for (int32 Frame = 0; Frame < 9; ++Frame)
	{
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, 0, 0.1f);
	}
	TestTrue(TEXT("a second in one zone accrues about a second"),
		FMath::IsNearlyEqual(Dwell.Seconds, 1.f, 0.001f));

	// THE BUG. Crossing straight from zone 0 into zone 1 without passing through
	// open ground must NOT carry zone 0's seconds across. Before this, a pawn that
	// had stood 4.9 s in one zone extracted from a different one on its first frame
	// inside — nine tenths of the dwell paid somewhere else entirely.
	const FSarkoDwell Crossed = SarkoExtract::AdvanceDwellInZone(Dwell, /*ZoneIndex*/ 1, 0.1f);
	TestEqual(TEXT("crossing into a different zone re-keys the dwell"), Crossed.ZoneIndex, 1);
	TestTrue(TEXT("crossing into a different zone restarts the count"),
		FMath::IsNearlyEqual(Crossed.Seconds, 0.1f, 0.001f));

	// Leaving resets to zero and forgets the zone, so re-entering the same one
	// starts over rather than resuming (spec §4.5: leaving resets it, and a pause
	// would let a player stitch five seconds out of safe fragments).
	const FSarkoDwell Left = SarkoExtract::AdvanceDwellInZone(Dwell, INDEX_NONE, 0.1f);
	TestEqual(TEXT("leaving forgets the zone"), Left.ZoneIndex, INDEX_NONE);
	TestEqual(TEXT("leaving resets to zero"), Left.Seconds, 0.f);

	const FSarkoDwell Reentered = SarkoExtract::AdvanceDwellInZone(Left, 0, 0.5f);
	TestTrue(TEXT("re-entering starts from zero"), FMath::IsNearlyEqual(Reentered.Seconds, 0.5f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellClampsHitchesAndIgnoresDeadFrames,
	"Sarko.Extract.DwellClampsHitchesAndIgnoresDeadFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellClampsHitchesAndIgnoresDeadFrames::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// A loading stall or a breakpoint produces one enormous delta, and without a
	// clamp that single frame completes most of an extraction the player never
	// stood through. The clamp has to survive being moved behind the new entry
	// point, including on the *entry* frame — a hitch on the frame the pawn
	// arrives is the easiest way to lose it.
	const FSarkoDwell AfterHitch = SarkoExtract::AdvanceDwellInZone(FSarkoDwell(), 0, 3.f);
	TestTrue(TEXT("a huge delta is clamped even on the entry frame"),
		AfterHitch.Seconds <= SarkoExtract::MaxDwellStepSeconds + KINDA_SMALL_NUMBER);

	FSarkoDwell Standing;
	Standing.ZoneIndex = 0;
	Standing.Seconds = 2.f;
	TestEqual(TEXT("a zero delta changes nothing"),
		SarkoExtract::AdvanceDwellInZone(Standing, 0, 0.f).Seconds, 2.f);
	TestEqual(TEXT("a negative delta changes nothing"),
		SarkoExtract::AdvanceDwellInZone(Standing, 0, -1.f).Seconds, 2.f);
	// ...but a zero delta on a *different* zone still re-keys, because the pawn is
	// somewhere else and its old progress is not transferable at any delta.
	const FSarkoDwell ZeroDeltaCross = SarkoExtract::AdvanceDwellInZone(Standing, 1, 0.f);
	TestEqual(TEXT("a zero delta still re-keys across zones"), ZeroDeltaCross.ZoneIndex, 1);
	TestEqual(TEXT("and drops the old zone's progress"), ZeroDeltaCross.Seconds, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoActivationIsTheDwellEpoch,
	"Sarko.Extract.ActivationIsTheDwellEpoch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoActivationIsTheDwellEpoch::RunTest(const FString& Parameters)
{
	using SarkoExtract::FSarkoDwell;

	// The rule the live e2e exposed, now written down: a pawn standing in a zone
	// before the raid went live owes the FULL dwell from the activation frame, not
	// a fraction of it and not zero. ASarkoRaidGameMode::ActivateRaid clears its
	// dwell map, so the next tick is an entry frame — which is what this default
	// state models. The alternative reading of "measured from entering the zone"
	// would hand an instant extraction to anyone who spawned on a pad.
	const FSarkoDwell AtActivation;
	TestEqual(TEXT("a cleared dwell knows no zone"), AtActivation.ZoneIndex, INDEX_NONE);
	TestEqual(TEXT("a cleared dwell has no seconds"), AtActivation.Seconds, 0.f);

	// Ten frames of 0.5 s reaches the 5 s dwell and not a frame sooner.
	FSarkoDwell Dwell = AtActivation;
	constexpr float Required = 5.f;
	int32 Frames = 0;
	while (Dwell.Seconds < Required && Frames < 100)
	{
		Dwell = SarkoExtract::AdvanceDwellInZone(Dwell, 0, 0.5f);
		++Frames;
	}
	TestEqual(TEXT("the full dwell is owed from the activation frame"), Frames, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoClientLayoutTriggersOnSessionReadyNotOnSeed,
	"Sarko.Extract.ClientLayoutTriggersOnSessionReadyNotOnSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoClientLayoutTriggersOnSessionReadyNotOnSeed::RunTest(const FString& Parameters)
{
	// The other open note. A client spawned its geometry from OnRep_Seed, and
	// replication sends no change when a property equals its default — so an
	// authoritative seed of exactly 0 (one in 2^32 of the backend's uint32, and
	// the *default* of the ?Seed= path) never fired the notify and left a joining
	// client standing in an empty world with no error anywhere.
	//
	// bSessionReady is the honest trigger: it is false until the raid is live and
	// its flip false->true always replicates. The rule is pure so the seed value is
	// provably irrelevant to it.
	TestFalse(TEXT("nothing spawns before the session is ready"),
		SarkoRaid::ShouldSpawnClientLayout(/*bLayoutBuilt*/ false, /*bSessionReady*/ false));
	TestTrue(TEXT("a ready session spawns the layout"),
		SarkoRaid::ShouldSpawnClientLayout(false, true));
	TestFalse(TEXT("an already-built layout is never rebuilt"),
		SarkoRaid::ShouldSpawnClientLayout(/*bLayoutBuilt*/ true, true));
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Extract`
Expected: `BUILD FAILED` — `no type named 'FSarkoDwell' in namespace 'SarkoExtract'` and `no member named 'ShouldSpawnClientLayout' in namespace 'SarkoRaid'`.

- [ ] **Step 3: Add the dwell state and its rule**

In `SarkoGame/Source/SarkoGame/Loot/SarkoExtractionZone.h`, inside `namespace SarkoExtract`, after `AdvanceDwell` (which stays — it is the primitive, and its six existing assertions in `Sarko.Extract.DwellAccumulatesAndResets` keep passing):

```cpp
	/**
	 * One pawn's extraction progress, and **which zone it belongs to**.
	 *
	 * The zone index is the fix for a real bug: AdvanceDwell takes only a boolean,
	 * so a pawn crossing straight from one zone into another kept its accumulated
	 * seconds and could extract from a zone it had stood in for a single frame.
	 * Carrying the identity makes "five seconds in *this* zone" a rule instead of
	 * an accident of the geometry not overlapping.
	 *
	 * A plain struct, not a USTRUCT: it lives in a TMap on the game mode (server
	 * only, never replicated — a client must not learn that somebody is extracting)
	 * and in these pure functions, and nothing reflects over it.
	 */
	struct FSarkoDwell
	{
		int32 ZoneIndex = INDEX_NONE;
		float Seconds = 0.f;
	};

	/**
	 * Advances the dwell for a pawn now standing in ZoneIndex (INDEX_NONE when
	 * outside every zone). Pure.
	 *
	 * Three rules, all tested:
	 *  - outside → forget everything. Leaving RESETS, it does not pause (spec
	 *    §4.5); pausing would let a player stitch five seconds together out of
	 *    safe fragments, and the dwell exists to make the last five seconds of a
	 *    raid dangerous;
	 *  - a different zone than last frame → this is an entry frame: the count
	 *    restarts, keyed to the new zone. Applies at any delta, including zero;
	 *  - the same zone → accumulate, with the same per-frame clamp AdvanceDwell
	 *    applies, so a hitch cannot complete an extraction nobody stood through.
	 *
	 * ASarkoRaidGameMode::ActivateRaid clears its whole dwell map, which makes the
	 * frame the raid goes live an entry frame for every pawn — so a pawn parked on
	 * a pad since spawn owes the FULL dwell from activation. That is deliberate:
	 * no dwell may accrue before there is a session to submit the result to, and
	 * the alternative reading would hand an instant extraction to anyone who
	 * spawned on a pad.
	 */
	FSarkoDwell AdvanceDwellInZone(const FSarkoDwell& Current, int32 ZoneIndex, float DeltaSeconds);
```

In `SarkoExtractionZone.cpp`, after `AdvanceDwell`:

```cpp
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
```

- [ ] **Step 4: Use it, and make activation the epoch**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, add `#include "Loot/SarkoExtractionZone.h"` (the header now needs the complete `FSarkoDwell` for the map's value type) and replace the `DwellSeconds` member:

```cpp
	/**
	 * Server-side per-pawn extraction progress, keyed weakly so a destroyed pawn's
	 * entry cannot keep it alive; stale entries are pruned when their key goes
	 * stale rather than left to accumulate.
	 *
	 * The value carries the zone it belongs to, so progress cannot leak across a
	 * zone boundary. Cleared wholesale in ActivateRaid, which makes the frame the
	 * raid goes live an entry frame for every pawn.
	 */
	TMap<TWeakObjectPtr<class ASarkoCharacter>, SarkoExtract::FSarkoDwell> Dwells;
```

In `SarkoRaidGameMode.cpp`, in `ActivateRaid`, immediately after `RaidState->StartRaidClock(ClockSeconds);` and **before** `RaidState->bSessionReady = true;`:

```cpp
	// Activation is the dwell's epoch. Cleared here rather than relied upon to be
	// empty: the Tick's IsLootable() guard means nothing has accrued yet *today*,
	// but that is a property of a guard somewhere else, and the rule "five seconds
	// from entering this zone, and the raid going live counts as entering" should
	// hold because this line exists. Pinned by Sarko.Extract.ActivationIsTheDwellEpoch.
	Dwells.Reset();
```

and in `Tick`, replace the prune loop and the per-pawn body:

```cpp
	// Prune pawns that are gone instead of letting the map grow for the whole
	// raid. Cheap: one entry per living player, not per actor.
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
```

The `%.2fs of dwell` in that log line is not decoration: it is what makes the next live run able to say whether the dwell was five seconds or four point eight, which is exactly the question the e2e had to answer by subtracting timestamps.

- [ ] **Step 5: Fix the `Seed == 0` trigger**

In `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, inside `namespace SarkoRaid`:

```cpp
	/**
	 * Whether this machine should build and spawn its copy of the map now. Pure.
	 *
	 * Keyed on bSessionReady rather than on the seed. The seed used to be the
	 * trigger, and that was a bug: replication sends no change when a property
	 * equals its default, so an authoritative seed of exactly 0 never fired
	 * OnRep_Seed and left a joining client in an empty world with nothing logging
	 * it. bSessionReady is false until the raid is live and its flip always
	 * replicates, and "the raid has begun" is what the trigger actually means.
	 */
	bool ShouldSpawnClientLayout(bool bLayoutBuilt, bool bSessionReady);
```

Change `bSessionReady`'s declaration to notify, and add the handler:

```cpp
	UPROPERTY(ReplicatedUsing = OnRep_SessionReady, BlueprintReadOnly, Category = "Raid")
	bool bSessionReady = false;

	/**
	 * Fires on clients the moment the raid goes live — the honest "the raid has
	 * begun" signal, and therefore where a client spawns its copy of the map.
	 * Replaces OnRep_Seed as the trigger; OnRep_Seed still calls the same
	 * idempotent builder, so whichever of the two properties a bunch delivers
	 * first, the layout is built exactly once.
	 */
	UFUNCTION()
	void OnRep_SessionReady();
```

and update the "known gap" paragraph on `Seed` to record that it is closed:

```cpp
	/**
	 * Shared basis for server-authoritative rolls. Set by the game mode on the
	 * server, which takes it from sarko-api's raid/start response. It does not
	 * shape the map — geometry comes from the map file — so changing it changes
	 * what is in the crates, not where they are.
	 *
	 * It is no longer the layout trigger. It used to be, and a seed of exactly 0
	 * equals this default, so replication sent no change and OnRep_Seed never
	 * fired: a joining client would have spawned no geometry at all.
	 * OnRep_SessionReady is the trigger now (SarkoRaid::ShouldSpawnClientLayout);
	 * OnRep_Seed still calls the same idempotent builder, because the seed
	 * arriving is also a perfectly good moment to have the map.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Seed, BlueprintReadOnly, Category = "Raid")
	int32 Seed = 0;
```

In `SarkoRaidGameState.cpp`:

```cpp
bool SarkoRaid::ShouldSpawnClientLayout(bool bLayoutBuilt, bool bSessionReady)
{
	return !bLayoutBuilt && bSessionReady;
}
```

```cpp
void ASarkoRaidGameState::OnRep_SessionReady()
{
	// The real "the raid has begun" edge. BuildAndSpawnLayout is idempotent, so
	// this and OnRep_Seed together still build exactly one map.
	BuildAndSpawnLayout();
}
```

and guard the builder with the pure rule, replacing `SpawnPrebuiltLayout`'s bare `if (bLayoutBuilt) return;` — note the rule takes `bSessionReady`, and the **server** reaches `SpawnPrebuiltLayout` directly from `StartPlay` before `bSessionReady` is set, so the authority bypasses the gate:

```cpp
void ASarkoRaidGameState::SpawnPrebuiltLayout(const FSarkoMapLayout& InLayout, const FSarkoMapDefinition& InDefinition)
{
	// The server calls this from StartPlay, before ActivateRaid sets bSessionReady
	// — it already has the layout and does not wait for a signal it sends itself.
	// A client only ever arrives here through an OnRep, where the gate applies.
	if (!HasAuthority() && !SarkoRaid::ShouldSpawnClientLayout(bLayoutBuilt, bSessionReady))
	{
		return;
	}
	if (bLayoutBuilt)
	{
		return;
	}
	// ... the rest of the function is unchanged ...
```

`GetLifetimeReplicatedProps` needs no change: `bSessionReady` is already registered with `DOREPLIFETIME`, and adding a `ReplicatedUsing` to an already-registered property does not change its registration.

- [ ] **Step 6: Run everything**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Extract`
Expected: `10 test(s) performed, 0 failed` (6 before + 4). `Sarko.Extract.DwellAccumulatesAndResets` must still be among them and still green — `AdvanceDwell` was kept precisely so no existing assertion is deleted.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `73 test(s) performed, 0 failed`.

- [ ] **Step 7: Prove the dwell live**

The unit tests pin the rule; only a run proves the game mode uses it. This one reads the dwell straight out of the new log line:

```bash
cd SarkoGame
echo "a5-t7-$(date +%s)" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -14500 18600 200" 2>/dev/null &
sleep 40 && kill %1 2>/dev/null
grep -E "raid live|extracted at zone" ~/Library/Logs/SarkoGame/SarkoGame.log
```

Expected: `extracted at zone 0 ('Северная тропа') after 5.0Xs of dwell`. The number must be **≥ 5.0**, and the wall-clock gap between the `raid live` line and this one must be ≥ 5 s — a pawn teleported onto the pad *before* activation still owes the full dwell from activation, which is the whole point of Step 4's `Dwells.Reset()`. A value like `4.85` means the clamp or the entry frame is wrong; a gap under 5 s means the epoch reset did not happen.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "fix(game): dwell is measured from entering a zone; bSessionReady triggers the client layout"
```

---

### Task 8: Live end-to-end verification of the whole loop

Eighth, and the only task that can fail for reasons no test can catch. It proves the two deliverables against the deployed backend: the loop runs, and the tutorial flag really flips on the first successful raid and not before.

**Files:**
- Create: `.superpowers/sdd/task-a5-report.md` (the evidence; this is the deliverable)
- Modify: `.superpowers/sdd/progress.md` (the ledger tail)

**Interfaces:**
- Consumes: everything above. Produces no code.

- [ ] **Step 1: Confirm the deployed backend has the migration**

Task 1 shipped a migration; nothing verified it ran on Railway. `main.go` migrates on every boot, so a redeploy is the trigger:

```bash
curl -s https://sarko-api-production.up.railway.app/healthz
DEVICE="a5-probe-$(date +%s)"
TOKEN=$(curl -s -X POST https://sarko-api-production.up.railway.app/v1/auth/anonymous \
  -H 'Content-Type: application/json' -d "{\"device_id\":\"$DEVICE\"}" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')
curl -s https://sarko-api-production.up.railway.app/v1/profile -H "Authorization: Bearer $TOKEN" | python3 -m json.tool
```

Expected: `{"status":"ok"}`, then a profile containing `"tutorial_completed": false`. If the field is **absent**, the deployed image predates Task 1 — the client will run in tutorial mode forever (the safe direction, and exactly what Task 2's `AbsentTutorialFlagMeansTutorialMode` test guarantees), but the rest of this task cannot be verified. Redeploy `sarko-api` from `main` and re-run this step before continuing. Record the raw JSON in the report either way.

- [ ] **Step 2: Run the tutorial raid, headlessly, on a fresh device**

A fresh device id is what makes this the *first* raid:

```bash
cd SarkoGame
DEVICE="a5-t8-tutorial-$(date +%s)"
echo "$DEVICE" > Saved/SarkoDevice.txt
echo "device: $DEVICE"
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -14500 18600 200" 2>/dev/null &
sleep 50 && kill %1 2>/dev/null
cp ~/Library/Logs/SarkoGame/SarkoGame.log /tmp/a5-tutorial.log
grep -E "authenticated as player|profile for |TUTORIAL loot|normal loot|raid session .* opened|raid live|after .* of dwell|raid finished as|result 'extracted'|returning to the shelter|SarkoTravel: travelling" /tmp/a5-tutorial.log
```

Expected, in order:
1. `authenticated as player <uuid>`
2. `profile for <uuid> — 3 stash rows, tier 'none', tutorial PENDING` (three rows is the starter kit)
3. `SarkoRaidGameMode: TUTORIAL loot requested but none of 42 containers in 'bridge' carries fixedItems — every container will roll instead.` — the Stage C marker. Its presence is the expected result of this stage, not a failure.
4. `raid session … opened, seed <n>` and `raid live — seed <n>, clock 900s`
5. `extracted at zone 0 ('Северная тропа') after 5.0Xs of dwell`
6. `raid finished as ESarkoRaidOutcome::Extracted`
7. `returning to the shelter in 5.0s`
8. `result 'extracted' recorded: {…"already_closed":false}`
9. `SarkoTravel: travelling to /Engine/Maps/Entry with options '(none — the shelter)'`
10. a **second** `profile for <uuid>` line, this time `tutorial completed`

Line 10 is the single most important line in this task: it proves the loop closed, the Shelter re-fetched, and the backend latched the flag. Paste the whole grep output into the report.

- [ ] **Step 3: Run a second raid on the same device and confirm the mode flipped**

```bash
cd SarkoGame
# Same device id: this is the point. Wait out any open session first — a killed
# run leaves a raid open until RAID_TTL (20 min) or until the result lands, and
# the run above submitted its result, so this should start cleanly.
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -14500 18600 200" 2>/dev/null &
sleep 50 && kill %1 2>/dev/null
cp ~/Library/Logs/SarkoGame/SarkoGame.log /tmp/a5-normal.log
grep -E "profile for |TUTORIAL loot|normal loot|raid_in_progress|insufficient_items|playing OFFLINE" /tmp/a5-normal.log
```

Expected: `tutorial completed` in the profile line, then `SarkoRaidGameMode: normal loot — containers roll against the raid seed`, and **no** `TUTORIAL loot` line. `409 raid_in_progress` means the previous raid is still open — wait and retry; do not rotate the device id, because that would silently make this a first raid again and the test would pass vacuously.

- [ ] **Step 4: Confirm the death path replays the tutorial**

A fresh device again, and this time no `BugItGo` — the pawn is left to be shot by the six bots, or the raid times out. `PostRaidReturnSeconds` plus the 15-minute clock makes the timeout path too slow for a scripted run, so drive it with the existing cheat:

```bash
cd SarkoGame
DEVICE="a5-t8-death-$(date +%s)"
echo "$DEVICE" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
  -game -RenderOffscreen -unattended -nosplash -ResX=1280 -ResY=720 -log \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo 6000 -6000 200" 2>/dev/null &
sleep 90 && kill %1 2>/dev/null
grep -E "TUTORIAL loot|raid finished as|result 'died'|returning to the shelter|profile for " \
  ~/Library/Logs/SarkoGame/SarkoGame.log | tail -8
```

`6000 -6000` is deep in the dense southern half of `bridge.json`, where the bots are. Expected: `TUTORIAL loot requested …`, `raid finished as ESarkoRaidOutcome::Died`, `result 'died' recorded`, `returning to the shelter`, and a final `profile for … tutorial PENDING` — **still pending after a death**, which is spec §6.5's "dying replays the tutorial with the same static layout".

If the pawn survives 90 s (the bots may not find it), verify this path against the backend directly instead and say so in the report:

```bash
TOKEN=$(curl -s -X POST https://sarko-api-production.up.railway.app/v1/auth/anonymous \
  -H 'Content-Type: application/json' -d "{\"device_id\":\"a5-death-probe-$(date +%s)\"}" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')
S=$(curl -s -X POST https://sarko-api-production.up.railway.app/v1/raid/start -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' -d '{"map_id":"bridge","loadout":[]}')
SID=$(echo "$S" | python3 -c 'import sys,json;print(json.load(sys.stdin)["session_id"])')
STOK=$(echo "$S" | python3 -c 'import sys,json;print(json.load(sys.stdin)["session_token"])')
curl -s -X POST https://sarko-api-production.up.railway.app/v1/raid/confirm -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' -d "{\"session_id\":\"$SID\",\"session_token\":\"$STOK\"}" > /dev/null
curl -s -X POST https://sarko-api-production.up.railway.app/v1/raid/result -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' -d "{\"session_id\":\"$SID\",\"session_token\":\"$STOK\",\"outcome\":\"died\",\"items\":[]}" | python3 -m json.tool
curl -s https://sarko-api-production.up.railway.app/v1/profile -H "Authorization: Bearer $TOKEN" \
  | python3 -c 'import sys,json;print("tutorial_completed:", json.load(sys.stdin)["tutorial_completed"])'
```

Expected: `tutorial_completed: False`.

- [ ] **Step 5: Look at the Shelter with human eyes**

No script can tell whether a menu is legible. Run windowed, on this machine, and click it:

```bash
cd SarkoGame
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry" -game -windowed -ResX=1280 -ResY=720 -log
```

Check, and record each answer in the report:
- The Shelter appears on boot — no raid, no HUD, no pawn.
- "УКРИТТЯ", the garage line, and the stash list are all readable; the three starter-kit items show **Ukrainian names**, not ids.
- "З'ЄДНАННЯ..." appears briefly and then goes away; "В РЕЙД" becomes clickable.
- "МАГАЗИН — НЕЗАБАРОМ" is visibly disabled and does nothing.
- Clicking "В РЕЙД" loads the raid, the HUD appears, and **the menu is gone** — a shelter widget still on screen over the HUD is the `EndPlay` removal having failed.
- After extracting or dying, the outcome banner shows, then the Shelter returns with the outcome line and an updated stash.

- [ ] **Step 6: Write the report**

Create `.superpowers/sdd/task-a5-report.md` with: the exact commands run, the raw grep output of each, the `curl` JSON from Steps 1 and 4, the six human answers from Step 5, and a **Gaps** section. Gaps that are already known and must be listed rather than discovered:

1. **No `fixedItems` are authored anywhere**, so the tutorial's static layout does not exist yet and every tutorial raid rolls. Stage C's job; the Warning in the log is the marker, and its disappearance is Stage C's acceptance bar.
2. **No recipe endpoint.** `SarkoShelter::BicycleRecipe()` mirrors `garage.go`'s unexported `recipes[TierBicycle]`. Propose `GET /v1/garage/recipe` for whichever stage first needs a second tier.
3. **No craft button.** The Shelter shows garage *progress*; `POST /v1/garage/craft` is not wired. §6.5 does not ask for it.
4. **Slate is throwaway UI.** It must be rebuilt in UMG when binary assets are allowed; the pure `SarkoShelter::Build*` functions are what survives.
5. **The device id is per-project** (`Saved/SarkoDevice.txt`), so two `-game` instances launched from this folder share one player and one stash. Carried forward from Stage A.
6. **No LOS check on looting** — carried forward from Stage A, deferred to Stage C polish.
7. Anything Step 5 turned up.

- [ ] **Step 7: Update the ledger and commit**

Append to `.superpowers/sdd/progress.md` a `=== STAGE A.5 ===` block naming each task's commit, the final counts (**73 UE tests, 94 backend tests**), and an `OPEN NOTES` line that carries forward the still-open items from the Stage A list — **device id per-project** and **no LOS check on looting** — and **removes** the two this plan closed (the dwell epoch, `Seed == 0`).

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add .superpowers && git commit -m "test: live e2e evidence for the Shelter and the one-time tutorial raid"
```

---

## Manual verification — the part no agent can do

Only the owner can answer these, and none of them is a test:

- **Does the Shelter feel like a place?** It is a grey Slate panel over an empty void. The MVP bar is "readable and obviously the main menu", not "atmospheric" — but if it reads as a crash screen, that is a finding.
- **Is "В РЕЙД" reachable one-handed on a phone?** The plan pins it to the lower-middle third; only a thumb on a real device can confirm it.
- **Are five seconds the right pause on the outcome banner?** `PostRaidReturnSeconds` is one ini line away from any other number.
- **Is being sent to the Shelter after every raid right, or does it want a "снова в рейд" button?** §6.5 says the Shelter; a second raid currently costs two clicks and a level load.
- **Should the tutorial be skippable?** Today it ends only by extracting. A player who cannot beat it is stuck in it.

---

## Self-Review

Checked against §6.5 and against the real code, with fresh eyes.

**Spec coverage.** Every clause of §6.5 maps to a task:

| §6.5 clause | Task |
|---|---|
| containers gain an optional `fixedItems` list | 6 |
| backend gains `tutorial_completed`, set on the first *successful* result | 1 |
| dying replays the tutorial | 1 (flag untouched on `died`, tested), 8 (verified live) |
| profile exposes the flag | 1 (wire), 2 (parse) |
| no flag → `fixedItems`; flag set → seeded rolls | 6 |
| fixed lists pass the same plausibility gate | 6 — parse-time catalog check + `domain.ValidateRaidItems` unchanged at the edge; no backend change needed |
| static layout authored along the Bridge_West route | **deliberately not done** — flagged as conflict 1, assigned to Stage C, marked by a per-raid Warning |
| boot → shelter → "В РЕЙД" → raid → outcome → shelter | 4 (boot + outbound), 5 (return) |
| MVP shelter is Slate-in-C++, no binary assets | 4, and Decision 1 |
| stash list from `/v1/profile` | 2, 4 |
| raid button | 4 |
| garage progress (bicycle 0/3) | 4 — mirrored recipe, flagged as conflict 3 |
| shop stub, no P2W | 4 — disabled button, no logic |
| the EXTRACTED summary moves to the shelter | 5 |
| profile is refreshed on return | 3 (`RecordRaidOutcome` clears `bProfileLoaded`), 4 (fetch on entry) |
| the 5 s dwell bug | 7 |
| `Seed == 0` never firing `OnRep_Seed` | 7 |

**Wire shapes, checked against the Go handlers, not the spec.** `GET /v1/profile` returns `store.Profile` serialised verbatim (`internal/api/profile_handler.go` → `WriteJSON(w, 200, profile)`), whose tags are `player_id`, `schema_version`, `stash`, `vehicle_tier`, `unlocked_maps` — and `tutorial_completed` after Task 1. `domain.ItemStack`'s tags are `item_id` and `quantity`. `stash` is ordered by `item_id` (`ORDER BY item_id` in `Profile`). `vehicle_tier` is `domain.Tier`, a string, and `COALESCE(g.vehicle_tier, 'none')` means it is never empty. The four existing calls are unchanged and their bodies/parsers are untouched. The one new SQL write is `UPDATE players SET tutorial_completed = true WHERE id = $1 AND NOT tutorial_completed`, inside `SubmitResult`'s existing transaction, after `addItemsTx` and before the session close, on the `extracted` branch only — not on the replay branch (credits nothing) and not on the expired branch (closes as `died`).

**Module deps actually present.** `SarkoGame.Build.cs` today lists `Core`, `CoreUObject`, `Engine`, `InputCore`, `AIModule`, `NavigationSystem`, `DeveloperSettings`, `Json`, `JsonUtilities`, `HTTP` — read, not assumed. This plan adds only `Slate` and `SlateCore`, and both are already reachable: `Engine.Build.cs` lists them in `PublicDependencyModuleNames` (lines 78-91). `UMG` is only in `Engine`'s **private** list, so it is *not* reachable and the plan never asks for it. `SButton`, `SScrollBox`, `SBoxPanel`, `STextBlock`, `SBorder` and `Styling/CoreStyle.h` were each confirmed present under `Runtime/Slate/Public` and `Runtime/SlateCore/Public`; `SButton`'s default `ButtonStyle`/`TextStyle` come from `FCoreStyle::Get()`, so no style asset is involved. `UGameViewportClient::AddViewportWidgetContent`/`RemoveViewportWidgetContent` exist at `Classes/Engine/GameViewportClient.h:258,265`.

**No binary assets anywhere.** Every created file in the File Structure is `.h`, `.cpp`, `.ini` or `.sql`. The only asset paths referenced are `/Engine/Maps/Entry` (already the project's map) and, indirectly through unchanged code, `/Engine/BasicShapes/*`. No `.umap` is created — which is precisely why the plan travels one level to itself with a `game=` option instead of adding a menu level, and why Decision 2 exists at all.

**The dwell-bug step is present** as Task 7, Steps 1-4 and 7: three pure tests, a `Dwells.Reset()` in `ActivateRaid`, a zone-keyed dwell struct, a `%.2fs of dwell` log line so the next live run can read the number instead of subtracting timestamps, and a live check whose stated pass condition is `≥ 5.0` seconds *and* a ≥ 5 s wall-clock gap after `raid live`. The disagreement between the e2e report ("correct and deliberate") and the owner ("a bug") is flagged as conflict 2 rather than silently resolved, and the resolution keeps the guard that stops a dwell completing before there is a session to submit to.

**Engine behaviour verified rather than remembered.** `?game=` overriding `GlobalDefaultGameMode` (`GameInstance.cpp:1514`, and `GetGameModeForName` passing an unaliased name through at `EngineSettingsModule.cpp:120`); `OpenLevel` appending options and calling `SetClientTravel` (`GameplayStatics.cpp:981-1005`); and the one that would have cost a session — `FURL`'s relative constructor copying `Base->Op` (`URL.cpp:168-175`), which is why both trips are `bAbsolute = true` and why a relative return trip would be an infinite raid loop.

**Type consistency.** `FSarkoProfile::bTutorialCompleted` is spelled that way everywhere it is used (Tasks 2, 4, 6). `ASarkoRaidGameMode::bTutorialLoot` is the server-only member and is distinct from it by name on purpose. `SarkoLoot::RollContainerFor` has one signature, declared in Task 6 and called only from `SarkoCharacter.cpp`. `SarkoShelter::BuildView` has one signature, introduced in Task 4 and filled in by Task 5 without changing it — Task 4's `(void)LastRaid;` placeholder exists so the signature is stable across the two tasks. `Dwells` is the member's name after Task 7 (renamed from `DwellSeconds`, and every reader in `Tick` and `ActivateRaid` is updated in the same step). `SarkoExtract::AdvanceDwell` is **kept**, not replaced, so no existing assertion is deleted and the test count only goes up.

**Test counts, traced.** UE: 56 → +3 (Task 2) → +1 (Task 3) → +3 (Task 4) → +2 (Task 5) → +4 (Task 6) → +4 (Task 7) = **73**. Backend: 89 → +5 (Task 1) = **94**. Every verify step names its running total, and no task deletes a test.
