# Bridge Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace procedural map generation with the hand-designed Bridge sector, loaded from a text data file, and give the project the overview-screenshot tool that makes designing a 400×400 m map by coordinates possible at all.

**Architecture:** `FSarkoMapLayout` stays exactly as it is in memory; only its source changes, from `BuildLayout(seed)` to `LoadLayout("bridge")`. Everything downstream — geometry spawning, pawn placement, bot spawning — is untouched. The map file is JSON, parsed by a pure function that takes a string and returns a layout, so the schema is unit-testable with no world. The seed keeps working, but for loot rather than geometry.

**Tech Stack:** UE 5.8, C++ only, `Build.sh` + `UnrealEditor-Cmd`, automation tests under `-nullrhi`, visual verification under `-RenderOffscreen`.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-30-sarko-bridge-map-design.md`. Section references below point at it.
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`.
- **`DefaultBuildSettings` stays `BuildSettingsVersion.V7`**; `SarkoGame.Build.cs` keeps `PrivateIncludePaths.Add(ModuleDirectory)`, `"DeveloperSettings"`, `"AIModule"`, `"NavigationSystem"`. This task adds `"Json"` and `"JsonUtilities"`.
- **Create no binary assets.** No `.uasset`, `.umap`, Blueprint, UMG widget, Enhanced Input action, DataTable, Behavior Tree. C++, `.ini` and `.json` only. Referencing an existing engine asset by path is fine.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code** — `UnrealEditor-Cmd` exits 0 even having run zero tests, and the script takes its verdict from the engine log.
- **`GameMapsSettings` lives in `DefaultEngine.ini`** (that engine class is `config=Engine`); `USarkoRaidSettings` is `config=Game` and lives in `DefaultGame.ini`. Misplacing either silently does nothing.
- The automation-test flag spelling that compiles is `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`.
- **`SetMobility(Static)` before `SetStaticMesh` silently no-ops after BeginPlay.** Existing `SpawnBox` goes Movable → assign → Static for exactly this reason; keep that order in any new spawn path.
- Exactly **one** directional light: the mobile forward shading path supports a single one.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything discarding working-tree changes — a subagent on this project already destroyed an uncommitted file that way.
- Extraction zones are data only in this plan. The extraction *mechanic* belongs to the next plan; this plan puts the points in the file and validates their placement.

## File Structure

```
SarkoGame/
├── Data/Maps/bridge.json               # the map itself — the deliverable
├── Config/DefaultGame.ini              # map id + raid duration (extend)
└── Source/SarkoGame/
    ├── SarkoGame.Build.cs              # + Json, JsonUtilities
    ├── Map/
    │   ├── SarkoMapBuilder.h/.cpp      # SpawnLayout stays; BuildLayout goes
    │   ├── SarkoMapDefinition.h/.cpp    # NEW: JSON -> FSarkoMapLayout, pure
    │   └── SarkoMapKinds.h/.cpp         # NEW: "kind" -> mesh + default extent
    ├── Debug/
    │   └── SarkoOverviewShot.h/.cpp     # NEW: whole-sector camera + screenshot
    └── Tests/
        ├── MapDefinitionTest.cpp        # NEW: schema parsing, pure
        └── BridgeMapTest.cpp            # NEW: invariants on the real file
```

The parse step is separated from the spawn step for the same reason the procedural generator was: a pure function taking text and returning a layout can be tested headlessly, while spawning needs a world. `SarkoMapKinds` is separate so adding a prop type is one table entry, not a change to the parser.

---

### Task 1: Overview screenshot tool

First, because every later task in this plan is verified by looking at the result. Authoring a 400×400 m map by typing coordinates is only possible if the whole sector can be seen in one frame.

**Files:**
- Create: `Source/SarkoGame/Debug/SarkoOverviewShot.h`, `.cpp`
- Modify: `Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp` (expose the console command)
- Modify: `Scripts/` — add `Scripts/overview-shot.sh`

**Interfaces:**
- Consumes: `USarkoRaidSettings` (for `MapExtent`), `UWorld`.
- Produces: console command `SarkoOverview` on the player controller; `SarkoDebug::FrameWholeSector(APlayerController& PC, float ExtentUU)` which positions the view to fit the sector and returns the camera height used; `Scripts/overview-shot.sh` which launches the game offscreen, fires the command, waits for the PNG and prints its path.

- [ ] **Step 1: Write the failing framing test**

`Source/SarkoGame/Tests/MapDefinitionTest.cpp` (this file gains the schema tests in Task 2; it starts here with the pure framing maths):

```cpp
#include "Misc/AutomationTest.h"

#include "Debug/SarkoOverviewShot.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOverviewHeightFitsSector,
	"Sarko.Debug.OverviewHeightFitsSector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOverviewHeightFitsSector::RunTest(const FString& Parameters)
{
	// A 90-degree vertical FOV sees exactly as far across as it is high, so a
	// sector of half-extent E needs at least E of height, and more for a
	// narrower FOV. Getting this wrong means the overview crops the map, which
	// is worse than useless: it looks like the map ends there.
	const float Height90 = SarkoDebug::HeightToFitSector(/*ExtentUU*/ 20000.f, /*VerticalFOVDegrees*/ 90.f);
	TestTrue(TEXT("a 90 degree FOV needs at least the sector's half-extent in height"), Height90 >= 20000.f);

	const float Height60 = SarkoDebug::HeightToFitSector(20000.f, 60.f);
	TestTrue(TEXT("a narrower FOV must pull the camera further back"), Height60 > Height90);

	// Margin: the frame should not end exactly at the sector edge, or the
	// outermost cover touches the screen border and cannot be judged.
	TestTrue(TEXT("there is headroom beyond the sector edge"), Height90 > 20000.f * 1.05f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Debug`
Expected: BUILD FAILED — `'Debug/SarkoOverviewShot.h' file not found`.

- [ ] **Step 3: Implement the framing maths and the command**

`Source/SarkoGame/Debug/SarkoOverviewShot.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace SarkoDebug
{
	/**
	 * Camera height that fits a square sector of the given half-extent into the
	 * frame, with a margin so the outermost geometry is not flush against the
	 * screen edge. Pure trigonometry, no world — which is why it can be tested.
	 */
	float HeightToFitSector(float ExtentUU, float VerticalFOVDegrees);

	/**
	 * Points the view straight down from above the sector's centre, high enough
	 * to see all of it. Used only by the overview screenshot: this is a design
	 * tool, not gameplay.
	 */
	void FrameWholeSector(APlayerController& Controller, float ExtentUU);
}
```

