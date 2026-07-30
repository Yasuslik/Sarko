# Loot and Extraction Implementation Plan (Stage A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Bridge sector from a place you can walk around into a raid you can complete: open containers for server-rolled loot, carry it in a 12-slot backpack, extract at a north-edge zone after a 5 s dwell — or die and lose it — with the outcome recorded by the deployed `sarko-api`.

**Architecture:** Every value-granting decision is a pure C++ function (`RollContainer`, `AddToBackpack`, `AdvanceDwell`, `CanInteract`) called only on the authority, so all of it is unit-testable headlessly and none of it is reachable from a client. Containers and extraction zones are *not* replicated actors: like the map's cover blocks, both sides spawn identical actors from the same shipped `bridge.json`, and only the mutable bit — which container indices have been looted — replicates, as a byte array on the game state. The backend is reached through `FSarkoBackendClient`, split into pure body-builders/response-parsers (tested) and a thin `FHttpModule` transport (not tested, loud on failure).

**Tech Stack:** UE 5.8, C++ only, `Build.sh` + `UnrealEditor-Cmd`, automation tests under `-nullrhi`, screenshots under `-RenderOffscreen`. Backend: Go stdlib mux, pgx, goose embedded migrations, Postgres.

## Global Constraints

- Specs: Stage A is **§4 of `docs/superpowers/specs/2026-07-30-sarko-production-raid-loop-design.md`**. Every request/response shape is **normative from the real handlers in `sarko-api/internal/api/`** — see "Contract of record" below, and read `docs/superpowers/specs/2026-07-29-sarko-raid-slice-design.md` §7/§10/§11 for the *reasons* (§7 lists endpoints only; it does not specify fields).
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`.
- **`DefaultBuildSettings` stays `BuildSettingsVersion.V7`.** `SarkoGame.Build.cs` keeps `PrivateIncludePaths.Add(ModuleDirectory)` and every existing dependency; this plan adds **`"HTTP"`** (`"Json"` and `"JsonUtilities"` are already there — verify, do not re-add).
- **Create no binary assets. Ever.** No `.uasset`, `.umap`, Blueprint, UMG widget, Enhanced Input action, DataTable, Behavior Tree. C++, `.ini`, `.json`, `.sh` only. Referencing an engine asset by path (`/Engine/BasicShapes/Cube.Cube`) is fine. Files written under `Saved/` at runtime (the device-id file) are fine.
- **HUD is `AHUD::DrawHUD` primitives only.** Input is the existing polling in `ASarkoPlayerController` — no input assets.
- **Touch layout:** the bottom corners are the thumbs' dead zone. New HUD goes along the **top** (prompt, dwell countdown, backpack counter, summary) or the **vertical centre band on the right** (the interact button). Never a bottom corner.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code** — `UnrealEditor-Cmd` exits 0 having run zero tests, and the script takes its verdict from the engine log. **The suite is at 27 tests before this plan.** Every verify step names the expected total.
- The automation-test flag spelling that compiles is `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`.
- **`USarkoRaidSettings` is `config = Game` → settings live in `Config/DefaultGame.ini`.** `GameMapsSettings` is `config = Engine` → `DefaultEngine.ini`. A setting in the wrong file silently loads the C++ default and nothing warns.
- **`SetMobility(Static)` before `SetStaticMesh` silently no-ops after BeginPlay.** Spawn Movable → assign mesh/scale/collision → Static. `SpawnMeshBox` in `Map/SarkoMapBuilder.cpp` is the reference and the only spawn path new geometry may use.
- **`AStaticMeshActor` does not replicate.** Nothing in this plan makes scenery replicate. Containers and extraction zones spawn deterministically on every machine from `Data/Maps/bridge.json`; the only replicated loot state is `ASarkoRaidGameState::LootedContainers`.
- **`UFUNCTION(Exec)` cannot sit inside `#if !UE_BUILD_SHIPPING`** — UHT rejects it. Declare unconditionally, guard the *body* in the `.cpp` (`SarkoOverview` is the precedent).
- **`FRotator` members are doubles in 5.8** — a bare float literal in `TestEqual` is ambiguous. Compare `FVector`/`FRotator` with `.Equals(..., Tolerance)`, and always suffix scalar literals with `f` when the compared value is a float.
- **Forward-declare structs at global scope**, never as an elaborated type specifier inside a namespace (`const struct FFoo&` inside `namespace Bar {}` declares a *second*, permanently incomplete `Bar::FFoo`). `Core/SarkoRaidGameState.h` carries the comment explaining the bug; follow it.
- **RPC inputs are hostile.** Every client-supplied container index is bounds-checked against `Definition.Containers.Num()` before use; every distance is re-measured from the server's own copy of the pawn. The `ServerRequestFire` direction-normalisation in `Combat/SarkoWeapon.cpp` is the precedent.
- **No per-tick HTTP and no per-tick allocation** in any Tick path this plan adds. HTTP happens exactly four times per raid (auth, start, confirm, result). The proximity scan iterates a cached `TArray` and allocates nothing.
- **HTTP callbacks fire after world teardown.** Every completion handler binds through `TWeakObjectPtr` / `CreateWeakLambda` and returns early when the owner is gone.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything that discards working-tree changes — a subagent on this project already destroyed an uncommitted file that way.
- Backend: all SQL stays in `internal/store`. Migrations are goose-embedded under `internal/db/migrations/`. Tests run `go test ./... -count=1 -p 1` with `-race` where invoked directly; test DB is Postgres on **port 5455** via `make test-db`.

### Contract of record (verified against the deployed service on 2026-07-30)

Base URL: `https://sarko-api-production.up.railway.app`. Field names are exactly these — do not invent, rename or add.

| Call | Request body | 200 response |
|---|---|---|
| `POST /v1/auth/anonymous` (no auth) | `{"device_id":"<≤128 chars>"}` | `{"player_id":"<uuid>","token":"<jwt>"}` |
| `GET /v1/profile` (Bearer) | — | `{"player_id","schema_version","stash":[{"item_id","quantity"}],"vehicle_tier","unlocked_maps":[...]}` |
| `POST /v1/raid/start` (Bearer) | `{"map_id":"bridge","loadout":[{"item_id","quantity"}]}` | `{"session_id","session_token","seed","expires_at"}` |
| `POST /v1/raid/confirm` (Bearer) | `{"session_id","session_token"}` | `{"expires_at":"<RFC3339>"}` |
| `POST /v1/raid/result` (Bearer) | `{"session_id","session_token","outcome":"extracted"\|"died","items":[{"item_id","quantity"}]}` | `{"session_id","outcome","credited_items":[...],"already_closed":bool}` |

Errors are always `{"error":{"code":"<snake_case>","message":"..."}}`. Codes this plan must handle by name: `map_locked` (403), `raid_in_progress` (409), `insufficient_items` (409), `session_not_open` (409), `bad_session_token` (401), `safe_pocket_overflow` (400).

### Three real conflicts between the spec and the deployed code — flagged, not silently resolved

1. **RESOLVED 2026-07-31 (owner decision):** the backend's tier-`none` map was renamed `forest` → `bridge` in `internal/domain/garage.go` (commit `80a5a4d`); the ladder above it is unchanged. The wire id and the local data-file name are now both `bridge`, so `BackendMapId` keeps its indirection only as configuration hygiene and defaults to `bridge`. Every `forest` literal that remained in Tasks 7–9 below has been updated to `bridge` accordingly.
2. **`RAID_TTL=12m` on the deployed service vs `raidDurationSeconds: 900` (15 min) in `bridge.json`.** `sarko-api/README.md` states the rule: the client timer must be **strictly shorter** than `RAID_TTL`, and `GRACE_BUFFER` is not play time. Confirm returns `expires_at = now + RAID_TTL + GRACE_BUFFER` = 14 min. Task 8 therefore clamps the raid clock to `min(map duration, (expires_at − now) − BackendGraceMarginSeconds)` = 12 min, and logs the clamp loudly. **To actually play 15 minutes, `RAID_TTL` must be raised to ≥ 16m on Railway.** That is an infra change and is out of this plan's scope — flag it in the task-9 report.
3. **`seed` overflows `int32`.** `StartRaid` does `int64(rand.Uint32())`, so seeds up to 4 294 967 295 arrive (the live probe returned `3402905197`). `ASarkoRaidGameMode::Seed` and `ASarkoRaidGameState::Seed` are `int32`. Task 7 adds `SarkoBackend::SeedToInt32` doing the bit-preserving `uint32` reinterpretation, with a test pinning that exact live value. A naïve assignment or `FCString::Atoi` is silent, platform-dependent corruption.

### Two deliberate spec extensions (not conflicts)

- `items.json` gains a **`category`** field (`weapon|ammo|med|junk|valuable|vehicle_part`) beyond spec §4.1's `{id, name, stackSize}`. Without it the ТЗ §30 rule "med never yields weapons or vehicle parts" cannot be checked by a test at all.
- Loot state replicates as `TArray<uint8>` on the game state rather than a `bLooted` bool per container actor. Spec §4.3 asks for a replicated `bLooted`; a locally-spawned, non-net-addressable actor **cannot** replicate a property, and making containers server-spawned replicated actors would break the "both sides build identical state locally" rule from the map plan. The byte array is the same information with the same authority.

## File Structure

```
SarkoGame/
├── Data/
│   ├── Items/items.json                    # NEW: the item catalog (Task 1)
│   └── Loot/loot-tables.json               # NEW: per-tier tables (Task 3)
├── Config/DefaultGame.ini                  # + loot/extract/backend settings
├── Scripts/hud-shot.sh                     # NEW: offscreen HUD screenshot (Task 9)
└── Source/SarkoGame/
    ├── SarkoGame.Build.cs                  # + "HTTP"
    ├── Loot/
    │   ├── SarkoItemCatalog.h/.cpp         # NEW: FSarkoItemStack, catalog, parse+load
    │   ├── SarkoLootTable.h/.cpp           # NEW: tables, validation, RollContainer
    │   ├── SarkoBackpack.h/.cpp            # NEW: pure AddToBackpack + replicated component
    │   ├── SarkoLootContainer.h/.cpp       # NEW: ASarkoLootContainer + CanInteract
    │   └── SarkoExtractionZone.h/.cpp      # NEW: ASarkoExtractionZone + AdvanceDwell
    ├── Net/
    │   └── SarkoBackendClient.h/.cpp       # NEW: pure bodies/parsers + FHttpModule transport
    ├── Core/
    │   ├── SarkoRaidSettings.h             # + loot, extract, backend knobs
    │   ├── SarkoRaidGameMode.h/.cpp        # raid outcome authority, dwell tick, backend calls
    │   ├── SarkoRaidGameState.h/.cpp       # + LootedContainers, Outcome, container registry
    │   └── SarkoPlayerController.h/.cpp    # + interact polling, input freeze, SarkoShot
    ├── Map/SarkoMapBuilder.h/.cpp          # container markers -> real actors; extraction zones
    ├── Pawn/SarkoCharacter.h/.cpp          # + backpack component, loot channel, death hook
    ├── UI/SarkoHUD.h/.cpp                  # + prompt, progress, backpack count, dwell, summary
    └── Tests/
        ├── LootTest.cpp                    # NEW: catalog, tables, rolls, backpack
        ├── ExtractionTest.cpp              # NEW: interact gate, dwell
        └── BackendClientTest.cpp           # NEW: bodies, parsers, seed, map id

sarko-api/
├── internal/db/migrations/0002_starter_kit.sql   # NEW
├── internal/domain/loot.go                       # NEW: known items + plausibility caps
├── internal/domain/loot_test.go                  # NEW
├── internal/store/players.go                     # + GrantStarterKit
├── internal/store/players_test.go                # + starter-kit tests
├── internal/api/auth_handler.go                  # calls GrantStarterKit
├── internal/api/raid_handler.go                  # + plausibility gate
└── internal/api/endpoints_test.go                # 3 stash assertions adjusted
```

Pure logic sits in `namespace SarkoLoot` / `SarkoExtract` / `SarkoBackend` free functions, separate from the actors that call them, for the same reason the map parser is separate from the map spawner: a function taking values and returning values is testable with no world, and `run-tests.sh` runs under `-nullrhi` where there is no world worth having.

---

### Task 1: Item catalog

The ids defined here are the wire ids for everything downstream — loot tables, the backpack, the raid result, and the backend's plausibility gate. Nothing else can be written first.

