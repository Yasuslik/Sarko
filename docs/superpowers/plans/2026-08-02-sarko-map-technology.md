# Map Technology Implementation Plan (Stage B)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the map *technology* Stage C's content needs: buildings the player can walk into (declared once in JSON, expanded to walls by a pure deterministic function), a readable per-surface palette with roads and water, nine new prop kinds including composite ones, stable ids on every entry — and close the two known debts (near-black shadows, `MapExtent` vs `extentUU`).

**Architecture:** Everything new is data plus a pure function. `buildings[]` is one declaration per building; `SarkoMap::ExpandBuilding` turns it into `FSarkoCoverBlock`s, which the existing `SpawnLayout` already knows how to spawn — so twenty buildings cost twenty JSON objects and zero new spawn code. Colour stops being two constants and becomes an `ESarkoSurface` enum with one palette lookup, carried on blocks and on each part of a prop kind; that makes roads (flat, non-colliding, dark) and water (flat, dark blue-grey) authorable with no new actor types. `FSarkoPropKind` becomes a list of boxes instead of one box, so a pylon or a road sign is one authored entry.

**Tech Stack:** UE 5.8, C++ only, `Build.sh` + `UnrealEditor-Cmd`, automation tests under `-nullrhi`, visual verification under `-RenderOffscreen`. Engine primitive meshes referenced by path. No new module dependencies.

## Global Constraints