`Source/SarkoGame/Debug/SarkoOverviewShot.cpp`:

```cpp
#include "Debug/SarkoOverviewShot.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Headroom beyond the sector edge, so outer geometry is not flush to the border. */
	constexpr float FrameMargin = 1.15f;

	/** Straight down. */
	const FRotator LookDown(-90.f, 0.f, 0.f);
}

float SarkoDebug::HeightToFitSector(float ExtentUU, float VerticalFOVDegrees)
{
	// Half the frame subtends half the FOV, so height = halfExtent / tan(FOV/2).
	const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Clamp(VerticalFOVDegrees, 10.f, 170.f) * 0.5f);
	const float Height = ExtentUU / FMath::Max(FMath::Tan(HalfFOVRadians), KINDA_SMALL_NUMBER);
	return Height * FrameMargin;
}

void SarkoDebug::FrameWholeSector(APlayerController& Controller, float ExtentUU)
{
	float FOV = 90.f;
	if (const APlayerCameraManager* Camera = Controller.PlayerCameraManager)
	{
		FOV = Camera->GetFOVAngle();
	}

	const float Height = HeightToFitSector(ExtentUU, FOV);

	// Detach from the pawn so the pawn's own top-down boom does not fight this.
	Controller.SetViewTargetWithBlend(&Controller, 0.f);
	Controller.SetControlRotation(LookDown);
	if (APlayerCameraManager* Camera = Controller.PlayerCameraManager)
	{
		Camera->SetActorLocationAndRotation(FVector(0.f, 0.f, Height), LookDown);
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoOverview: framing %.0f uu sector from %.0f uu up at %.0f deg FOV"),
		ExtentUU, Height, FOV);
}
```

- [ ] **Step 4: Expose it as a console command**

Add to `Core/SarkoPlayerController.h`, inside the `#if !UE_BUILD_SHIPPING` block that already holds the desktop test input:

```cpp
	/**
	 * Frames the whole sector from above and takes a screenshot. This is the
	 * design loop for a hand-authored map: edit the data file, run offscreen,
	 * look at the frame, adjust. Without it the layout is written blind.
	 */
	UFUNCTION(Exec)
	void SarkoOverview();
```

and to `Core/SarkoPlayerController.cpp`, inside the same guard:

```cpp
void ASarkoPlayerController::SarkoOverview()
{
	SarkoDebug::FrameWholeSector(*this, GetDefault<USarkoRaidSettings>()->MapExtent);

	// One frame later, so the new camera position is what gets captured.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1600x1600"), /*bWriteToLog*/ true);
	}), 0.25f, false);
}
```

with `#include "Debug/SarkoOverviewShot.h"` and `#include "Core/SarkoRaidSettings.h"`.

A square capture is deliberate: the sector is square, and a 16:9 frame would waste half the pixels on empty ground.

- [ ] **Step 5: Add the driver script**

`Scripts/overview-shot.sh`:

```bash
#!/usr/bin/env bash
#
# Launches the game with a real renderer but no window, frames the whole
# sector from above, screenshots it, and prints the PNG path.
#
# This is the design loop for a hand-authored map. -nullrhi renders nothing,
# so the automation suite cannot see a map at all; -RenderOffscreen gives a
# real Metal RHI with no window, which is what makes the frame available.
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${OVERVIEW_TIMEOUT:-240}"

rm -rf "$SHOT_DIR"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" /Engine/Maps/Entry \
	-game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=1600 \
	-ExecCmds="t.MaxFPS 10, SarkoOverview" > /dev/null 2>&1 &
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

- [ ] **Step 6: Verify the tool end to end**

Run: `cd SarkoGame && chmod +x Scripts/overview-shot.sh && ./Scripts/overview-shot.sh`
Expected: prints a path to a PNG. Read that PNG — the whole sector should be in frame, not cropped, with the ground filling most of it.

- [ ] **Step 7: Run the suite and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: all existing tests plus `Sarko.Debug.OverviewHeightFitsSector`, zero failed.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): overview screenshot tool for designing a map by coordinates"
```

---

### Task 2: Map definition schema and loader

**Files:**
- Create: `Source/SarkoGame/Map/SarkoMapDefinition.h`, `.cpp`
- Modify: `Source/SarkoGame/SarkoGame.Build.cs` (add `Json`, `JsonUtilities`)
- Test: `Source/SarkoGame/Tests/MapDefinitionTest.cpp` (extend)

**Interfaces:**
- Consumes: `FSarkoMapLayout`, `FSarkoCoverBlock` from `Map/SarkoMapBuilder.h`.
- Produces: `FSarkoMapProp { FName Kind; FVector Location; float Yaw; }`; `FSarkoLootContainerSpot { FVector Location; FName Tier; }`; `FSarkoExtractionSpot { FVector Location; float RadiusUU; FString Name; }`; `FSarkoBotSpot { FVector Location; FName Zone; }`; `FSarkoMapDefinition { FString Id; float ExtentUU; float RaidDurationSeconds; TArray<FSarkoCoverBlock> Blocks; TArray<FSarkoMapProp> Props; TArray<FSarkoLootContainerSpot> Containers; TArray<FTransform> PlayerSpawns; TArray<FSarkoBotSpot> BotSpawns; TArray<FSarkoExtractionSpot> Extractions; }`; `bool SarkoMap::ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError)` — pure; `FSarkoMapLayout SarkoMap::ToLayout(const FSarkoMapDefinition&)` — pure; `bool SarkoMap::LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& Out, FString& OutError)`.

Parsing is split from disk access so the schema can be tested from a string literal with no file, and split from `ToLayout` so the conversion to the existing in-memory type is its own reviewable step.

- [ ] **Step 1: Write the failing schema tests**

Append to `Source/SarkoGame/Tests/MapDefinitionTest.cpp`:

```cpp
#include "Map/SarkoMapDefinition.h"

namespace
{
	const FString MinimalMapJson = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"blocks": [ { "kind": "wall", "pos": [-4200, 1800, 0], "yaw": 90, "extent": [800, 120, 200] } ],
		"props": [ { "kind": "car_wreck", "pos": [1200, -300, 0], "yaw": 35 } ],
		"containers": [ { "pos": [1250, -280, 0], "tier": "military" } ],
		"playerSpawns": [ { "pos": [-16000, 17000, 100], "yaw": 135 } ],
		"botSpawns": [ { "pos": [8000, -12000, 100], "zone": "deep" } ],
		"extractions": [ { "pos": [-14000, 19000, 0], "radiusUU": 400, "name": "North path" } ]
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapDefinitionParses,
	"Sarko.Map.DefinitionParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapDefinitionParses::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("a well-formed map parses"), SarkoMap::ParseDefinition(MinimalMapJson, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());

	TestEqual(TEXT("id survives"), Definition.Id, FString(TEXT("test")));
	TestEqual(TEXT("extent survives"), Definition.ExtentUU, 20000.f);
	TestEqual(TEXT("raid duration survives"), Definition.RaidDurationSeconds, 900.f);
	TestEqual(TEXT("one block"), Definition.Blocks.Num(), 1);
	TestEqual(TEXT("one prop"), Definition.Props.Num(), 1);
	TestEqual(TEXT("one container"), Definition.Containers.Num(), 1);
	TestEqual(TEXT("one player spawn"), Definition.PlayerSpawns.Num(), 1);
	TestEqual(TEXT("one bot spawn"), Definition.BotSpawns.Num(), 1);
	TestEqual(TEXT("one extraction"), Definition.Extractions.Num(), 1);

	// The block's numbers must land in the right fields, not merely be present.
	TestTrue(TEXT("block position is read in order x,y,z"),
		Definition.Blocks[0].Location.Equals(FVector(-4200.f, 1800.f, 0.f), 0.01f));
	TestEqual(TEXT("block yaw is read"), Definition.Blocks[0].Rotation.Yaw, 90.f);
	TestTrue(TEXT("block extent is read"),
		Definition.Blocks[0].Extent.Equals(FVector(800.f, 120.f, 200.f), 0.01f));
	TestEqual(TEXT("extraction name is read"), Definition.Extractions[0].Name, FString(TEXT("North path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapDefinitionRejectsBadInput,
	"Sarko.Map.DefinitionRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapDefinitionRejectsBadInput::RunTest(const FString& Parameters)
{
	// A map file is hand-edited, so it will be broken sooner or later. Every
	// rejection must name the problem: a silent empty map is the worst outcome,
	// because the game launches and simply has nothing in it.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json at all"),        TEXT("{{{") },
		{ TEXT("missing id"),             TEXT(R"({"extentUU":20000,"raidDurationSeconds":900})") },
		{ TEXT("missing extent"),         TEXT(R"({"id":"x","raidDurationSeconds":900})") },
		{ TEXT("negative extent"),        TEXT(R"({"id":"x","extentUU":-5,"raidDurationSeconds":900})") },
		{ TEXT("no player spawn"),        TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[]})") },
		{ TEXT("position not a triple"),  TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[1,2],"yaw":0}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		const bool bParsed = SarkoMap::ParseDefinition(Case.Value, Definition, Error);
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key), bParsed);
		TestFalse(FString::Printf(TEXT("error message is not empty: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDefinitionConvertsToLayout,
	"Sarko.Map.DefinitionConvertsToLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDefinitionConvertsToLayout::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::ParseDefinition(MinimalMapJson, Definition, Error))
	{
		AddError(FString::Printf(TEXT("fixture failed to parse: %s"), *Error));
		return false;
	}

	// The whole point of the seam: the existing in-memory type is unchanged, so
	// everything downstream of it keeps working untouched.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TestEqual(TEXT("extent carries over"), Layout.Extent, Definition.ExtentUU);
	TestEqual(TEXT("cover carries over"), Layout.Cover.Num(), Definition.Blocks.Num());
	TestEqual(TEXT("player starts carry over"), Layout.PlayerStarts.Num(), Definition.PlayerSpawns.Num());
	TestEqual(TEXT("bot spawns carry over"), Layout.EnemySpawns.Num(), Definition.BotSpawns.Num());
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: BUILD FAILED — `'Map/SarkoMapDefinition.h' file not found`.

- [ ] **Step 3: Add the JSON modules**

In `Source/SarkoGame/SarkoGame.Build.cs`, extend the dependency list with `"Json"` and `"JsonUtilities"`, leaving every existing entry in place.

- [ ] **Step 4: Implement the definition types**

`Source/SarkoGame/Map/SarkoMapDefinition.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapBuilder.h"

#include "SarkoMapDefinition.generated.h"

/** A placeable object: a wreck, a fuel pump, a freight car. Kind picks the mesh. */
USTRUCT()
struct FSarkoMapProp
{
	GENERATED_BODY()

	UPROPERTY()
	FName Kind;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;
};

/** Where a lootable container sits, and how good its contents are. */
USTRUCT()
struct FSarkoLootContainerSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Tier;
};

/** A place the player can leave the raid from. Mechanic lands in a later plan. */
USTRUCT()
struct FSarkoExtractionSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float RadiusUU = 400.f;

	UPROPERTY()
	FString Name;
};

/** A bot spawn, tagged with the risk zone it belongs to. */
USTRUCT()
struct FSarkoBotSpot
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FName Zone;
};

/**
 * A whole hand-authored map, exactly as it appears in the data file.
 *
 * Distinct from FSarkoMapLayout on purpose: the definition is what a designer
 * writes and can include things the spawner does not care about (extraction
 * names, container tiers, zone tags), while the layout is the reduced form the
 * existing spawn code already consumes.
 */
USTRUCT()
struct FSarkoMapDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	float ExtentUU = 0.f;

	UPROPERTY()
	float RaidDurationSeconds = 0.f;

	UPROPERTY()
	TArray<FSarkoCoverBlock> Blocks;

	UPROPERTY()
	TArray<FSarkoMapProp> Props;

	UPROPERTY()
	TArray<FSarkoLootContainerSpot> Containers;

	UPROPERTY()
	TArray<FTransform> PlayerSpawns;

	UPROPERTY()
	TArray<FSarkoBotSpot> BotSpawns;

	UPROPERTY()
	TArray<FSarkoExtractionSpot> Extractions;
};