**Files:**
- Create: `SarkoGame/Data/Items/items.json`
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`

**Interfaces:**
- Consumes: `Json` module (already a dependency), the `ParseDefinition` + `LoadDefinitionFromDisk` shape from `Map/SarkoMapDefinition.cpp` (copy that structure exactly: pure parse, thin disk wrapper, error string names the problem).
- Produces:
  - `FSarkoItemStack { FName Item; int32 Quantity; }`
  - `ESarkoItemCategory { Weapon, Ammo, Med, Junk, Valuable, VehiclePart }`
  - `FSarkoItemDef { FName Id; FString Name; int32 StackSize; ESarkoItemCategory Category; }`
  - `FSarkoItemCatalog { TArray<FSarkoItemDef> Items; const FSarkoItemDef* Find(FName Id) const; }`
  - `bool SarkoLoot::ParseItemCatalog(const FString& Json, FSarkoItemCatalog& Out, FString& OutError)`
  - `bool SarkoLoot::LoadItemCatalogFromDisk(FSarkoItemCatalog& Out, FString& OutError)`
  - `const FSarkoItemCatalog& SarkoLoot::GetItemCatalog()` — loads once, logs `Error` and returns empty on failure.

- [ ] **Step 1: Write the failing catalog tests**

Create `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Loot/SarkoItemCatalog.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	const FString GoodCatalogJson = TEXT(R"({
		"items": [
			{ "id": "pistol",      "name": "Пістолет",     "stackSize": 1,  "category": "weapon" },
			{ "id": "ammo_9mm",    "name": "Патрони 9мм",   "stackSize": 60, "category": "ammo" },
			{ "id": "medkit",      "name": "Аптечка",       "stackSize": 3,  "category": "med" },
			{ "id": "scrap_metal", "name": "Металолом",     "stackSize": 10, "category": "junk" },
			{ "id": "chain",       "name": "Ланцюг",        "stackSize": 1,  "category": "vehicle_part" }
		]
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoItemCatalogParses,
	"Sarko.Loot.ItemCatalogParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoItemCatalogParses::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	TestTrue(TEXT("a well-formed catalog parses"), SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("all five items are read"), Catalog.Items.Num(), 5);

	const FSarkoItemDef* Ammo = Catalog.Find(TEXT("ammo_9mm"));
	if (!Ammo)
	{
		AddError(TEXT("ammo_9mm did not resolve, so every field check below is meaningless"));
		return false;
	}
	TestEqual(TEXT("the UA display name survives"), Ammo->Name, FString(TEXT("Патрони 9мм")));
	TestEqual(TEXT("stack size survives"), Ammo->StackSize, 60);
	TestTrue(TEXT("category is read, not defaulted"), Ammo->Category == ESarkoItemCategory::Ammo);

	TestTrue(TEXT("a vehicle part is categorised as one"),
		Catalog.Find(TEXT("chain")) && Catalog.Find(TEXT("chain"))->Category == ESarkoItemCategory::VehiclePart);
	TestNull(TEXT("an unknown id resolves to nothing, never to a default item"), Catalog.Find(TEXT("nonsense")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoItemCatalogRejectsBadInput,
	"Sarko.Loot.ItemCatalogRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoItemCatalogRejectsBadInput::RunTest(const FString& Parameters)
{
	// The catalog is the wire contract with the backend. A silently-accepted
	// broken entry means an item that exists on the client, does not exist in
	// the backend's known-items set, and makes /v1/raid/result reject the whole
	// haul at the end of a raid — the worst possible moment to find out.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"),           TEXT("{{{") },
		{ TEXT("no items array"),     TEXT(R"({"stuff":[]})") },
		{ TEXT("empty items array"),  TEXT(R"({"items":[]})") },
		{ TEXT("missing id"),         TEXT(R"({"items":[{"name":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("empty id"),           TEXT(R"({"items":[{"id":"","name":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("missing name"),       TEXT(R"({"items":[{"id":"x","stackSize":1,"category":"junk"}]})") },
		{ TEXT("zero stack size"),    TEXT(R"({"items":[{"id":"x","name":"x","stackSize":0,"category":"junk"}]})") },
		{ TEXT("unknown category"),   TEXT(R"({"items":[{"id":"x","name":"x","stackSize":1,"category":"cheese"}]})") },
		{ TEXT("duplicate id"),       TEXT(R"({"items":[{"id":"x","name":"x","stackSize":1,"category":"junk"},{"id":"x","name":"y","stackSize":1,"category":"junk"}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoItemCatalog Catalog;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoLoot::ParseItemCatalog(Case.Value, Catalog, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRealItemCatalogIsUsable,
	"Sarko.Loot.RealItemCatalogIsUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRealItemCatalogIsUsable::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error))
	{
		AddError(FString::Printf(TEXT("Data/Items/items.json failed to load: %s"), *Error));
		return false;
	}

	// The starter kit the backend grants at registration must exist here, or the
	// shelter shows a player three items it cannot name.
	for (const FName Required : { FName(TEXT("pistol")), FName(TEXT("ammo_9mm")), FName(TEXT("medkit")) })
	{
		TestNotNull(*FString::Printf(TEXT("starter-kit item '%s' is in the catalog"), *Required.ToString()),
			Catalog.Find(Required));
	}

	// The bicycle recipe in sarko-api/internal/domain/garage.go is the only place
	// the backend names item ids of its own. Those three must be findable in a
	// raid or the first garage step is unreachable.
	for (const FName Part : { FName(TEXT("bike_frame")), FName(TEXT("wheel_small")), FName(TEXT("chain")) })
	{
		const FSarkoItemDef* Def = Catalog.Find(Part);
		TestNotNull(*FString::Printf(TEXT("bicycle part '%s' is in the catalog"), *Part.ToString()), Def);
		if (Def)
		{
			TestTrue(*FString::Printf(TEXT("'%s' is categorised as a vehicle part"), *Part.ToString()),
				Def->Category == ESarkoItemCategory::VehiclePart);
		}
	}

	// item_id is capped at 64 characters by domain.ValidateStacks; anything
	// longer is rejected by the backend at result time.
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		TestTrue(*FString::Printf(TEXT("'%s' fits the backend's 64-char item_id cap"), *Def.Id.ToString()),
			Def.Id.ToString().Len() <= 64);
		TestFalse(*FString::Printf(TEXT("'%s' has a display name"), *Def.Id.ToString()), Def.Name.IsEmpty());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `BUILD FAILED` — `'Loot/SarkoItemCatalog.h' file not found`.

- [ ] **Step 3: Write the header**

`SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "SarkoItemCatalog.generated.h"

/**
 * What an item is for. Beyond spec §4.1's {id, name, stackSize}, and needed:
 * ТЗ §30 forbids the `med` loot tier from yielding weapons or vehicle parts,
 * and without a category on the item that rule cannot be checked by anything
 * except a human reading the table.
 */
UENUM()
enum class ESarkoItemCategory : uint8
{
	Weapon,
	Ammo,
	Med,
	Junk,
	Valuable,
	VehiclePart
};

/**
 * A quantity of one item id. The unit of loot, of the backpack, and of the
 * backend's `{"item_id","quantity"}` wire shape.
 */
USTRUCT()
struct FSarkoItemStack
{
	GENERATED_BODY()

	UPROPERTY()
	FName Item;

	UPROPERTY()
	int32 Quantity = 0;
};

/** One catalog entry. Presentation plus the two rules loot needs: stacking and category. */
USTRUCT()
struct FSarkoItemDef
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	/** Ukrainian display name. The HUD draws this, never the id. */
	UPROPERTY()
	FString Name;

	/** How many of this item share one backpack slot. Always >= 1. */
	UPROPERTY()
	int32 StackSize = 1;

	UPROPERTY()
	ESarkoItemCategory Category = ESarkoItemCategory::Junk;
};

/**
 * Every item in the game.
 *
 * The backend is the source of truth for item *ids* — they are free-form TEXT
 * in `stash_items`, and `sarko-api/internal/domain/garage.go` fixes the vehicle
 * part ids by naming them in its recipes. This catalog is presentation plus the
 * client-side rules, and Task 2 makes the backend reject any id that is not in
 * it, so the two can only drift with a test going red.
 */
USTRUCT()
struct FSarkoItemCatalog
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoItemDef> Items;

	/** Null for an unknown id — never a default item. A default would put an
	 *  invented id into a raid result and get the whole haul rejected. */
	const FSarkoItemDef* Find(FName Id) const;
};

namespace SarkoLoot
{
	/**
	 * Parses the catalog. Pure: text in, catalog out, no disk and no world,
	 * which is what lets the schema be tested from string literals.
	 *
	 * Every failure sets OutError to something that names the problem, for the
	 * same reason the map parser does: this file is hand-edited, and a silently
	 * half-loaded catalog surfaces as a rejected raid result fifteen minutes
	 * later instead of as an error at load.
	 */
	bool ParseItemCatalog(const FString& Json, FSarkoItemCatalog& OutCatalog, FString& OutError);

	/** Reads Data/Items/items.json from the project directory. */
	bool LoadItemCatalogFromDisk(FSarkoItemCatalog& OutCatalog, FString& OutError);

	/**
	 * The process-wide catalog, loaded on first use. Logs Error and returns an
	 * empty catalog if the file is broken; callers then find nothing, which is
	 * the safe direction — no loot rather than invented loot.
	 */
	const FSarkoItemCatalog& GetItemCatalog();
}
```

- [ ] **Step 4: Write the parser**

`SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.cpp`:

```cpp
#include "Loot/SarkoItemCatalog.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const FSarkoItemDef* FSarkoItemCatalog::Find(FName Id) const
{
	return Items.FindByPredicate([Id](const FSarkoItemDef& Def) { return Def.Id == Id; });
}

namespace
{
	/** Category names as they appear in items.json. Unknown is an error, not a default. */
	bool ParseCategory(const FString& Text, ESarkoItemCategory& Out)
	{
		static const TMap<FString, ESarkoItemCategory> Names = {
			{ TEXT("weapon"),       ESarkoItemCategory::Weapon },
			{ TEXT("ammo"),         ESarkoItemCategory::Ammo },
			{ TEXT("med"),          ESarkoItemCategory::Med },
			{ TEXT("junk"),         ESarkoItemCategory::Junk },
			{ TEXT("valuable"),     ESarkoItemCategory::Valuable },
			{ TEXT("vehicle_part"), ESarkoItemCategory::VehiclePart },
		};
		if (const ESarkoItemCategory* Found = Names.Find(Text))
		{
			Out = *Found;
			return true;
		}
		return false;
	}
}

bool SarkoLoot::ParseItemCatalog(const FString& Json, FSarkoItemCatalog& OutCatalog, FString& OutError)
{
	OutCatalog = FSarkoItemCatalog();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(TEXT("items"), Items) || !Items)
	{
		OutError = TEXT("'items' is missing or not an array");
		return false;
	}
	if (Items->Num() == 0)
	{
		OutError = TEXT("'items' is empty — an empty catalog makes every loot table invalid");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object) || !Object)
		{
			OutError = TEXT("'items' contains a non-object entry");
			return false;
		}

		FSarkoItemDef Def;

		FString Id;
		if (!(*Object)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			OutError = TEXT("an item has no 'id'");
			return false;
		}
		Def.Id = FName(*Id);

		if (OutCatalog.Find(Def.Id) != nullptr)
		{
			OutError = FString::Printf(TEXT("item '%s' is defined twice"), *Id);
			return false;
		}

		if (!(*Object)->TryGetStringField(TEXT("name"), Def.Name) || Def.Name.IsEmpty())
		{
			OutError = FString::Printf(TEXT("item '%s' has no 'name'"), *Id);
			return false;
		}

		double StackSize = 0.0;
		if (!(*Object)->TryGetNumberField(TEXT("stackSize"), StackSize) || StackSize < 1.0)
		{
			OutError = FString::Printf(TEXT("item '%s': 'stackSize' is missing or less than 1"), *Id);
			return false;
		}
		Def.StackSize = static_cast<int32>(StackSize);

		FString CategoryText;
		if (!(*Object)->TryGetStringField(TEXT("category"), CategoryText) || !ParseCategory(CategoryText, Def.Category))
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'category' must be weapon, ammo, med, junk, valuable or vehicle_part"), *Id);
			return false;
		}

		OutCatalog.Items.Add(Def);
	}

	return true;
}

bool SarkoLoot::LoadItemCatalogFromDisk(FSarkoItemCatalog& OutCatalog, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Items") / TEXT("items.json");

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read the item catalog at %s"), *Path);
		return false;
	}
	if (!ParseItemCatalog(Json, OutCatalog, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}

const FSarkoItemCatalog& SarkoLoot::GetItemCatalog()
{
	// Loaded once per process. The catalog never changes at runtime, and every
	// loot roll and every HUD draw would otherwise re-read a file from disk.
	static FSarkoItemCatalog Catalog;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Error;
		if (!LoadItemCatalogFromDisk(Catalog, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoLoot: %s — no item will resolve and no container will yield loot"), *Error);
			Catalog = FSarkoItemCatalog();
		}
	}
	return Catalog;
}
```

- [ ] **Step 5: Author the catalog**

`SarkoGame/Data/Items/items.json`. Ids are the wire contract — lowercase snake_case, ≤ 64 characters, never renamed once a player's stash holds one. `bike_frame`, `wheel_small` and `chain` are copied **verbatim** from `sarko-api/internal/domain/garage.go`'s `TierBicycle` recipe; `fuel_tank` and `battery` also exist there and are deliberately absent from this catalog, because they belong to later tiers and this sector must not complete a vehicle step.

```json
{
  "_readme": "Item ids are the wire contract with sarko-api (stash_items.item_id). Never rename an id that a player's stash may already hold. bike_frame / wheel_small / chain are copied verbatim from sarko-api/internal/domain/garage.go's bicycle recipe. Task 2 mirrors this list into internal/domain/loot.go, and a Go test reads this file to catch drift.",
  "items": [
    { "id": "pistol",       "name": "Пістолет ПМ",          "stackSize": 1,  "category": "weapon" },
    { "id": "ammo_9mm",     "name": "Патрони 9×18",         "stackSize": 60, "category": "ammo" },
    { "id": "medkit",       "name": "Аптечка",              "stackSize": 3,  "category": "med" },
    { "id": "bandage",      "name": "Бинт",                 "stackSize": 5,  "category": "med" },
    { "id": "painkillers",  "name": "Обезболювальне",       "stackSize": 5,  "category": "med" },
    { "id": "scrap_metal",  "name": "Металолом",            "stackSize": 10, "category": "junk" },
    { "id": "copper_wire",  "name": "Мідний дріт",          "stackSize": 10, "category": "junk" },
    { "id": "duct_tape",    "name": "Армований скотч",      "stackSize": 5,  "category": "junk" },
    { "id": "canned_food",  "name": "Консерви",             "stackSize": 5,  "category": "valuable" },
    { "id": "vodka",        "name": "Горілка",              "stackSize": 3,  "category": "valuable" },
    { "id": "cigarettes",   "name": "Цигарки",              "stackSize": 5,  "category": "valuable" },
    { "id": "toolbox",      "name": "Ящик з інструментами", "stackSize": 1,  "category": "valuable" },
    { "id": "bike_frame",   "name": "Рама велосипеда",      "stackSize": 1,  "category": "vehicle_part" },
    { "id": "wheel_small",  "name": "Мале колесо",          "stackSize": 2,  "category": "vehicle_part" },
    { "id": "chain",        "name": "Ланцюг",               "stackSize": 1,  "category": "vehicle_part" }
  ]
}
```

`Config/DefaultEngine.ini` already stages the whole `Data` tree (`+DirectoriesToAlwaysStageAsNonUFS=(Path="Data")`), so `Data/Items` and `Data/Loot` ship without another entry. **Verify that line is still present; do not add a duplicate.**

- [ ] **Step 6: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `3 test(s) performed, 0 failed`, `ALL GREEN` — `ItemCatalogParses`, `ItemCatalogRejectsBadInput`, `RealItemCatalogIsUsable`.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `30 test(s) performed, 0 failed` (27 before this plan + 3).

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): item catalog loaded from Data/Items/items.json"
```

---

### Task 2: Backend — starter kit at registration, and a plausibility gate on submitted items

Second, because the client cannot legally start a raid until a new player's stash holds something, and because the trust boundary in spec §4.7 must exist before the client starts submitting hauls.

**Answer to the question spec §4.7 leaves open: no, the backend does not plausibility-check submitted items today.** `internal/api/raid_handler.go` → `domain.ValidateStacks` enforces only: ≤ 64 stacks, non-empty `item_id` of ≤ 64 characters, `0 < quantity ≤ 1 000 000`. Plus, for `outcome=died` only, ≤ 2 merged stacks (`maxSafePocketItems`). There is **no item catalog and no loot table anywhere in the backend**, so `{"item_id":"turbine","quantity":1000000}` on an `extracted` result is accepted and credited today. This task closes that to the "count + tier" floor the spec asks for; full loot-table reachability is deliberately deferred (see the note at the end of this task).

**Files:**
- Create: `sarko-api/internal/db/migrations/0002_starter_kit.sql`
- Create: `sarko-api/internal/domain/loot.go`, `loot_test.go`
- Modify: `sarko-api/internal/store/players.go` (add `GrantStarterKit`)
- Modify: `sarko-api/internal/store/players_test.go` (add starter-kit tests)
- Modify: `sarko-api/internal/api/auth_handler.go` (call it)
- Modify: `sarko-api/internal/api/raid_handler.go` (apply the gate)
- Modify: `sarko-api/internal/api/endpoints_test.go` (3 stash assertions now see the kit)

**Interfaces:**
- Consumes: the item ids authored in Task 1's `items.json`; `store.Store`, `domain.ItemStack`, `domain.MergeStacks`, `addItemsTx` (already unexported in `players.go`).
- Produces:
  - `domain.KnownItemIDs` — `map[string]struct{}` of every legal `item_id`
  - `domain.StarterKit() []ItemStack`
  - `domain.MaxRaidStacks = 12`, `domain.MaxRaidUnitsPerItem = 720`
  - `domain.ValidateRaidItems(stacks []ItemStack) error` — unknown id / too many stacks / implausible quantity
  - `(*store.Store).GrantStarterKit(ctx, playerID string) (bool, error)` — true when it granted, false when already granted

- [ ] **Step 1: Write the failing domain tests**

Create `sarko-api/internal/domain/loot_test.go`:

```go
package domain_test

import (
	"encoding/json"
	"os"
	"strings"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/domain"
)

func TestStarterKitIsThePistolAmmoAndMedkit(t *testing.T) {
	kit := domain.StarterKit()
	want := map[string]int{"pistol": 1, "ammo_9mm": 60, "medkit": 1}

	if len(kit) != len(want) {
		t.Fatalf("starter kit has %d stacks, want %d: %v", len(kit), len(want), kit)
	}
	for _, s := range kit {
		if want[s.ItemID] != s.Quantity {
			t.Errorf("starter kit %s = %d, want %d", s.ItemID, s.Quantity, want[s.ItemID])
		}
	}
	// Everything granted must be a known item, or the very first raid the new
	// player starts is rejected by the gate below for carrying its own kit.
	if err := domain.ValidateRaidItems(kit); err != nil {
		t.Errorf("the starter kit must pass its own validation: %v", err)
	}
}

func TestValidateRaidItemsRejectsInventedIDs(t *testing.T) {
	// Until now the backend credited any item_id a caller invented: item ids are
	// free-form TEXT in stash_items and nothing checked them. A client that says
	// it found "turbine" — a helicopter part — in a starter sector was believed.
	err := domain.ValidateRaidItems([]domain.ItemStack{{ItemID: "unobtanium", Quantity: 1}})
	if err == nil {
		t.Fatal("an invented item id was accepted")
	}
	if !strings.Contains(err.Error(), "unknown") {
		t.Errorf("error should name the problem, got %q", err)
	}
}

func TestValidateRaidItemsCapsStacksAndQuantities(t *testing.T) {
	tooMany := make([]domain.ItemStack, 0, domain.MaxRaidStacks+1)
	for _, id := range []string{
		"pistol", "ammo_9mm", "medkit", "bandage", "painkillers", "scrap_metal",
		"copper_wire", "duct_tape", "canned_food", "vodka", "cigarettes",
		"toolbox", "chain",
	} {
		tooMany = append(tooMany, domain.ItemStack{ItemID: id, Quantity: 1})
	}
	if len(tooMany) <= domain.MaxRaidStacks {
		t.Fatalf("fixture must exceed the cap: %d stacks vs cap %d", len(tooMany), domain.MaxRaidStacks)
	}
	// The backpack holds 12 slots (spec §4.4), so 13 distinct stacks could not
	// have been carried out no matter how the raid went.
	if err := domain.ValidateRaidItems(tooMany); err == nil {
		t.Errorf("%d distinct stacks was accepted, cap is %d", len(tooMany), domain.MaxRaidStacks)
	}

	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "scrap_metal", Quantity: domain.MaxRaidUnitsPerItem + 1},
	}); err == nil {
		t.Errorf("quantity %d was accepted, cap is %d", domain.MaxRaidUnitsPerItem+1, domain.MaxRaidUnitsPerItem)
	}

	// The generous end still passes: 12 slots of the largest stack (ammo, 60)
	// is 720 units, and a legitimate ammo haul must not be rejected.
	if err := domain.ValidateRaidItems([]domain.ItemStack{
		{ItemID: "ammo_9mm", Quantity: domain.MaxRaidUnitsPerItem},
	}); err != nil {
		t.Errorf("a full backpack of ammo must be accepted: %v", err)
	}
}

func TestValidateRaidItemsAcceptsSplitEntriesThatMergeUnderTheCap(t *testing.T) {
	// endpoints_test.go already relies on this: 50 separate one-unit entries of
	// the same id merge to a single stack, which is one stack, not fifty.
	stacks := make([]domain.ItemStack, 50)
	for i := range stacks {
		stacks[i] = domain.ItemStack{ItemID: "chain", Quantity: 1}
	}
	if err := domain.ValidateRaidItems(stacks); err != nil {
		t.Errorf("50 split entries of one id must merge to one stack: %v", err)
	}
}

// TestKnownItemsMatchTheClientCatalog is the drift alarm. The client's
// items.json is the authored source; this list is a mirror, and a mirror that
// is never compared is a lie waiting to happen. The test skips rather than
// fails when the file is absent, because the deployed container image contains
// only sarko-api/ and this must not break a build that has no game directory.
func TestKnownItemsMatchTheClientCatalog(t *testing.T) {
	const path = "../../../SarkoGame/Data/Items/items.json"
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("client catalog not present at %s: %v", path, err)
	}

	var catalog struct {
		Items []struct {
			ID string `json:"id"`
		} `json:"items"`
	}
	if err := json.Unmarshal(raw, &catalog); err != nil {
		t.Fatalf("client catalog is not valid JSON: %v", err)
	}
	if len(catalog.Items) == 0 {
		t.Fatal("client catalog parsed to zero items")
	}

	client := make(map[string]struct{}, len(catalog.Items))
	for _, item := range catalog.Items {
		client[item.ID] = struct{}{}
		if _, ok := domain.KnownItemIDs[item.ID]; !ok {
			t.Errorf("client item %q is missing from domain.KnownItemIDs — a raid carrying it would be rejected", item.ID)
		}
	}
	for id := range domain.KnownItemIDs {
		if _, ok := client[id]; !ok {
			t.Errorf("domain.KnownItemIDs has %q, which the client catalog does not define", id)
		}
	}
}
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd sarko-api && go test ./internal/domain/ -run 'StarterKit|ValidateRaidItems|KnownItems' -count=1`
Expected: compile failure — `undefined: domain.StarterKit`, `undefined: domain.ValidateRaidItems`, `undefined: domain.KnownItemIDs`, `undefined: domain.MaxRaidStacks`, `undefined: domain.MaxRaidUnitsPerItem`.

- [ ] **Step 3: Write the domain rules**

Create `sarko-api/internal/domain/loot.go`:

```go
package domain

import "fmt"

// KnownItemIDs is every item id the game can legitimately produce. It mirrors
// SarkoGame/Data/Items/items.json, which is the authored source; loot_test.go
// reads that file and fails if the two lists drift.
//
// Why the backend needs its own copy: stash_items.item_id is free-form TEXT and
// ValidateStacks only bounds its *length*, so before this list existed a client
// could submit any id it liked — including a helicopter turbine out of a starter
// sector — and have it credited. The client is the raid host in this slice
// (spec §4 "зафиксированная иллюзия"), so it is exactly the party that cannot
// be trusted with the set of things that exist.
var KnownItemIDs = map[string]struct{}{
	// Weapons and ammo.
	"pistol":   {},
	"ammo_9mm": {},
	// Medical.
	"medkit":      {},
	"bandage":     {},
	"painkillers": {},
	// Junk and valuables.
	"scrap_metal": {},
	"copper_wire": {},
	"duct_tape":   {},
	"canned_food": {},
	"vodka":       {},
	"cigarettes":  {},
	"toolbox":     {},
	// Vehicle parts. These three are the bicycle recipe in garage.go; the later
	// tiers' parts (engine_small, fuel_tank, engine_large, wheel_medium,
	// wheel_large, gearbox, battery, turbine, rotor_blade, avionics) are
	// deliberately absent — no shipped loot table can produce them yet, so
	// accepting them would only ever be accepting a lie.
	"bike_frame":  {},
	"wheel_small": {},
	"chain":       {},
}

const (
	// MaxRaidStacks is the in-raid backpack size (spec §4.4): twelve slots, so
	// a result claiming thirteen distinct item stacks describes a raid that
	// could not have happened.
	MaxRaidStacks = 12
	// MaxRaidUnitsPerItem is twelve slots of the largest stack in the catalog
	// (ammo_9mm, 60), i.e. the most units of one item a full backpack can hold.
	// Deliberately generous: this is the ceiling on the physically possible,
	// not a balance number, and the balance ceiling is the loot tables.
	MaxRaidUnitsPerItem = 720
)

// StarterKit is what a brand-new player is given once, at anonymous
// registration (spec §4.6). Without it a new player has an empty stash, cannot
// field a loadout, and — since the loadout is debited at /v1/raid/start —
// enters the first raid unarmed, which the spec's decision journal (§2.2)
// explicitly rejects as breaking the loot economy.
func StarterKit() []ItemStack {
	return []ItemStack{
		{ItemID: "pistol", Quantity: 1},
		{ItemID: "ammo_9mm", Quantity: 60},
		{ItemID: "medkit", Quantity: 1},
	}
}

// ValidateRaidItems is the plausibility gate on anything a client claims to
// have carried: every id must exist, the haul must fit a backpack, and no
// single stack may exceed what a backpack could physically hold.
//
// It is deliberately separate from ValidateStacks, which bounds shape rather
// than content and is called from inside store.StartRaid and
// store.SubmitResult. Keeping content validation at the API edge leaves the
// store's own tests — which use fixture ids like "ammo" and "rifle" — working
// against the layer they actually test.
func ValidateRaidItems(stacks []ItemStack) error {
	for _, s := range stacks {
		if _, ok := KnownItemIDs[s.ItemID]; !ok {
			// The id is not echoed: it is unbounded caller input, and
			// ValidateStacks already refuses to echo it for the same reason.
			return fmt.Errorf("unknown item id")
		}
	}

	merged := MergeStacks(stacks)
	if len(merged) > MaxRaidStacks {
		return fmt.Errorf("at most %d item stacks fit a backpack, got %d", MaxRaidStacks, len(merged))
	}
	for _, s := range merged {
		if s.Quantity > MaxRaidUnitsPerItem {
			return fmt.Errorf("item %s: at most %d units fit a backpack, got %d",
				s.ItemID, MaxRaidUnitsPerItem, s.Quantity)
		}
	}
	return nil
}
```

- [ ] **Step 4: Run the domain tests and confirm they pass**

Run: `cd sarko-api && go test ./internal/domain/ -race -count=1 -v -run 'StarterKit|ValidateRaidItems|KnownItems'`
Expected: five `--- PASS` lines (`TestStarterKitIsThePistolAmmoAndMedkit`, `TestValidateRaidItemsRejectsInventedIDs`, `TestValidateRaidItemsCapsStacksAndQuantities`, `TestValidateRaidItemsAcceptsSplitEntriesThatMergeUnderTheCap`, `TestKnownItemsMatchTheClientCatalog`), `ok`. If the last one skips, the relative path to `items.json` is wrong — fix the path, do not leave it skipping.

- [ ] **Step 5: Write the failing store test for the starter kit**

Append to `sarko-api/internal/store/players_test.go`:

```go
func TestGrantStarterKitIsOneTime(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-starter")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}

	granted, err := s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("first GrantStarterKit: %v", err)
	}
	if !granted {
		t.Fatal("first GrantStarterKit reported nothing granted")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	got := make(map[string]int, len(profile.Stash))
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	for _, want := range domain.StarterKit() {
		if got[want.ItemID] != want.Quantity {
			t.Errorf("stash %s = %d, want %d", want.ItemID, got[want.ItemID], want.Quantity)
		}
	}

	// Idempotent: every app launch calls /v1/auth/anonymous, so a second grant
	// would be a free pistol per launch.
	granted, err = s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("second GrantStarterKit: %v", err)
	}
	if granted {
		t.Error("second GrantStarterKit granted the kit again")
	}

	profile, err = s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile after second grant: %v", err)
	}
	if len(profile.Stash) != len(domain.StarterKit()) {
		t.Errorf("stash grew on the second grant: %v", profile.Stash)
	}
}

func TestGrantStarterKitDoesNotComeBackAfterTheKitIsSpent(t *testing.T) {
	s := store.New(testutil.Pool(t))
	ctx := context.Background()

	playerID, err := s.UpsertPlayer(ctx, "device-spender")
	if err != nil {
		t.Fatalf("UpsertPlayer: %v", err)
	}
	if _, err := s.GrantStarterKit(ctx, playerID); err != nil {
		t.Fatalf("GrantStarterKit: %v", err)
	}

	// Take the raid: the loadout is debited at start, so the pistol leaves the
	// stash. ON CONFLICT DO NOTHING would silently re-grant it on the next
	// launch — which is why the flag lives on the player row, not on the item.
	started, err := s.StartRaid(ctx, store.StartRaidParams{
		PlayerID:   playerID,
		MapID:      "bridge",
		Loadout:    []domain.ItemStack{{ItemID: "pistol", Quantity: 1}},
		PendingTTL: time.Minute,
	})
	if err != nil {
		t.Fatalf("StartRaid: %v", err)
	}
	_ = started

	granted, err := s.GrantStarterKit(ctx, playerID)
	if err != nil {
		t.Fatalf("GrantStarterKit after spending: %v", err)
	}
	if granted {
		t.Error("the starter kit came back after being spent")
	}

	profile, err := s.Profile(ctx, playerID)
	if err != nil {
		t.Fatalf("Profile: %v", err)
	}
	for _, item := range profile.Stash {
		if item.ItemID == "pistol" {
			t.Errorf("pistol is back in the stash: %v", profile.Stash)
		}
	}
}
```

`players_test.go` already imports `context`, `errors`, `testing`, `domain`, `store` and `testutil`; add `"time"` for `PendingTTL`.

- [ ] **Step 6: Run it and confirm it fails**

Run: `cd sarko-api && make test-db && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./internal/store/ -race -count=1 -p 1 -run GrantStarterKit`
Expected: compile failure — `s.GrantStarterKit undefined (type *store.Store has no field or method GrantStarterKit)`.

- [ ] **Step 7: Add the migration**

Create `sarko-api/internal/db/migrations/0002_starter_kit.sql`:

```sql
-- +goose Up
-- One-time flag rather than "does the stash already contain a pistol": the kit
-- is spent in the first raid, and a stash-contents check would hand out a fresh
-- one on the next app launch. /v1/auth/anonymous runs on every launch, so
-- "already granted" has to be recorded, not inferred.
ALTER TABLE players ADD COLUMN starter_kit_granted BOOLEAN NOT NULL DEFAULT false;

-- Players that predate this migration keep their stash untouched and are
-- treated as not yet granted, so existing testers get the kit on next launch.

-- +goose Down
ALTER TABLE players DROP COLUMN starter_kit_granted;
```

- [ ] **Step 8: Implement `GrantStarterKit`**

Append to `sarko-api/internal/store/players.go`:

```go
// GrantStarterKit credits domain.StarterKit() to a player exactly once, ever.
// It reports whether this call was the one that granted it.
//
// The flag flip and the credit are one transaction, and the flip is the
// conditional statement: an UPDATE with `AND NOT starter_kit_granted` either
// affects one row or none, so two concurrent logins cannot both credit. Doing
// it the other way round — read the flag, then credit — is a race that hands
// out two pistols to a client that fires two auth calls at once.
func (s *Store) GrantStarterKit(ctx context.Context, playerID string) (bool, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return false, fmt.Errorf("begin: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	tag, err := tx.Exec(ctx,
		`UPDATE players SET starter_kit_granted = true
		 WHERE id = $1 AND NOT starter_kit_granted`, playerID)
	if err != nil {
		return false, fmt.Errorf("claim starter kit: %w", err)
	}
	if tag.RowsAffected() == 0 {
		// Already granted, or no such player. Either way nothing to credit, and
		// "no such player" is not this call's error to report: the caller just
		// created the row.
		return false, nil
	}

	if err := addItemsTx(ctx, tx, playerID, domain.StarterKit()); err != nil {
		return false, err
	}
	if err := tx.Commit(ctx); err != nil {
		return false, fmt.Errorf("commit: %w", err)
	}
	return true, nil
}
```

- [ ] **Step 9: Call it from the auth handler**

In `sarko-api/internal/api/auth_handler.go`, between `UpsertPlayer` and `Issuer.Issue`:

```go
		// A new device gets its free kit here, not at first raid: /v1/raid/start
		// debits the loadout, so a player with an empty stash cannot legally
		// take anything in. Failure is logged and swallowed — a missing kit is a
		// bad first raid, but refusing the token would lock the player out
		// entirely, and the next launch retries because the grant is one-time by
		// flag rather than by attempt.
		if granted, err := deps.Store.GrantStarterKit(r.Context(), playerID); err != nil {
			slog.Error("grant starter kit", "err", err, "player_id", playerID)
		} else if granted {
			slog.Info("granted starter kit", "player_id", playerID)
		}
```

- [ ] **Step 10: Apply the plausibility gate in the raid handler**

In `sarko-api/internal/api/raid_handler.go`, in `handleRaidStart`, immediately after the existing `domain.ValidateStacks(req.Loadout)` check:

```go
		// The loadout is content-checked too: an id the game does not define
		// cannot have been in a stash that only this API ever writes to, so a
		// loadout naming one is either a stale client or a forged request.
		if err := domain.ValidateRaidItems(req.Loadout); err != nil {
			WriteError(w, http.StatusBadRequest, "implausible_items", err.Error())
			return
		}
```

and in `handleRaidResult`, immediately after the existing `domain.ValidateStacks(req.Items)` check and **before** the `maxSafePocketItems` check:

```go
		// Spec §4.7: the UE server is a client to this API and is not trusted
		// with arbitrary item grants. This is the count-and-quantity floor —
		// every id must exist, the haul must fit the 12-slot backpack, and no
		// stack may exceed what a backpack holds.
		if err := domain.ValidateRaidItems(req.Items); err != nil {
			WriteError(w, http.StatusBadRequest, "implausible_items", err.Error())
			return
		}
```

- [ ] **Step 11: Fix the three endpoint assertions the kit now changes**

Every new device in `internal/api/endpoints_test.go` now starts with three stash rows. Add this helper near the top of the file (it needs `"github.com/Yasuslik/sarko-api/internal/domain"`, already imported):

```go
// loot strips the starter kit that /v1/auth/anonymous grants every new device,
// so a test asserting what a raid credited sees only what the raid credited.
func loot(stash []domain.ItemStack) []domain.ItemStack {
	kit := make(map[string]struct{}, len(domain.StarterKit()))
	for _, s := range domain.StarterKit() {
		kit[s.ItemID] = struct{}{}
	}
	out := make([]domain.ItemStack, 0, len(stash))
	for _, s := range stash {
		if _, isKit := kit[s.ItemID]; !isKit {
			out = append(out, s)
		}
	}
	return out
}
```

Then replace exactly three assertions (line numbers as of this plan; match on the text, not the number):

| Was | Becomes |
|---|---|
| `if len(profile.Stash) != 1 \|\| profile.Stash[0].ItemID != "chain" {` (~line 147) | `if got := loot(profile.Stash); len(got) != 1 \|\| got[0].ItemID != "chain" {` |
| `if len(profile.Stash) != 0 {` … `"intruder stash = %v, want empty"` (~line 373) | `if got := loot(profile.Stash); len(got) != 0 {` — and pass `got` to the `Errorf` |
| `if len(profile.Stash) != 1 \|\| profile.Stash[0].ItemID != "chain" \|\| profile.Stash[0].Quantity != 50 {` (~line 452) | `if got := loot(profile.Stash); len(got) != 1 \|\| got[0].ItemID != "chain" \|\| got[0].Quantity != 50 {` |

Update the `Errorf` argument in each from `profile.Stash` to `got` so a failure prints what was compared.

**`internal/store` tests need no changes**: they call `UpsertPlayer` (via `seedPlayer`) and never `GrantStarterKit`, which is exactly why the grant lives in a separate method rather than inside `UpsertPlayer`.

- [ ] **Step 12: Add an endpoint test proving the kit arrives over HTTP**

Append to `sarko-api/internal/api/endpoints_test.go`:

```go
func TestAnonymousAuthGrantsTheStarterKitOnceOverHTTP(t *testing.T) {
	c := newClient(t)
	c.login("device-kit-http")

	var profile store.Profile
	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	got := make(map[string]int, len(profile.Stash))
	for _, item := range profile.Stash {
		got[item.ItemID] = item.Quantity
	}
	for _, want := range domain.StarterKit() {
		if got[want.ItemID] != want.Quantity {
			t.Errorf("stash %s = %d, want %d (full stash %v)", want.ItemID, got[want.ItemID], want.Quantity, profile.Stash)
		}
	}

	// Logging in again is what every app launch does.
	c.login("device-kit-http")
	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status after re-login = %d, want 200", code)
	}
	if len(profile.Stash) != len(domain.StarterKit()) {
		t.Errorf("re-login grew the stash: %v", profile.Stash)
	}
}

func TestRaidResultRejectsAnInventedItem(t *testing.T) {
	c := newClient(t)
	c.login("device-inventor")

	var started store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "bridge", "loadout": []any{}}, &started); code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}
	c.confirm(started)

	if code := c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
		"outcome":       "extracted",
		"items":         []map[string]any{{"item_id": "turbine", "quantity": 1}},
	}, nil); code != http.StatusBadRequest {
		t.Errorf("result with a helicopter turbine out of the starter map = %d, want 400", code)
	}
}
```

`newClient`, `c.login`, `c.do` and `c.confirm` are the existing helpers in that file — read them before writing, and match their signatures rather than guessing.

- [ ] **Step 13: Run the whole backend suite**

Run: `cd sarko-api && make test`
Expected: `ok` for every package, zero failures. Then re-run the packages that touch the new code with the race detector explicitly:

Run: `cd sarko-api && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./internal/domain/ ./internal/store/ ./internal/api/ -race -count=1 -p 1`
Expected: three `ok` lines, no `DATA RACE`.

- [ ] **Step 14: Verify the migration applies to the deployed database**

`internal/db/migrate.go` runs goose on startup, so pushing is the migration. Do **not** push here — Task 9 does the live pass. Instead prove the migration is reversible and idempotent locally:

Run: `cd sarko-api && TEST_DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable" go test ./internal/db/ -race -count=1`
Expected: `ok` — `migrate_test.go` and `migrate_lock_test.go` already assert that migrations apply cleanly and that concurrent starts do not collide.

- [ ] **Step 15: Commit**

```bash
git add sarko-api && git commit -m "feat(api): starter kit at registration and a plausibility gate on raid items"
```

**Deliberately deferred, and why.** Full loot-table reachability (spec §4.7's preferred rule) would mean a second copy of `loot-tables.json` inside the backend, plus the map's container-tier census, plus keeping both in step with the client — a second source of truth for balance data whose only purpose is to catch a cheat that the count-and-catalog gate already makes unprofitable. The honest fix is the dedicated server holding the session token (slice-1 spec §7 "Готовность к Dedicated Server"), after which the client cannot report an outcome at all. Record this in the task report as an open item.

---

### Task 3: Loot tables and the deterministic server roll

**Files:**
- Create: `SarkoGame/Data/Loot/loot-tables.json`
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (extend)

**Interfaces:**
- Consumes: `FSarkoItemCatalog`, `FSarkoItemDef`, `FSarkoItemStack`, `ESarkoItemCategory`, `SarkoLoot::GetItemCatalog` (Task 1).
- Produces:
  - `FSarkoLootEntry { FName Item; float Weight; int32 MinQuantity; int32 MaxQuantity; }`
  - `FSarkoLootTable { FName Tier; int32 MinRolls; int32 MaxRolls; float EmptyChance; TArray<FSarkoLootEntry> Entries; }`
  - `FSarkoLootTables { TArray<FSarkoLootTable> Tables; const FSarkoLootTable* Find(FName Tier) const; }`
  - `bool SarkoLoot::ParseLootTables(const FString& Json, const FSarkoItemCatalog& Catalog, FSarkoLootTables& Out, FString& OutError)`
  - `bool SarkoLoot::LoadLootTablesFromDisk(const FSarkoItemCatalog& Catalog, FSarkoLootTables& Out, FString& OutError)`
  - `const FSarkoLootTables& SarkoLoot::GetLootTables()`
  - `int32 SarkoLoot::ContainerSeed(int32 RaidSeed, int32 ContainerIndex)`
  - `TArray<FSarkoItemStack> SarkoLoot::RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream)`

- [ ] **Step 1: Write the failing table tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (add `#include "Loot/SarkoLootTable.h"` and `#include "Map/SarkoMapDefinition.h"` next to the existing include):

```cpp
namespace
{
	/** A catalog the table fixtures below can be validated against. */
	FSarkoItemCatalog FixtureCatalog()
	{
		FSarkoItemCatalog Catalog;
		FString Error;
		SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error);
		return Catalog;
	}

	const FString GoodTablesJson = TEXT(R"({
		"tiers": {
			"junk":     { "rolls": {"min":1,"max":2}, "emptyChance": 0.15, "entries": [ {"item":"scrap_metal","weight":70,"qty":{"min":1,"max":3}}, {"item":"medkit","weight":30,"qty":{"min":1,"max":1}} ] },
			"common":   { "rolls": {"min":1,"max":2}, "emptyChance": 0.08, "entries": [ {"item":"scrap_metal","weight":60,"qty":{"min":1,"max":4}}, {"item":"ammo_9mm","weight":40,"qty":{"min":8,"max":16}} ] },
			"med":      { "rolls": {"min":1,"max":3}, "emptyChance": 0.10, "entries": [ {"item":"medkit","weight":100,"qty":{"min":1,"max":2}} ] },
			"good":     { "rolls": {"min":2,"max":3}, "emptyChance": 0.0,  "entries": [ {"item":"ammo_9mm","weight":50,"qty":{"min":10,"max":24}}, {"item":"chain","weight":2,"qty":{"min":1,"max":1}} ] },
			"military": { "rolls": {"min":2,"max":4}, "emptyChance": 0.0,  "entries": [ {"item":"pistol","weight":20,"qty":{"min":1,"max":1}}, {"item":"ammo_9mm","weight":80,"qty":{"min":16,"max":32}} ] }
		}
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootTablesParse,
	"Sarko.Loot.TablesParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootTablesParse::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	TestTrue(TEXT("well-formed tables parse"), SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("all five tiers are present"), Tables.Tables.Num(), 5);

	const FSarkoLootTable* Military = Tables.Find(TEXT("military"));
	if (!Military)
	{
		AddError(TEXT("the military tier did not resolve"));
		return false;
	}
	TestEqual(TEXT("rolls.min is read"), Military->MinRolls, 2);
	TestEqual(TEXT("rolls.max is read"), Military->MaxRolls, 4);
	TestEqual(TEXT("emptyChance is read"), Military->EmptyChance, 0.f);
	TestEqual(TEXT("both entries are read"), Military->Entries.Num(), 2);
	TestEqual(TEXT("entry weight is read"), Military->Entries[0].Weight, 20.f);
	TestEqual(TEXT("qty.min is read"), Military->Entries[1].MinQuantity, 16);
	TestEqual(TEXT("qty.max is read"), Military->Entries[1].MaxQuantity, 32);

	TestNull(TEXT("an unknown tier resolves to nothing"), Tables.Find(TEXT("legendary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootTablesRejectBadInput,
	"Sarko.Loot.TablesRejectBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootTablesRejectBadInput::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();

	// Spec §4.1: "Unknown item id in any loot table = load error, not a silent
	// skip." A skipped entry is a container that quietly yields less than the
	// designer wrote, which no test and no playthrough would ever localise.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"), TEXT("{{{") },
		{ TEXT("no tiers object"), TEXT(R"({"stuff":{}})") },
		{ TEXT("a tier is missing"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("unknown item id"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"plutonium","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("no entries"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[]}}})") },
		{ TEXT("zero weight"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":0,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("qty max below min"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":3,"max":1}}]}}})") },
		{ TEXT("rolls max below min"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":3,"max":1},"emptyChance":0.1,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
		{ TEXT("emptyChance above 1"), TEXT(R"({"tiers":{"junk":{"rolls":{"min":1,"max":1},"emptyChance":1.5,"entries":[{"item":"scrap_metal","weight":1,"qty":{"min":1,"max":1}}]}}})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoLootTables Tables;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoLoot::ParseLootTables(Case.Value, Catalog, Tables, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLootRollIsDeterministicPerContainer,
	"Sarko.Loot.RollIsDeterministicPerContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLootRollIsDeterministicPerContainer::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	if (!SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("fixture tables failed to parse: %s"), *Error));
		return false;
	}
	const FSarkoLootTable& Military = *Tables.Find(TEXT("military"));

	// Same raid seed and same container index must give the same contents,
	// forever: the whole reason the seed is replicated is that the server can
	// re-derive a roll without storing it, and a raid that re-rolls on retry is
	// a duplication bug.
	FRandomStream A(SarkoLoot::ContainerSeed(12345, 7));
	FRandomStream B(SarkoLoot::ContainerSeed(12345, 7));
	const TArray<FSarkoItemStack> First = SarkoLoot::RollContainer(Military, A);
	const TArray<FSarkoItemStack> Second = SarkoLoot::RollContainer(Military, B);

	TestEqual(TEXT("the same seed and index give the same number of stacks"), First.Num(), Second.Num());
	for (int32 i = 0; i < FMath::Min(First.Num(), Second.Num()); ++i)
	{
		TestEqual(TEXT("the same item"), First[i].Item, Second[i].Item);
		TestEqual(TEXT("the same quantity"), First[i].Quantity, Second[i].Quantity);
	}

	// Different containers in the same raid must not all hold the same thing.
	int32 Distinct = 0;
	for (int32 Index = 0; Index < 42; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(12345, Index));
		const TArray<FSarkoItemStack> Roll = SarkoLoot::RollContainer(Military, Stream);
		if (Roll.Num() != First.Num() || (Roll.Num() > 0 && Roll[0].Quantity != First[0].Quantity))
		{
			++Distinct;
		}
	}
	TestTrue(TEXT("42 containers do not all roll identically"), Distinct > 10);

	// ContainerSeed must not overflow-trap on the largest seeds the backend
	// sends: StartRaid returns int64(rand.Uint32()), so about half of all real
	// seeds arrive with the sign bit set.
	FRandomStream Wrapped(SarkoLoot::ContainerSeed(MIN_int32, 41));
	TestTrue(TEXT("a negative seed still produces a usable stream"),
		SarkoLoot::RollContainer(Military, Wrapped).Num() >= Military.MinRolls);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRollObeysTheTableBounds,
	"Sarko.Loot.RollObeysTheTableBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRollObeysTheTableBounds::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();
	FSarkoLootTables Tables;
	FString Error;
	if (!SarkoLoot::ParseLootTables(GoodTablesJson, Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("fixture tables failed to parse: %s"), *Error));
		return false;
	}

	// Over a thousand rolls: a stack outside the table's declared range, an item
	// not in the table, or a good/military container coming up empty are all
	// silent-in-play, obvious-in-aggregate faults.
	const FSarkoLootTable& Good = *Tables.Find(TEXT("good"));
	int32 Empties = 0;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(999, Index));
		const TArray<FSarkoItemStack> Roll = SarkoLoot::RollContainer(Good, Stream);
		if (Roll.Num() == 0)
		{
			++Empties;
			continue;
		}
		TestTrue(TEXT("roll count is within rolls.min/max"),
			Roll.Num() >= Good.MinRolls && Roll.Num() <= Good.MaxRolls);
		for (const FSarkoItemStack& Stack : Roll)
		{
			const FSarkoLootEntry* Entry = Good.Entries.FindByPredicate(
				[&Stack](const FSarkoLootEntry& E) { return E.Item == Stack.Item; });
			TestNotNull(TEXT("every rolled item is in the table"), Entry);
			if (Entry)
			{
				TestTrue(TEXT("quantity is within qty.min/max"),
					Stack.Quantity >= Entry->MinQuantity && Stack.Quantity <= Entry->MaxQuantity);
			}
		}
	}
	TestEqual(TEXT("a good container is never empty (ТЗ §30)"), Empties, 0);

	// A junk container's empty rate must sit near its declared chance, not
	// wildly off: an emptyChance that does nothing is a table nobody tuned.
	const FSarkoLootTable& Junk = *Tables.Find(TEXT("junk"));
	int32 JunkEmpties = 0;
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		FRandomStream Stream(SarkoLoot::ContainerSeed(7, Index));
		if (SarkoLoot::RollContainer(Junk, Stream).Num() == 0)
		{
			++JunkEmpties;
		}
	}
	const float Rate = static_cast<float>(JunkEmpties) / 2000.f;
	TestTrue(FString::Printf(TEXT("junk empties at roughly 15%% (got %.1f%%)"), Rate * 100.f),
		Rate > 0.10f && Rate < 0.21f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRealLootTablesObeyTheDesignRules,
	"Sarko.Loot.RealLootTablesObeyTheDesignRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRealLootTablesObeyTheDesignRules::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error))
	{
		AddError(FString::Printf(TEXT("items.json failed to load: %s"), *Error));
		return false;
	}
	FSarkoLootTables Tables;
	if (!SarkoLoot::LoadLootTablesFromDisk(Catalog, Tables, Error))
	{
		AddError(FString::Printf(TEXT("loot-tables.json failed to load: %s"), *Error));
		return false;
	}

	// ТЗ §30, verbatim as rules.
	const auto EmptyCap = [](FName Tier) -> float
	{
		if (Tier == TEXT("junk"))     { return 0.15f; }
		if (Tier == TEXT("common"))   { return 0.08f; }
		if (Tier == TEXT("med"))      { return 0.15f; }
		return 0.f; // good and military are never empty
	};

	for (const FSarkoLootTable& Table : Tables.Tables)
	{
		TestTrue(FString::Printf(TEXT("'%s' emptyChance is within its cap"), *Table.Tier.ToString()),
			Table.EmptyChance <= EmptyCap(Table.Tier) + KINDA_SMALL_NUMBER);

		float TotalWeight = 0.f;
		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			TotalWeight += Entry.Weight;
		}
		TestTrue(FString::Printf(TEXT("'%s' has positive total weight"), *Table.Tier.ToString()), TotalWeight > 0.f);

		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			const FSarkoItemDef* Def = Catalog.Find(Entry.Item);
			TestNotNull(*FString::Printf(TEXT("'%s' entry '%s' is a catalog item"),
				*Table.Tier.ToString(), *Entry.Item.ToString()), Def);
			if (!Def)
			{
				continue;
			}

			// The med tier is medicine, not a shortcut to a weapon or a bicycle.
			if (Table.Tier == TEXT("med"))
			{
				TestTrue(*FString::Printf(TEXT("med tier does not yield '%s' (a %s)"),
					*Entry.Item.ToString(), *Entry.Item.ToString()),
					Def->Category != ESarkoItemCategory::Weapon && Def->Category != ESarkoItemCategory::VehiclePart);
			}

			// "Bicycle parts appear only as rare singles" — otherwise the first
			// sector completes a vehicle tier in one or two raids and the whole
			// garage progression is over before it starts.
			if (Def->Category == ESarkoItemCategory::VehiclePart)
			{
				TestEqual(*FString::Printf(TEXT("vehicle part '%s' drops as a single"), *Entry.Item.ToString()),
					Entry.MaxQuantity, 1);
				TestTrue(*FString::Printf(TEXT("vehicle part '%s' is rare in '%s' (%.1f%% of weight)"),
					*Entry.Item.ToString(), *Table.Tier.ToString(), 100.f * Entry.Weight / TotalWeight),
					Entry.Weight / TotalWeight <= 0.03f);
			}
		}
	}

	// Every tier the shipped map actually uses must have a table, or those
	// containers open to nothing at all.
	FSarkoMapDefinition Map;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestNotNull(*FString::Printf(TEXT("bridge.json tier '%s' has a loot table"), *Spot.Tier.ToString()),
			Tables.Find(Spot.Tier));
	}
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `BUILD FAILED` — `'Loot/SarkoLootTable.h' file not found`.

- [ ] **Step 3: Write the header**

`SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoLootTable.generated.h"

/** One possible drop: which item, how likely relative to its siblings, how many. */
USTRUCT()
struct FSarkoLootEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName Item;

	/** Relative weight within its tier. Always > 0; probabilities are weight/total. */
	UPROPERTY()
	float Weight = 1.f;

	UPROPERTY()
	int32 MinQuantity = 1;

	UPROPERTY()
	int32 MaxQuantity = 1;
};

/** What one container tier can hold. */
USTRUCT()
struct FSarkoLootTable
{
	GENERATED_BODY()

	UPROPERTY()
	FName Tier;

	UPROPERTY()
	int32 MinRolls = 1;

	UPROPERTY()
	int32 MaxRolls = 1;

	/**
	 * Chance the container is simply empty. ТЗ §30 caps this per tier: junk
	 * 0.15, common 0.08, med 0.15, and good/military never empty — walking
	 * across the ravine for a locked-and-empty military crate is the single
	 * most demoralising outcome an extraction map can produce.
	 */
	UPROPERTY()
	float EmptyChance = 0.f;

	UPROPERTY()
	TArray<FSarkoLootEntry> Entries;
};

/** Every tier's table. */
USTRUCT()
struct FSarkoLootTables
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoLootTable> Tables;

	/** Null for an unknown tier. A default tier would silently mis-loot a container. */
	const FSarkoLootTable* Find(FName Tier) const;
};

namespace SarkoLoot
{
	/** The five tiers every table file must define, in this order. */
	extern const FName TierJunk;
	extern const FName TierCommon;
	extern const FName TierMed;
	extern const FName TierGood;
	extern const FName TierMilitary;

	/**
	 * Parses the loot tables and validates them against the catalog. Pure.
	 *
	 * An item id that is not in the catalog is a load **error**, never a skipped
	 * entry (spec §4.1): a skip changes the drop table silently, and the symptom
	 * is "loot feels thin", which nobody can trace to a typo in a data file.
	 */
	bool ParseLootTables(const FString& Json, const FSarkoItemCatalog& Catalog,
		FSarkoLootTables& OutTables, FString& OutError);

	/** Reads Data/Loot/loot-tables.json from the project directory. */
	bool LoadLootTablesFromDisk(const FSarkoItemCatalog& Catalog, FSarkoLootTables& OutTables, FString& OutError);

	/** The process-wide tables, loaded on first use against GetItemCatalog(). Loud and empty on failure. */
	const FSarkoLootTables& GetLootTables();

	/**
	 * The stream seed for one container: `RaidSeed ^ ContainerIndex` (spec §4.2).
	 *
	 * XOR is done in uint32 and reinterpreted, because the backend's seed is
	 * `int64(rand.Uint32())` and therefore routinely has the sign bit set —
	 * signed arithmetic on that is undefined behaviour, and "undefined" here
	 * means two machines can disagree about what is in a crate.
	 */
	int32 ContainerSeed(int32 RaidSeed, int32 ContainerIndex);

	/**
	 * Rolls one container's contents. Called **only on the server, only at the
	 * moment the container is opened** (spec §4.2, §6.1): contents generated
	 * ahead of time would replicate to clients and be readable out of memory,
	 * which is a loot map.
	 *
	 * Deterministic in Stream, so the same raid seed and container index always
	 * produce the same haul and a retried transfer cannot duplicate loot.
	 */
	TArray<FSarkoItemStack> RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream);
}
```

- [ ] **Step 4: Write the parser and the roll**

`SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.cpp`:

```cpp
#include "Loot/SarkoLootTable.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const FName SarkoLoot::TierJunk(TEXT("junk"));
const FName SarkoLoot::TierCommon(TEXT("common"));
const FName SarkoLoot::TierMed(TEXT("med"));
const FName SarkoLoot::TierGood(TEXT("good"));
const FName SarkoLoot::TierMilitary(TEXT("military"));

const FSarkoLootTable* FSarkoLootTables::Find(FName Tier) const
{
	return Tables.FindByPredicate([Tier](const FSarkoLootTable& Table) { return Table.Tier == Tier; });
}

namespace
{
	/** Reads {"min":a,"max":b} with a >= Floor and b >= a. */
	bool ReadRange(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32 Floor,
		int32& OutMin, int32& OutMax, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Range = nullptr;
		if (!Object->TryGetObjectField(Field, Range) || !Range)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an object"), *Field);
			return false;
		}
		double Min = 0.0;
		double Max = 0.0;
		if (!(*Range)->TryGetNumberField(TEXT("min"), Min) || !(*Range)->TryGetNumberField(TEXT("max"), Max))
		{
			OutError = FString::Printf(TEXT("'%s' needs both 'min' and 'max'"), *Field);
			return false;
		}
		if (Min < static_cast<double>(Floor) || Max < Min)
		{
			OutError = FString::Printf(TEXT("'%s' must satisfy %d <= min <= max, got min=%g max=%g"),
				*Field, Floor, Min, Max);
			return false;
		}
		OutMin = static_cast<int32>(Min);
		OutMax = static_cast<int32>(Max);
		return true;
	}
}

bool SarkoLoot::ParseLootTables(const FString& Json, const FSarkoItemCatalog& Catalog,
	FSarkoLootTables& OutTables, FString& OutError)
{
	OutTables = FSarkoLootTables();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	const TSharedPtr<FJsonObject>* Tiers = nullptr;
	if (!Root->TryGetObjectField(TEXT("tiers"), Tiers) || !Tiers)
	{
		OutError = TEXT("'tiers' is missing or not an object");
		return false;
	}

	// Fixed set, fixed order. A tier the map uses but the file omits would open
	// a container onto nothing, so absence is an error rather than a default.
	const TArray<FName> Required = { TierJunk, TierCommon, TierMed, TierGood, TierMilitary };

	for (const FName& Tier : Required)
	{
		const TSharedPtr<FJsonObject>* TierObject = nullptr;
		if (!(*Tiers)->TryGetObjectField(Tier.ToString(), TierObject) || !TierObject)
		{
			OutError = FString::Printf(TEXT("tier '%s' is missing"), *Tier.ToString());
			return false;
		}

		FSarkoLootTable Table;
		Table.Tier = Tier;

		if (!ReadRange(*TierObject, TEXT("rolls"), 1, Table.MinRolls, Table.MaxRolls, OutError))
		{
			OutError = FString::Printf(TEXT("tier '%s': %s"), *Tier.ToString(), *OutError);
			return false;
		}

		double EmptyChance = 0.0;
		if (!(*TierObject)->TryGetNumberField(TEXT("emptyChance"), EmptyChance) ||
			EmptyChance < 0.0 || EmptyChance > 1.0)
		{
			OutError = FString::Printf(TEXT("tier '%s': 'emptyChance' must be between 0 and 1"), *Tier.ToString());
			return false;
		}
		Table.EmptyChance = static_cast<float>(EmptyChance);

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!(*TierObject)->TryGetArrayField(TEXT("entries"), Entries) || !Entries || Entries->Num() == 0)
		{
			OutError = FString::Printf(TEXT("tier '%s': 'entries' is missing or empty"), *Tier.ToString());
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("tier '%s': 'entries' has a non-object member"), *Tier.ToString());
				return false;
			}

			FSarkoLootEntry Entry;

			FString ItemId;
			if (!(*Object)->TryGetStringField(TEXT("item"), ItemId) || ItemId.IsEmpty())
			{
				OutError = FString::Printf(TEXT("tier '%s': an entry has no 'item'"), *Tier.ToString());
				return false;
			}
			Entry.Item = FName(*ItemId);
			if (Catalog.Find(Entry.Item) == nullptr)
			{
				// Spec §4.1: an unknown id is a load error, not a silent skip.
				OutError = FString::Printf(TEXT("tier '%s': item '%s' is not in the item catalog"),
					*Tier.ToString(), *ItemId);
				return false;
			}

			double Weight = 0.0;
			if (!(*Object)->TryGetNumberField(TEXT("weight"), Weight) || Weight <= 0.0)
			{
				OutError = FString::Printf(TEXT("tier '%s': item '%s' needs a positive 'weight'"),
					*Tier.ToString(), *ItemId);
				return false;
			}
			Entry.Weight = static_cast<float>(Weight);

			if (!ReadRange(*Object, TEXT("qty"), 1, Entry.MinQuantity, Entry.MaxQuantity, OutError))
			{
				OutError = FString::Printf(TEXT("tier '%s', item '%s': %s"), *Tier.ToString(), *ItemId, *OutError);
				return false;
			}

			Table.Entries.Add(Entry);
		}

		OutTables.Tables.Add(Table);
	}

	return true;
}

