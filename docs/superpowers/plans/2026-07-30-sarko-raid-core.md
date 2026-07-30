# Sarko Raid Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A playable top-down raid in `SarkoGame`: run around a procedurally generated map with touch controls, shoot an AI enemy that shoots back, and die — all on UE replication so a dedicated server later is a build target, not a rewrite.

**Architecture:** Everything is authored as C++ text, with zero binary assets created. Input polls the touch state directly instead of using Enhanced Input (its actions and mapping contexts are `.uasset`). The HUD draws with primitives in `AHUD::DrawHUD` instead of UMG widgets (also `.uasset`). The map is spawned procedurally from the seed `sarko-api` already returns, so no level is authored in the editor. Visuals use engine primitives referenced by path (`/Engine/BasicShapes/Cube`), which requires no authoring. Combat is server-authoritative: the client sends an aim direction, the server traces and applies damage.

**Tech Stack:** UE 5.8 (installed binary engine), C++ only, `Build.sh` + `UnrealEditor-Cmd` from the command line, automation tests under `-nullrhi`.

## Global Constraints

- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project at `SarkoGame/`, module `SarkoGame`, C++ class prefix `Sarko`.
- **`DefaultBuildSettings = BuildSettingsVersion.V7`.** V5 cannot be used with an installed binary engine: the editor target shares build products with the precompiled `UnrealEditor` and UBT rejects a target that changes shared warning levels.
- **Create no binary assets.** No `.uasset`, no `.umap`, no Blueprint, no UMG widget, no Enhanced Input action or mapping context, no DataTable asset. Everything is C++ or CSV. Referencing an *engine* asset by path is fine; authoring a new one is not.
- **Verify with `Scripts/run-tests.sh`, never with a bare exit code.** `UnrealEditor-Cmd` exits 0 even when it ran zero tests. The script takes its verdict from the engine log at `~/Library/Logs/Unreal Engine/SarkoGameEditor/SarkoGame.log` and fails on a failed build, a missing `N tests performed` line, zero matched tests, or any non-Success result.
- **Server-authoritative from day one** (spec §10). Movement is client-predicted and server-validated; shots, damage, and all loot are server-only. The client may draw effects immediately, but never decides an outcome.
- Test naming: `Sarko.<Area>.<Behaviour>`, e.g. `Sarko.Combat.HitscanRespectsCover`.
- Target 60 FPS on iPhone 12. Mobile renderer; Nanite and Lumen stay off.
- Tunables live in `Config/DefaultGame.ini` under `[/Script/SarkoGame.SarkoRaidSettings]` or in CSV — never as literals in code.
- Spec: `docs/superpowers/specs/2026-07-29-sarko-raid-slice-design.md`. §9 is the control scheme, §10 the trust boundaries.

## Scope

This plan is the **raid core** only: movement, touch controls, shooting, damage, death, one AI enemy, on a procedural map. It ends with something playable that answers the project's real question — is touchscreen combat controllable at all.

Two further plans follow, each independently testable:
- **Loot and extraction:** inventory (slots + weight), safe pocket, server-rolled loot containers, extraction zone, raid timer, outcome reporting, CSV data tables.
- **Meta and device:** `IStashStorage` over HTTP against the live `sarko-api`, shelter and garage flow, iOS device build.

Deliberately absent here: inventory, loot, extraction, HTTP, the shelter, PvP, dedicated servers, sound, and any authored art.

---

## File Structure

```
SarkoGame/
├── Config/DefaultGame.ini              # tunables (extend)
├── Scripts/run-tests.sh                # verification loop (exists)
└── Source/SarkoGame/
    ├── SarkoGame.Build.cs              # add modules as needed (exists)
    ├── Core/
    │   ├── SarkoRaidSettings.h/.cpp    # UDeveloperSettings: every tunable
    │   ├── SarkoRaidGameMode.h/.cpp    # server-only: builds the map, spawns actors
    │   ├── SarkoRaidGameState.h/.cpp   # replicated raid clock
    │   └── SarkoPlayerController.h/.cpp# touch polling -> pawn intent
    ├── Map/
    │   └── SarkoMapBuilder.h/.cpp      # pure-ish: seed -> layout; spawns from layout
    ├── Pawn/
    │   ├── SarkoCharacter.h/.cpp       # movement, aim state, replication
    │   └── SarkoHealthComponent.h/.cpp # health, damage, death
    ├── Combat/
    │   └── SarkoWeapon.h/.cpp          # hitscan, magazine, reload, server RPC
    ├── AI/
    │   ├── SarkoEnemyCharacter.h/.cpp  # enemy pawn
    │   └── SarkoAIController.h/.cpp    # C++ state machine, no Behavior Tree
    ├── UI/
    │   └── SarkoHUD.h/.cpp             # DrawHUD primitives: sticks, aim cone, bars
    └── Tests/
        ├── SarkoSmokeTest.cpp          # exists
        ├── MapBuilderTest.cpp
        ├── AimTest.cpp
        ├── CombatTest.cpp
        └── AITest.cpp
```

Split by responsibility, not by engine category: the map builder's geometry decisions are separable from spawning, aim maths is separable from the weapon that consumes it, and the AI's decisions are separable from the pawn it drives. That separation is what makes them testable headlessly.

---

### Task 1: Tunables and the raid game framework

**Files:**
- Create: `Source/SarkoGame/Core/SarkoRaidSettings.h`, `.cpp`
- Create: `Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp`
- Create: `Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp`
- Modify: `Config/DefaultGame.ini`
- Test: `Source/SarkoGame/Tests/MapBuilderTest.cpp` (settings portion)

**Interfaces:**
- Consumes: nothing.
- Produces: `USarkoRaidSettings` (a `UDeveloperSettings` singleton, reachable via `GetDefault<USarkoRaidSettings>()`) exposing `RaidDurationSeconds` (float, default 480), `MapExtent` (float, default 10000 uu = 100 m half-extent), `CoverCount` (int32, default 40), `WalkSpeed` (float, default 400), `AimConeHalfAngleDegrees` (float, default 6), `WeaponRangeUU` (float, default 4000), `WeaponDamage` (float, default 22), `MagazineSize` (int32, default 30), `ReloadSeconds` (float, default 2.2), `EnemyHearingRadiusUU` (float, default 2500), `EnemyFireIntervalSeconds` (float, default 0.9); `ASarkoRaidGameMode` with `int32 Seed`; `ASarkoRaidGameState` with replicated `float RemainingSeconds`.

- [ ] **Step 1: Write the failing settings test**

`Source/SarkoGame/Tests/MapBuilderTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSettingsHaveSaneDefaults,
	"Sarko.Config.SettingsHaveSaneDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSettingsHaveSaneDefaults::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings singleton resolves"), Settings);

	TestTrue(TEXT("raid lasts a positive number of seconds"), Settings->RaidDurationSeconds > 0.f);
	TestTrue(TEXT("map has a positive extent"), Settings->MapExtent > 0.f);
	TestTrue(TEXT("weapon range is shorter than the map"), Settings->WeaponRangeUU < Settings->MapExtent);
	TestTrue(TEXT("aim assist is a nudge, not an aimbot"), Settings->AimConeHalfAngleDegrees > 0.f && Settings->AimConeHalfAngleDegrees <= 10.f);
	TestTrue(TEXT("magazine holds at least one round"), Settings->MagazineSize > 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Config`
Expected: BUILD FAILED — `'Core/SarkoRaidSettings.h' file not found`.

- [ ] **Step 3: Implement the settings object**

`Source/SarkoGame/Core/SarkoRaidSettings.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SarkoRaidSettings.generated.h"

/**
 * Every tunable for the raid, in one place, editable from Config/DefaultGame.ini.
 * Nothing in gameplay code hardcodes a balance number: the whole point of the
 * slice is to change these quickly while looking for what feels good.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Sarko Raid"))
class USarkoRaidSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** How long a raid runs before the timer expires. */
	UPROPERTY(EditAnywhere, config, Category = "Raid")
	float RaidDurationSeconds = 480.f;

	/** Half-extent of the square play area, in unreal units (10000 uu = 100 m). */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	float MapExtent = 10000.f;

	/** How many cover blocks the generator scatters. */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	int32 CoverCount = 40;

	UPROPERTY(EditAnywhere, config, Category = "Movement")
	float WalkSpeed = 400.f;

	/**
	 * Half-angle of the cone inside which a shot snaps to a target. This is a
	 * nudge that compensates for a thumb, not an aimbot — it is applied on the
	 * server, identically for everyone, so it never becomes an advantage.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float AimConeHalfAngleDegrees = 6.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponRangeUU = 4000.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponDamage = 22.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float ReloadSeconds = 2.2f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyHearingRadiusUU = 2500.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyFireIntervalSeconds = 0.9f;
};
```