- **Spec §5 of `docs/superpowers/specs/2026-07-30-sarko-production-raid-loop-design.md` is normative** for this stage. The design of record for what the content will eventually be is `docs/design/bridge-full-map-tz.md` (the owner's ТЗ) — §13 walkable buildings, §14 readability, §15 filling the north, §16 iOS performance, §18 ids, §32 prop kinds. Where this plan deviates from the ТЗ it says so in the task, with the reason.
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`. All paths in this plan are relative to `/Users/ruslanbondarenko/project/ai-workspace/home/Sarko` unless absolute; **agent shells reset their cwd between calls, so every command below begins with an explicit `cd`.**
- **`SarkoGame.Build.cs` is not touched.** This plan adds no module dependency: `Json`, `Engine`, `CoreUObject` already cover everything (`ASkyLight`, `USkyLightComponent`, `UTextureCube` are all `Engine`). `DefaultBuildSettings` stays `BuildSettingsVersion.V7` and `PrivateIncludePaths.Add(ModuleDirectory)` stays, which is why every include below is module-relative (`"Map/SarkoMapPalette.h"`).
- **Create no binary assets. Ever.** No `.uasset`, `.umap`, Blueprint, UMG widget, material asset, DataTable, Behavior Tree, font. C++, `.ini`, `.json`, `.sh` only. Referencing an engine asset by path is fine and is how all geometry, all materials and (new in Task 6) the ambient cubemap are obtained. `SarkoGame/Content/Mannequins/` is committed Epic template content and is **read-only**.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code.** `UnrealEditor-Cmd` exits 0 having run zero tests; the script takes its verdict from the `Automation Test Queue Empty N tests performed` line. **Test counts in this plan are RELATIVE.** The committed suite is at **62**; the working tree also carries in-flight Stage A.5 tasks that add more. Task 1 Step 1 records the real baseline `B`, and every later step names `B + <delta>`: T1 +3, T2 +3, T3 +1, T4 +2, T5 +6, T6 +2, T7 +3, T8 +2 → **B + 22** at the end. (T3 is +1 rather than +2 because it rewrites an existing test in place.) Recompute `B` at execution time; do not copy 62.
- The automation-test flag spelling that compiles is `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`.
- **Automation runs under `-nullrhi` and can see nothing.** Every visual claim is verified by `-RenderOffscreen` + a screenshot that is then **read as an image**, never by a test and never by assertion. `Scripts/overview-shot.sh` is the tool; Task 6 adds `Scripts/eye-shot.sh` for a player-height frame. The loop is: edit JSON → shoot → **read the PNG** → fix.
- **`BugItGo` calls `Ghost()`**, which disables capsule collision for the rest of the run. Any headless run that needs collision must append `Walk` to `-ExecCmds` after the teleport. `Scripts/eye-shot.sh` does this; keep it if you edit the script.
- **`SetMobility(Static)` before `SetStaticMesh` silently no-ops after BeginPlay.** The one spawn path is `SpawnMeshBox` in `Map/SarkoMapBuilder.cpp` and the order is Movable → mesh → scale → collision → paint → Static. Nothing in this plan adds a second spawn path; new geometry goes through `SpawnMeshBox` or it does not exist.
- **Every primitive gets an explicit material.** `/Engine/BasicShapes/Cube` ships with `WorldGridMaterial`, and at 400× scale one checker texel was player-sized. `PaintFlat` (already in `SarkoMapBuilder.cpp`) is mandatory for every spawned primitive; it sets both `Color` and `BaseColor` because the parameter is renamed across engine versions.
- **Palette values are LINEAR, not sRGB.** They feed `BaseColor` directly. An sRGB swatch pasted in comes out roughly twice as bright as intended. Existing values in `SarkoMap::Palette` are the reference for scale.
- **`config = Game` → `Config/DefaultGame.ini`; `config = Engine` → `Config/DefaultEngine.ini`.** `USarkoRaidSettings` is `config = Game`. `ProjectPackagingSettings` and `RendererSettings` are `Engine`. A setting in the wrong file silently loads the C++ default and nothing warns. **ini string values containing `//` must be quoted** or `SwallowDoubleSlashComments` truncates them.
- **`Config/DefaultEngine.ini`'s renderer comments are history, not noise.** Lumen and Nanite are off for the mobile target; **virtual shadow maps are deliberately back ON** because without them a movable directional light casts no shadow and grey cover on a grey floor was invisible from above. Auto-exposure is off on purpose. Do not "tidy" any of it.
- **`FRotator` and `FVector` members are doubles in 5.8.** A bare float literal in `TestEqual` is ambiguous; compare with `.Equals(..., Tolerance)` and suffix scalar literals with `f`.
- **Forward-declare structs at global scope**, never as an elaborated type specifier inside a namespace — `const struct FFoo&` written inside `namespace Bar {}` declares a second, permanently incomplete `Bar::FFoo`. `Map/SarkoMapBuilder.h` carries the comment and the scar; follow it.
- **No per-tick allocation and nothing new on Tick at all.** Everything this plan adds happens once, at map load.
- **iOS actor budget.** The current sector spawns ~310 actors (1 floor + 8 blocks + 238 props + 42 containers + 3 zones + 16 bots + 1 pawn + 1 light). This plan takes it to ~345 (see Task 8's tally) and prefers few large forms to many small ones, per ТЗ §16. Instancing is **not** introduced here: the threshold where `UHierarchicalInstancedStaticMeshComponent` becomes worth the complexity is ~550 actors, which Stage C will reach (≈20 buildings × 8 walls + ~30 treeline + fillers). Task 8 states the number so Stage C inherits a decision, not a surprise.
- **`bridge.json` must keep loading.** The parser gains sections and optional fields only; no field that exists today becomes required, and no existing value changes meaning. Task 1 pins backward compatibility with a test over the real file, and every task that touches the schema re-runs it.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything that discards working-tree changes — a subagent on this project already destroyed an uncommitted file that way. Never `git add -A`; stage the exact paths each task names.

## Explicitly out of scope for Stage B

- **Authoring Bridge_West.** That is Stage C (spec §6). The three buildings converted in Task 7 and the roads/water in Task 8 exist to prove the technology inside the shipped map and to give the screenshot loop something to look at — they are placed at ТЗ ledger coordinates that survive into the full map, and they are not the sector.
- **The ravine as a real physical pit.** Spec §5.2 decided this and it is **not reopened**: the ravine stays visually deep and physically flat. Cliffs (the existing rim walls) plus a dark bed plus dark water read identically from a top-down camera, and a real 400–700 uu dig buys fall damage, stuck pawns and nav holes on iOS for zero gameplay. Crossability is enforced by the rim walls exactly as it is today. Task 8 builds the *visual* depth; nothing digs.
- **Translucent water.** The project cannot ship a translucent material without authoring a material asset, and it authors none. Water is an **opaque** dark blue-grey slab. This is a deliberate, documented limitation (Task 8 records it), not an oversight.
- **Real art.** Production art means marketplace packs the owner installs, after which the kind table's `Mesh` paths point at them. The palette is the ceiling without binary assets (spec §7).
- **Roofs, stairs, second floors, destructibility, doors as a mechanic.** ТЗ invariants forbid all of them.

## What the ТЗ asks for that the engine genuinely cannot do

Flagged here rather than silently dropped:

1. **Translucent/animated water** (ТЗ §6 "мелкая вода" at the ford) — needs a material asset. Opaque slab instead; the ford reads as a lighter water tone (Task 8 gives it a separate surface value).
2. **A sky, a horizon, or any distance fog** — all need either an asset (cubemap sky sphere material) or `SkyAtmosphere` machinery whose per-frame LUT cost Task 6 rejects for mobile. The world ends at the floor's edge in black. Acceptable because the camera is top-down and never sees the horizon; it will look wrong the first time anyone takes a low-angle screenshot, and that is expected.
3. **Individual trees** (ТЗ §15 implies a treed north) — a canopy hides the player from a top-down camera, which is a gameplay defect, not a look. Spec §5.3's `treeline` replaces them: a tall dark-green impassable boundary. This is a deliberate design substitution, already approved in the spec.
4. **A second light of any kind** — mobile forward shading supports exactly one directional light, and a second makes the engine warn on screen and pick one by brightness. Task 6's ambient is a sky light (SH irradiance), which is not a second directional light and does not trip that path.

## File Structure

```
SarkoGame/
├── Config/
│   └── DefaultEngine.ini                    # + cook the engine ambient cubemap directory (Task 6)
├── Data/Maps/
│   └── bridge.json                          # + ids (T1), + 3 buildings (T7), + roads/water/ravine bed (T8)
├── Scripts/
│   └── eye-shot.sh                          # NEW (T6): player-height offscreen screenshot
└── Source/SarkoGame/
    ├── Map/
    │   ├── SarkoMapPalette.h/.cpp            # NEW (T2): ESarkoSurface + the whole §14 palette
    │   ├── SarkoBuildings.h/.cpp             # NEW (T5): FSarkoBuilding + the pure expander
    │   ├── SarkoMapKinds.h/.cpp              # T3: parts-based kinds; T4: nine new kinds
    │   ├── SarkoMapDefinition.h/.cpp         # T1: ids; T2: block surface/collision; T7: buildings[]
    │   └── SarkoMapBuilder.h/.cpp            # T2: per-surface paint; T6: sky light; T7: expanded walls
    └── Tests/
        ├── MapDefinitionTest.cpp             # T1 +3, T2 +2, T3 +1, T4 +2, T7 +2
        ├── MapBuilderTest.cpp                # T2 +1, T6 +2
        ├── BuildingTest.cpp                  # NEW (T5): 6 expander tests
        └── BridgeMapTest.cpp                 # T1 +0 (extended), T7 +1, T8 +2
```

`SarkoMapPalette.h` is a separate small header rather than more content in `SarkoMapBuilder.h` because both the kind table and the map definition need `ESarkoSurface`, and `SarkoMapKinds.h` must not have to include the builder. `SarkoBuildings.h/.cpp` is separate for the same reason the map parser is separate from the map spawner: it is pure geometry, it is where all the invariants live, and `run-tests.sh` runs under `-nullrhi` where there is no world to expand a building into.

---

### Task 1: Stable ids, uniqueness, and the `MapExtent` / `extentUU` debt

First, because ids are the handle every later section needs (a building without an id cannot be referenced in a report or a test), and because it is the one task that touches no geometry — so if it breaks anything, it broke the parser and nothing else.

Two levels of strictness, deliberately:

- **`ParseDefinition` (pure, string in)**: `id` is optional everywhere; a present id must be a non-empty string; **ids must be unique across the whole file**. This keeps every string-literal test fixture in the suite valid and keeps the promise that old maps load.
- **`LoadDefinitionFromDisk` (the shipped map)**: additionally *requires* an id on every container, player spawn, bot spawn, extraction and building. A shipped map with an anonymous container is a map whose loot ledger cannot be audited, and Stage C will author 42 of them.

That split is why "old maps keep loading" and spec §5.4's "required on containers/spawns/extracts/buildings" are both true at once.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h` (`FSarkoCoverBlock` gains `Id`)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, `.cpp`
- Modify: `SarkoGame/Data/Maps/bridge.json` (65 ids)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (+3 tests)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (extends `FSarkoBridgeMapIsValid`, no new test)

**Interfaces:**
- Consumes: `SarkoMap::ParseDefinition`, `SarkoMap::LoadDefinitionFromDisk`, the file-local `ReadOptionalString` helper in `SarkoMapDefinition.cpp`.
- Produces:
  - `FString Id` on `FSarkoCoverBlock`, `FSarkoMapProp`, `FSarkoLootContainerSpot`, `FSarkoBotSpot`, `FSarkoExtractionSpot`, and a parallel `TArray<FString> PlayerSpawnIds` on `FSarkoMapDefinition` (player spawns are `FTransform`s and cannot carry a field).
  - `bool SarkoMap::CollectIds(const FSarkoMapDefinition& Definition, TArray<FString>& OutIds, FString& OutError)` — every id in file order; error on a duplicate, naming it.
  - `bool SarkoMap::RequireIdentifiedEntries(const FSarkoMapDefinition& Definition, FString& OutError)` — the shipped-map rule.
  - Both are declared in `SarkoMapDefinition.h` inside `namespace SarkoMap`.

- [ ] **Step 1: Record the test baseline**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and a line `==> B test(s) performed, 0 failed`. **Write `B` down in the task report.** Every later expected count in this plan is `B + delta`. If this run is not green, stop and report — Stage B does not start on a red suite.

- [ ] **Step 2: Write the failing parser tests**

Append inside the existing `#if WITH_AUTOMATION_TESTS` block at the end of `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapIdsAreOptionalAndUnique,
	"Sarko.Map.IdsAreOptionalAndUnique",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapIdsAreOptionalAndUnique::RunTest(const FString& Parameters)
{
	// Ids are how a report, a bug and a test all name the same object (ТЗ §18).
	// Optional in the pure parser so every fixture in this suite stays valid;
	// unique always, because two objects answering to one name is worse than
	// neither having one — a fix applied to "bridge_house_d1" would silently
	// land on whichever of them the code happened to find first.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"blocks": [ { "id": "b1", "kind": "wall", "pos": [0, 0, 100], "extent": [100, 100, 100] } ],
		"props": [ { "id": "p1", "kind": "crate", "pos": [200, 0, 70] } ],
		"containers": [ { "id": "c1", "pos": [250, 0, 0], "tier": "junk" } ],
		"playerSpawns": [ { "id": "s1", "pos": [-16000, 17000, 100], "yaw": 135 } ],
		"botSpawns": [ { "id": "n1", "pos": [8000, -12000, 100], "zone": "deep" } ],
		"extractions": [ { "id": "e1", "pos": [-14000, 19000, 0], "radiusUU": 400, "name": "North" } ]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(TEXT("a fully identified map parses"), SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("a block's id is read"), Definition.Blocks[0].Id, FString(TEXT("b1")));
	TestEqual(TEXT("a prop's id is read"), Definition.Props[0].Id, FString(TEXT("p1")));
	TestEqual(TEXT("a container's id is read"), Definition.Containers[0].Id, FString(TEXT("c1")));
	TestEqual(TEXT("a bot's id is read"), Definition.BotSpawns[0].Id, FString(TEXT("n1")));
	TestEqual(TEXT("an extraction's id is read"), Definition.Extractions[0].Id, FString(TEXT("e1")));
	// Player spawns are FTransforms and cannot carry a field, so their ids ride
	// a parallel array. The arrays must stay index-aligned or an id names the
	// wrong spawn, which is worse than no id at all.
	TestEqual(TEXT("player spawn ids are index-aligned with the spawns"),
		Definition.PlayerSpawnIds.Num(), Definition.PlayerSpawns.Num());
	TestEqual(TEXT("a player spawn's id is read"), Definition.PlayerSpawnIds[0], FString(TEXT("s1")));

	// Every id collected, in file order, from one call.
	TArray<FString> Ids;
	FString CollectError;
	TestTrue(TEXT("ids collect cleanly"), SarkoMap::CollectIds(Definition, Ids, CollectError));
	TestEqual(TEXT("six ids in the file, six collected"), Ids.Num(), 6);

	// The same fixture with no ids at all must still parse: this is the promise
	// that a hand-written map from before this task keeps working.
	FSarkoMapDefinition Anonymous;
	TestTrue(TEXT("a map with no ids anywhere still parses"),
		SarkoMap::ParseDefinition(MinimalMapJson, Anonymous, Error));
	TArray<FString> NoIds;
	TestTrue(TEXT("collecting from an anonymous map is not an error"),
		SarkoMap::CollectIds(Anonymous, NoIds, CollectError));
	TestEqual(TEXT("an anonymous map yields no ids"), NoIds.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMapRejectsBadIds,
	"Sarko.Map.RejectsBadIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMapRejectsBadIds::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> BadCases = {
		// A duplicate across two *different* sections is the realistic mistake:
		// copy a container line, paste it as a prop, forget to rename.
		{ TEXT("duplicate id across sections"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]}],
				"containers":[{"id":"same","pos":[150,100,0],"tier":"junk"}]})") },
		{ TEXT("duplicate id inside one section"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]},{"id":"same","kind":"crate","pos":[300,100,70]}]})") },
		// Present-but-empty is not "absent": it is a field the author started
		// filling in and abandoned, and it would collide with the next one.
		{ TEXT("empty id string"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"","kind":"crate","pos":[100,100,70]}]})") },
		// Same discipline as every other optional field in this parser.
		{ TEXT("id is a number"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":7,"kind":"crate","pos":[100,100,70]}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Case.Value, Definition, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShippedMapsMustIdentifyEveryPlaceable,
	"Sarko.Map.ShippedMapsMustIdentifyEveryPlaceable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShippedMapsMustIdentifyEveryPlaceable::RunTest(const FString& Parameters)
{
	// The stricter rule that only the on-disk path enforces. Blocks and props
	// are exempt because there are 246 of them and none is ever referred to
	// individually; containers, spawns and extractions each carry state or a
	// ledger row, so an anonymous one cannot be audited.
	const FString Anonymous = TEXT(R"({
		"id": "anon",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"containers": [ { "pos": [250, 0, 0], "tier": "junk" } ]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::ParseDefinition(Anonymous, Definition, Error))
	{
		AddError(FString::Printf(TEXT("fixture must still parse: %s"), *Error));
		return false;
	}
	FString RequireError;
	TestFalse(TEXT("an anonymous container fails the shipped-map rule"),
		SarkoMap::RequireIdentifiedEntries(Definition, RequireError));
	TestTrue(TEXT("the failure names the section"), RequireError.Contains(TEXT("containers")));

	// And the real map must pass it, through the real entry point.
	FSarkoMapDefinition Bridge;
	FString LoadError;
	TestTrue(FString::Printf(TEXT("bridge.json loads: %s"), *LoadError),
		SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Bridge, LoadError));
	FString BridgeRequireError;
	TestTrue(FString::Printf(TEXT("bridge.json identifies every placeable: %s"), *BridgeRequireError),
		SarkoMap::RequireIdentifiedEntries(Bridge, BridgeRequireError));
	return true;
}
```

- [ ] **Step 3: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `BUILD FAILED`, with `no member named 'Id' in 'FSarkoCoverBlock'`, `no member named 'PlayerSpawnIds'`, and `no member named 'CollectIds' in namespace 'SarkoMap'` in the tail of the build log.

- [ ] **Step 4: Add the id fields**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h`, inside `FSarkoCoverBlock`, above `Location`:

```cpp
	/**
	 * Optional stable name (ТЗ §18). Optional on a block because there are
	 * hundreds and none is referenced individually; carried anyway so an
	 * expanded building's walls can be traced back to their building.
	 */
	UPROPERTY()
	FString Id;
```

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, add the same `UPROPERTY() FString Id;` as the first member of `FSarkoMapProp`, `FSarkoLootContainerSpot`, `FSarkoBotSpot` and `FSarkoExtractionSpot`, and add to `FSarkoMapDefinition` immediately after `PlayerSpawns`:

```cpp
	/**
	 * Ids for PlayerSpawns, index-aligned. A player spawn is an FTransform —
	 * an engine type with nowhere to put a name — so its id rides alongside.
	 * ParseDefinition always appends to both arrays in the same iteration, and
	 * a test pins that the lengths agree.
	 */
	UPROPERTY()
	TArray<FString> PlayerSpawnIds;
```

Then declare the two new functions at the end of `namespace SarkoMap` in the same header:

```cpp
	/**
	 * Every id in the definition, in file order. Fails (naming the id) on a
	 * duplicate: ids are a single namespace across the whole file, because the
	 * thing that reads them — a report, a test, a person — does not know or
	 * care which section an object was declared in.
	 */
	bool CollectIds(const FSarkoMapDefinition& Definition, TArray<FString>& OutIds, FString& OutError);

	/**
	 * The stricter rule for a map that ships: every container, player spawn,
	 * bot spawn, extraction and building must be named. Enforced by
	 * LoadDefinitionFromDisk, not by ParseDefinition — a test fixture built
	 * from a string literal has no reason to name anything, and making the
	 * pure parser strict here would break every fixture in the suite and the
	 * promise that an older map file still loads.
	 */
	bool RequireIdentifiedEntries(const FSarkoMapDefinition& Definition, FString& OutError);
```

- [ ] **Step 5: Read ids in the parser**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp`, add this helper to the anonymous namespace, below `ReadOptionalString`:

```cpp
	/**
	 * Reads an optional stable id. Absent is fine; present-but-empty is not —
	 * an empty id is a name nothing can be found by, and two of them collide.
	 */
	bool ReadOptionalId(const TSharedPtr<FJsonObject>& Object, FString& Out, FString& OutError)
	{
		if (!ReadOptionalString(Object, TEXT("id"), Out, OutError))
		{
			return false;
		}
		if (Object->HasField(TEXT("id")) && Out.IsEmpty())
		{
			OutError = TEXT("'id' is present but empty");
			return false;
		}
		return true;
	}
```

In each of the six per-entry loops, immediately after the `pos` read, add the id read with the section-prefixed error the file already uses. For `blocks` it reads:

```cpp
				if (!ReadOptionalId(*Object, Block.Id, OutError))
				{
					OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
					return false;
				}
```

and identically for `props` (`Prop.Id`), `containers` (`Spot.Id`), `botSpawns` (`Spot.Id`), `extractions` (`Spot.Id`). For `playerSpawns`, which builds an `FTransform`, read into a local and push both arrays together:

```cpp
				FString SpawnId;
				if (!ReadOptionalId(*Object, SpawnId, OutError))
				{
					OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
					return false;
				}
				const FRotator Rotation(0.f, static_cast<float>(Yaw), 0.f);
				OutDefinition.PlayerSpawns.Add(FTransform(Rotation, Location));
				OutDefinition.PlayerSpawnIds.Add(SpawnId);
```

Then, immediately before `ParseDefinition`'s final `return true;`, enforce uniqueness — so a duplicate is a parse error and never reaches anything downstream:

```cpp
	// Uniqueness is a parse-time rule rather than a caller's responsibility:
	// every consumer of a definition assumes an id names one object, and there
	// is no safe behaviour for the case where it names two.
	TArray<FString> Ids;
	if (!CollectIds(OutDefinition, Ids, OutError))
	{
		return false;
	}

	return true;
```

- [ ] **Step 6: Implement the two functions**

Append to `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp`:

```cpp
bool SarkoMap::CollectIds(const FSarkoMapDefinition& Definition, TArray<FString>& OutIds, FString& OutError)
{
	OutIds.Reset();
	OutError.Reset();

	TSet<FString> Seen;
	const auto Take = [&OutIds, &OutError, &Seen](const FString& Id, const TCHAR* Section, int32 Index) -> bool
	{
		if (Id.IsEmpty())
		{
			return true;
		}
		if (Seen.Contains(Id))
		{
			OutError = FString::Printf(TEXT("duplicate id '%s' (%s[%d])"), *Id, Section, Index);
			return false;
		}
		Seen.Add(Id);
		OutIds.Add(Id);
		return true;
	};

	for (int32 I = 0; I < Definition.Blocks.Num(); ++I)       { if (!Take(Definition.Blocks[I].Id, TEXT("blocks"), I))          { return false; } }
	for (int32 I = 0; I < Definition.Props.Num(); ++I)        { if (!Take(Definition.Props[I].Id, TEXT("props"), I))            { return false; } }
	for (int32 I = 0; I < Definition.Containers.Num(); ++I)   { if (!Take(Definition.Containers[I].Id, TEXT("containers"), I))  { return false; } }
	for (int32 I = 0; I < Definition.PlayerSpawnIds.Num(); ++I) { if (!Take(Definition.PlayerSpawnIds[I], TEXT("playerSpawns"), I)) { return false; } }
	for (int32 I = 0; I < Definition.BotSpawns.Num(); ++I)    { if (!Take(Definition.BotSpawns[I].Id, TEXT("botSpawns"), I))    { return false; } }
	for (int32 I = 0; I < Definition.Extractions.Num(); ++I)  { if (!Take(Definition.Extractions[I].Id, TEXT("extractions"), I)) { return false; } }
	return true;
}

bool SarkoMap::RequireIdentifiedEntries(const FSarkoMapDefinition& Definition, FString& OutError)
{
	OutError.Reset();

	const auto Require = [&OutError](const FString& Id, const TCHAR* Section, int32 Index) -> bool
	{
		if (!Id.IsEmpty())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("%s[%d] has no 'id'; containers, spawns, extractions and buildings must be named"),
			Section, Index);
		return false;
	};

	for (int32 I = 0; I < Definition.Containers.Num(); ++I)  { if (!Require(Definition.Containers[I].Id, TEXT("containers"), I))  { return false; } }
	for (int32 I = 0; I < Definition.PlayerSpawnIds.Num(); ++I) { if (!Require(Definition.PlayerSpawnIds[I], TEXT("playerSpawns"), I)) { return false; } }
	for (int32 I = 0; I < Definition.BotSpawns.Num(); ++I)   { if (!Require(Definition.BotSpawns[I].Id, TEXT("botSpawns"), I))    { return false; } }
	for (int32 I = 0; I < Definition.Extractions.Num(); ++I) { if (!Require(Definition.Extractions[I].Id, TEXT("extractions"), I)) { return false; } }
	// PlayerSpawnIds is index-aligned with PlayerSpawns, so a spawn that never
	// reached the id array would be invisible to the loop above.
	if (Definition.PlayerSpawnIds.Num() != Definition.PlayerSpawns.Num())
	{
		OutError = FString::Printf(TEXT("playerSpawns: %d spawns but %d ids — the arrays must stay index-aligned"),
			Definition.PlayerSpawns.Num(), Definition.PlayerSpawnIds.Num());
		return false;
	}
	return true;
}
```

Add `#include "Containers/Set.h"` only if the build complains; `CoreMinimal.h` normally covers `TSet`.

- [ ] **Step 7: Enforce the strict rule on the disk path**

In `SarkoMap::LoadDefinitionFromDisk`, replace the tail so the stricter rule runs after a successful parse:

```cpp
	if (!ParseDefinition(Json, OutDefinition, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	// Stricter than the pure parser on purpose (see RequireIdentifiedEntries).
	// A shipped map with an anonymous container cannot be audited against the
	// ТЗ's loot ledger, and Stage C authors 42 of them.
	FString IdError;
	if (!RequireIdentifiedEntries(OutDefinition, IdError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *IdError);
		OutDefinition = FSarkoMapDefinition();
		return false;
	}
	return true;
```

- [ ] **Step 8: Author the 65 ids into `bridge.json`**

Line-based and order-preserving on purpose: a `json.load`/`json.dump` round trip would reflow all 378 lines and bury the change. Every entry in this file is already one line.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && python3 - <<'PY'
import re
path = 'SarkoGame/Data/Maps/bridge.json'
prefix = {'containers': 'bridge_loot', 'playerSpawns': 'bridge_spawn',
          'botSpawns': 'bridge_bot', 'extractions': 'bridge_extract'}
section, counters, out = None, {}, []
for line in open(path).read().split('\n'):
    m = re.match(r'\s*"(\w+)":\s*\[\s*$', line)
    if m:
        section = m.group(1)
    elif re.match(r'\s*\]', line):
        section = None
    elif section in prefix and re.match(r'\s*\{', line):
        n = counters.get(section, 0) + 1
        counters[section] = n
        line = re.sub(r'\{\s*', '{ "id": "%s_%02d", ' % (prefix[section], n), line, count=1)
    out.append(line)
open(path, 'w').write('\n'.join(out))
print(counters)
PY
```

Expected output: `{'containers': 42, 'playerSpawns': 4, 'botSpawns': 16, 'extractions': 3}`.

Then hand-rename the three extraction ids to the ТЗ §18 style, because these three *are* referred to by name (E1/E2/E3 in the ТЗ, and Stage C wires only E1):

- `bridge_extract_01` → `bridge_extract_north_path` (the `Северная тропа` row, ТЗ E1)
- `bridge_extract_02` → `bridge_extract_north_highway` (`Шоссе на север`, E2)
- `bridge_extract_03` → `bridge_extract_east_cordon` (`Восточный кордон`, E3)

Index-based ids elsewhere are a deliberate placeholder: Stage C re-authors this file for Bridge_West and will give containers zone-derived names (`bridge_gas_shop_good`, …). The ids exist now so the *mechanism* is proven and nothing can be added later without one.

- [ ] **Step 9: Close the extent debt**

Nothing today asserts that `USarkoRaidSettings::MapExtent` (which the AI's patrol square and the overview camera's framing height read) agrees with the loaded map's `extentUU` (which the floor is built from). They are both 20000 by luck. Extend `FSarkoBridgeMapIsValid` in `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` — add after the existing `TestEqual` on the raid duration:

```cpp
	// MapExtent (settings) and extentUU (map file) are two copies of one number.
	// Disagreement is silent and ugly in both directions: too small and the AI
	// patrols a box inside the map while the overview crops the sector; too
	// large and bots walk off the floor. Load through the settings' own MapId,
	// so this also pins that the configured map is the one that exists.
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);
	if (Settings)
	{
		FSarkoMapDefinition Configured;
		FString ConfiguredError;
		const bool bConfiguredLoaded = SarkoMap::LoadDefinitionFromDisk(
			Settings->MapId.ToString(), Configured, ConfiguredError);
		TestTrue(FString::Printf(TEXT("the configured map '%s' exists on disk: %s"),
			*Settings->MapId.ToString(), *ConfiguredError), bConfiguredLoaded);
		if (bConfiguredLoaded)
		{
			TestEqual(TEXT("MapExtent agrees with the map file's extentUU"),
				Settings->MapExtent, Configured.ExtentUU);
			TestEqual(TEXT("RaidDurationSeconds is not contradicted by the map file"),
				Configured.RaidDurationSeconds, Map.RaidDurationSeconds);
		}
	}
```

`Core/SarkoRaidSettings.h` is already included by this test file.

- [ ] **Step 10: Run the map tests and confirm they pass**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `ALL GREEN`, and the listed results include `Sarko.Map.IdsAreOptionalAndUnique`, `Sarko.Map.RejectsBadIds`, `Sarko.Map.ShippedMapsMustIdentifyEveryPlaceable`, all `Success`.

- [ ] **Step 11: Run the whole suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and `==> B+3 test(s) performed, 0 failed`.

- [ ] **Step 12: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): stable ids on every entry, unique by construction, required on the shipped map"
```

---

### Task 2: `ESarkoSurface` and the ТЗ §14 palette; blocks gain a surface and a collision flag

Second, because every later task needs a surface value: prop parts (Task 3), the nine new kinds (Task 4), building walls (Task 5), roads and water (Task 8). Nothing visible changes in this task — `blocks[]` and props keep their current colours by default — which is exactly what makes it safe to land first.

This overturns one existing decision on purpose. `SpawnProps` currently says props "share the cover grey rather than getting a colour each" because "a per-kind palette would compete with the blue/red that carries friend/foe". ТЗ §14 requires the opposite (rust industry, warm village, dark water, light dirt roads). The resolution is **saturation discipline, not greyness**: every world surface stays below 0.35 in its brightest channel and below 0.20 of chroma spread, while the player blue (0.16, 0.34, 0.85), the enemy red (0.80, 0.12, 0.10) and the extraction green (0.16, 0.62, 0.24) keep a channel at or above 0.6. A test pins that gap, so the palette cannot drift into competing with the characters.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h` (include the palette; `FSarkoCoverBlock` gains `Surface`, `bBlocksMovement`; the old `Palette::Ground/Structure` constants move)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp` (`SpawnLayout` paints per surface)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp` (block `surface`, `blocksMovement`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp` (+1 test)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (+2 tests)

**Interfaces:**
- Consumes: `FLinearColor`, the existing `Palette::Ground`/`Structure`/`GroundRoughness`/`StructureRoughness` values (they move file, keep names and values).
- Produces:
  - `enum class ESarkoSurface : uint8 { Ground, Dirt, Asphalt, Concrete, Structure, Rust, Timber, Vegetation, Water, Ravine, Extraction, Count }`
  - `const FLinearColor& SarkoMap::Palette::ColourFor(ESarkoSurface)`
  - `float SarkoMap::Palette::RoughnessFor(ESarkoSurface)`
  - `bool SarkoMap::ParseSurfaceName(const FString& Name, ESarkoSurface& Out)` and `FString SarkoMap::SurfaceName(ESarkoSurface)`
  - `FSarkoCoverBlock::Surface` (default `ESarkoSurface::Structure`) and `FSarkoCoverBlock::bBlocksMovement` (default `true`)
  - The JSON keys `"surface"` (string, one of the eleven names) and `"blocksMovement"` (bool) on a block entry, both optional.

- [ ] **Step 1: Write the failing palette test**

Append to `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp` inside the `#if WITH_AUTOMATION_TESTS` block:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSurfacePaletteIsReadable,
	"Sarko.Config.SurfacePaletteIsReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole readability argument of ТЗ §14, written down as assertions. These
 * are not style preferences: a top-down player reads the map by luminance
 * first, and every relation below was chosen because its opposite made
 * something unreadable in a real frame.
 */
bool FSarkoSurfacePaletteIsReadable::RunTest(const FString& Parameters)
{
	using namespace SarkoMap;
	using namespace SarkoMap::Palette;

	const auto Lum = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	const auto Spread = [](const FLinearColor& C)
	{
		return FMath::Max3(C.R, C.G, C.B) - FMath::Min3(C.R, C.G, C.B);
	};

	// Every enum value must have a colour, a roughness and a name. The Count
	// sentinel makes this loop exhaustive: adding a twelfth surface and
	// forgetting a switch case fails here instead of shipping black geometry.
	for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Raw);
		const FString Name = SurfaceName(Surface);
		TestFalse(FString::Printf(TEXT("surface %d has a name"), Raw), Name.IsEmpty());

		ESarkoSurface RoundTripped = ESarkoSurface::Count;
		TestTrue(FString::Printf(TEXT("'%s' parses back"), *Name), ParseSurfaceName(Name, RoundTripped));
		TestEqual(FString::Printf(TEXT("'%s' round-trips"), *Name),
			static_cast<uint8>(RoundTripped), Raw);

		const FLinearColor Colour = ColourFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' is in gamut"), *Name),
			Colour.R >= 0.f && Colour.G >= 0.f && Colour.B >= 0.f &&
			Colour.R <= 1.f && Colour.G <= 1.f && Colour.B <= 1.f);
		TestTrue(FString::Printf(TEXT("'%s' is lit, not black"), *Name), Lum(Colour) > 0.005f);
		const float Roughness = RoughnessFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' has a sane roughness"), *Name),
			Roughness > 0.f && Roughness <= 1.f);
	}

	// No two surfaces may be the same colour — two names for one look is a
	// palette that silently lost a distinction.
	for (uint8 A = 0; A < static_cast<uint8>(ESarkoSurface::Count); ++A)
	{
		for (uint8 B = A + 1; B < static_cast<uint8>(ESarkoSurface::Count); ++B)
		{
			const FLinearColor First = ColourFor(static_cast<ESarkoSurface>(A));
			const FLinearColor Second = ColourFor(static_cast<ESarkoSurface>(B));
			TestFalse(FString::Printf(TEXT("'%s' and '%s' are not the same colour"),
				*SurfaceName(static_cast<ESarkoSurface>(A)), *SurfaceName(static_cast<ESarkoSurface>(B))),
				First.Equals(Second, 0.004f));
		}
	}

	const float GroundLum = Lum(ColourFor(ESarkoSurface::Ground));

	// ТЗ §14, clause by clause.
	TestTrue(TEXT("a dirt road is lighter than the ground it cuts through"),
		Lum(ColourFor(ESarkoSurface::Dirt)) > GroundLum * 1.6f);
	TestTrue(TEXT("asphalt is darker than the ground"),
		Lum(ColourFor(ESarkoSurface::Asphalt)) < GroundLum);
	TestTrue(TEXT("the bridge deck contrasts hard against its own asphalt"),
		Lum(ColourFor(ESarkoSurface::Concrete)) > Lum(ColourFor(ESarkoSurface::Asphalt)) * 4.f);
	{
		const FLinearColor Water = ColourFor(ESarkoSurface::Water);
		TestTrue(TEXT("water is blue-grey: blue leads, red trails"), Water.B > Water.G && Water.G > Water.R);
		TestTrue(TEXT("water is darker than the ground, so the ravine reads as depth"),
			Lum(Water) < GroundLum);
	}
	{
		const FLinearColor Rust = ColourFor(ESarkoSurface::Rust);
		TestTrue(TEXT("rust is red-dominant"), Rust.R > Rust.G && Rust.G > Rust.B);
		TestTrue(TEXT("rust separates from the ground by brightness too"),
			Lum(Rust) > GroundLum * 1.4f);
	}
	{
		const FLinearColor Timber = ColourFor(ESarkoSurface::Timber);
		TestTrue(TEXT("the village tone is warm"), Timber.R > Timber.B * 2.f);
		TestTrue(TEXT("the village tone is brighter than the ground"), Lum(Timber) > GroundLum * 1.8f);
	}
	{
		const FLinearColor Veg = ColourFor(ESarkoSurface::Vegetation);
		TestTrue(TEXT("vegetation is green-dominant"), Veg.G > Veg.R && Veg.G > Veg.B);
		TestTrue(TEXT("a treeline is darker than the ground it borders, so it reads as a wall"),
			Lum(Veg) < GroundLum);
	}
	TestTrue(TEXT("the ravine bed is the darkest thing in the sector"),
		Lum(ColourFor(ESarkoSurface::Ravine)) < Lum(ColourFor(ESarkoSurface::Water)));
	{
		const FLinearColor Green = ColourFor(ESarkoSurface::Extraction);
		TestTrue(TEXT("the extraction is unmistakably green"), Green.G > Green.R * 2.f && Green.G > Green.B * 2.f);
		TestTrue(TEXT("the extraction is the brightest surface in the sector"), Lum(Green) > 0.3f);
	}

	// The colour budget belongs to the characters. Every *world* surface stays
	// muted; the three gameplay tints do not. This is the constraint that lets
	// §14's palette exist without competing with friend/foe reading.
	for (uint8 Raw = 0; Raw < static_cast<uint8>(ESarkoSurface::Count); ++Raw)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Raw);
		if (Surface == ESarkoSurface::Extraction)
		{
			continue; // deliberately loud: it is a gameplay marker, not scenery
		}
		const FLinearColor Colour = ColourFor(Surface);
		TestTrue(FString::Printf(TEXT("'%s' is muted enough not to fight the characters"),
			*SurfaceName(Surface)),
			FMath::Max3(Colour.R, Colour.G, Colour.B) < 0.35f && Spread(Colour) < 0.20f);
	}

	// The two original constants still mean what the previous palette test says
	// they mean, and the lookup agrees with them.
	TestTrue(TEXT("ColourFor(Ground) is the Ground constant"), ColourFor(ESarkoSurface::Ground).Equals(Ground, 0.0001f));
	TestTrue(TEXT("ColourFor(Structure) is the Structure constant"), ColourFor(ESarkoSurface::Structure).Equals(Structure, 0.0001f));
	TestEqual(TEXT("RoughnessFor(Ground) is GroundRoughness"), RoughnessFor(ESarkoSurface::Ground), GroundRoughness);
	TestEqual(TEXT("RoughnessFor(Structure) is StructureRoughness"), RoughnessFor(ESarkoSurface::Structure), StructureRoughness);

	ESarkoSurface Unknown = ESarkoSurface::Count;
	TestFalse(TEXT("an unknown surface name does not parse"), ParseSurfaceName(TEXT("chartreuse"), Unknown));
	return true;
}
```

- [ ] **Step 2: Write the failing schema tests**

Append to `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBlocksCarrySurfaceAndCollision,
	"Sarko.Map.BlocksCarrySurfaceAndCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBlocksCarrySurfaceAndCollision::RunTest(const FString& Parameters)
{
	// A road and a water strip are flat boxes that must not be walked into, and
	// a building wall is a coloured box that must be. One block type covers all
	// of it with two optional fields, which is why there is no separate
	// "surfaces" section and no second spawn path.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"blocks": [
			{ "id": "wall", "pos": [0, 0, 175], "extent": [400, 15, 175] },
			{ "id": "road", "pos": [0, 4000, 2], "extent": [550, 6000, 2], "surface": "asphalt", "blocksMovement": false },
			{ "id": "creek", "pos": [0, -4000, 5], "extent": [20000, 700, 3], "surface": "water", "blocksMovement": false }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(FString::Printf(TEXT("surfaced blocks parse: %s"), *Error),
		SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("three blocks"), Definition.Blocks.Num(), 3);

	// Backward compatibility, stated as an assertion: a block written before
	// this task means exactly what it meant before.
	TestEqual(TEXT("an unsurfaced block defaults to Structure"),
		static_cast<uint8>(Definition.Blocks[0].Surface), static_cast<uint8>(ESarkoSurface::Structure));
	TestTrue(TEXT("an unsurfaced block still blocks movement"), Definition.Blocks[0].bBlocksMovement);

	TestEqual(TEXT("the road's surface is read"),
		static_cast<uint8>(Definition.Blocks[1].Surface), static_cast<uint8>(ESarkoSurface::Asphalt));
	TestFalse(TEXT("the road does not block movement"), Definition.Blocks[1].bBlocksMovement);
	TestEqual(TEXT("the water's surface is read"),
		static_cast<uint8>(Definition.Blocks[2].Surface), static_cast<uint8>(ESarkoSurface::Water));
	TestFalse(TEXT("the water does not block movement"), Definition.Blocks[2].bBlocksMovement);

	// The layout is what the spawner consumes, so the two new fields have to
	// survive the reduction or a road spawns as grey cover.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TestEqual(TEXT("all three blocks reach the layout"), Layout.Cover.Num(), 3);
	TestFalse(TEXT("the road is still non-colliding in the layout"), Layout.Cover[1].bBlocksMovement);
	TestEqual(TEXT("the water is still water in the layout"),
		static_cast<uint8>(Layout.Cover[2].Surface), static_cast<uint8>(ESarkoSurface::Water));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRejectsBadSurfaceFields,
	"Sarko.Map.RejectsBadSurfaceFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRejectsBadSurfaceFields::RunTest(const FString& Parameters)
{
	// An unknown or mistyped surface must not silently fall back to grey: a
	// typo in "asphalt" would produce a light grey highway across a dark map
	// and look like a lighting bug.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("unknown surface name"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"surface":"tarmac"}]})") },
		{ TEXT("surface is not a string"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"surface":3}]})") },
		{ TEXT("blocksMovement is not a bool"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"blocks":[{"pos":[0,0,100],"extent":[100,100,100],"blocksMovement":"no"}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Case.Value, Definition, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}
```

- [ ] **Step 3: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko`
Expected: `BUILD FAILED` with `unknown type name 'ESarkoSurface'` and `no member named 'ColourFor' in namespace 'SarkoMap::Palette'`.

- [ ] **Step 4: Create the palette header**

Create `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "SarkoMapPalette.generated.h"

/**
 * What a piece of geometry is made of, for colour purposes only. Not a physical
 * material: nothing queries this for friction or footstep sounds, and the
 * project ships one material (BasicShapeMaterial) with a colour parameter.
 *
 * The list is ТЗ §14's palette plus the two surfaces the ravine needs. Keep
 * Count last: Sarko.Config.SurfacePaletteIsReadable loops to it, so a new
 * surface with no colour, no roughness or no name fails a test instead of
 * shipping as black.
 */
UENUM()
enum class ESarkoSurface : uint8
{
	/** Bare earth. The one thing everything else must be distinguishable from. */
	Ground,
	/** Dirt track — lighter than the ground, per §14. */
	Dirt,
	/** Highway and yards — dark, per §14. */
	Asphalt,
	/** Bridge deck, kerbs, pipe bases, sign plates — the pale contrast tone. */
	Concrete,
	/** Generic built grey: walls, wrecks, crates. The neutral of the frame. */
	Structure,
	/** Industry (§14 "промзона ржавая"): tanks, freight cars, trailers, pylon legs. */
	Rust,
	/** §14's warm village tone. Doubles as timber: roofs, fences, logs, sheds. */
	Timber,
	/** Bushes and the treeline boundary. Dark green, deliberately darker than the ground. */
	Vegetation,
	/** The ravine's water. Dark blue-grey and OPAQUE — see the Water constant. */
	Water,
	/** The ravine bed. The visual stand-in for depth the map does not physically dig. */
	Ravine,
	/** Extraction pads. The one saturated world colour, and it is a gameplay marker. */
	Extraction,

	Count UMETA(Hidden)
};

namespace SarkoMap
{
	/**
	 * The sector palette, ТЗ §14.
	 *
	 * These are linear-space base colours, not sRGB swatches — they are fed
	 * straight into a material's BaseColor, which is linear. An sRGB value
	 * pasted here comes out roughly twice as bright as intended.
	 *
	 * The named constants are kept (rather than folded into the lookup) because
	 * Sarko.Config.PaletteSeparatesGroundFromCover asserts against them by
	 * name, and because "which two colours is the whole readability argument
	 * about" deserves to be answerable without reading a switch.
	 */
	namespace Palette
	{
		/**
		 * Ground: desaturated olive/khaki. Dark enough that grey cover sits on
		 * top of it rather than dissolving into it — which is exactly what
		 * happened before, when floor and cover both wore the engine grid
		 * material and measured (156,155,151) against (158,157,153) in the same
		 * frame. Three levels apart is not cover.
		 */
		const FLinearColor Ground(0.046f, 0.051f, 0.028f);

		/** Cover and props: neutral grey, deliberately much lighter than the ground. */
		const FLinearColor Structure(0.150f, 0.150f, 0.155f);

		/** Ground roughness. Near-matte, so a 400 m plane cannot catch a specular sheet. */
		constexpr float GroundRoughness = 0.92f;

		/** Cover roughness. Slightly glossier than the ground, which helps the edges catch light. */
		constexpr float StructureRoughness = 0.75f;

		/** The colour of one surface. Never a default — every value is listed. */
		const FLinearColor& ColourFor(ESarkoSurface Surface);

		/** The roughness of one surface. */
		float RoughnessFor(ESarkoSurface Surface);
	}

	/** Maps a JSON surface name to the enum. False for anything unlisted. */
	bool ParseSurfaceName(const FString& Name, ESarkoSurface& Out);

	/** The JSON name of a surface — the inverse of ParseSurfaceName. */
	FString SurfaceName(ESarkoSurface Surface);
}
```

- [ ] **Step 5: Implement the palette**

Create `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.cpp`:

```cpp
#include "Map/SarkoMapPalette.h"

namespace
{
	/**
	 * Every surface's linear base colour and roughness, in enum order.
	 *
	 * The numbers are chosen against each other, not in isolation. Read
	 * Sarko.Config.SurfacePaletteIsReadable alongside this table: it encodes
	 * every relation that matters (dirt above ground, asphalt below it, deck
	 * far above asphalt, water blue and below ground, treeline green and below
	 * ground, ravine below water, nothing but the extraction saturated).
	 */
	struct FSurfaceStyle
	{
		FLinearColor Colour;
		float Roughness;
	};

	const FSurfaceStyle& StyleFor(ESarkoSurface Surface)
	{
		static const FSurfaceStyle Styles[static_cast<int32>(ESarkoSurface::Count)] = {
			/* Ground     */ { SarkoMap::Palette::Ground,               SarkoMap::Palette::GroundRoughness },
			/* Dirt       */ { FLinearColor(0.115f, 0.098f, 0.062f),    0.95f },
			/* Asphalt    */ { FLinearColor(0.022f, 0.022f, 0.025f),    0.80f },
			/* Concrete   */ { FLinearColor(0.235f, 0.232f, 0.222f),    0.78f },
			/* Structure  */ { SarkoMap::Palette::Structure,            SarkoMap::Palette::StructureRoughness },
			/* Rust       */ { FLinearColor(0.160f, 0.070f, 0.036f),    0.85f },
			/* Timber     */ { FLinearColor(0.185f, 0.100f, 0.055f),    0.80f },
			/* Vegetation */ { FLinearColor(0.020f, 0.042f, 0.016f),    0.95f },
			// Opaque, and that is a shipped limitation rather than a choice: a
			// translucent material cannot exist here without authoring an asset
			// (spec §5.2). Dark and blue is enough for the read from above.
			/* Water      */ { FLinearColor(0.018f, 0.028f, 0.046f),    0.55f },
			/* Ravine     */ { FLinearColor(0.013f, 0.013f, 0.010f),    0.93f },
			// Mirrors ASarkoExtractionZone's pad tint so the two cannot drift.
			/* Extraction */ { FLinearColor(0.160f, 0.620f, 0.240f),    0.70f },
		};

		const int32 Index = static_cast<int32>(Surface);
		// Count (or anything cast in from outside) falls back to the neutral
		// rather than reading past the array: a wrong colour is a bug you can
		// see, an out-of-bounds read is one you cannot.
		if (Index < 0 || Index >= static_cast<int32>(ESarkoSurface::Count))
		{
			return Styles[static_cast<int32>(ESarkoSurface::Structure)];
		}
		return Styles[Index];
	}

	/** JSON names, in enum order. Lower snake case, like every other key. */
	const TCHAR* const SurfaceNames[static_cast<int32>(ESarkoSurface::Count)] = {
		TEXT("ground"), TEXT("dirt"), TEXT("asphalt"), TEXT("concrete"), TEXT("structure"),
		TEXT("rust"), TEXT("timber"), TEXT("vegetation"), TEXT("water"), TEXT("ravine"),
		TEXT("extraction")
	};
}

const FLinearColor& SarkoMap::Palette::ColourFor(ESarkoSurface Surface)
{
	return StyleFor(Surface).Colour;
}

float SarkoMap::Palette::RoughnessFor(ESarkoSurface Surface)
{
	return StyleFor(Surface).Roughness;
}

bool SarkoMap::ParseSurfaceName(const FString& Name, ESarkoSurface& Out)
{
	for (int32 Index = 0; Index < static_cast<int32>(ESarkoSurface::Count); ++Index)
	{
		if (Name.Equals(SurfaceNames[Index], ESearchCase::IgnoreCase))
		{
			Out = static_cast<ESarkoSurface>(Index);
			return true;
		}
	}
	return false;
}

FString SarkoMap::SurfaceName(ESarkoSurface Surface)
{
	const int32 Index = static_cast<int32>(Surface);
	if (Index < 0 || Index >= static_cast<int32>(ESarkoSurface::Count))
	{
		return FString();
	}
	return SurfaceNames[Index];
}
```

- [ ] **Step 6: Move the constants and extend the block**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h`:

1. Add `#include "Map/SarkoMapPalette.h"` under the existing `#include "CoreMinimal.h"`.
2. **Delete** the whole `namespace Palette { ... }` block from `namespace SarkoMap` — the constants now live in `SarkoMapPalette.h`, which this header includes, so `SarkoMap::Palette::Ground` still resolves for every existing caller and `MapBuilderTest`'s `using namespace SarkoMap::Palette;` keeps working unchanged. Leave a one-line pointer in its place:

```cpp
	// The palette (ESarkoSurface, Palette::ColourFor, the named constants) lives
	// in Map/SarkoMapPalette.h, included above: the kind table needs it too, and
	// it must not have to include the spawner to get a colour.
```

3. Add to `FSarkoCoverBlock`, below `Extent`:

```cpp
	/**
	 * What this block is made of, for colour. Structure by default, so every
	 * block authored before surfaces existed keeps the grey it had.
	 */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;

	/**
	 * False turns the block into a flat surface the player walks over: a road,
	 * a water strip, a ravine bed. True — the default — is cover, which is what
	 * every block in the sector was before this field existed.
	 */
	UPROPERTY()
	bool bBlocksMovement = true;
```

- [ ] **Step 7: Paint per surface**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp`, replace the floor and cover spawns in `SpawnLayout`:

```cpp
	// Floor: one flattened cube covering the play area.
	SpawnMeshBox(World, CubeMesh, FVector(0.f, 0.f, -25.f), FRotator::ZeroRotator,
		FVector(Layout.Extent, Layout.Extent, 25.f), true,
		Palette::ColourFor(ESarkoSurface::Ground), Palette::RoughnessFor(ESarkoSurface::Ground));

	for (const FSarkoCoverBlock& Block : Layout.Cover)
	{
		SpawnMeshBox(World, CubeMesh, Block.Location, Block.Rotation, Block.Extent, Block.bBlocksMovement,
			Palette::ColourFor(Block.Surface), Palette::RoughnessFor(Block.Surface));
	}
```

- [ ] **Step 8: Parse the two new block fields**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp`, add to the anonymous namespace:

```cpp
	/**
	 * Reads an optional bool with the same discipline as every other optional
	 * field here: absent keeps the caller's default, present-but-not-a-bool is
	 * a named error rather than a silent false.
	 */
	bool ReadOptionalBool(const TSharedPtr<FJsonObject>& Object, const FString& Field, bool& Out, FString& OutError)
	{
		if (!Object->HasField(Field))
		{
			return true;
		}
		if (!Object->TryGetBoolField(Field, Out))
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a boolean"), *Field);
			return false;
		}
		return true;
	}

	/**
	 * Reads an optional surface name. An unlisted name is an error: falling
	 * back to grey would turn a typo in "asphalt" into a light highway across a
	 * dark map, which reads as a lighting bug and not as a data bug.
	 */
	bool ReadOptionalSurface(const TSharedPtr<FJsonObject>& Object, ESarkoSurface& Out, FString& OutError)
	{
		FString Name;
		if (!ReadOptionalString(Object, TEXT("surface"), Name, OutError))
		{
			return false;
		}
		if (Name.IsEmpty())
		{
			return true;
		}
		if (!SarkoMap::ParseSurfaceName(Name, Out))
		{
			OutError = FString::Printf(TEXT("'surface' is not a known surface: '%s'"), *Name);
			return false;
		}
		return true;
	}
```

and in the `blocks` loop, after the yaw read and before `OutDefinition.Blocks.Add(Block)`:

```cpp
				if (!ReadOptionalSurface(*Object, Block.Surface, OutError) ||
					!ReadOptionalBool(*Object, TEXT("blocksMovement"), Block.bBlocksMovement, OutError))
				{
					OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
					return false;
				}
```

`ToLayout` needs no change — it copies `Definition.Blocks` into `Layout.Cover` wholesale, so both new fields ride along.

- [ ] **Step 9: Run the tests and confirm they pass**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and `==> B+6 test(s) performed, 0 failed`. `Sarko.Config.PaletteSeparatesGroundFromCover` must still be `Success` — it is the backward-compatibility pin for the two original constants.

- [ ] **Step 10: Confirm nothing changed visually**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Expected: a PNG path. **Read the PNG.** It must be indistinguishable from the current sector — olive ground, grey cover. This task rewires how colour is chosen without changing any colour, so a visible difference means the lookup disagrees with the constants it replaced.

- [ ] **Step 11: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.cpp SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp && git commit -m "feat(map): ESarkoSurface and the section 14 palette; blocks carry a surface and a collision flag"
```

---

### Task 3: Prop kinds become lists of boxes (composite kinds)

Third, because Task 4's nine new kinds include two that cannot be expressed as one box (`pylon`, `road_sign`), and because doing the machinery separately from the content means a reviewer can reject "these extents are wrong" without rejecting "kinds can have parts".

**The current `FSarkoPropKind` cannot do this**: it is exactly one `Mesh`, one `Extent`, one `bBlocksMovement`. There is no offset, no per-part collision and no per-part surface, so a pylon is either one box (a grey slab, not a pylon) or four separate authored props that a designer must keep in formation by hand. Extending it is therefore part of this stage, as the brief anticipated.

**Authoring convention, stated once because it is a real trap:** `bridge.json` places single-box props with `pos.z` equal to the kind's own half-height, so the prop rests on the floor. That convention is **preserved** — a part's `Offset` defaults to zero, so every one of the 238 existing props spawns at exactly the same transform as before. **Composite** kinds are authored with `pos.z = 0` and each part carries its own height in `Offset.Z`. The kind table's comment says so, and a test pins that no part is authored below the floor.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.h` (`FSarkoPropPart`, `FSarkoPropKind::Parts`)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp` (the eleven existing kinds, ported unchanged, via helpers)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp` (`SpawnProps` spawns every part)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (rewrites `FSarkoPropKindsAreComplete`, +1 test)

**Interfaces:**
- Consumes: `ESarkoSurface`, `SarkoMap::Palette::ColourFor`/`RoughnessFor` (Task 2).
- Produces:
  - `FSarkoPropPart { FSoftObjectPath Mesh; FVector Extent; FVector Offset; bool bBlocksMovement; ESarkoSurface Surface; }`
  - `FSarkoPropKind { TArray<FSarkoPropPart> Parts; }` — `Parts` is never empty for a kind that resolves.
  - `SarkoMap::FindPropKind(FName, FSarkoPropKind&)` — unchanged signature, new payload.
  - `int32 SarkoMap::CountPropActors(const FSarkoMapDefinition& Definition)` — how many actors the props section will spawn. The iOS budget number, in code rather than in a comment.

- [ ] **Step 1: Write the failing tests**

Replace `FSarkoPropKindsAreComplete` in `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` with this, and append the second test after it:

```cpp
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
		if (!bFound)
		{
			continue;
		}
		// A kind with no parts resolves successfully and spawns nothing — the
		// worst possible outcome, because the map looks authored and is empty.
		TestTrue(FString::Printf(TEXT("kind '%s' has at least one part"), *Kind.ToString()),
			Resolved.Parts.Num() >= 1);
		for (int32 Index = 0; Index < Resolved.Parts.Num(); ++Index)
		{
			const FSarkoPropPart& Part = Resolved.Parts[Index];
			TestTrue(FString::Printf(TEXT("kind '%s' part %d has a positive extent"), *Kind.ToString(), Index),
				Part.Extent.GetMin() > 0.f);
			TestTrue(FString::Printf(TEXT("kind '%s' part %d names a mesh"), *Kind.ToString(), Index),
				!Part.Mesh.ToString().IsEmpty());
			// Nothing may be authored below the floor: a part whose bottom is
			// underground is invisible, and if it collides it is an invisible
			// wall. Tolerance of 1 uu for the flush case (bridge_deck).
			TestTrue(FString::Printf(TEXT("kind '%s' part %d sits on or above the floor"), *Kind.ToString(), Index),
				Part.Offset.Z - Part.Extent.Z >= -1.f);
		}
	}

	// The eleven kinds that existed before parts did must be single-box and must
	// keep their exact extents, because bridge.json's 238 props are placed with
	// pos.z equal to the kind's own half-height. A changed half-height sinks or
	// floats every instance of that kind at once.
	const TArray<TPair<FName, FVector>> LegacyExtents = {
		{ TEXT("wall"),        FVector(400.f, 60.f, 140.f) },
		{ TEXT("car_wreck"),   FVector(230.f, 95.f, 75.f) },
		{ TEXT("bus"),         FVector(600.f, 130.f, 160.f) },
		{ TEXT("house"),       FVector(500.f, 400.f, 300.f) },
		{ TEXT("fuel_pump"),   FVector(60.f, 40.f, 110.f) },
		{ TEXT("freight_car"), FVector(700.f, 150.f, 200.f) },
		{ TEXT("water_tower"), FVector(220.f, 220.f, 700.f) },
		{ TEXT("sandbag"),     FVector(180.f, 70.f, 55.f) },
		{ TEXT("crate"),       FVector(70.f, 70.f, 70.f) },
		{ TEXT("pipe"),        FVector(90.f, 90.f, 600.f) },
		{ TEXT("bridge_deck"), FVector(900.f, 300.f, 30.f) },
	};
	for (const TPair<FName, FVector>& Expected : LegacyExtents)
	{
		FSarkoPropKind Resolved;
		if (!SarkoMap::FindPropKind(Expected.Key, Resolved) || Resolved.Parts.Num() != 1)
		{
			AddError(FString::Printf(TEXT("kind '%s' must still be a single box"), *Expected.Key.ToString()));
			continue;
		}
		TestTrue(FString::Printf(TEXT("kind '%s' keeps its extent"), *Expected.Key.ToString()),
			Resolved.Parts[0].Extent.Equals(Expected.Value, 0.01f));
		TestTrue(FString::Printf(TEXT("kind '%s' keeps its zero offset"), *Expected.Key.ToString()),
			Resolved.Parts[0].Offset.IsNearlyZero());
		TestTrue(FString::Printf(TEXT("kind '%s' still blocks movement"), *Expected.Key.ToString()),
			Resolved.Parts[0].bBlocksMovement);
	}

	FSarkoPropKind Unknown;
	TestFalse(TEXT("an unknown kind does not resolve"), SarkoMap::FindPropKind(TEXT("nonsense"), Unknown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropActorCountIsWithinTheMobileBudget,
	"Sarko.Map.PropActorCountIsWithinTheMobileBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropActorCountIsWithinTheMobileBudget::RunTest(const FString& Parameters)
{
	// Composite kinds make one authored prop cost several actors, and ТЗ §16 is
	// a budget, not advice. This is the number that decides when instancing
	// stops being premature: see the Global Constraints' actor tally.
	FSarkoMapDefinition Map;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const int32 PropActors = SarkoMap::CountPropActors(Map);
	TestTrue(TEXT("every prop resolves, so the count is not silently short"),
		PropActors >= Map.Props.Num());
	TestTrue(FString::Printf(TEXT("props stay inside the mobile actor budget (%d)"), PropActors),
		PropActors <= 400);

	// One authored prop of a single-box kind is exactly one actor: this is the
	// promise that adding parts cost the existing map nothing.
	FSarkoMapDefinition OneCrate;
	FString ParseError;
	const FString Json = TEXT(R"({
		"id": "one",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "pos": [0, 0, 100], "yaw": 0 } ],
		"props": [ { "kind": "crate", "pos": [100, 100, 70] } ]
	})");
	if (!SarkoMap::ParseDefinition(Json, OneCrate, ParseError))
	{
		AddError(FString::Printf(TEXT("fixture failed to parse: %s"), *ParseError));
		return false;
	}
	TestEqual(TEXT("a single-box prop is one actor"), SarkoMap::CountPropActors(OneCrate), 1);
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `BUILD FAILED` with `no member named 'Parts' in 'FSarkoPropKind'` and `no member named 'CountPropActors' in namespace 'SarkoMap'`.

- [ ] **Step 3: Rewrite the kind struct**

Replace the contents of `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.h` with:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapPalette.h"

#include "SarkoMapKinds.generated.h"

// Forward-declared at global scope, never as an elaborated specifier inside
// namespace SarkoMap below — see the comment in Map/SarkoMapBuilder.h for the
// bug that rule exists to prevent.
struct FSarkoMapDefinition;

/**
 * One box of a prop.
 *
 * Every mesh is an engine primitive referenced by path: this project authors no
 * assets, so a "car wreck" is a scaled box until real art arrives at the same
 * coordinates.
 */
USTRUCT()
struct FSarkoPropPart
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Mesh;

	/** Half-extents in unreal units. The pawn is ~176 uu tall for scale. */
	UPROPERTY()
	FVector Extent = FVector(100.f);

	/**
	 * Offset from the prop's own origin, in the prop's unrotated frame; the
	 * prop's yaw rotates it. Zero for a single-box kind, which is what keeps
	 * every prop authored before parts existed at exactly its old transform.
	 *
	 * AUTHORING CONVENTION: a single-box prop is placed in the map file with
	 * pos.z equal to the kind's own half-height, so it rests on the floor. A
	 * COMPOSITE kind is placed with pos.z = 0 and each part carries its own
	 * centre height here. Mixing the two conventions buries or floats the prop.
	 */
	UPROPERTY()
	FVector Offset = FVector::ZeroVector;

	/** False for decoration the player can walk through, true for cover. */
	UPROPERTY()
	bool bBlocksMovement = true;

	/** What this part is made of, for colour (ТЗ §14). */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;
};

/**
 * How one prop kind is built: one or more boxes, spawned as one actor each.
 *
 * A list rather than a single box because ТЗ §32 asks for things that are not
 * boxes — a pylon is legs plus crossarms, a road sign is a post plus a plate,
 * a fuel canopy is a roof plus posts — and authoring those as separate props
 * means a designer keeps four entries in formation by hand for every one.
 */
USTRUCT()
struct FSarkoPropKind
{
	GENERATED_BODY()

	/** Never empty for a kind that resolves; pinned by a test. */
	UPROPERTY()
	TArray<FSarkoPropPart> Parts;
};

namespace SarkoMap
{
	/** Looks up a kind by name. False for an unknown kind — never a default. */
	bool FindPropKind(FName Kind, FSarkoPropKind& OutKind);

	/**
	 * How many actors the props section of a definition will spawn — the sum of
	 * every resolved kind's part count. Unknown kinds contribute nothing,
	 * exactly as SpawnProps skips them. This is the ТЗ §16 budget number, and a
	 * test holds it to a ceiling.
	 */
	int32 CountPropActors(const FSarkoMapDefinition& Definition);
}
```

- [ ] **Step 4: Port the eleven kinds unchanged**

Replace `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp`'s namespace block and add `CountPropActors`:

```cpp
#include "Map/SarkoMapKinds.h"

#include "Map/SarkoMapDefinition.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const FString Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	/** One box, centred on the prop's origin. The shape of every legacy kind. */
	FSarkoPropKind Box(const FString& Mesh, const FVector& Extent, bool bBlocks, ESarkoSurface Surface)
	{
		FSarkoPropPart Part;
		Part.Mesh = FSoftObjectPath(Mesh);
		Part.Extent = Extent;
		Part.bBlocksMovement = bBlocks;
		Part.Surface = Surface;

		FSarkoPropKind Kind;
		Kind.Parts.Add(Part);
		return Kind;
	}

	/** One part of a composite kind, offset from the prop's origin. */
	FSarkoPropPart Part(const FString& Mesh, const FVector& Extent, const FVector& Offset,
		bool bBlocks, ESarkoSurface Surface)
	{
		FSarkoPropPart Result;
		Result.Mesh = FSoftObjectPath(Mesh);
		Result.Extent = Extent;
		Result.Offset = Offset;
		Result.bBlocksMovement = bBlocks;
		Result.Surface = Surface;
		return Result;
	}

	/** Several boxes as one authored entry. pos.z = 0; the parts carry height. */
	FSarkoPropKind Composite(TArray<FSarkoPropPart> Parts)
	{
		FSarkoPropKind Kind;
		Kind.Parts = MoveTemp(Parts);
		return Kind;
	}

	/**
	 * The whole prop vocabulary of the Bridge sector.
	 *
	 * Sizes are chosen against a ~176 uu tall pawn: a car wreck is chest-high
	 * cover you can shoot over, a house is tall enough to break line of sight
	 * entirely, sandbags are crouch-height. That relationship is the level
	 * design — the numbers are not arbitrary.
	 *
	 * Every extent below is byte-identical to the pre-parts table: 238 props in
	 * bridge.json are placed with pos.z equal to their kind's half-height, so a
	 * changed half-height moves every instance of that kind at once.
	 */
	const TMap<FName, FSarkoPropKind>& KindTable()
	{
		static const TMap<FName, FSarkoPropKind> Table = {
			{ TEXT("wall"),        Box(Cube,     FVector(400.f, 60.f, 140.f),  true, ESarkoSurface::Structure) },
			{ TEXT("car_wreck"),   Box(Cube,     FVector(230.f, 95.f, 75.f),   true, ESarkoSurface::Structure) },
			{ TEXT("bus"),         Box(Cube,     FVector(600.f, 130.f, 160.f), true, ESarkoSurface::Structure) },
			{ TEXT("house"),       Box(Cube,     FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Structure) },
			{ TEXT("fuel_pump"),   Box(Cube,     FVector(60.f, 40.f, 110.f),   true, ESarkoSurface::Structure) },
			{ TEXT("freight_car"), Box(Cube,     FVector(700.f, 150.f, 200.f), true, ESarkoSurface::Structure) },
			{ TEXT("water_tower"), Box(Cylinder, FVector(220.f, 220.f, 700.f), true, ESarkoSurface::Structure) },
			{ TEXT("sandbag"),     Box(Cube,     FVector(180.f, 70.f, 55.f),   true, ESarkoSurface::Structure) },
			{ TEXT("crate"),       Box(Cube,     FVector(70.f, 70.f, 70.f),    true, ESarkoSurface::Structure) },
			{ TEXT("pipe"),        Box(Cylinder, FVector(90.f, 90.f, 600.f),   true, ESarkoSurface::Structure) },
			{ TEXT("bridge_deck"), Box(Cube,     FVector(900.f, 300.f, 30.f),  true, ESarkoSurface::Structure) },
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

int32 SarkoMap::CountPropActors(const FSarkoMapDefinition& Definition)
{
	int32 Total = 0;
	for (const FSarkoMapProp& Prop : Definition.Props)
	{
		FSarkoPropKind Kind;
		if (FindPropKind(Prop.Kind, Kind))
		{
			Total += Kind.Parts.Num();
		}
	}
	return Total;
}
```

The eleven kinds keep `ESarkoSurface::Structure` in this task **on purpose**: re-surfacing them (rust for freight cars, timber for village walls) is a content judgement that belongs with Task 4's palette pass, and keeping it out of here means the screenshot after this task must be identical.

- [ ] **Step 5: Spawn every part**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp`, replace the body of `SpawnProps`' loop after the kind lookup:

```cpp
		const FRotator Rotation(0.f, Prop.Yaw, 0.f);
		for (const FSarkoPropPart& Part : Kind.Parts)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Part.Mesh.TryLoad());
			if (!Mesh)
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoMap: mesh missing for kind '%s'"), *Prop.Kind.ToString());
				++Skipped;
				continue;
			}
			// The part's offset is authored in the prop's own frame, so it
			// rotates with the prop: a road sign's plate stays over its post at
			// any yaw. Rotating the offset and not the part would shear the
			// composite apart at every angle except zero.
			const FVector PartLocation = Prop.Location + Rotation.RotateVector(Part.Offset);
			SpawnMeshBox(World, Mesh, PartLocation, Rotation, Part.Extent, Part.bBlocksMovement,
				Palette::ColourFor(Part.Surface), Palette::RoughnessFor(Part.Surface));
		}