bool SarkoLoot::LoadLootTablesFromDisk(const FSarkoItemCatalog& Catalog, FSarkoLootTables& OutTables, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Loot") / TEXT("loot-tables.json");

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read the loot tables at %s"), *Path);
		return false;
	}
	if (!ParseLootTables(Json, Catalog, OutTables, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}

const FSarkoLootTables& SarkoLoot::GetLootTables()
{
	static FSarkoLootTables Tables;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Error;
		if (!LoadLootTablesFromDisk(GetItemCatalog(), Tables, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoLoot: %s — every container will open empty"), *Error);
			Tables = FSarkoLootTables();
		}
	}
	return Tables;
}

int32 SarkoLoot::ContainerSeed(int32 RaidSeed, int32 ContainerIndex)
{
	// Unsigned XOR then reinterpret: the backend's seed is int64(rand.Uint32()),
	// so the sign bit is set about half the time, and signed overflow is UB.
	const uint32 Mixed = static_cast<uint32>(RaidSeed) ^ static_cast<uint32>(ContainerIndex);
	return static_cast<int32>(Mixed);
}

TArray<FSarkoItemStack> SarkoLoot::RollContainer(const FSarkoLootTable& Table, FRandomStream& Stream)
{
	TArray<FSarkoItemStack> Out;

	// Empty check first, so emptyChance means "this container is empty" rather
	// than "one of its rolls produced nothing".
	if (Table.EmptyChance > 0.f && Stream.FRand() < Table.EmptyChance)
	{
		return Out;
	}
	if (Table.Entries.Num() == 0)
	{
		return Out;
	}

	float TotalWeight = 0.f;
	for (const FSarkoLootEntry& Entry : Table.Entries)
	{
		TotalWeight += Entry.Weight;
	}
	if (TotalWeight <= 0.f)
	{
		return Out;
	}

	const int32 Rolls = Stream.RandRange(Table.MinRolls, Table.MaxRolls);
	Out.Reserve(Rolls);

	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		float Pick = Stream.FRand() * TotalWeight;
		const FSarkoLootEntry* Chosen = &Table.Entries.Last();
		for (const FSarkoLootEntry& Entry : Table.Entries)
		{
			Pick -= Entry.Weight;
			if (Pick <= 0.f)
			{
				Chosen = &Entry;
				break;
			}
		}
		// Chosen defaults to the last entry so a float that never crosses zero
		// (FRand can return values arbitrarily close to 1) still drops something
		// instead of silently skipping a roll.
		Out.Add(FSarkoItemStack{ Chosen->Item, Stream.RandRange(Chosen->MinQuantity, Chosen->MaxQuantity) });
	}

	return Out;
}
```

- [ ] **Step 5: Author the loot tables**

`SarkoGame/Data/Loot/loot-tables.json`. Weights are relative within a tier. The bicycle parts appear only in `good` and `military`, at ≤ 3 % of their tier's weight and never more than one at a time — with 22 `good` and 8 `military` containers on `bridge.json`, that is roughly one part per raid, so three raids minimum for a bicycle even with perfect luck. `bridge.json` uses only `junk`, `good` and `military` today; `common` and `med` are authored because the schema requires all five and Stage C's containers will use them.

```json
{
  "_readme": "ТЗ §30 rules, enforced by Sarko.Loot.RealLootTablesObeyTheDesignRules: junk may be empty <=15%, common <=8%, med <=15%, good and military NEVER empty. med yields no weapons and no vehicle parts. Vehicle parts drop as singles at <=3% of their tier's weight, so this sector cannot complete the bicycle in one or two raids. Item ids must exist in Data/Items/items.json or the file fails to load.",
  "tiers": {
    "junk": {
      "rolls": { "min": 1, "max": 2 },
      "emptyChance": 0.15,
      "entries": [
        { "item": "scrap_metal", "weight": 40, "qty": { "min": 1, "max": 4 } },
        { "item": "copper_wire", "weight": 25, "qty": { "min": 1, "max": 3 } },
        { "item": "duct_tape",   "weight": 15, "qty": { "min": 1, "max": 2 } },
        { "item": "cigarettes",  "weight": 12, "qty": { "min": 1, "max": 2 } },
        { "item": "bandage",     "weight": 8,  "qty": { "min": 1, "max": 2 } }
      ]
    },
    "common": {
      "rolls": { "min": 1, "max": 2 },
      "emptyChance": 0.08,
      "entries": [
        { "item": "scrap_metal",  "weight": 26, "qty": { "min": 2, "max": 5 } },
        { "item": "copper_wire",  "weight": 20, "qty": { "min": 1, "max": 4 } },
        { "item": "canned_food",  "weight": 18, "qty": { "min": 1, "max": 2 } },
        { "item": "ammo_9mm",     "weight": 16, "qty": { "min": 8, "max": 16 } },
        { "item": "bandage",      "weight": 12, "qty": { "min": 1, "max": 3 } },
        { "item": "cigarettes",   "weight": 8,  "qty": { "min": 1, "max": 3 } }
      ]
    },
    "med": {
      "rolls": { "min": 1, "max": 2 },
      "emptyChance": 0.12,
      "entries": [
        { "item": "bandage",     "weight": 50, "qty": { "min": 1, "max": 3 } },
        { "item": "painkillers", "weight": 32, "qty": { "min": 1, "max": 2 } },
        { "item": "medkit",      "weight": 18, "qty": { "min": 1, "max": 1 } }
      ]
    },
    "good": {
      "rolls": { "min": 2, "max": 3 },
      "emptyChance": 0.0,
      "entries": [
        { "item": "ammo_9mm",    "weight": 28, "qty": { "min": 12, "max": 24 } },
        { "item": "canned_food", "weight": 20, "qty": { "min": 1, "max": 3 } },
        { "item": "vodka",       "weight": 16, "qty": { "min": 1, "max": 2 } },
        { "item": "toolbox",     "weight": 14, "qty": { "min": 1, "max": 1 } },
        { "item": "medkit",      "weight": 12, "qty": { "min": 1, "max": 1 } },
        { "item": "copper_wire", "weight": 8,  "qty": { "min": 2, "max": 5 } },
        { "item": "chain",       "weight": 2,  "qty": { "min": 1, "max": 1 } }
      ]
    },
    "military": {
      "rolls": { "min": 2, "max": 4 },
      "emptyChance": 0.0,
      "entries": [
        { "item": "ammo_9mm",    "weight": 34, "qty": { "min": 20, "max": 40 } },
        { "item": "medkit",      "weight": 20, "qty": { "min": 1, "max": 2 } },
        { "item": "pistol",      "weight": 14, "qty": { "min": 1, "max": 1 } },
        { "item": "painkillers", "weight": 14, "qty": { "min": 1, "max": 3 } },
        { "item": "toolbox",     "weight": 10, "qty": { "min": 1, "max": 1 } },
        { "item": "duct_tape",   "weight": 5,  "qty": { "min": 1, "max": 3 } },
        { "item": "bike_frame",  "weight": 2,  "qty": { "min": 1, "max": 1 } },
        { "item": "wheel_small", "weight": 1,  "qty": { "min": 1, "max": 1 } }
      ]
    }
  }
}
```

- [ ] **Step 6: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `8 test(s) performed, 0 failed` — the 3 from Task 1 plus `TablesParse`, `TablesRejectBadInput`, `RollIsDeterministicPerContainer`, `RollObeysTheTableBounds`, `RealLootTablesObeyTheDesignRules`.

If `RealLootTablesObeyTheDesignRules` fails, fix `loot-tables.json`, not the test — the test *is* ТЗ §30. If the junk empty-rate band fails, the roll's empty check is wrong, not the band.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `35 test(s) performed, 0 failed`.

```bash
git add SarkoGame && git commit -m "feat(game): loot tables and the deterministic server-side container roll"
```

---

### Task 4: The 12-slot backpack

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp` (own the component, clear it on death)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h` (`BackpackSlots`)
- Modify: `SarkoGame/Config/DefaultGame.ini` (`BackpackSlots=12`)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp` (the `used/12` readout)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (extend)

**Interfaces:**
- Consumes: `FSarkoItemStack`, `FSarkoItemCatalog`, `SarkoLoot::GetItemCatalog` (Task 1).
- Produces:
  - `int32 SarkoLoot::AddToBackpack(TArray<FSarkoItemStack>& Slots, const FSarkoItemCatalog& Catalog, int32 SlotLimit, FName Item, int32 Quantity)` — returns the quantity that did **not** fit
  - `USarkoBackpackComponent` with `const TArray<FSarkoItemStack>& GetSlots() const`, `int32 AddItem(FName, int32)`, `void ClearOnDeath()`, `int32 GetUsedSlots() const`, `int32 GetSlotLimit() const`
  - `ASarkoCharacter::BackpackComponent` (a `TObjectPtr<USarkoBackpackComponent>`)
  - `USarkoRaidSettings::BackpackSlots` (`int32`, default 12)