`Source/SarkoGame/Core/SarkoRaidSettings.cpp`:

```cpp
#include "Core/SarkoRaidSettings.h"
```

- [ ] **Step 4: Run the test and confirm it passes**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Config`
Expected: `1 test(s) performed, 0 failed` and `ALL GREEN`.

- [ ] **Step 5: Implement the game state**

`Source/SarkoGame/Core/SarkoRaidGameState.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

#include "SarkoRaidGameState.generated.h"

/** Raid clock. The server owns it; every client reads it to draw the timer. */
UCLASS()
class ASarkoRaidGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASarkoRaidGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	float RemainingSeconds = 0.f;

	/** Server only: begins the countdown. */
	void StartRaidClock(float DurationSeconds);

	bool IsRaidOver() const { return bClockStarted && RemainingSeconds <= 0.f; }

private:
	bool bClockStarted = false;
};
```

`Source/SarkoGame/Core/SarkoRaidGameState.cpp`:

```cpp
#include "Core/SarkoRaidGameState.h"

#include "Net/UnrealNetwork.h"

ASarkoRaidGameState::ASarkoRaidGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASarkoRaidGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoRaidGameState, RemainingSeconds);
}

void ASarkoRaidGameState::StartRaidClock(float DurationSeconds)
{
	RemainingSeconds = DurationSeconds;
	bClockStarted = true;
}

void ASarkoRaidGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The clock only runs on the server; clients receive RemainingSeconds.
	if (!HasAuthority() || !bClockStarted)
	{
		return;
	}
	RemainingSeconds = FMath::Max(0.f, RemainingSeconds - DeltaSeconds);
}
```

- [ ] **Step 6: Implement the game mode**

`Source/SarkoGame/Core/SarkoRaidGameMode.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "SarkoRaidGameMode.generated.h"

/**
 * Server-only raid authority. Builds the map from the seed and starts the clock.
 * The seed comes from sarko-api's raid/start response, so every client in a
 * match generates the same layout.
 */
UCLASS()
class ASarkoRaidGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASarkoRaidGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

	/** Seed for the procedural layout. Read from the `Seed` URL option when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Raid")
	int32 Seed = 1;
};
```

`Source/SarkoGame/Core/SarkoRaidGameMode.cpp`:

```cpp
#include "Core/SarkoRaidGameMode.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"

ASarkoRaidGameMode::ASarkoRaidGameMode()
{
	GameStateClass = ASarkoRaidGameState::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ASarkoRaidGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// ?Seed=12345 on the travel URL. Defaults to 1 so a bare PIE session is
	// still deterministic rather than accidentally random.
	const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("Seed"));
	if (!SeedOption.IsEmpty())
	{
		Seed = FCString::Atoi(*SeedOption);
	}
}

void ASarkoRaidGameMode::StartPlay()
{
	Super::StartPlay();

	if (ASarkoRaidGameState* RaidState = GetGameState<ASarkoRaidGameState>())
	{
		RaidState->StartRaidClock(GetDefault<USarkoRaidSettings>()->RaidDurationSeconds);
	}
}
```

Add `#include "Kismet/GameplayStatics.h"` to the includes above `Core/SarkoRaidGameMode.h`'s implementation if `ParseOption` does not resolve.

- [ ] **Step 7: Point the config at the new classes**

Append to `Config/DefaultGame.ini`:

```ini
[/Script/EngineSettings.GameMapsSettings]
GlobalDefaultGameMode=/Script/SarkoGame.SarkoRaidGameMode

[/Script/SarkoGame.SarkoRaidSettings]
RaidDurationSeconds=480.000000
MapExtent=10000.000000
CoverCount=40
WalkSpeed=400.000000
AimConeHalfAngleDegrees=6.000000
WeaponRangeUU=4000.000000
WeaponDamage=22.000000
MagazineSize=30
ReloadSeconds=2.200000
EnemyHearingRadiusUU=2500.000000
EnemyFireIntervalSeconds=0.900000
```

- [ ] **Step 8: Verify the whole suite still passes**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: two tests performed, zero failed.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): raid settings, game mode and replicated raid clock"
```

---

### Task 2: Procedural map from a seed

The raid map is generated in code so no level is ever authored in the editor. The layout is a pure function of the seed, which means the same seed gives the same map on every machine — a prerequisite for the dedicated server later, and the reason the map can be tested headlessly at all.

**Files:**
- Create: `Source/SarkoGame/Map/SarkoMapBuilder.h`, `.cpp`
- Modify: `Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (call the builder)
- Test: `Source/SarkoGame/Tests/MapBuilderTest.cpp` (extend)

**Interfaces:**
- Consumes: `USarkoRaidSettings`.
- Produces: `FSarkoCoverBlock { FVector Location; FRotator Rotation; FVector Extent; }`; `FSarkoMapLayout { TArray<FSarkoCoverBlock> Cover; TArray<FVector> PlayerStarts; TArray<FVector> EnemySpawns; float Extent; }`; `FSarkoMapLayout SarkoMap::BuildLayout(int32 Seed, const USarkoRaidSettings& Settings)` — pure, no world, no spawning; `void SarkoMap::SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout)` — spawns floor and cover as `AStaticMeshActor`s using `/Engine/BasicShapes/Cube.Cube`.

Splitting the pure layout from the spawning is what makes this testable: the test calls `BuildLayout` and asserts geometric properties without needing a world at all.

- [ ] **Step 1: Write the failing layout tests**

Append to `Source/SarkoGame/Tests/MapBuilderTest.cpp`:

```cpp
#include "Map/SarkoMapBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLayoutIsDeterministic,
	"Sarko.Map.LayoutIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLayoutIsDeterministic::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	const FSarkoMapLayout A = SarkoMap::BuildLayout(4242, Settings);
	const FSarkoMapLayout B = SarkoMap::BuildLayout(4242, Settings);
	const FSarkoMapLayout Other = SarkoMap::BuildLayout(9999, Settings);

	TestEqual(TEXT("same seed gives the same cover count"), A.Cover.Num(), B.Cover.Num());
	if (A.Cover.Num() == B.Cover.Num() && A.Cover.Num() > 0)
	{
		bool bIdentical = true;
		for (int32 i = 0; i < A.Cover.Num(); ++i)
		{
			bIdentical &= A.Cover[i].Location.Equals(B.Cover[i].Location, 0.01f);
		}
		TestTrue(TEXT("same seed places cover identically"), bIdentical);
	}

	TestNotEqual(TEXT("a different seed gives a different first block"),
		A.Cover[0].Location.X, Other.Cover[0].Location.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLayoutRespectsBounds,
	"Sarko.Map.LayoutRespectsBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoLayoutRespectsBounds::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FSarkoMapLayout Layout = SarkoMap::BuildLayout(7, Settings);

	TestEqual(TEXT("cover count matches the setting"), Layout.Cover.Num(), Settings.CoverCount);
	TestTrue(TEXT("there is somewhere to spawn the player"), Layout.PlayerStarts.Num() > 0);
	TestTrue(TEXT("there is somewhere to spawn an enemy"), Layout.EnemySpawns.Num() > 0);

	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		TestTrue(TEXT("cover stays inside the play area"),
			FMath::Abs(Block.Location.X) <= Settings.MapExtent && FMath::Abs(Block.Location.Y) <= Settings.MapExtent);
	}

	// A player who spawns inside a wall is the classic procedural-map bug.
	for (const FVector& Start : Layout.PlayerStarts)
	{
		for (const FSarkoCoverBlock& Block : Layout.Cover)
		{
			const float PlanarDistance = FVector::Dist2D(Start, Block.Location);
			TestTrue(TEXT("no spawn point sits inside a cover block"),
				PlanarDistance > Block.Extent.GetMax());
		}
	}
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: BUILD FAILED — `'Map/SarkoMapBuilder.h' file not found`.

- [ ] **Step 3: Implement the builder**

`Source/SarkoGame/Map/SarkoMapBuilder.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "SarkoMapBuilder.generated.h"

class USarkoRaidSettings;

/** One piece of cover: a box the player can hide behind and shots cannot cross. */
USTRUCT()
struct FSarkoCoverBlock
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector Extent = FVector(200.f, 200.f, 150.f);
};