```

and change the closing log so the counts still mean something with parts in play:

```cpp
	UE_LOG(LogTemp, Display, TEXT("SarkoMap: spawned %d prop actors from %d authored props, skipped %d parts"),
		CountPropActors(Definition) - Skipped, Definition.Props.Num(), Skipped);
```

`Skipped` keeps counting *parts* now rather than props, which is what the message says. The unknown-kind `continue` above it still increments `Skipped` once per unknown prop; that is a deliberate approximation of "at least one thing did not appear" and the log names both totals so it cannot mislead.

- [ ] **Step 6: Run the tests and confirm they pass**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and `==> B+7 test(s) performed, 0 failed`. `FSarkoPropKindsAreComplete` was **rewritten in place**, not added, so this task adds exactly one test — `Sarko.Map.PropActorCountIsWithinTheMobileBudget`. (Record the actual number in the task report and carry it forward.)

- [ ] **Step 7: Confirm nothing moved**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Expected: a PNG path. **Read the PNG and compare it to Task 2's.** The frames must be identical: 238 props, same places, same grey. Any prop that moved means a part offset leaked into a legacy kind.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.h SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp && git commit -m "feat(map): prop kinds are lists of boxes, so a pylon is one authored entry"
```

---

### Task 4: The nine new prop kinds (ТЗ §15 / §32)

Fourth: the content of the kind table, now that it can hold it. Spec §5.3 names exactly these nine and they are all the north needs filling with (ТЗ §15: "25–40 камней, 20–35 кустов, 10–15 брёвен, 8–12 заборов, 5–8 остовов, опоры ЛЭП, 2–3 техстроения — крупные формы, не сотни мелочи").

Two deliberate design calls, both from the spec:

- **`bush` does not collide.** It is the only decoration in the vocabulary the player walks through, and that is the point: it breaks up the ground visually without adding a snag the player fights.
- **`treeline` replaces trees.** A canopy hides the player from a top-down camera, which is a gameplay defect. A treeline is a tall dark-green impassable wall, tiled along a boundary — forest as a border, never as cover the player stands under.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp` (nine entries)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (+2 tests)

**Interfaces:**
- Consumes: `Box`, `Part`, `Composite`, `Cube`, `Cylinder`, `Sphere`, `ESarkoSurface` (Task 3).
- Produces: the kind names `rock`, `bush`, `log`, `fence_section`, `road_sign`, `concrete_barrier`, `trailer`, `pylon`, `treeline`.

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoNewPropKindsExist,
	"Sarko.Map.NewPropKindsExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoNewPropKindsExist::RunTest(const FString& Parameters)
{
	// Spec §5.3's vocabulary, by name. A missing kind here is a Stage C author
	// discovering mid-sector that the thing the ТЗ asked for does not exist.
	const TArray<FName> Required = {
		TEXT("rock"), TEXT("bush"), TEXT("log"), TEXT("fence_section"), TEXT("road_sign"),
		TEXT("concrete_barrier"), TEXT("trailer"), TEXT("pylon"), TEXT("treeline")
	};

	for (const FName& Kind : Required)
	{
		FSarkoPropKind Resolved;
		const bool bFound = SarkoMap::FindPropKind(Kind, Resolved);
		TestTrue(FString::Printf(TEXT("kind '%s' resolves"), *Kind.ToString()), bFound);
		if (!bFound)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("kind '%s' has parts"), *Kind.ToString()), Resolved.Parts.Num() >= 1);
		for (int32 Index = 0; Index < Resolved.Parts.Num(); ++Index)
		{
			const FSarkoPropPart& Piece = Resolved.Parts[Index];
			TestTrue(FString::Printf(TEXT("'%s' part %d has a positive extent"), *Kind.ToString(), Index),
				Piece.Extent.GetMin() > 0.f);
			TestTrue(FString::Printf(TEXT("'%s' part %d names an engine mesh"), *Kind.ToString(), Index),
				Piece.Mesh.ToString().StartsWith(TEXT("/Engine/BasicShapes/")));
			TestTrue(FString::Printf(TEXT("'%s' part %d is on or above the floor"), *Kind.ToString(), Index),
				Piece.Offset.Z - Piece.Extent.Z >= -1.f);
		}
	}

	// The two composites are the reason parts exist. If either collapses back to
	// one box, the abstraction bought nothing.
	FSarkoPropKind Pylon;
	if (SarkoMap::FindPropKind(TEXT("pylon"), Pylon))
	{
		TestTrue(TEXT("a pylon is composite"), Pylon.Parts.Num() >= 4);
	}
	FSarkoPropKind Sign;
	if (SarkoMap::FindPropKind(TEXT("road_sign"), Sign))
	{
		TestTrue(TEXT("a road sign is a post and a plate"), Sign.Parts.Num() >= 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPropKindScaleMatchesThePawn,
	"Sarko.Map.PropKindScaleMatchesThePawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPropKindScaleMatchesThePawn::RunTest(const FString& Parameters)
{
	// The pawn is ~176 uu tall. Whether a prop is shootable-over cover or a
	// sight blocker is a gameplay fact, so it is asserted rather than left to
	// whoever next edits a number in the table.
	constexpr float PawnHeightUU = 176.f;

	const auto TopOf = [this](FName Name, float& OutTop) -> bool
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Name, Kind) || Kind.Parts.Num() == 0)
		{
			AddError(FString::Printf(TEXT("kind '%s' does not resolve"), *Name.ToString()));
			return false;
		}
		OutTop = 0.f;
		for (const FSarkoPropPart& Piece : Kind.Parts)
		{
			if (Piece.bBlocksMovement)
			{
				OutTop = FMath::Max(OutTop, static_cast<float>(Piece.Offset.Z + Piece.Extent.Z));
			}
		}
		return true;
	};

	// Cover you shoot over: below the pawn's full height, above its knees.
	for (const FName& Name : { FName(TEXT("car_wreck")), FName(TEXT("sandbag")),
		FName(TEXT("concrete_barrier")), FName(TEXT("log")), FName(TEXT("rock")) })
	{
		float Top = 0.f;
		if (TopOf(Name, Top))
		{
			TestTrue(FString::Printf(TEXT("'%s' can be shot over"), *Name.ToString()), Top < PawnHeightUU);
			TestTrue(FString::Printf(TEXT("'%s' is tall enough to be cover"), *Name.ToString()), Top > 60.f);
		}
	}

	// Sight blockers: taller than the pawn, so they cut line of sight outright.
	for (const FName& Name : { FName(TEXT("house")), FName(TEXT("wall")),
		FName(TEXT("treeline")), FName(TEXT("fence_section")) })
	{
		float Top = 0.f;
		if (TopOf(Name, Top))
		{
			TestTrue(FString::Printf(TEXT("'%s' blocks sight"), *Name.ToString()), Top >= PawnHeightUU);
		}
	}

	// A bush the player cannot walk through is a snag, not decoration.
	FSarkoPropKind Bush;
	if (SarkoMap::FindPropKind(TEXT("bush"), Bush))
	{
		for (const FSarkoPropPart& Piece : Bush.Parts)
		{
			TestFalse(TEXT("a bush never blocks movement"), Piece.bBlocksMovement);
		}
		TestEqual(TEXT("a bush is vegetation"),
			static_cast<uint8>(Bush.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
	}

	// A treeline is the map's boundary: impassable, dark green, and taller than
	// anything the player can climb (there is no climbing).
	FSarkoPropKind Treeline;
	if (SarkoMap::FindPropKind(TEXT("treeline"), Treeline))
	{
		TestTrue(TEXT("a treeline blocks movement"), Treeline.Parts[0].bBlocksMovement);
		TestEqual(TEXT("a treeline is vegetation"),
			static_cast<uint8>(Treeline.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Vegetation));
		TestTrue(TEXT("a treeline is long enough to tile into a border"),
			Treeline.Parts[0].Extent.X >= 400.f);
	}

	// Nothing may be a skyscraper: the top-down camera frames the whole sector
	// from 20000+ uu, and a 50 m prop is a smear across the frame.
	const TArray<FName> AllKinds = {
		TEXT("wall"), TEXT("car_wreck"), TEXT("bus"), TEXT("house"), TEXT("fuel_pump"),
		TEXT("freight_car"), TEXT("water_tower"), TEXT("sandbag"), TEXT("crate"), TEXT("pipe"),
		TEXT("bridge_deck"), TEXT("rock"), TEXT("bush"), TEXT("log"), TEXT("fence_section"),
		TEXT("road_sign"), TEXT("concrete_barrier"), TEXT("trailer"), TEXT("pylon"), TEXT("treeline")
	};
	for (const FName& Name : AllKinds)
	{
		FSarkoPropKind Kind;
		if (!SarkoMap::FindPropKind(Name, Kind))
		{
			AddError(FString::Printf(TEXT("kind '%s' does not resolve"), *Name.ToString()));
			continue;
		}
		for (const FSarkoPropPart& Piece : Kind.Parts)
		{
			TestTrue(FString::Printf(TEXT("'%s' is under 25 m tall"), *Name.ToString()),
				Piece.Offset.Z + Piece.Extent.Z <= 2500.f);
			TestTrue(FString::Printf(TEXT("'%s' is under 20 m wide"), *Name.ToString()),
				Piece.Extent.X <= 1000.f && Piece.Extent.Y <= 1000.f);
		}
	}
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `ALL GREEN` is NOT reached — the run completes and reports failures, `kind 'rock' resolves` and eight more like it. (This is a data test, not a compile error, so the build succeeds and the test fails. That is the expected shape here.)

- [ ] **Step 3: Add the nine kinds**

Append these entries to `KindTable()` in `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp`, after `bridge_deck`:

```cpp
			// ---- ТЗ §15 / §32: filling the world. Large forms, not hundreds of
			// small ones — every entry below is one actor except the two
			// composites, and the north needs ~90 of them in total.

			// A boulder. Sphere rather than cube so the silhouette is not a
			// fourth kind of box, and chest-high so it reads as cover.
			{ TEXT("rock"),             Box(Sphere,   FVector(110.f, 95.f, 72.f),  true,  ESarkoSurface::Structure) },
			// The one thing the player walks through. Wide and low: it breaks up
			// the ground without ever being mistaken for cover.
			{ TEXT("bush"),             Box(Sphere,   FVector(135.f, 130.f, 68.f), false, ESarkoSurface::Vegetation) },
			// A fallen log. A box, not a cylinder: FSarkoPropPart has no roll, so
			// a cylinder would stand upright like a stump.
			{ TEXT("log"),              Box(Cube,     FVector(330.f, 55.f, 55.f),  true,  ESarkoSurface::Timber) },
			// Fence: long, thin, taller than the pawn, so a line of them reads as
			// a boundary from above and blocks sight at ground level.
			{ TEXT("fence_section"),    Box(Cube,     FVector(400.f, 12.f, 92.f),  true,  ESarkoSurface::Timber) },
			// Jersey barrier. Low, heavy, and the pale concrete tone, which is
			// what makes a row of them read as a deliberate closure.
			{ TEXT("concrete_barrier"), Box(Cube,     FVector(150.f, 45.f, 55.f),  true,  ESarkoSurface::Concrete) },

			// Post plus plate: the plate is pale concrete so it catches the eye
			// from above, which is the entire job of a road sign in this game.
			// The post blocks (you cannot walk through a pole); the plate is 320
			// uu up and does not, so it never snags the pawn.
			{ TEXT("road_sign"),        Composite({
				Part(Cube, FVector(8.f, 8.f, 150.f),   FVector(0.f, 0.f, 150.f), true,  ESarkoSurface::Structure),
				Part(Cube, FVector(85.f, 10.f, 55.f),  FVector(0.f, 0.f, 320.f), false, ESarkoSurface::Concrete),
			}) },

			// Cargo trailer: body plus tow bar. Rust, because it belongs to the
			// industrial and roadside vocabulary, and ТЗ L01 puts junk loot in one.
			{ TEXT("trailer"),          Composite({
				Part(Cube, FVector(350.f, 120.f, 110.f), FVector(0.f, 0.f, 140.f),   true, ESarkoSurface::Rust),
				Part(Cube, FVector(90.f, 15.f, 12.f),    FVector(-440.f, 0.f, 62.f), true, ESarkoSurface::Rust),
			}) },

			// ЛЭП pylon: two legs and two crossarms. An A-frame reads correctly
			// from directly above and costs four actors instead of the six a
			// four-legged tower would; the crossarms are 15 m up and do not
			// collide, so they cost no physics at all.
			{ TEXT("pylon"),            Composite({
				Part(Cube, FVector(30.f, 30.f, 900.f),  FVector(-140.f, 0.f, 900.f), true,  ESarkoSurface::Rust),
				Part(Cube, FVector(30.f, 30.f, 900.f),  FVector(140.f, 0.f, 900.f),  true,  ESarkoSurface::Rust),
				Part(Cube, FVector(420.f, 25.f, 20.f),  FVector(0.f, 0.f, 1500.f),   false, ESarkoSurface::Structure),
				Part(Cube, FVector(300.f, 25.f, 20.f),  FVector(0.f, 0.f, 1780.f),   false, ESarkoSurface::Structure),
			}) },

			// The forest, as a border. NOT trees: a canopy hides the player from
			// a top-down camera, which is a gameplay defect and not a look
			// (spec §5.3). Tile these along an edge; 1200 uu long each, 10 m
			// tall, dark green, impassable.
			{ TEXT("treeline"),         Box(Cube,     FVector(600.f, 200.f, 500.f), true,  ESarkoSurface::Vegetation) },
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and the suite total is 2 higher than after Task 3. `Sarko.Map.NewPropKindsExist` and `Sarko.Map.PropKindScaleMatchesThePawn` are both `Success`.

No screenshot for this task: nothing places any of these kinds yet, so the frame cannot change. Task 8's shot is where they first appear.

- [ ] **Step 5: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp && git commit -m "feat(map): nine new prop kinds, including the pylon and the treeline"
```

---

### Task 5: The building expander (pure, no JSON, no world)

Fifth, and the heart of the stage. Twenty buildings authored as ~400 hand-placed walls is not maintainable, and it is not reviewable either: nobody can look at a list of wall coordinates and say whether the doors line up. **One declaration in, walls out, deterministic, and every invariant a test** — that is the whole reason this abstraction exists.

This task builds the structs and the function with no JSON and no world at all. Task 7 wires it to the parser and the spawner. The split is deliberate: the geometry is where the bugs live, and it can be tested exhaustively from string-free C++ under `-nullrhi`.

**The local frame, defined once:** the origin is the centre of the footprint at floor level; `size` is the **full outer** footprint (X × Y) before yaw; `+Y` is local north, `+X` local east. Sides are `N` (the `+Y` edge), `E` (`+X`), `S` (`-Y`), `W` (`-X`). Walls are built **inside** the footprint, so `size` is the building's true outer dimension. N and S walls span the full width; E and W walls are shortened by one thickness at each end so they butt into them — **no wall ever overlaps another wall**, which is what makes the coverage and overlap tests exact rather than approximate.

**A door's `offset` is signed, measured from the midpoint of its side** (local X for N/S, local Y for E/W). Signed-from-centre rather than distance-from-a-corner because buildings are usually symmetric and `0` should mean "centred"; and because a validator can check both ends of a span with one comparison.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/Map/SarkoBuildings.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Tests/BuildingTest.cpp` (6 tests)

**Interfaces:**
- Consumes: `FSarkoCoverBlock` (from `Map/SarkoMapBuilder.h`, including its Task 2 `Surface`/`bBlocksMovement` and Task 1 `Id`), `ESarkoSurface`.
- Produces:
  - `enum class ESarkoBuildingSide : uint8 { North, East, South, West }`
  - `FSarkoBuildingDoor { ESarkoBuildingSide Side; float OffsetUU; float WidthUU; }`
  - `FSarkoBuildingInteriorWall { FVector2D From; FVector2D To; bool bHasDoor; FSarkoBuildingDoor Door; }`
  - `FSarkoBuilding { FString Id; FVector Location; FVector2D SizeUU; float Yaw; float WallHeightUU; float WallThicknessUU; ESarkoSurface Surface; TArray<FSarkoBuildingDoor> Doors; TArray<FSarkoBuildingInteriorWall> InteriorWalls; }`
  - `bool SarkoMap::ExpandBuilding(const FSarkoBuilding&, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)` — **resets** `OutBlocks`
  - `bool SarkoMap::ExpandBuildings(const TArray<FSarkoBuilding>&, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)` — resets, then accumulates
  - `bool SarkoMap::IsPointInsideBlocksXY(const FVector2D& Point, const TArray<FSarkoCoverBlock>& Blocks)`
  - `bool SarkoMap::ParseBuildingSide(const FString& Name, ESarkoBuildingSide& Out)`
  - Constants `SarkoMap::MinDoorwayUU = 250.f`, `SarkoMap::PreferredDoorwayUU = 300.f`, `SarkoMap::MinInteriorPassageUU = 250.f`

- [ ] **Step 1: Write the failing tests**