- [ ] **Step 1: Write the failing backpack tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (add `#include "Loot/SarkoBackpack.h"`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackpackStacksAndOverflows,
	"Sarko.Loot.BackpackStacksAndOverflows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackpackStacksAndOverflows::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog(); // pistol/1, ammo_9mm/60, medkit/3, scrap_metal/10, chain/1
	TArray<FSarkoItemStack> Slots;

	// Stacking: 25 rounds of ammo (stackSize 60) is one slot, not 25.
	TestEqual(TEXT("25 ammo all fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 25), 0);
	TestEqual(TEXT("and occupies one slot"), Slots.Num(), 1);
	TestEqual(TEXT("with the right quantity"), Slots[0].Quantity, 25);

	// Topping up the same stack does not open a second slot.
	TestEqual(TEXT("30 more ammo fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 30), 0);
	TestEqual(TEXT("still one slot"), Slots.Num(), 1);
	TestEqual(TEXT("55 rounds"), Slots[0].Quantity, 55);

	// Past the stack size, a second slot opens and carries the remainder.
	TestEqual(TEXT("20 more ammo fits, spilling into a new stack"),
		SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("ammo_9mm"), 20), 0);
	TestEqual(TEXT("two slots now"), Slots.Num(), 2);
	TestEqual(TEXT("the first is full"), Slots[0].Quantity, 60);
	TestEqual(TEXT("the second holds the rest"), Slots[1].Quantity, 15);

	// A non-stacking item takes one whole slot each.
	TArray<FSarkoItemStack> Pistols;
	TestEqual(TEXT("three pistols fit"), SarkoLoot::AddToBackpack(Pistols, Catalog, 12, TEXT("pistol"), 3), 0);
	TestEqual(TEXT("in three slots (stackSize 1)"), Pistols.Num(), 3);

	// Overflow: what does not fit is reported, and nothing is invented.
	TArray<FSarkoItemStack> Small;
	const int32 Leftover = SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), 5);
	TestEqual(TEXT("only two pistols fit in two slots"), Small.Num(), 2);
	TestEqual(TEXT("three are reported as leftover"), Leftover, 3);

	// Spec §4.3: overflow stays in the container, so the caller must be able to
	// tell exactly how much it kept. Silently dropping the remainder would look
	// identical to a full transfer and lose loot without a word.
	int32 Total = 0;
	for (const FSarkoItemStack& Stack : Small)
	{
		Total += Stack.Quantity;
	}
	TestEqual(TEXT("what fit plus what did not equals what was offered"), Total + Leftover, 5);

	// A quantity that is zero or negative changes nothing, and an unknown item
	// is refused whole rather than added with a guessed stack size.
	TestEqual(TEXT("zero quantity is a no-op"), SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), 0), 0);
	TestEqual(TEXT("negative quantity is a no-op"), SarkoLoot::AddToBackpack(Small, Catalog, 2, TEXT("pistol"), -4), 0);
	TestEqual(TEXT("an unknown item is refused entirely"),
		SarkoLoot::AddToBackpack(Small, Catalog, 12, TEXT("plutonium"), 3), 3);
	TestEqual(TEXT("and did not touch the slots"), Small.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackpackFillsPartialStacksBeforeOpeningSlots,
	"Sarko.Loot.BackpackFillsPartialStacksBeforeOpeningSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackpackFillsPartialStacksBeforeOpeningSlots::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog Catalog = FixtureCatalog();

	// Two partial medkit stacks (stackSize 3) plus one more medkit must top up
	// an existing stack rather than open a third slot — otherwise a 12-slot
	// backpack fills up with half-empty stacks and the limit means nothing.
	TArray<FSarkoItemStack> Slots;
	Slots.Add(FSarkoItemStack{ TEXT("medkit"), 1 });
	Slots.Add(FSarkoItemStack{ TEXT("scrap_metal"), 2 });

	TestEqual(TEXT("one medkit fits"), SarkoLoot::AddToBackpack(Slots, Catalog, 12, TEXT("medkit"), 1), 0);
	TestEqual(TEXT("no new slot was opened"), Slots.Num(), 2);
	TestEqual(TEXT("the partial medkit stack grew"), Slots[0].Quantity, 2);

	// A full backpack of partial stacks still accepts a top-up: the limit is
	// slots, not items.
	TArray<FSarkoItemStack> Full;
	Full.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 10 });
	TestEqual(TEXT("a one-slot backpack still accepts more ammo"),
		SarkoLoot::AddToBackpack(Full, Catalog, 1, TEXT("ammo_9mm"), 40), 0);
	TestEqual(TEXT("still one slot"), Full.Num(), 1);
	TestEqual(TEXT("50 rounds"), Full[0].Quantity, 50);
	// But it refuses a different item, because that would need a second slot.
	TestEqual(TEXT("a one-slot backpack refuses a different item"),
		SarkoLoot::AddToBackpack(Full, Catalog, 1, TEXT("medkit"), 1), 1);
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `BUILD FAILED` — `'Loot/SarkoBackpack.h' file not found`.

- [ ] **Step 3: Write the header**

`SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoBackpack.generated.h"

namespace SarkoLoot
{
	/**
	 * Adds Quantity of Item to Slots, stacking by the catalog's stackSize, and
	 * returns how much did **not** fit.
	 *
	 * Pure: it takes the slots by reference and the limit as an argument rather
	 * than reading a component or the settings, so the whole stacking rule — the
	 * thing that decides whether a haul survives a raid — is unit tested with no
	 * world, no actor and no config.
	 *
	 * Partial stacks are topped up before a new slot is opened, or a 12-slot
	 * backpack fills with half-empty stacks and the limit stops meaning
	 * anything. An unknown item is refused whole: guessing a stack size would
	 * put an id the backend rejects into a raid result.
	 */
	int32 AddToBackpack(TArray<FSarkoItemStack>& Slots, const FSarkoItemCatalog& Catalog,
		int32 SlotLimit, FName Item, int32 Quantity);
}

/**
 * What the player is carrying right now.
 *
 * Replicated **owner-only** (spec §6.1): another player's haul is exactly the
 * information that makes them worth killing, and in a PvP slice it must not be
 * on the wire at all. It is also why this is a component on the pawn rather
 * than state on the game state, which replicates to everyone.
 *
 * Cleared on death by the server, so a dead player's loot is gone before any
 * result is submitted — the loss is real, not cosmetic (spec §4.4).
 */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoBackpackComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FSarkoItemStack>& GetSlots() const { return Slots; }

	int32 GetUsedSlots() const { return Slots.Num(); }

	/** Reads USarkoRaidSettings::BackpackSlots. */
	int32 GetSlotLimit() const;

	/**
	 * Server only. Returns the quantity that did not fit, which the caller
	 * leaves in the container (spec §4.3: partial loot is allowed).
	 */
	int32 AddItem(FName Item, int32 Quantity);

	/** Server only. Everything carried is lost. */
	void ClearOnDeath();

	/** Test seam: sets a known state without a world or a replication cycle. */
	void SetSlotsForTest(const TArray<FSarkoItemStack>& NewSlots) { Slots = NewSlots; }

private:
	/**
	 * COND_OwnerOnly. A replicated UPROPERTY that is not registered in
	 * GetLifetimeReplicatedProps silently never replicates, and nothing in a
	 * standalone test would notice, because the server is also the only client.
	 */
	UPROPERTY(Replicated)
	TArray<FSarkoItemStack> Slots;
};
```

- [ ] **Step 4: Write the implementation**

`SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.cpp`:

```cpp
#include "Loot/SarkoBackpack.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/UnrealNetwork.h"

int32 SarkoLoot::AddToBackpack(TArray<FSarkoItemStack>& Slots, const FSarkoItemCatalog& Catalog,
	int32 SlotLimit, FName Item, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return 0;
	}

	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		// Refused whole. The alternative — a guessed stack size — puts an id the
		// backend will reject into the raid result, and the whole haul dies with it.
		return Quantity;
	}

	const int32 StackSize = FMath::Max(1, Def->StackSize);
	int32 Remaining = Quantity;

	// Top up existing partial stacks first.
	for (FSarkoItemStack& Stack : Slots)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Stack.Item != Item || Stack.Quantity >= StackSize)
		{
			continue;
		}
		const int32 Room = StackSize - Stack.Quantity;
		const int32 Moved = FMath::Min(Room, Remaining);
		Stack.Quantity += Moved;
		Remaining -= Moved;
	}

	// Then open new slots, while there are slots to open.
	while (Remaining > 0 && Slots.Num() < FMath::Max(0, SlotLimit))
	{
		const int32 Moved = FMath::Min(StackSize, Remaining);
		Slots.Add(FSarkoItemStack{ Item, Moved });
		Remaining -= Moved;
	}

	return Remaining;
}

USarkoBackpackComponent::USarkoBackpackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Without this the component itself is never considered for replication and
	// the Slots registration below has nothing to run on.
	SetIsReplicatedByDefault(true);
}

void USarkoBackpackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Owner-only: a haul on the wire is a target list for everyone else.
	DOREPLIFETIME_CONDITION(USarkoBackpackComponent, Slots, COND_OwnerOnly);
}

int32 USarkoBackpackComponent::GetSlotLimit() const
{
	return FMath::Max(1, GetDefault<USarkoRaidSettings>()->BackpackSlots);
}

int32 USarkoBackpackComponent::AddItem(FName Item, int32 Quantity)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// A client calling this would change nothing on the server and then be
		// corrected by the next replication — a confusing flicker rather than a
		// cheat, but still a bug worth refusing loudly.
		UE_LOG(LogTemp, Warning, TEXT("SarkoBackpack: AddItem called without authority; ignored"));
		return Quantity;
	}
	return SarkoLoot::AddToBackpack(Slots, SarkoLoot::GetItemCatalog(), GetSlotLimit(), Item, Quantity);
}

void USarkoBackpackComponent::ClearOnDeath()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	// Empty, not "marked lost": the server submits the raid result from this
	// array, so the only way loot cannot be credited is for it not to be here.
	Slots.Reset();
}
```

- [ ] **Step 5: Add the setting**

In `Core/SarkoRaidSettings.h`, in a new `Loot` category:

```cpp
	/**
	 * In-raid backpack size in slots (spec §4.4). Items stack within a slot by
	 * their catalog stackSize, so this is a decision about how many *kinds* of
	 * thing a raid can bring home — the greed dial. The backend's
	 * domain.MaxRaidStacks mirrors it as a plausibility cap, so raising it here
	 * without raising it there makes full hauls get rejected at result time.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	int32 BackpackSlots = 12;
```

and in `Config/DefaultGame.ini` under `[/Script/SarkoGame.SarkoRaidSettings]`:

```ini
BackpackSlots=12
```

- [ ] **Step 6: Give the character a backpack**

In `Pawn/SarkoCharacter.h`, next to `WeaponComponent`:

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USarkoBackpackComponent> BackpackComponent;
```

with `class USarkoBackpackComponent;` added to the forward declarations at the top (global scope, alongside `class USarkoWeaponComponent;`).

In `Pawn/SarkoCharacter.cpp`, add `#include "Loot/SarkoBackpack.h"`, create the component in the constructor next to the existing ones:

```cpp
	BackpackComponent = CreateDefaultSubobject<USarkoBackpackComponent>(TEXT("Backpack"));
```

and empty it in `HandleDeath`, before anything else in that function:

```cpp
	// Spec §4.4: died means the haul is gone. This runs before the game mode is
	// told, so by the time a result is submitted there is nothing to credit.
	if (BackpackComponent)
	{
		BackpackComponent->ClearOnDeath();
	}
```

- [ ] **Step 7: Draw the backpack counter**

In `UI/SarkoHUD.h`, declare `void DrawBackpack();` alongside the other private draw functions, and call it from `DrawHUD` after `DrawAmmo()`.

In `UI/SarkoHUD.cpp`, add `#include "Loot/SarkoBackpack.h"` and:

```cpp
void ASarkoHUD::DrawBackpack()
{
	// Top-left, immediately right of the ammo readout: spec §9 puts every
	// number along the top, and the bottom corners belong to the thumbs.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->BackpackComponent)
	{
		return;
	}

	const USarkoBackpackComponent* Backpack = Pawn->BackpackComponent;
	const int32 Used = Backpack->GetUsedSlots();
	const int32 Limit = Backpack->GetSlotLimit();
	const FString Text = FString::Printf(TEXT("%d/%d"), Used, Limit);

	// Amber when full, so "the crate had more in it" is legible at a glance
	// rather than being discovered by counting.
	const FLinearColor Colour = Used >= Limit ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;
	DrawText(Text, Colour, 140.f, 24.f, GEngine->GetLargeFont(), 1.f);
}
```