namespace SarkoMap
{
	/**
	 * Parses a map file. Pure: text in, definition out, no disk and no world,
	 * which is what lets the schema be tested from string literals.
	 *
	 * Every failure sets OutError to something that names the problem. A map
	 * file is hand-edited, so it will be broken eventually, and the worst
	 * outcome is a silent empty map — the game launches with nothing in it and
	 * no clue why.
	 */
	bool ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError);

	/** Reduces a definition to the layout the existing spawn code consumes. */
	FSarkoMapLayout ToLayout(const FSarkoMapDefinition& Definition);

	/** Reads Data/Maps/<MapId>.json from the project directory. */
	bool LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError);
}
```

- [ ] **Step 5: Implement the parser**

`Source/SarkoGame/Map/SarkoMapDefinition.cpp`:

```cpp
#include "Map/SarkoMapDefinition.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Reads a ["x","y","z"] array into a vector, naming the field on failure. */
	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array) || !Array)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an array"), *Field);
			return false;
		}
		if (Array->Num() != 3)
		{
			OutError = FString::Printf(TEXT("'%s' must have exactly 3 numbers, found %d"), *Field, Array->Num());
			return false;
		}
		Out = FVector(
			static_cast<float>((*Array)[0]->AsNumber()),
			static_cast<float>((*Array)[1]->AsNumber()),
			static_cast<float>((*Array)[2]->AsNumber()));
		return true;
	}
}

bool SarkoMap::ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	OutDefinition = FSarkoMapDefinition();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	if (!Root->TryGetStringField(TEXT("id"), OutDefinition.Id) || OutDefinition.Id.IsEmpty())
	{
		OutError = TEXT("'id' is missing or empty");
		return false;
	}

	double Extent = 0.0;
	if (!Root->TryGetNumberField(TEXT("extentUU"), Extent) || Extent <= 0.0)
	{
		OutError = TEXT("'extentUU' is missing or not positive");
		return false;
	}
	OutDefinition.ExtentUU = static_cast<float>(Extent);

	double Duration = 0.0;
	if (!Root->TryGetNumberField(TEXT("raidDurationSeconds"), Duration) || Duration <= 0.0)
	{
		OutError = TEXT("'raidDurationSeconds' is missing or not positive");
		return false;
	}
	OutDefinition.RaidDurationSeconds = static_cast<float>(Duration);

	// blocks
	const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
	if (Root->TryGetArrayField(TEXT("blocks"), Blocks) && Blocks)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Blocks)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'blocks' contains a non-object entry");
				return false;
			}
			FSarkoCoverBlock Block;
			if (!ReadVector(*Object, TEXT("pos"), Block.Location, OutError) ||
				!ReadVector(*Object, TEXT("extent"), Block.Extent, OutError))
			{
				OutError = FString::Printf(TEXT("block: %s"), *OutError);
				return false;
			}
			Block.Rotation = FRotator(0.f, static_cast<float>((*Object)->GetNumberField(TEXT("yaw"))), 0.f);
			OutDefinition.Blocks.Add(Block);
		}
	}

	// props
	const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
	if (Root->TryGetArrayField(TEXT("props"), Props) && Props)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Props)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'props' contains a non-object entry");
				return false;
			}
			FSarkoMapProp Prop;
			if (!ReadVector(*Object, TEXT("pos"), Prop.Location, OutError))
			{
				OutError = FString::Printf(TEXT("prop: %s"), *OutError);
				return false;
			}
			Prop.Kind = FName(*(*Object)->GetStringField(TEXT("kind")));
			Prop.Yaw = static_cast<float>((*Object)->GetNumberField(TEXT("yaw")));
			OutDefinition.Props.Add(Prop);
		}
	}

	// containers
	const TArray<TSharedPtr<FJsonValue>>* Containers = nullptr;
	if (Root->TryGetArrayField(TEXT("containers"), Containers) && Containers)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Containers)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'containers' contains a non-object entry");
				return false;
			}
			FSarkoLootContainerSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("container: %s"), *OutError);
				return false;
			}
			Spot.Tier = FName(*(*Object)->GetStringField(TEXT("tier")));
			OutDefinition.Containers.Add(Spot);
		}
	}

	// playerSpawns — at least one, or there is nowhere to put the player
	const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
	if (Root->TryGetArrayField(TEXT("playerSpawns"), Spawns) && Spawns)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Spawns)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'playerSpawns' contains a non-object entry");
				return false;
			}
			FVector Location;
			if (!ReadVector(*Object, TEXT("pos"), Location, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawn: %s"), *OutError);
				return false;
			}
			const FRotator Rotation(0.f, static_cast<float>((*Object)->GetNumberField(TEXT("yaw"))), 0.f);
			OutDefinition.PlayerSpawns.Add(FTransform(Rotation, Location));
		}
	}
	if (OutDefinition.PlayerSpawns.Num() == 0)
	{
		OutError = TEXT("'playerSpawns' must contain at least one entry");
		return false;
	}

	// botSpawns
	const TArray<TSharedPtr<FJsonValue>>* Bots = nullptr;
	if (Root->TryGetArrayField(TEXT("botSpawns"), Bots) && Bots)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Bots)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'botSpawns' contains a non-object entry");
				return false;
			}
			FSarkoBotSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawn: %s"), *OutError);
				return false;
			}
			Spot.Zone = FName(*(*Object)->GetStringField(TEXT("zone")));
			OutDefinition.BotSpawns.Add(Spot);
		}
	}

	// extractions
	const TArray<TSharedPtr<FJsonValue>>* Extractions = nullptr;
	if (Root->TryGetArrayField(TEXT("extractions"), Extractions) && Extractions)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Extractions)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'extractions' contains a non-object entry");
				return false;
			}
			FSarkoExtractionSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("extraction: %s"), *OutError);
				return false;
			}
			Spot.RadiusUU = static_cast<float>((*Object)->GetNumberField(TEXT("radiusUU")));
			(*Object)->TryGetStringField(TEXT("name"), Spot.Name);
			OutDefinition.Extractions.Add(Spot);
		}
	}

	return true;
}