Create `SarkoGame/Source/SarkoGame/Tests/BuildingTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Map/SarkoBuildings.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** A 2000 x 1500 shed with two doors — the shape of half the ТЗ's ledger. */
	FSarkoBuilding MakeShed()
	{
		FSarkoBuilding Building;
		Building.Id = TEXT("test_shed");
		Building.Location = FVector(0.f, 0.f, 0.f);
		Building.SizeUU = FVector2D(2000.f, 1500.f);
		Building.WallHeightUU = 350.f;
		Building.WallThicknessUU = 30.f;
		Building.Surface = ESarkoSurface::Structure;
		return Building;
	}

	FSarkoBuildingDoor MakeDoor(ESarkoBuildingSide Side, float Offset, float Width)
	{
		FSarkoBuildingDoor Door;
		Door.Side = Side;
		Door.OffsetUU = Offset;
		Door.WidthUU = Width;
		return Door;
	}

	/**
	 * Walks a straight line in the horizontal plane and counts how many samples
	 * land inside a wall. Sampling rather than analysis on purpose: it tests the
	 * geometry that was actually emitted, not the geometry the expander meant to
	 * emit, so an off-by-one in a segment's centre cannot hide behind matching
	 * arithmetic in the test.
	 */
	int32 CountInside(const TArray<FSarkoCoverBlock>& Blocks, const FVector2D& From, const FVector2D& To, int32 Samples)
	{
		int32 Inside = 0;
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(Samples);
			if (SarkoMap::IsPointInsideBlocksXY(From + (To - From) * Alpha, Blocks))
			{
				++Inside;
			}
		}
		return Inside;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoClosedBuildingIsSealed,
	"Sarko.Map.ClosedBuildingIsSealed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoClosedBuildingIsSealed::RunTest(const FString& Parameters)
{
	// A building with no doors is the degenerate case the ТЗ's ledger needs four
	// times (N01, S06, S17, S18 are закрыты) and it must be genuinely sealed:
	// four walls, meeting at the corners, with nothing inside and no lid.
	const FSarkoBuilding Shed = MakeShed();
	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a closed building expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Shed, Blocks, Error));
	TestEqual(TEXT("a closed rectangle is exactly four walls"), Blocks.Num(), 4);

	const float T = Shed.WallThicknessUU;
	const float HalfX = Shed.SizeUU.X * 0.5f;
	const float HalfY = Shed.SizeUU.Y * 0.5f;

	// No roof and no floor slab: a top-down camera must see inside, and spec
	// §5.1 forbids both. Every emitted block is a WALL, i.e. thin on exactly one
	// horizontal axis. A roof or a floor would be thick on both.
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		const bool bThinX = Block.Extent.X <= T * 0.5f + 0.01f;
		const bool bThinY = Block.Extent.Y <= T * 0.5f + 0.01f;
		TestTrue(TEXT("every emitted block is a wall, thin on one axis"), bThinX || bThinY);
		TestFalse(TEXT("no block is thin on both axes (that would be a post, not a wall)"), bThinX && bThinY);
		TestTrue(TEXT("walls are the building's height"),
			FMath::IsNearlyEqual(static_cast<float>(Block.Extent.Z), Shed.WallHeightUU * 0.5f, 0.01f));
		TestTrue(TEXT("walls stand on the floor, not through it"),
			FMath::IsNearlyEqual(static_cast<float>(Block.Location.Z), Shed.WallHeightUU * 0.5f, 0.01f));
		TestTrue(TEXT("walls block movement and bullets"), Block.bBlocksMovement);
		TestEqual(TEXT("walls carry the building's surface"),
			static_cast<uint8>(Block.Surface), static_cast<uint8>(Shed.Surface));
		TestTrue(TEXT("every wall is traceable to its building"), Block.Id.StartsWith(Shed.Id));
	}

	// The interior is empty. This is the "no roof, no floor" assertion from the
	// other side: if either existed, the centre of the building would be solid.
	TestFalse(TEXT("the middle of the building is walkable"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(0.f, 0.f), Blocks));

	// The shell closes. Walk each wall's centre line end to end: with no doors,
	// every single sample must be inside a wall. One gap anywhere and the
	// building is not a building.
	const float WallY = HalfY - T * 0.5f;
	const float WallX = HalfX - T * 0.5f;
	TestEqual(TEXT("the north wall has no gaps"),
		CountInside(Blocks, FVector2D(-HalfX + 1.f, WallY), FVector2D(HalfX - 1.f, WallY), 200), 201);
	TestEqual(TEXT("the south wall has no gaps"),
		CountInside(Blocks, FVector2D(-HalfX + 1.f, -WallY), FVector2D(HalfX - 1.f, -WallY), 200), 201);
	TestEqual(TEXT("the east wall has no gaps"),
		CountInside(Blocks, FVector2D(WallX, -HalfY + T + 1.f), FVector2D(WallX, HalfY - T - 1.f), 200), 201);
	TestEqual(TEXT("the west wall has no gaps"),
		CountInside(Blocks, FVector2D(-WallX, -HalfY + T + 1.f), FVector2D(-WallX, HalfY - T - 1.f), 200), 201);

	// No wall overlaps another. Overlapping geometry is not a rendering problem
	// here, it is a review problem: it makes "how much wall did we emit" and
	// "how much wall is there" different numbers, which is how a missing segment
	// hides.
	for (int32 A = 0; A < Blocks.Num(); ++A)
	{
		for (int32 B = A + 1; B < Blocks.Num(); ++B)
		{
			const bool bOverlapX = FMath::Abs(Blocks[A].Location.X - Blocks[B].Location.X)
				< Blocks[A].Extent.X + Blocks[B].Extent.X - 0.01f;
			const bool bOverlapY = FMath::Abs(Blocks[A].Location.Y - Blocks[B].Location.Y)
				< Blocks[A].Extent.Y + Blocks[B].Extent.Y - 0.01f;
			TestFalse(FString::Printf(TEXT("walls %d and %d do not overlap"), A, B), bOverlapX && bOverlapY);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDoorwaysAreRealGaps,
	"Sarko.Map.DoorwaysAreRealGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDoorwaysAreRealGaps::RunTest(const FString& Parameters)
{
	// The single invariant this whole abstraction exists to guarantee: a doorway
	// declared in data is an actual hole in the actual geometry. The failure it
	// guards against is not theoretical — an off-by-half-thickness in a segment
	// centre leaves a 15 uu lip across the opening, which is invisible in a
	// screenshot and stops the pawn dead.
	FSarkoBuilding Shed = MakeShed();
	Shed.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 300.f, 320.f));
	Shed.Doors.Add(MakeDoor(ESarkoBuildingSide::South, -400.f, 300.f));

	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a building with two doors expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Shed, Blocks, Error));
	// Two doors, each splitting one side in two: 4 + 2 = 6 segments.
	TestEqual(TEXT("two doors turn four walls into six segments"), Blocks.Num(), 6);

	const float T = Shed.WallThicknessUU;
	const float WallX = Shed.SizeUU.X * 0.5f - T * 0.5f;
	const float WallY = Shed.SizeUU.Y * 0.5f - T * 0.5f;

	// The east door: clear across its whole declared width, at the wall's own
	// centre line. Sampled 1 uu inside each edge so the test is about the gap
	// and not about floating-point equality at the boundary.
	TestEqual(TEXT("the east doorway is completely open"),
		CountInside(Blocks, FVector2D(WallX, 300.f - 160.f + 1.f), FVector2D(WallX, 300.f + 160.f - 1.f), 60), 0);
	// And the wall resumes immediately outside it, both sides. A "gap" that is
	// really the end of the wall would pass the test above and fail this one.
	TestTrue(TEXT("the east wall resumes north of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, 300.f + 160.f + 5.f), Blocks));
	TestTrue(TEXT("the east wall resumes south of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, 300.f - 160.f - 5.f), Blocks));

	TestEqual(TEXT("the south doorway is completely open"),
		CountInside(Blocks, FVector2D(-400.f - 150.f + 1.f, -WallY), FVector2D(-400.f + 150.f - 1.f, -WallY), 60), 0);
	TestTrue(TEXT("the south wall resumes east of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-400.f + 150.f + 5.f, -WallY), Blocks));
	TestTrue(TEXT("the south wall resumes west of the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-400.f - 150.f - 5.f, -WallY), Blocks));

	// The doorway is wide enough to walk through carrying a backpack: the pawn's
	// capsule is ~68 uu across, and ТЗ §13 sets the floor at 250 uu.
	TestTrue(TEXT("no shipped doorway is narrower than the minimum"),
		Shed.Doors[0].WidthUU >= SarkoMap::MinDoorwayUU && Shed.Doors[1].WidthUU >= SarkoMap::MinDoorwayUU);

	// Total wall length on a side equals the side's length minus the door: the
	// arithmetic check that complements the sampling. A doubled segment or a
	// missing stub changes this number and nothing else.
	float SouthLength = 0.f;
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		if (FMath::IsNearlyEqual(static_cast<float>(Block.Location.Y), -WallY, 0.01f))
		{
			SouthLength += static_cast<float>(Block.Extent.X) * 2.f;
		}
	}
	TestTrue(TEXT("the south wall is exactly its length minus the doorway"),
		FMath::IsNearlyEqual(SouthLength, Shed.SizeUU.X - 300.f, 0.05f));

	// Sides with no doors are untouched by the doors on other sides.
	TestEqual(TEXT("the north wall is still one unbroken segment"),
		CountInside(Blocks, FVector2D(-Shed.SizeUU.X * 0.5f + 1.f, WallY),
			FVector2D(Shed.SizeUU.X * 0.5f - 1.f, WallY), 200), 201);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoInteriorWallsDivideAndPass,
	"Sarko.Map.InteriorWallsDivideAndPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoInteriorWallsDivideAndPass::RunTest(const FString& Parameters)
{
	// ТЗ §9's АЗС is "зал + подсобка + служебная" — three rooms in one 2200x1500
	// shell. That is two interior walls and two interior doorways, and both the
	// dividing and the passing have to be true at once.
	FSarkoBuilding Station = MakeShed();
	Station.Id = TEXT("test_station");
	Station.SizeUU = FVector2D(2200.f, 1500.f);
	Station.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 320.f));
	Station.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 600.f, 300.f));

	const float T = Station.WallThicknessUU;
	const float InnerY = Station.SizeUU.Y * 0.5f - T;

	// One wall across the depth at local x = -300, with a door in it.
	FSarkoBuildingInteriorWall Divider;
	Divider.From = FVector2D(-300.f, -InnerY);
	Divider.To = FVector2D(-300.f, InnerY);
	Divider.bHasDoor = true;
	Divider.Door = MakeDoor(ESarkoBuildingSide::North /* ignored for interior walls */, 200.f, 300.f);
	Station.InteriorWalls.Add(Divider);

	TArray<FSarkoCoverBlock> Blocks;
	FString Error;
	TestTrue(FString::Printf(TEXT("a divided building expands: %s"), *Error),
		SarkoMap::ExpandBuilding(Station, Blocks, Error));
	// 4 perimeter walls + 2 door splits + 2 interior segments = 8.
	TestEqual(TEXT("segment count accounts for every wall and every door"), Blocks.Num(), 8);

	// Both rooms are walkable...
	TestFalse(TEXT("the west room is walkable"), SarkoMap::IsPointInsideBlocksXY(FVector2D(-700.f, 0.f), Blocks));
	TestFalse(TEXT("the east room is walkable"), SarkoMap::IsPointInsideBlocksXY(FVector2D(500.f, 0.f), Blocks));

	// ...they are genuinely separated: a straight line between them crosses the
	// divider everywhere except at the doorway.
	TestTrue(TEXT("the divider is solid away from its door"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, -500.f), Blocks));
	TestTrue(TEXT("the divider is solid on the far side too"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, 600.f), Blocks));

	// ...and the interior doorway is a real, walkable gap of the declared width.
	TestEqual(TEXT("the interior doorway is completely open"),
		CountInside(Blocks, FVector2D(-300.f, 200.f - 150.f + 1.f), FVector2D(-300.f, 200.f + 150.f - 1.f), 60), 0);
	TestTrue(TEXT("the interior doorway is at least the minimum passage"),
		Station.InteriorWalls[0].Door.WidthUU >= SarkoMap::MinInteriorPassageUU);

	// A wall with no door is one unbroken segment — the closet/store-room case.
	FSarkoBuilding Solid = Station;
	Solid.InteriorWalls[0].bHasDoor = false;
	TArray<FSarkoCoverBlock> SolidBlocks;
	TestTrue(TEXT("a doorless divider expands"), SarkoMap::ExpandBuilding(Solid, SolidBlocks, Error));
	TestEqual(TEXT("a doorless divider is one segment"), SolidBlocks.Num(), 7);
	TestTrue(TEXT("a doorless divider is solid where the door used to be"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-300.f, 200.f), SolidBlocks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingExpansionRejectsBadGeometry,
	"Sarko.Map.BuildingExpansionRejectsBadGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingExpansionRejectsBadGeometry::RunTest(const FString& Parameters)
{
	// A building is hand-authored, so it will be wrong eventually, and every one
	// of these produces geometry that looks plausible in a screenshot and is
	// broken to play: a 200 uu door the pawn cannot fit through, a door hanging
	// off the end of a wall, a room with one exit, a 120 uu corridor.
	TArray<TPair<FString, FSarkoBuilding>> BadCases;

	{
		FSarkoBuilding B = MakeShed();
		B.Id.Reset();
		BadCases.Add({ TEXT("no id"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.SizeUU = FVector2D(400.f, 1500.f);
		BadCases.Add({ TEXT("footprint too small to stand in"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.WallHeightUU = 120.f;
		BadCases.Add({ TEXT("wall shorter than the pawn"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.WallThicknessUU = 0.f;
		BadCases.Add({ TEXT("zero wall thickness"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 320.f));
		BadCases.Add({ TEXT("exactly one exit"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 200.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		BadCases.Add({ TEXT("doorway narrower than 250 uu"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 950.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 0.f, 300.f));
		BadCases.Add({ TEXT("doorway runs off the end of its wall"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -100.f, 400.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 100.f, 400.f));
		BadCases.Add({ TEXT("two doorways on one side overlap"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -200.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 160.f, 300.f));
		BadCases.Add({ TEXT("two doorways leave no wall between them"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Diagonal;
		Diagonal.From = FVector2D(-400.f, -400.f);
		Diagonal.To = FVector2D(400.f, 400.f);
		B.InteriorWalls.Add(Diagonal);
		BadCases.Add({ TEXT("diagonal interior wall"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Outside;
		Outside.From = FVector2D(-400.f, -3000.f);
		Outside.To = FVector2D(-400.f, 3000.f);
		B.InteriorWalls.Add(Outside);
		BadCases.Add({ TEXT("interior wall reaches outside the footprint"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		// 120 uu from the inner face of the east wall: an unreachable closet.
		FSarkoBuildingInteriorWall TooClose;
		TooClose.From = FVector2D(850.f, -700.f);
		TooClose.To = FVector2D(850.f, 700.f);
		B.InteriorWalls.Add(TooClose);
		BadCases.Add({ TEXT("interior passage narrower than 250 uu"), B });
	}
	{
		FSarkoBuilding B = MakeShed();
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 0.f, 300.f));
		B.Doors.Add(MakeDoor(ESarkoBuildingSide::West, 0.f, 300.f));
		FSarkoBuildingInteriorWall Divider;
		Divider.From = FVector2D(0.f, -700.f);
		Divider.To = FVector2D(0.f, 700.f);
		Divider.bHasDoor = true;
		Divider.Door = MakeDoor(ESarkoBuildingSide::North, 0.f, 180.f);
		B.InteriorWalls.Add(Divider);
		BadCases.Add({ TEXT("interior doorway narrower than 250 uu"), B });
	}

	for (const TPair<FString, FSarkoBuilding>& Case : BadCases)
	{
		TArray<FSarkoCoverBlock> Blocks;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ExpandBuilding(Case.Value, Blocks, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
		// A rejected building leaves nothing behind: a caller that ignores the
		// return value must not find half a shed in its array.
		TestEqual(FString::Printf(TEXT("nothing survives a rejection: %s"), *Case.Key), Blocks.Num(), 0);
	}

	// And the shapes that must be accepted, so the rules above cannot be
	// "correct" by rejecting everything: closed, and two doors on one side.
	{
		TArray<FSarkoCoverBlock> Blocks;
		FString Error;
		TestTrue(TEXT("a closed building is legal"), SarkoMap::ExpandBuilding(MakeShed(), Blocks, Error));

		FSarkoBuilding TwoOnASide = MakeShed();
		TwoOnASide.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -600.f, 300.f));
		TwoOnASide.Doors.Add(MakeDoor(ESarkoBuildingSide::North, 600.f, 300.f));
		TestTrue(FString::Printf(TEXT("two well-separated doors on one side are legal: %s"), *Error),
			SarkoMap::ExpandBuilding(TwoOnASide, Blocks, Error));
		TestEqual(TEXT("two doors on one side make three segments there, seven in all"), Blocks.Num(), 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingYawRotatesTheWholeShell,
	"Sarko.Map.BuildingYawRotatesTheWholeShell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingYawRotatesTheWholeShell::RunTest(const FString& Parameters)
{
	// A yawed building must be the unyawed one turned, doors included. The bug
	// this guards is the one that only shows at non-zero angles: rotating a
	// wall's own orientation but not its offset, which shears the shell open.
	FSarkoBuilding Flat = MakeShed();
	Flat.Location = FVector(5000.f, -3000.f, 0.f);
	Flat.Doors.Add(MakeDoor(ESarkoBuildingSide::East, 200.f, 300.f));
	Flat.Doors.Add(MakeDoor(ESarkoBuildingSide::West, -200.f, 300.f));

	FSarkoBuilding Turned = Flat;
	Turned.Yaw = 90.f;

	TArray<FSarkoCoverBlock> FlatBlocks;
	TArray<FSarkoCoverBlock> TurnedBlocks;
	FString Error;
	TestTrue(TEXT("the flat building expands"), SarkoMap::ExpandBuilding(Flat, FlatBlocks, Error));
	TestTrue(TEXT("the turned building expands"), SarkoMap::ExpandBuilding(Turned, TurnedBlocks, Error));
	TestEqual(TEXT("yaw changes nothing about how many walls there are"),
		TurnedBlocks.Num(), FlatBlocks.Num());

	const FRotator Rotation(0.f, 90.f, 0.f);
	for (int32 Index = 0; Index < FlatBlocks.Num(); ++Index)
	{
		const FVector ExpectedLocation = Flat.Location
			+ Rotation.RotateVector(FlatBlocks[Index].Location - Flat.Location);
		TestTrue(FString::Printf(TEXT("wall %d is where rotating it puts it"), Index),
			TurnedBlocks[Index].Location.Equals(ExpectedLocation, 0.05f));
		TestTrue(FString::Printf(TEXT("wall %d keeps its own dimensions"), Index),
			TurnedBlocks[Index].Extent.Equals(FlatBlocks[Index].Extent, 0.01f));
		// FRotator members are doubles in 5.8, so compare with Equals and a
		// tolerance rather than a float literal.
		TestTrue(FString::Printf(TEXT("wall %d is turned with the building"), Index),
			TurnedBlocks[Index].Rotation.Equals(
				FRotator(0.f, FlatBlocks[Index].Rotation.Yaw + 90.f, 0.f), 0.01f));
	}

	// And the doorway is still a doorway after the rotation: the east door at
	// local (+X) lands on the world +Y side once turned 90 degrees.
	const float WallX = Flat.SizeUU.X * 0.5f - Flat.WallThicknessUU * 0.5f;
	const FVector2D LocalDoorCentre(WallX, 200.f);
	const FVector Rotated = Rotation.RotateVector(FVector(LocalDoorCentre.X, LocalDoorCentre.Y, 0.f));
	TestFalse(TEXT("the doorway is still open after the building is turned"),
		SarkoMap::IsPointInsideBlocksXY(
			FVector2D(Flat.Location.X + Rotated.X, Flat.Location.Y + Rotated.Y), TurnedBlocks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingExpansionIsDeterministic,
	"Sarko.Map.BuildingExpansionIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingExpansionIsDeterministic::RunTest(const FString& Parameters)
{
	// The expander runs on every machine that loads the map, so identical input
	// must give byte-identical output in the same order — otherwise a wall is in
	// a different place on the server than on the client, which is a desync that
	// looks like a physics bug.
	FSarkoBuilding Building = MakeShed();
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::North, -500.f, 320.f));
	Building.Doors.Add(MakeDoor(ESarkoBuildingSide::South, 400.f, 300.f));
	FSarkoBuildingInteriorWall Divider;
	Divider.From = FVector2D(100.f, -720.f);
	Divider.To = FVector2D(100.f, 720.f);
	Divider.bHasDoor = true;
	Divider.Door = MakeDoor(ESarkoBuildingSide::North, -100.f, 300.f);
	Building.InteriorWalls.Add(Divider);

	TArray<FSarkoCoverBlock> First;
	TArray<FSarkoCoverBlock> Second;
	FString Error;
	TestTrue(TEXT("first expansion succeeds"), SarkoMap::ExpandBuilding(Building, First, Error));
	TestTrue(TEXT("second expansion succeeds"), SarkoMap::ExpandBuilding(Building, Second, Error));
	TestEqual(TEXT("the same building yields the same wall count"), Second.Num(), First.Num());
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("wall %d is identical"), Index),
			First[Index].Location.Equals(Second[Index].Location, 0.0001f) &&
			First[Index].Extent.Equals(Second[Index].Extent, 0.0001f));
		TestEqual(FString::Printf(TEXT("wall %d has the same id"), Index), First[Index].Id, Second[Index].Id);
	}

	// Ids are unique within a building, because Task 1 makes duplicate ids a
	// load error and a building that generates two identical wall ids would make
	// the whole map unloadable — from data that looks perfectly reasonable.
	TSet<FString> Ids;
	for (const FSarkoCoverBlock& Block : First)
	{
		TestFalse(FString::Printf(TEXT("wall id '%s' is unique"), *Block.Id), Ids.Contains(Block.Id));
		Ids.Add(Block.Id);
	}

	// ExpandBuildings resets rather than appends, and accumulates across
	// buildings in author order.
	FSarkoBuilding Second1 = MakeShed();
	Second1.Id = TEXT("test_shed_2");
	Second1.Location = FVector(8000.f, 0.f, 0.f);
	TArray<FSarkoCoverBlock> Many;
	Many.Add(FSarkoCoverBlock()); // must be discarded
	TestTrue(TEXT("expanding a list succeeds"),
		SarkoMap::ExpandBuildings({ Building, Second1 }, Many, Error));
	TestEqual(TEXT("the list expansion resets its output and sums the parts"),
		Many.Num(), First.Num() + 4);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `BUILD FAILED` with `'Map/SarkoBuildings.h' file not found`.

- [ ] **Step 3: Write the header**

Create `SarkoGame/Source/SarkoGame/Map/SarkoBuildings.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Map/SarkoMapBuilder.h"

#include "SarkoBuildings.generated.h"

/** Which face of a building's footprint a door is in. N is the +Y edge. */
UENUM()
enum class ESarkoBuildingSide : uint8
{
	North,
	East,
	South,
	West
};

/**
 * A doorway: not a door. There is no door mechanic (ТЗ §13) — this declares a
 * gap in a wall, and the gap is all there is.
 */
USTRUCT()
struct FSarkoBuildingDoor
{
	GENERATED_BODY()

	/** Which wall. Ignored for an interior wall's door, which has only one wall to be in. */
	UPROPERTY()
	ESarkoBuildingSide Side = ESarkoBuildingSide::North;

	/**
	 * Signed distance along the wall from that wall's MIDPOINT, in local uu:
	 * local X for a north or south wall, local Y for an east or west one.
	 * Zero is centred, which is what most buildings want.
	 */
	UPROPERTY()
	float OffsetUU = 0.f;

	/** The clear opening. Never below SarkoMap::MinDoorwayUU. */
	UPROPERTY()
	float WidthUU = 300.f;
};

/**
 * A wall inside a building, dividing it into rooms, with an optional doorway.
 *
 * Axis-aligned only: a diagonal interior wall is rejected. Nothing in the ТЗ
 * needs one, and supporting them would mean the passage-clearance rule (the one
 * that keeps a room reachable) stops being computable by comparing two numbers.
 */
USTRUCT()
struct FSarkoBuildingInteriorWall
{
	GENERATED_BODY()

	/** Endpoints in the building's local frame, before yaw. */
	UPROPERTY()
	FVector2D From = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D To = FVector2D::ZeroVector;

	/** False makes this a solid divider — a store room with no way in. */
	UPROPERTY()
	bool bHasDoor = false;

	/** Only Door.OffsetUU and Door.WidthUU are read; Side has no meaning here. */
	UPROPERTY()
	FSarkoBuildingDoor Door;
};

/**
 * One walkable building, declared once.
 *
 * This is the whole point of Stage B: ~20 buildings as ~20 of these instead of
 * ~400 hand-placed wall blocks. The expander below turns one of these into the
 * walls, and every invariant the ТЗ states about buildings (§13) is checked
 * there rather than trusted to the author.
 *
 * No roofs (the camera is above), no stairs, no second floor. The local frame:
 * origin at the centre of the footprint at floor level, SizeUU is the full
 * OUTER footprint, +Y is local north, +X local east, and Yaw turns the lot.
 */
USTRUCT()
struct FSarkoBuilding
{
	GENERATED_BODY()

	/** Required (ТЗ §18). Every emitted wall's id is derived from it. */
	UPROPERTY()
	FString Id;

	/** World position of the footprint's centre, at floor level. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Full outer footprint: X by Y, before yaw. */
	UPROPERTY()
	FVector2D SizeUU = FVector2D(2000.f, 1500.f);

	UPROPERTY()
	float Yaw = 0.f;

	/**
	 * Wall height. 350 uu by default, per spec §5.1: the pawn is ~176 uu, so a
	 * wall this tall cuts line of sight completely at ground level while the
	 * top-down camera still sees over it into the room.
	 */
	UPROPERTY()
	float WallHeightUU = 350.f;

	UPROPERTY()
	float WallThicknessUU = 30.f;

	/** Colour for every wall of this building (ТЗ §14). */
	UPROPERTY()
	ESarkoSurface Surface = ESarkoSurface::Structure;

	/**
	 * Empty means a CLOSED building — the ТЗ's four "закрытых" entries — and it
	 * is legal. One door is NOT: ТЗ §13 requires two exits from anything the
	 * player can enter, or a bot in the doorway is a death sentence.
	 */
	UPROPERTY()
	TArray<FSarkoBuildingDoor> Doors;

	UPROPERTY()
	TArray<FSarkoBuildingInteriorWall> InteriorWalls;
};

namespace SarkoMap
{
	/** ТЗ §13's floor for any opening the player walks through. */
	constexpr float MinDoorwayUU = 250.f;

	/** ТЗ §13 prefers 300-350; below this the expander logs a warning, not an error. */
	constexpr float PreferredDoorwayUU = 300.f;

	/** ТЗ §13's floor for the clear space between two parallel interior walls. */
	constexpr float MinInteriorPassageUU = 250.f;

	/** Absolute floor for a wall: it must break the line of sight of a ~176 uu pawn. */
	constexpr float MinWallHeightUU = 200.f;

	/**
	 * Turns one building declaration into wall blocks.
	 *
	 * Pure and deterministic: same input, same output, same order, on every
	 * machine — the map is expanded independently on server and client, so a
	 * wall in a different place on each is a desync that presents as a physics
	 * bug. RESETS OutBlocks; on failure leaves it empty and names the problem
	 * (with the building's id) in OutError.
	 *
	 * Emission order is fixed: north, east, south, west, then interior walls in
	 * author order, each side's segments running from its negative end.
	 */
	bool ExpandBuilding(const FSarkoBuilding& Building, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError);

	/** ExpandBuilding over a list, accumulating in author order. Resets OutBlocks. */
	bool ExpandBuildings(const TArray<FSarkoBuilding>& Buildings, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError);

	/**
	 * Whether a point in the horizontal plane is inside any block's footprint.
	 * Horizontal only: everything here is a full-height wall or a flat surface,
	 * and the question being asked is always "can the pawn be here".
	 */
	bool IsPointInsideBlocksXY(const FVector2D& Point, const TArray<FSarkoCoverBlock>& Blocks);

	/** "N"/"north", "E"/"east", "S"/"south", "W"/"west", case-insensitive. */
	bool ParseBuildingSide(const FString& Name, ESarkoBuildingSide& Out);
}
```

- [ ] **Step 4: Write the expander**

Create `SarkoGame/Source/SarkoGame/Map/SarkoBuildings.cpp`:

```cpp
#include "Map/SarkoBuildings.h"

namespace
{
	/** A doorway projected onto its wall's own axis: the interval to remove. */
	struct FGap
	{
		float Min = 0.f;
		float Max = 0.f;
	};

	/**
	 * Splits a wall's span into the segments left over once the gaps are cut out.
	 *
	 * The gaps arrive sorted and validated, so this is arithmetic and cannot
	 * fail; every rule about where a door may be lives in ValidateGaps below,
	 * where it can produce an error message naming the door.
	 */
	void SegmentsBetweenGaps(float SpanMin, float SpanMax, const TArray<FGap>& Gaps, TArray<FGap>& OutSegments)
	{
		float Cursor = SpanMin;
		for (const FGap& Gap : Gaps)
		{
			if (Gap.Min - Cursor > KINDA_SMALL_NUMBER)
			{
				OutSegments.Add({ Cursor, Gap.Min });
			}
			Cursor = Gap.Max;
		}
		if (SpanMax - Cursor > KINDA_SMALL_NUMBER)
		{
			OutSegments.Add({ Cursor, SpanMax });
		}
	}

	/**
	 * Every rule about doorways on one wall, in one place.
	 *
	 * `Thickness` is the minimum wall that must survive at each end and between
	 * two doors: a stub shorter than the wall is thick reads as a floating fleck
	 * of geometry, and a zero-length one would emit a degenerate block that the
	 * map parser rejects (extent components must be positive) — turning an
	 * authoring slip into "the whole map failed to load".
	 */
	bool ValidateGaps(const FString& BuildingId, const TCHAR* WallName, float SpanMin, float SpanMax,
		float Thickness, TArray<FGap>& Gaps, FString& OutError)
	{
		Gaps.Sort([](const FGap& A, const FGap& B) { return A.Min < B.Min; });

		for (int32 Index = 0; Index < Gaps.Num(); ++Index)
		{
			const FGap& Gap = Gaps[Index];
			if (Gap.Min < SpanMin + Thickness - KINDA_SMALL_NUMBER ||
				Gap.Max > SpanMax - Thickness + KINDA_SMALL_NUMBER)
			{
				OutError = FString::Printf(
					TEXT("building '%s': a doorway on the %s wall runs to %.1f..%.1f, which leaves less than %.1f uu of wall at an end (the wall spans %.1f..%.1f)"),
					*BuildingId, WallName, Gap.Min, Gap.Max, Thickness, SpanMin, SpanMax);
				return false;
			}
			if (Index > 0)
			{
				const float Between = Gap.Min - Gaps[Index - 1].Max;
				if (Between < Thickness - KINDA_SMALL_NUMBER)
				{
					OutError = FString::Printf(
						TEXT("building '%s': two doorways on the %s wall are %.1f uu apart; they must leave at least %.1f uu of wall between them (%.1f uu means they overlap)"),
						*BuildingId, WallName, Between, Thickness, Between);
					return false;
				}
			}
		}
		return true;
	}

	/** One wall segment, as a block, in the building's local frame. */
	FSarkoCoverBlock MakeWall(const FSarkoBuilding& Building, const FString& Id,
		const FVector2D& LocalCentre, const FVector2D& LocalHalfExtent)
	{
		FSarkoCoverBlock Block;
		Block.Id = Id;
		Block.Surface = Building.Surface;
		Block.bBlocksMovement = true;
		Block.Extent = FVector(LocalHalfExtent.X, LocalHalfExtent.Y, Building.WallHeightUU * 0.5f);

		// Local -> world: rotate the offset by the building's yaw, then translate.
		// Rotating the offset AND the block is both halves of the job; doing only
		// the second shears the shell open at every angle except zero.
		const FRotator Rotation(0.f, Building.Yaw, 0.f);
		const FVector Rotated = Rotation.RotateVector(FVector(LocalCentre.X, LocalCentre.Y, 0.f));
		Block.Location = Building.Location + Rotated + FVector(0.f, 0.f, Building.WallHeightUU * 0.5f);
		Block.Rotation = Rotation;
		return Block;
	}
}