- [ ] **Step 8: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Loot`
Expected: `10 test(s) performed, 0 failed` — the 8 from Tasks 1 and 3 plus `BackpackStacksAndOverflows` and `BackpackFillsPartialStacksBeforeOpeningSlots`.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `37 test(s) performed, 0 failed`.

```bash
git add SarkoGame && git commit -m "feat(game): 12-slot owner-only replicated backpack"
```

---

### Task 5: Interactive loot containers

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoLootContainer.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h`, `.cpp` (marker boxes become real actors)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (`LootedContainers`, the registry)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp` (the channel and its RPCs)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp` (interact polling, `E`, the touch button)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp` (prompt, progress bar, interact button)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, `SarkoGame/Config/DefaultGame.ini`
- Create: `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp` (interaction gate; dwell lands in Task 6)

**Interfaces:**
- Consumes: `FSarkoMapDefinition`, `FSarkoLootContainerSpot`, `SarkoMap::SpawnMeshBox`'s mobility dance, `SarkoLoot::RollContainer`, `SarkoLoot::ContainerSeed`, `SarkoLoot::GetLootTables`, `USarkoBackpackComponent::AddItem`, `ASarkoRaidGameState::Seed`.
- Produces:
  - `ASarkoLootContainer` with `int32 ContainerIndex`, `FName Tier`, `bool IsLooted() const`, `void RefreshVisualState()`
  - `void SarkoMap::SpawnLootContainers(UWorld&, const FSarkoMapDefinition&)`
  - `bool SarkoLoot::CanInteract(const FVector& PawnLocation, const FVector& ContainerLocation, float RadiusUU, bool bPawnAlive, bool bAlreadyLooted)`
  - `FBox2D SarkoInput::InteractButtonRect(FVector2D ViewportSize)`
  - `ASarkoRaidGameState::IsContainerLooted(int32) / MarkContainerLooted(int32) / RegisterContainer(ASarkoLootContainer*) / SizeLootState(int32)`
  - `ASarkoCharacter::BeginLootChannel(int32) / CancelLootChannel() / GetLootChannelIndex() / GetLootChannelElapsed()`
  - `USarkoRaidSettings::InteractRadiusUU` (250), `LootChannelSeconds` (1.5)

- [ ] **Step 1: Write the failing interaction-gate tests**

Create `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Loot/SarkoLootContainer.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteractGateIsServerSide,
	"Sarko.Loot.InteractGateIsServerSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteractGateIsServerSide::RunTest(const FString& Parameters)
{
	const FVector Pawn(0.f, 0.f, 100.f);
	const FVector Near(200.f, 0.f, 35.f);
	const FVector Far(900.f, 0.f, 35.f);
	constexpr float Radius = 250.f;

	TestTrue(TEXT("a live pawn beside an unlooted container may interact"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, /*bAlive*/ true, /*bLooted*/ false));

	// Every one of these is a thing a hostile client will claim.
	TestFalse(TEXT("distance is enforced"),
		SarkoLoot::CanInteract(Pawn, Far, Radius, true, false));
	TestFalse(TEXT("a looted container cannot be looted twice"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, true, true));
	TestFalse(TEXT("a corpse cannot loot"),
		SarkoLoot::CanInteract(Pawn, Near, Radius, false, false));

	// Height is ignored: the container sits on the ground and the pawn's origin
	// is at capsule centre, so a 3D distance check would make a crate at the
	// player's feet unreachable at the edge of the radius. Planar distance is
	// what the player sees on a top-down camera.
	const FVector Above(200.f, 0.f, 900.f);
	TestTrue(TEXT("vertical separation does not block interaction"),
		SarkoLoot::CanInteract(Pawn, Above, Radius, true, false));

	// Exactly at the radius counts as inside, so a value tuned to 250 does not
	// behave like 249.99 on one machine and 250.01 on another.
	const FVector Exactly(Radius, 0.f, 35.f);
	TestTrue(TEXT("the radius boundary is inclusive"),
		SarkoLoot::CanInteract(Pawn, Exactly, Radius, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteractButtonAvoidsTheThumbs,
	"Sarko.Input.InteractButtonAvoidsTheThumbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteractButtonAvoidsTheThumbs::RunTest(const FString& Parameters)
{
	// Landscape iPhone, and a small window, because the rect is computed from
	// the viewport and a fixed pixel offset would leave the screen on one of them.
	for (const FVector2D Viewport : { FVector2D(2532.f, 1170.f), FVector2D(1280.f, 720.f) })
	{
		const FBox2D Rect = SarkoInput::InteractButtonRect(Viewport);

		TestTrue(TEXT("the button is on screen"),
			Rect.Min.X >= 0.f && Rect.Min.Y >= 0.f && Rect.Max.X <= Viewport.X && Rect.Max.Y <= Viewport.Y);
		TestTrue(TEXT("the button is big enough for a thumb (>= 88 px)"),
			Rect.GetSize().X >= 88.f && Rect.GetSize().Y >= 88.f);

		// Spec §9: the bottom corners are covered by the thumbs that drive the
		// sticks. A button there is a button that fights the controls.
		const float BottomBandY = Viewport.Y * 0.75f;
		const float LeftBandX = Viewport.X * 0.25f;
		const float RightBandX = Viewport.X * 0.75f;
		const bool bInBottomLeft = Rect.Min.Y > BottomBandY && Rect.Min.X < LeftBandX;
		const bool bInBottomRight = Rect.Min.Y > BottomBandY && Rect.Max.X > RightBandX;
		TestFalse(TEXT("the button is not in the bottom-left thumb zone"), bInBottomLeft);
		TestFalse(TEXT("the button is not in the bottom-right thumb zone"), bInBottomRight);

		// Reachable: a button pinned to the very top edge cannot be pressed
		// without letting go of a stick, which is the whole problem it solves.
		TestTrue(TEXT("the button sits in the vertical centre band, not against an edge"),
			Rect.GetCenter().Y > Viewport.Y * 0.3f && Rect.GetCenter().Y < Viewport.Y * 0.7f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko`
Expected: `BUILD FAILED` — `'Loot/SarkoLootContainer.h' file not found`.

- [ ] **Step 3: Write the container header**

`SarkoGame/Source/SarkoGame/Loot/SarkoLootContainer.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SarkoLootContainer.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace SarkoLoot
{
	/**
	 * Whether this pawn may open this container, right now.
	 *
	 * Pure and shared: the client calls it to decide whether to draw a prompt,
	 * and the server calls it — with its own copy of the pawn's location — to
	 * decide whether to honour the RPC. Both must agree on the rule, and only
	 * the server's answer counts.
	 *
	 * Distance is planar. The container sits on the ground while the pawn's
	 * origin is at capsule centre, so a 3D check would make a crate at the
	 * player's feet unreachable at the edge of the radius, which reads as the
	 * prompt flickering for no reason.
	 */
	bool CanInteract(const FVector& PawnLocation, const FVector& ContainerLocation,
		float RadiusUU, bool bPawnAlive, bool bAlreadyLooted);
}

/**
 * One lootable container.
 *
 * **Not a replicated actor.** Every machine spawns its own identical set from
 * Data/Maps/bridge.json, exactly as it does for cover blocks: the map never
 * crosses the network (see ASarkoRaidGameState's comment), and a locally-spawned
 * actor has no net identity to replicate a property through even if it wanted
 * one. The single mutable fact — which indices have been emptied — lives on the
 * game state as ASarkoRaidGameState::LootedContainers and replicates from there.
 *
 * ContainerIndex is the index into FSarkoMapDefinition::Containers. It is the
 * name of this container on the wire, the loot roll's stream salt, and the only
 * thing a client sends when it asks to open something — so every use of it is
 * bounds-checked.
 */
UCLASS()
class ASarkoLootContainer : public AActor
{
	GENERATED_BODY()

public:
	ASarkoLootContainer();

	virtual void BeginPlay() override;

	/** Index into the map definition's Containers array. Set at spawn, never changed. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 ContainerIndex = INDEX_NONE;

	/** Loot table tier, copied from the map definition. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FName Tier;

	/** Reads the replicated bit off the game state — never a local guess. */
	bool IsLooted() const;

	/** Recolours the lid to match IsLooted(). Called at spawn and whenever the replicated state changes. */
	void RefreshVisualState();

	void SetupFromSpot(int32 InIndex, FName InTier);

private:
	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Lid;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LidMaterial;
};
```

- [ ] **Step 4: Write the container implementation**

`SarkoGame/Source/SarkoGame/Loot/SarkoLootContainer.cpp`:

```cpp
#include "Loot/SarkoLootContainer.h"

#include "Components/StaticMeshComponent.h"
#include "Core/SarkoRaidGameState.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

bool SarkoLoot::CanInteract(const FVector& PawnLocation, const FVector& ContainerLocation,
	float RadiusUU, bool bPawnAlive, bool bAlreadyLooted)
{
	if (!bPawnAlive || bAlreadyLooted)
	{
		return false;
	}
	const FVector2D Flat(PawnLocation.X - ContainerLocation.X, PawnLocation.Y - ContainerLocation.Y);
	// <=, so a radius tuned to 250 does not behave like 249.99 on one machine.
	return Flat.SizeSquared() <= RadiusUU * RadiusUU;
}

namespace
{
	/** Closed: rusted olive. Emptied: washed out, so a cleared route reads at a glance. */
	const FLinearColor ClosedTint(0.28f, 0.30f, 0.16f);
	const FLinearColor LootedTint(0.42f, 0.42f, 0.44f);

	/** Chest-height crate: findable from above, low enough not to hide anything. */
	constexpr float BodyHalfWidth = 45.f;
	constexpr float BodyHalfHeight = 32.f;
	constexpr float LidHalfHeight = 8.f;
}

ASarkoLootContainer::ASarkoLootContainer()
{
	PrimaryActorTick.bCanEverTick = false;
	// Not replicated: every machine spawns its own from the map file. See the
	// class comment; making this true would create a second, competing copy on
	// every client.
	bReplicates = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(Body);
}

void ASarkoLootContainer::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cube || !BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoLootContainer %d: engine cube or basic material missing; container is invisible"),
			ContainerIndex);
		return;
	}

	// Movable -> assign -> Static, in that order. SetStaticMesh silently
	// no-ops on a Static component once the world has begun play, and this
	// actor is spawned well after BeginPlay; getting the order wrong leaves a
	// container with no mesh and no collision, which looks exactly like a
	// container that was never spawned.
	const auto Build = [Cube, BaseMaterial](UStaticMeshComponent& Component, const FVector& Extent, const FVector& Offset)
	{
		Component.SetMobility(EComponentMobility::Movable);
		Component.SetStaticMesh(Cube);
		Component.SetRelativeLocation(Offset);
		// The engine cube is 100 uu across, so scale is extent/50 per axis.
		Component.SetWorldScale3D(Extent / 50.f);
		Component.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component.SetMobility(EComponentMobility::Static);
	};

	Build(*Body, FVector(BodyHalfWidth, BodyHalfWidth, BodyHalfHeight), FVector::ZeroVector);
	Build(*Lid, FVector(BodyHalfWidth * 1.05f, BodyHalfWidth * 1.05f, LidHalfHeight),
		FVector(0.f, 0.f, BodyHalfHeight + LidHalfHeight));

	// The lid is the state indicator, so it gets the dynamic material. Same
	// BasicShapeMaterial trick SarkoBody uses; the parameter is called "Color"
	// in current engine versions and "BaseColor" in older copies, so both are set.
	LidMaterial = Lid->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMaterial);

	if (ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr)
	{
		// Registering rather than ticking: the state changes a few dozen times
		// per raid, so a per-frame poll on 42 actors would be pure waste.
		RaidState->RegisterContainer(this);
	}
	RefreshVisualState();
}

void ASarkoLootContainer::SetupFromSpot(int32 InIndex, FName InTier)
{
	ContainerIndex = InIndex;
	Tier = InTier;
}

bool ASarkoLootContainer::IsLooted() const
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	return RaidState && RaidState->IsContainerLooted(ContainerIndex);
}

void ASarkoLootContainer::RefreshVisualState()
{
	if (!LidMaterial)
	{
		return;
	}
	const FLinearColor Tint = IsLooted() ? LootedTint : ClosedTint;
	LidMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
	LidMaterial->SetVectorParameterValue(TEXT("BaseColor"), Tint);
}
```

- [ ] **Step 5: Hold the looted state on the game state**

In `Core/SarkoRaidGameState.h`, add `class ASarkoLootContainer;` to the global-scope forward declarations (next to `struct FSarkoMapDefinition;`) and:

```cpp
	/**
	 * One byte per container index: 1 means emptied. This is the only loot state
	 * on the wire.
	 *
	 * Spec §4.3 asks for a replicated `bLooted` on the container. A container is
	 * spawned locally on every machine from the map file — like every cover
	 * block — so it has no net identity and cannot replicate a property of its
	 * own; this array is the same fact, owned by the one actor that does
	 * replicate. A byte array rather than a bitmask because 42 bytes is nothing
	 * and a bitmask is a debugging tax.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_LootedContainers)
	TArray<uint8> LootedContainers;

	UFUNCTION()
	void OnRep_LootedContainers();

	/** Server only: sizes the array to the map's container count. Idempotent. */
	void SizeLootState(int32 ContainerCount);

	/** False for an out-of-range index — a client-supplied index is never trusted to be in range. */
	bool IsContainerLooted(int32 ContainerIndex) const;

	/** Server only. Bounds-checked; logs and does nothing for a bad index. */
	void MarkContainerLooted(int32 ContainerIndex);

	/** Containers register at BeginPlay so a replicated change can recolour them without anything ticking. */
	void RegisterContainer(ASarkoLootContainer* Container);

private:
	/** Weak: containers are destroyed with the world and this must not keep them alive. */
	TArray<TWeakObjectPtr<ASarkoLootContainer>> RegisteredContainers;
```

(keep the existing `private:` members below; do not create two private sections in a way that hides them).

In `Core/SarkoRaidGameState.cpp`, add `#include "Loot/SarkoLootContainer.h"`, register the property and implement:

```cpp
	// In GetLifetimeReplicatedProps, alongside RemainingSeconds and Seed:
	DOREPLIFETIME(ASarkoRaidGameState, LootedContainers);
```

```cpp
void ASarkoRaidGameState::SizeLootState(int32 ContainerCount)
{
	if (!HasAuthority())
	{
		return;
	}
	if (LootedContainers.Num() != ContainerCount)
	{
		LootedContainers.SetNumZeroed(FMath::Max(0, ContainerCount));
	}
}

bool ASarkoRaidGameState::IsContainerLooted(int32 ContainerIndex) const
{
	// Out of range reads as "not looted" rather than asserting: on a client the
	// array can legitimately arrive a frame after the containers spawn.
	return LootedContainers.IsValidIndex(ContainerIndex) && LootedContainers[ContainerIndex] != 0;
}

void ASarkoRaidGameState::MarkContainerLooted(int32 ContainerIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!LootedContainers.IsValidIndex(ContainerIndex))
	{
		// A client-supplied index that got this far is either a stale map or a
		// forged RPC. Loud, and nothing happens.
		UE_LOG(LogTemp, Warning, TEXT("SarkoRaidGameState: container index %d is out of range (%d containers)"),
			ContainerIndex, LootedContainers.Num());
		return;
	}
	LootedContainers[ContainerIndex] = 1;

	// The server never receives its own OnRep, so it refreshes explicitly.
	OnRep_LootedContainers();
}

void ASarkoRaidGameState::OnRep_LootedContainers()
{
	for (int32 Index = RegisteredContainers.Num() - 1; Index >= 0; --Index)
	{
		if (ASarkoLootContainer* Container = RegisteredContainers[Index].Get())
		{
			Container->RefreshVisualState();
		}
		else
		{
			RegisteredContainers.RemoveAtSwap(Index);
		}
	}
}

void ASarkoRaidGameState::RegisterContainer(ASarkoLootContainer* Container)
{
	if (Container)
	{
		RegisteredContainers.AddUnique(Container);
	}
}
```

In `SpawnPrebuiltLayout`, after `SarkoMap::SpawnProps`, add:

```cpp
	// Containers are actors now, not marker boxes, and they spawn here so both
	// the server and each client build the same set from the same file.
	SarkoMap::SpawnLootContainers(*World, InDefinition);
	SizeLootState(InDefinition.Containers.Num());
```

- [ ] **Step 6: Spawn containers as actors**

In `Map/SarkoMapBuilder.h`:

```cpp
	/**
	 * Spawns one ASarkoLootContainer per definition entry, in array order, so
	 * ContainerIndex means the same thing on every machine. Deterministic and
	 * local — nothing here replicates.
	 */
	void SpawnLootContainers(UWorld& World, const struct FSarkoMapDefinition& Definition);
```

(the `struct FSarkoMapDefinition` here is inside `namespace SarkoMap`, which is exactly the elaborated-type trap — the file already forward-declares `FSarkoMapDefinition` at global scope for that reason. **Reuse that declaration and write the parameter as `const FSarkoMapDefinition&`.**)

In `Map/SarkoMapBuilder.cpp`, add `#include "Loot/SarkoLootContainer.h"`, **delete** the container-marker block from `SpawnProps` (the `CrateMesh` loop and its comment), adjust that function's closing log line to mention props only, and add:

```cpp
void SarkoMap::SpawnLootContainers(UWorld& World, const FSarkoMapDefinition& Definition)
{
	for (int32 Index = 0; Index < Definition.Containers.Num(); ++Index)
	{
		const FSarkoLootContainerSpot& Spot = Definition.Containers[Index];

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ASarkoLootContainer* Container = World.SpawnActor<ASarkoLootContainer>(
			ASarkoLootContainer::StaticClass(), Spot.Location, FRotator::ZeroRotator, Params);
		if (!Container)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn container %d at %s"), Index, *Spot.Location.ToString());
			continue;
		}
		// Set before BeginPlay would have been nicer, but SpawnActor runs it;
		// RefreshVisualState is called again from the game state's OnRep, so a
		// container that learns its index a moment late still ends up correct.
		Container->SetupFromSpot(Index, Spot.Tier);
		Container->RefreshVisualState();
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d loot containers"), Definition.Containers.Num());
}
```

- [ ] **Step 7: Put the channel on the character**

In `Pawn/SarkoCharacter.h`, public:

```cpp
	/** Client intent: begin opening the container at this index. Validated server-side. */
	void RequestBeginLoot(int32 ContainerIndex);

	/** Client intent: stop opening. Also called automatically when the pawn walks away or dies. */
	void RequestCancelLoot();

	/** INDEX_NONE when not channelling. Server truth; the client keeps its own cosmetic copy. */
	int32 GetLootChannelIndex() const { return LootChannelIndex; }

	/** Seconds the channel has been running, or 0. Used by the HUD for the progress bar. */
	float GetLootChannelElapsed() const;
```

private:

```cpp
	/**
	 * Server RPC. Reliable: a dropped begin would leave a player holding the
	 * button with nothing happening, which reads as a broken container.
	 *
	 * ContainerIndex is hostile input — the server bounds-checks it against its
	 * own map definition and re-measures the distance from its own copy of this
	 * pawn, exactly as ServerRequestFire re-derives the muzzle origin.
	 */
	UFUNCTION(Server, Reliable)
	void ServerBeginLoot(int32 ContainerIndex);

	UFUNCTION(Server, Reliable)
	void ServerCancelLoot();

	/** Server: completes the channel, rolls and transfers. Called from Tick. */
	void TickLootChannel();

	/** Server-side channel state. Not replicated: the HUD's bar is local and cosmetic. */
	int32 LootChannelIndex = INDEX_NONE;
	float LootChannelStartSeconds = 0.f;

	/** The client's own copy, so the bar moves without waiting for a round trip. */
	int32 LocalChannelIndex = INDEX_NONE;
	float LocalChannelStartSeconds = 0.f;
```

In `Pawn/SarkoCharacter.cpp`, add the includes (`Core/SarkoRaidGameMode.h`, `Core/SarkoRaidGameState.h`, `Core/SarkoRaidSettings.h`, `Loot/SarkoBackpack.h`, `Loot/SarkoLootContainer.h`, `Loot/SarkoLootTable.h`, `Map/SarkoMapDefinition.h`) and:

```cpp
void ASarkoCharacter::RequestBeginLoot(int32 ContainerIndex)
{
	// Local copy first, so the progress bar starts moving this frame rather than
	// after a round trip — the same reason a shot is drawn before the server
	// confirms it (spec §10).
	LocalChannelIndex = ContainerIndex;
	LocalChannelStartSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (HasAuthority())
	{
		ServerBeginLoot_Implementation(ContainerIndex);
	}
	else
	{
		ServerBeginLoot(ContainerIndex);
	}
}

void ASarkoCharacter::RequestCancelLoot()
{
	LocalChannelIndex = INDEX_NONE;
	if (HasAuthority())
	{
		ServerCancelLoot_Implementation();
	}
	else
	{
		ServerCancelLoot();
	}
}

float ASarkoCharacter::GetLootChannelElapsed() const
{
	const int32 Index = HasAuthority() ? LootChannelIndex : LocalChannelIndex;
	const float Start = HasAuthority() ? LootChannelStartSeconds : LocalChannelStartSeconds;
	if (Index == INDEX_NONE || !GetWorld())
	{
		return 0.f;
	}
	return FMath::Max(0.f, GetWorld()->GetTimeSeconds() - Start);
}

void ASarkoCharacter::ServerBeginLoot_Implementation(int32 ContainerIndex)
{
	const ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState)
	{
		return;
	}

	// Bounds check before the index is used for anything at all.
	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: loot request for out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive, RaidState->IsContainerLooted(ContainerIndex)))
	{
		return;
	}

	LootChannelIndex = ContainerIndex;
	LootChannelStartSeconds = GetWorld()->GetTimeSeconds();
}

void ASarkoCharacter::ServerCancelLoot_Implementation()
{
	LootChannelIndex = INDEX_NONE;
}

void ASarkoCharacter::TickLootChannel()
{
	if (LootChannelIndex == INDEX_NONE)
	{
		return;
	}

	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState)
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(LootChannelIndex))
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const bool bAlive = HealthComponent && !HealthComponent->IsDead();

	// Re-checked every tick, not only at the start: walking away or dying
	// mid-channel must cancel it, and both are things the server sees first.
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[LootChannelIndex].Location,
			Settings.InteractRadiusUU, bAlive, RaidState->IsContainerLooted(LootChannelIndex)))
	{
		LootChannelIndex = INDEX_NONE;
		return;
	}

	if (GetWorld()->GetTimeSeconds() - LootChannelStartSeconds < Settings.LootChannelSeconds)
	{
		return;
	}

	// Channel complete. Roll here and now — never ahead of time: pre-rolled
	// contents replicate and can be read out of memory, which is a loot map
	// (slice-1 spec §6.1).
	const int32 Index = LootChannelIndex;
	LootChannelIndex = INDEX_NONE;

	const FSarkoLootTable* Table = SarkoLoot::GetLootTables().Find(Spots[Index].Tier);
	if (!Table)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoCharacter: container %d has tier '%s' with no loot table"),
			Index, *Spots[Index].Tier.ToString());
		RaidState->MarkContainerLooted(Index);
		return;
	}

	FRandomStream Stream(SarkoLoot::ContainerSeed(RaidState->Seed, Index));
	const TArray<FSarkoItemStack> Rolled = SarkoLoot::RollContainer(*Table, Stream);

	int32 Taken = 0;
	int32 LeftBehind = 0;
	for (const FSarkoItemStack& Stack : Rolled)
	{
		const int32 Leftover = BackpackComponent ? BackpackComponent->AddItem(Stack.Item, Stack.Quantity) : Stack.Quantity;
		Taken += Stack.Quantity - Leftover;
		LeftBehind += Leftover;
	}

	// Marked looted either way. Spec §4.3 allows partial loot, and re-opening a
	// container to fish out the remainder would re-run the roll and duplicate
	// the part already taken — the same roll, credited twice.
	RaidState->MarkContainerLooted(Index);

	UE_LOG(LogTemp, Display, TEXT("SarkoCharacter: looted container %d (tier %s): took %d units, left %d behind"),
		Index, *Spots[Index].Tier.ToString(), Taken, LeftBehind);
}
```

and call it from the existing `Tick`, server side only:

```cpp
	if (HasAuthority())
	{
		TickLootChannel();
	}
```

In `HandleDeath`, cancel the channel alongside clearing the backpack:

```cpp
	LootChannelIndex = INDEX_NONE;
```

- [ ] **Step 8: Poll the interact input**

In `Core/SarkoPlayerController.h`, add to `namespace SarkoInput`:

```cpp
	/**
	 * Where the on-screen interact button lives, in viewport pixels.
	 *
	 * Right-hand side, vertically centred: the bottom corners are covered by
	 * the thumbs driving the sticks (spec §9), and the very top cannot be
	 * reached without letting go of one. Computed from the viewport rather than
	 * fixed, so it stays on screen on a phone and in a small desktop window.
	 */
	FBox2D InteractButtonRect(FVector2D ViewportSize);
```

and to the class, public:

```cpp
	/** The container the pawn could open right now, or nullptr. The HUD reads this to draw the prompt. */
	class ASarkoLootContainer* GetInteractTarget() const { return InteractTarget.Get(); }

	/** True while the player is holding interact. The HUD reads this to draw the progress bar. */
	bool IsInteractHeld() const { return bInteractHeld; }
```

private:

```cpp
	/** Finds the nearest openable container and turns held input into channel start/stop. */
	void UpdateInteract();

	/** Cached once; containers never move and never change count during a raid. */
	TArray<TWeakObjectPtr<class ASarkoLootContainer>> CachedContainers;
	bool bContainersCached = false;

	TWeakObjectPtr<class ASarkoLootContainer> InteractTarget;
	bool bInteractHeld = false;

	/** Which touch slot is holding the interact button, or INDEX_NONE. Claimed before stick classification. */
	int32 InteractTouchIndex = INDEX_NONE;
```

In `Core/SarkoPlayerController.cpp`:

```cpp
FBox2D SarkoInput::InteractButtonRect(FVector2D ViewportSize)
{
	// Scaled to the shorter axis so the button is the same physical size on a
	// phone and in a window, with a floor so it never drops below a thumb.
	const float Size = FMath::Max(96.f, FMath::Min(ViewportSize.X, ViewportSize.Y) * 0.14f);
	const float Right = ViewportSize.X - Size - Size * 0.35f;
	const float Top = ViewportSize.Y * 0.5f - Size * 0.5f;
	return FBox2D(FVector2D(Right, Top), FVector2D(Right + Size, Top + Size));
}
```

```cpp
void ASarkoPlayerController::UpdateInteract()
{
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		bInteractHeld = false;
		InteractTarget = nullptr;
		return;
	}

	// One-time gather. Containers are spawned from the map file at level start,
	// never added or removed, so re-running an actor iterator every frame would
	// be pure waste on a phone.
	if (!bContainersCached)
	{
		for (TActorIterator<ASarkoLootContainer> It(GetWorld()); It; ++It)
		{
			CachedContainers.Add(*It);
		}
		bContainersCached = true;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector PawnLocation = Pawn->GetActorLocation();
	const bool bAlive = Pawn->HealthComponent && !Pawn->HealthComponent->IsDead();

	// Nearest openable container. No allocation: this walks a cached array and
	// keeps one pointer and one float.
	ASarkoLootContainer* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ASarkoLootContainer>& Weak : CachedContainers)
	{
		ASarkoLootContainer* Container = Weak.Get();
		if (!Container)
		{
			continue;
		}
		if (!SarkoLoot::CanInteract(PawnLocation, Container->GetActorLocation(),
				Settings.InteractRadiusUU, bAlive, Container->IsLooted()))
		{
			continue;
		}
		const float DistanceSquared = FVector2D(
			PawnLocation.X - Container->GetActorLocation().X,
			PawnLocation.Y - Container->GetActorLocation().Y).SizeSquared();
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Container;
		}
	}
	InteractTarget = Best;

	// Held state: the touch button, or E on a desktop. E exists because that is
	// how this game is actually played during development, and a mouse cannot
	// hold a virtual button and a movement stick at once.
	bool bHeld = InteractTouchIndex != INDEX_NONE;
#if !UE_BUILD_SHIPPING
	bHeld = bHeld || IsInputKeyDown(EKeys::E);
#endif

	if (bHeld && Best && !bInteractHeld)
	{
		Pawn->RequestBeginLoot(Best->ContainerIndex);
	}
	else if (!bHeld && bInteractHeld)
	{
		Pawn->RequestCancelLoot();
	}
	else if (bHeld && !Best && bInteractHeld)
	{
		// Walked away while holding.
		Pawn->RequestCancelLoot();
	}
	bInteractHeld = bHeld && Best != nullptr;
}
```

with `#include "EngineUtils.h"`, `#include "Loot/SarkoLootContainer.h"` and `#include "Pawn/SarkoHealthComponent.h"` added, and `UpdateInteract()` called from `PlayerTick` after `UpdateSticks()`.

In `UpdateSticks()`, claim the interact touch **before** stick classification, and skip that slot in the classification loop. Insert immediately after the viewport size is read:

```cpp
	const FBox2D InteractRect = SarkoInput::InteractButtonRect(Viewport);
	bool bInteractTouchStillDown = false;
```

then, as the first branch inside the `for (int32 Index = 0; Index < 2; ++Index)` loop (before the `MoveTouchIndex` check):

```cpp
		if (Index == InteractTouchIndex)
		{
			if (bPressed)
			{
				bInteractTouchStillDown = true;
			}
			continue;
		}
```

and inside the "a touch this controller has not seen before" block, before the left/right-half decision:

```cpp
		// The interact button wins over the aim stick for a touch that lands on
		// it. Without this, pressing it would also start an aim drag and fire a
		// shot on release — the button would shoot.
		if (InteractTouchIndex == INDEX_NONE && InteractRect.IsInside(Position))
		{
			InteractTouchIndex = Index;
			bInteractTouchStillDown = true;
			continue;
		}
```

and next to the existing stick-release handling:

```cpp
	if (!bInteractTouchStillDown)
	{
		InteractTouchIndex = INDEX_NONE;
	}
```

**Two touch slots are polled today and this adds a third consumer.** Raise the loop bound from 2 to 3 so movement, aim and interact can be held simultaneously, and say so in the comment above the loop.

- [ ] **Step 9: Draw the prompt, the bar and the button**

In `UI/SarkoHUD.h` declare `void DrawInteract();` and call it from `DrawHUD` after `DrawBackpack()`.

In `UI/SarkoHUD.cpp`, add `#include "Loot/SarkoLootContainer.h"` and:

```cpp
void ASarkoHUD::DrawInteract()
{
	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// The button is always drawn, so the player learns where it is before they
	// need it; it dims when there is nothing in reach.
	const FBox2D Rect = SarkoInput::InteractButtonRect(FVector2D(Canvas->SizeX, Canvas->SizeY));
	const ASarkoLootContainer* Target = PC->GetInteractTarget();
	const FLinearColor ButtonColour = Target
		? FLinearColor(0.95f, 0.8f, 0.25f, 0.55f)
		: FLinearColor(1.f, 1.f, 1.f, 0.15f);
	DrawRect(ButtonColour, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);

	float LabelWidth = 0.f;
	float LabelHeight = 0.f;
	GetTextSize(TEXT("E"), LabelWidth, LabelHeight, GEngine->GetLargeFont(), 1.f);
	DrawText(TEXT("E"), FLinearColor::White,
		Rect.GetCenter().X - LabelWidth * 0.5f, Rect.GetCenter().Y - LabelHeight * 0.5f,
		GEngine->GetLargeFont(), 1.f);

	if (!Target)
	{
		return;
	}

	// Prompt: top-centre, under the clock. Never a bottom corner (spec §9).
	const FString Prompt = FString::Printf(TEXT("ОБШУКАТИ (%s)"), *Target->Tier.ToString());
	float PromptWidth = 0.f;
	float PromptHeight = 0.f;
	GetTextSize(Prompt, PromptWidth, PromptHeight, GEngine->GetLargeFont(), 1.f);
	const float PromptX = (Canvas->SizeX - PromptWidth) * 0.5f;
	constexpr float PromptY = 76.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), PromptX - 10.f, PromptY - 4.f, PromptWidth + 20.f, PromptHeight + 8.f);
	DrawText(Prompt, FLinearColor::White, PromptX, PromptY, GEngine->GetLargeFont(), 1.f);

	if (!PC->IsInteractHeld())
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// Progress: local and cosmetic. The server owns whether the channel
	// completes; this bar only has to stop the player wondering whether the
	// hold is doing anything.
	const float Duration = FMath::Max(0.01f, GetDefault<USarkoRaidSettings>()->LootChannelSeconds);
	const float Fraction = FMath::Clamp(Pawn->GetLootChannelElapsed() / Duration, 0.f, 1.f);

	constexpr float BarWidth = 260.f;
	constexpr float BarHeight = 12.f;
	const float BarX = (Canvas->SizeX - BarWidth) * 0.5f;
	const float BarY = PromptY + PromptHeight + 10.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);
	DrawRect(FLinearColor(0.95f, 0.8f, 0.25f, 0.9f), BarX, BarY, BarWidth * Fraction, BarHeight);
}
```

- [ ] **Step 10: Add the settings**

In `Core/SarkoRaidSettings.h`, `Loot` category:

```cpp
	/** How close the pawn must be to open a container. Enforced on the server. */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	float InteractRadiusUU = 250.f;

	/**
	 * Press-and-hold time to open a container (spec §4.3). This is the cost of
	 * looting: standing still for a second and a half beside a crate is what
	 * makes a container a decision rather than a pickup.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	float LootChannelSeconds = 1.5f;
```

and in `Config/DefaultGame.ini`:

```ini
InteractRadiusUU=250.000000
LootChannelSeconds=1.500000
```

- [ ] **Step 11: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `39 test(s) performed, 0 failed` (37 after Task 4 plus `Sarko.Loot.InteractGateIsServerSide` and `Sarko.Input.InteractButtonAvoidsTheThumbs`).

Then play it on the desktop — this step is the point of the task and automation cannot see it:

Run: `cd SarkoGame && "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" SarkoGame.uproject /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900`

Walk to a container with WASD, hold `E`, and confirm: the prompt appears within ~250 uu, the bar fills over 1.5 s, the lid goes grey, the backpack counter climbs, and holding `E` on the same container again does nothing. Then walk away mid-hold and confirm the bar resets and nothing is granted.

```bash
git add SarkoGame && git commit -m "feat(game): interactive loot containers with a server-validated channel"
```

---

### Task 6: Extraction zones and the raid outcome

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoExtractionZone.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h`, `.cpp` (spawn the zones)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (`Outcome`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp` (dwell tick, outcome authority)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp` (replicated dwell for the HUD)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp` (freeze input once the raid is over)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp` (countdown, summary)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, `SarkoGame/Config/DefaultGame.ini`
- Modify: `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp` (extend)

**Interfaces:**
- Consumes: `FSarkoExtractionSpot`, `ASarkoCharacter`, `USarkoBackpackComponent`, `USarkoHealthComponent::OnDied`, `ASarkoRaidGameState::IsRaidOver`.
- Produces:
  - `ESarkoRaidOutcome { InProgress, Extracted, Died, MIA }`
  - `float SarkoExtract::AdvanceDwell(float CurrentSeconds, bool bInsideZone, float DeltaSeconds)`
  - `int32 SarkoExtract::FindZoneContaining(const FVector& PawnLocation, const TArray<FSarkoExtractionSpot>& Zones)`
  - `ASarkoExtractionZone` with `int32 ZoneIndex`, `FString ZoneName`, `float RadiusUU`
  - `void SarkoMap::SpawnExtractionZones(UWorld&, const FSarkoMapDefinition&)`
  - `ASarkoRaidGameState::Outcome` (replicated), `ASarkoRaidGameState::IsRaidFinished() const`
  - `ASarkoRaidGameMode::HandlePlayerDied(ASarkoCharacter*)`, `ASarkoRaidGameMode::FinishRaid(ESarkoRaidOutcome)`
  - `ASarkoCharacter::ExtractDwellSeconds` / `ExtractZoneIndex` (owner-only replicated)
  - `USarkoRaidSettings::ExtractDwellSeconds` (5)

- [ ] **Step 1: Write the failing dwell tests**

Append to `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp` (add `#include "Loot/SarkoExtractionZone.h"` and `#include "Map/SarkoMapDefinition.h"`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDwellAccumulatesAndResets,
	"Sarko.Extract.DwellAccumulatesAndResets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDwellAccumulatesAndResets::RunTest(const FString& Parameters)
{
	// Inside: time accrues.
	float Dwell = 0.f;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Dwell = SarkoExtract::AdvanceDwell(Dwell, /*bInside*/ true, 0.1f);
	}
	TestTrue(TEXT("a second inside the zone accrues about a second"), FMath::IsNearlyEqual(Dwell, 1.f, 0.001f));

	// Leaving resets to zero, not "pauses". Spec §4.5: leaving resets it.
	// A pause would let a player hide behind cover, step in for a moment, step
	// out, and stitch five seconds together out of safe fragments — which is
	// exactly the risk the dwell exists to create.
	Dwell = SarkoExtract::AdvanceDwell(Dwell, /*bInside*/ false, 0.1f);
	TestEqual(TEXT("stepping out resets the dwell to zero"), Dwell, 0.f);

	// Re-entering starts from zero.
	Dwell = SarkoExtract::AdvanceDwell(Dwell, true, 0.5f);
	TestEqual(TEXT("re-entering starts from zero"), Dwell, 0.5f);

	// A frame hitch must not skip the dwell: a 3-second delta on a loading
	// stall would otherwise complete most of an extraction the player never
	// stood through. Clamped to a sane per-frame maximum.
	const float AfterHitch = SarkoExtract::AdvanceDwell(0.f, true, 3.f);
	TestTrue(TEXT("a huge frame delta is clamped"), AfterHitch <= 0.5f);

	// Negative or zero delta changes nothing.
	TestEqual(TEXT("a zero delta changes nothing"), SarkoExtract::AdvanceDwell(2.f, true, 0.f), 2.f);
	TestEqual(TEXT("a negative delta changes nothing"), SarkoExtract::AdvanceDwell(2.f, true, -1.f), 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoZoneLookupIsPlanarAndBounded,
	"Sarko.Extract.ZoneLookupIsPlanarAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoZoneLookupIsPlanarAndBounded::RunTest(const FString& Parameters)
{
	TArray<FSarkoExtractionSpot> Zones;
	FSarkoExtractionSpot First;
	First.Location = FVector(-14500.f, 18600.f, 0.f);
	First.RadiusUU = 500.f;
	First.Name = TEXT("Північна стежка");
	Zones.Add(First);

	FSarkoExtractionSpot Second;
	Second.Location = FVector(-1500.f, 18700.f, 0.f);
	Second.RadiusUU = 500.f;
	Second.Name = TEXT("Шосе на північ");
	Zones.Add(Second);

	TestEqual(TEXT("dead centre of the first zone"),
		SarkoExtract::FindZoneContaining(FVector(-14500.f, 18600.f, 150.f), Zones), 0);
	TestEqual(TEXT("inside the second zone"),
		SarkoExtract::FindZoneContaining(FVector(-1400.f, 18650.f, 150.f), Zones), 1);
	TestEqual(TEXT("between the zones is no zone"),
		SarkoExtract::FindZoneContaining(FVector(-8000.f, 18600.f, 150.f), Zones), INDEX_NONE);

	// Height is ignored: the zone is a circle on the ground, and the pawn's
	// origin is at capsule centre 150 uu up.
	TestEqual(TEXT("standing height does not matter"),
		SarkoExtract::FindZoneContaining(FVector(-14500.f, 18600.f, 900.f), Zones), 0);

	// An empty list is not a crash and not zone zero.
	TestEqual(TEXT("no zones means no zone"),
		SarkoExtract::FindZoneContaining(FVector::ZeroVector, TArray<FSarkoExtractionSpot>()), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeExtractionsAreReachableAndDistinct,
	"Sarko.Extract.BridgeExtractionsAreReachableAndDistinct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeExtractionsAreReachableAndDistinct::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("there is somewhere to extract"), Map.Extractions.Num() >= 1);

	// Overlapping zones would make the dwell ambiguous: the pawn would be in two
	// at once and FindZoneContaining would silently pick the earlier one, so the
	// HUD could name a zone the player is not aiming for.
	for (int32 A = 0; A < Map.Extractions.Num(); ++A)
	{
		for (int32 B = A + 1; B < Map.Extractions.Num(); ++B)
		{
			const float Distance = FVector2D(
				Map.Extractions[A].Location.X - Map.Extractions[B].Location.X,
				Map.Extractions[A].Location.Y - Map.Extractions[B].Location.Y).Size();
			TestTrue(*FString::Printf(TEXT("extractions '%s' and '%s' do not overlap"),
				*Map.Extractions[A].Name, *Map.Extractions[B].Name),
				Distance > Map.Extractions[A].RadiusUU + Map.Extractions[B].RadiusUU);
		}
	}

	// No spawn inside an extraction: the player must not be able to extract at
	// second zero with the loadout they walked in with.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestEqual(TEXT("no player spawn sits inside an extraction zone"),
			SarkoExtract::FindZoneContaining(Spawn.GetLocation(), Map.Extractions), INDEX_NONE);
	}
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Extract`
Expected: `BUILD FAILED` — `'Loot/SarkoExtractionZone.h' file not found`.

- [ ] **Step 3: Write the zone header**

`SarkoGame/Source/SarkoGame/Loot/SarkoExtractionZone.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SarkoExtractionZone.generated.h"

class UStaticMeshComponent;

// Forward-declared at global scope, not as an elaborated type inside the
// namespace below: `const struct FSarkoExtractionSpot&` written inside
// `namespace SarkoExtract` declares a second, permanently-incomplete
// SarkoExtract::FSarkoExtractionSpot that shadows the real one. That exact bug
// already happened once in SarkoMapBuilder.h.
struct FSarkoExtractionSpot;

namespace SarkoExtract
{
	/**
	 * Largest per-frame delta the dwell will accept, in seconds.
	 *
	 * A loading stall or a breakpoint produces one enormous delta, and without a
	 * clamp that single frame would complete most of an extraction the player
	 * never actually stood through.
	 */
	constexpr float MaxDwellStepSeconds = 0.5f;

	/**
	 * Advances the extraction dwell timer. Pure.
	 *
	 * Leaving the zone **resets to zero**, not pauses (spec §4.5). Pausing would
	 * let a player assemble five seconds out of safe fragments — step in, step
	 * behind cover, step in again — and the dwell exists precisely to make the
	 * last five seconds of a raid dangerous.
	 */
	float AdvanceDwell(float CurrentSeconds, bool bInsideZone, float DeltaSeconds);

	/**
	 * Index of the zone the pawn is standing in, or INDEX_NONE.
	 *
	 * Planar: a zone is a circle painted on the ground and the pawn's origin is
	 * at capsule centre, so a 3D test would shrink every zone by the pawn's half
	 * height for no reason.
	 */
	int32 FindZoneContaining(const FVector& PawnLocation, const TArray<FSarkoExtractionSpot>& Zones);
}

/**
 * A place the player can leave the raid from.
 *
 * Visual only. The dwell is measured by the game mode against the map
 * definition, on the server, so this actor never decides anything: it exists so
 * the zone can be seen from a top-down camera, and it carries no collision at
 * all (a collision volume here would be a second, disagreeing source of truth
 * about whether the player is inside).
 *
 * Like containers, spawned locally on every machine from the map file and never
 * replicated.
 */
UCLASS()
class ASarkoExtractionZone : public AActor
{
	GENERATED_BODY()

public:
	ASarkoExtractionZone();

	virtual void BeginPlay() override;

	void SetupFromSpot(int32 InIndex, const FString& InName, float InRadiusUU);

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	int32 ZoneIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	FString ZoneName;

	UPROPERTY(BlueprintReadOnly, Category = "Extraction")
	float RadiusUU = 500.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Extraction")
	TObjectPtr<UStaticMeshComponent> Pad;
};
```

- [ ] **Step 4: Write the zone implementation**

`SarkoGame/Source/SarkoGame/Loot/SarkoExtractionZone.cpp`:

```cpp
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
```

- [ ] **Step 5: Spawn the zones from the map**

In `Map/SarkoMapBuilder.h`, next to `SpawnLootContainers`:

```cpp
	/** Spawns one visual pad per extraction spot, in array order, on every machine. */
	void SpawnExtractionZones(UWorld& World, const FSarkoMapDefinition& Definition);
```

In `Map/SarkoMapBuilder.cpp`, add `#include "Loot/SarkoExtractionZone.h"` and:

```cpp
void SarkoMap::SpawnExtractionZones(UWorld& World, const FSarkoMapDefinition& Definition)
{
	for (int32 Index = 0; Index < Definition.Extractions.Num(); ++Index)
	{
		const FSarkoExtractionSpot& Spot = Definition.Extractions[Index];

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// Sizing happens before BeginPlay builds the mesh, so the pad is scaled
		// to the zone's real radius rather than to the class default.
		ASarkoExtractionZone* Zone = World.SpawnActorDeferred<ASarkoExtractionZone>(
			ASarkoExtractionZone::StaticClass(), FTransform(FRotator::ZeroRotator, Spot.Location));
		if (!Zone)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn extraction zone %d"), Index);
			continue;
		}
		Zone->SetupFromSpot(Index, Spot.Name, Spot.RadiusUU);
		Zone->FinishSpawning(FTransform(FRotator::ZeroRotator, Spot.Location));
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d extraction zones"), Definition.Extractions.Num());
}
```

and call it in `ASarkoRaidGameState::SpawnPrebuiltLayout`, right after `SpawnLootContainers`:

```cpp
	SarkoMap::SpawnExtractionZones(*World, InDefinition);
```

- [ ] **Step 6: Add the outcome to the game state**

In `Core/SarkoRaidGameState.h`, above the class:

```cpp
/** How a raid ended. Replicated to everyone: the HUD's final screen reads it. */
UENUM()
enum class ESarkoRaidOutcome : uint8
{
	InProgress,
	/** Stood the full dwell in a zone. The backpack is the haul. */
	Extracted,
	/** HP hit zero. The backpack is already empty by the time this is set. */
	Died,
	/**
	 * The raid clock ran out. Spec §4.5: MIA is death — loot lost, submitted to
	 * the backend as `died`. It is the ceiling on a raid, not its goal.
	 */
	MIA
};
```

and inside the class:

```cpp
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	ESarkoRaidOutcome Outcome = ESarkoRaidOutcome::InProgress;

	bool IsRaidFinished() const { return Outcome != ESarkoRaidOutcome::InProgress; }
```

Register it: `DOREPLIFETIME(ASarkoRaidGameState, Outcome);`.

- [ ] **Step 7: Make the game mode the outcome authority**

In `Core/SarkoRaidGameMode.h`:

```cpp
	virtual void Tick(float DeltaSeconds) override;

	/** Called by the player pawn's death handler, server side. */
	void HandlePlayerDied(class ASarkoCharacter* Pawn);

	/**
	 * Ends the raid exactly once. Freezes input via the game state's replicated
	 * Outcome and (from Task 8) submits the result to the backend. Idempotent —
	 * a death on the same frame the clock expires must not submit twice, and the
	 * backend's idempotency is a safety net, not a licence.
	 */
	void FinishRaid(ESarkoRaidOutcome NewOutcome);

private:
	/** Server-side per-pawn dwell. Keyed weakly so a destroyed pawn drops out. */
	TMap<TWeakObjectPtr<class ASarkoCharacter>, float> DwellSeconds;
```

and set `PrimaryActorTick.bCanEverTick = true;` in the constructor (`AGameModeBase` does not tick by default, and without this the dwell never advances — a silent failure where standing in the zone simply does nothing).

In `Core/SarkoRaidGameMode.cpp`:

```cpp
void ASarkoRaidGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	if (!RaidState || RaidState->IsRaidFinished())
	{
		return;
	}

	// The clock is the ceiling. Spec §4.5: time out is MIA, which is death.
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

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
		if (!Pawn || (Pawn->HealthComponent && Pawn->HealthComponent->IsDead()))
		{
			continue;
		}

		const int32 ZoneIndex = SarkoExtract::FindZoneContaining(Pawn->GetActorLocation(), Zones);
		float& Dwell = DwellSeconds.FindOrAdd(Pawn);
		Dwell = SarkoExtract::AdvanceDwell(Dwell, ZoneIndex != INDEX_NONE, DeltaSeconds);

		// Replicated to the owner only, so that pawn's HUD can draw a countdown
		// without anyone else learning that somebody is extracting.
		Pawn->SetExtractProgress(ZoneIndex, Dwell);

		if (Dwell >= RequiredDwell)
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
	// HandleDeath), so by the time a result is submitted there is nothing to
	// credit. The order matters and is deliberate.
	FinishRaid(ESarkoRaidOutcome::Died);
}

void ASarkoRaidGameMode::FinishRaid(ESarkoRaidOutcome NewOutcome)
{
	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	if (!RaidState || RaidState->IsRaidFinished() || NewOutcome == ESarkoRaidOutcome::InProgress)
	{
		return;
	}
	RaidState->Outcome = NewOutcome;

	// Task 8 adds the backend submission here. Deliberately after the state is
	// set: the HUD must show the outcome immediately, whether or not the network
	// cooperates (spec §4.6 — the game never hard-locks on network).
}
```

with `#include "Core/SarkoRaidSettings.h"`, `#include "Loot/SarkoBackpack.h"`, `#include "Loot/SarkoExtractionZone.h"` and `#include "Pawn/SarkoHealthComponent.h"` added.

- [ ] **Step 8: Replicate the dwell to its owner and notify the game mode on death**

In `Pawn/SarkoCharacter.h`, public:

```cpp
	/** Server only: pushes the dwell state the owning client's HUD draws. */
	void SetExtractProgress(int32 ZoneIndex, float DwellSeconds);

	/** INDEX_NONE when not in a zone. Owner-only replicated. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Extraction")
	int32 ExtractZoneIndex = INDEX_NONE;

	/** Seconds stood in the current zone. Owner-only replicated. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Extraction")
	float ExtractDwellSeconds = 0.f;
```

In `Pawn/SarkoCharacter.cpp`, register both owner-only and implement:

```cpp
	// In GetLifetimeReplicatedProps, alongside AimDirection:
	// Owner-only for the same reason the backpack is: "that player is extracting"
	// is the single most valuable thing an opponent could know.
	DOREPLIFETIME_CONDITION(ASarkoCharacter, ExtractZoneIndex, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASarkoCharacter, ExtractDwellSeconds, COND_OwnerOnly);
```

```cpp
void ASarkoCharacter::SetExtractProgress(int32 ZoneIndex, float DwellSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	ExtractZoneIndex = ZoneIndex;
	ExtractDwellSeconds = DwellSeconds;
}
```

and in `HandleDeath`, after clearing the backpack and the loot channel:

```cpp
	ExtractZoneIndex = INDEX_NONE;
	ExtractDwellSeconds = 0.f;

	// The game mode owns the raid's outcome; the pawn only reports its own death.
	if (ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr)
	{
		GameMode->HandlePlayerDied(this);
	}
```

- [ ] **Step 9: Freeze input once the raid is over**

In `ASarkoPlayerController::PlayerTick`, immediately after the `Pawn` null check:

```cpp
	// Spec §4.5: on a finished raid input is frozen and the summary is all that
	// is left. Frozen here rather than by unpossessing the pawn, so the HUD can
	// still read the backpack it is about to list.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		Pawn->SetMoveIntent(FVector2D::ZeroVector);
		Pawn->SetAimIntent(FVector2D::ZeroVector, false);
		bInteractHeld = false;
		InteractTarget = nullptr;
		return;
	}
```

with `#include "Core/SarkoRaidGameState.h"` added.

- [ ] **Step 10: Draw the countdown and the summary**

In `UI/SarkoHUD.h` declare `void DrawExtraction();` and `void DrawOutcomeSummary();`, calling both from `DrawHUD` last (the summary must be on top of everything).

In `UI/SarkoHUD.cpp`, add `#include "Loot/SarkoExtractionZone.h"`, `#include "Map/SarkoMapDefinition.h"` and:

```cpp
void ASarkoHUD::DrawExtraction()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || Pawn->ExtractZoneIndex == INDEX_NONE)
	{
		return;
	}

	// The zone's name is resolved locally from the map file rather than
	// replicated: an FString on the wire every frame for a value that never
	// changes would be pure waste, and every machine already has the file.
	FSarkoMapDefinition Definition;
	FString Error;
	FString ZoneName = TEXT("ЕВАКУАЦІЯ");
	if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId.ToString(), Definition, Error) &&
		Definition.Extractions.IsValidIndex(Pawn->ExtractZoneIndex))
	{
		ZoneName = Definition.Extractions[Pawn->ExtractZoneIndex].Name;
	}

	const float Required = FMath::Max(0.1f, GetDefault<USarkoRaidSettings>()->ExtractDwellSeconds);
	const float Left = FMath::Max(0.f, Required - Pawn->ExtractDwellSeconds);
	const FString Text = FString::Printf(TEXT("%s — %.1f"), *ZoneName, Left);

	// Top-centre, below the clock and the loot prompt's slot: everything
	// informational lives along the top (spec §9).
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, GEngine->GetLargeFont(), 1.5f);
	const float X = (Canvas->SizeX - Width) * 0.5f;
	constexpr float Y = 130.f;
	DrawRect(FLinearColor(0.f, 0.25f, 0.05f, 0.55f), X - 14.f, Y - 6.f, Width + 28.f, Height + 12.f);
	DrawText(Text, FLinearColor(0.55f, 1.f, 0.6f), X, Y, GEngine->GetLargeFont(), 1.5f);
}

void ASarkoHUD::DrawOutcomeSummary()
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsRaidFinished())
	{
		return;
	}

	FString Title;
	FLinearColor Colour;
	switch (RaidState->Outcome)
	{
	case ESarkoRaidOutcome::Extracted: Title = TEXT("EXTRACTED"); Colour = FLinearColor(0.4f, 1.f, 0.45f); break;
	case ESarkoRaidOutcome::MIA:       Title = TEXT("MIA");       Colour = FLinearColor(1.f, 0.65f, 0.2f);  break;
	default:                           Title = TEXT("KIA");       Colour = FLinearColor(1.f, 0.25f, 0.2f);  break;
	}

	// Dim the world so the summary is unmistakably a final screen rather than
	// another HUD element.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	GetTextSize(Title, TitleWidth, TitleHeight, GEngine->GetLargeFont(), 2.5f);
	float Y = Canvas->SizeY * 0.22f;
	DrawText(Title, Colour, (Canvas->SizeX - TitleWidth) * 0.5f, Y, GEngine->GetLargeFont(), 2.5f);
	Y += TitleHeight + 24.f;

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const USarkoBackpackComponent* Backpack = Pawn ? Pawn->BackpackComponent : nullptr;
	if (!Backpack || Backpack->GetUsedSlots() == 0)
	{
		// Died and MIA both arrive here: the server emptied the backpack before
		// the outcome was set, so "nothing" is the honest and correct summary.
		const FString Empty = TEXT("НІЧОГО НЕ ВИНЕСЕНО");
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Empty, Width, Height, GEngine->GetLargeFont(), 1.f);
		DrawText(Empty, FLinearColor(0.8f, 0.8f, 0.8f), (Canvas->SizeX - Width) * 0.5f, Y, GEngine->GetLargeFont(), 1.f);
		return;
	}

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	for (const FSarkoItemStack& Stack : Backpack->GetSlots())
	{
		const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
		// The id is the fallback, not the label: an id on screen means the
		// catalog and the loot table have drifted, and it should be visible.
		const FString Line = FString::Printf(TEXT("%s  x%d"),
			Def ? *Def->Name : *Stack.Item.ToString(), Stack.Quantity);

		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, GEngine->GetLargeFont(), 1.f);
		DrawText(Line, FLinearColor::White, (Canvas->SizeX - Width) * 0.5f, Y, GEngine->GetLargeFont(), 1.f);
		Y += Height + 6.f;
	}
}
```

with `#include "Loot/SarkoBackpack.h"` and `#include "Loot/SarkoItemCatalog.h"` added.

- [ ] **Step 11: Add the setting**

In `Core/SarkoRaidSettings.h`, a new `Extraction` category:

```cpp
	/**
	 * Seconds the player must stand inside an extraction zone (spec §4.5).
	 *
	 * Slice-1 spec §8 says ten; the Stage A spec says five and is the later
	 * decision, so five it is. This being tunable rather than a constant is the
	 * point: it is the length of the most frightening moment in the raid.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Extraction")
	float ExtractDwellSeconds = 5.f;
```

and in `Config/DefaultGame.ini`:

```ini
ExtractDwellSeconds=5.000000
```

- [ ] **Step 12: Run the tests and play it**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `42 test(s) performed, 0 failed` (39 after Task 5 plus `Sarko.Extract.DwellAccumulatesAndResets`, `Sarko.Extract.ZoneLookupIsPlanarAndBounded`, `Sarko.Extract.BridgeExtractionsAreReachableAndDistinct`).

Then, in a windowed run: loot a container, walk north to the green pad at `(-14500, 18600)`, and confirm the countdown appears and counts down from 5.0, resets when you step out, and produces the `EXTRACTED` summary listing what you took. Then start again, let a bot kill you, and confirm `KIA` with `НІЧОГО НЕ ВИНЕСЕНО`.

```bash
git add SarkoGame && git commit -m "feat(game): extraction zones with a server-side dwell and raid outcomes"
```

---

### Task 7: `FSarkoBackendClient`

Split in two on purpose. The bodies and parsers are pure functions over strings and are tested exhaustively; the transport is twenty lines of `FHttpModule` that cannot be tested headlessly and therefore contains no logic worth testing.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp`
- Modify: `SarkoGame/Source/SarkoGame/SarkoGame.Build.cs` (add `"HTTP"`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, `SarkoGame/Config/DefaultGame.ini`

**Interfaces:**
- Consumes: `FSarkoItemStack` (Task 1), the `Json` module, `FHttpModule`.
- Produces:
  - `FSarkoRaidSession { FString SessionId; FString SessionToken; int32 Seed; FDateTime ExpiresAt; }`
  - `FSarkoBackendError { FString Code; FString Message; }`
  - pure: `SarkoBackend::MakeAnonymousBody`, `MakeRaidStartBody`, `MakeSessionBody`, `MakeRaidResultBody`, `ParseAnonymousResponse`, `ParseRaidStartResponse`, `ParseExpiresAtResponse`, `ParseErrorResponse`, `SeedToInt32`, `EnsureDeviceId`
  - `FSarkoBackendClient` (a `TSharedFromThis` class) with `Authenticate`, `StartRaid`, `ConfirmRaid`, `SubmitResult`, `IsAuthenticated`
  - `USarkoRaidSettings::bBackendEnabled`, `BackendBaseUrl`, `BackendMapId`, `BackendGraceMarginSeconds`, `BackendTimeoutSeconds`

- [ ] **Step 1: Write the failing wire-shape tests**

Create `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/SarkoBackendClient.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendBodiesMatchTheContract,
	"Sarko.Backend.BodiesMatchTheContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendBodiesMatchTheContract::RunTest(const FString& Parameters)
{
	// Every field name below is copied from sarko-api/internal/api's request
	// structs. A renamed field is not a compile error on either side — it is a
	// 400 fifteen minutes into a raid, with the haul on the line.

	const FString Anonymous = SarkoBackend::MakeAnonymousBody(TEXT("device-abc"));
	TestTrue(TEXT("anonymous body carries device_id"), Anonymous.Contains(TEXT("\"device_id\"")));
	TestTrue(TEXT("anonymous body carries the value"), Anonymous.Contains(TEXT("device-abc")));

	TArray<FSarkoItemStack> Loadout;
	Loadout.Add(FSarkoItemStack{ TEXT("pistol"), 1 });
	Loadout.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 60 });
	const FString Start = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), Loadout);
	TestTrue(TEXT("start body carries map_id"), Start.Contains(TEXT("\"map_id\"")));
	TestTrue(TEXT("start body carries the map"), Start.Contains(TEXT("bridge")));
	TestTrue(TEXT("start body carries loadout"), Start.Contains(TEXT("\"loadout\"")));
	TestTrue(TEXT("stacks use item_id, not id or item"), Start.Contains(TEXT("\"item_id\"")));
	TestTrue(TEXT("stacks use quantity, not qty or count"), Start.Contains(TEXT("\"quantity\"")));
	TestFalse(TEXT("no stray 'item' key"), Start.Contains(TEXT("\"item\":")));

	// An empty loadout is legal (domain.ValidateStacks allows it) and must still
	// produce an array, not a null.
	const FString EmptyStart = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), TArray<FSarkoItemStack>());
	TestTrue(TEXT("an empty loadout is an empty array"), EmptyStart.Contains(TEXT("\"loadout\":[]")));

	const FString Session = SarkoBackend::MakeSessionBody(TEXT("sid"), TEXT("stok"));
	TestTrue(TEXT("session body carries session_id"), Session.Contains(TEXT("\"session_id\"")));
	TestTrue(TEXT("session body carries session_token"), Session.Contains(TEXT("\"session_token\"")));

	TArray<FSarkoItemStack> Haul;
	Haul.Add(FSarkoItemStack{ TEXT("scrap_metal"), 4 });
	const FString Result = SarkoBackend::MakeRaidResultBody(TEXT("sid"), TEXT("stok"), TEXT("extracted"), Haul);
	TestTrue(TEXT("result body carries session_id"), Result.Contains(TEXT("\"session_id\"")));
	TestTrue(TEXT("result body carries session_token"), Result.Contains(TEXT("\"session_token\"")));
	TestTrue(TEXT("result body carries outcome"), Result.Contains(TEXT("\"outcome\":\"extracted\"")));
	TestTrue(TEXT("result body carries items"), Result.Contains(TEXT("\"items\"")));

	// Zero and negative quantities are dropped before they leave: the backend
	// rejects the whole request for one bad stack (domain.ValidateStacks), so a
	// stack that should not exist must not cost the player the rest of the haul.
	TArray<FSarkoItemStack> Dirty;
	Dirty.Add(FSarkoItemStack{ TEXT("scrap_metal"), 0 });
	Dirty.Add(FSarkoItemStack{ TEXT("vodka"), -2 });
	Dirty.Add(FSarkoItemStack{ TEXT("medkit"), 1 });
	const FString Cleaned = SarkoBackend::MakeRaidResultBody(TEXT("sid"), TEXT("stok"), TEXT("died"), Dirty);
	TestTrue(TEXT("a valid stack survives"), Cleaned.Contains(TEXT("medkit")));
	TestFalse(TEXT("a zero-quantity stack is dropped"), Cleaned.Contains(TEXT("scrap_metal")));
	TestFalse(TEXT("a negative-quantity stack is dropped"), Cleaned.Contains(TEXT("vodka")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendParsesRealResponses,
	"Sarko.Backend.ParsesRealResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendParsesRealResponses::RunTest(const FString& Parameters)
{
	// These two bodies are verbatim captures from the deployed service on
	// 2026-07-30, not invented shapes.
	const FString AuthJson = TEXT(R"({"player_id":"4ebb53e6-08ef-4709-b0e0-b3a8b6c06ca8","token":"eyJhbGciOiJIUzI1NiJ9.e30.x"})");
	FString PlayerId;
	FString Token;
	FString Error;
	TestTrue(TEXT("the real auth response parses"),
		SarkoBackend::ParseAnonymousResponse(AuthJson, PlayerId, Token, Error));
	TestEqual(TEXT("player_id is read"), PlayerId, FString(TEXT("4ebb53e6-08ef-4709-b0e0-b3a8b6c06ca8")));
	TestFalse(TEXT("token is read"), Token.IsEmpty());

	const FString StartJson = TEXT(R"({"session_id":"62494d7d-9e8f-4248-836c-2de6ac64f87a","session_token":"zLpNBQdn-3W2oTqLXEqUPs3DCH6ODeWbez_IWCSZr_s","seed":3402905197,"expires_at":"2026-07-30T20:46:28.858863Z"})");
	FSarkoRaidSession Session;
	TestTrue(TEXT("the real raid/start response parses"),
		SarkoBackend::ParseRaidStartResponse(StartJson, Session, Error));
	TestEqual(TEXT("session_id is read"), Session.SessionId, FString(TEXT("62494d7d-9e8f-4248-836c-2de6ac64f87a")));
	TestFalse(TEXT("session_token is read"), Session.SessionToken.IsEmpty());
	TestTrue(TEXT("expires_at is read"), Session.ExpiresAt.GetYear() == 2026);

	// The seed the deployed service actually returned. StartRaid does
	// int64(rand.Uint32()), so about half of all seeds exceed INT32_MAX; a naive
	// assignment or FCString::Atoi is silent, platform-dependent corruption, and
	// a corrupted seed means the server and the client disagree about what is in
	// every crate.
	TestEqual(TEXT("a seed above INT32_MAX wraps bit-for-bit"),
		SarkoBackend::SeedToInt32(3402905197LL), static_cast<int32>(static_cast<uint32>(3402905197u)));
	TestEqual(TEXT("and that is negative, not clamped"), Session.Seed, SarkoBackend::SeedToInt32(3402905197LL));
	TestTrue(TEXT("the wrapped seed really is negative"), Session.Seed < 0);
	TestEqual(TEXT("a small seed is unchanged"), SarkoBackend::SeedToInt32(7LL), 7);
	TestEqual(TEXT("the largest uint32 wraps to -1"), SarkoBackend::SeedToInt32(4294967295LL), -1);

	const FString ConfirmJson = TEXT(R"({"expires_at":"2026-07-30T21:00:00Z"})");
	FDateTime ExpiresAt;
	TestTrue(TEXT("the confirm response parses"),
		SarkoBackend::ParseExpiresAtResponse(ConfirmJson, ExpiresAt, Error));
	TestEqual(TEXT("the confirm deadline is read"), ExpiresAt.GetHour(), 21);

	// Errors are always {"error":{"code","message"}}.
	const FString ErrorJson = TEXT(R"({"error":{"code":"map_locked","message":"your garage does not unlock this map"}})");
	FSarkoBackendError Parsed;
	TestTrue(TEXT("an error body parses"), SarkoBackend::ParseErrorResponse(ErrorJson, Parsed));
	TestEqual(TEXT("the code is read"), Parsed.Code, FString(TEXT("map_locked")));
	TestFalse(TEXT("the message is read"), Parsed.Message.IsEmpty());
	TestFalse(TEXT("a success body is not an error"), SarkoBackend::ParseErrorResponse(AuthJson, Parsed));

	// Malformed input never yields a half-filled session.
	for (const FString& Bad : { FString(TEXT("{{{")), FString(TEXT("{}")),
		FString(TEXT(R"({"session_id":"x"})")), FString(TEXT(R"({"session_id":"x","session_token":"y"})")) })
	{
		FSarkoRaidSession Broken;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Bad),
			SarkoBackend::ParseRaidStartResponse(Bad, Broken, Error));
		TestFalse(TEXT("and names the problem"), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendSettingsAreShippable,
	"Sarko.Backend.SettingsAreShippable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendSettingsAreShippable::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// USarkoRaidSettings is config=Game, so these come from DefaultGame.ini. A
	// value put in DefaultEngine.ini by mistake would silently leave the C++
	// default here and nothing would warn.
	TestTrue(TEXT("the base url is https"), Settings.BackendBaseUrl.StartsWith(TEXT("https://")));
	TestFalse(TEXT("the base url has no trailing slash"), Settings.BackendBaseUrl.EndsWith(TEXT("/")));

	// The wire map id must be a map the backend actually unlocks at vehicle tier
	// `none`. sarko-api/internal/domain/garage.go maps TierNone to "bridge" and
	// knows nothing called "bridge", which /v1/raid/start answers with 403
	// map_locked — verified live on 2026-07-30. See the plan's conflict note.
	TestEqual(TEXT("the wire map id is the tier-none map"), Settings.BackendMapId, FString(TEXT("bridge")));
	TestEqual(TEXT("the local data file is still bridge"), Settings.MapId, FName(TEXT("bridge")));

	// The grace margin exists so the client's clock ends before the server's
	// deadline: RAID_TTL is 12m and GRACE_BUFFER 2m on the deployed service, and
	// sarko-api/README.md is explicit that the buffer is not play time.
	TestTrue(TEXT("there is a grace margin"), Settings.BackendGraceMarginSeconds >= 60.f);
	TestTrue(TEXT("the HTTP timeout is short enough not to stall a raid"),
		Settings.BackendTimeoutSeconds > 0.f && Settings.BackendTimeoutSeconds <= 20.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `BUILD FAILED` — `'Net/SarkoBackendClient.h' file not found`.

- [ ] **Step 3: Add the HTTP module**

In `Source/SarkoGame/SarkoGame.Build.cs`, add `"HTTP"` to `PublicDependencyModuleNames`, leaving every existing entry — including `"Json"` and `"JsonUtilities"`, which are already present — exactly as it is. The final list is `Core, CoreUObject, Engine, InputCore, AIModule, NavigationSystem, DeveloperSettings, Json, JsonUtilities, HTTP`.

- [ ] **Step 4: Write the header**

`SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoBackendClient.generated.h"

/** What /v1/raid/start hands back. Field names match the Go struct exactly. */
USTRUCT()
struct FSarkoRaidSession
{
	GENERATED_BODY()

	UPROPERTY()
	FString SessionId;

	/**
	 * One-time plaintext token. Returned once and never again — the backend
	 * stores only its SHA-256 hash — so losing it means losing the raid's
	 * result, and it is never logged.
	 */
	UPROPERTY()
	FString SessionToken;

	/**
	 * The raid seed, wrapped into int32 (see SeedToInt32). The backend produces
	 * it as int64(rand.Uint32()), so this is routinely negative.
	 */
	UPROPERTY()
	int32 Seed = 0;

	UPROPERTY()
	FDateTime ExpiresAt = FDateTime(0);
};

/** The `{"error":{"code","message"}}` shape every failing endpoint returns. */
USTRUCT()
struct FSarkoBackendError
{
	GENERATED_BODY()

	UPROPERTY()
	FString Code;

	UPROPERTY()
	FString Message;
};

namespace SarkoBackend
{
	// ---- pure: request bodies -------------------------------------------------
	// Built by hand rather than through FJsonObjectConverter so the exact field
	// names are visible in this file and cannot drift with a struct rename.

	FString MakeAnonymousBody(const FString& DeviceId);
	FString MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout);
	FString MakeSessionBody(const FString& SessionId, const FString& SessionToken);

	/**
	 * Outcome is the literal string the backend accepts: "extracted" or "died".
	 * Stacks with a non-positive quantity are dropped, because the backend
	 * rejects the whole request for one bad stack and losing a haul to a stray
	 * zero would be absurd.
	 */
	FString MakeRaidResultBody(const FString& SessionId, const FString& SessionToken,
		const FString& Outcome, const TArray<FSarkoItemStack>& Items);

	// ---- pure: responses ------------------------------------------------------

	bool ParseAnonymousResponse(const FString& Json, FString& OutPlayerId, FString& OutToken, FString& OutError);
	bool ParseRaidStartResponse(const FString& Json, FSarkoRaidSession& OutSession, FString& OutError);

	/** Reads `{"expires_at": "<RFC3339>"}` — the confirm response. */
	bool ParseExpiresAtResponse(const FString& Json, FDateTime& OutExpiresAt, FString& OutError);

	/** True when the body is an error envelope. Never treats a success body as an error. */
	bool ParseErrorResponse(const FString& Json, FSarkoBackendError& OutError);

	/**
	 * Wraps the backend's seed into int32, preserving every bit.
	 *
	 * StartRaid returns `int64(rand.Uint32())`, so values above INT32_MAX arrive
	 * routinely (3402905197 was observed live). Assigning that to an int32 is
	 * implementation-defined, and FCString::Atoi saturates — either way the
	 * client's seed stops matching the server's and every container rolls
	 * differently on the two machines. Going through uint32 is well-defined and
	 * lossless.
	 */
	int32 SeedToInt32(int64 Seed);

	/**
	 * Returns this install's device id, creating and persisting one on first run
	 * under Saved/SarkoDevice.txt.
	 *
	 * A GUID string, 36 characters, comfortably inside the backend's 128-char
	 * cap. Persisted rather than derived from hardware so it survives an OS
	 * update and never identifies the machine.
	 */
	FString EnsureDeviceId();

	/** Absolute path of the device-id file. Exposed so a test can reason about it. */
	FString DeviceIdFilePath();
}

/**
 * The client's side of sarko-api.
 *
 * Owned by the raid game mode (the server, in this slice's listen-server
 * topology) and shared, because an HTTP completion can fire after the world has
 * torn down: every callback captures a TWeakPtr to this object and a
 * TWeakObjectPtr to whatever UObject it wants to touch, and does nothing if
 * either is gone.
 *
 * **Offline degradation is a feature (spec §4.6):** every failure logs at Error
 * and calls back with success=false. The raid still plays; nothing persists.
 * The game must never hard-lock on the network, because the developer plays it
 * on a laptop and the player plays it in a lift.
 */
class FSarkoBackendClient : public TSharedFromThis<FSarkoBackendClient>
{
public:
	/** Called with success and, on failure, a human-readable reason already logged. */
	using FOnDone = TFunction<void(bool bSuccess, const FString& Error)>;
	using FOnSession = TFunction<void(bool bSuccess, const FSarkoRaidSession& Session, const FString& Error)>;
	using FOnDeadline = TFunction<void(bool bSuccess, const FDateTime& ExpiresAt, const FString& Error)>;

	bool IsAuthenticated() const { return !Jwt.IsEmpty(); }

	/** POST /v1/auth/anonymous with the persisted device id. */
	void Authenticate(FOnDone OnDone);

	/** POST /v1/raid/start. Debits the loadout. */
	void StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone);

	/** POST /v1/raid/confirm. Until this lands the loadout comes back after PENDING_TTL. */
	void ConfirmRaid(const FSarkoRaidSession& Session, FOnDeadline OnDone);

	/** POST /v1/raid/result. Idempotent server-side, so a retry is safe. */
	void SubmitResult(const FSarkoRaidSession& Session, const FString& Outcome,
		const TArray<FSarkoItemStack>& Items, FOnDone OnDone);

private:
	/** One place that builds, sends and unwraps a request. */
	void Send(const FString& Path, const FString& Body, bool bAuthenticated,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& Error)> OnComplete);

	FString Jwt;
	FString PlayerId;
};
```

- [ ] **Step 5: Write the pure half**

`SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.cpp`, first section:

```cpp
#include "Net/SarkoBackendClient.h"

#include "Core/SarkoRaidSettings.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Serialises stacks as the backend's `[{"item_id","quantity"}]`, dropping non-positive quantities. */
	FString StacksToJsonArray(const TArray<FSarkoItemStack>& Stacks)
	{
		TArray<FString> Parts;
		Parts.Reserve(Stacks.Num());
		for (const FSarkoItemStack& Stack : Stacks)
		{
			if (Stack.Quantity <= 0)
			{
				// domain.ValidateStacks rejects the whole request for one
				// non-positive quantity, and a stray zero must not cost the
				// player the rest of the haul.
				continue;
			}
			Parts.Add(FString::Printf(TEXT("{\"item_id\":\"%s\",\"quantity\":%d}"),
				*Stack.Item.ToString(), Stack.Quantity));
		}
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(",")));
	}

	/** Parses a root JSON object, or fails with a named error. */
	bool ReadRoot(const FString& Json, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			OutError = TEXT("response was not valid JSON");
			return false;
		}
		return true;
	}
}

FString SarkoBackend::MakeAnonymousBody(const FString& DeviceId)
{
	return FString::Printf(TEXT("{\"device_id\":\"%s\"}"), *DeviceId);
}

FString SarkoBackend::MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout)
{
	return FString::Printf(TEXT("{\"map_id\":\"%s\",\"loadout\":%s}"), *MapId, *StacksToJsonArray(Loadout));
}

FString SarkoBackend::MakeSessionBody(const FString& SessionId, const FString& SessionToken)
{
	return FString::Printf(TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\"}"), *SessionId, *SessionToken);
}

FString SarkoBackend::MakeRaidResultBody(const FString& SessionId, const FString& SessionToken,
	const FString& Outcome, const TArray<FSarkoItemStack>& Items)
{
	return FString::Printf(
		TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\",\"outcome\":\"%s\",\"items\":%s}"),
		*SessionId, *SessionToken, *Outcome, *StacksToJsonArray(Items));
}

bool SarkoBackend::ParseAnonymousResponse(const FString& Json, FString& OutPlayerId, FString& OutToken, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	if (!Root->TryGetStringField(TEXT("player_id"), OutPlayerId) || OutPlayerId.IsEmpty())
	{
		OutError = TEXT("auth response has no 'player_id'");
		return false;
	}
	if (!Root->TryGetStringField(TEXT("token"), OutToken) || OutToken.IsEmpty())
	{
		OutError = TEXT("auth response has no 'token'");
		return false;
	}
	return true;
}

bool SarkoBackend::ParseRaidStartResponse(const FString& Json, FSarkoRaidSession& OutSession, FString& OutError)
{
	// Reset first: a half-filled session is worse than none, because a caller
	// that ignores the return value would submit a result with a blank token and
	// get a 401 it cannot explain.
	OutSession = FSarkoRaidSession();

	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	if (!Root->TryGetStringField(TEXT("session_id"), OutSession.SessionId) || OutSession.SessionId.IsEmpty())
	{
		OutError = TEXT("raid/start response has no 'session_id'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	if (!Root->TryGetStringField(TEXT("session_token"), OutSession.SessionToken) || OutSession.SessionToken.IsEmpty())
	{
		OutError = TEXT("raid/start response has no 'session_token'");
		OutSession = FSarkoRaidSession();
		return false;
	}

	// Read as a double, because the value can exceed INT32_MAX and the JSON
	// reader has no int64 accessor. Every uint32 is exactly representable in a
	// double, so nothing is lost before SeedToInt32 wraps it.
	double Seed = 0.0;
	if (!Root->TryGetNumberField(TEXT("seed"), Seed))
	{
		OutError = TEXT("raid/start response has no 'seed'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	OutSession.Seed = SeedToInt32(static_cast<int64>(Seed));

	FString ExpiresAt;
	if (!Root->TryGetStringField(TEXT("expires_at"), ExpiresAt) ||
		!FDateTime::ParseIso8601(*ExpiresAt, OutSession.ExpiresAt))
	{
		OutError = TEXT("raid/start response has no parseable 'expires_at'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	return true;
}

bool SarkoBackend::ParseExpiresAtResponse(const FString& Json, FDateTime& OutExpiresAt, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	FString ExpiresAt;
	if (!Root->TryGetStringField(TEXT("expires_at"), ExpiresAt) ||
		!FDateTime::ParseIso8601(*ExpiresAt, OutExpiresAt))
	{
		OutError = TEXT("response has no parseable 'expires_at'");
		return false;
	}
	return true;
}

bool SarkoBackend::ParseErrorResponse(const FString& Json, FSarkoBackendError& OutError)
{
	OutError = FSarkoBackendError();

	TSharedPtr<FJsonObject> Root;
	FString Ignored;
	if (!ReadRoot(Json, Root, Ignored))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Envelope = nullptr;
	if (!Root->TryGetObjectField(TEXT("error"), Envelope) || !Envelope)
	{
		return false;
	}
	(*Envelope)->TryGetStringField(TEXT("code"), OutError.Code);
	(*Envelope)->TryGetStringField(TEXT("message"), OutError.Message);
	return !OutError.Code.IsEmpty();
}

int32 SarkoBackend::SeedToInt32(int64 Seed)
{
	// Truncate to 32 bits, then reinterpret. Well-defined both ways, and the
	// same bits FRandomStream would have got from the backend's uint32.
	return static_cast<int32>(static_cast<uint32>(static_cast<uint64>(Seed) & 0xFFFFFFFFull));
}

FString SarkoBackend::DeviceIdFilePath()
{
	// Under Saved/ so it survives a rebuild, is never committed, and is a plain
	// runtime file rather than an asset — this project ships no binary assets.
	return FPaths::ProjectSavedDir() / TEXT("SarkoDevice.txt");
}

FString SarkoBackend::EnsureDeviceId()
{
	const FString Path = DeviceIdFilePath();

	FString Existing;
	if (FFileHelper::LoadFileToString(Existing, *Path))
	{
		Existing.TrimStartAndEndInline();
		if (!Existing.IsEmpty() && Existing.Len() <= 128)
		{
			return Existing;
		}
	}

	// A GUID, not a hardware id: 36 characters, inside the backend's 128-char
	// cap, and it identifies an install rather than a machine.
	const FString Fresh = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	if (!FFileHelper::SaveStringToFile(Fresh, *Path))
	{
		// Not fatal, but loud: without persistence every launch is a new player
		// with a new stash, which looks exactly like the backend losing data.
		UE_LOG(LogTemp, Error, TEXT("SarkoBackend: could not persist the device id to %s — progress will not carry across launches"),
			*Path);
	}
	return Fresh;
}
```

- [ ] **Step 6: Write the transport half**

Append to `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.cpp`:

```cpp
void FSarkoBackendClient::Send(const FString& Path, const FString& Body, bool bAuthenticated,
	TFunction<void(bool, const FString&, const FString&)> OnComplete)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	if (!Settings.bBackendEnabled)
	{
		OnComplete(false, FString(), TEXT("the backend is disabled in settings"));
		return;
	}
	if (bAuthenticated && Jwt.IsEmpty())
	{
		OnComplete(false, FString(), TEXT("no token: authenticate first"));
		return;
	}

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.BackendBaseUrl + Path);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (bAuthenticated)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Jwt);
	}
	Request->SetContentAsString(Body);
	// Bounded, because a stalled request must not hold the end of a raid open.
	Request->SetTimeout(Settings.BackendTimeoutSeconds);

	// Weak self: an HTTP completion routinely fires after the world — and this
	// client with it — has been torn down.
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Path, OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!WeakSelf.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("SarkoBackend: %s completed after the client was destroyed; ignored"), *Path);
				return;
			}
			if (!bConnected || !Response.IsValid())
			{
				const FString Error = FString::Printf(TEXT("%s: no response (offline?)"), *Path);
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
				OnComplete(false, FString(), Error);
				return;
			}

			const int32 Code = Response->GetResponseCode();
			const FString ResponseBody = Response->GetContentAsString();
			if (Code < 200 || Code >= 300)
			{
				FSarkoBackendError Parsed;
				const FString Error = SarkoBackend::ParseErrorResponse(ResponseBody, Parsed)
					? FString::Printf(TEXT("%s: HTTP %d %s — %s"), *Path, Code, *Parsed.Code, *Parsed.Message)
					: FString::Printf(TEXT("%s: HTTP %d"), *Path, Code);
				// Error, not Warning: every one of these costs the player a
				// raid's worth of persistence, and the log is the only place it
				// is visible.
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
				OnComplete(false, ResponseBody, Error);
				return;
			}

			OnComplete(true, ResponseBody, FString());
		});

	if (!Request->ProcessRequest())
	{
		const FString Error = FString::Printf(TEXT("%s: the request could not be dispatched"), *Path);
		UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
		OnComplete(false, FString(), Error);
	}
}

void FSarkoBackendClient::Authenticate(FOnDone OnDone)
{
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Send(TEXT("/v1/auth/anonymous"), SarkoBackend::MakeAnonymousBody(SarkoBackend::EnsureDeviceId()),
		/*bAuthenticated*/ false,
		[WeakSelf, OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			TSharedPtr<FSarkoBackendClient> Self = WeakSelf.Pin();
			if (!Self)
			{
				return;
			}
			if (!bSuccess)
			{
				OnDone(false, Error);
				return;
			}
			FString ParseError;
			if (!SarkoBackend::ParseAnonymousResponse(Body, Self->PlayerId, Self->Jwt, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, ParseError);
				return;
			}
			// The token is never logged. The player id is, because it is the only
			// way to find this player's rows in the database from a log line.
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: authenticated as player %s"), *Self->PlayerId);
			OnDone(true, FString());
		});
}

void FSarkoBackendClient::StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone)
{
	Send(TEXT("/v1/raid/start"), SarkoBackend::MakeRaidStartBody(MapId, Loadout), /*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FSarkoRaidSession(), Error);
				return;
			}
			FSarkoRaidSession Session;
			FString ParseError;
			if (!SarkoBackend::ParseRaidStartResponse(Body, Session, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FSarkoRaidSession(), ParseError);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: raid session %s opened, seed %d"),
				*Session.SessionId, Session.Seed);
			OnDone(true, Session, FString());
		});
}

void FSarkoBackendClient::ConfirmRaid(const FSarkoRaidSession& Session, FOnDeadline OnDone)
{
	Send(TEXT("/v1/raid/confirm"), SarkoBackend::MakeSessionBody(Session.SessionId, Session.SessionToken),
		/*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FDateTime(0), Error);
				return;
			}
			FDateTime ExpiresAt(0);
			FString ParseError;
			if (!SarkoBackend::ParseExpiresAtResponse(Body, ExpiresAt, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FDateTime(0), ParseError);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: raid confirmed, server deadline %s"),
				*ExpiresAt.ToIso8601());
			OnDone(true, ExpiresAt, FString());
		});
}

void FSarkoBackendClient::SubmitResult(const FSarkoRaidSession& Session, const FString& Outcome,
	const TArray<FSarkoItemStack>& Items, FOnDone OnDone)
{
	Send(TEXT("/v1/raid/result"),
		SarkoBackend::MakeRaidResultBody(Session.SessionId, Session.SessionToken, Outcome, Items),
		/*bAuthenticated*/ true,
		[Outcome, OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, Error);
				return;
			}
			// The response carries credited_items and already_closed; logged
			// rather than parsed into a struct, because nothing in the raid acts
			// on them — the shelter reads the profile next launch.
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: result '%s' recorded: %s"), *Outcome, *Body);
			OnDone(true, FString());
		});
}
```

- [ ] **Step 7: Add the settings**

In `Core/SarkoRaidSettings.h`, a new `Backend` category:

```cpp
	/**
	 * Whether the raid talks to sarko-api at all. Off means the raid runs on a
	 * local seed and nothing persists — useful on a plane, and the only way to
	 * iterate on gameplay while the backend is down.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	bool bBackendEnabled = true;

	/** No trailing slash: paths are appended verbatim. */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	FString BackendBaseUrl = TEXT("https://sarko-api-production.up.railway.app");

	/**
	 * The map id sent to /v1/raid/start, which is NOT the local data file name.
	 *
	 * sarko-api unlocks maps by vehicle tier (internal/domain/garage.go), and
	 * tier `none` unlocks exactly "bridge" (renamed from "forest" in 80a5a4d), and
	 * sending that gets 403 map_locked — verified against the deployed service.
	 * The Bridge sector is what tier-none players actually play, so it goes over
	 * the wire as the tier-none map until the backend's ladder is renamed.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	FString BackendMapId = TEXT("bridge");

	/**
	 * How far short of the server's deadline the in-raid clock stops.
	 *
	 * /v1/raid/confirm returns expires_at = now + RAID_TTL + GRACE_BUFFER (14
	 * minutes on the deployed service). sarko-api/README.md is explicit that the
	 * grace buffer is slack for a slow submission and not play time, so the
	 * clock ends this many seconds earlier. Playing right up to expires_at means
	 * a player who extracts on the last second can lose the run to network
	 * latency.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	float BackendGraceMarginSeconds = 120.f;

	/** Per-request timeout. Short: a stalled call must not hold the end of a raid open. */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	float BackendTimeoutSeconds = 10.f;
```

and in `Config/DefaultGame.ini`:

```ini
bBackendEnabled=True
BackendBaseUrl=https://sarko-api-production.up.railway.app
BackendMapId=bridge
BackendGraceMarginSeconds=120.000000
BackendTimeoutSeconds=10.000000
```

- [ ] **Step 8: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `3 test(s) performed, 0 failed` — `BodiesMatchTheContract`, `ParsesRealResponses`, `SettingsAreShippable`.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `45 test(s) performed, 0 failed`.

```bash
git add SarkoGame && git commit -m "feat(game): FSarkoBackendClient over FHttpModule with pure, tested wire shapes"
```

---

### Task 8: Wire the backend into the raid loop

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (`bSessionReady`)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp` (refuse looting before the seed lands)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp` (the connecting banner)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp` (extend)

**Interfaces:**
- Consumes: everything from Task 7, `ASarkoRaidGameState::Seed`, `StartRaidClock`, `FinishRaid`, `USarkoBackpackComponent::GetSlots`.
- Produces:
  - `float SarkoBackend::ClockSecondsFromDeadline(float MapDurationSeconds, double SecondsUntilDeadline, float GraceMarginSeconds)` — pure
  - `const TCHAR* SarkoBackend::OutcomeToWire(ESarkoRaidOutcome)` — pure
  - `TArray<FSarkoItemStack> SarkoBackend::StarterLoadout()` — what the raid takes in
  - `ASarkoRaidGameState::bSessionReady` (replicated), `ASarkoRaidGameState::IsLootable() const`

- [ ] **Step 1: Write the failing clock and outcome tests**

Append to `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp` (add `#include "Core/SarkoRaidGameState.h"`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRaidClockStopsShortOfTheServerDeadline,
	"Sarko.Backend.RaidClockStopsShortOfTheServerDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRaidClockStopsShortOfTheServerDeadline::RunTest(const FString& Parameters)
{
	// The deployed service runs RAID_TTL=12m, GRACE_BUFFER=2m, so confirm hands
	// back a deadline 14 minutes out. bridge.json asks for 15 minutes of play.
	// sarko-api/README.md: the client's timer must be strictly shorter than
	// RAID_TTL, and the grace buffer is not play time — so the clock must land at
	// 12 minutes, not 14 and not 15.
	const float Clock = SarkoBackend::ClockSecondsFromDeadline(
		/*MapDurationSeconds*/ 900.f, /*SecondsUntilDeadline*/ 840.0, /*GraceMarginSeconds*/ 120.f);
	TestTrue(TEXT("the clock stops short of the server deadline"), Clock < 840.f);
	TestTrue(TEXT("and equals the deadline minus the grace margin"), FMath::IsNearlyEqual(Clock, 720.f, 0.5f));

	// When the server is generous, the map's own duration still wins: a 15-minute
	// map must not become a 25-minute raid because RAID_TTL was raised.
	TestTrue(TEXT("the map duration is the ceiling"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(900.f, 1800.0, 120.f), 900.f, 0.5f));

	// A deadline already in the past, or inside the margin, must not produce a
	// zero or negative clock — that would end the raid on the spawn frame.
	TestTrue(TEXT("an expired deadline still yields a playable floor"),
		SarkoBackend::ClockSecondsFromDeadline(900.f, -5.0, 120.f) >= 30.f);
	TestTrue(TEXT("a deadline inside the margin still yields a playable floor"),
		SarkoBackend::ClockSecondsFromDeadline(900.f, 60.0, 120.f) >= 30.f);

	// A map with no duration falls back to the settings default, never to zero.
	TestTrue(TEXT("a zero map duration does not produce a zero clock"),
		SarkoBackend::ClockSecondsFromDeadline(0.f, 840.0, 120.f) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOutcomeMapsToTheWireStrings,
	"Sarko.Backend.OutcomeMapsToTheWireStrings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOutcomeMapsToTheWireStrings::RunTest(const FString& Parameters)
{
	// domain.IsValidOutcome accepts exactly "extracted" and "died". Anything else
	// is a 400 and a lost raid.
	TestEqual(TEXT("extracted"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::Extracted)), FString(TEXT("extracted")));
	TestEqual(TEXT("died"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::Died)), FString(TEXT("died")));

	// Spec §4.5: MIA is death. There is no third outcome on the wire, and
	// inventing one would be rejected outright.
	TestEqual(TEXT("MIA is submitted as died"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::MIA)), FString(TEXT("died")));

	// InProgress must never be submitted; it maps to died so a bug cannot invent
	// an extraction, which is the direction that would grant loot for free.
	TestEqual(TEXT("InProgress degrades to died, never to extracted"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::InProgress)), FString(TEXT("died")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStarterLoadoutIsAffordable,
	"Sarko.Backend.StarterLoadoutIsAffordable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStarterLoadoutIsAffordable::RunTest(const FString& Parameters)
{
	const TArray<FSarkoItemStack> Loadout = SarkoBackend::StarterLoadout();
	TestTrue(TEXT("the raid takes something in"), Loadout.Num() > 0);

	// Every item must be in the catalog, or /v1/raid/start answers 400
	// implausible_items and no raid ever begins.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	for (const FSarkoItemStack& Stack : Loadout)
	{
		TestNotNull(*FString::Printf(TEXT("loadout item '%s' is in the catalog"), *Stack.Item.ToString()),
			Catalog.Find(Stack.Item));
		TestTrue(TEXT("quantities are positive"), Stack.Quantity > 0);
	}

	// It must be exactly what the backend's starter kit grants, or the very first
	// /v1/raid/start fails with 409 insufficient_items: the loadout is debited at
	// entry, and a new player owns nothing else.
	TestEqual(TEXT("the loadout is one pistol"),
		Loadout.FindByPredicate([](const FSarkoItemStack& S) { return S.Item == TEXT("pistol"); }) != nullptr, true);
	const FSarkoItemStack* Ammo = Loadout.FindByPredicate(
		[](const FSarkoItemStack& S) { return S.Item == TEXT("ammo_9mm"); });
	TestNotNull(TEXT("the loadout carries ammo"), Ammo);
	if (Ammo)
	{
		TestTrue(TEXT("no more ammo than the starter kit grants"), Ammo->Quantity <= 60);
	}
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `BUILD FAILED` — `no member named 'ClockSecondsFromDeadline' in namespace 'SarkoBackend'`.

- [ ] **Step 3: Add the three pure helpers**

In `Net/SarkoBackendClient.h`, inside `namespace SarkoBackend` (and add `#include "Core/SarkoRaidGameState.h"`… **no** — that would be a circular include, since the game state does not include this header but the game mode includes both. Instead forward-declare the enum at global scope above the namespace):

```cpp
// The outcome enum lives on the game state. Declared here rather than included,
// so this header stays free of the game framework and can be included from a
// test that has no world.
enum class ESarkoRaidOutcome : uint8;
```

then in the namespace:

```cpp
	/**
	 * How long the in-raid clock should run, given the map's own duration and the
	 * server's deadline.
	 *
	 * The server's deadline is RAID_TTL + GRACE_BUFFER from confirm time, and
	 * sarko-api/README.md is explicit that the buffer is slack for a slow result
	 * submission rather than play time: a player who extracts on the last second
	 * of a clock that runs to the deadline can lose the whole run to latency. So
	 * the clock is min(map duration, deadline − margin), with a floor so a stale
	 * or already-expired deadline never ends the raid on the spawn frame.
	 */
	float ClockSecondsFromDeadline(float MapDurationSeconds, double SecondsUntilDeadline, float GraceMarginSeconds);

	/**
	 * The literal outcome string /v1/raid/result accepts. Exactly "extracted" or
	 * "died" (domain.IsValidOutcome); MIA is death (spec §4.5), and anything
	 * unexpected degrades to "died" — the direction that cannot grant loot for
	 * free.
	 */
	const TCHAR* OutcomeToWire(ESarkoRaidOutcome Outcome);

	/**
	 * What the raid takes in. Must be a subset of what the backend's starter kit
	 * grants, or a new player's first /v1/raid/start is 409 insufficient_items:
	 * the loadout is debited at entry and a new player owns nothing else.
	 *
	 * The medkit stays in the stash: this slice has no healing item to use, so
	 * taking it in would only risk losing it.
	 */
	TArray<FSarkoItemStack> StarterLoadout();
```

In `Net/SarkoBackendClient.cpp` add `#include "Core/SarkoRaidGameState.h"` and:

```cpp
float SarkoBackend::ClockSecondsFromDeadline(float MapDurationSeconds, double SecondsUntilDeadline, float GraceMarginSeconds)
{
	/** Never end a raid on the spawn frame, whatever the server said. */
	constexpr float MinimumPlayableSeconds = 30.f;

	const float FromMap = MapDurationSeconds > 0.f
		? MapDurationSeconds
		: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;

	const float FromServer = static_cast<float>(SecondsUntilDeadline) - FMath::Max(0.f, GraceMarginSeconds);

	// The map is the ceiling and the server is the other ceiling; the floor stops
	// a bad clock or an old deadline from producing a raid that is already over.
	return FMath::Max(MinimumPlayableSeconds, FMath::Min(FromMap, FMath::Max(FromServer, MinimumPlayableSeconds)));
}

const TCHAR* SarkoBackend::OutcomeToWire(ESarkoRaidOutcome Outcome)
{
	// Only Extracted maps to "extracted". Everything else — including a state
	// that should never reach here — is "died", because the failure direction
	// that costs a player their haul is recoverable and the one that grants free
	// loot is not.
	return Outcome == ESarkoRaidOutcome::Extracted ? TEXT("extracted") : TEXT("died");
}

TArray<FSarkoItemStack> SarkoBackend::StarterLoadout()
{
	// Mirrors domain.StarterKit() minus the medkit. Keep them in step: this is
	// debited from the stash at /v1/raid/start, and asking for more than the kit
	// granted is 409 insufficient_items on a brand-new player's first raid.
	TArray<FSarkoItemStack> Loadout;
	Loadout.Add(FSarkoItemStack{ TEXT("pistol"), 1 });
	Loadout.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 60 });
	return Loadout;
}
```

- [ ] **Step 4: Gate looting on the session**

In `Core/SarkoRaidGameState.h`:

```cpp
	/**
	 * True once the raid's authoritative seed is in place — either because
	 * sarko-api opened a session, or because the backend is disabled or
	 * unreachable and the local seed is being used instead.
	 *
	 * Containers refuse to open until it is set, because a container looted
	 * against the placeholder seed and then re-derived against the real one would
	 * give two different hauls for one crate.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	bool bSessionReady = false;

	bool IsLootable() const { return bSessionReady && !IsRaidFinished(); }
```

Register it (`DOREPLIFETIME(ASarkoRaidGameState, bSessionReady);`).

In `Pawn/SarkoCharacter.cpp`, in `ServerBeginLoot_Implementation`, immediately after the `RaidState` null check:

```cpp
	if (!RaidState->IsLootable())
	{
		// The seed has not landed yet, or the raid is already over. Refusing is
		// the safe direction: a roll against a placeholder seed is a roll that
		// disagrees with every later re-derivation of the same container.
		return;
	}
```

and in `ASarkoPlayerController::UpdateInteract`, so the prompt does not appear before the raid is live, immediately after the pawn check:

```cpp
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsLootable())
	{
		bInteractHeld = false;
		InteractTarget = nullptr;
		return;
	}
```

- [ ] **Step 5: Drive the sequence from the game mode**

In `Core/SarkoRaidGameMode.h`:

```cpp
	virtual void BeginPlay() override;

private:
	/** Kicks off auth → start → confirm. No-op when the backend is disabled. */
	void BeginBackendSession();

	/** Everything after a failed call: log, use the local seed, keep playing (spec §4.6). */
	void FallBackToOfflineRaid(const FString& Reason);

	/** Marks the seed authoritative and lets containers open. */
	void ActivateRaid(int32 AuthoritativeSeed, float ClockSeconds);

	/** Shared, because the client is used across several async callbacks. */
	TSharedPtr<class FSarkoBackendClient> Backend;

	FSarkoRaidSession Session;
	bool bSessionSubmitted = false;
```

with `#include "Net/SarkoBackendClient.h"` added to that header (it brings `FSarkoRaidSession`, which is a `USTRUCT` and therefore needs the full type here).

In `Core/SarkoRaidGameMode.cpp`:

```cpp
void ASarkoRaidGameMode::BeginPlay()
{
	Super::BeginPlay();

	// StartPlay already started the clock with the map's duration and handed the
	// layout to the game state; this only adds the backend, so a failed network
	// call changes nothing about whether the raid runs.
	if (!GetDefault<USarkoRaidSettings>()->bBackendEnabled)
	{
		FallBackToOfflineRaid(TEXT("the backend is disabled in settings"));
		return;
	}
	BeginBackendSession();
}

void ASarkoRaidGameMode::BeginBackendSession()
{
	Backend = MakeShared<FSarkoBackendClient>();

	// TWeakObjectPtr through every hop: an HTTP completion can land after the
	// level has been torn down, and this game mode is the first thing to go.
	TWeakObjectPtr<ASarkoRaidGameMode> WeakThis(this);

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

		const FString MapId = GetDefault<USarkoRaidSettings>()->BackendMapId;
		Self->Backend->StartRaid(MapId, SarkoBackend::StarterLoadout(),
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

						if (Confirmed->CachedDefinition.RaidDurationSeconds > Clock + 1.f)
						{
							// Loud, because it is a configuration mismatch a
							// player would experience as "the raid was shorter
							// than the map says". RAID_TTL on the deployed
							// service is 12m against a 15-minute map.
							UE_LOG(LogTemp, Warning,
								TEXT("SarkoRaidGameMode: the map asks for %.0fs but the server's deadline allows %.0fs — raising RAID_TTL is the fix"),
								Confirmed->CachedDefinition.RaidDurationSeconds, Clock);
						}

						Confirmed->ActivateRaid(Confirmed->Session.Seed, Clock);
					});
			});
	});
}

void ASarkoRaidGameMode::FallBackToOfflineRaid(const FString& Reason)
{
	// Spec §4.6: any HTTP failure logs loudly, the raid still plays, nothing
	// persists. The game must never hard-lock on the network — the alternative is
	// a black screen in a lift, and a developer who cannot iterate on a plane.
	UE_LOG(LogTemp, Error,
		TEXT("SarkoRaidGameMode: playing OFFLINE — nothing will be saved. Reason: %s"), *Reason);

	Session = FSarkoRaidSession();
	// The Seed the game mode already holds (URL option or its default) becomes
	// authoritative for this raid.
	ActivateRaid(Seed, CachedDefinition.RaidDurationSeconds);
}

void ASarkoRaidGameMode::ActivateRaid(int32 AuthoritativeSeed, float ClockSeconds)
{
	ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>();
	if (!RaidState || RaidState->bSessionReady)
	{
		return;
	}

	Seed = AuthoritativeSeed;
	RaidState->Seed = AuthoritativeSeed;
	RaidState->StartRaidClock(ClockSeconds);
	RaidState->bSessionReady = true;

	UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: raid live — seed %d, clock %.0fs, session '%s'"),
		AuthoritativeSeed, ClockSeconds, Session.SessionId.IsEmpty() ? TEXT("(offline)") : *Session.SessionId);
}
```

**Two edits to what `StartPlay` already does.** It currently sets `RaidState->Seed = Seed;` and calls `StartRaidClock` unconditionally, which would let containers roll against a placeholder seed before `raid/start` answers. Change it to:
- keep `RaidState->SpawnPrebuiltLayout(...)`, the bot spawning and `CachedLayout` exactly as they are;
- **remove** the `RaidState->Seed = Seed;` assignment and the `StartRaidClock` call — both now happen in `ActivateRaid`. Leave a comment where they were saying so, because their absence otherwise looks like an omission.

`bSessionReady` gates looting, so between `StartPlay` and `ActivateRaid` the player can move and shoot but not loot. That gap is one HTTP round trip.

- [ ] **Step 6: Submit the result**

In `ASarkoRaidGameMode::FinishRaid`, replacing the placeholder comment added in Task 6:

```cpp
	// Submitted once. The backend is idempotent by session id, which is a safety
	// net for a dropped connection — not a licence to send twice.
	if (bSessionSubmitted)
	{
		return;
	}
	bSessionSubmitted = true;

	if (!Backend || Session.SessionId.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("SarkoRaidGameMode: raid ended '%s' with no backend session — nothing was saved"),
			*FString(SarkoBackend::OutcomeToWire(NewOutcome)));
		return;
	}

	// Only an extraction carries anything out. On death and MIA the pawn's
	// backpack was already emptied by ASarkoCharacter::HandleDeath, but sending
	// an explicitly empty array makes the intent unambiguous rather than relying
	// on that ordering.
	TArray<FSarkoItemStack> Haul;
	if (NewOutcome == ESarkoRaidOutcome::Extracted)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const ASarkoCharacter* Pawn = It->IsValid() ? Cast<ASarkoCharacter>((*It)->GetPawn()) : nullptr;
			if (Pawn && Pawn->BackpackComponent)
			{
				Haul = Pawn->BackpackComponent->GetSlots();
				break;
			}
		}
	}

	Backend->SubmitResult(Session, SarkoBackend::OutcomeToWire(NewOutcome), Haul,
		[Outcome = NewOutcome](bool bSuccess, const FString& Error)
		{
			// No TWeakObjectPtr capture needed: nothing here touches the game
			// mode, precisely so a result landing after teardown is still logged.
			if (bSuccess)
			{
				UE_LOG(LogTemp, Display, TEXT("SarkoRaidGameMode: result submitted"));
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("SarkoRaidGameMode: the raid result was NOT saved: %s. The session expires on the server and closes as died."),
					*Error);
			}
		});
```

**Known limitation to write into the task report:** a failed submission is not retried. Slice-1 spec §11 wants a local retry queue; that needs the shelter (which does not exist yet) to own the queue, because the raid's world is gone moments later. The server-side `expires_at` sweeper means an unsubmitted raid closes as `died` rather than staying open forever, so the failure mode is a lost haul, not a stuck account.

- [ ] **Step 7: Show the player that it is connecting, and that it is offline**

In `UI/SarkoHUD.cpp`, in `DrawTopBar`, after the clock:

```cpp
	// The player must be able to tell "the raid has not started yet" from "the
	// crates are broken". Spec §4.6's loud degradation is a log line for the
	// developer; this is the same fact for the player.
	if (!RaidState->bSessionReady)
	{
		const FString Connecting = TEXT("З'ЄДНАННЯ...");
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Connecting, Width, Height, GEngine->GetLargeFont(), 1.f);
		DrawText(Connecting, FLinearColor(1.f, 0.75f, 0.2f), (Canvas->SizeX - Width) * 0.5f, 56.f,
			GEngine->GetLargeFont(), 1.f);
	}
```

- [ ] **Step 8: Run the suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Backend`
Expected: `6 test(s) performed, 0 failed` — the 3 from Task 7 plus `RaidClockStopsShortOfTheServerDeadline`, `OutcomeMapsToTheWireStrings`, `StarterLoadoutIsAffordable`.

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `48 test(s) performed, 0 failed`.

- [ ] **Step 9: Prove the offline path before trusting the online one**

Set `bBackendEnabled=False` in `Config/DefaultGame.ini`, run windowed, and confirm: the log carries `playing OFFLINE — nothing will be saved`, the connecting banner disappears immediately, containers open, and extraction shows `EXTRACTED` with a summary. Then set `BackendBaseUrl=https://sarko-api-production.invalid` with `bBackendEnabled=True` and confirm the same, with `no response (offline?)` in the log and no hang at startup.

**Restore both settings to their real values before committing.**

- [ ] **Step 10: Commit**

```bash
git add SarkoGame && git commit -m "feat(game): wire the raid loop to sarko-api with loud offline degradation"
```

---

### Task 9: Live end-to-end verification against the deployed backend

Last, and the only step that proves Stage A actually works: everything above is green tests and a windowed run on one machine. This task deploys the backend change, plays a real raid, and shows the stash growing in Postgres.

**Files:**
- Create: `SarkoGame/Scripts/hud-shot.sh`
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp` (a delayed-screenshot exec)

**Interfaces:**
- Consumes: everything. Produces: two PNGs, two `curl` transcripts, and the report.

No `CheatGiveLoot` or `CheatExtract` exists and none is added: granting loot from the console would be a second, untested path into the thing this whole plan exists to make trustworthy. The offscreen runs move the pawn with the **engine's own** `BugItGo` cheat, which teleports and nothing more.

- [ ] **Step 1: Add the delayed-screenshot exec**

In `Core/SarkoPlayerController.h`, next to `SarkoOverview` (unconditional declaration — `UFUNCTION(Exec)` inside `#if !UE_BUILD_SHIPPING` is rejected by UHT):

```cpp
	/**
	 * Takes a screenshot DelaySeconds from now, without touching the camera.
	 *
	 * SarkoOverview reframes the view, which is wrong for verifying a HUD state:
	 * the delay is the whole point, because -ExecCmds fires everything at load
	 * and a HUD state that takes five seconds of dwell to appear cannot be
	 * captured at load.
	 */
	UFUNCTION(Exec)
	void SarkoShot(float DelaySeconds);
```

In `Core/SarkoPlayerController.cpp`:

```cpp
void ASarkoPlayerController::SarkoShot(float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	const float Delay = FMath::Clamp(DelaySeconds, 0.1f, 60.f);
	UE_LOG(LogTemp, Display, TEXT("SarkoShot: screenshot in %.1fs"), Delay);

	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1920x1080"), /*bWriteToLog*/ true);
	}), Delay, false);
#endif
}
```

- [ ] **Step 2: Add the HUD screenshot script**

Create `SarkoGame/Scripts/hud-shot.sh` and `chmod +x` it. It is `overview-shot.sh` with the exec commands passed in, because a HUD state needs the pawn somewhere specific and a delay before the capture:

```bash
#!/usr/bin/env bash
#
# Launches the game offscreen with a real renderer, runs the given console
# commands, waits for a screenshot and prints its path.
#
# Usage:
#   Scripts/hud-shot.sh "BugItGo -17400 16500 200, SarkoShot 3"
#
# -nullrhi renders nothing, so the automation suite cannot see a HUD at all;
# -RenderOffscreen gives a real Metal RHI with no window, which is what makes
# the frame available. BugItGo is UCheatManager's own teleport — no project
# cheat is involved, and EnableCheats is issued first because a cheat manager
# is only created for a local controller in a non-shipping build.
set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: $0 \"<comma-separated console commands>\"" >&2
	exit 2
fi

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${HUD_SHOT_TIMEOUT:-240}"
COMMANDS="$1"

rm -rf "$SHOT_DIR"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" /Engine/Maps/Entry \
	-game -RenderOffscreen -unattended -nosplash -ResX=1920 -ResY=1080 \
	-ExecCmds="EnableCheats, t.MaxFPS 20, $COMMANDS" > /dev/null 2>&1 &
PID=$!

ELAPSED=0
while [[ $ELAPSED -lt $TIMEOUT ]]; do
	if compgen -G "$SHOT_DIR/*.png" > /dev/null; then
		sleep 2   # let the write finish
		break
	fi
	kill -0 "$PID" 2>/dev/null || break
	sleep 5
	ELAPSED=$((ELAPSED + 5))
done

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

SHOT="$(ls -t "$SHOT_DIR"/*.png 2>/dev/null | head -1 || true)"
if [[ -z "$SHOT" ]]; then
	echo "FAIL: no screenshot produced within ${TIMEOUT}s"
	exit 1
fi
echo "$SHOT"
```

- [ ] **Step 3: Deploy the backend change**

The migration runs on startup (`internal/db/migrate.go`), so deploying is migrating. Push the branch and let Railway build (project `sarko`, service `sarko-api`, root directory `sarko-api`, health check `/healthz`), then:

```bash
curl -fsS https://sarko-api-production.up.railway.app/healthz && echo
```

Expected: `{"status":"ok"}`. If the health check never goes green, read the deploy log — a failed goose migration stops the process before it listens, which shows up as a failing health check rather than as a database error.

- [ ] **Step 4: Confirm the starter kit is live**

```bash
API=https://sarko-api-production.up.railway.app
DEVICE="e2e-$(date +%s)"
AUTH=$(curl -fsS -X POST $API/v1/auth/anonymous -H 'Content-Type: application/json' -d "{\"device_id\":\"$DEVICE\"}")
echo "$AUTH"
JWT=$(python3 -c "import sys,json;print(json.load(sys.stdin)['token'])" <<<"$AUTH")
echo "$DEVICE" > /tmp/sarko-e2e-device.txt
echo "$JWT" > /tmp/sarko-e2e-jwt.txt
curl -fsS $API/v1/profile -H "Authorization: Bearer $JWT"; echo
```

Expected profile: `"stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"medkit","quantity":1},{"item_id":"pistol","quantity":1}]` (sorted by `item_id`), `"vehicle_tier":"none"`, `"unlocked_maps":["bridge"]`.

Also confirm the gate rejects an invented item, using a throwaway session:

```bash
API=https://sarko-api-production.up.railway.app
JWT=$(cat /tmp/sarko-e2e-jwt.txt)
S=$(curl -fsS -X POST $API/v1/raid/start -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
  -d '{"map_id":"bridge","loadout":[]}')
SID=$(python3 -c "import sys,json;print(json.load(sys.stdin)['session_id'])" <<<"$S")
STOK=$(python3 -c "import sys,json;print(json.load(sys.stdin)['session_token'])" <<<"$S")
curl -fsS -X POST $API/v1/raid/confirm -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"$SID\",\"session_token\":\"$STOK\"}"; echo
curl -s -w '\n%{http_code}\n' -X POST $API/v1/raid/result -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"$SID\",\"session_token\":\"$STOK\",\"outcome\":\"extracted\",\"items\":[{\"item_id\":\"turbine\",\"quantity\":1}]}"
```

Expected: `400` with `{"error":{"code":"implausible_items",...}}`. Then close that session honestly so it does not block the real raid (`one_open_raid_per_player` allows only one):

```bash
curl -fsS -X POST $API/v1/raid/result -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"$SID\",\"session_token\":\"$STOK\",\"outcome\":\"died\",\"items\":[]}"; echo
```

- [ ] **Step 5: Point the game at that device and record the stash before**

The game creates its own device id under `Saved/SarkoDevice.txt`. For a verifiable before/after, write the one used above into it so the same player is on both sides of the raid:

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame
cat /tmp/sarko-e2e-device.txt > Saved/SarkoDevice.txt
API=https://sarko-api-production.up.railway.app
JWT=$(cat /tmp/sarko-e2e-jwt.txt)
curl -fsS $API/v1/profile -H "Authorization: Bearer $JWT" | tee /tmp/sarko-profile-before.json; echo
```

- [ ] **Step 6: Play the raid — WASD and E**

This is the run that matters, and it is a human run, because a raid has no scripted input path and adding one would be untested code in the middle of the thing being verified.

Run: `cd SarkoGame && "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" SarkoGame.uproject /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900 -log`

Then, in order:
1. Wait for `З'ЄДНАННЯ...` to clear — that is `raid/start` and `raid/confirm` landing. If it never clears, the log has the reason and the raid is offline.
2. Walk WASD to the junk container near spawn at roughly `(-17400, 16350)`. Confirm the prompt.
3. Hold `E` for 1.5 s. Confirm the bar, the grey lid, and the backpack counter climbing.
4. Loot a second container, so the summary has more than one line.
5. Walk north to the green pad at `(-14500, 18600)`. Confirm the countdown from 5.0, step out once to confirm it resets, then stand the full five.
6. Confirm `EXTRACTED` with the items listed by their Ukrainian names.
7. Leave the process running long enough for the log to show `SarkoBackend: result 'extracted' recorded:` — that line contains `credited_items`.

Note the exact items and quantities from the summary. They are what the next step must match.

- [ ] **Step 7: Prove the backend recorded it**

```bash
API=https://sarko-api-production.up.railway.app
JWT=$(cat /tmp/sarko-e2e-jwt.txt)
curl -fsS $API/v1/profile -H "Authorization: Bearer $JWT" | tee /tmp/sarko-profile-after.json; echo
python3 - <<'PY'
import json
before = {i["item_id"]: i["quantity"] for i in json.load(open("/tmp/sarko-profile-before.json"))["stash"]}
after  = {i["item_id"]: i["quantity"] for i in json.load(open("/tmp/sarko-profile-after.json"))["stash"]}
for item in sorted(set(before) | set(after)):
    delta = after.get(item, 0) - before.get(item, 0)
    if delta:
        print(f"{item:>14}: {before.get(item,0):>4} -> {after.get(item,0):>4}  ({delta:+d})")
PY
```

Expected: `pistol` and `ammo_9mm` show the loadout debit at `raid/start`, and every item from the `EXTRACTED` summary shows a positive delta matching the summary's quantities exactly. A summary line with no matching delta means the haul was submitted and not credited — read the `credited_items` in the log before concluding anything.

If the JWT has expired between steps (30-day tokens, so unlikely), re-authenticate with the same device id from `/tmp/sarko-e2e-device.txt` and re-read the profile; the stash is on the player, not the token.

- [ ] **Step 8: Capture the interact prompt offscreen**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame
./Scripts/hud-shot.sh "BugItGo -17400 16500 200, SarkoShot 4"
```

Expected: a PNG path. Read the image: the pawn stands beside a container, `ОБШУКАТИ (junk)` is drawn top-centre, the interact button is lit on the right-hand side at mid-height, and `0/12` is top-left. The delay is 4 s so the session has time to land — if the shot still shows `З'ЄДНАННЯ...`, raise it to 8 and retake.

If `BugItGo` reports an unknown command, the cheat manager was not created; add `-debugcheats` to the arguments in `hud-shot.sh` and retake. If it still fails, take both screenshots from the windowed run with `screencapture -x` instead and say so in the report — the screenshots are evidence, not a mechanism, and the mechanism is not worth a day.

- [ ] **Step 9: Capture the EXTRACTED summary offscreen**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame
./Scripts/hud-shot.sh "BugItGo -14500 18600 200, SarkoShot 12"
```

`BugItGo` drops the pawn at the centre of extraction E1, the dwell runs for 5 s unattended, and the shot lands well after. Expected: the dimmed frame with `EXTRACTED` and `НІЧОГО НЕ ВИНЕСЕНО` — this run looted nothing, which is correct and still proves the summary path. The itemised summary is evidenced by the windowed run in step 6; note that split in the report rather than pretending one screenshot shows both.

This run also submits a real `extracted` result with an empty haul. Use a **different** device id for it than the one in step 5 (delete `Saved/SarkoDevice.txt` first and let it generate one) so it cannot muddy the before/after diff.

- [ ] **Step 10: Run everything one last time and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: `48 test(s) performed, 0 failed`, `ALL GREEN`.

Run: `cd sarko-api && make test`
Expected: `ok` for every package.

```bash
git add SarkoGame && git commit -m "test(game): offscreen HUD screenshot tooling for verifying loot and extraction"
```

- [ ] **Step 11: Write the report**

The report must contain:
1. The two screenshot paths, and what each shows.
2. The before/after stash diff, verbatim.
3. The `credited_items` log line from the windowed run.
4. **The `RAID_TTL` finding:** with `RAID_TTL=12m` the clock clamps to 12 minutes against a 15-minute map, and the warning log line that says so. Raising `RAID_TTL` to `16m` on Railway is a one-variable change and is the owner's call.
5. **The `map_id` finding:** resolved — the backend tier ladder now starts at `bridge` (80a5a4d), so `raid_sessions.map_id` records `bridge` for this sector.
6. Anything that needed changing in the plan, and why.

---

## Manual verification — the part no agent can do

Automation proves the rolls are deterministic, the tables obey ТЗ §30, the dwell resets, and the wire shapes match. None of it can answer whether the loop is any good. Play ten raids and answer:

1. **Is 1.5 s the right channel?** Standing still beside a crate with bots nearby should be a decision. If it never feels risky it is too short; if it feels like a chore it is too long.
2. **Is 12 slots the right greed dial?** The interesting moment is "the crate has more in it than I can carry". If that never happens, slots are too many.
3. **Does the 5 s dwell create the right last moment?** Extraction should be the most frightening part of the raid, not a formality.
4. **Is the loot worth the walk?** A `military` crate across the ravine must feel different from a `junk` crate by the spawn. If they feel the same, the tables are flat, not the map.
5. **Is the interact button reachable without letting go of a stick?** On the phone, in landscape, with both thumbs where they naturally sit.
6. **Does MIA feel fair?** Losing everything to the clock should feel like a mistake you made, not something that happened to you.
7. **Is losing a haul bearable?** This is the question the whole genre turns on. If death feels cheap, the loot is too easy; if it feels unbearable, the raid is too long.

Every one of these is a `Config/DefaultGame.ini` or `Data/Loot/loot-tables.json` edit — which is the point of putting them there.

---

## Self-Review

**Spec coverage — Stage A, §4 clause by clause.**
- §4.1 items catalog at `Data/Items/items.json` with `{id, name (UA), stackSize}`, pure `ParseItemCatalog` + `LoadItemCatalogFromDisk`, loud failure, automation-tested, unknown id in a loot table a load error → Task 1 and Task 3 (`TablesRejectBadInput`'s "unknown item id" case). Ids match the backend: verified against `internal/domain/garage.go`'s recipes, and Task 2's `TestKnownItemsMatchTheClientCatalog` reads the actual file so drift fails a test.
- §4.2 loot tables per tier with `rolls/emptyChance/entries[{item,weight,qty}]`, ТЗ §30 rules, `FRandomStream(Seed ^ ContainerIndex)` rolled at loot time → Task 3, with `RealLootTablesObeyTheDesignRules` encoding each rule and `TickLootChannel` rolling only at completion.
- §4.3 containers from the existing map path, replicated looted flag, visual change, ≤ 250 uu prompt, 1.5 s hold with a bar, `E` on desktop, server RPC validating alive/unlooted/distance/channel time, overflow left behind → Task 5. The one deviation — looted state as a byte array on the game state rather than a per-actor `bLooted` — is stated with its reason in the Global Constraints.
- §4.4 owner-only replicated 12 slots, stacking by `stackSize`, `used/12` beside the ammo, cleared on death → Task 4.
- §4.5 zones from map data, server overlap plus 5 s dwell, zone name and countdown, leaving resets, input frozen, `EXTRACTED` summary, timer→MIA→death → Task 6.
- §4.6 `FSarkoBackendClient` over `FHttpModule`, anonymous auth with a device token under `Saved/`, start/confirm/result to the Slice-1 contract, base URL and enable flag in settings, loud offline degradation → Tasks 7 and 8. New-player starter kit at anonymous registration, idempotent, one-time → Task 2.
- §4.7 rolls, transfer, dwell and death all server-side; the HUD renders replicated state only; `/raid/result` items plausibility-checked → Task 2's `ValidateRaidItems`. **The spec's parenthetical is answered explicitly: the backend has no loot tables and no item catalog today, so the plan implements the count-and-catalog floor and records loot-table reachability as deferred, with the reason.**
- §8 testing: every new pure function is automation-tested; `run-tests.sh` is the only verdict and every verify step names an exact count (27 → 30 → 35 → 37 → 39 → 42 → 45 → 48); backend keeps `-race -p 1` green; live proof is Task 9.

**Schema fields, checked field by field against the real Go structs** (not against Slice-1 §7, which lists endpoints only and specifies no fields — that mismatch is called out in the Global Constraints): `device_id`; `player_id`, `token`; `map_id`, `loadout`; `item_id`, `quantity`; `session_id`, `session_token`, `seed`, `expires_at`; `outcome` ∈ {`extracted`, `died`}; `items`; `credited_items`, `already_closed`; `error.code`, `error.message`. Nothing else is sent and nothing is renamed. `ParsesRealResponses` uses verbatim captured bodies rather than invented ones.

**Item ids, checked against the real backend.** The backend has no item catalog — `stash_items.item_id` is free-form `TEXT`. The only ids it names anywhere are the garage recipes in `internal/domain/garage.go`: `bike_frame, wheel_small, chain, engine_small, wheel_medium, fuel_tank, engine_large, wheel_large, gearbox, battery, turbine, rotor_blade, avionics`. Task 1 uses the three bicycle ids verbatim and deliberately omits the later tiers' parts so no shipped table can produce them. Everything else (`pistol`, `ammo_9mm`, `medkit`, `bandage`, `painkillers`, `scrap_metal`, `copper_wire`, `duct_tape`, `canned_food`, `vodka`, `cigarettes`, `toolbox`) is new and becomes the contract in both places at once, mirrored in Task 2 with a drift test.

**Module deps in `Build.cs`.** `Json` and `JsonUtilities` are already present (added by the Bridge Map plan) — Task 7 says so and adds only `HTTP`. `DefaultBuildSettings = BuildSettingsVersion.V7` and `PrivateIncludePaths.Add(ModuleDirectory)` are untouched, which is why every include in this plan is written module-relative (`"Loot/SarkoItemCatalog.h"`).

**No binary assets anywhere.** The plan creates: 2 `.json` data files, 12 `.h`/`.cpp` files, 1 `.sh`, 1 `.sql`, 2 `.go`, and edits to `.ini`/`.cs`/existing sources. Geometry is `/Engine/BasicShapes/Cube.Cube` and `Cylinder.Cylinder`; the container lid and the extraction pad tint through `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial` dynamic instances, the same trick `SarkoBody` already uses. All HUD is `DrawHUD` primitives. Input is polled key/touch state — no Enhanced Input action. `Saved/SarkoDevice.txt` is written at runtime, which the constraints permit. `Config/DefaultEngine.ini` already stages `Data`, so no packaging change is needed and Task 1 says to verify rather than duplicate that line.

**Trap checklist, walked deliberately.** `run-tests.sh` with a named count in every verify step (never a bare exit code). `config=Game` settings only in `DefaultGame.ini`, and a test asserts the values actually loaded. `AStaticMeshActor` never made to replicate; containers and zones spawn locally on both sides and only a byte array crosses the wire. Movable → mesh → Static in both new spawn paths, with the reason in the comment. `UFUNCTION(Exec)` declared unconditionally with the body guarded (`SarkoShot`). No `FRotator` float comparison; `.Equals` or `f`-suffixed literals throughout. Every client-supplied container index bounds-checked twice — at RPC entry and every channel tick — and the distance re-measured from the server's own pawn. `FSarkoExtractionSpot` forward-declared at global scope above `namespace SarkoExtract` with the reason written out. Every HTTP callback captures `TWeakPtr`/`TWeakObjectPtr` and returns early. No per-tick HTTP; the proximity scan walks a cached array and allocates nothing.

**Type consistency across tasks.** `FSarkoItemStack{Item, Quantity}` is defined once in Task 1 and used unchanged by Tasks 3, 4, 5, 7, 8. `SarkoLoot::CanInteract` has one signature, used by both the controller (prompt) and the character (RPC). `ASarkoRaidGameState::Seed` stays `int32` and is fed only through `SarkoBackend::SeedToInt32`. `ESarkoRaidOutcome` is declared once on the game state and forward-declared (not redefined) in the backend header. `BackpackSlots` (client setting) and `domain.MaxRaidStacks` (backend cap) are both 12, and Task 4's comment says raising one without the other breaks full hauls.

**Three conflicts flagged, none silently resolved:** `map_id` `bridge` → 403 `map_locked`; `RAID_TTL=12m` versus a 15-minute map; `seed` exceeding `int32`. Each has a mechanism in the plan that is safe today and a named decision left to the owner, and Task 9's report asks for all three.

**Known limitations, for the task reports rather than for silence.** A failed `/v1/raid/result` is not retried — Slice-1 §11 wants a local queue and that belongs to the shelter, which does not exist; the server's `expires_at` sweeper closes the session as `died`, so the failure costs a haul rather than wedging an account. Loot-table reachability is not checked server-side. `DrawExtraction` reloads the map definition from disk to resolve a zone name; that is a file read inside `DrawHUD`, acceptable only because it happens exclusively while the player is standing in a zone, and worth caching on the HUD if it ever shows up in a profile. There is one player, so the per-pawn dwell map and the player-controller iteration are more general than they need to be — deliberately, because PvP is the next topology and neither costs anything now.