FSarkoMapLayout SarkoMap::ToLayout(const FSarkoMapDefinition& Definition)
{
	FSarkoMapLayout Layout;
	Layout.Extent = Definition.ExtentUU;
	Layout.Cover = Definition.Blocks;

	Layout.PlayerStarts.Reserve(Definition.PlayerSpawns.Num());
	for (const FTransform& Spawn : Definition.PlayerSpawns)
	{
		Layout.PlayerStarts.Add(Spawn.GetLocation());
	}

	Layout.EnemySpawns.Reserve(Definition.BotSpawns.Num());
	for (const FSarkoBotSpot& Spot : Definition.BotSpawns)
	{
		Layout.EnemySpawns.Add(Spot.Location);
	}

	return Layout;
}

bool SarkoMap::LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Maps") / (MapId + TEXT(".json"));

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read map file at %s"), *Path);
		return false;
	}
	if (!ParseDefinition(Json, OutDefinition, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}
```

- [ ] **Step 6: Run the tests and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: the three new definition tests pass alongside the existing map tests.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): JSON map definition schema and loader"
```

---

### Task 3: Prop kinds and spawning from a definition

**Files:**
- Create: `Source/SarkoGame/Map/SarkoMapKinds.h`, `.cpp`
- Modify: `Source/SarkoGame/Map/SarkoMapBuilder.h`, `.cpp` (spawn props and containers)
- Modify: `Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (load instead of generate)
- Modify: `Config/DefaultGame.ini` (map id)
- Test: `Source/SarkoGame/Tests/MapDefinitionTest.cpp` (extend with kind-table tests)

**Interfaces:**
- Consumes: `FSarkoMapDefinition`, `FSarkoMapProp`.
- Produces: `FSarkoPropKind { FSoftObjectPath Mesh; FVector Extent; bool bBlocksMovement; }`; `bool SarkoMap::FindPropKind(FName Kind, FSarkoPropKind& Out)`; `void SarkoMap::SpawnProps(UWorld&, const FSarkoMapDefinition&)`; and `USarkoRaidSettings::MapId` (an `FName`, default `bridge`).

Kinds live in one table so adding a prop type is a table entry rather than a parser change. Every mesh is an engine primitive referenced by path — this project authors no assets, so a "car wreck" is a scaled, rotated box until real art arrives at the same coordinates.

- [ ] **Step 1: Write the failing kind-table test**

Append to `Source/SarkoGame/Tests/MapDefinitionTest.cpp`:

```cpp
#include "Map/SarkoMapKinds.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropKindsAreComplete,
	"Sarko.Map.PropKindsAreComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropKindsAreComplete::RunTest(const FString& Parameters)
{
	// Every kind the Bridge map uses must resolve, or that prop silently does
	// not appear and the map has a hole in it that no test would otherwise see.
	const TArray<FName> UsedKinds = {
		TEXT("wall"), TEXT("car_wreck"), TEXT("bus"), TEXT("house"),
		TEXT("fuel_pump"), TEXT("freight_car"), TEXT("water_tower"),
		TEXT("sandbag"), TEXT("crate"), TEXT("pipe"), TEXT("bridge_deck")
	};

	for (const FName& Kind : UsedKinds)
	{
		FSarkoPropKind Resolved;
		const bool bFound = SarkoMap::FindPropKind(Kind, Resolved);
		TestTrue(FString::Printf(TEXT("kind '%s' resolves"), *Kind.ToString()), bFound);
		if (bFound)
		{
			TestTrue(FString::Printf(TEXT("kind '%s' has a positive extent"), *Kind.ToString()),
				Resolved.Extent.GetMin() > 0.f);
			TestTrue(FString::Printf(TEXT("kind '%s' names a mesh"), *Kind.ToString()),
				Resolved.Mesh.IsValid() || !Resolved.Mesh.ToString().IsEmpty());
		}
	}

	FSarkoPropKind Unknown;
	TestFalse(TEXT("an unknown kind does not resolve"), SarkoMap::FindPropKind(TEXT("nonsense"), Unknown));
	return true;
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: BUILD FAILED — `'Map/SarkoMapKinds.h' file not found`.

- [ ] **Step 3: Implement the kind table**

`Source/SarkoGame/Map/SarkoMapKinds.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "SarkoMapKinds.generated.h"

/**
 * How one prop kind is built. Every mesh is an engine primitive referenced by
 * path: this project authors no assets, so a "car wreck" is a scaled box until
 * real art arrives at the same coordinates.
 */
USTRUCT()
struct FSarkoPropKind
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Mesh;

	/** Half-extents in unreal units. The pawn is ~176 uu tall for scale. */
	UPROPERTY()
	FVector Extent = FVector(100.f);

	/** False for decoration the player can walk through, true for cover. */
	UPROPERTY()
	bool bBlocksMovement = true;
};

namespace SarkoMap
{
	/** Looks up a kind by name. False for an unknown kind — never a default. */
	bool FindPropKind(FName Kind, FSarkoPropKind& OutKind);
}
```

`Source/SarkoGame/Map/SarkoMapKinds.cpp`:

```cpp
#include "Map/SarkoMapKinds.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/**
	 * The whole prop vocabulary of the Bridge sector.
	 *
	 * Sizes are chosen against a ~176 uu tall pawn: a car wreck is chest-high
	 * cover you can shoot over, a house is tall enough to break line of sight
	 * entirely, sandbags are crouch-height. That relationship is the level
	 * design — the numbers are not arbitrary.
	 */
	const TMap<FName, FSarkoPropKind>& KindTable()
	{
		static const TMap<FName, FSarkoPropKind> Table = {
			{ TEXT("wall"),        { FSoftObjectPath(Cube),     FVector(400.f, 60.f, 140.f),  true  } },
			{ TEXT("car_wreck"),   { FSoftObjectPath(Cube),     FVector(230.f, 95.f, 75.f),   true  } },
			{ TEXT("bus"),         { FSoftObjectPath(Cube),     FVector(600.f, 130.f, 160.f), true  } },
			{ TEXT("house"),       { FSoftObjectPath(Cube),     FVector(500.f, 400.f, 300.f), true  } },
			{ TEXT("fuel_pump"),   { FSoftObjectPath(Cube),     FVector(60.f, 40.f, 110.f),   true  } },
			{ TEXT("freight_car"), { FSoftObjectPath(Cube),     FVector(700.f, 150.f, 200.f), true  } },
			{ TEXT("water_tower"), { FSoftObjectPath(Cylinder), FVector(220.f, 220.f, 700.f), true  } },
			{ TEXT("sandbag"),     { FSoftObjectPath(Cube),     FVector(180.f, 70.f, 55.f),   true  } },
			{ TEXT("crate"),       { FSoftObjectPath(Cube),     FVector(70.f, 70.f, 70.f),    true  } },
			{ TEXT("pipe"),        { FSoftObjectPath(Cylinder), FVector(90.f, 90.f, 600.f),   true  } },
			{ TEXT("bridge_deck"), { FSoftObjectPath(Cube),     FVector(900.f, 300.f, 30.f),  true  } },
		};
		return Table;
	}
}

bool SarkoMap::FindPropKind(FName Kind, FSarkoPropKind& OutKind)
{
	if (const FSarkoPropKind* Found = KindTable().Find(Kind))
	{
		OutKind = *Found;
		return true;
	}
	return false;
}
```

- [ ] **Step 4: Spawn props from the definition**

Add to `Map/SarkoMapBuilder.h`:

```cpp
	/**
	 * Spawns everything in a definition that is not already covered by
	 * SpawnLayout's floor and cover: props and container markers. Logs and
	 * skips an unknown kind rather than substituting a default, because a
	 * silently wrong prop is harder to notice than a missing one.
	 */
	void SpawnProps(UWorld& World, const struct FSarkoMapDefinition& Definition);
```

and to `Map/SarkoMapBuilder.cpp`, reusing the existing `SpawnBox` approach — including its Movable → assign → Static ordering, which exists because `SetStaticMesh` silently no-ops on a Static component after BeginPlay:

```cpp
void SarkoMap::SpawnProps(UWorld& World, const FSarkoMapDefinition& Definition)
{
	int32 Skipped = 0;

	for (const FSarkoMapProp& Prop : Definition.Props)
	{
		FSarkoPropKind Kind;
		if (!FindPropKind(Prop.Kind, Kind))
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: unknown prop kind '%s' at %s — skipped"),
				*Prop.Kind.ToString(), *Prop.Location.ToString());
			++Skipped;
			continue;
		}

		UStaticMesh* Mesh = Cast<UStaticMesh>(Kind.Mesh.TryLoad());
		if (!Mesh)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: mesh missing for kind '%s'"), *Prop.Kind.ToString());
			++Skipped;
			continue;
		}

		SpawnMeshBox(World, Mesh, Prop.Location, FRotator(0.f, Prop.Yaw, 0.f), Kind.Extent, Kind.bBlocksMovement);
	}

	// Container markers: the loot mechanic lands in a later plan, but the
	// positions must be visible now so the layout can be judged.
	UStaticMesh* CrateMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CrateMesh)
	{
		for (const FSarkoLootContainerSpot& Spot : Definition.Containers)
		{
			SpawnMeshBox(World, CrateMesh, Spot.Location, FRotator::ZeroRotator, FVector(45.f, 45.f, 35.f), true);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d props and %d containers, skipped %d"),
		Definition.Props.Num() - Skipped, Definition.Containers.Num(), Skipped);
}
```

Extract the existing lambda inside `SpawnLayout` into a file-local `SpawnMeshBox(UWorld&, UStaticMesh*, const FVector&, const FRotator&, const FVector&, bool bCollides)` so both `SpawnLayout` and `SpawnProps` use one spawn path rather than two copies of the mobility dance.

- [ ] **Step 5: Load the map instead of generating it**

Add `MapId` to `Core/SarkoRaidSettings.h`:

```cpp
	/** Which file under Data/Maps to load. */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	FName MapId = TEXT("bridge");
```

and in `Config/DefaultGame.ini` under `[/Script/SarkoGame.SarkoRaidSettings]` add `MapId=bridge`.

In `ASarkoRaidGameMode::InitGame`, replace the `BuildLayout` call with a load, keeping the existing `CachedLayout` contract that `RestartPlayer` depends on:

```cpp
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
```

Declare `FSarkoMapDefinition CachedDefinition;` alongside `CachedLayout`, and call `SarkoMap::SpawnProps(*World, CachedDefinition)` wherever `SpawnLayout` is already called — including the client path in `ASarkoRaidGameState`, so props appear on every machine exactly as the geometry does.

Also apply the map's own raid duration: in `StartPlay`, prefer `CachedDefinition.RaidDurationSeconds` over the settings default when it is positive, so a per-map duration works as the spec requires (15 minutes here, 30 on real maps).

- [ ] **Step 6: Run the suite and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: all tests pass. The map file does not exist yet, so the log will carry the loud load error — that is the correct behaviour at this step.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): prop kinds and spawning a map from its definition"
```

---

### Task 4: Author the Bridge sector

The deliverable. Every earlier task existed to make this step possible and verifiable.

**Files:**
- Create: `SarkoGame/Data/Maps/bridge.json`
- Modify: `Config/DefaultGame.ini` (`MapExtent=20000`)
- Modify: `Config/DefaultEngine.ini` (package the data directory)

**Interfaces:**
- Consumes: everything from Tasks 1-3.
- Produces: the map file itself.

Build it to the design in spec §4. Coordinates are in unreal units with the sector centred on the origin, so the playable area runs from -20000 to +20000 on both axes.

- [ ] **Step 1: Make the data directory ship**

In `Config/DefaultEngine.ini`, add:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="Data")
```

Without this the map loads in the editor and is missing from a packaged build — the kind of failure that only appears on device.

- [ ] **Step 2: Set the sector size**

In `Config/DefaultGame.ini`, set `MapExtent=20000.000000` (400 × 400 m).

- [ ] **Step 3: Author the map file**

Write `SarkoGame/Data/Maps/bridge.json` following spec §4. Work north-to-south so the risk gradient is built in the order the player experiences it:

1. **Ravine** — a line of `wall` blocks across the sector at y ≈ 0, spanning x from -20000 to +20000, with three gaps: the bridge at x ≈ 0, the pipe crossing at x ≈ -14000, the ford at x ≈ 13000. This single line is the map's spine; everything else hangs off it.
2. **Bridge** — `bridge_deck` blocks filling the central gap, with `wall` parapets either side so the crossing reads as a bridge from above and gives cover while on it.
3. **Pipe and ford** — the pipe is a narrow `pipe`-lined walkway; the ford is simply a wider gap with no cover at all. Their cost is exposure, not distance.
4. **Highway** — a north-south line of `car_wreck` props from the north edge to the bridge, in a broken traffic jam rather than a neat row: clusters of two or three with gaps, so it is cover with holes in it.
5. **Near half** — spawn area near the north-west corner, bus stop with a `bus`, a checkpoint before the bridge from `sandbag` and `crate`, ruins near the east edge.
6. **Far half** — petrol station (`house` shop plus `fuel_pump`s), village of six `house` blocks arranged so their gaps form fighting lanes, rail siding of `freight_car`s, the `water_tower` as a landmark, industrial yard in the far corner.
7. **Containers** — `tier` follows the risk gradient from §4: `junk` near spawn, `military` at the checkpoint, `good` in the village and industrial yard. At least one per zone.
8. **Player spawns** — three or four in the north-west, all clear of blocks.
9. **Bot spawns** — `zone` tagged `near`, `mid` or `deep`, weighted to the far half.
10. **Extractions** — three on the **north** edge only. Never on the east or south edges: those become passages into neighbouring sectors, and an extraction there would vanish when the map grows.

- [ ] **Step 4: Look at it and iterate**

Run: `cd SarkoGame && ./Scripts/overview-shot.sh`

Read the PNG it prints. Check against the design, and fix what is wrong in the file:
- Does the ravine actually split the sector, with exactly three crossings?
- Is the bridge visually the obvious main route?
- Is the north half readably emptier than the south?
- Do the village houses form lanes, or a solid block with no way through?
- Is anything overlapping, floating, or outside the sector?

Iterate until the overview matches the design. Attach the final overview to the report.

- [ ] **Step 5: Verify from the player's eye**

Run the game normally and take a gameplay screenshot near the spawn, then near the bridge. The overview proves the layout; these prove it is legible from the camera the player actually uses — a map that reads as a plan can still be unreadable at ground level.

- [ ] **Step 6: Run the suite and commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "feat(game): author the Bridge sector"
```

---

### Task 5: Validate the real map file

**Files:**
- Create: `Source/SarkoGame/Tests/BridgeMapTest.cpp`

**Interfaces:**
- Consumes: `SarkoMap::LoadDefinitionFromDisk`, `SarkoMap::FindPropKind`, the authored `bridge.json`.
- Produces: nothing — this task only adds tests.

These are the invariants from spec §8. A hand-edited file with a hundred entries will break, and the failure mode is a silently broken map.

- [ ] **Step 1: Write the invariant tests**

`Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Map/SarkoMapDefinition.h"
#include "Map/SarkoMapKinds.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	bool LoadBridge(FSarkoMapDefinition& Out, FString& Error)
	{
		return SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Out, Error);
	}

	bool IsInsideBlock(const FVector& Point, const TArray<FSarkoCoverBlock>& Blocks)
	{
		for (const FSarkoCoverBlock& Block : Blocks)
		{
			const FVector Local = Block.Rotation.UnrotateVector(Point - Block.Location);
			if (FMath::Abs(Local.X) <= Block.Extent.X && FMath::Abs(Local.Y) <= Block.Extent.Y)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeMapIsValid,
	"Sarko.Map.BridgeMapIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeMapIsValid::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const float Extent = Map.ExtentUU;
	TestEqual(TEXT("the sector is 400 m across"), Extent, 20000.f);
	TestEqual(TEXT("the tutorial raid is 15 minutes"), Map.RaidDurationSeconds, 900.f);

	// Everything inside the sector.
	const auto CheckInside = [this, Extent](const FVector& Point, const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s is inside the sector"), What),
			FMath::Abs(Point.X) <= Extent && FMath::Abs(Point.Y) <= Extent);
	};
	for (const FSarkoCoverBlock& Block : Map.Blocks)          { CheckInside(Block.Location, TEXT("a block")); }
	for (const FSarkoMapProp& Prop : Map.Props)               { CheckInside(Prop.Location, TEXT("a prop")); }
	for (const FSarkoLootContainerSpot& C : Map.Containers)   { CheckInside(C.Location, TEXT("a container")); }
	for (const FSarkoBotSpot& B : Map.BotSpawns)              { CheckInside(B.Location, TEXT("a bot spawn")); }

	// Nobody starts inside geometry.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		CheckInside(Spawn.GetLocation(), TEXT("a player spawn"));
		TestFalse(TEXT("no player spawn sits inside a block"), IsInsideBlock(Spawn.GetLocation(), Map.Blocks));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestFalse(TEXT("no bot spawn sits inside a block"), IsInsideBlock(Bot.Location, Map.Blocks));
	}

	// Every prop kind resolves, or that prop silently does not appear.
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		FSarkoPropKind Kind;
		TestTrue(FString::Printf(TEXT("prop kind '%s' resolves"), *Prop.Kind.ToString()),
			SarkoMap::FindPropKind(Prop.Kind, Kind));
	}

	// Content density: the frame must not be an empty plain.
	TestTrue(TEXT("there are at least 12 points of interest worth of props"), Map.Props.Num() >= 40);
	TestTrue(TEXT("there is loot to find"), Map.Containers.Num() >= 15);
	TestTrue(TEXT("there are bots"), Map.BotSpawns.Num() >= 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeExtractionsAreOnOuterEdges,
	"Sarko.Map.BridgeExtractionsAreOnOuterEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeExtractionsAreOnOuterEdges::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("there is more than one way out"), Map.Extractions.Num() >= 2);

	// This sector is a corner of the eventual 800x800 map: the north and west
	// edges stay world border, while east and south open into neighbouring
	// sectors. An extraction on an inner edge would vanish when the map grows.
	const float Edge = Map.ExtentUU * 0.85f;
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		const bool bOnNorth = Spot.Location.Y >= Edge;
		const bool bOnWest = Spot.Location.X <= -Edge;
		TestTrue(FString::Printf(TEXT("extraction '%s' is on an outer edge"), *Spot.Name),
			bOnNorth || bOnWest);
		TestTrue(FString::Printf(TEXT("extraction '%s' has a usable radius"), *Spot.Name),
			Spot.RadiusUU >= 200.f);
		TestFalse(FString::Printf(TEXT("extraction '%s' is named"), *Spot.Name), Spot.Name.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeRiskGradientExists,
	"Sarko.Map.BridgeRiskGradientExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeRiskGradientExists::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The design's core promise: the far half is worth crossing for, and more
	// dangerous. If loot and bots are spread evenly the map has no gradient and
	// the bridge means nothing.
	int32 NearBots = 0;
	int32 FarBots = 0;
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		(Bot.Location.Y > 0.f ? NearBots : FarBots)++;
	}
	TestTrue(TEXT("the far half holds more bots than the near half"), FarBots > NearBots);

	int32 FarGoodLoot = 0;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		if (Spot.Location.Y < 0.f && (Spot.Tier == TEXT("good") || Spot.Tier == TEXT("military")))
		{
			++FarGoodLoot;
		}
	}
	TestTrue(TEXT("the best loot is across the ravine"), FarGoodLoot >= 4);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and fix whatever the map violates**