bool SarkoMap::ParseBuildingSide(const FString& Name, ESarkoBuildingSide& Out)
{
	if (Name.Equals(TEXT("N"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("north"), ESearchCase::IgnoreCase)) { Out = ESarkoBuildingSide::North; return true; }
	if (Name.Equals(TEXT("E"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("east"), ESearchCase::IgnoreCase))  { Out = ESarkoBuildingSide::East;  return true; }
	if (Name.Equals(TEXT("S"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("south"), ESearchCase::IgnoreCase)) { Out = ESarkoBuildingSide::South; return true; }
	if (Name.Equals(TEXT("W"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("west"), ESearchCase::IgnoreCase))  { Out = ESarkoBuildingSide::West;  return true; }
	return false;
}

bool SarkoMap::IsPointInsideBlocksXY(const FVector2D& Point, const TArray<FSarkoCoverBlock>& Blocks)
{
	for (const FSarkoCoverBlock& Block : Blocks)
	{
		const FVector Delta(Point.X - Block.Location.X, Point.Y - Block.Location.Y, 0.f);
		const FVector Local = Block.Rotation.UnrotateVector(Delta);
		if (FMath::Abs(Local.X) <= Block.Extent.X && FMath::Abs(Local.Y) <= Block.Extent.Y)
		{
			return true;
		}
	}
	return false;
}

bool SarkoMap::ExpandBuilding(const FSarkoBuilding& Building, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)
{
	OutBlocks.Reset();
	OutError.Reset();

	const FString& Id = Building.Id;
	if (Id.IsEmpty())
	{
		OutError = TEXT("a building has no 'id'; every building must be named (ТЗ §18)");
		return false;
	}

	const float T = Building.WallThicknessUU;
	const float H = Building.WallHeightUU;
	const float HalfX = Building.SizeUU.X * 0.5f;
	const float HalfY = Building.SizeUU.Y * 0.5f;

	if (T < 10.f || T > 200.f)
	{
		OutError = FString::Printf(TEXT("building '%s': wallThickness %.1f is outside 10..200 uu"), *Id, T);
		return false;
	}
	if (H < MinWallHeightUU || H > 800.f)
	{
		OutError = FString::Printf(
			TEXT("building '%s': wallHeight %.1f is outside %.0f..800 uu — below %.0f it does not break the line of sight of a 176 uu pawn"),
			*Id, H, MinWallHeightUU, MinWallHeightUU);
		return false;
	}
	// A room the pawn cannot stand and turn in is not a room. 500 uu of clear
	// interior on each axis is the floor: a 250 uu passage plus something to
	// pass to.
	if (Building.SizeUU.X - 2.f * T < 500.f || Building.SizeUU.Y - 2.f * T < 500.f)
	{
		OutError = FString::Printf(
			TEXT("building '%s': footprint %.0fx%.0f with %.0f uu walls leaves %.0fx%.0f of interior; each axis needs at least 500 uu"),
			*Id, Building.SizeUU.X, Building.SizeUU.Y, T,
			Building.SizeUU.X - 2.f * T, Building.SizeUU.Y - 2.f * T);
		return false;
	}
	if (T * 4.f > FMath::Min(Building.SizeUU.X, Building.SizeUU.Y))
	{
		OutError = FString::Printf(TEXT("building '%s': walls %.0f uu thick are too heavy for a %.0fx%.0f footprint"),
			*Id, T, Building.SizeUU.X, Building.SizeUU.Y);
		return false;
	}
	// ТЗ §13: "у проходимого здания два выхода". Zero is a closed building and
	// legal; one is a room a single bot in the doorway turns into a coffin.
	if (Building.Doors.Num() == 1)
	{
		OutError = FString::Printf(
			TEXT("building '%s': one doorway. A building the player can enter needs two exits (ТЗ §13); a closed building has none"),
			*Id);
		return false;
	}
	for (const FSarkoBuildingDoor& Door : Building.Doors)
	{
		if (Door.WidthUU < MinDoorwayUU)
		{
			OutError = FString::Printf(TEXT("building '%s': a doorway is %.1f uu wide; the minimum is %.0f (ТЗ §13)"),
				*Id, Door.WidthUU, MinDoorwayUU);
			return false;
		}
		if (Door.WidthUU < PreferredDoorwayUU)
		{
			// A warning, not an error: 250 is legal and 300-350 is preferred, and
			// the difference is comfort rather than correctness.
			UE_LOG(LogTemp, Warning, TEXT("SarkoMap: building '%s' has a %.0f uu doorway; ТЗ §13 prefers %.0f-350"),
				*Id, Door.WidthUU, PreferredDoorwayUU);
		}
	}

	// ---- Perimeter. N and S span the full width; E and W are shortened by one
	// thickness at each end so they butt into them, which is what makes "no wall
	// overlaps another wall" exactly true rather than nearly true.
	struct FSideSpec
	{
		ESarkoBuildingSide Side;
		const TCHAR* Name;
		bool bAlongX;
		float Across;   // the fixed coordinate of the wall's centre line
		float SpanMin;
		float SpanMax;
	};
	const FSideSpec Sides[4] = {
		{ ESarkoBuildingSide::North, TEXT("north"), true,   HalfY - T * 0.5f, -HalfX,       HalfX },
		{ ESarkoBuildingSide::East,  TEXT("east"),  false,  HalfX - T * 0.5f, -HalfY + T,   HalfY - T },
		{ ESarkoBuildingSide::South, TEXT("south"), true,  -HalfY + T * 0.5f, -HalfX,       HalfX },
		{ ESarkoBuildingSide::West,  TEXT("west"),  false, -HalfX + T * 0.5f, -HalfY + T,   HalfY - T },
	};

	for (const FSideSpec& Side : Sides)
	{
		TArray<FGap> Gaps;
		for (const FSarkoBuildingDoor& Door : Building.Doors)
		{
			if (Door.Side == Side.Side)
			{
				// Offset is measured from the wall's midpoint, and every span
				// above is symmetric about zero, so the midpoint IS zero.
				Gaps.Add({ Door.OffsetUU - Door.WidthUU * 0.5f, Door.OffsetUU + Door.WidthUU * 0.5f });
			}
		}
		if (!ValidateGaps(Id, Side.Name, Side.SpanMin, Side.SpanMax, T, Gaps, OutError))
		{
			OutBlocks.Reset();
			return false;
		}

		TArray<FGap> Segments;
		SegmentsBetweenGaps(Side.SpanMin, Side.SpanMax, Gaps, Segments);
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const float Centre = (Segments[Index].Min + Segments[Index].Max) * 0.5f;
			const float Half = (Segments[Index].Max - Segments[Index].Min) * 0.5f;
			const FString WallId = FString::Printf(TEXT("%s_%s_%d"), *Id, Side.Name, Index);
			OutBlocks.Add(Side.bAlongX
				? MakeWall(Building, WallId, FVector2D(Centre, Side.Across), FVector2D(Half, T * 0.5f))
				: MakeWall(Building, WallId, FVector2D(Side.Across, Centre), FVector2D(T * 0.5f, Half)));
		}
	}

	// ---- Interior walls.
	const float InnerX = HalfX - T;
	const float InnerY = HalfY - T;

	for (int32 WallIndex = 0; WallIndex < Building.InteriorWalls.Num(); ++WallIndex)
	{
		const FSarkoBuildingInteriorWall& Wall = Building.InteriorWalls[WallIndex];
		const bool bAlongY = FMath::IsNearlyEqual(Wall.From.X, Wall.To.X, 1.f);
		const bool bAlongX = FMath::IsNearlyEqual(Wall.From.Y, Wall.To.Y, 1.f);
		if (bAlongY == bAlongX)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] runs from (%.0f,%.0f) to (%.0f,%.0f), which is %s; interior walls must be axis-aligned"),
				*Id, WallIndex, Wall.From.X, Wall.From.Y, Wall.To.X, Wall.To.Y,
				bAlongX ? TEXT("zero-length") : TEXT("diagonal"));
			OutBlocks.Reset();
			return false;
		}

		const float Across = bAlongY ? Wall.From.X : Wall.From.Y;
		const float SpanMin = bAlongY ? FMath::Min(Wall.From.Y, Wall.To.Y) : FMath::Min(Wall.From.X, Wall.To.X);
		const float SpanMax = bAlongY ? FMath::Max(Wall.From.Y, Wall.To.Y) : FMath::Max(Wall.From.X, Wall.To.X);
		const float AcrossLimit = bAlongY ? InnerX : InnerY;
		const float SpanLimit = bAlongY ? InnerY : InnerX;

		if (FMath::Abs(Across) > AcrossLimit + 1.f ||
			SpanMin < -SpanLimit - 1.f || SpanMax > SpanLimit + 1.f)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] reaches outside the footprint (interior is %.0f..%.0f by %.0f..%.0f)"),
				*Id, WallIndex, -InnerX, InnerX, -InnerY, InnerY);
			OutBlocks.Reset();
			return false;
		}
		if (SpanMax - SpanMin < T)
		{
			OutError = FString::Printf(TEXT("building '%s': interiorWalls[%d] is %.1f uu long, shorter than it is thick"),
				*Id, WallIndex, SpanMax - SpanMin);
			OutBlocks.Reset();
			return false;
		}

		// ТЗ §13's "внутренний проход ≥250": the clear space between this wall
		// and anything parallel it could form a corridor with. Checked against
		// the two parallel perimeter faces and against every earlier parallel
		// interior wall whose span overlaps this one — a wall that does not
		// overlap does not form a corridor and is not compared.
		const float ClearToPerimeter = (AcrossLimit - FMath::Abs(Across)) - T * 0.5f;
		if (ClearToPerimeter < MinInteriorPassageUU)
		{
			OutError = FString::Printf(
				TEXT("building '%s': interiorWalls[%d] leaves %.1f uu between it and the outer wall; the minimum passage is %.0f (ТЗ §13)"),
				*Id, WallIndex, ClearToPerimeter, MinInteriorPassageUU);
			OutBlocks.Reset();
			return false;
		}
		for (int32 Other = 0; Other < WallIndex; ++Other)
		{
			const FSarkoBuildingInteriorWall& Prior = Building.InteriorWalls[Other];
			const bool bPriorAlongY = FMath::IsNearlyEqual(Prior.From.X, Prior.To.X, 1.f);
			if (bPriorAlongY != bAlongY)
			{
				continue; // perpendicular walls make a corner, not a corridor
			}
			const float PriorAcross = bAlongY ? Prior.From.X : Prior.From.Y;
			const float PriorMin = bAlongY ? FMath::Min(Prior.From.Y, Prior.To.Y) : FMath::Min(Prior.From.X, Prior.To.X);
			const float PriorMax = bAlongY ? FMath::Max(Prior.From.Y, Prior.To.Y) : FMath::Max(Prior.From.X, Prior.To.X);
			if (PriorMax <= SpanMin || PriorMin >= SpanMax)
			{
				continue; // they never face each other
			}
			const float Clear = FMath::Abs(Across - PriorAcross) - T;
			if (Clear < MinInteriorPassageUU)
			{
				OutError = FString::Printf(
					TEXT("building '%s': interiorWalls[%d] and interiorWalls[%d] leave a %.1f uu corridor; the minimum passage is %.0f (ТЗ §13)"),
					*Id, WallIndex, Other, Clear, MinInteriorPassageUU);
				OutBlocks.Reset();
				return false;
			}
		}

		TArray<FGap> Gaps;
		if (Wall.bHasDoor)
		{
			if (Wall.Door.WidthUU < MinInteriorPassageUU)
			{
				OutError = FString::Printf(
					TEXT("building '%s': interiorWalls[%d] has a %.1f uu doorway; the minimum interior passage is %.0f (ТЗ §13)"),
					*Id, WallIndex, Wall.Door.WidthUU, MinInteriorPassageUU);
				OutBlocks.Reset();
				return false;
			}
			// Interior offsets are measured from the wall's own midpoint, which
			// (unlike a perimeter side) is not necessarily zero.
			const float Mid = (SpanMin + SpanMax) * 0.5f;
			Gaps.Add({ Mid + Wall.Door.OffsetUU - Wall.Door.WidthUU * 0.5f,
			           Mid + Wall.Door.OffsetUU + Wall.Door.WidthUU * 0.5f });
		}
		const FString WallName = FString::Printf(TEXT("interior %d"), WallIndex);
		if (!ValidateGaps(Id, *WallName, SpanMin, SpanMax, T, Gaps, OutError))
		{
			OutBlocks.Reset();
			return false;
		}

		TArray<FGap> Segments;
		SegmentsBetweenGaps(SpanMin, SpanMax, Gaps, Segments);
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const float Centre = (Segments[Index].Min + Segments[Index].Max) * 0.5f;
			const float Half = (Segments[Index].Max - Segments[Index].Min) * 0.5f;
			const FString SegmentId = FString::Printf(TEXT("%s_interior%d_%d"), *Id, WallIndex, Index);
			OutBlocks.Add(bAlongY
				? MakeWall(Building, SegmentId, FVector2D(Across, Centre), FVector2D(T * 0.5f, Half))
				: MakeWall(Building, SegmentId, FVector2D(Centre, Across), FVector2D(Half, T * 0.5f)));
		}
	}

	return true;
}

bool SarkoMap::ExpandBuildings(const TArray<FSarkoBuilding>& Buildings, TArray<FSarkoCoverBlock>& OutBlocks, FString& OutError)
{
	OutBlocks.Reset();
	OutError.Reset();

	TArray<FSarkoCoverBlock> One;
	for (const FSarkoBuilding& Building : Buildings)
	{
		if (!ExpandBuilding(Building, One, OutError))
		{
			OutBlocks.Reset();
			return false;
		}
		OutBlocks.Append(One);
	}
	return true;
}
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `ALL GREEN`, with all six new results `Success`: `ClosedBuildingIsSealed`, `DoorwaysAreRealGaps`, `InteriorWallsDivideAndPass`, `BuildingExpansionRejectsBadGeometry`, `BuildingYawRotatesTheWholeShell`, `BuildingExpansionIsDeterministic`.

If a segment-count assertion fails by one, the cause is almost always a door whose gap coincides with a span end, leaving a zero-length stub that `SegmentsBetweenGaps` correctly drops while the test expected it. Fix the test's expectation only after confirming with the sampling assertions that the wall is actually closed — the sampling is the ground truth, the counts are the cross-check.

- [ ] **Step 6: Run the whole suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN`, six more tests than after Task 4.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoBuildings.h SarkoGame/Source/SarkoGame/Map/SarkoBuildings.cpp SarkoGame/Source/SarkoGame/Tests/BuildingTest.cpp && git commit -m "feat(map): pure building expander — one declaration becomes walls, doorways really are gaps"
```

---

### Task 6: Debt — shadows fall to near-black (ambient sky light + a shadow lift)

Sixth, before anything else is looked at: from here on this plan judges its own work by screenshot, and it should not judge readability under lighting it already knows is wrong.

**The debt, precisely.** `SpawnLighting` creates exactly one directional light and nothing else, and the comment in `SarkoMapBuilder.cpp` explains why there is no fill: mobile forward shading supports one directional light, and a `SkyLight` set to `SLS_CapturedScene` would capture a level with no sky and light the scene with the black it found. Both halves of that are true. The consequence is that every surface facing away from the sun receives zero diffuse light, and every cast shadow is the same, so the sides of walls and the ground beside them go to near-black — which on a top-down camera is most of what a building looks like.

**The fix, and why this one.** A sky light with `SLS_SpecifiedCubemap` pointing at `/Engine/MapTemplates/Sky/DaylightAmbientCubemap` — the cubemap the engine's own map templates use. This is an **engine asset referenced by path**, which the constraints allow and which the project already does for every mesh and material. It is captured **once** at spawn (`RecaptureSky()`) into spherical harmonics; there is no per-frame capture and no `SkyAtmosphere`.

Verified in engine source before choosing it:
- `USkyLightComponent::Cubemap`, `SourceType`, `CubemapResolution`, `bLowerHemisphereIsBlack`, `LowerHemisphereColor` are public `UPROPERTY`s; `SetCubemap`, `SetIntensity`, `SetLightColor`, `RecaptureSky` are `ENGINE_API` members (`Runtime/Engine/Classes/Components/SkyLightComponent.h`).
- `/Users/Shared/Epic Games/UE_5.8/Engine/Content/MapTemplates/Sky/DaylightAmbientCubemap.uasset` exists and is a `TextureCube`.
- A sky light with `SLS_SpecifiedCubemap` and no cubemap is treated as **invalid** (`SkyLightComponent.cpp:436`), so the path must resolve or there is no ambient at all — hence the existence test in Step 1.

**Mobile cost, stated plainly:** one processed cubemap at `CubemapResolution = 32` (6 faces × 32² × RGBA16F ≈ 50 KB with mips) plus three SH coefficient vectors in the mobile base pass, which is arithmetic on constants and effectively free per pixel; rough materials take at most one low-mip cubemap sample for specular. **No per-frame cost**, because the capture happens once. The alternative — `ASkyAtmosphere` plus `bRealTimeCapture` — was rejected: it adds sky LUT passes and a cubemap re-render every frame on a phone, to produce the same SH ambient this gets for one capture.

The **second, free** half: `UDirectionalLightComponent::SetShadowAmount(0.6f)`. "Control the amount of shadow occlusion. A value of 0 means no occlusion" — a scalar in the light's shader parameters, zero runtime cost, and it lifts cast shadows out of black without touching the shadow map itself. The sky light fixes unlit *faces*; `ShadowAmount` fixes *cast shadows*. They are different problems and both are in the debt.

**Cooking, flagged not assumed:** the cubemap is loaded by literal path, so nothing hard-references it and the cooker has no reason to include it — the exact class of failure that once left the Mannequins invisible in a packaged build and visible in the editor. Step 5 adds a `DirectoriesToAlwaysCook` entry for the engine directory, mirroring the existing `/Game/Mannequins` line. **This cannot be verified without a device build**, which is out of scope here: the task report must flag it as unverified-on-device.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h` (a `Lighting` constants namespace)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp` (`SpawnLighting`)
- Modify: `SarkoGame/Config/DefaultEngine.ini` (cook the engine sky directory)
- Create: `SarkoGame/Scripts/eye-shot.sh`
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp` (+2 tests)

**Interfaces:**
- Consumes: `ADirectionalLight`, `ASkyLight`, `USkyLightComponent`, `UDirectionalLightComponent`, `UTextureCube`, `FPackageName`.
- Produces: `namespace SarkoMap::Lighting { SunIntensityLux, SunRotation, ShadowAmount, AmbientIntensity, AmbientCubemapPath, AmbientColour, GroundBounceColour, AmbientCubemapResolution }` in `SarkoMapBuilder.h`.

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoLightingHasAnAmbientTerm,
	"Sarko.Config.LightingHasAnAmbientTerm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Automation runs under -nullrhi and can see nothing, so this cannot assert that
 * the frame looks right — Step 7's screenshot does that. What it CAN pin is
 * every way the ambient silently does not exist: a cubemap path that does not
 * resolve (a sky light with SLS_SpecifiedCubemap and no cubemap is invalid and
 * contributes nothing at all), an intensity of zero, a shadow lift that lifts
 * nothing, or an ambient bright enough to flatten the sun out of the frame.
 */
bool FSarkoLightingHasAnAmbientTerm::RunTest(const FString& Parameters)
{
	using namespace SarkoMap::Lighting;

	// The one failure mode that produces no log, no warning and no ambient: a
	// typo'd or moved engine asset path. Checked as a package rather than a
	// LoadObject so it is safe with no RHI.
	const FString Package = FSoftObjectPath(AmbientCubemapPath).GetLongPackageName();
	TestFalse(TEXT("the ambient cubemap path names a package"), Package.IsEmpty());
	TestTrue(FString::Printf(TEXT("the ambient cubemap package '%s' exists"), *Package),
		FPackageName::DoesPackageExist(Package));
	TestTrue(TEXT("the ambient cubemap is an engine asset, not one we authored"),
		Package.StartsWith(TEXT("/Engine/")));

	TestTrue(TEXT("the ambient actually contributes"), AmbientIntensity > 0.f);
	TestTrue(TEXT("the ambient does not flatten the sun out of the frame"), AmbientIntensity < SunIntensityLux);
	TestTrue(TEXT("the ambient cubemap is small enough for a phone"),
		AmbientCubemapResolution > 0 && AmbientCubemapResolution <= 64);

	// Cool sky against a warm sun: the shadowed side of a wall should read as a
	// different colour temperature, not merely a darker grey.
	TestTrue(TEXT("the sky fill is cool"), AmbientColour.B > AmbientColour.R);
	// The ground bounce exists but is dim — bLowerHemisphereIsBlack false with a
	// bright lower colour lights the undersides of everything and looks like a
	// missing shadow.
	const auto Lum = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
	TestTrue(TEXT("the ground bounce is present"), Lum(GroundBounceColour) > 0.f);
	TestTrue(TEXT("the ground bounce is dimmer than the sky"),
		Lum(GroundBounceColour) < Lum(AmbientColour) * 0.5f);

	// A shadow lift of 1.0 is what the engine already does, and 0.0 removes
	// shadows entirely — which would undo the reason virtual shadow maps were
	// deliberately re-enabled in DefaultEngine.ini.
	TestTrue(TEXT("shadows are lifted but not removed"), ShadowAmount > 0.2f && ShadowAmount < 1.f);

	// The sun still comes from above, or a top-down camera sees mostly shadow.
	TestTrue(TEXT("the sun is steep, not horizontal"), SunRotation.Pitch < -30.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEngineMeshPathsResolve,
	"Sarko.Config.EngineMeshPathsResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEngineMeshPathsResolve::RunTest(const FString& Parameters)
{
	// Every engine asset this project reaches by literal string, in one place.
	// A moved or renamed engine asset produces a log line at runtime and nothing
	// else — geometry simply does not appear, or keeps the grid material.
	const TArray<FString> Paths = {
		TEXT("/Engine/BasicShapes/Cube.Cube"),
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
		TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"),
	};
	for (const FString& Path : Paths)
	{
		const FString Package = FSoftObjectPath(Path).GetLongPackageName();
		TestTrue(FString::Printf(TEXT("'%s' exists"), *Path), FPackageName::DoesPackageExist(Package));
	}
	return true;
}
```

Add `#include "Misc/PackageName.h"` and `#include "UObject/SoftObjectPath.h"` at the top of `MapBuilderTest.cpp`.

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Config`
Expected: `BUILD FAILED` with `no member named 'Lighting' in namespace 'SarkoMap'`.

- [ ] **Step 3: Publish the lighting constants**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h`, add to `namespace SarkoMap`, beside the palette pointer comment:

```cpp
	/**
	 * The sector's lighting, in the header so the numbers the readability
	 * argument rests on can be asserted rather than trusted.
	 *
	 * Mobile forward shading supports exactly ONE directional light — a second
	 * makes the engine warn on screen that lights are "competing to be the
	 * single one used for forward shading" and then pick one by brightness. The
	 * ambient here is a sky light, which is spherical-harmonic irradiance and
	 * not a second directional light, so it does not touch that path.
	 */
	namespace Lighting
	{
		/** Steep rather than horizontal, so a top-down camera sees lit surfaces. */
		const FRotator SunRotation(-55.f, 30.f, 0.f);

		/** Bright enough to read grey boxes on a phone screen in daylight. */
		constexpr float SunIntensityLux = 6.f;

		/**
		 * How much of the sun's shadow is actually occluded. 1.0 is the engine
		 * default and produced near-black stripes beside every wall; 0.0 removes
		 * shadows entirely, which would undo the reason virtual shadow maps are
		 * deliberately enabled in DefaultEngine.ini. This is a scalar in the
		 * light's shader parameters: it costs nothing at all.
		 */
		constexpr float ShadowAmount = 0.6f;

		/**
		 * The engine's own map-template ambient cubemap, referenced by path —
		 * this project authors no assets. A sky light with SLS_SpecifiedCubemap
		 * and no cubemap is treated as INVALID by the engine and contributes
		 * nothing, so a broken path here is a silent loss of all ambient;
		 * Sarko.Config.LightingHasAnAmbientTerm pins that it resolves.
		 */
		const TCHAR* const AmbientCubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");

		/** Captured once at spawn. 32 keeps the processed cubemap around 50 KB. */
		constexpr int32 AmbientCubemapResolution = 32;

		/** Enough to lift unlit faces off black, far short of flattening the sun. */
		constexpr float AmbientIntensity = 1.0f;

		/** Cool, against the sun's warm — a shadowed wall reads blue-grey, not black. */
		const FLinearColor AmbientColour(0.55f, 0.62f, 0.78f);

		/**
		 * The lower hemisphere. bLowerHemisphereIsBlack is turned off so the
		 * undersides of things are not pure black, but this stays dim: a bright
		 * ground bounce lights everything from below and reads as a missing
		 * shadow.
		 */
		const FLinearColor GroundBounceColour(0.050f, 0.045f, 0.030f);
	}
```