/** A complete raid layout. Derived only from the seed and the settings. */
USTRUCT()
struct FSarkoMapLayout
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSarkoCoverBlock> Cover;

	UPROPERTY()
	TArray<FVector> PlayerStarts;

	UPROPERTY()
	TArray<FVector> EnemySpawns;

	UPROPERTY()
	float Extent = 0.f;
};

namespace SarkoMap
{
	/**
	 * Pure: seed in, layout out. No world, no actors, no side effects — which is
	 * why the layout rules can be tested headlessly and why every machine in a
	 * match generates an identical map from the seed sarko-api handed out.
	 */
	FSarkoMapLayout BuildLayout(int32 Seed, const USarkoRaidSettings& Settings);

	/** Spawns floor and cover for a layout using engine primitive meshes. */
	void SpawnLayout(UWorld& World, const FSarkoMapLayout& Layout);
}
```

`Source/SarkoGame/Map/SarkoMapBuilder.cpp`:

```cpp
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

	bool IsClearOfCover(const FVector& Candidate, const TArray<FSarkoCoverBlock>& Cover)
	{
		for (const FSarkoCoverBlock& Block : Cover)
		{
			if (FVector::Dist2D(Candidate, Block.Location) <= Block.Extent.GetMax() + SpawnClearanceUU)
			{
				return false;
			}
		}
		return true;
	}

	/** Rejection-samples a point clear of cover; falls back to the centre-ish. */
	FVector PickClearPoint(FRandomStream& Stream, float Extent, const TArray<FSarkoCoverBlock>& Cover)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const FVector Candidate(
				Stream.FRandRange(-Extent, Extent),
				Stream.FRandRange(-Extent, Extent),
				100.f);
			if (IsClearOfCover(Candidate, Cover))
			{
				return Candidate;
			}
		}
		return FVector(0.f, 0.f, 100.f);
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
```

- [ ] **Step 4: Run and confirm the tests pass**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: two map tests performed, zero failed.

- [ ] **Step 5: Build the map from the game mode**

Add to `ASarkoRaidGameMode::StartPlay` in `Source/SarkoGame/Core/SarkoRaidGameMode.cpp`, before the clock starts:

```cpp
	// Server builds the geometry. Clients get it through actor replication, so
	// the layout is generated exactly once and never disagrees between machines.
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			const FSarkoMapLayout Layout = SarkoMap::BuildLayout(Seed, *GetDefault<USarkoRaidSettings>());
			SarkoMap::SpawnLayout(*World, Layout);
			CachedLayout = Layout;
		}
	}
```

Add `#include "Map/SarkoMapBuilder.h"` to the includes, and declare in `SarkoRaidGameMode.h`:

```cpp
	/** The layout this raid was built from; pawns spawn against it. */
	FSarkoMapLayout CachedLayout;
```

- [ ] **Step 6: Verify the whole suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: four tests performed, zero failed.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): deterministic procedural raid map from the server seed"
```

---

### Task 3: Character, movement and aim state

**Files:**
- Create: `Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp`
- Modify: `Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (set default pawn, spawn at a layout point)
- Test: `Source/SarkoGame/Tests/AimTest.cpp`

**Interfaces:**
- Consumes: `USarkoRaidSettings`, `FSarkoMapLayout`.
- Produces: `ASarkoCharacter` with a top-down camera, `void SetMoveIntent(FVector2D Intent)`, `void SetAimIntent(FVector2D Intent, bool bIsAiming)`, replicated `FVector_NetQuantizeNormal AimDirection`, `bool IsAiming() const`; and the free function `FVector2D SarkoAim::StickToWorldDirection(FVector2D Stick, float CameraYaw)` — pure, so aim maths is tested without a world.

- [ ] **Step 1: Write the failing aim maths test**

`Source/SarkoGame/Tests/AimTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Pawn/SarkoCharacter.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickMapsToWorldDirection,
	"Sarko.Aim.StickMapsToWorldDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickMapsToWorldDirection::RunTest(const FString& Parameters)
{
	// With the camera unrotated, pushing the stick "up" must move the character
	// away from the viewer, i.e. along +X. Getting this wrong is the classic
	// top-down bug where the character walks sideways relative to your thumb.
	const FVector2D Up = SarkoAim::StickToWorldDirection(FVector2D(0.f, 1.f), 0.f);
	TestTrue(TEXT("stick up maps to +X"), Up.X > 0.9f);
	TestTrue(TEXT("stick up has no sideways component"), FMath::Abs(Up.Y) < 0.01f);

	const FVector2D Right = SarkoAim::StickToWorldDirection(FVector2D(1.f, 0.f), 0.f);
	TestTrue(TEXT("stick right maps to +Y"), Right.Y > 0.9f);

	// Rotating the camera 90 degrees must rotate the mapping with it.
	const FVector2D UpRotated = SarkoAim::StickToWorldDirection(FVector2D(0.f, 1.f), 90.f);
	TestTrue(TEXT("camera yaw rotates the mapping"), UpRotated.Y > 0.9f);

	// A dead stick must not produce a direction, or the character spins.
	const FVector2D Dead = SarkoAim::StickToWorldDirection(FVector2D::ZeroVector, 0.f);
	TestTrue(TEXT("a centred stick yields no direction"), Dead.IsNearlyZero());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Aim`
Expected: BUILD FAILED — `'Pawn/SarkoCharacter.h' file not found`.

- [ ] **Step 3: Implement the character**

`Source/SarkoGame/Pawn/SarkoCharacter.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Net/Core/PushModel/PushModel.h"

#include "SarkoCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;

namespace SarkoAim
{
	/**
	 * Converts a thumbstick vector into a world-space planar direction, taking
	 * the camera yaw into account. Pure and world-free so the mapping — the
	 * thing a player feels most immediately — is unit tested.
	 */
	FVector2D StickToWorldDirection(FVector2D Stick, float CameraYaw);
}

/** The player pawn. Top-down camera, server-authoritative aim. */
UCLASS()
class ASarkoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASarkoCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called every frame by the controller from the left stick. */
	void SetMoveIntent(FVector2D Intent);

	/** Called by the controller from the right stick. */
	void SetAimIntent(FVector2D Intent, bool bInIsAiming);

	bool IsAiming() const { return bIsAiming; }

	/** Muzzle position for traces and effects. */
	FVector GetMuzzleLocation() const;

	/** Where this pawn is aiming, replicated so others see the facing. */
	UPROPERTY(ReplicatedUsing = OnRep_AimDirection, BlueprintReadOnly, Category = "Combat")
	FVector_NetQuantizeNormal AimDirection = FVector::ForwardVector;

protected:
	UFUNCTION()
	void OnRep_AimDirection() {}

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

private:
	/** Server RPC: the client's aim is validated and republished by the server. */
	UFUNCTION(Server, Unreliable)
	void ServerSetAim(FVector_NetQuantizeNormal NewAim, bool bInIsAiming);

	FVector2D MoveIntent = FVector2D::ZeroVector;
	bool bIsAiming = false;
};
```

`Source/SarkoGame/Pawn/SarkoCharacter.cpp`:

```cpp
#include "Pawn/SarkoCharacter.h"

#include "Camera/CameraComponent.h"
#include "Core/SarkoRaidSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

FVector2D SarkoAim::StickToWorldDirection(FVector2D Stick, float CameraYaw)
{
	if (Stick.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}

	// Screen "up" (+Y on the stick) is world forward for an unrotated camera.
	const FVector Planar(Stick.Y, Stick.X, 0.f);
	const FVector Rotated = FRotator(0.f, CameraYaw, 0.f).RotateVector(Planar);
	const FVector Normalised = Rotated.GetSafeNormal();
	return FVector2D(Normalised.X, Normalised.Y);
}

ASarkoCharacter::ASarkoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
	Movement->MaxWalkSpeed = GetDefault<USarkoRaidSettings>()->WalkSpeed;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 1400.f;
	CameraBoom->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom);
	TopDownCamera->bUsePawnControlRotation = false;
}

void ASarkoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoCharacter, AimDirection);
}

void ASarkoCharacter::SetMoveIntent(FVector2D Intent)
{
	MoveIntent = Intent.GetSafeNormal();
}

void ASarkoCharacter::SetAimIntent(FVector2D Intent, bool bInIsAiming)
{
	bIsAiming = bInIsAiming;
	if (Intent.IsNearlyZero())
	{
		return;
	}

	const FVector NewAim = FVector(Intent.X, Intent.Y, 0.f).GetSafeNormal();
	AimDirection = NewAim;

	// The client applies its aim locally for responsiveness and tells the
	// server, which republishes it. The server never trusts it for damage —
	// it re-traces from its own copy when the shot is taken.
	if (!HasAuthority())
	{
		ServerSetAim(NewAim, bInIsAiming);
	}
}

void ASarkoCharacter::ServerSetAim_Implementation(FVector_NetQuantizeNormal NewAim, bool bInIsAiming)
{
	AimDirection = NewAim;
	bIsAiming = bInIsAiming;
}

FVector ASarkoCharacter::GetMuzzleLocation() const
{
	// Chest height, slightly ahead of the capsule.
	return GetActorLocation() + FVector(0.f, 0.f, 40.f) + FVector(AimDirection) * 60.f;
}

void ASarkoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!MoveIntent.IsNearlyZero())
	{
		AddMovementInput(FVector(MoveIntent.X, MoveIntent.Y, 0.f), 1.f);
	}

	// Face the aim while aiming, otherwise face travel — spec §9.
	const FVector Facing = bIsAiming
		? FVector(AimDirection)
		: FVector(MoveIntent.X, MoveIntent.Y, 0.f);

	if (!Facing.IsNearlyZero())
	{
		const FRotator Target(0.f, Facing.Rotation().Yaw, 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Target, DeltaSeconds, 12.f));
	}
}
```

- [ ] **Step 4: Run and confirm the aim test passes**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Aim`
Expected: one test performed, zero failed.

- [ ] **Step 5: Spawn the pawn from the layout**

In `ASarkoRaidGameMode`'s constructor add:

```cpp
	DefaultPawnClass = ASarkoCharacter::StaticClass();
```

and override player spawn placement so pawns appear at a layout point rather than at the world origin. Add to `SarkoRaidGameMode.h`:

```cpp
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
```

and to `SarkoRaidGameMode.cpp`:

```cpp
AActor* ASarkoRaidGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// The procedural layout owns spawn placement; there are no PlayerStart
	// actors in the level because there is no authored level.
	if (CachedLayout.PlayerStarts.Num() > 0)
	{
		const int32 Index = NumPlayersStarted % CachedLayout.PlayerStarts.Num();
		if (UWorld* World = GetWorld())
		{
			AActor* Marker = World->SpawnActor<AActor>(AActor::StaticClass(),
				CachedLayout.PlayerStarts[Index], FRotator::ZeroRotator);
			return Marker;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}
```

Add `#include "Pawn/SarkoCharacter.h"` to the game mode's includes.

- [ ] **Step 6: Verify the whole suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: five tests performed, zero failed.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): top-down character with replicated aim and camera"
```

---

### Task 4: Touch controls and the HUD

Spec §9 in code, with no UMG and no Enhanced Input assets: the controller polls the touch state each frame and the HUD draws the sticks and the aim cone with primitives.

**Files:**
- Create: `Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp`
- Create: `Source/SarkoGame/UI/SarkoHUD.h`, `.cpp`
- Modify: `Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (set controller and HUD classes)
- Modify: `Config/DefaultInput.ini`
- Test: `Source/SarkoGame/Tests/AimTest.cpp` (extend with stick-zone tests)

**Interfaces:**
- Consumes: `ASarkoCharacter`, `USarkoRaidSettings`.
- Produces: `ASarkoPlayerController` holding `FSarkoTouchStick { bool bActive; FVector2D Origin; FVector2D Current; FVector2D Value() const; }` for left and right; `ASarkoHUD` drawing both sticks, the aim cone, health and the raid clock; and the pure helper `bool SarkoInput::IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize)`.

Design points that come straight from §9 and must not drift: **floating** sticks — the origin is wherever the thumb lands, not a fixed rosette; the aim cone is drawn on the ground because a touchscreen has no cursor; and there is **no auto-fire** — releasing the right thumb is what fires.

- [ ] **Step 1: Write the failing stick-zone tests**

Append to `Source/SarkoGame/Tests/AimTest.cpp`:

```cpp
#include "Core/SarkoPlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTouchZonesSplitTheScreen,
	"Sarko.Input.TouchZonesSplitTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTouchZonesSplitTheScreen::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(2400.f, 1080.f);

	TestTrue(TEXT("a touch on the left is the move stick"), SarkoInput::IsLeftHalf(FVector2D(300.f, 900.f), Viewport));
	TestFalse(TEXT("a touch on the right is the aim stick"), SarkoInput::IsLeftHalf(FVector2D(2100.f, 900.f), Viewport));
	TestTrue(TEXT("the boundary belongs to the left"), SarkoInput::IsLeftHalf(FVector2D(1199.f, 500.f), Viewport));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStickIsFloatingAndNormalised,
	"Sarko.Input.StickIsFloatingAndNormalised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStickIsFloatingAndNormalised::RunTest(const FString& Parameters)
{
	FSarkoTouchStick Stick;
	Stick.bActive = true;
	// The origin is wherever the thumb landed — a fixed rosette is the known
	// failure mode on phones, so the stick must be relative to its own origin.
	Stick.Origin = FVector2D(700.f, 800.f);
	Stick.Current = FVector2D(700.f, 700.f); // dragged up 100 px

	const FVector2D Value = Stick.Value();
	TestTrue(TEXT("dragging up gives a positive Y"), Value.Y > 0.9f);
	TestTrue(TEXT("the value is clamped to unit length"), Value.Size() <= 1.001f);

	// Beyond the stick radius the value saturates instead of growing.
	Stick.Current = FVector2D(700.f, 100.f);
	TestTrue(TEXT("a long drag saturates at 1"), FMath::IsNearlyEqual(Stick.Value().Size(), 1.f, 0.01f));

	// A thumb that has not moved must not steer.
	Stick.Current = Stick.Origin;
	TestTrue(TEXT("no drag means no input"), Stick.Value().IsNearlyZero());
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Input`
Expected: BUILD FAILED — `'Core/SarkoPlayerController.h' file not found`.

- [ ] **Step 3: Implement the controller**

`Source/SarkoGame/Core/SarkoPlayerController.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SarkoPlayerController.generated.h"

namespace SarkoInput
{
	/** Left half drives movement, right half drives aim. Boundary is inclusive. */
	bool IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize);
}

/** One floating virtual stick, anchored wherever the thumb first touched. */
USTRUCT()
struct FSarkoTouchStick
{
	GENERATED_BODY()

	/** Screen distance at which the stick reads full deflection. */
	static constexpr float RadiusPx = 160.f;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D Current = FVector2D::ZeroVector;

	/** Deflection in the range [-1, 1] per axis, Y up. */
	FVector2D Value() const
	{
		if (!bActive)
		{
			return FVector2D::ZeroVector;
		}
		// Screen Y grows downward; flip it so "up" is positive.
		const FVector2D Delta(Current.X - Origin.X, Origin.Y - Current.Y);
		const float Length = Delta.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		return (Delta / Length) * FMath::Min(1.f, Length / RadiusPx);
	}
};

/**
 * Polls raw touch state instead of using Enhanced Input, because input actions
 * and mapping contexts are binary assets this project cannot author.
 */
UCLASS()
class ASarkoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASarkoPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	const FSarkoTouchStick& GetMoveStick() const { return MoveStick; }
	const FSarkoTouchStick& GetAimStick() const { return AimStick; }

private:
	void UpdateSticks();

	FSarkoTouchStick MoveStick;
	FSarkoTouchStick AimStick;

	/** True on the frame the aim thumb lifts — that is when the shot goes off. */
	bool bAimReleasedThisFrame = false;
};
```