Run: `cd SarkoGame && ./Scripts/run-tests.sh Sarko.Map`

Expect real failures on the first run — these tests are the specification of the map, and the file was authored by hand. Fix `bridge.json`, not the tests, unless a test genuinely encodes the wrong rule; if so, say which and why in your report.

- [ ] **Step 3: Run the whole suite and commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "test(game): pin the Bridge sector's invariants"
```

---

### Task 6: Delete the procedural generator

Last, so nothing still depends on it.

**Files:**
- Modify: `Source/SarkoGame/Map/SarkoMapBuilder.h`, `.cpp` (remove generation)
- Modify: `Source/SarkoGame/Tests/MapBuilderTest.cpp` (remove generation tests)
- Modify: `Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (seed no longer builds geometry)

**Interfaces:**
- Consumes: nothing.
- Produces: a smaller `SarkoMapBuilder` that only spawns.

- [ ] **Step 1: Remove generation**

Delete `BuildLayout`, `PickClearPoint`, `NearestBlockIndex`, `NearestCoverClearance`, `ClearsAllCover`, `DisplaceClearOfBlock`, `LatticeClearPoint` and their constants. Keep `SpawnLayout`, `SpawnProps`, `SpawnLighting` and the shared `SpawnMeshBox`.

Delete the generation tests: `LayoutIsDeterministic`, `LayoutIsSeedPortable`, `LayoutRespectsBounds`, `SpawnPointsClearCoverEvenWhenCrowded`, `SpawnPointsClearCoverWhenSaturated`. Keep `SettingsHaveSaneDefaults`.