- [ ] **Step 4: Spawn the ambient**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp`, add the includes:

```cpp
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
```

(`Components/DirectionalLightComponent.h` is already there.) Delete the file-local `SunRotation` and `SunIntensityLux` constants — they now live in the header as `Lighting::` — and replace `SpawnLighting` with:

```cpp
	/**
	 * The sun, plus an ambient term.
	 *
	 * Exactly one directional light. Not two: the mobile forward shading path
	 * supports a single directional light, and a second one makes the engine warn
	 * on screen that lights are "competing to be the single one used for forward
	 * shading" and then pick one by brightness. A fill light from the opposite
	 * side is the obvious way to stop cover's shadowed faces going black, and it
	 * is exactly what this renderer cannot have.
	 *
	 * The ambient is therefore a SkyLight, which contributes spherical-harmonic
	 * irradiance rather than a second shaded light. Its source is the engine's
	 * own map-template cubemap referenced by path — NOT SLS_CapturedScene, which
	 * would capture this level (no sky, no atmosphere, nothing beyond the floor)
	 * and light the scene with the black it found. That was the original reason
	 * for having no ambient at all, and a shipped cubemap is the way around it
	 * without authoring an asset.
	 *
	 * Movable mobility matters for both: these are spawned after BeginPlay, so
	 * there is no baked lighting for a Static light to have contributed to, and
	 * a Static light created at runtime lights nothing at all.
	 */
	void SpawnLighting(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(
			FVector(0.f, 0.f, 5000.f), SarkoMap::Lighting::SunRotation, Params);
		if (!Sun)
		{
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the sun; the raid will render black"));
			return;
		}

		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunComponent->SetIntensity(SarkoMap::Lighting::SunIntensityLux);
			SunComponent->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f));
			SunComponent->SetCastShadows(true);
			// Lifts cast shadows off black without removing them. A shader
			// constant, so this line is free.
			SunComponent->SetShadowAmount(SarkoMap::Lighting::ShadowAmount);
		}

		ASkyLight* Sky = World.SpawnActor<ASkyLight>(FVector(0.f, 0.f, 5000.f), FRotator::ZeroRotator, Params);
		if (!Sky)
		{
			// Not fatal: the sun still lights the scene, the shadowed sides just
			// go dark again.
			UE_LOG(LogTemp, Error, TEXT("SarkoMap: failed to spawn the sky light; shadowed faces will read as black"));
			return;
		}
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, SarkoMap::Lighting::AmbientCubemapPath);
			if (!Cubemap)
			{
				// A sky light with SLS_SpecifiedCubemap and a null cubemap is
				// invalid and contributes nothing, so say so loudly rather than
				// leaving a light in the scene that does not light.
				UE_LOG(LogTemp, Error, TEXT("SarkoMap: ambient cubemap '%s' failed to load; there will be no ambient light"),
					SarkoMap::Lighting::AmbientCubemapPath);
				return;
			}
			SkyComponent->SetMobility(EComponentMobility::Movable);
			SkyComponent->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
			SkyComponent->CubemapResolution = SarkoMap::Lighting::AmbientCubemapResolution;
			SkyComponent->bLowerHemisphereIsBlack = false;
			SkyComponent->LowerHemisphereColor = SarkoMap::Lighting::GroundBounceColour;
			SkyComponent->SetCubemap(Cubemap);
			SkyComponent->SetIntensity(SarkoMap::Lighting::AmbientIntensity);
			SkyComponent->SetLightColor(SarkoMap::Lighting::AmbientColour);
			// Once. There is no time of day and nothing in the sky moves, so a
			// real-time capture would re-render a cubemap every frame on a phone
			// to produce the same numbers.
			SkyComponent->RecaptureSky();
		}
	}
```

- [ ] **Step 5: Cook the cubemap**

In `SarkoGame/Config/DefaultEngine.ini`, under the existing `[/Script/UnrealEd.ProjectPackagingSettings]` section, below the `/Game/Mannequins` line:

```ini
; The ambient cubemap is reached only by LoadObject with a literal path, so
; nothing in the project hard-references it and the cooker has no reason to
; include it — the same failure as the Mannequins line above: ambient light in
; the editor, none in a packaged iOS build, and nothing in any log to say so.
+DirectoriesToAlwaysCook=(Path="/Engine/MapTemplates/Sky")
```

**Flag in the task report:** this line cannot be verified without a packaged device build, which is out of scope for Stage B. If a later iOS build shows black shadowed faces on device while the editor looks right, this is the first suspect.

- [ ] **Step 6: Add the player-eye screenshot script**

Create `SarkoGame/Scripts/eye-shot.sh` and `chmod +x` it:

```bash
#!/usr/bin/env bash
#
# Launches the game with a real renderer but no window, teleports the player to
# a given point, and screenshots the frame from the player's own camera.
#
# The overview shot answers "is the layout right". This answers "is it readable
# from where the player actually is" — which is the only place lighting, wall
# height and doorway width can be judged.
#
# Usage:
#   Scripts/eye-shot.sh -13500 -9000 200
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${EYE_TIMEOUT:-240}"

X="${1:?usage: eye-shot.sh X Y Z}"
Y="${2:?usage: eye-shot.sh X Y Z}"
Z="${3:-200}"

rm -rf "$SHOT_DIR"

# Three things here are load-bearing:
#  * ?game=... — GlobalDefaultGameMode is the shelter, so a bare
#    /Engine/Maps/Entry boots a menu with no map to photograph.
#  * EnableCheats — a cheat manager only exists for a local controller in a
#    non-shipping build, and BugItGo is UCheatManager's own teleport.
#  * Walk after BugItGo — BugItGo calls Ghost(), which turns OFF capsule
#    collision for the rest of the run. Without Walk the pawn floats through
#    every wall this plan just built and the frame proves nothing about them.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=900 \
	-ExecCmds="t.MaxFPS 10, EnableCheats, BugItGo $X $Y $Z, Walk, HighResShot 1600x900" > /dev/null 2>&1 &
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

`Scripts/overview-shot.sh` already passes the `?game=` option for the same reason; keep the two scripts' launch URLs in step.

- [ ] **Step 7: Verify visually — this is the actual test**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh` — expected `ALL GREEN`, two more tests than after Task 5.

Then:

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -600 -3200 200`

**Read both PNGs.** What must be true:
- Shadows are visibly *dark grey-blue*, not black. Compare against Task 3's overview: the stripes beside the ravine walls were near-black there.
- The sides of walls facing away from the sun are distinguishable from the ground, rather than merging into one dark mass.
- The frame has not become flat: the sun's direction is still readable from which faces are brightest. If everything is evenly lit, `AmbientIntensity` is too high — halve it and re-shoot.
- The ground has not turned blue. If it has, `AmbientColour` is too saturated.

Record both PNG paths in the task report and say in words what changed. If the sky light produced no visible difference at all, the likely cause is the mobile renderer path (`r.MobileHDR=False`) dropping sky-light contribution in the editor preview; note it in the report and keep `ShadowAmount`, which works regardless — do **not** start adding `SkyAtmosphere`, which this task rejected on cost.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.cpp SarkoGame/Config/DefaultEngine.ini SarkoGame/Scripts/eye-shot.sh SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp && git commit -m "fix(map): ambient sky light and a shadow lift, so shadowed faces stop reading as black"
```

---

### Task 7: `buildings[]` in the map file, wired to the world, and three real buildings

Seventh: the expander from Task 5 gets a JSON section, reaches `SpawnLayout` through `ToLayout`, and is proven in the shipped map.

**The smallest honest proof.** `bridge.json` places 19 solid `house` props today — each a 10×8×6 m box the player bounces off. Three of them, at ТЗ ledger coordinates that survive into the full map, become walkable buildings: **S01 the gas station** (-13500, -9000, 2200×1500, "зал + подсобка + служебная", entrances E and S), **S04 house D1** (-6500, -10500, 2000×1500), **S05 house D2** (-1500, -10500, 2200×1600). The other 16 stay solid — Stage C authors the rest of the ledger. This is technology demonstration inside real content, not content authoring: three buildings is what it takes to prove a perimeter, two entrances, an interior division and a per-building surface all work in the actual game.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, `.cpp` (the `buildings[]` section)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapBuilder.h` (nothing new — `ToLayout` already feeds `Layout.Cover`)
- Modify: `SarkoGame/Data/Maps/bridge.json`
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (+2 tests)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test, and `IsInsideBlock` is replaced by the shared helper)

**Interfaces:**
- Consumes: `SarkoMap::ExpandBuilding`, `ExpandBuildings`, `ParseBuildingSide`, `IsPointInsideBlocksXY`, `ParseSurfaceName`, `RequireIdentifiedEntries`.
- Produces:
  - `FSarkoMapDefinition::Buildings` (`TArray<FSarkoBuilding>`)
  - The JSON keys `buildings[]`, each `{ id, pos, size, yaw?, wallHeight?, wallThickness?, surface?, doors[]?, interiorWalls[]? }`
  - `SarkoMap::ToLayout` appends every building's expanded walls to `Layout.Cover`, after the authored blocks.

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`. Add `#include "Map/SarkoBuildings.h"` beside the existing map includes.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingsParseAndReachTheLayout,
	"Sarko.Map.BuildingsParseAndReachTheLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingsParseAndReachTheLayout::RunTest(const FString& Parameters)
{
	// One JSON object per building, and the spawner sees walls. If the walls do
	// not reach Layout.Cover then buildings are decoration: the player walks
	// through them and every test about doorways is describing nothing.
	const FString Json = TEXT(R"({
		"id": "test",
		"extentUU": 20000,
		"raidDurationSeconds": 900,
		"playerSpawns": [ { "id": "s1", "pos": [0, 0, 100], "yaw": 0 } ],
		"blocks": [ { "id": "rim", "pos": [0, 5000, 500], "extent": [2000, 300, 500] } ],
		"buildings": [
			{
				"id": "test_shop",
				"pos": [-13500, -9000, 0],
				"size": [2200, 1500],
				"surface": "timber",
				"doors": [
					{ "side": "E", "offset": 0, "width": 320 },
					{ "side": "S", "offset": 600, "width": 300 }
				],
				"interiorWalls": [
					{ "from": [-300, -720], "to": [-300, 720], "door": { "offset": 200, "width": 300 } }
				]
			},
			{ "id": "test_bunker", "pos": [4000, 4000, 0], "size": [1200, 1000], "yaw": 45 }
		]
	})");

	FSarkoMapDefinition Definition;
	FString Error;
	TestTrue(FString::Printf(TEXT("buildings parse: %s"), *Error),
		SarkoMap::ParseDefinition(Json, Definition, Error));
	TestEqual(TEXT("two buildings"), Definition.Buildings.Num(), 2);
	TestEqual(TEXT("the id is read"), Definition.Buildings[0].Id, FString(TEXT("test_shop")));
	TestTrue(TEXT("the footprint is read"), Definition.Buildings[0].SizeUU.Equals(FVector2D(2200.f, 1500.f), 0.01f));
	TestEqual(TEXT("the surface is read"),
		static_cast<uint8>(Definition.Buildings[0].Surface), static_cast<uint8>(ESarkoSurface::Timber));
	TestEqual(TEXT("both doors are read"), Definition.Buildings[0].Doors.Num(), 2);
	TestEqual(TEXT("a door's side is read"),
		static_cast<uint8>(Definition.Buildings[0].Doors[0].Side), static_cast<uint8>(ESarkoBuildingSide::East));
	TestEqual(TEXT("a door's width is read"), Definition.Buildings[0].Doors[0].WidthUU, 320.f);
	TestEqual(TEXT("the interior wall is read"), Definition.Buildings[0].InteriorWalls.Num(), 1);
	TestTrue(TEXT("the interior wall's door is read"), Definition.Buildings[0].InteriorWalls[0].bHasDoor);
	TestEqual(TEXT("wallHeight defaults to 350"), Definition.Buildings[0].WallHeightUU, 350.f);
	TestEqual(TEXT("the second building's yaw is read"), Definition.Buildings[1].Yaw, 45.f);
	// A building with no doors is legal — the ТЗ's four closed entries.
	TestEqual(TEXT("a closed building has no doors"), Definition.Buildings[1].Doors.Num(), 0);

	// The layout is what SpawnLayout consumes. Authored blocks first, then every
	// building's walls, so an index into Layout.Cover means the same thing on
	// every machine.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Definition);
	TArray<FSarkoCoverBlock> ShopWalls;
	TArray<FSarkoCoverBlock> BunkerWalls;
	FString ExpandError;
	SarkoMap::ExpandBuilding(Definition.Buildings[0], ShopWalls, ExpandError);
	SarkoMap::ExpandBuilding(Definition.Buildings[1], BunkerWalls, ExpandError);
	TestEqual(TEXT("the layout holds the authored block plus every expanded wall"),
		Layout.Cover.Num(), 1 + ShopWalls.Num() + BunkerWalls.Num());
	TestEqual(TEXT("the authored block comes first"), Layout.Cover[0].Id, FString(TEXT("rim")));
	TestTrue(TEXT("the first expanded wall follows it"), Layout.Cover[1].Id.StartsWith(TEXT("test_shop")));

	// And the doorway is a doorway in the LAYOUT, not merely in the expander:
	// this is the end-to-end version of Task 5's invariant.
	const float WallX = -13500.f + 2200.f * 0.5f - 30.f * 0.5f;
	TestFalse(TEXT("the shop's east doorway is open in the spawned layout"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, -9000.f), Layout.Cover));
	TestTrue(TEXT("the shop's east wall is solid beside the doorway"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(WallX, -9000.f + 400.f), Layout.Cover));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBuildingsFailLoudly,
	"Sarko.Map.BuildingsFailLoudly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBuildingsFailLoudly::RunTest(const FString& Parameters)
{
	// Broken building geometry must be a LOAD error, not a spawn-time surprise:
	// ToLayout has no error channel, so anything that could fail has to fail in
	// the parser, where the message reaches a human.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("buildings is not an array"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],"buildings":{}})") },
		{ TEXT("building with no id"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"pos":[0,0,0],"size":[2000,1500]}]})") },
		{ TEXT("building with no size"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0]}]})") },
		{ TEXT("size is not a pair"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500,300]}]})") },
		{ TEXT("unknown door side"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],
				"doors":[{"side":"up","offset":0,"width":300},{"side":"S","offset":0,"width":300}]}]})") },
		// The expander's own rules must be reachable from the parser, or a
		// 200 uu doorway ships and nobody finds out until a pawn sticks.
		{ TEXT("doorway below the minimum"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],
				"doors":[{"side":"E","offset":0,"width":200},{"side":"W","offset":0,"width":300}]}]})") },
		{ TEXT("only one exit"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"buildings":[{"id":"b","pos":[0,0,0],"size":[2000,1500],"doors":[{"side":"E","offset":0,"width":300}]}]})") },
		// Ids are one namespace across the whole file, buildings included.
		{ TEXT("a building's id collides with a prop's"),
			TEXT(R"({"id":"x","extentUU":20000,"raidDurationSeconds":900,"playerSpawns":[{"pos":[0,0,0],"yaw":0}],
				"props":[{"id":"same","kind":"crate","pos":[100,100,70]}],
				"buildings":[{"id":"same","pos":[0,0,0],"size":[2000,1500]}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoMapDefinition Definition;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoMap::ParseDefinition(Case.Value, Definition, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
	}
	return true;
}
```

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeBuildingsAreEnterable,
	"Sarko.Map.BridgeBuildingsAreEnterable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeBuildingsAreEnterable::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	TestTrue(TEXT("the sector has walkable buildings at all"), Map.Buildings.Num() >= 3);

	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	for (const FSarkoBuilding& Building : Map.Buildings)
	{
		TestFalse(FString::Printf(TEXT("building '%s' is named"), *Building.Id), Building.Id.IsEmpty());
		TestTrue(FString::Printf(TEXT("building '%s' is inside the sector"), *Building.Id),
			FMath::Abs(Building.Location.X) <= Map.ExtentUU && FMath::Abs(Building.Location.Y) <= Map.ExtentUU);
		// ТЗ §13: two exits or none. The expander enforces it, but a shipped
		// walkable building with zero doors would pass that and still be a box.
		TestTrue(FString::Printf(TEXT("building '%s' is closed or has two exits"), *Building.Id),
			Building.Doors.Num() == 0 || Building.Doors.Num() >= 2);

		// The centre of every building is standable. This is the one assertion
		// that would catch "the expander emitted a floor" or "the interior walls
		// filled the room" in real authored data rather than in a fixture.
		if (Building.Doors.Num() > 0)
		{
			TestFalse(FString::Printf(TEXT("building '%s' has a standable interior"), *Building.Id),
				SarkoMap::IsPointInsideBlocksXY(
					FVector2D(Building.Location.X, Building.Location.Y), Layout.Cover));
		}
	}

	// Nobody spawns inside a wall — now including building walls, which the old
	// version of this check could not see because it only looked at Map.Blocks.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestFalse(TEXT("no player spawn sits inside a building wall"),
			SarkoMap::IsPointInsideBlocksXY(
				FVector2D(Spawn.GetLocation().X, Spawn.GetLocation().Y), Layout.Cover));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestFalse(FString::Printf(TEXT("bot '%s' does not spawn inside a wall"), *Bot.Id.ToString()),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Bot.Location.X, Bot.Location.Y), Layout.Cover));
	}
	// ТЗ §29: "перед контейнером 120 uu свободно" — a container buried in a wall
	// cannot be looted, and a building wall is the easiest thing to bury one in.
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestFalse(FString::Printf(TEXT("container '%s' is not inside a wall"), *Spot.Id),
			SarkoMap::IsPointInsideBlocksXY(FVector2D(Spot.Location.X, Spot.Location.Y), Layout.Cover));
	}
	return true;
}
```

In the same file, **delete** the file-local `IsInsideBlock` helper and replace its two call sites in `FSarkoBridgeMapIsValid` with `SarkoMap::IsPointInsideBlocksXY(FVector2D(P.X, P.Y), Map.Blocks)`. Add `#include "Map/SarkoBuildings.h"`. Two copies of a point-in-block predicate is exactly how one of them ends up not knowing about buildings.

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: `BUILD FAILED` with `no member named 'Buildings' in 'FSarkoMapDefinition'`.

- [ ] **Step 3: Add the section to the definition**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h`, add `#include "Map/SarkoBuildings.h"` beside the existing `#include "Map/SarkoMapBuilder.h"`, and add to `FSarkoMapDefinition` after `Blocks`:

```cpp
	/**
	 * Walkable buildings, one declaration each. ToLayout expands them into
	 * Layout.Cover alongside the authored blocks — there is no second spawn
	 * path and no building actor, because a building IS its walls.
	 */
	UPROPERTY()
	TArray<FSarkoBuilding> Buildings;
```

- [ ] **Step 4: Parse it**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp`, add a 2D reader to the anonymous namespace beside `ReadVector`:

```cpp
	/** Reads a ["x","y"] pair. Same discipline as ReadVector, one axis shorter. */
	bool ReadVector2(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector2D& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array) || !Array)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an array"), *Field);
			return false;
		}
		if (Array->Num() != 2)
		{
			OutError = FString::Printf(TEXT("'%s' must have exactly 2 numbers, found %d"), *Field, Array->Num());
			return false;
		}
		double Components[2] = { 0.0, 0.0 };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (!(*Array)[Index]->TryGetNumber(Components[Index]))
			{
				OutError = FString::Printf(TEXT("'%s[%d]' is not a number"), *Field, Index);
				return false;
			}
		}
		Out = FVector2D(static_cast<float>(Components[0]), static_cast<float>(Components[1]));
		return true;
	}

	/** Reads a doorway object. `side` is required for a perimeter door only. */
	bool ReadDoor(const TSharedPtr<FJsonObject>& Object, bool bNeedsSide, FSarkoBuildingDoor& Out, FString& OutError)
	{
		if (bNeedsSide)
		{
			FString SideName;
			if (!Object->TryGetStringField(TEXT("side"), SideName) || SideName.IsEmpty())
			{
				OutError = TEXT("'side' is missing or empty");
				return false;
			}
			if (!SarkoMap::ParseBuildingSide(SideName, Out.Side))
			{
				OutError = FString::Printf(TEXT("'side' must be N, E, S or W, found '%s'"), *SideName);
				return false;
			}
		}
		double Offset = 0.0;
		if (!ReadOptionalNumber(Object, TEXT("offset"), Offset, OutError))
		{
			return false;
		}
		Out.OffsetUU = static_cast<float>(Offset);

		// Width has a real default (300, the ТЗ's preferred opening) but a
		// present-and-unparseable value is still an error, not a silent 300.
		double Width = static_cast<double>(Out.WidthUU);
		if (!ReadOptionalNumber(Object, TEXT("width"), Width, OutError))
		{
			return false;
		}
		Out.WidthUU = static_cast<float>(Width);
		return true;
	}
```

Then add the section, after the `blocks` loop and before `props`:

```cpp
	// buildings
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("buildings"), Buildings, OutError))
	{
		return false;
	}
	if (Buildings)
	{
		for (int32 Index = 0; Index < Buildings->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!(*Buildings)[Index]->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("buildings[%d]: not an object"), Index);
				return false;
			}
			FSarkoBuilding Building;
			// Required, unlike every other section's id: a building's id is the
			// prefix of every wall id it generates, so an anonymous building
			// would emit walls named "_north_0" and collide with the next one.
			if (!(*Object)->TryGetStringField(TEXT("id"), Building.Id) || Building.Id.IsEmpty())
			{
				OutError = FString::Printf(TEXT("buildings[%d]: 'id' is missing or empty"), Index);
				return false;
			}
			if (!ReadVector(*Object, TEXT("pos"), Building.Location, OutError) ||
				!ReadVector2(*Object, TEXT("size"), Building.SizeUU, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			double Yaw = 0.0;
			double WallHeight = static_cast<double>(Building.WallHeightUU);
			double WallThickness = static_cast<double>(Building.WallThicknessUU);
			if (!ReadOptionalNumber(*Object, TEXT("yaw"), Yaw, OutError) ||
				!ReadOptionalNumber(*Object, TEXT("wallHeight"), WallHeight, OutError) ||
				!ReadOptionalNumber(*Object, TEXT("wallThickness"), WallThickness, OutError) ||
				!ReadOptionalSurface(*Object, Building.Surface, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			Building.Yaw = static_cast<float>(Yaw);
			Building.WallHeightUU = static_cast<float>(WallHeight);
			Building.WallThicknessUU = static_cast<float>(WallThickness);

			const TArray<TSharedPtr<FJsonValue>>* Doors = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("doors"), Doors, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			if (Doors)
			{
				for (int32 DoorIndex = 0; DoorIndex < Doors->Num(); ++DoorIndex)
				{
					const TSharedPtr<FJsonObject>* DoorObject = nullptr;
					if (!(*Doors)[DoorIndex]->TryGetObject(DoorObject) || !DoorObject)
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): doors[%d] is not an object"),
							Index, *Building.Id, DoorIndex);
						return false;
					}
					FSarkoBuildingDoor Door;
					if (!ReadDoor(*DoorObject, /*bNeedsSide*/ true, Door, OutError))
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): doors[%d]: %s"),
							Index, *Building.Id, DoorIndex, *OutError);
						return false;
					}
					Building.Doors.Add(Door);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* Walls = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("interiorWalls"), Walls, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			if (Walls)
			{
				for (int32 WallIndex = 0; WallIndex < Walls->Num(); ++WallIndex)
				{
					const TSharedPtr<FJsonObject>* WallObject = nullptr;
					if (!(*Walls)[WallIndex]->TryGetObject(WallObject) || !WallObject)
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d] is not an object"),
							Index, *Building.Id, WallIndex);
						return false;
					}
					FSarkoBuildingInteriorWall Wall;
					if (!ReadVector2(*WallObject, TEXT("from"), Wall.From, OutError) ||
						!ReadVector2(*WallObject, TEXT("to"), Wall.To, OutError))
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d]: %s"),
							Index, *Building.Id, WallIndex, *OutError);
						return false;
					}
					const TSharedPtr<FJsonObject>* DoorObject = nullptr;
					if ((*WallObject)->HasField(TEXT("door")))
					{
						if (!(*WallObject)->TryGetObjectField(TEXT("door"), DoorObject) || !DoorObject)
						{
							OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d]: 'door' is present but not an object"),
								Index, *Building.Id, WallIndex);
							return false;
						}
						if (!ReadDoor(*DoorObject, /*bNeedsSide*/ false, Wall.Door, OutError))
						{
							OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d]: %s"),
								Index, *Building.Id, WallIndex, *OutError);
							return false;
						}
						Wall.bHasDoor = true;
					}
					Building.InteriorWalls.Add(Wall);
				}
			}

			// Expand here and throw the result away. ToLayout has no error
			// channel, so every geometric rule the expander enforces has to be
			// checked at load time, where the message reaches a person — and the
			// only way to check them all is to run the real function.
			TArray<FSarkoCoverBlock> Scratch;
			FString ExpandError;
			if (!ExpandBuilding(Building, Scratch, ExpandError))
			{
				OutError = FString::Printf(TEXT("buildings[%d]: %s"), Index, *ExpandError);
				return false;
			}
			OutDefinition.Buildings.Add(Building);
		}
	}
```

Extend `CollectIds` with the buildings, so a building's id shares the one namespace — insert after the `Blocks` line:

```cpp
	for (int32 I = 0; I < Definition.Buildings.Num(); ++I) { if (!Take(Definition.Buildings[I].Id, TEXT("buildings"), I)) { return false; } }
```

and extend `RequireIdentifiedEntries` the same way (buildings are already required by the parser; the check is here for completeness and costs nothing):

```cpp
	for (int32 I = 0; I < Definition.Buildings.Num(); ++I) { if (!Require(Definition.Buildings[I].Id, TEXT("buildings"), I)) { return false; } }