`Source/SarkoGame/Core/SarkoPlayerController.cpp`:

```cpp
#include "Core/SarkoPlayerController.h"

#include "Pawn/SarkoCharacter.h"

bool SarkoInput::IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize)
{
	return ScreenPosition.X < ViewportSize.X * 0.5f;
}

ASarkoPlayerController::ASarkoPlayerController()
{
	bShowMouseCursor = false;
	// Touch is the only input path in this slice.
	DefaultMouseCursor = EMouseCursor::None;
}

void ASarkoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateSticks();

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		return;
	}

	float CameraYaw = 0.f;
	if (PlayerCameraManager)
	{
		CameraYaw = PlayerCameraManager->GetCameraRotation().Yaw;
	}

	Pawn->SetMoveIntent(SarkoAim::StickToWorldDirection(MoveStick.Value(), CameraYaw));

	const FVector2D AimValue = AimStick.Value();
	Pawn->SetAimIntent(SarkoAim::StickToWorldDirection(AimValue, CameraYaw), AimStick.bActive);
}

void ASarkoPlayerController::UpdateSticks()
{
	bAimReleasedThisFrame = false;

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const FVector2D Viewport(static_cast<float>(ViewportX), static_cast<float>(ViewportY));

	const bool bWasAiming = AimStick.bActive;

	// Two fingers are enough for this scheme; poll both touch slots.
	bool bSeenLeft = false;
	bool bSeenRight = false;

	for (int32 Index = 0; Index < 2; ++Index)
	{
		float TouchX = 0.f;
		float TouchY = 0.f;
		bool bPressed = false;
		GetInputTouchState(static_cast<ETouchIndex::Type>(Index), TouchX, TouchY, bPressed);
		if (!bPressed)
		{
			continue;
		}

		const FVector2D Position(TouchX, TouchY);
		FSarkoTouchStick& Stick = SarkoInput::IsLeftHalf(Position, Viewport) ? MoveStick : AimStick;
		bool& bSeen = SarkoInput::IsLeftHalf(Position, Viewport) ? bSeenLeft : bSeenRight;

		if (!Stick.bActive)
		{
			// Floating stick: the origin is where the thumb landed.
			Stick.bActive = true;
			Stick.Origin = Position;
		}
		Stick.Current = Position;
		bSeen = true;
	}

	if (!bSeenLeft)
	{
		MoveStick = FSarkoTouchStick();
	}
	if (!bSeenRight)
	{
		AimStick = FSarkoTouchStick();
	}

	// Release of the aim thumb is the fire signal — never auto-fire (spec §9).
	bAimReleasedThisFrame = bWasAiming && !AimStick.bActive;
}
```

- [ ] **Step 4: Run and confirm the input tests pass**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Input`
Expected: two tests performed, zero failed.

- [ ] **Step 5: Implement the HUD**

`Source/SarkoGame/UI/SarkoHUD.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SarkoHUD.generated.h"

/**
 * Drawn with primitives rather than UMG, because widget blueprints are binary
 * assets. Layout follows spec §9: all information along the top, because the
 * bottom corners are physically covered by the player's thumbs.
 */
UCLASS()
class ASarkoHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawStick(const struct FSarkoTouchStick& Stick, const FLinearColor& Colour);
	void DrawAimCone();
	void DrawTopBar();
};
```

`Source/SarkoGame/UI/SarkoHUD.cpp`:

```cpp
#include "UI/SarkoHUD.h"

#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/Canvas.h"
#include "Pawn/SarkoCharacter.h"

void ASarkoHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	DrawStick(PC->GetMoveStick(), FLinearColor(1.f, 1.f, 1.f, 0.35f));
	DrawStick(PC->GetAimStick(), FLinearColor(1.f, 0.85f, 0.2f, 0.45f));
	DrawAimCone();
	DrawTopBar();
}

void ASarkoHUD::DrawStick(const FSarkoTouchStick& Stick, const FLinearColor& Colour)
{
	if (!Stick.bActive)
	{
		return;
	}

	// Ring at the thumb's landing point, dot at the current position.
	const int32 Segments = 24;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = (2.f * PI * i) / Segments;
		const float A1 = (2.f * PI * (i + 1)) / Segments;
		DrawLine(
			Stick.Origin.X + FMath::Cos(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.X + FMath::Cos(A1) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A1) * FSarkoTouchStick::RadiusPx,
			Colour, 2.f);
	}
	DrawRect(Colour, Stick.Current.X - 12.f, Stick.Current.Y - 12.f, 24.f, 24.f);
}

void ASarkoHUD::DrawAimCone()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->IsAiming())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector Muzzle = Pawn->GetMuzzleLocation();
	const FVector Aim = FVector(Pawn->AimDirection);
	const float HalfAngle = FMath::DegreesToRadians(Settings.AimConeHalfAngleDegrees);

	// Two edges of the cone, projected to screen. On a touchscreen there is no
	// cursor, so without this the player is shooting blind.
	const auto ProjectAndDraw = [this, &Muzzle](const FVector& End, const FLinearColor& Colour)
	{
		const FVector Start2D = Project(Muzzle);
		const FVector End2D = Project(End);
		DrawLine(Start2D.X, Start2D.Y, End2D.X, End2D.Y, Colour, 1.5f);
	};

	const FVector Left = Muzzle + Aim.RotateAngleAxis(FMath::RadiansToDegrees(HalfAngle), FVector::UpVector) * Settings.WeaponRangeUU;
	const FVector Right = Muzzle + Aim.RotateAngleAxis(-FMath::RadiansToDegrees(HalfAngle), FVector::UpVector) * Settings.WeaponRangeUU;

	const FLinearColor Colour(1.f, 0.85f, 0.2f, 0.5f);
	ProjectAndDraw(Left, Colour);
	ProjectAndDraw(Right, Colour);
}

void ASarkoHUD::DrawTopBar()
{
	// Top only: the lower corners are dead zones under the player's thumbs.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState)
	{
		return;
	}

	const int32 Total = FMath::CeilToInt(RaidState->RemainingSeconds);
	const FString Clock = FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);

	float OutWidth = 0.f;
	float OutHeight = 0.f;
	GetTextSize(Clock, OutWidth, OutHeight, GEngine->GetLargeFont(), 1.f);
	DrawText(Clock, FLinearColor::White, (Canvas->SizeX - OutWidth) * 0.5f, 24.f, GEngine->GetLargeFont(), 1.f);
}
```

- [ ] **Step 6: Wire the controller and HUD into the game mode**

In `ASarkoRaidGameMode`'s constructor add:

```cpp
	PlayerControllerClass = ASarkoPlayerController::StaticClass();
	HUDClass = ASarkoHUD::StaticClass();
```

with the matching includes.

- [ ] **Step 7: Verify the whole suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: seven tests performed, zero failed.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): floating touch sticks and primitive-drawn HUD with aim cone"
```

---

### Task 5: Health, damage and death

**Files:**
- Create: `Source/SarkoGame/Pawn/SarkoHealthComponent.h`, `.cpp`
- Modify: `Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp` (own the component)
- Test: `Source/SarkoGame/Tests/CombatTest.cpp`

**Interfaces:**
- Consumes: nothing beyond engine.
- Produces: `USarkoHealthComponent` with replicated `float Health`, `float MaxHealth`, `bool IsDead() const`, `void ApplyDamage(float Amount, AActor* Instigator)` (server only), and `FSarkoDiedSignature OnDied` (a multicast delegate the character and AI subscribe to).

Built before the weapon deliberately: damage is what the weapon *does*, and having it testable first means the weapon's test can assert an outcome rather than a call.

- [ ] **Step 1: Write the failing health tests**