This is a deliberate deletion of working, tested code, including two rounds of review fixes. It is dead the moment maps are authored by hand, and keeping it means carrying a second map-creation system nobody uses. Git has it.

- [ ] **Step 2: Keep the seed, drop its geometry role**

`ASarkoRaidGameState::Seed` stays replicated and `OnRep_Seed` stays — the seed is still needed, for loot rolls in a later plan. What changes is that receiving it no longer builds geometry: clients build from the map file, which every machine already has, so `BuildAndSpawnLayout` loads the definition rather than generating from the seed.

Update the comments that explain seed-driven geometry. They were correct and are now wrong, and a stale comment that confidently explains the wrong mechanism is worse than none.

- [ ] **Step 3: Verify nothing referenced it**

Run: `cd SarkoGame && grep -rn "BuildLayout\|PickClearPoint\|LatticeClearPoint" Source/ || echo "no references remain"`
Expected: no references remain.

- [ ] **Step 4: Run the suite and commit**

Run: `cd SarkoGame && ./Scripts/run-tests.sh`
Expected: green, with the generation tests gone and the Bridge tests present.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame && git commit -m "refactor(game): delete procedural map generation, now that maps are authored"
```

---

## Manual verification — the part no agent can do

Automation proves the file is well-formed and the invariants hold. The overview screenshot proves the layout matches the design. Neither can answer whether the map is any good. After Task 6, play it and judge:

1. **Do you know where you are?** Without a minimap, do the water tower and the bridge tell you your position?
2. **Is the bridge frightening?** Crossing should feel like a commitment.
3. **Is the gradient legible without being told?** Does the north half feel poor and safe, the south rich and dangerous?
4. **Are the village lanes fights or corridors?** Six houses can make good fighting space or a maze.
5. **Is 400 m the right size at 4 m/s?** Does crossing feel like a journey or a chore?
6. **Is 15 minutes right for a first raid?**

Any of these being wrong is a data-file edit, not a code change — which is the point of the whole design.

---

## Self-Review

**Spec coverage.** §2 purpose: PvE, one player, corner sector — extractions constrained to outer edges (Task 5), bot spawns zoned (Task 3/4). §3 sizes and time: `MapExtent=20000` and per-map `raidDurationSeconds` (Tasks 3, 4), both asserted (Task 5). §4 layout: authored in Task 4 to the diagram, with the ravine-and-three-crossings spine, risk gradient asserted in Task 5. §5 format: schema in Task 2, shipping via `DirectoriesToAlwaysStageAsNonUFS` in Task 4. §6 one point of change: `ToLayout` keeps `FSarkoMapLayout` intact; the seed's loot role preserved in Task 6. §7 overview tool: Task 1, first because everything else is verified with it. §8 testing: Tasks 2 and 5.

**Deliberately out of scope**, per spec §9: the extraction *mechanic* (points are data here), loot contents, real art, terrain and heights, raid reconnection, neighbouring sectors, radiation.

**Known limitation.** The invariant tests check geometry and density, not playability. A map can satisfy every assertion and still be dull — a solid wall of houses with no lanes passes "props ≥ 40" comfortably. The overview screenshot and the manual pass are the only checks on quality, and they are the reason Task 1 comes first.

**Open questions carried from spec §10** and unresolved here: how many bots the sector should hold (Task 4 sets a starting number, and the 8 converging bots that turned a firefight into an execution say the answer is not "more"); how the impassable ravine reads visually without terrain; whether in-raid transport is needed when the map grows to 800 × 800.

---

## Next

After the Bridge sector is playable and judged: **loot and extraction** — inventory with slots and weight, the safe pocket, server-rolled container contents against the seed this plan preserved, the extraction mechanic against the points this plan places, and the outcome report to the live `sarko-api`.