```

- [ ] **Step 5: Feed the walls to the spawner**

In `SarkoMap::ToLayout`, after `Layout.Cover = Definition.Blocks;`:

```cpp
	// Authored blocks first, then every building's walls in author order, so an
	// index into Layout.Cover means the same thing on every machine. Expansion
	// cannot fail here: ParseDefinition already ran the expander on every
	// building and refused the file if any of them was broken.
	TArray<FSarkoCoverBlock> BuildingWalls;
	FString ExpandError;
	if (ExpandBuildings(Definition.Buildings, BuildingWalls, ExpandError))
	{
		Layout.Cover.Append(BuildingWalls);
	}
	else
	{
		// Unreachable via LoadDefinitionFromDisk. Reachable if someone hands
		// ToLayout a definition they built in code, which is why it logs instead
		// of silently producing a map with no buildings in it.
		UE_LOG(LogTemp, Error, TEXT("SarkoMap: building expansion failed in ToLayout: %s"), *ExpandError);
	}
```

- [ ] **Step 6: Author the three buildings**

In `SarkoGame/Data/Maps/bridge.json`, add a `"buildings"` section immediately after the closing `]` of `"blocks"`, and **delete the three `house` props** at (-13500, -9000), (-6500, -10500) and (-1500, -10500) — grep for those coordinates; if a `house` prop is at a nearby but not identical position, keep the building at the ТЗ ledger coordinate and remove the prop that overlaps it, noting the offset in the task report.

```json
  "buildings": [
    { "note": "S01 АЗС (ТЗ §28/§9). Зал + подсобка + служебная, входы В и Ю. Two exits because a bot in the only doorway is a coffin (ТЗ §13). Replaces the solid house prop that stood here.",
      "id": "bridge_gas_station", "pos": [-13500, -9000, 0], "size": [2200, 1500], "surface": "structure",
      "doors": [ { "side": "E", "offset": 0, "width": 340 }, { "side": "S", "offset": 600, "width": 320 } ],
      "interiorWalls": [
        { "note": "зал | подсобка", "from": [-300, -720], "to": [-300, 720], "door": { "offset": 250, "width": 320 } },
        { "note": "служебная, off the подсобка", "from": [-300, 200], "to": [-1070, 200], "door": { "offset": 150, "width": 300 } }
      ] },
    { "note": "S04 D1 (ТЗ §28/§9), проходим, 1 common + 1 good. Two rooms, entrances south and west.",
      "id": "bridge_village_d1", "pos": [-6500, -10500, 0], "size": [2000, 1500], "surface": "timber",
      "doors": [ { "side": "S", "offset": -300, "width": 320 }, { "side": "W", "offset": 200, "width": 300 } ],
      "interiorWalls": [ { "from": [200, -720], "to": [200, 720], "door": { "offset": -200, "width": 300 } } ] },
    { "note": "S05 D2 (ТЗ §28/§9), проходим, 2 good. One room, two exits — the simplest walkable shape, kept simple on purpose so the three buildings cover a range.",
      "id": "bridge_village_d2", "pos": [-1500, -10500, 0], "size": [2200, 1600], "surface": "timber",
      "doors": [ { "side": "N", "offset": 400, "width": 320 }, { "side": "E", "offset": -300, "width": 320 } ] },
  ],
```

Remove the trailing comma after the closing `]` if it ends up before another section's key — JSON has no trailing commas and the parser will report `not valid JSON`, which is the correct and immediate failure.

- [ ] **Step 7: Run the tests**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN`, three more tests than after Task 6. If `BridgeBuildingsAreEnterable` reports a container inside a wall, a container from the ТЗ ledger (L20 зал АЗС at -13300,-8800; L21 подсобка at -14100,-9100; L22 служебная at -13000,-9800; L31 кухня D1 at -6600,-10500; L32 D2 at -1600,-10600) has landed on an interior wall. Move the **container** by up to 400 uu, per ТЗ §29's "смещение при коллизии ≤400 uu", and record the offset in the task report — do not move the wall, because the wall is the room layout.

- [ ] **Step 8: Verify visually, inside and out**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -13500 -9000 200`

**Read both PNGs.** In the overview: three buildings at the gas station and the two village plots now read as **rooms with openings**, not as solid blocks, and the two village buildings are visibly warmer than the grey. From the player's eye inside the gas station: walls to the sides, sky above (no roof), and the two doorways visible as gaps. If the eye shot shows the pawn *outside* a wall it is standing in, `Walk` did not take effect — check the `-ExecCmds` order in `eye-shot.sh`.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.h SarkoGame/Source/SarkoGame/Map/SarkoMapDefinition.cpp SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): buildings[] in the map file, expanded into the world; three houses become enterable"
```

---

### Task 8: Roads, water and the ravine bed — and the readability verdict

Last, because it is the task whose output is a judgement about a picture, and every other piece of technology has to exist for that picture to be worth looking at.

**What gets authored, and why only this much.** Enough to prove each technique in the shipped map and to make the readability call: the ravine bed and its water (ТЗ §5–6), the highway through the middle (§7), and the west dirt road that Bridge_West's route actually uses (§7). The east dirt road, the ford's shallows and the village lanes are Stage C — authoring the full road network is content, and the ТЗ's §7 coordinates are the record for it.

**Water is opaque, and that is shipped.** A translucent material cannot exist here without authoring a material asset (spec §5.2, and the Global Constraints). The water is a dark blue-grey slab 6 uu thick sitting on top of a darker ravine-bed slab, and from a top-down camera at 20000+ uu that reads as water in a shadowed cut. It will not read as water in a close-up. **This is a documented limitation, not a bug**, and the task report must state it in those words so nobody "fixes" it by adding an asset.

**The ravine is still not a pit.** Spec §5.2, restated because this is the task where the temptation appears: the bed is a flat slab a few uu above the floor, not a hole. Crossability is enforced by the existing rim wall blocks, exactly as it is today. A real dig buys fall damage, stuck pawns and nav holes on iOS for zero gameplay.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json`
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+2 tests)

**Interfaces:**
- Consumes: everything from Tasks 1–7. No new C++.
- Produces: no new interfaces. `bridge.json` gains ~14 flat non-colliding blocks.

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeHasReadableGroundSurfaces,
	"Sarko.Map.BridgeHasReadableGroundSurfaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeHasReadableGroundSurfaces::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	int32 Roads = 0;
	int32 Water = 0;
	int32 RavineBed = 0;
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		switch (Block.Surface)
		{
			case ESarkoSurface::Asphalt:
			case ESarkoSurface::Dirt:   ++Roads; break;
			case ESarkoSurface::Water:  ++Water; break;
			case ESarkoSurface::Ravine: ++RavineBed; break;
			default: break;
		}
		// Every flat surface must be walk-over, and every walk-over surface must
		// be flat. A non-colliding block a metre tall is an invisible ramp the
		// pawn climbs; a colliding road is a kerb across the whole map.
		const bool bFlat = Block.Extent.Z <= 30.f;
		const bool bGroundSurface = Block.Surface == ESarkoSurface::Asphalt
			|| Block.Surface == ESarkoSurface::Dirt
			|| Block.Surface == ESarkoSurface::Water
			|| Block.Surface == ESarkoSurface::Ravine;
		if (bGroundSurface)
		{
			TestTrue(FString::Printf(TEXT("ground surface '%s' is flat"), *Block.Id), bFlat);
			TestFalse(FString::Printf(TEXT("ground surface '%s' does not block movement"), *Block.Id),
				Block.bBlocksMovement);
		}
		if (!Block.bBlocksMovement)
		{
			TestTrue(FString::Printf(TEXT("non-colliding block '%s' is flat enough to walk over"), *Block.Id), bFlat);
		}
	}

	TestTrue(TEXT("the sector has roads"), Roads >= 6);
	TestTrue(TEXT("the ravine has water"), Water >= 1);
	TestTrue(TEXT("the ravine has a dark bed"), RavineBed >= 1);

	// ТЗ §5: water sits inside y = -700..+700, the ravine bed inside -2200..+2200.
	// Water outside the bed is a river running across the map.
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		if (Block.Surface == ESarkoSurface::Water)
		{
			TestTrue(FString::Printf(TEXT("water block '%s' stays in the ravine bed"), *Block.Id),
				FMath::Abs(Block.Location.Y) + Block.Extent.Y <= 900.f);
		}
		if (Block.Surface == ESarkoSurface::Ravine)
		{
			TestTrue(FString::Printf(TEXT("ravine bed '%s' stays inside the ravine"), *Block.Id),
				FMath::Abs(Block.Location.Y) + Block.Extent.Y <= 2400.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeStaysInsideTheActorBudget,
	"Sarko.Map.BridgeStaysInsideTheActorBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeStaysInsideTheActorBudget::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// ТЗ §16 is a budget, and this is the whole bill: one floor, every block
	// including expanded building walls, every prop part, a container each, a
	// pad each, a bot each, the player, and two lights.
	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const int32 Actors = 1 + Layout.Cover.Num() + SarkoMap::CountPropActors(Map)
		+ Map.Containers.Num() + Map.Extractions.Num() + Map.BotSpawns.Num() + 1 + 2;

	AddInfo(FString::Printf(TEXT("bridge.json spawns %d actors (%d blocks+walls, %d prop actors from %d props)"),
		Actors, Layout.Cover.Num(), SarkoMap::CountPropActors(Map), Map.Props.Num()));

	// 450 is the ceiling for Stage B. Stage C's full ledger (≈20 buildings, a
	// treeline border, the north filled per §15) will pass it, and that is the
	// point at which instanced static meshes stop being premature — see the
	// Global Constraints. A failure here is not a bug, it is that decision
	// coming due.
	TestTrue(FString::Printf(TEXT("the sector spawns at most 450 actors (it spawns %d)"), Actors), Actors <= 450);
	return true;
}
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map`
Expected: the build succeeds and `BridgeHasReadableGroundSurfaces` fails on `the sector has roads` (there are none yet). `BridgeStaysInsideTheActorBudget` should already pass — it is a ceiling, and Step 3 is what pushes toward it.

- [ ] **Step 3: Author the ground surfaces**

Add these to the **end** of `bridge.json`'s `"blocks"` array (after the eight rim walls, before the closing `]`, with a comma after the last existing entry). Z ordering is deliberate and tight: floor top is z = 0, the ravine bed occupies 0–4, the water 4–10, and the bridge deck already sits at z = 10 with a 40 uu lip, so nothing pokes through anything.

```json
,
    { "note": "RAVINE BED — the visual stand-in for a 400-700 uu dig (spec §5.2). Flat and non-colliding: cliffs plus a dark bed plus water read identically from a top-down camera, and a real pit buys fall and stuck bugs on iOS for zero gameplay. Crossability is the rim walls above, exactly as before.",
      "id": "bridge_ravine_bed", "pos": [0, 0, 2], "extent": [20000, 2200, 2], "surface": "ravine", "blocksMovement": false },
    { "note": "WATER — y = -700..+700 per ТЗ §5. OPAQUE: a translucent material needs an authored asset and this project authors none (spec §5.2). Dark blue-grey on the darker bed reads as water from above; it will not read as water in a close-up, and that is a shipped limitation.",
      "id": "bridge_ravine_water", "pos": [0, 0, 7], "extent": [20000, 700, 3], "surface": "water", "blocksMovement": false },

    { "note": "HIGHWAY (ТЗ §7): (-1500,+20000) -> (0,0) -> (+1000,-10000), 900-1300 wide. Three straight segments approximating the two doglegs; the bridge deck props carry it across the ravine.",
      "id": "bridge_road_highway_north", "pos": [-750, 11000, 2], "extent": [550, 9000, 2], "surface": "asphalt", "blocksMovement": false, "yaw": 4 },
    { "id": "bridge_road_highway_approach", "pos": [-200, 3400, 2], "extent": [550, 1600, 2], "surface": "asphalt", "blocksMovement": false },
    { "id": "bridge_road_highway_south", "pos": [500, -6000, 2], "extent": [550, 4200, 2], "surface": "asphalt", "blocksMovement": false, "yaw": -6 },
    { "id": "bridge_road_highway_fork", "pos": [400, -11000, 2], "extent": [500, 1200, 2], "surface": "asphalt", "blocksMovement": false },

    { "note": "WEST DIRT ROAD (ТЗ §7): (-18000,+17000) -> (-15500,+11000) -> (-14500,+3000) -> pipes -> (-14500,-5000) -> АЗС. 500-800 wide, lighter than the ground. This is Bridge_West's actual route, which is why it is the road that exists now.",
      "id": "bridge_road_west_spawn", "pos": [-16750, 14000, 2], "extent": [325, 3300, 2], "surface": "dirt", "blocksMovement": false, "yaw": -22 },
    { "id": "bridge_road_west_upper", "pos": [-15000, 7000, 2], "extent": [325, 4100, 2], "surface": "dirt", "blocksMovement": false, "yaw": -7 },
    { "id": "bridge_road_west_pipes_north", "pos": [-14500, 2600, 2], "extent": [325, 1900, 2], "surface": "dirt", "blocksMovement": false },
    { "id": "bridge_road_west_pipes_south", "pos": [-14500, -3200, 2], "extent": [325, 1500, 2], "surface": "dirt", "blocksMovement": false },
    { "id": "bridge_road_west_station", "pos": [-14000, -7000, 2], "extent": [325, 2800, 2], "surface": "dirt", "blocksMovement": false, "yaw": 8 },
    { "note": "The spur into the АЗС forecourt, so the station reads as somewhere a road goes rather than a shed in a field.",
      "id": "bridge_road_station_yard", "pos": [-13400, -8100, 2], "extent": [900, 300, 2], "surface": "dirt", "blocksMovement": false }
```

Nothing else changes. The rim walls, the props, the containers and the bots stay exactly where they are — a road that runs under a wreck is correct, because the wreck is on the road.

- [ ] **Step 4: Run the tests**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`
Expected: `ALL GREEN` and two more tests than after Task 7 — the final total is `B + 22`. The `AddInfo` line from `BridgeStaysInsideTheActorBudget` appears in the log; **copy the actor count into the task report**, because Stage C inherits it.

- [ ] **Step 5: The readability verdict**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -14500 -3200 200`

**Read both PNGs and answer each of these in the task report, in words, not with an assertion:**

1. Is the ravine legible as a ravine — a dark band with a darker water line — without being a hole?
2. Does the highway read as a dark ribbon against the olive ground, and the dirt road as a *lighter* one? (ТЗ §14: "грунтовка светлее, асфальт тёмный". If they read as the same value, the palette needs the numbers moved, not the test relaxed.)
3. Does the bridge deck stand out against the asphalt approaching it?
4. Are the three buildings' interiors visible from above, and are the two village ones visibly warmer than the industrial grey?
5. From the player's eye on the dirt road at the pipes: can you tell road from ground at ground level, or does the surface distinction only exist from the overview? (Both answers are useful; only the second is a problem worth a follow-up.)

If any answer is no, adjust the palette values in `SarkoMapPalette.cpp` and re-shoot. The palette test's relations are the floor, not the target — moving a value further apart keeps the test green.

- [ ] **Step 6: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): the ravine reads as a ravine — dark bed, water, and the roads that cross it"
```

---

## Self-Review

**Spec §5 coverage, clause by clause.**

- **§5.1 buildings** — `buildings[]` with `{ id, pos, size, wallHeight=350, wallThickness=30, doors:[{side,offset,width≥250}], interiorWalls:[{from,to,door?}] }` in local coords → Task 5 (structs + expander) and Task 7 (JSON + wiring). Pure and deterministic, unit-tested: door gaps really are gaps (`DoorwaysAreRealGaps`, by sampling the wall's own centre line across the declared opening and 5 uu outside each edge), walls really close (`ClosedBuildingIsSealed`, 201 samples per side, every one inside), no wall overlaps another (pairwise AABB check in the same test), determinism pinned separately (`BuildingExpansionIsDeterministic`). No roofs, no stairs, no second floor: the "every emitted block is thin on exactly one horizontal axis" assertion is what makes a roof or a floor slab impossible to emit unnoticed. Closed buildings are the degenerate case and are explicitly legal (zero doors); **one** door is an error, which is ТЗ §13's two-exits rule made unforgettable.
- **§5.2 readability** — Task 2 (`ESarkoSurface`, the palette, per-surface paint) and Task 8 (roads, water, ravine bed authored). Dirt lighter than ground, asphalt dark, deck contrasting, water dark blue-grey, industry rust, village warm, extraction green: every one is an assertion in `Sarko.Config.SurfacePaletteIsReadable`, not a comment. Roads are flat non-colliding boxes. Water is opaque and documented as a limitation in three places (Global Constraints, the `Water` palette comment, Task 8's preamble). **Ravine depth stays visual, not physical** — restated in the out-of-scope section and again in Task 8, with the reason (identical read from above; a real pit buys fall/stuck/nav bugs on iOS).
- **§5.3 new kinds** — Task 4 adds exactly the nine named: `rock`, `bush` (no collision, asserted), `log`, `fence_section`, `road_sign` (composite), `concrete_barrier`, `trailer` (composite), `pylon` (composite, ≥4 parts asserted), `treeline` (tall, impassable, dark green, asserted). Composite support is Task 3.
- **§5.4 ids and groups** — Task 1. Optional on every entry, **required** on containers/spawns/extractions (via `LoadDefinitionFromDisk`) and on buildings (via the parser itself), uniqueness enforced across the whole file as a parse error and pinned by `RejectsBadIds`. ТЗ §18 naming: the three extractions get meaningful ids by hand; the rest are index-based placeholders with the reason stated (Stage C re-authors this file).
- **The two debts** — near-black shadows: Task 6, whole task, with the mobile cost stated and the alternative rejected on cost. `MapExtent` vs `extentUU`: Task 1 Step 9, asserted through the settings' own `MapId` so it also pins that the configured map exists.

**Backward compatibility with the real `bridge.json`, checked field by field.** Every schema addition is optional except `buildings[].id`, and `buildings` is a section that does not exist in the file today. `id` is optional in `ParseDefinition` (`IdsAreOptionalAndUnique` re-parses the existing `MinimalMapJson` fixture with no ids to prove it) — the only place it is required is `LoadDefinitionFromDisk`, and Task 1 Step 8 authors the 65 ids that satisfy it in the same commit. `blocks[].surface` defaults to `Structure` and `blocks[].blocksMovement` to `true`, which is exactly what the eight existing rim walls already were — asserted in `BlocksCarrySurfaceAndCollision`. `FSarkoPropKind` changing shape does not touch the file at all: all eleven legacy kinds keep byte-identical extents and zero offsets, pinned by a table of expected values in `PropKindsAreComplete`, which matters because 238 props are placed with `pos.z` equal to their kind's half-height. The parser continues to ignore `note` and `_readme`.

**Kind extents against a 176 uu pawn.** `PropKindScaleMatchesThePawn` asserts the relationships rather than the numbers: `car_wreck`, `sandbag`, `concrete_barrier`, `log`, `rock` are all below 176 uu and above 60 (cover you shoot over, not a kerb); `house`, `wall`, `treeline`, `fence_section` are at or above 176 (they cut line of sight); nothing exceeds 25 m tall or 20 m wide (a top-down frame of the whole sector cannot afford a smear). `bush` never collides. Composite parts are checked never to sit below the floor (`Offset.Z - Extent.Z >= -1`), which is the specific way a composite kind goes wrong.

**No binary assets anywhere.** The plan creates: 4 `.h`/`.cpp` pairs' worth of C++ (`SarkoMapPalette`, `SarkoBuildings`), 1 new test file, 1 `.sh`, and edits to `.json`, `.ini` and existing sources. Every mesh is `/Engine/BasicShapes/{Cube,Cylinder,Sphere}`; every material is `/Engine/BasicShapes/BasicShapeMaterial` through a dynamic instance; the one new engine asset is `/Engine/MapTemplates/Sky/DaylightAmbientCubemap`, referenced by path, verified to exist on disk as a `TextureCube`, and pinned by `Sarko.Config.EngineMeshPathsResolve`. `SarkoGame/Content/Mannequins/` is untouched. No `.uasset`, `.umap`, Blueprint, UMG, material, DataTable or font is authored or edited.

**Trap checklist, walked deliberately.** `./Scripts/run-tests.sh` with a named expected count in every verify step and never a bare exit code — and the counts are **relative to a baseline recorded in Task 1 Step 1**, because in-flight Stage A.5 work moves the number. Visual claims are `-RenderOffscreen` + `HighResShot` and the instruction is always "read the PNG"; `eye-shot.sh` passes `?game=/Script/SarkoGame.SarkoRaidGameMode` (the Shelter is now the global default game mode, so a bare launch photographs a menu) and appends `Walk` after `BugItGo` because `BugItGo` calls `Ghost()`. All new geometry goes through the single `SpawnMeshBox`, keeping Movable → mesh → scale → collision → paint → Static. Every primitive is painted, so nothing can inherit `WorldGridMaterial`. `DefaultEngine.ini` gains one line under an `Engine`-config section (`ProjectPackagingSettings`), and it contains no `//` so it needs no quoting; nothing is added to `DefaultGame.ini`. Every `FRotator`/`FVector` comparison in a test uses `.Equals(..., Tolerance)` and every scalar literal is `f`-suffixed; the yaw test compares `FRotator`s with `Equals` specifically because 5.8's members are doubles. `FSarkoMapDefinition` is forward-declared at **global** scope at the top of `SarkoMapKinds.h`, never as an elaborated specifier inside `namespace SarkoMap`. No per-tick anything: every line this plan adds runs once at map load, and the sky light is captured once rather than per frame. The actor bill is asserted (`BridgeStaysInsideTheActorBudget`, ceiling 450, current ~345) and the instancing threshold is written down for Stage C rather than left to be discovered.

**Type consistency across tasks.** `ESarkoSurface` is defined once in Task 2 and used unchanged by Tasks 3, 4, 5, 7, 8. `Palette::ColourFor`/`RoughnessFor` keep one signature everywhere. `FSarkoCoverBlock` gains `Id` in Task 1 and `Surface`/`bBlocksMovement` in Task 2, and Task 5's `MakeWall` sets all three. `ExpandBuilding` resets its output in every caller and every test; `ExpandBuildings` resets then accumulates, asserted. `IsPointInsideBlocksXY` takes `(FVector2D, const TArray<FSarkoCoverBlock>&)` in the expander, in `BuildingTest`, in `MapDefinitionTest` and in `BridgeMapTest`, and Task 7 deletes `BridgeMapTest`'s private duplicate — two point-in-block predicates is exactly how one of them ends up not knowing about buildings. `CountPropActors` is defined in Task 3 and used in Tasks 3 and 8. Door offsets are signed-from-midpoint in the struct comment, in the expander, in `ReadDoor` and in every fixture.

**Deliberate deviations, flagged rather than buried.**
1. `ParseDefinition` and `LoadDefinitionFromDisk` enforce **different** id strictness. Spec §5.4 wants ids required; the ТЗ §19 test list wants old maps to keep loading; every fixture in the suite has no ids. The split satisfies all three, and the reason is written in `RequireIdentifiedEntries`' own comment.
2. Props stop being uniformly grey, reversing a comment currently in `SpawnProps`. ТЗ §14 requires it; the replacement guarantee is a saturation ceiling on every world surface with the three gameplay tints exempt, asserted.
3. Task 4's nine kinds ship, but the eleven legacy kinds are **not** re-surfaced (freight cars stay grey rather than rusting). That is a content judgement about the existing 238 props and it belongs to Stage C, not to a task whose screenshot must be identical to the one before it.
4. `bridge.json` gains 3 buildings and ~14 flat surfaces. That is technology proof inside real content at ТЗ ledger coordinates, not Bridge_West authoring — which is out of scope and says so.

**Known limitations, for the task reports rather than for silence.** Opaque water. No sky, no horizon, no fog — the world ends at the floor's edge in black, which is invisible from a top-down camera and will look wrong in the first low-angle screenshot anyone takes. The `DirectoriesToAlwaysCook` line for the engine sky directory **cannot be verified without a packaged device build**; if a later iOS build shows black shadowed faces while the editor looks right, that line is the first suspect. `ShadowAmount` lifts cast shadows and the sky light lifts unlit faces, but if `r.MobileHDR=False` drops sky-light contribution on the mobile preview path, the sky light will do nothing and the report must say so rather than escalating to `SkyAtmosphere`, which this plan rejected on per-frame cost. The road network and the ford's shallows are three straight segments and nothing, respectively — the ТЗ's §7 coordinates remain the record for Stage C. Interior passage clearance is checked between *parallel* walls only; a room made unreachable by two perpendicular walls forming a 100 uu dogleg would pass, and the fix if it ever bites is a flood fill from each doorway, which is more machinery than the ТЗ's building shapes need today.