`Source/SarkoGame/Tests/CombatTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Pawn/SarkoHealthComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHealthDamageAndDeath,
	"Sarko.Combat.HealthDamageAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHealthDamageAndDeath::RunTest(const FString& Parameters)
{
	USarkoHealthComponent* Health = NewObject<USarkoHealthComponent>();
	Health->ResetForTest(100.f);

	TestFalse(TEXT("a fresh pawn is alive"), Health->IsDead());
	TestEqual(TEXT("health starts full"), Health->GetHealth(), 100.f);

	Health->ApplyDamage(30.f, nullptr);
	TestEqual(TEXT("damage subtracts"), Health->GetHealth(), 70.f);
	TestFalse(TEXT("still alive at 70"), Health->IsDead());

	int32 DeathCount = 0;
	Health->OnDied.AddLambda([&DeathCount](AActor*) { ++DeathCount; });

	Health->ApplyDamage(1000.f, nullptr);
	TestEqual(TEXT("health floors at zero rather than going negative"), Health->GetHealth(), 0.f);
	TestTrue(TEXT("the pawn is dead"), Health->IsDead());
	TestEqual(TEXT("death fires exactly once"), DeathCount, 1);

	// Overkill on a corpse must not fire the delegate again — that would double
	// every death-driven consequence, including losing a raid.
	Health->ApplyDamage(50.f, nullptr);
	TestEqual(TEXT("death does not fire twice"), DeathCount, 1);

	// Healing is not a mechanic in this slice, but negative damage must not heal.
	Health->ApplyDamage(-25.f, nullptr);
	TestEqual(TEXT("negative damage cannot resurrect or heal"), Health->GetHealth(), 0.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Combat`
Expected: BUILD FAILED — `'Pawn/SarkoHealthComponent.h' file not found`.

- [ ] **Step 3: Implement the health component**

`Source/SarkoGame/Pawn/SarkoHealthComponent.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FSarkoDiedSignature, AActor* /*Killer*/);

/** Health and death. The server is the only thing that may change either. */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoHealthComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	bool IsDead() const { return bDead; }

	/**
	 * Server only. Clamps at zero, ignores non-positive amounts, and fires
	 * OnDied exactly once no matter how much overkill arrives.
	 */
	void ApplyDamage(float Amount, AActor* DamageInstigator);

	/** Test seam: puts the component in a known state without a world. */
	void ResetForTest(float NewMaxHealth);

	FSarkoDiedSignature OnDied;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(Replicated)
	bool bDead = false;
};
```

`Source/SarkoGame/Pawn/SarkoHealthComponent.cpp`:

```cpp
#include "Pawn/SarkoHealthComponent.h"

#include "Net/UnrealNetwork.h"

USarkoHealthComponent::USarkoHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USarkoHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USarkoHealthComponent, Health);
	DOREPLIFETIME(USarkoHealthComponent, MaxHealth);
	DOREPLIFETIME(USarkoHealthComponent, bDead);
}

void USarkoHealthComponent::ResetForTest(float NewMaxHealth)
{
	MaxHealth = NewMaxHealth;
	Health = NewMaxHealth;
	bDead = false;
}

void USarkoHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator)
{
	// Non-positive damage is either a bug upstream or an attempt to heal through
	// the damage path. Either way it must do nothing.
	if (Amount <= 0.f || bDead)
	{
		return;
	}

	Health = FMath::Max(0.f, Health - Amount);
	if (Health > 0.f)
	{
		return;
	}

	// Latch the flag before broadcasting so a handler that deals more damage
	// cannot re-enter and fire death twice.
	bDead = true;
	OnDied.Broadcast(DamageInstigator);
}
```

- [ ] **Step 4: Run and confirm the test passes**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Combat`
Expected: one test performed, zero failed.

- [ ] **Step 5: Give the character health**

Add to `ASarkoCharacter`'s header:

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	TObjectPtr<USarkoHealthComponent> HealthComponent;
```

and in its constructor:

```cpp
	HealthComponent = CreateDefaultSubobject<USarkoHealthComponent>(TEXT("Health"));
```

with `#include "Pawn/SarkoHealthComponent.h"`.

Add a death reaction in `BeginPlay` (server only), stopping movement and disabling collision so the corpse does not block shots:

```cpp
void ASarkoCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoCharacter::HandleDeath);
	}
}

void ASarkoCharacter::HandleDeath(AActor* Killer)
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveIntent = FVector2D::ZeroVector;
}
```

declaring `virtual void BeginPlay() override;` and `void HandleDeath(AActor* Killer);` in the header, with `#include "Components/CapsuleComponent.h"`.

- [ ] **Step 6: Verify the whole suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: eight tests performed, zero failed.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): replicated health, damage and single-fire death"
```

---

### Task 6: Hitscan weapon and the AI enemy

The last task: something that shoots back. Both halves land together because neither is meaningful alone — an enemy that cannot shoot is scenery, and a weapon with nothing to shoot at proves nothing.

**Files:**
- Create: `Source/SarkoGame/Combat/SarkoWeapon.h`, `.cpp`
- Create: `Source/SarkoGame/AI/SarkoEnemyCharacter.h`, `.cpp`
- Create: `Source/SarkoGame/AI/SarkoAIController.h`, `.cpp`
- Modify: `Source/SarkoGame/Pawn/SarkoCharacter.cpp` (hold a weapon, fire on aim release)
- Modify: `Source/SarkoGame/Core/SarkoPlayerController.cpp` (forward the fire signal)
- Modify: `Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (spawn enemies from the layout)
- Modify: `Source/SarkoGame/SarkoGame.Build.cs` (add `AIModule`, `NavigationSystem`)
- Test: `Source/SarkoGame/Tests/CombatTest.cpp` (extend), `Source/SarkoGame/Tests/AITest.cpp`

**Interfaces:**
- Consumes: `USarkoHealthComponent`, `USarkoRaidSettings`, `ASarkoCharacter`.
- Produces: `USarkoWeaponComponent` with `bool CanFire() const`, `void ServerFire(FVector Origin, FVector Direction)` (server only), `int32 GetAmmoInMagazine() const`, `void StartReload()`; the pure helper `FVector SarkoCombat::ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets)`; `ASarkoEnemyCharacter`; `ASarkoAIController` with `enum class ESarkoAIState : uint8 { Idle, Patrol, Chase, Shoot }` and `ESarkoAIState GetState() const`.

- [ ] **Step 1: Write the failing aim-assist and AI tests**

Append to `Source/SarkoGame/Tests/CombatTest.cpp`:

```cpp
#include "Combat/SarkoWeapon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAimAssistIsANudgeNotAnAimbot,
	"Sarko.Combat.AimAssistIsANudgeNotAnAimbot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAimAssistIsANudgeNotAnAimbot::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Forward(1.f, 0.f, 0.f);

	// A target just outside the thumb's precision but inside the cone: snap.
	const TArray<FVector> NearTarget = { FVector(1000.f, 40.f, 0.f) };
	const FVector Assisted = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, NearTarget);
	TestTrue(TEXT("a target inside the cone pulls the shot"), Assisted.Y > 0.f);

	// A target far outside the cone must be ignored, or this is an aimbot.
	const TArray<FVector> FarTarget = { FVector(1000.f, 900.f, 0.f) };
	const FVector Unassisted = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, FarTarget);
	TestTrue(TEXT("a target outside the cone is ignored"), Unassisted.Equals(Forward, 0.001f));

	// With no targets the direction is returned untouched.
	TestTrue(TEXT("no targets means no change"),
		SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, {}).Equals(Forward, 0.001f));

	// With two candidates inside the cone, the nearer one wins.
	const TArray<FVector> TwoTargets = { FVector(3000.f, -60.f, 0.f), FVector(800.f, 30.f, 0.f) };
	const FVector Nearest = SarkoCombat::ApplyAimAssist(Origin, Forward, 6.f, TwoTargets);
	TestTrue(TEXT("the nearest in-cone target wins"), Nearest.Y > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMagazineGatesFiring,
	"Sarko.Combat.MagazineGatesFiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMagazineGatesFiring::RunTest(const FString& Parameters)
{
	USarkoWeaponComponent* Weapon = NewObject<USarkoWeaponComponent>();
	Weapon->ResetForTest(3);

	TestTrue(TEXT("a loaded weapon can fire"), Weapon->CanFire());

	Weapon->ConsumeRoundForTest();
	Weapon->ConsumeRoundForTest();
	Weapon->ConsumeRoundForTest();
	TestEqual(TEXT("the magazine empties"), Weapon->GetAmmoInMagazine(), 0);
	TestFalse(TEXT("an empty weapon cannot fire"), Weapon->CanFire());
	return true;
}
```

`Source/SarkoGame/Tests/AITest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "AI/SarkoAIController.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAIStateTransitions,
	"Sarko.AI.StateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAIStateTransitions::RunTest(const FString& Parameters)
{
	// Pure decision function: distance and visibility in, next state out. This
	// is why the AI is a C++ state machine and not a Behavior Tree — a tree is a
	// binary asset that cannot be written or tested here at all.
	using namespace SarkoAI;

	TestEqual(TEXT("no target at all means patrol"),
		static_cast<int32>(DecideState(ESarkoAIState::Idle, /*bHasTarget*/ false, 0.f, false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Patrol));

	TestEqual(TEXT("a heard but unseen target is chased"),
		static_cast<int32>(DecideState(ESarkoAIState::Patrol, true, 2000.f, /*bHasLineOfSight*/ false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a visible target in range is shot at"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 900.f, true, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Shoot));

	TestEqual(TEXT("a visible target beyond firing range is closed on"),
		static_cast<int32>(DecideState(ESarkoAIState::Shoot, true, 2200.f, true, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Chase));

	TestEqual(TEXT("a target outside hearing range is forgotten"),
		static_cast<int32>(DecideState(ESarkoAIState::Chase, true, 9000.f, false, 2500.f, 1200.f)),
		static_cast<int32>(ESarkoAIState::Patrol));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko`
Expected: BUILD FAILED — `'Combat/SarkoWeapon.h' file not found`.

- [ ] **Step 3: Add the AI modules to the build**

In `Source/SarkoGame/SarkoGame.Build.cs`, extend the dependency list:

```csharp
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NavigationSystem"
		});
```

Note `EnhancedInput` is deliberately dropped: this project polls touch state directly because input actions are binary assets.

- [ ] **Step 4: Implement the weapon**

`Source/SarkoGame/Combat/SarkoWeapon.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoWeapon.generated.h"

namespace SarkoCombat
{
	/**
	 * Nudges a shot toward the nearest target inside a narrow cone, and returns
	 * the direction unchanged when nothing qualifies. Pure so the fairness rule
	 * is unit tested: it must never reach a target outside the cone, or the
	 * "aim assist" becomes an aimbot. Applied on the server for everyone
	 * equally, so it can never be a paid or platform advantage (spec §9, §12).
	 */
	FVector ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets);
}

/** Hitscan weapon. Only the server decides whether a shot connected. */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	bool CanFire() const { return AmmoInMagazine > 0 && !bReloading; }
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	/** Server only: traces, applies aim assist and damage. */
	void ServerFire(FVector Origin, FVector Direction);

	void StartReload();

	/** Test seams — no world required. */
	void ResetForTest(int32 Rounds);
	void ConsumeRoundForTest() { AmmoInMagazine = FMath::Max(0, AmmoInMagazine - 1); }

protected:
	UPROPERTY(Replicated)
	int32 AmmoInMagazine = 0;

	UPROPERTY(Replicated)
	bool bReloading = false;

private:
	void FinishReload();

	FTimerHandle ReloadTimer;
};
```

`Source/SarkoGame/Combat/SarkoWeapon.cpp`:

```cpp
#include "Combat/SarkoWeapon.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Pawn/SarkoHealthComponent.h"
#include "TimerManager.h"

FVector SarkoCombat::ApplyAimAssist(FVector Origin, FVector Direction, float ConeHalfAngleDeg, const TArray<FVector>& CandidateTargets)
{
	const FVector Aim = Direction.GetSafeNormal();
	if (Aim.IsNearlyZero() || CandidateTargets.Num() == 0)
	{
		return Direction;
	}

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));

	const FVector* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FVector& Target : CandidateTargets)
	{
		const FVector ToTarget = Target - Origin;
		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		if (ToTargetDir.IsNearlyZero())
		{
			continue;
		}

		// Strictly inside the cone. Anything else is left alone — this single
		// comparison is what separates assistance from aimbotting.
		if (FVector::DotProduct(Aim, ToTargetDir) < CosLimit)
		{
			continue;
		}

		const float DistanceSq = ToTarget.SizeSquared();
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = &Target;
		}
	}

	return Best ? (*Best - Origin).GetSafeNormal() : Direction;
}

USarkoWeaponComponent::USarkoWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USarkoWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USarkoWeaponComponent, AmmoInMagazine);
	DOREPLIFETIME(USarkoWeaponComponent, bReloading);
}

void USarkoWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	AmmoInMagazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
}

void USarkoWeaponComponent::ResetForTest(int32 Rounds)
{
	AmmoInMagazine = Rounds;
	bReloading = false;
}

void USarkoWeaponComponent::ServerFire(FVector Origin, FVector Direction)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority() || !CanFire())
	{
		return;
	}

	--AmmoInMagazine;

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// Collect plausible targets, then let the pure helper decide the nudge.
	TArray<FVector> Candidates;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (Other == Owner)
		{
			continue;
		}
		if (const USarkoHealthComponent* Health = Other->FindComponentByClass<USarkoHealthComponent>())
		{
			if (!Health->IsDead())
			{
				Candidates.Add(Other->GetActorLocation());
			}
		}
	}

	const FVector Adjusted = SarkoCombat::ApplyAimAssist(Origin, Direction, Settings.AimConeHalfAngleDegrees, Candidates);
	const FVector End = Origin + Adjusted * Settings.WeaponRangeUU;

	// Cover must stop bullets, so this is a real trace against world geometry
	// rather than a distance check.
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Pawn, Params))
	{
		return;
	}

	if (AActor* HitActor = Hit.GetActor())
	{
		if (USarkoHealthComponent* Health = HitActor->FindComponentByClass<USarkoHealthComponent>())
		{
			Health->ApplyDamage(Settings.WeaponDamage, Owner);
		}
	}
}

void USarkoWeaponComponent::StartReload()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bReloading)
	{
		return;
	}

	bReloading = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReloadTimer, this, &USarkoWeaponComponent::FinishReload,
			GetDefault<USarkoRaidSettings>()->ReloadSeconds, false);
	}
}

void USarkoWeaponComponent::FinishReload()
{
	AmmoInMagazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
	bReloading = false;
}
```

Add `#include "EngineUtils.h"` for `TActorIterator`.

- [ ] **Step 5: Implement the AI decision function and controller**

`Source/SarkoGame/AI/SarkoAIController.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "SarkoAIController.generated.h"

UENUM()
enum class ESarkoAIState : uint8
{
	Idle,
	Patrol,
	Chase,
	Shoot
};

namespace SarkoAI
{
	/**
	 * The whole AI decision, as a pure function of what the enemy can perceive.
	 * A C++ state machine rather than a Behavior Tree because BTs, Blackboards
	 * and StateTrees are all binary assets — unwritable and untestable here.
	 */
	ESarkoAIState DecideState(
		ESarkoAIState Current,
		bool bHasTarget,
		float DistanceToTarget,
		bool bHasLineOfSight,
		float HearingRadius,
		float FiringRange);
}

/** Drives one enemy pawn from the decision function above. */
UCLASS()
class ASarkoAIController : public AAIController
{
	GENERATED_BODY()

public:
	ASarkoAIController();

	virtual void Tick(float DeltaSeconds) override;

	ESarkoAIState GetState() const { return State; }

private:
	APawn* FindNearestLivingPlayer() const;

	ESarkoAIState State = ESarkoAIState::Idle;
	float FireCooldown = 0.f;
	FVector PatrolTarget = FVector::ZeroVector;
};
```

`Source/SarkoGame/AI/SarkoAIController.cpp`:

```cpp
#include "AI/SarkoAIController.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

ESarkoAIState SarkoAI::DecideState(
	ESarkoAIState Current,
	bool bHasTarget,
	float DistanceToTarget,
	bool bHasLineOfSight,
	float HearingRadius,
	float FiringRange)
{
	// Nothing to react to: wander.
	if (!bHasTarget || DistanceToTarget > HearingRadius)
	{
		return ESarkoAIState::Patrol;
	}

	// Seen and close enough to hit: shoot. Otherwise close the distance.
	if (bHasLineOfSight && DistanceToTarget <= FiringRange)
	{
		return ESarkoAIState::Shoot;
	}
	return ESarkoAIState::Chase;
}

ASarkoAIController::ASarkoAIController()
{
	PrimaryActorTick.bCanEverTick = true;
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

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	FireCooldown = FMath::Max(0.f, FireCooldown - DeltaSeconds);

	APawn* Target = FindNearestLivingPlayer();
	const float Distance = Target ? FVector::Dist(Self->GetActorLocation(), Target->GetActorLocation()) : 0.f;
	const bool bLineOfSight = Target ? LineOfSightTo(Target) : false;

	State = SarkoAI::DecideState(State, Target != nullptr, Distance, bLineOfSight,
		Settings.EnemyHearingRadiusUU, Settings.WeaponRangeUU * 0.5f);

	switch (State)
	{
	case ESarkoAIState::Patrol:
		if (FVector::Dist2D(Self->GetActorLocation(), PatrolTarget) < 200.f)
		{
			const float Extent = Settings.MapExtent * 0.8f;
			PatrolTarget = FVector(FMath::FRandRange(-Extent, Extent), FMath::FRandRange(-Extent, Extent), Self->GetActorLocation().Z);
		}
		MoveToLocation(PatrolTarget, 100.f);
		break;

	case ESarkoAIState::Chase:
		if (Target)
		{
			MoveToActor(Target, 200.f);
		}
		break;

	case ESarkoAIState::Shoot:
		StopMovement();
		if (Target && FireCooldown <= 0.f)
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
					FireCooldown = Settings.EnemyFireIntervalSeconds;
				}
			}
		}
		break;

	default:
		break;
	}
}
```

- [ ] **Step 6: Implement the enemy pawn**

`Source/SarkoGame/AI/SarkoEnemyCharacter.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "SarkoEnemyCharacter.generated.h"

class USarkoHealthComponent;
class USarkoWeaponComponent;

/** One enemy archetype — the slice deliberately has exactly one. */
UCLASS()
class ASarkoEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASarkoEnemyCharacter();

	virtual void BeginPlay() override;

protected:
	void HandleDeath(AActor* Killer);

	UPROPERTY(VisibleAnywhere, Category = "Health")
	TObjectPtr<USarkoHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<USarkoWeaponComponent> WeaponComponent;
};
```

`Source/SarkoGame/AI/SarkoEnemyCharacter.cpp`:

```cpp
#include "AI/SarkoEnemyCharacter.h"

#include "AI/SarkoAIController.h"
#include "Combat/SarkoWeapon.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Pawn/SarkoHealthComponent.h"

ASarkoEnemyCharacter::ASarkoEnemyCharacter()
{
	AIControllerClass = ASarkoAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	HealthComponent = CreateDefaultSubobject<USarkoHealthComponent>(TEXT("Health"));
	WeaponComponent = CreateDefaultSubobject<USarkoWeaponComponent>(TEXT("Weapon"));

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->MaxWalkSpeed = 340.f;
}

void ASarkoEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnDied.AddUObject(this, &ASarkoEnemyCharacter::HandleDeath);
	}
}

void ASarkoEnemyCharacter::HandleDeath(AActor* Killer)
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The corpse stays as a marker; loot arrives in the next plan.
	SetLifeSpan(60.f);
}
```

- [ ] **Step 7: Give the player a weapon and fire on aim release**

Add to `ASarkoCharacter`'s header:

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<USarkoWeaponComponent> WeaponComponent;

	/** Called by the controller the moment the aim thumb lifts. */
	void RequestFire();

private:
	UFUNCTION(Server, Reliable)
	void ServerRequestFire(FVector Origin, FVector Direction);
```

and to the `.cpp`:

```cpp
	WeaponComponent = CreateDefaultSubobject<USarkoWeaponComponent>(TEXT("Weapon"));
```

```cpp
void ASarkoCharacter::RequestFire()
{
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

	const FVector Origin = GetMuzzleLocation();
	const FVector Direction = FVector(AimDirection);

	if (HasAuthority())
	{
		WeaponComponent->ServerFire(Origin, Direction);
		return;
	}
	// Effects can be drawn locally right away; only the server decides the hit.
	ServerRequestFire(Origin, Direction);
}

void ASarkoCharacter::ServerRequestFire_Implementation(FVector Origin, FVector Direction)
{
	// The server re-derives the origin from its own copy of the pawn so a
	// client cannot shoot from an arbitrary position.
	WeaponComponent->ServerFire(GetMuzzleLocation(), Direction);
}
```

In `ASarkoPlayerController::PlayerTick`, after updating the sticks, forward the release:

```cpp
	if (bAimReleasedThisFrame)
	{
		Pawn->RequestFire();
	}
```

- [ ] **Step 8: Spawn enemies from the layout**

In `ASarkoRaidGameMode::StartPlay`, after the map is spawned:

```cpp
		for (const FVector& Spawn : CachedLayout.EnemySpawns)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			World->SpawnActor<ASarkoEnemyCharacter>(ASarkoEnemyCharacter::StaticClass(), Spawn, FRotator::ZeroRotator, Params);
		}
```

with `#include "AI/SarkoEnemyCharacter.h"`.

- [ ] **Step 9: Run the whole suite**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: eleven tests performed, zero failed.

- [ ] **Step 10: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): hitscan weapon with server-side aim assist and a C++ AI enemy"
```

---

## Manual verification — the part no agent can do

Automation proves the rules hold. It cannot answer the question this slice exists to ask. After Task 6, run the game and judge:

1. **Does the character go where your thumb says?** Push the left stick in eight directions and watch for drift or a wrong axis.
2. **Can you hit anything?** Drag the right thumb, watch the cone, release. If landing shots feels random rather than earned, the cone or the assist angle is wrong — change `AimConeHalfAngleDegrees` in `Config/DefaultGame.ini` and try again.
3. **Is the cone readable?** Look at it against a bright floor and a dark one.
4. **Is being shot at legible?** Can you tell where fire is coming from before you die?
5. **Does anything feel unfair?** Especially the enemy's `EnemyFireIntervalSeconds` and `WeaponDamage`.

To play it, open the project in the editor and press Play, or run:

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" "$(pwd)/SarkoGame/SarkoGame.uproject" -game -windowed -ResX=1280 -ResY=720
```

Touch input needs a touchscreen or the editor's touch emulation; a mouse click registers as touch index 0, which exercises one stick at a time.

**If combat cannot be made to feel controllable after tuning, that is a finding, not a failure** — spec §9 records the fallback: radiation and the environment become the primary threat, and the damage system already built supports that unchanged.

---

## Self-Review

**Spec coverage.** §9's control scheme: floating sticks (Task 4), drag-to-aim with a ground cone (Task 4), no auto-fire — release fires (Task 4/6), server-side aim magnetism identical for all (Task 6), information along the top only (Task 4). §10's trust boundaries: movement client-predicted and server-validated (Task 3), shots and damage server-only (Tasks 5, 6). Replication from day one throughout. The AI is a C++ state machine, not a Behavior Tree (Task 6). No binary assets anywhere.

**Deliberately out of scope**, deferred to the two follow-on plans and recorded so nobody thinks they were forgotten: inventory and weight, the safe pocket, loot containers, the extraction zone, the raid outcome report, CSV data tables, `IStashStorage` and the HTTP link to `sarko-api`, the shelter and garage, the iOS device build.

**Known limitation to carry forward.** Tasks 3-6 include code that only runs with a world — pawn possession, traces against real geometry, navigation. The headless tests here cover the *pure* parts (stick mapping, layout generation, aim assist, damage rules, AI decisions), which is where the logic errors actually live, but they do not prove the actors wire together. That gap closes in the manual pass above, and the follow-on plan for extraction should add a functional test that spawns a world, possesses a pawn and fires a real shot end to end.

---

## Next

After the raid core is playable and judged, the two remaining plans in order: **loot and extraction**, then **meta and device**. The backend those will talk to is already live at `https://sarko-api-production.up.railway.app`, so `IStashStorage` can be written against a real API from its first line rather than against a mock.
