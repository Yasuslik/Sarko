# Bridge_West Content Implementation Plan (Stage C)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Author the Bridge sector's **west third** as a complete, playable raid — spawn camp → pipe crossing → gas station → rail depot → **E1** — with the rest of the 400×400 m map physically closed so it reads as world rather than as a wall; author the one-time tutorial's static loot layout so `TUTORIAL loot requested but none of N containers carries fixedItems` stops appearing; and fill the empty north per ТЗ §15.

**Architecture:** Almost nothing here is code. Stage B built the technology (`buildings[]` + a validating expander, 20 prop kinds, 11 surfaces, strict parser, ids); Stage C is `SarkoGame/Data/Maps/bridge.json` plus the automation tests that pin what the ТЗ says about it. Three exceptions, each a table entry rather than a system: two `house` variants and a `bridge_rail` kind so clusters separate by hue (ТЗ §14), and a twelfth `ESarkoSurface` for the ford's shallow water (ТЗ §6). The east/north closure is deliberately **6 long blocks plus scattered props** rather than ~32 tiled treeline props — a block can be any length, a prop cannot, and that one structural choice is worth 26 actors against a mobile budget.

**Tech Stack:** UE 5.8, C++ only, `Build.sh` + `UnrealEditor-Cmd`, automation tests under `-nullrhi`, visual verification under `-RenderOffscreen`. Engine primitive meshes by path. No new module dependencies, no backend change.

## Global Constraints

- **Spec §6 and §6.5 of `docs/superpowers/specs/2026-07-30-sarko-production-raid-loop-design.md` are normative** for this stage. The design of record for the full map is `docs/design/bridge-full-map-tz.md` (the owner's ТЗ): §5–6 crossings, §7 roads, §8 north, §9 south, §11 bots, §12 extractions, §13 buildings, §14 readability, §15 filling the north, §16 iOS performance, §18 ids, §28 building ledger, §29 container ledger, §30 loot rules, §32 density. Where this plan deviates it says so in the task, with the reason.
- **The owner's Bridge_West numbers win over the full-map ledgers where they conflict**: ~10 buildings, **19 containers** (3 junk / 7 common / 6 good / 1 med / 2 military), **6 bots** (2 gas station, 4 rail), **E1 (-15500, +19500)** the only reachable extraction. E2/E3 stay in `extractions[]` behind the closure. Every conflict with §28/§29/§11 is listed in the task that creates it and again in the Self-Review.
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`. **Agent shells reset their cwd between calls, so every command below begins with an explicit `cd`.**
- **Create no binary assets. Ever.** No `.uasset`, `.umap`, Blueprint, UMG, material asset, DataTable, Behavior Tree, font. C++, `.ini`, `.json`, `.sh` only. Referencing an engine asset by path is how all geometry and all materials are obtained. `SarkoGame/Content/Mannequins/` is committed Epic template content and is **read-only**.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code.** `UnrealEditor-Cmd` exits 0 having run zero tests. The script also **refuses to run** while a process holds the project open, and refuses if the module dylib is older than the newest source file — a stale binary once reported a full green suite for code that was never compiled. Both refusals are features; do not work around them.
- **Test counts here are RELATIVE.** The UE suite was at **103** before the expander fix wave (`1ae671c`) landed and that wave adds more; the Go suite is at **94** and **this stage does not touch the backend**, so 94 must not move. Task 1 Step 1 records the real baseline `B`; every later step names `B + <delta>`: T1 +1, T2 +1, T3 +0, T4 +1, T5 +1, T6 +1, T7 +1, T8 +0 → **B + 6**. Recompute `B` at execution time; do not copy 103.
- **This plan depends on the expander fix wave** (in flight while the plan was written, landed as `1ae671c` "fix(map): the expander refuses posts, sealed rooms and overlapping walls"): hairline wall stubs refused via `SarkoMap::MinWallStubUU` (120 uu), interior walls trimmed where they tee into another wall instead of overlapping it, unreachable sealed rooms refused by a flood fill gated on `SarkoMap::MinSealedRoomUU`, plus two new map-wide tests — `Sarko.Map.BridgeBuildingsAreEnterable` now asserts **no two solid blocks intersect anywhere in the shipped map**, and `Sarko.Map.BridgePropsClearTheWalls` asserts **no prop part overlaps a building wall**. Every building and every barrier below is authored against those rules. If Task 1 Step 1 does not show those tests in the suite, stop and report — the geometry in this plan assumes them.
- **The no-overlap rule shapes the closure.** Solid block ↔ solid block and prop ↔ building wall are both forbidden; **prop ↔ prop and prop ↔ block are not tested** and are used deliberately (a treeline prop straddling the barrier block it hides is correct). Every barrier joint below **butts exactly** — two blocks that share a face plane are *separated* at `BlocksOverlapXY`'s default 0.01 slack, which is the same trick the expander's own perimeter uses. An overlap of even 1 uu fails the suite.
- **Two authoring conventions for `pos.z`, and they must not be mixed.** A single-box kind is authored with `pos.z` equal to the kind's own half-height (`crate` → 70, `car_wreck` → 75, `rock` → 65, `bush` → 45, `log` → 55, `fence_section` → 92, `concrete_barrier` → 55, `treeline` → 500). A **composite** kind (`pylon`, `road_sign`, `trailer`) is authored with **`pos.z = 0`** because each part carries its own height. Getting this backwards buries the prop or floats it.
- **Non-colliding blocks must be flat**: `extent.Z <= 30` or `Sarko.Map.BridgeHasReadableGroundSurfaces` fails, and every `asphalt`/`dirt`/`water`/`ravine` block must additionally be `"blocksMovement": false`. `concrete` pads are the same shape but are not in that list, so they only have to obey the "non-colliding ⇒ flat" half.
- **Automation runs under `-nullrhi` and can see nothing.** Every visual claim is `-RenderOffscreen` + a screenshot that is then **read as an image**. `Scripts/overview-shot.sh` (needs `?game=/Script/SarkoGame.SarkoRaidGameMode`, which it already passes — the Shelter is the global default game mode, so a bare launch photographs a menu), `Scripts/eye-shot.sh <x> <y> <z>` for a player-height frame. The loop is: edit JSON → shoot → **read the PNG** → fix.
- **The overview camera puts east up and north right.** Rotate the PNG mentally (or with an image tool) before judging it against the north-up infographic. A "the map is mirrored" conclusion from an unrotated frame has been reached on this project before and was wrong.
- **`BugItGo` calls `Ghost()`**, which disables capsule collision for the rest of the run. `Scripts/eye-shot.sh` already appends `Walk`; keep it if you edit the script.
- **Never profile with `-csvCaptureFrames`.** It repoints `FPaths::ProjectSavedDir()`, so a profiled headless run silently plays as a different player and swallows `Shot showui`. Use `-ExecCmds="CsvProfile Start"` (Task 3 does).
- **`config = Game` → `Config/DefaultGame.ini`; `config = Engine` → `Config/DefaultEngine.ini`.** `USarkoRaidSettings` and `UProjectPackagingSettings` are **`config = Game`** (the packaging block was just moved there after sitting dead in the Engine ini, which would have shipped a device build with no map). `RendererSettings` is Engine. ini string values containing `//` must be quoted or `SwallowDoubleSlashComments` truncates them. Task 8 is the only task that may touch an ini, and only with a named justification.
- **`SetMobility(Static)` before `SetStaticMesh` silently no-ops after BeginPlay.** The one spawn path is `SpawnMeshBox` in `Map/SarkoMapBuilder.cpp`: Movable → mesh → scale → collision → paint → Static. This plan adds no second spawn path.
- **Palette values are LINEAR, not sRGB.** They feed `BaseColor` directly.
- **`FRotator`/`FVector` members are doubles in 5.8.** Compare with `.Equals(..., Tolerance)`; suffix scalar literals with `f`.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything that discards working-tree changes — another agent is editing C++ on this branch, and a subagent on this project has already destroyed an uncommitted file that way. **Never `git add -A`**; stage the exact paths each task names. Do not switch branches. Do not push.

## Explicitly out of scope

- **The east, the south and the north-east as play space.** The village (S04–S13), the промзона (S14–S18), the КПП (N04/N05), the water tower shed (N06), the bridge and the ford as *routes*, and the old дачи (N01–N03) are Stage D — they open when the barriers come down. Their authored content stays in the file where it exists (the two walkable village houses, the industrial clusters, the traffic jam) and is visible beyond the closure as world; nothing new is authored there except the two landmark passes in Task 7.
- **The ravine as a physical pit.** Spec §5.2 decided this and it is not reopened: cliffs + dark bed + water read identically from a top-down camera, and a real dig buys fall damage, stuck pawns and nav holes on iOS for zero gameplay.
- **Instanced static meshes.** Task 3 makes that decision explicitly and rejects it for this stage, with the trigger that would reverse it written down.
- **Any backend change.** No new item, no loot-table edit, no `tutorial_completed` change. The tutorial layout uses only ids already in `Data/Items/items.json`, so `domain.ValidateRaidItems` accepts the haul unchanged.
- **Roofs, stairs, second floors, destructibility, doors as a mechanic, quests, keys, bosses, weather, in-raid vehicles.** ТЗ invariants forbid all of them.
- **Real art.** Production art means marketplace packs the owner installs, after which the kind table points at them. Colour and silhouette are the ceiling without binary assets.

## What the ТЗ asks for that Stage B's technology still cannot express

Flagged here rather than silently dropped. Each one is repeated in the task where it bites and in the Self-Review.

1. **The АЗС's навес (canopy)** — ТЗ §9. A roof is forbidden: the camera is above and ТЗ §13 says "крыша не закрывает игрока". Task 4 expresses the canopy as a concrete pad plus four pillars, so it reads as a canopy *footprint* from above and as a colonnade at eye level, and never as shelter.
2. **The вывеска АЗС as a landmark** — ТЗ §14 lists it beside the bridge and the water tower. The `road_sign` kind is a 170 uu plate at 320 uu, which is signage, not a landmark. Task 4 builds it as a `pipe` mast with a `road_sign` at its foot. A tall plate on a mast needs a new composite kind and is not worth one this stage.
3. **Individual trees** (ТЗ §8's "дерево" at L02, ТЗ §15's implied treed north). A canopy hides the player from a top-down camera — a gameplay defect, not a look. `treeline` is the approved substitution (spec §5.3), so a lone tree becomes a small treeline clump. This is why L02's container stands beside a hedge rather than a trunk.
4. **The fallen ЛЭП pylon** (ТЗ §8 "одна упавшая"). `FSarkoPropPart` has no pitch or roll, so a leaning or fallen tower is not expressible. Task 6 authors three standing pylons in the active north and notes that the fallen one belongs to the closed stretch of the line (its container L07 at (-4500,+12500) is behind the closure anyway).
5. **The ж/д цистерна** (ТЗ §9). No tank kind; an upright `pipe` cylinder is what a vertical fuel tank looks like from above and is used instead. A horizontal tank needs roll.
6. **Translucent or animated water.** Needs a material asset; the project authors none. Task 7 adds a *second opaque tone* (`ESarkoSurface::Shallow`) so the ford's «мелкая вода» reads as a lighter band against the deep water instead of as nothing at all. It will still not read as water in a close-up. That is shipped, documented, and not a bug.
7. **A sky, a horizon or distance fog.** The world ends at the floor's edge in black. Invisible from a top-down camera; it will look wrong in the first low-angle screenshot anyone takes.
8. **Bots that hold a position without hearing through walls.** ТЗ §11 says "проблемы решать размещением, не глобальным слухом" — this plan solves it by placement only (every pair of the six bots is ≥1800 uu apart, which is the hearing radius), and changes no AI code.

## File Structure

```
SarkoGame/
├── Data/Maps/
│   └── bridge.json                          # T1 ledger, T2 closure, T4 camps, T5 fixedItems,
│                                            #   T6 north fill, T7 deck/rails/ford/house kinds
├── Scripts/
│   └── overview-shot.sh                     # T8: one optional env var, OVERVIEW_EXTRA_CMDS
└── Source/SarkoGame/
    ├── Map/
    │   ├── SarkoMapKinds.cpp                # T7: bridge_rail, house_timber, house_industrial
    │   ├── SarkoMapPalette.h/.cpp            # T7: ESarkoSurface::Shallow
    └── Tests/
        ├── BridgeMapTest.cpp                # T1 +1, T2 +1, T3 edits, T4 +1, T6 +1
        ├── MapDefinitionTest.cpp            # T2 edits (two assertions), T7 edits + T7 +1
        └── MapBuilderTest.cpp               # T7 edits (palette relations)
```

No new file is created. That is the point of Stage C: Stage B's technology was built so that a sector is a data file.

---

### Task 1: The Bridge_West ledger — spawns, containers, bots, extractions

First, and it is the whole raid: after this task a player spawns at ТЗ §8's north-west camp, loots nineteen containers along the west route, meets six bots at the gas station and the rail depot, and walks out at **E1**. Nothing else in this plan is needed for that, which is the owner's rule — the raid must be playable from spawn to extraction before anything is decorated.

The four sections are replaced wholesale rather than edited. Twenty-three of the forty-two containers and ten of the sixteen bots are behind the coming closure, the spawns are 400–1500 uu off their ledger coordinates, and all three extractions are 900–3500 uu off theirs; a wholesale replacement is one diff to review instead of sixty.

**Deliberate divergences from the ТЗ, all of them the owner's Bridge_West numbers winning:**

- **Tiers.** §29 makes L13, L18, L25 and L30 junk and L29 common. The owner's split is 3 junk / 7 common / 6 good / 1 med / 2 military, so the three junk containers are the three at the spawn camp (nothing there is worth dying for, which is the point of a spawn camp), L13/L18/L25/L30 become common and L29 becomes good. When the full map opens, §29's tiers govern again.
- **Two military containers that §29 does not have.** §29 puts no military west of the bridge — its four are the КПП (L08/L09) and the промзона (L39/L41), all behind the closure. The owner asked for two, so they are authored at the rail depot, which is Bridge_West's deep zone, with ids that do not pretend to be ledger rows: `bridge_loot_rail_mil_01/02`. They retire when §29's four arrive.
- **Four rail bots.** §11 has five bot positions west of the bridge: 2 АЗС, 2 ж/д, 1 трубы. The owner asked for 2 + 4 and for no bot at the pipes (spec §6.5 teaches healing "before the first bot", and the first bot is at the gas station). So the two §11 rail positions are used verbatim and two more are added at the depot's own sub-areas.
- **Shifts.** §11 allows ±300–800 uu for bots; §29 allows ≤400 uu for containers "фиксировать в отчёте". Every shift used is listed in the step and must be repeated in the task report.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (`playerSpawns`, `containers`, `botSpawns`, `extractions`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test)

**Interfaces:**
- Consumes: `SarkoMap::LoadDefinitionFromDisk`, `SarkoMap::ToLayout`, `SarkoMap::IsPointInsideBlocksXY`, the file-local `SolidOnly` helper already in `BridgeMapTest.cpp`.
- Produces: no C++ interface. The container ids `bridge_loot_l01 … bridge_loot_l30` and `bridge_loot_rail_mil_01/02` are what Task 5 authors `fixedItems` onto; the extraction id `bridge_extract_north_path` keeps its name and moves to E1.

- [ ] **Step 1: Record the test baseline**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN` and `==> B test(s) performed, 0 failed`. **Write `B` down in the task report** — every later count in this plan is `B + delta`. Also confirm `Sarko.Map.BridgePropsClearTheWalls` appears in the result list: if it does not, the expander fix wave this plan depends on is not in the tree, and the geometry in Tasks 2 and 4 has not been checked against the rules that will judge it. Stop and report in that case.

If the run refuses because a process holds the project open, close the editor rather than working around it.

- [ ] **Step 2: Write the failing ledger test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`, inside the existing `#if WITH_AUTOMATION_TESTS` block:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestLedgerIsAuthored,
	"Sarko.Map.BridgeWestLedgerIsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestLedgerIsAuthored::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The owner's Bridge_West numbers, as numbers. The full map's 42/16 move to
	// docs/design/bridge-full-map-tz.md as the acceptance bar for Stage D; this
	// is the bar for the sector that ships now.
	TestEqual(TEXT("nineteen containers"), Map.Containers.Num(), 19);
	TestEqual(TEXT("six bots"), Map.BotSpawns.Num(), 6);
	TestEqual(TEXT("four player spawns"), Map.PlayerSpawns.Num(), 4);
	TestEqual(TEXT("three extractions, one of them reachable"), Map.Extractions.Num(), 3);

	// 3 junk / 7 common / 6 good / 1 med / 2 military.
	TMap<FName, int32> Tiers;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		Tiers.FindOrAdd(Spot.Tier)++;
	}
	TestEqual(TEXT("three junk"), Tiers.FindRef(TEXT("junk")), 3);
	TestEqual(TEXT("seven common"), Tiers.FindRef(TEXT("common")), 7);
	TestEqual(TEXT("six good"), Tiers.FindRef(TEXT("good")), 6);
	TestEqual(TEXT("one med"), Tiers.FindRef(TEXT("med")), 1);
	TestEqual(TEXT("two military"), Tiers.FindRef(TEXT("military")), 2);

	// The active third: the barrier Task 2 builds runs at x = -6100 north of the
	// ravine and x = -9100 south of it, so nothing the player is meant to reach
	// may be authored east of those lines. Checked as data rather than left to
	// the closure, because a container behind the barrier is loot that exists,
	// is on the ledger, and can never be picked up.
	const auto InActiveThird = [](const FVector& P)
	{
		return P.Y > 0.f ? P.X <= -6100.f : P.X <= -9100.f;
	};
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		TestTrue(FString::Printf(TEXT("container '%s' is inside the active third"), *Spot.Id),
			InActiveThird(Spot.Location));
	}
	for (const FSarkoBotSpot& Bot : Map.BotSpawns)
	{
		TestTrue(FString::Printf(TEXT("bot '%s' is inside the active third"), *Bot.Id),
			InActiveThird(Bot.Location));
	}
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		TestTrue(TEXT("every player spawn is inside the active third"),
			InActiveThird(Spawn.GetLocation()));
	}

	// ТЗ §11: the first fight is one bot. Eight bots that all heard the player
	// and converged turned a firefight into an execution once already, and the
	// hearing radius is 1800 uu — so no two bots may be able to hear the same
	// shot. This is the whole reason six positions were chosen by hand.
	for (int32 A = 0; A < Map.BotSpawns.Num(); ++A)
	{
		for (int32 B = A + 1; B < Map.BotSpawns.Num(); ++B)
		{
			const float Distance = FVector2D(
				Map.BotSpawns[A].Location.X - Map.BotSpawns[B].Location.X,
				Map.BotSpawns[A].Location.Y - Map.BotSpawns[B].Location.Y).Size();
			TestTrue(FString::Printf(TEXT("bots '%s' and '%s' are %.0f uu apart (>= 1800)"),
				*Map.BotSpawns[A].Id, *Map.BotSpawns[B].Id, Distance), Distance >= 1800.f);
		}
	}

	// ТЗ §8: "бот не виден при появлении". The nearest bot is a whole zone away.
	for (const FTransform& Spawn : Map.PlayerSpawns)
	{
		for (const FSarkoBotSpot& Bot : Map.BotSpawns)
		{
			const float Distance = FVector2D(
				Spawn.GetLocation().X - Bot.Location.X,
				Spawn.GetLocation().Y - Bot.Location.Y).Size();
			TestTrue(FString::Printf(TEXT("bot '%s' is not visible from a spawn (%.0f uu)"),
				*Bot.Id, Distance), Distance >= 6000.f);
		}
	}

	// ТЗ §12's E1, to the unit. This is the one extraction a Bridge_West player
	// can reach, and the tutorial's last teaching beat is standing in it.
	const FSarkoExtractionSpot* E1 = Map.Extractions.FindByPredicate(
		[](const FSarkoExtractionSpot& Spot) { return Spot.Id == TEXT("bridge_extract_north_path"); });
	TestNotNull(TEXT("E1 exists by id"), E1);
	if (E1)
	{
		TestTrue(TEXT("E1 is at ТЗ §12's (-15500, +19500)"),
			FMath::IsNearlyEqual(static_cast<float>(E1->Location.X), -15500.f, 1.f) &&
			FMath::IsNearlyEqual(static_cast<float>(E1->Location.Y), 19500.f, 1.f));
		TestTrue(TEXT("E1 is inside the active third"), InActiveThird(E1->Location));
	}
	// E2 and E3 are data behind the closure, deliberately: ТЗ §12 lists three and
	// Stage D opens them. They must NOT be inside the active third, or the sector
	// would ship with three working exits and no reason to learn the route.
	for (const FSarkoExtractionSpot& Spot : Map.Extractions)
	{
		if (Spot.Id != TEXT("bridge_extract_north_path"))
		{
			TestFalse(FString::Printf(TEXT("extraction '%s' is behind the closure"), *Spot.Id),
				InActiveThird(Spot.Location));
		}
	}
	return true;
}
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map.BridgeWestLedgerIsAuthored`

Expected: the build succeeds, one test runs, and it fails on `nineteen containers` (there are 42), `six bots` (16), and several `is inside the active third` lines. If it fails to *build*, the appended test is inside the `#endif` — move it above.

- [ ] **Step 4: Replace `playerSpawns` with ТЗ §8's four**

In `SarkoGame/Data/Maps/bridge.json`, replace the whole `"playerSpawns"` array with:

```json
  "playerSpawns": [
    { "note": "ТЗ §8's four, verbatim. One is chosen per raid. All four sit behind the broken fence at the north-west corner, facing south-east into the sector, at least 400 uu from any prop and clear of every block — including Task 2's north and west world border, which starts at y = +19700 and x = -19700.",
      "id": "bridge_spawn_01", "pos": [-18000, 17500, 150], "yaw": -45 },
    { "id": "bridge_spawn_02", "pos": [-16500, 18500, 150], "yaw": -45 },
    { "id": "bridge_spawn_03", "pos": [-15000, 17000, 150], "yaw": -45 },
    { "id": "bridge_spawn_04", "pos": [-17500, 15000, 150], "yaw": -45 }
  ],
```

Then **move one prop out of spawn 03's way**: find the `car_wreck` at `[-15000, 17200, 75]` (yaw 45) in the `props` array and change it to `[-15400, 16700, 75]` with `"yaw": 30`. At yaw 30 a `car_wreck` (half-extents 230×95) covers x -15646..-15153 and y 16503..16897, so spawn 03 at (-15000, +17000) is outside it on both axes; where it was, the spawn stood inside the wreck. Add the note `"Moved 400 west and 500 south of where it was authored: ТЗ §8's spawn 03 at (-15000,+17000) stood inside this wreck's footprint. It is now L03's остов."` to that prop.

- [ ] **Step 5: Replace `containers` with the nineteen**

Replace the whole `"containers"` array with the following. Every row names its §29 ledger row, every shift from that row is stated, and the two rows that have no ledger row say so. `fixedItems` is **not** authored here — Task 5 does that against the final geometry, which is spec §6.5's own ordering requirement.

```json
  "containers": [
    { "note": "SPAWN CAMP (ТЗ §29 L01-L03) — the three junk containers. §29 has L13/L18/L25/L30 as junk too, but the owner's Bridge_West split is 3 junk, so junk is exactly the spawn camp: the first container the player ever opens teaches the interaction and holds nothing worth dying for.",
      "id": "bridge_loot_l01", "pos": [-17800, 17100, 35], "tier": "junk" },
    { "note": "L02 'дерево'. There is no tree kind and cannot be one (a canopy hides the player from a top-down camera), so this stands beside a treeline clump — see the flag list.",
      "id": "bridge_loot_l02", "pos": [-16400, 18100, 35], "tier": "junk" },
    { "note": "L03 'остов', shifted +100,+100 from §29's (-15100,+16600) so it stands 153 uu clear of the wreck's face rather than 53 — ТЗ §29 wants 120 uu free in front of a container.",
      "id": "bridge_loot_l03", "pos": [-15000, 16700, 35], "tier": "junk" },

    { "note": "THE PIPE CROSSING (L13, L18, L19). Re-tiered junk -> common per the owner's split. L19 (техдвор, south of the crossing) is where Task 5 puts the medkit: spec §6.5 teaches healing before the first bot, and the first bot is at the gas station.",
      "id": "bridge_loot_l13", "pos": [-14200, 3300, 35], "tier": "common" },
    { "id": "bridge_loot_l18", "pos": [-14800, -3800, 35], "tier": "common" },
    { "id": "bridge_loot_l19", "pos": [-13300, -4600, 35], "tier": "common" },

    { "note": "THE GAS STATION (L20-L25). L20 is the зал, inside the walkable bridge_gas_station: a container you have to walk into a building for is the whole point of the building.",
      "id": "bridge_loot_l20", "pos": [-13300, -8800, 35], "tier": "common" },
    { "note": "L21, подсобка — the west room, 285 uu clear of both interior walls.",
      "id": "bridge_loot_l21", "pos": [-14100, -9100, 35], "tier": "good" },
    { "note": "L22, shifted +200 in y from §29's (-13000,-9800), which is 50 uu OUTSIDE the building's south wall. It is now at the south end of the зал.",
      "id": "bridge_loot_l22", "pos": [-13000, -9600, 35], "tier": "good" },
    { "note": "L23, стойка, the sector's only med container. Shifted 50 west of §29's (-12500,-8400) so it stands 120 uu off the east wall's inner face rather than 70 — ТЗ §29 wants 80.",
      "id": "bridge_loot_l23", "pos": [-12550, -8400, 35], "tier": "med" },
    { "note": "L24, фургон — beside the trailer Task 4 authors at (-15000,-7900).",
      "id": "bridge_loot_l24", "pos": [-15000, -7600, 35], "tier": "good" },
    { "note": "L25, inside сарай S02 (Task 4). Re-tiered junk -> common.",
      "id": "bridge_loot_l25", "pos": [-15700, -10500, 35], "tier": "common" },

    { "note": "THE RAIL DEPOT (L26-L30) — the deep end of the route, and the only place with military. L26 is the разгрузка pad, north of the warehouse.",
      "id": "bridge_loot_l26", "pos": [-15800, -16600, 35], "tier": "good" },
    { "id": "bridge_loot_l27", "pos": [-11200, -17500, 35], "tier": "good" },
    { "note": "L28, inside the диспетчерская (Task 4).",
      "id": "bridge_loot_l28", "pos": [-12600, -16900, 35], "tier": "common" },
    { "note": "L29, паллеты, inside the warehouse's east room (Task 4). Re-tiered common -> good.",
      "id": "bridge_loot_l29", "pos": [-14500, -18100, 35], "tier": "good" },
    { "note": "L30, упор. Re-tiered junk -> common.",
      "id": "bridge_loot_l30", "pos": [-17200, -17400, 35], "tier": "common" },
    { "note": "MILITARY — no ledger row. ТЗ §29 puts no military west of the bridge (its four are the КПП and the промзона, all behind the closure), and the owner's Bridge_West split asks for two. They are here, at the deepest point of the route, and they retire when §29's four arrive with Stage D. This one is inside the warehouse's west room.",
      "id": "bridge_loot_rail_mil_01", "pos": [-15600, -17600, 35], "tier": "military" },
    { "note": "The second, beside the вагон at the depot's east end — the furthest point from E1, which is what makes carrying it home a decision.",
      "id": "bridge_loot_rail_mil_02", "pos": [-10700, -17500, 35], "tier": "military" }
  ],
```

- [ ] **Step 6: Replace `botSpawns` with the six**

```json
  "botSpawns": [
    { "note": "THE GAS STATION — two, per the owner's split. §11's АЗС парковка, verbatim.",
      "id": "bridge_bot_gas_forecourt", "pos": [-12500, -7500, 150], "zone": "mid" },
    { "note": "§11's АЗС двор (-15000,-10500) shifted +400,-200 — the ledger point is exactly on сарай S02's east wall centre line, and a bot spawned inside a wall is the oldest bug on this map. §11 allows +/-300-800.",
      "id": "bridge_bot_gas_yard", "pos": [-14600, -10700, 150], "zone": "deep" },

    { "note": "THE RAIL DEPOT — four, per the owner's split. These two are §11's ж/д запад and ж/д разгрузка, verbatim.",
      "id": "bridge_bot_rail_west", "pos": [-16000, -16500, 150], "zone": "deep" },
    { "id": "bridge_bot_rail_dock", "pos": [-11000, -17500, 150], "zone": "deep" },
    { "note": "The two the owner's count adds beyond §11's west entries. Placed at the depot's own sub-areas and spaced so no two of the six can hear one shot: the closest pair on this map is rail_west and rail_warehouse at 2524 uu, against an 1800 uu hearing radius.",
      "id": "bridge_bot_rail_warehouse", "pos": [-14600, -18600, 150], "zone": "deep" },
    { "id": "bridge_bot_rail_dispatcher", "pos": [-12400, -15600, 150], "zone": "deep" }
  ],
```

No bot at the pipe crossing, on purpose: ТЗ §6 wants one at its southern exit, but spec §6.5's teaching order heals the player *before* the first bot, so the first bot is the one on the gas station forecourt. The pipes bot returns with Stage D.

- [ ] **Step 7: Move the three extractions to ТЗ §12**

Replace the whole `"extractions"` array. The **ids do not change** — nothing in the code or the tests references them by name, and churning them costs a review for no gain.

```json
  "extractions": [
    { "note": "E1 (ТЗ §12), the pipes route, and the only extraction a Bridge_West player can reach. §12's trigger is ~1000x700; a 500 uu radius is the round equivalent. Task 2's north border leaves a 1200 uu mouth at exactly this x so E1 is the one gap in the treeline — the way out of the world.",
      "id": "bridge_extract_north_path", "pos": [-15500, 19500, 0], "radiusUU": 500, "name": "Северная тропа" },
    { "note": "E2 (ТЗ §12), the main exit after the bridge. Behind the closure until Stage D; kept in data because §12 lists three and because Sarko.Extract.BridgeExtractionsAreReachableAndDistinct checks they never overlap.",
      "id": "bridge_extract_north_highway", "pos": [-1000, 19500, 0], "radiusUU": 500, "name": "Шоссе на север" },
    { "note": "E3 (ТЗ §12), after the ford. Behind the closure until Stage D.",
      "id": "bridge_extract_east_cordon", "pos": [14500, 19500, 0], "radiusUU": 500, "name": "Восточный кордон" }
  ]
```

- [ ] **Step 8: Run the map tests**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko`

Expected: `ALL GREEN`, `B + 1` tests performed. Specifically:

- `Sarko.Map.BridgeWestLedgerIsAuthored` passes.
- `Sarko.Map.BridgeMapIsValid` still passes: its floors are ≥15 containers (19), ≥6 bots (6) and ≥40 props (unchanged).
- `Sarko.Map.BridgeRiskGradientExists` still passes: all six bots and eight good+military containers are south of the ravine, so `FarBots > NearBots` is 6 > 0 and `FarGoodLoot >= 4` is 8.
- `Sarko.Map.BridgeBuildingsAreEnterable` still passes: no container sits inside a wall. If `bridge_loot_l20`, `l21`, `l22` or `l23` is reported inside one, the shift arithmetic in Step 5 was mistyped — fix the container, not the building, and record the offset.
- `Sarko.Extract.BridgeExtractionsAreReachableAndDistinct` still passes: the three zones are ≥13500 uu apart and no spawn is inside one (the nearest, spawn 02, is 1414 uu from E1's centre against a 500 uu radius).
- `Sarko.Map.BridgeStaysInsideTheActorBudget` now reports a *lower* number than before — retiring 23 containers and 10 bots is −33 actors, taking the sector from 344 to **311**. **Copy the `AddInfo` line into the task report**; Task 3 needs it.

- [ ] **Step 9: Look at the sector**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`

**Read the PNG** (remember: east is up, north is right). Answer in the task report:

1. Is the loot visibly *clustered* along one west-side route now, instead of spread over the whole sector?
2. Is the north-east half of the frame visibly empty — no containers, no bots? (It should be. That emptiness is what Task 2 closes off.)
3. Does E1's pad sit at the north-west corner, away from the highway pad?

- [ ] **Step 10: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): Bridge_West's ledger — four spawns, nineteen containers, six bots, E1"
```

---

### Task 2: The closure and the world border — and first light for nine prop kinds

Second, because it is what makes the ledger above a *sector* instead of a corner of an open field: the player who wanders east now finds a treeline, a collapsed road and a jack-knifed bus rather than 250 m of unauthored ground, and the player who wanders west or south finds a forest edge rather than the floor's edge and a fall.

**Nine of Stage B's twenty prop kinds have never rendered.** Nothing places `rock`, `bush`, `log`, `fence_section`, `road_sign`, `concrete_barrier`, `trailer`, `pylon` or `treeline`, so their extents, offsets and colours have only ever been asserted, never seen. Step 2 places exactly one of each and photographs them **before** Tasks 4 and 6 author a hundred and forty. Fixing a kind after that is a hundred and forty coordinates.

**Why the barrier is blocks and not tiled props.** A `treeline` prop is 1200 uu long, so closing 17800 uu of flank twice takes ~32 of them. A **block** takes any extent, so the same closure is 6 blocks — and because the barrier blocks carry `"surface": "vegetation"`, they are the *same colour* as the treeline props, which then straddle them and break the straight edge. That is 26 actors saved against ТЗ §16's budget for an identical read, and it is the one structural saving this stage takes before Task 3 decides the rest.

**Every joint butts exactly.** The shipped map now forbids solid-block overlap anywhere (`Sarko.Map.BridgeBuildingsAreEnterable`), so the twelve barrier blocks below are computed to share face planes with each other and with the ravine's existing rim walls — never to overlap by even a unit. The table in Step 5 lists every span for exactly that reason.

**Where the lines run.** North of the ravine at **x = -6100**; south of it at **x = -9100**, west of the village's own ledger footprint (§9 puts the village at X -9000..+3500) so no barrier cuts through the two walkable houses Stage B authored. The step between the two runs is hidden inside the ravine, whose rim walls already close everything between x = -13600 and -1100. The world border runs at **x = -19850**, **y = -19850** and **y = +19850**, with a single 1200 uu mouth at E1.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (`blocks` +12, `props` +35, four props moved)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (two assertions in `Sarko.Map.NewPropKindsExist` invert)

**Interfaces:**
- Consumes: the twenty kinds in `SarkoMap::FindPropKind`, `SarkoMap::CountPropActors`, `ESarkoSurface::Vegetation`, `SarkoMap::IsPointInsideBlocksXY`.
- Produces: no C++ interface. Block ids `bridge_west_closure_{north,south}_{a,b,c}` and `bridge_west_border_*`, which Task 6's fill and Task 8's screenshots refer to by name.

- [ ] **Step 1: Write the failing enclosure test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestIsEnclosed,
	"Sarko.Map.BridgeWestIsEnclosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestIsEnclosed::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const FSarkoMapLayout Layout = SarkoMap::ToLayout(Map);
	const TArray<FSarkoCoverBlock> Solid = SolidOnly(Layout.Cover);

	// Walk each barrier's centre line at 100 uu — a quarter of the pawn's own
	// width — and require solid geometry at every sample. Sampling is the only
	// honest check here: "there are six blocks" says nothing about whether they
	// meet, and the failure mode of a barrier is a gap, not a missing piece.
	const auto RequireSolidAlongY = [this, &Solid](float X, float FromY, float ToY, const TCHAR* What)
	{
		int32 Holes = 0;
		float FirstHole = 0.f;
		for (float Y = FromY; Y <= ToY; Y += 100.f)
		{
			if (!SarkoMap::IsPointInsideBlocksXY(FVector2D(X, Y), Solid))
			{
				if (Holes == 0) { FirstHole = Y; }
				++Holes;
			}
		}
		TestEqual(FString::Printf(TEXT("%s is unbroken (first hole at y=%.0f of %d samples)"),
			What, FirstHole, Holes), Holes, 0);
	};
	const auto RequireSolidAlongX = [this, &Solid](float Y, float FromX, float ToX, const TCHAR* What)
	{
		int32 Holes = 0;
		float FirstHole = 0.f;
		for (float X = FromX; X <= ToX; X += 100.f)
		{
			if (!SarkoMap::IsPointInsideBlocksXY(FVector2D(X, Y), Solid))
			{
				if (Holes == 0) { FirstHole = X; }
				++Holes;
			}
		}
		TestEqual(FString::Printf(TEXT("%s is unbroken (first hole at x=%.0f of %d samples)"),
			What, FirstHole, Holes), Holes, 0);
	};

	// The east closure. Each run stops at the ravine, where the rim walls close
	// everything between x = -13600 and -1100 already — so the two runs plus the
	// rims are one continuous flank, and the step between them is invisible
	// because it happens inside a gorge nobody can walk along.
	RequireSolidAlongY(-6100.f, 2100.f, 20000.f, TEXT("the east closure, north of the ravine"));
	RequireSolidAlongY(-9100.f, -20000.f, -2100.f, TEXT("the east closure, south of the ravine"));

	// The world border. Without it the rail depot at y = -19000 is 1000 uu from
	// the floor's edge, and walking off a 400 m plane is a fall, a KillZ death and
	// a lost haul — the worst way to lose a raid, and not one the ТЗ ever asked
	// for. The ravine's mouth at the map's west edge gets its own piece: the
	// gorge is reachable through the pipes, and it runs straight off the world.
	RequireSolidAlongY(-19850.f, -19700.f, -2100.f, TEXT("the west border, south of the ravine"));
	RequireSolidAlongY(-19850.f, -1500.f, 1500.f, TEXT("the west border across the ravine mouth"));
	RequireSolidAlongY(-19850.f, 2100.f, 19700.f, TEXT("the west border, north of the ravine"));
	RequireSolidAlongX(-19850.f, -20000.f, -9300.f, TEXT("the south border"));

	// The north border has exactly one mouth, and it is E1. Both halves are
	// unbroken; the gap between them contains the extraction and nothing else.
	RequireSolidAlongX(19850.f, -20000.f, -16100.f, TEXT("the north border west of E1"));
	RequireSolidAlongX(19850.f, -14900.f, -6300.f, TEXT("the north border east of E1"));
	TestFalse(TEXT("E1's mouth is open"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-15500.f, 19850.f), Solid));

	// And the mouth is E1's, not a hole beside it: the open span is 1200 uu and
	// E1's 500 uu radius sits inside it.
	TestTrue(TEXT("the mouth is where the extraction is"),
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-16200.f, 19850.f), Solid) &&
		SarkoMap::IsPointInsideBlocksXY(FVector2D(-14800.f, 19850.f), Solid));
	return true;
}
```

- [ ] **Step 2: Place one of each new kind, and nothing else**

This step exists to be looked at. In `SarkoGame/Data/Maps/bridge.json`, append these nine entries to the **end** of the `props` array (comma after the current last entry). Note the two conventions: single-box kinds carry their half-height in `pos.z`, the two composites carry `0`.

```json
    { "note": "FIRST LIGHT FOR THE NEW KINDS. One of each of Stage B's nine, at the collapsed junction road where the east closure will cross it. Nothing places these kinds today, so their extents and colours have never been seen in a frame; they are photographed here before Tasks 4 and 6 author a hundred and forty. Each one stays as real content afterwards.", "kind": "treeline", "pos": [-6500, 16000, 500] },
    { "kind": "concrete_barrier", "pos": [-6500, 16700, 55], "yaw": 90 },
    { "note": "Composite: pos.z = 0, the parts carry the height.", "kind": "road_sign", "pos": [-6800, 16400, 0] },
    { "kind": "trailer", "pos": [-6900, 17300, 0], "yaw": 10 },
    { "kind": "rock", "pos": [-6500, 15600, 65] },
    { "kind": "bush", "pos": [-6350, 15300, 45] },
    { "kind": "log", "pos": [-6600, 15000, 55], "yaw": 100 },
    { "kind": "fence_section", "pos": [-6800, 14600, 92], "yaw": 90 },
    { "note": "ЛЭП pylon #3 of the three in the active north, on ТЗ §8's line (-11000,+19000) -> (+4000,+8500): at x = -6800 that line is at y = 16060. The other two are Task 6's.", "kind": "pylon", "pos": [-6800, 16060, 0] }
```

- [ ] **Step 3: Photograph them and read the frames**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -6650 15900 200`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`

**Read both PNGs and answer every one of these in the task report, in words:**

1. Does each of the nine actually appear? A kind whose mesh path is wrong logs and skips, and the frame is the only place that shows up.
2. Is the `bush` visibly *lower* than the `rock` and the `log`? It is 90 uu tall against their 130 and 110, and it is the one thing with no collision — if it reads as cover, someone will die behind it.
3. Does the `pylon` read as four boxes, with its crossarms clear of the pawn's head, or as a solid slab?
4. Does the `road_sign`'s plate hang over its post at that yaw, or has the composite offset rotated wrong?
5. Is the `treeline` unmistakably darker than the ground, and tall enough to cut sight completely?
6. Does the `trailer` sit **on** the floor — its body rides 30..250 uu, so a 30 uu gap under it is correct and a floating or buried body is not?

If any answer is wrong, fix `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp` **now** and re-shoot, and update the matching expectation in `Sarko.Map.NewPropKindsExist` / `Sarko.Map.PropKindScaleMatchesThePawn` in the same commit. Record the verdict for all nine either way — Task 6 authors eighty of them on the strength of this frame.

- [ ] **Step 4: Invert the two assertions that said the map places none of them**

`Sarko.Map.NewPropKindsExist` in `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` currently ends with a loop asserting `bridge.json does not yet place '<kind>'` and an equality asserting the map is one actor per authored prop. Both were correct for Stage B and are wrong from this step on. Replace that whole tail — from the comment beginning `// Adding kinds must not add entries:` to just before `return true;` — with:

```cpp
	// Stage C places all nine. The assertion inverts: the point of the kinds was
	// always that a sector would use them, and a kind nothing places is a kind
	// whose extents have never been seen in a frame (which is why Task 2 of the
	// Bridge_West plan photographs one of each before authoring the rest).
	FSarkoMapDefinition Bridge;
	FString LoadError;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Bridge, LoadError))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *LoadError));
		return false;
	}
	for (const FName& Kind : Required)
	{
		const bool bPlaced = Bridge.Props.ContainsByPredicate(
			[&Kind](const FSarkoMapProp& Prop) { return Prop.Kind == Kind; });
		TestTrue(FString::Printf(TEXT("bridge.json places '%s'"), *Kind.ToString()), bPlaced);
	}

	// Composites now cost more actors than they cost authored entries, which is
	// exactly what they were for. The relation that still has to hold is that
	// every prop resolves — a kind that did not would silently contribute zero
	// and make the budget number optimistic in the one case where the map is
	// broken. The absolute ceiling lives in
	// Sarko.Map.PropActorCountIsWithinTheMobileBudget.
	int32 ExpectedActors = 0;
	for (const FSarkoMapProp& Prop : Bridge.Props)
	{
		FSarkoPropKind Resolved;
		TestTrue(FString::Printf(TEXT("placed kind '%s' resolves"), *Prop.Kind.ToString()),
			SarkoMap::FindPropKind(Prop.Kind, Resolved));
		ExpectedActors += Resolved.Parts.Num();
	}
	TestEqual(TEXT("the actor count is the sum of every placed kind's parts"),
		SarkoMap::CountPropActors(Bridge), ExpectedActors);
	TestTrue(TEXT("composites are actually in use, so the sum exceeds the entry count"),
		SarkoMap::CountPropActors(Bridge) > Bridge.Props.Num());
```

Then, in `Sarko.Map.PropActorCountIsWithinTheMobileBudget` in the same file, delete the line

```cpp
	TestEqual(TEXT("the shipped map is one actor per authored prop"), PropActors, Map.Props.Num());
```

and its two-line comment, replacing them with:

```cpp
	// Was an equality until Stage C: the shipped map used no composite kind, so
	// actors and authored entries were the same number. Bridge_West places
	// pylons, road signs and trailers, so the relation is now strictly greater —
	// asserted, because a *fall back* to equality would mean a composite had
	// silently collapsed to one box and the pylon had lost its crossarms.
	TestTrue(FString::Printf(TEXT("composite kinds are in use (%d actors from %d props)"),
		PropActors, Map.Props.Num()), PropActors > Map.Props.Num());
```

- [ ] **Step 5: Author the twelve barrier blocks**

Add these to the **end** of the `blocks` array. Every span is stated because every joint has to butt and nothing may overlap; the rim walls they meet are the existing eight, at y = ±(1500..2100).

| id | pos | extent | covers | butts against |
|---|---|---|---|---|
| `bridge_west_closure_north_a` | (-6100, 5075) | (200, 2975, 500) | y 2100..8050 | the north rim wall's face at y=2100 |
| `bridge_west_closure_north_b` | (-6100, 11000) | (200, 2950, 500) | y 8050..13950 | `_a` at 8050 |
| `bridge_west_closure_north_c` | (-6100, 16975) | (200, 3025, 500) | y 13950..20000 | `_b` at 13950 |
| `bridge_west_closure_south_a` | (-9100, -5075) | (200, 2975, 500) | y -8050..-2100 | the south rim wall's face at y=-2100 |
| `bridge_west_closure_south_b` | (-9100, -11000) | (200, 2950, 500) | y -13950..-8050 | `_a` at -8050 |
| `bridge_west_closure_south_c` | (-9100, -16975) | (200, 3025, 500) | y -20000..-13950 | `_b` at -13950 |
| `bridge_west_border_west_south` | (-19850, -10900) | (150, 8800, 500) | y -19700..-2100 | the rim wall at -2100, the south border at -19700 |
| `bridge_west_border_west_ravine` | (-19850, 0) | (150, 1500, 500) | y -1500..1500 | both rim walls, at ±1500 |
| `bridge_west_border_west_north` | (-19850, 10900) | (150, 8800, 500) | y 2100..19700 | the rim wall at 2100, the north border at 19700 |
| `bridge_west_border_south` | (-14650, -19850) | (5350, 150, 500) | x -20000..-9300 | the west border at -19700 (y), `closure_south_c` at x=-9300 |
| `bridge_west_border_north_west` | (-18050, 19850) | (1950, 150, 500) | x -20000..-16100 | the west border at y=19700 |
| `bridge_west_border_north_east` | (-10600, 19850) | (4300, 150, 500) | x -14900..-6300 | `closure_north_c` at x=-6300 |

```json
    { "note": "THE EAST CLOSURE, north of the ravine. Bridge_West is the west third; the rest of the sector is physically closed and opens in Stage D by deleting these blocks and their dressing. A BLOCK rather than tiled treeline props because a block takes any extent and a prop does not: this is 3 actors where 15 props would be, and it carries surface 'vegetation' so it is the same colour as the treeline props that straddle it and break its straight edge. 1000 uu tall (extent.z 500), which is 5.7x the pawn — it cuts sight completely.",
      "id": "bridge_west_closure_north_a", "pos": [-6100, 5075, 500], "extent": [200, 2975, 500], "surface": "vegetation" },
    { "id": "bridge_west_closure_north_b", "pos": [-6100, 11000, 500], "extent": [200, 2950, 500], "surface": "vegetation" },
    { "id": "bridge_west_closure_north_c", "pos": [-6100, 16975, 500], "extent": [200, 3025, 500], "surface": "vegetation" },

    { "note": "THE EAST CLOSURE, south of the ravine, 3000 uu further west so it clears the village's own ledger footprint (ТЗ §9 puts the village at X -9000..+3500) and never cuts through the two walkable houses Stage B authored. The step between the two runs happens inside the ravine, where the rim walls already close x -13600..-1100 — so it is a step nobody can stand at.",
      "id": "bridge_west_closure_south_a", "pos": [-9100, -5075, 500], "extent": [200, 2975, 500], "surface": "vegetation" },
    { "id": "bridge_west_closure_south_b", "pos": [-9100, -11000, 500], "extent": [200, 2950, 500], "surface": "vegetation" },
    { "id": "bridge_west_closure_south_c", "pos": [-9100, -16975, 500], "extent": [200, 3025, 500], "surface": "vegetation" },

    { "note": "THE WORLD BORDER, west edge. Not decoration: the floor is 40000x40000 and ends, the rail depot sits 1000 uu from its southern edge, and walking off a 400 m plane is a KillZ death with the haul in the backpack. Three pieces because the ravine's rim walls reach the map's west edge and nothing may overlap them — the middle piece closes the gorge's mouth, which IS reachable through the pipe crossing.",
      "id": "bridge_west_border_west_south", "pos": [-19850, -10900, 500], "extent": [150, 8800, 500], "surface": "vegetation" },
    { "id": "bridge_west_border_west_ravine", "pos": [-19850, 0, 500], "extent": [150, 1500, 500], "surface": "vegetation" },
    { "id": "bridge_west_border_west_north", "pos": [-19850, 10900, 500], "extent": [150, 8800, 500], "surface": "vegetation" },

    { "note": "THE WORLD BORDER, south edge. Stops at x = -9300, which is closure_south_c's west face: east of that the border is the closure's job and a second block there would overlap it.",
      "id": "bridge_west_border_south", "pos": [-14650, -19850, 500], "extent": [5350, 150, 500], "surface": "vegetation" },

    { "note": "THE WORLD BORDER, north edge, in two pieces with a 1200 uu mouth between them at x -16100..-14900. That mouth is E1: the only gap in the treeline is the way out of the world, which is exactly what an extraction should look like from above.",
      "id": "bridge_west_border_north_west", "pos": [-18050, 19850, 500], "extent": [1950, 150, 500], "surface": "vegetation" },
    { "id": "bridge_west_border_north_east", "pos": [-10600, 19850, 500], "extent": [4300, 150, 500], "surface": "vegetation" }
```

- [ ] **Step 6: Move the four props the barrier now runs through**

The bus stop was authored across what is now the closure line, and one freight car crosses the southern run. Prop-versus-block overlap is not tested, so nothing fails — it just looks like a bus embedded in a forest. Edit these four entries in `props` in place:

| kind, current pos | new pos | why |
|---|---|---|
| `bus` (-6200, 16400) | **(-6900, 16400)** | its body spans 1200 uu; at -6900 it ends exactly at the closure's west face and reads as a bus parked against the treeline, which is also the closure's story at the road |
| `wall` (-6200, 17000) | **(-6900, 17000)** | same reason; keeps the bus stop's shelter beside the bus |
| `crate` (-6000, 16750) | **(-6700, 16750)** | it was 100 uu inside the barrier |
| `freight_car` (-9200, -16400) | **(-10000, -16400)** | at yaw 25 its footprint reaches x = -8560, straight through the southern run |

Leave the bus stop's other props — the wall at (-5300, 16400) and the sandbag at (-5600, 17200) — exactly where they are. They are beyond the closure now, and a stop you can see across the trees but not reach is the read this whole task is for.

- [ ] **Step 7: Dress the closure so it reads as world**

Append these 26 props (the nine from Step 2 stay). `treeline` half-extent is 600×200, so a 2200 uu pitch with varied x and yaw breaks the block's straight edge without pretending to tile it.

```json
    { "note": "TREELINE, north run — the silhouette. The barrier block does the blocking; these break its edge and give the player something with depth to read at eye level. Varied x by +/-100 and yaw by a few degrees on purpose: a line of identical boxes reads as a fence.",
      "kind": "treeline", "pos": [-6400, 2600, 500], "yaw": 4 },
    { "kind": "treeline", "pos": [-6550, 4800, 500], "yaw": -3 },
    { "kind": "treeline", "pos": [-6400, 7000, 500], "yaw": 2 },
    { "kind": "treeline", "pos": [-6600, 9200, 500], "yaw": -5 },
    { "kind": "treeline", "pos": [-6400, 11400, 500], "yaw": 3 },
    { "kind": "treeline", "pos": [-6550, 13600, 500], "yaw": -2 },
    { "kind": "treeline", "pos": [-6400, 18200, 500], "yaw": 5 },
    { "kind": "treeline", "pos": [-6600, 19400, 500], "yaw": -4 },

    { "note": "TREELINE, south run.",
      "kind": "treeline", "pos": [-9400, -2700, 500], "yaw": -3 },
    { "kind": "treeline", "pos": [-9550, -5200, 500], "yaw": 4 },
    { "kind": "treeline", "pos": [-9400, -7800, 500], "yaw": -2 },
    { "kind": "treeline", "pos": [-9600, -10600, 500], "yaw": 3 },
    { "kind": "treeline", "pos": [-9400, -13400, 500], "yaw": -5 },
    { "kind": "treeline", "pos": [-9550, -16200, 500], "yaw": 2 },
    { "kind": "treeline", "pos": [-9400, -18800, 500], "yaw": -3 },

    { "note": "THE COLLAPSED JUNCTION ROAD (north) — the closure needs a cause, not just a wall. Concrete blocks across the carriageway, a wreck shoved off it, and a sign facing the player who walks up.",
      "kind": "concrete_barrier", "pos": [-6500, 17000, 55], "yaw": 90 },
    { "kind": "concrete_barrier", "pos": [-6500, 16100, 55], "yaw": 90 },
    { "kind": "car_wreck", "pos": [-6700, 16900, 75], "yaw": 80 },

    { "note": "THE COLLAPSED SOUTH ROAD — the same story on the southern run, where the village lane used to come through.",
      "kind": "concrete_barrier", "pos": [-9400, -11000, 55], "yaw": 90 },
    { "kind": "concrete_barrier", "pos": [-9400, -11400, 55], "yaw": 90 },
    { "kind": "car_wreck", "pos": [-9600, -11200, 75], "yaw": 15 },
    { "kind": "road_sign", "pos": [-9500, -10700, 0], "yaw": 90 },

    { "note": "SCATTER along both runs, so the treeline has an understorey rather than a hem.",
      "kind": "rock", "pos": [-6700, 12000, 65] },
    { "kind": "rock", "pos": [-9500, -6000, 65] },
    { "kind": "bush", "pos": [-6800, 8000, 45] },
    { "kind": "bush", "pos": [-9600, -15000, 45] }
```

- [ ] **Step 8: Run the tests**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, `B + 2` tests performed. Watch for three specific failures:

- `Sarko.Map.BridgeWestIsEnclosed` reporting a hole. Read the `first hole at y=…` value: it names the joint that does not butt. Fix the two spans so they share a face plane exactly.
- `Sarko.Map.BridgeBuildingsAreEnterable` reporting that two solid blocks intersect. That is the same arithmetic from the other side: one of the twelve overlaps a neighbour or a rim wall by a unit or two. **Do not "fix" it by moving a rim wall** — the ravine's spine is the map's oldest invariant.
- `Sarko.Map.BridgeStaysInsideTheActorBudget`. The sector goes from 311 to about **364** (+12 blocks, +35 props, +6 actors from the composites). Still under the 450 ceiling. **Copy the `AddInfo` line into the task report.**

- [ ] **Step 9: Verify it reads as world, not as a wall**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -7200 15800 200`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -9800 -11000 200`

**Read all three PNGs.** Answer in the task report:

1. From the overview (east up, north right): does the west third read as a bounded place with a forest along its eastern flank, or does it read as a rectangle drawn on a field? A hard straight dark-green edge for 18000 uu is the failure; if you see one, add two or three more treeline props at the offending stretch and re-shoot.
2. Are the bridge and the water tower still visible beyond the closure? They are the landmarks the player navigates by, and losing them to the treeline would be a real loss.
3. From the player's eye at the junction road: is it obvious *why* you cannot continue — barriers, a wreck, a sign, a bus — or does it just stop?
4. From the player's eye on the southern run: same question, and is the treeline's height enough that you cannot see the village over it?

- [ ] **Step 10: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp && git commit -m "feat(map): the east closes as world — treeline, collapsed roads, and a border you cannot fall off"
```

---

### Task 3: The actor-budget decision, made once and written down

Third, and before any content that spends the budget. The sector was at 344 actors when this plan was written; the test ceilings are **400 prop actors** (`Sarko.Map.PropActorCountIsWithinTheMobileBudget`) and **450 total** (`Sarko.Map.BridgeStaysInsideTheActorBudget`). Task 6's §15 fill alone projects past the total. Stage B's own comment says a failure there "is not a bug, it is that decision coming due", so this task is where it comes due — with a measurement, an explicit choice, and the trigger that would reverse it.

**The projection, computed from the content this plan authors** (so nothing is guessed later):

| after | floor | blocks | building walls | prop actors | containers | pads | bots | pawn | lights | total |
|---|---|---|---|---|---|---|---|---|---|---|
| Task 1 | 1 | 20 | 24 | 235 | 19 | 3 | 6 | 1 | 2 | **311** |
| Task 2 | 1 | 32 | 24 | 276 | 19 | 3 | 6 | 1 | 2 | **364** |
| Task 4 | 1 | 40 | 58 | 302 | 19 | 3 | 6 | 1 | 2 | **432** |
| Task 6 | 1 | 40 | 66 | 388 | 19 | 3 | 6 | 1 | 2 | **526** |
| Task 7 | 1 | 43 | 66 | 388 | 19 | 3 | 6 | 1 | 2 | **529** |

Every total here is **±10**: it was computed from the authored rows below rather than measured, and a container nudged 400 uu or a clump given one more rock moves it. The ceiling is chosen to absorb that, and Step 2 says what to do when reality and the table disagree by more than ten.

**The decision: raise the ceilings to 560 total and 420 prop actors. Do not introduce instancing this stage.** Four reasons, in the order they matter:

1. **What costs money here is draw calls, and instancing as such does not fix them.** `SpawnMeshBox` → `PaintFlat` gives **every actor its own `UMaterialInstanceDynamic`**, and UE's mesh-draw pipeline only auto-instances primitives that share a mesh *and* a material. So 529 actors are ~529 draws today, and would still be ~529 draws after moving them into `UHierarchicalInstancedStaticMeshComponent`s *unless* the per-actor MIDs go away first. The cheap, low-risk mitigation is therefore **one shared material instance per `ESarkoSurface`** — eleven MIDs instead of five hundred, after which identical cube+surface pairs batch on their own. That is a ~20-line change in `PaintFlat` plus a cache, it changes rendering for every actor on the map, and it must be made with a before/after frame-time measurement in front of it. It is **not** this stage's work: Stage C authors content, and rewriting the one spawn path in a content stage is how a content bug becomes a rendering bug.
2. **529 trivial static cubes is not where mobile falls over.** ТЗ §16 asks for simple geometry, few materials, static vehicles, no physics debris and no expensive particles — all of which hold. Nothing added by this plan ticks, allocates per frame, or replicates.
3. **The saving that was available has already been taken.** Task 2's closure is 6 blocks instead of ~32 tiled props (−26 actors), and Task 1 retired 33 actors of population that the closure put out of reach. Instancing would be the *second* optimisation, applied before the first one had been measured.
4. **The ceiling stays a ceiling.** 560 leaves ~30 actors of headroom over the projection, which is enough for a container that needs a crate beside it and not enough to hide a fill that doubled.

**The trigger that reverses this**, written into the test comment so it cannot be lost: *a packaged iOS build that misses 30 fps in the sector*. The order of response is then (a) shared per-surface material instances, measured; (b) `HISM` per (mesh, surface) pair for the fill kinds only — rock, bush, log, fence_section, treeline, which are ~150 of the 388; (c) cutting content. Never (c) first.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (`Sarko.Map.BridgeStaysInsideTheActorBudget`, ceiling and comment)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (`Sarko.Map.PropActorCountIsWithinTheMobileBudget`, ceiling and comment)

**Interfaces:**
- Consumes: `SarkoMap::CountPropActors`, `SarkoMap::ToLayout`.
- Produces: no interface. Two numbers and a documented trigger.

- [ ] **Step 1: Measure the frame you actually have**

`-csvCaptureFrames` must not be used: it repoints `FPaths::ProjectSavedDir()`, which makes the run play as a different player and swallows screenshot commands. Use the exec form:

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && rm -rf Saved/Profiling && "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" SarkoGame.uproject "/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" -game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=1600 -ExecCmds="CsvProfile Start, t.MaxFPS 0, SarkoOverview" -nopause > /dev/null 2>&1 & sleep 90; pkill -f "UnrealEditor.*SarkoGame" || true; find Saved/Profiling -name "*.csv" | head -3
```

Then read the CSV's `FrameTime`, `RenderThreadTime` and `GPUTime` columns (mean of the last 100 rows) and record all three in the task report, together with the actor count from Step 2.

**If no CSV appears**, say so in the report and move on — the decision above does not depend on the measurement, it is *informed* by it. Do not invent a number, and do not let a missing CSV become a reason to skip the report line. A Mac frame time is in any case only a smoke test: the ТЗ's budget is about iOS, and nothing in this repo can build for a device unattended.

- [ ] **Step 2: Confirm the arithmetic against the real file**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map.BridgeStaysInsideTheActorBudget`

Expected: PASS, with an `AddInfo` line reporting about **364** actors (the Task 2 row of the table). If the number differs by more than 10 from the table, the table is stale — **update the table in this plan document as part of this task's commit** rather than proceeding against numbers nobody believes.

- [ ] **Step 3: Raise the total ceiling, with the reason in the file**

In `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`, replace the comment and assertion at the end of `FSarkoBridgeStaysInsideTheActorBudget::RunTest` with:

```cpp
	// 560 is Bridge_West's ceiling, and it is a decision rather than a drift.
	//
	// The sector projects to ~529 actors: 1 floor, 43 blocks (12 of them the east
	// closure and the world border), 66 walls from 11 buildings, 388 prop actors
	// from 376 authored props (the difference is the composite kinds — pylons,
	// road signs, trailers), 19 containers, 3 extraction pads, 6 bots, the pawn
	// and two lights. 560 leaves about thirty of headroom: enough for a crate
	// beside a container, not enough to hide a doubled fill.
	//
	// Instancing was considered here and deliberately NOT taken. What costs money
	// is draw calls, and SpawnMeshBox -> PaintFlat gives every actor its own
	// UMaterialInstanceDynamic — UE only auto-instances primitives that share a
	// mesh AND a material, so moving these into HISM components would not reduce
	// the draw count until the per-actor MIDs go away first. The cheap mitigation
	// is therefore one shared material instance per ESarkoSurface (eleven instead
	// of five hundred), which is a change to the single spawn path and belongs
	// with its own before/after measurement, not inside a content stage.
	//
	// The trigger that reopens this: a PACKAGED iOS build that misses 30 fps in
	// this sector. Response order is (1) shared per-surface material instances,
	// measured; (2) HISM per (mesh, surface) pair for the fill kinds only — rock,
	// bush, log, fence_section, treeline, about 150 of the 388; (3) cutting
	// content. Never (3) first.
	//
	// A failure here still means what it always meant: not a bug, but this
	// decision coming due again.
	TestTrue(FString::Printf(TEXT("the sector spawns at most 560 actors (it spawns %d)"), Actors), Actors <= 560);
```

- [ ] **Step 4: Raise the prop ceiling the same way**

In `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`, in `FSarkoPropActorCountIsWithinTheMobileBudget::RunTest`, replace

```cpp
	TestTrue(FString::Printf(TEXT("props stay inside the mobile actor budget (%d)"), PropActors),
		PropActors <= 400);
```

with

```cpp
	// 420 for Bridge_West: the sector projects to 388 prop actors from 376
	// authored props once ТЗ §15's fill of the north is in (about 150 of them are
	// rocks, bushes, logs, fences and treeline). Raised from 400 in the same
	// commit that raised the total ceiling to 560, and for the same documented
	// reasons — see Sarko.Map.BridgeStaysInsideTheActorBudget, which carries the
	// argument and the trigger that would reverse it.
	TestTrue(FString::Printf(TEXT("props stay inside the mobile actor budget (%d)"), PropActors),
		PropActors <= 420);
```

- [ ] **Step 5: Run the whole suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, still `B + 2` tests performed — this task adds no test, it changes two numbers and writes down why.

- [ ] **Step 6: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp docs/superpowers/plans/2026-08-03-sarko-bridge-west-content.md && git commit -m "test(map): Bridge_West's actor ceilings are 560/420, with the instancing trigger written down"
```

(The plan document is staged only if Step 2 required updating its table. If it did not, drop that path from the `git add`.)

---

### Task 4: The three camps — pipes, gas station, rail depot

Fourth: the ten buildings the owner's Bridge_West list names, the pumps that have been standing 3500 uu from their shop since the shop moved to its §28 ledger coordinate, and the props that make each camp a place rather than a cluster of grey boxes.

**Six new buildings**, all authored against the expander's rules — footprint minus twice the wall thickness ≥ 500 uu on each axis, walls 10..200 uu thick, height 200..800, **zero or two-plus doorways** (one is an error), each doorway ≥250 uu (300–350 preferred) and leaving ≥120 uu of wall at each end of its wall, interior walls axis-aligned with ≥250 uu of clear passage, and no sealed pocket. North and south walls span the full footprint width; **east and west walls are shortened by one thickness at each end**, so a door offset on an E/W wall is bounded by `±(SizeY/2 − T − 120)`. Every door offset below was chosen inside that bound; the numbers are in the table so a reviewer can check them without running anything.

| id | pos | size | surface | doors | rooms |
|---|---|---|---|---|---|
| `bridge_pipes_shed` | (-14900, -4600) | 1200×900 | rust | N @ -300 w300, E @ 0 w300 | one |
| `bridge_gas_storeroom` | (-15500, -10500) | 1000×800 | timber | N @ 0 w300, E @ 0 w300 | one (§28 S02) |
| `bridge_gas_wc` | (-14300, -7600) | 700×600 | structure | none (closed) | — |
| `bridge_rail_dispatcher` | (-12500, -17000) | 1100×800 | structure | N @ 0 w300, W @ 0 w300 | one (§28 S03) |
| `bridge_rail_warehouse` | (-15200, -17600) | 2400×1600 | rust | N @ -600 w340, E @ 300 w320 | two, divider at local x=400 |
| `bridge_rail_booth` | (-17000, -16000) | 700×600 | timber | none (closed) | — |

Two of them are closed on purpose: ТЗ §28 ships four "закрытых" buildings, a closed building is explicitly legal (zero doors), and the expander skips the reachability fill for one because there is nothing to reach. They are silhouette and cover, not content — nothing is authored inside them.

**Where this diverges from the ТЗ, and why:**

- `bridge_pipes_shed`, `bridge_gas_wc`, `bridge_rail_warehouse` and `bridge_rail_booth` **have no §28 ledger row.** §28's 24 buildings put nothing at the pipe crossing and give the ж/д тупик only a диспетчерская and a будка. The owner's Bridge_West list asks for a pipes tech shed, a gas-station wc, and a rail warehouse and shed, so they exist and are named descriptively. **They need ledger rows when the full map is authored** — flagged here, in the task report, and in the Self-Review, because four unlisted buildings is exactly the sort of thing that quietly becomes 28 buildings in a 24-building ledger.
- The **навес** is a pad and four pillars, not a canopy (flag 1 in the flag list): a roof would hide the player from the camera, which ТЗ §13 forbids in the same breath as it asks for the навес.
- The **вывеска** is a `pipe` mast with a `road_sign` at its foot (flag 2): the sign kind is signage-scale, and a landmark-scale plate needs a composite kind this stage does not add.
- The **цистерна** is the upright `pipe` already standing at (-17400, -15600) (flag 5): parts have no roll, so a horizontal tank is not expressible.

**Five containers are already at their ledger coordinates in the open**, authored in Task 1: L25 (-15700,-10500) lands in the storeroom, L28 (-12600,-16900) in the dispatcher, L29 (-14500,-18100) and `rail_mil_01` (-15600,-17600) in the warehouse's two rooms. The door offsets and the warehouse divider above were chosen so that **no wall lands on any of them** — the closest is L29, 315 uu east of the divider's face. `Sarko.Map.BridgeBuildingsAreEnterable` is the check; if it reports one inside a wall, move the **container** by ≤400 uu (ТЗ §29) and record the offset. Never move a wall to free a container: the wall is the room.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (`buildings` +6, `blocks` +8, `props`: 26 added, 3 pumps moved, 11 forecourt props re-authored)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test)

**Interfaces:**
- Consumes: `SarkoMap::ExpandBuildings`, `SarkoMap::MinDoorwayUU`, `SarkoMap::MinInteriorPassageUU`, `ESarkoSurface::{Rust,Timber,Structure,Concrete}`.
- Produces: the six building ids above. Task 5 authors `fixedItems` into the containers they enclose; Task 6 reuses `bridge_north_shed_*` as the same pattern.

- [ ] **Step 1: Write the failing camps test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestCampsAreAuthored,
	"Sarko.Map.BridgeWestCampsAreAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestCampsAreAuthored::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The owner's Bridge_West list, by id. A camp is not "some buildings near a
	// place" — each of these is a named room the route runs through, and a
	// missing one is a container standing in a field.
	const TArray<FString> Expected = {
		TEXT("bridge_pipes_shed"),
		TEXT("bridge_gas_station"),      // Stage B authored this one
		TEXT("bridge_gas_storeroom"),
		TEXT("bridge_gas_wc"),
		TEXT("bridge_rail_dispatcher"),
		TEXT("bridge_rail_warehouse"),
		TEXT("bridge_rail_booth")
	};
	for (const FString& Id : Expected)
	{
		const bool bFound = Map.Buildings.ContainsByPredicate(
			[&Id](const FSarkoBuilding& B) { return B.Id == Id; });
		TestTrue(FString::Printf(TEXT("building '%s' is authored"), *Id), bFound);
	}

	// Every walkable building on the ACTIVE side must hold something or lead
	// somewhere: the ones with doors are the ones the route enters, and a walkable
	// room with no loot and no through-route is 6 wall actors of nothing. Checked
	// as "at least one container inside, or at least two doorways" — the second
	// clause is what lets the pipes shed be a shortcut rather than a store.
	for (const FSarkoBuilding& Building : Map.Buildings)
	{
		if (Building.Doors.Num() == 0)
		{
			continue; // closed buildings are silhouette, by design
		}
		if (Building.Location.X > -6100.f)
		{
			continue; // behind the closure: Stage D's problem
		}
		TestTrue(FString::Printf(TEXT("walkable building '%s' has two exits"), *Building.Id),
			Building.Doors.Num() >= 2);
	}

	// The pump row stands on its own forecourt. It sat 3500 uu north of the shop
	// from the moment the shop moved to its ТЗ §28 ledger coordinate, which read
	// as a fuel station with no fuel and a shed with no reason to exist.
	int32 PumpsNearTheShop = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		if (Prop.Kind == TEXT("fuel_pump"))
		{
			const float Distance = FVector2D(
				Prop.Location.X - (-13500.f), Prop.Location.Y - (-9000.f)).Size();
			TestTrue(FString::Printf(TEXT("a fuel pump is on the shop's forecourt (%.0f uu away)"), Distance),
				Distance <= 2600.f);
			++PumpsNearTheShop;
		}
	}
	TestTrue(TEXT("ТЗ §9 wants 3-4 pumps"), PumpsNearTheShop >= 3 && PumpsNearTheShop <= 4);

	// ТЗ §9's ж/д путь: the siding finally has rails, and they are paint — flat,
	// non-colliding, rust-coloured. A colliding rail is a kerb across the depot.
	int32 Rails = 0;
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		if (Block.Id.StartsWith(TEXT("bridge_rail_track")))
		{
			++Rails;
			TestFalse(FString::Printf(TEXT("rail '%s' does not block movement"), *Block.Id),
				Block.bBlocksMovement);
			TestTrue(FString::Printf(TEXT("rail '%s' is flat"), *Block.Id), Block.Extent.Z <= 30.f);
		}
	}
	TestEqual(TEXT("the siding has two rails"), Rails, 2);
	return true;
}
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map.BridgeWestCampsAreAuthored`

Expected: FAIL on `building 'bridge_pipes_shed' is authored`, on the fuel-pump distance (they are ~3900 uu away), and on `the siding has two rails` (there are none).

- [ ] **Step 3: Author the six buildings**

Add to the `buildings` array in `SarkoGame/Data/Maps/bridge.json`, after `bridge_gas_station` and before `bridge_village_d1`:

```json
    { "note": "THE PIPE CROSSING's техдвор shed (ТЗ §6 'техдвор на юге'). No §28 ledger row — §28 puts no building at the crossing, and the owner's Bridge_West list asks for a tech shed; it needs a row when the full map is authored. Two doorways because the shed is a shortcut off the crossing's south mouth, and a bot in the only doorway is a coffin (ТЗ §13). Interior 1140x840. Rust, because it belongs to the industrial vocabulary and reads apart from the timber sheds further south.",
      "id": "bridge_pipes_shed", "pos": [-14900, -4600, 0], "size": [1200, 900], "surface": "rust",
      "doors": [ { "side": "N", "offset": -300, "width": 300 }, { "side": "E", "offset": 0, "width": 300 } ] },
    { "note": "S02 сарай АЗС (ТЗ §28) at its ledger coordinate, walkable because §29's L25 is inside it. Interior 940x740; the container at (-15700,-10500) stands 270 uu off the west wall's inner face. Timber: this is the warm village tone, and the АЗС's own shop is structure grey — two materials, two readings, one camp.",
      "id": "bridge_gas_storeroom", "pos": [-15500, -10500, 0], "size": [1000, 800], "surface": "timber",
      "doors": [ { "side": "N", "offset": 0, "width": 300 }, { "side": "E", "offset": 0, "width": 300 } ] },
    { "note": "The АЗС's wc — CLOSED (zero doors), which ТЗ §28 has four of and the expander explicitly allows. It is silhouette and cover on the forecourt's north edge, nothing more; no container is authored inside, and the reachability fill is skipped because there is nothing to reach. Interior 640x540, which is the smallest footprint the expander accepts with 30 uu walls.",
      "id": "bridge_gas_wc", "pos": [-14300, -7600, 0], "size": [700, 600], "surface": "structure",
      "doors": [] },
    { "note": "S03 диспетчерская (ТЗ §28/§9) at its ledger coordinate, walkable, holding §29's L28. North opens onto the siding, west onto the warehouse yard — the two sides people arrive from and leave by. Interior 1040x740.",
      "id": "bridge_rail_dispatcher", "pos": [-12500, -17000, 0], "size": [1100, 800], "surface": "structure",
      "doors": [ { "side": "N", "offset": 0, "width": 300 }, { "side": "W", "offset": 0, "width": 300 } ] },
    { "note": "THE RAIL WAREHOUSE — no §28 ledger row (the owner's list asks for one; §9's ж/д has only the диспетчерская and a будка). The deepest room on the route and the only two-room building west of the village: the divider at local x=400 leaves a big west room and a narrow east one, and the military container sits in the west while the паллеты sit in the east, so the richest loot is the furthest from both doorways. Interior 2340x1540; the divider leaves 755 uu of clear passage to the east wall, well over ТЗ §13's 250.",
      "id": "bridge_rail_warehouse", "pos": [-15200, -17600, 0], "size": [2400, 1600], "surface": "rust",
      "doors": [ { "side": "N", "offset": -600, "width": 340 }, { "side": "E", "offset": 300, "width": 320 } ],
      "interiorWalls": [ { "from": [400, -770], "to": [400, 770], "door": { "offset": 0, "width": 300 } } ] },
    { "note": "The будка at the depot's west end (ТЗ §9). CLOSED, like the wc: it breaks the skyline at the end of the siding and gives the bot at (-16000,-16500) something to stand behind.",
      "id": "bridge_rail_booth", "pos": [-17000, -16000, 0], "size": [700, 600], "surface": "timber",
      "doors": [] },
```

- [ ] **Step 4: Author the eight flat blocks — pads and rails**

Add to the end of the `blocks` array. Concrete pads are flat and non-colliding, which is the only rule that binds them; the rails are `rust` so the siding reads as a railway from above for the first time.

```json
    { "note": "THE PIPE CROSSING — concrete bases under the two pipe rows (ТЗ §6 'бетонные основания'). Flat paint, not kerbs: a colliding base at the mouth of a 420 uu walkway would make the crossing a squeeze.",
      "id": "bridge_pipes_base_north", "pos": [-14000, 1900, 2], "extent": [500, 300, 2], "surface": "concrete", "blocksMovement": false },
    { "id": "bridge_pipes_base_south", "pos": [-14000, -1900, 2], "extent": [500, 300, 2], "surface": "concrete", "blocksMovement": false },
    { "note": "The техдвор's yard, so the shed reads as a working yard rather than a hut in grass.",
      "id": "bridge_pipes_yard", "pos": [-13900, -4600, 2], "extent": [700, 450, 2], "surface": "concrete", "blocksMovement": false },
    { "note": "THE АЗС FORECOURT — this pad IS the навес. A canopy roof cannot exist here: the camera is above and ТЗ §13 says the roof must not hide the player, so the shelter is expressed as its own footprint plus the four pillars in props. Held east of x = -12900 so it does not run under the shop's east wall.",
      "id": "bridge_gas_canopy_pad", "pos": [-11400, -9000, 2], "extent": [900, 900, 2], "surface": "concrete", "blocksMovement": false },
    { "note": "THE RAIL DEPOT — the разгрузка pad L26 stands on.",
      "id": "bridge_rail_dock_pad", "pos": [-15800, -16400, 2], "extent": [1200, 500, 2], "surface": "concrete", "blocksMovement": false },
    { "note": "ТЗ §9's путь, at last: two rails under the freight cars that have been parked on nothing since the sector was first authored. Rust, flat, non-colliding — 20 uu half-width each, 240 uu apart, which reads as gauge from above.",
      "id": "bridge_rail_track_north", "pos": [-13750, -15480, 2], "extent": [4250, 20, 2], "surface": "rust", "blocksMovement": false },
    { "id": "bridge_rail_track_south", "pos": [-13750, -15720, 2], "extent": [4250, 20, 2], "surface": "rust", "blocksMovement": false },
    { "note": "The depot's approach off the west dirt road, so the siding is somewhere a road goes.",
      "id": "bridge_road_rail_approach", "pos": [-14500, -13200, 3], "extent": [325, 1800, 3], "surface": "dirt", "blocksMovement": false }
```

- [ ] **Step 5: Re-author the gas station cluster**

The eleven props under the `PETROL STATION FORECOURT` note were authored around a shop that has since moved 3800 uu south to its ledger coordinate. Replace that whole run of eleven entries — the three `fuel_pump`s at (-12400, -4900/-5500/-6100), the four `wall`s, the three `crate`s and the `car_wreck` — with:

```json
    { "note": "THE АЗС FORECOURT, re-authored around the shop's ТЗ §28 coordinate. The pumps stood 3900 uu north of their own shop from the moment the shop moved, which read as a station with no fuel; they are now a row of three on the canopy pad, east of the shop's east doorway, exactly where a car would pull in. ТЗ §9 wants 3-4.",
      "kind": "fuel_pump", "pos": [-11400, -8700, 110] },
    { "kind": "fuel_pump", "pos": [-11400, -9000, 110] },
    { "kind": "fuel_pump", "pos": [-11400, -9300, 110] },
    { "note": "The canopy's four pillars — see bridge_gas_canopy_pad. A pipe is a 1200 uu cylinder, which is what a canopy stanchion looks like from above and the only upright the kind table has.",
      "kind": "pipe", "pos": [-12200, -8200, 600] },
    { "kind": "pipe", "pos": [-10600, -8200, 600] },
    { "kind": "pipe", "pos": [-12200, -9800, 600] },
    { "kind": "pipe", "pos": [-10600, -9800, 600] },
    { "note": "THE ВЫВЕСКА (ТЗ §14 lists it as a landmark). A mast with a sign at its foot: the road_sign kind is a 170 uu plate at 320 uu, which is signage rather than a landmark, and a landmark-scale plate needs a composite kind this stage does not add. Flagged, not silently dropped.",
      "kind": "pipe", "pos": [-12300, -7700, 600] },
    { "kind": "road_sign", "pos": [-12300, -7400, 0], "yaw": 90 },
    { "note": "L24's фургон. Composite: pos.z = 0.",
      "kind": "trailer", "pos": [-15000, -7900, 0], "yaw": 90 },
    { "note": "Cover on the forecourt, so crossing it is not a free walk with two bots watching.",
      "kind": "concrete_barrier", "pos": [-12000, -10200, 55] },
    { "kind": "concrete_barrier", "pos": [-11600, -10200, 55] },
    { "kind": "car_wreck", "pos": [-10800, -7600, 75], "yaw": 100 },
    { "kind": "crate", "pos": [-14100, -8000, 70] },
    { "kind": "rock", "pos": [-16200, -9400, 65] },
    { "kind": "bush", "pos": [-16400, -8600, 45] }
```

- [ ] **Step 6: Dress the pipe crossing and the rail depot**

Append to `props`:

```json
    { "note": "THE PIPE CROSSING, south side — L01's sibling vocabulary: a trailer in the техдвор, barriers along the walkway's mouth, and enough scatter that the crossing's exit is not a bare gap. The eight standing pipes and the walkway between them are Stage B's and do not move.",
      "kind": "trailer", "pos": [-13600, -4200, 0], "yaw": 0 },
    { "kind": "concrete_barrier", "pos": [-14300, -2600, 55], "yaw": 90 },
    { "kind": "concrete_barrier", "pos": [-13700, -2600, 55], "yaw": 90 },
    { "kind": "crate", "pos": [-14300, -3900, 70] },
    { "kind": "fence_section", "pos": [-13400, -5400, 92] },
    { "kind": "rock", "pos": [-15600, -2900, 65] },
    { "kind": "bush", "pos": [-13200, -2200, 45] },
    { "note": "THE PIPE CROSSING, north side — the approach L13 sits on. A sign at the head of the walkway, because from the north the crossing is a gap between two grey cylinders and nothing says 'this is a way across'.",
      "kind": "road_sign", "pos": [-14000, 3600, 0] },
    { "kind": "rock", "pos": [-14700, 2900, 65] },
    { "kind": "bush", "pos": [-13900, 2500, 45] },
    { "kind": "log", "pos": [-15200, 3400, 55], "yaw": 70 },

    { "note": "THE RAIL DEPOT — the разгрузка. A trailer backed onto the dock pad, pallets as crates, and the упор at the west end that L30 stands against.",
      "kind": "trailer", "pos": [-15800, -16200, 0], "yaw": 90 },
    { "kind": "crate", "pos": [-15400, -16500, 70] },
    { "kind": "crate", "pos": [-15250, -16750, 70] },
    { "kind": "concrete_barrier", "pos": [-17200, -17700, 55], "yaw": 90 },
    { "kind": "concrete_barrier", "pos": [-16800, -17700, 55], "yaw": 90 },
    { "kind": "crate", "pos": [-12900, -16500, 70] },
    { "kind": "fence_section", "pos": [-13600, -18400, 92] },
    { "kind": "fence_section", "pos": [-12800, -18400, 92] },
    { "kind": "rock", "pos": [-11800, -16200, 65] },
    { "kind": "bush", "pos": [-16600, -18900, 45] },
    { "kind": "log", "pos": [-10400, -16800, 55], "yaw": 20 }
```

- [ ] **Step 7: Run the tests**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, `B + 3` tests performed. The failures to expect and how to read them:

- **`building '<id>': …` from the parser** — the expander refused a building and the whole map failed to load, which is why every other map test fails at once. The message names the building and the rule: a doorway too near a wall's end (leave ≥120 uu), an interior wall shorter than it is thick, a passage under 250 uu, a footprint that leaves under 500 uu of interior, or exactly one doorway. Fix the building; the rules are ТЗ §13's.
- **`container '<id>' is not inside a wall`** — one of L25/L28/L29/`rail_mil_01` landed on a wall. Move the container ≤400 uu and **record the offset in the task report** (ТЗ §29).
- **`prop '<id>' does not overlap wall '<id>'`** — a prop from Step 5 or 6 is inside one of the new buildings' walls. Move the prop.
- **`solid blocks … do not intersect`** — a pad is colliding (it must be `"blocksMovement": false`) or a building overlaps the closure. `bridge_gas_wc` at (-14300,-7600) spans x -14650..-13950 and y -7900..-7300, clear of the shop's y -9750..-8250; `bridge_pipes_shed` spans x -15500..-14300, y -5050..-4150, clear of everything.
- **`Sarko.Map.BridgeStaysInsideTheActorBudget`** should report about **432**, under the 560 ceiling Task 3 set. Copy the line into the report.

- [ ] **Step 8: Verify every camp from the ground**

Run these four and **read every PNG**:

```
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -14500 -3200 200
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -13500 -9000 200
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -15200 -17600 200
```

Answer in the task report:

1. Pipes: does the crossing read as a route with a yard at its south end, and is the shed's interior visible from above (open rooms, not a solid block)?
2. Gas station: are the pumps, the canopy pad and the pillars all on the same forecourt as the shop's east doorway — i.e. does it read as one place now? Is the pad's concrete visibly paler than the ground and the asphalt?
3. Rail depot: do the two rails read as a track under the freight cars, and does the warehouse read as **two** rooms?
4. Anywhere: is a wall's shadow still near-black at ground level, or did Stage B's ambient sky light hold up in a room with more walls around it than the three buildings it was tuned on?

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): the three camps — pipes yard, a gas station with its own pumps, a rail depot with rails"
```

---

### Task 5: The tutorial's static loot layout — this stage's acceptance bar

Fifth, and it is the task the stage is judged on. Stage A.5 built the `fixedItems` mechanism and authored nothing, so every tutorial raid logs

```
SarkoRaidGameMode: TUTORIAL loot requested but none of 42 containers in 'bridge' carries fixedItems — every container will roll instead. Authoring the static layout is Stage C's job (spec §6.5).
```

**That Warning disappearing, replaced by `TUTORIAL loot — 19 of 19 containers carry fixedItems`, is the acceptance bar.** It comes last of the content tasks on purpose: spec §6.5 requires the static layout to be authored "against Bridge_West's final geometry", and the geometry became final in Task 4.

**Every one of the nineteen gets a list.** The Warning only needs one, but the *teaching* needs determinism: a route where fourteen containers are scripted and five roll is a route where the lesson lands or does not depending on a seed. Nineteen lists also make the parser's own rule bite usefully — an empty `fixedItems` array is a named error, because written to mean "this one is empty" it would silently fall through to a seeded roll.

**The teaching order (spec §6.5), one line per stop:**

| stop | container | holds | teaches |
|---|---|---|---|
| spawn camp | L01, L02, L03 (junk) | scrap metal, copper wire, duct tape | that containers open at all, with nothing worth dying for in them |
| pipe crossing | L13, L18 (common) | bandages, 12 rounds | that the route pays a little for walking it |
| pipes, south side | **L19 (common)** | **1 medkit** + 1 bandage | heal *before* the first bot — the bot is on the forecourt 4400 uu south |
| gas station, зал | **L20 (common)** | **20 rounds** | that buildings hold loot, and that you reload before a fight |
| gas station, подсобка | **L21 (good)** | **1 toolbox** + canned food | the first thing worth carrying home — the first valuable |
| gas station, rest | L22, L23, L24, L25 | food, the med cabinet, the van, the storeroom | that a camp is worth clearing, not just crossing |
| rail depot | L26–L30 | 24 rounds, food, bandages, pallets, scrap | that deeper is richer |
| rail warehouse, west room | **`rail_mil_01` (military)** | **1 pistol** + 40 rounds | that the deepest room is the richest — the military beat |
| beside the вагон | `rail_mil_02` (military) | medkit, painkillers, 20 rounds | the same lesson at the furthest point from E1: now carry it back |
| E1 (-15500,+19500) | — | — | extract, and the haul becomes the stash |

**The haul fits, with one slot spare.** A player who opens all nineteen carries: scrap_metal 7 (1 slot), copper_wire 9 (1), duct_tape 1 (1), bandage 5 (1), ammo_9mm 116 (2), medkit 3 (1), painkillers 3 (1), canned_food 5 (1), toolbox 1 (1), pistol 1 (1) — **11 of 12 slots**. This is deliberate and asserted: `USarkoRaidSettings::BackpackSlots` is 12 and the backend's `domain.MaxRaidStacks` mirrors it, so a route that yielded 14 stacks would teach the player about overflow by silently leaving loot in a box on their first ever raid. Stack sizes come from `Data/Items/items.json` and the arithmetic is done by the test, not by hand.

**No vehicle part is authored anywhere.** ТЗ §30 and spec §4.2: the bicycle is several successful raids, and a guaranteed `bike_frame` on a scripted route would hand a third of it out on raid one. Asserted.

**No new item id, no loot-table change, no backend change.** Every id below is already in `Data/Items/items.json`, so `domain.ValidateRaidItems` accepts the haul unchanged and the Go suite stays at 94.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (`fixedItems` on all 19 containers)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (one stale comment)

**Interfaces:**
- Consumes: `FSarkoLootContainerSpot::FixedItems`, `SarkoLoot::GetItemCatalog()`, `SarkoLoot::AddToBackpack(Slots, Catalog, SlotLimit, Item, Quantity)` — which returns **how much did not fit**, so zero is success — and `USarkoRaidSettings::BackpackSlots`.
- Produces: no interface. The container ids from Task 1 now carry contents.

- [ ] **Step 1: Write the failing teaching test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`. It needs two more includes at the top of that file if they are not already there: `#include "Core/SarkoRaidSettings.h"` (already present) and `#include "Loot/SarkoBackpack.h"`.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestTutorialLayoutTeachesInOrder,
	"Sarko.Map.BridgeWestTutorialLayoutTeachesInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestTutorialLayoutTeachesInOrder::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// THE ACCEPTANCE BAR of Stage C, as an assertion. SetTutorialLoot counts
	// containers with FixedItems.Num() > 0 and logs a Warning naming the gap when
	// the count is zero; every container carries a list, so the count is 19 of 19
	// and the Warning cannot fire. Partial authoring is refused deliberately: a
	// route where five of nineteen roll is a route whose lesson depends on a seed.
	int32 WithFixedItems = 0;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		if (Spot.FixedItems.Num() > 0)
		{
			++WithFixedItems;
		}
		else
		{
			AddError(FString::Printf(TEXT("container '%s' has no fixedItems"), *Spot.Id));
		}
	}
	TestEqual(TEXT("every container carries a static list"), WithFixedItems, Map.Containers.Num());

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	// ТЗ §30 / spec §4.2: the bicycle takes several successful raids. A guaranteed
	// vehicle part on a scripted route hands a third of it out on raid one and the
	// Garage stops being a reason to raid again.
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		for (const FSarkoItemStack& Stack : Spot.FixedItems)
		{
			const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
			TestNotNull(FString::Printf(TEXT("'%s' in '%s' is in the catalog"),
				*Stack.Item.ToString(), *Spot.Id), Def);
			if (Def)
			{
				TestTrue(FString::Printf(TEXT("'%s' in '%s' is not a vehicle part"),
					*Stack.Item.ToString(), *Spot.Id),
					Def->Category != ESarkoItemCategory::VehiclePart);
			}
			TestTrue(FString::Printf(TEXT("'%s' in '%s' has a positive quantity"),
				*Stack.Item.ToString(), *Spot.Id), Stack.Quantity > 0);
		}
	}

	// The teaching order, beat by beat. Positions rather than ids where the beat
	// is about a place: "a medkit at the pipes" is a claim about the pipes.
	const auto Holds = [&Map](const FString& Id, const FName& Item, int32 AtLeast)
	{
		const FSarkoLootContainerSpot* Spot = Map.Containers.FindByPredicate(
			[&Id](const FSarkoLootContainerSpot& S) { return S.Id == Id; });
		if (!Spot) { return false; }
		for (const FSarkoItemStack& Stack : Spot->FixedItems)
		{
			if (Stack.Item == Item && Stack.Quantity >= AtLeast) { return true; }
		}
		return false;
	};

	// 1. Junk at spawn: open a container.
	TestTrue(TEXT("L01 at the spawn camp holds junk to pick up"),
		Holds(TEXT("bridge_loot_l01"), TEXT("scrap_metal"), 1));
	// 2. A medkit at the pipes, before the first bot (which is on the АЗС
	//    forecourt, 4400 uu further south).
	TestTrue(TEXT("L19 at the pipes holds a medkit"),
		Holds(TEXT("bridge_loot_l19"), TEXT("medkit"), 1));
	// 3. Ammo and the first valuable at the gas station, both INSIDE the building.
	TestTrue(TEXT("L20 in the зал holds ammo"),
		Holds(TEXT("bridge_loot_l20"), TEXT("ammo_9mm"), 20));
	TestTrue(TEXT("L21 in the подсобка holds the first valuable"),
		Holds(TEXT("bridge_loot_l21"), TEXT("toolbox"), 1));
	// 4. Military at the rail depot: deeper is richer.
	TestTrue(TEXT("the warehouse's military container holds a weapon"),
		Holds(TEXT("bridge_loot_rail_mil_01"), TEXT("pistol"), 1));
	TestTrue(TEXT("the warehouse's military container holds ammo for it"),
		Holds(TEXT("bridge_loot_rail_mil_01"), TEXT("ammo_9mm"), 40));

	// The medkit must arrive BEFORE the deepest fight, not after it. Containers are
	// authored in route order (spawn -> pipes -> gas station -> rail depot), which
	// this asserts rather than assumes.
	int32 FirstMedkitIndex = INDEX_NONE;
	int32 FirstWeaponIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Map.Containers.Num(); ++Index)
	{
		for (const FSarkoItemStack& Stack : Map.Containers[Index].FixedItems)
		{
			const FSarkoItemDef* Def = Catalog.Find(Stack.Item);
			if (!Def) { continue; }
			if (Def->Category == ESarkoItemCategory::Med && FirstMedkitIndex == INDEX_NONE)
			{
				FirstMedkitIndex = Index;
			}
			if (Def->Category == ESarkoItemCategory::Weapon && FirstWeaponIndex == INDEX_NONE)
			{
				FirstWeaponIndex = Index;
			}
		}
	}
	TestTrue(TEXT("healing is taught before the military beat"),
		FirstMedkitIndex != INDEX_NONE && FirstWeaponIndex != INDEX_NONE &&
		FirstMedkitIndex < FirstWeaponIndex);

	// The whole route fits in the backpack. BackpackSlots is 12 and the backend's
	// domain.MaxRaidStacks mirrors it, so a route yielding 14 stacks teaches a
	// first-time player about overflow by leaving loot in a box. Simulated through
	// the real stacking function, not by counting item names.
	const USarkoRaidSettings* Settings = GetDefault<USarkoRaidSettings>();
	TestNotNull(TEXT("settings resolve"), Settings);
	const int32 SlotLimit = Settings ? Settings->BackpackSlots : 12;
	TArray<FSarkoItemStack> Slots;
	for (const FSarkoLootContainerSpot& Spot : Map.Containers)
	{
		for (const FSarkoItemStack& Stack : Spot.FixedItems)
		{
			const int32 Leftover = SarkoLoot::AddToBackpack(Slots, Catalog, SlotLimit, Stack.Item, Stack.Quantity);
			TestEqual(FString::Printf(TEXT("'%s' from '%s' fits (%d left over)"),
				*Stack.Item.ToString(), *Spot.Id, Leftover), Leftover, 0);
		}
	}
	AddInfo(FString::Printf(TEXT("the whole tutorial route fills %d of %d backpack slots"),
		Slots.Num(), SlotLimit));
	TestTrue(FString::Printf(TEXT("the tutorial haul fits with a slot spare (%d of %d)"),
		Slots.Num(), SlotLimit), Slots.Num() <= SlotLimit - 1);
	return true;
}
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map.BridgeWestTutorialLayoutTeachesInOrder`

Expected: FAIL with nineteen `container '<id>' has no fixedItems` errors and every teaching-beat assertion red.

- [ ] **Step 3: Author the nineteen lists**

Replace the whole `"containers"` array in `SarkoGame/Data/Maps/bridge.json` with this — the same rows, the same positions, the same tiers, plus `fixedItems`. Quantities are whole numbers (a fraction is a named parse error) and the order of rows is the **route order** the test depends on.

```json
  "containers": [
    { "note": "SPAWN CAMP — the three junk containers, and the tutorial's first lesson: a container opens, and this one holds scrap. fixedItems is only consulted while the profile says tutorial_completed is false; afterwards it is dead data and every container rolls.",
      "id": "bridge_loot_l01", "pos": [-17800, 17100, 35], "tier": "junk",
      "fixedItems": [ { "item": "scrap_metal", "qty": 2 } ] },
    { "id": "bridge_loot_l02", "pos": [-16400, 18100, 35], "tier": "junk",
      "fixedItems": [ { "item": "copper_wire", "qty": 2 } ] },
    { "id": "bridge_loot_l03", "pos": [-15000, 16700, 35], "tier": "junk",
      "fixedItems": [ { "item": "duct_tape", "qty": 1 } ] },

    { "note": "THE PIPE CROSSING — the route starts paying. L13 is the north approach, L18 the pipe base.",
      "id": "bridge_loot_l13", "pos": [-14200, 3300, 35], "tier": "common",
      "fixedItems": [ { "item": "bandage", "qty": 2 } ] },
    { "id": "bridge_loot_l18", "pos": [-14800, -3800, 35], "tier": "common",
      "fixedItems": [ { "item": "ammo_9mm", "qty": 12 } ] },
    { "note": "THE MEDKIT, and the reason it is here: spec §6.5 teaches healing BEFORE the first bot, and the first bot stands on the АЗС forecourt 2900 uu south of this техдвор. A common container may hold базовая медицина (ТЗ §30), so the tier does not have to move for the beat to land.",
      "id": "bridge_loot_l19", "pos": [-13300, -4600, 35], "tier": "common",
      "fixedItems": [ { "item": "medkit", "qty": 1 }, { "item": "bandage", "qty": 1 } ] },

    { "note": "THE GAS STATION — buildings hold loot. L20 is inside the зал: twenty rounds, which is a reload before the forecourt.",
      "id": "bridge_loot_l20", "pos": [-13300, -8800, 35], "tier": "common",
      "fixedItems": [ { "item": "ammo_9mm", "qty": 20 } ] },
    { "note": "THE FIRST VALUABLE — a toolbox in the подсобка. The first thing in the raid that is worth carrying home rather than using, which is the whole extraction loop in one object.",
      "id": "bridge_loot_l21", "pos": [-14100, -9100, 35], "tier": "good",
      "fixedItems": [ { "item": "toolbox", "qty": 1 }, { "item": "canned_food", "qty": 1 } ] },
    { "id": "bridge_loot_l22", "pos": [-13000, -9600, 35], "tier": "good",
      "fixedItems": [ { "item": "canned_food", "qty": 1 } ] },
    { "note": "The med cabinet — the sector's one med container.",
      "id": "bridge_loot_l23", "pos": [-12550, -8400, 35], "tier": "med",
      "fixedItems": [ { "item": "medkit", "qty": 1 }, { "item": "painkillers", "qty": 1 } ] },
    { "id": "bridge_loot_l24", "pos": [-15000, -7600, 35], "tier": "good",
      "fixedItems": [ { "item": "canned_food", "qty": 1 } ] },
    { "id": "bridge_loot_l25", "pos": [-15700, -10500, 35], "tier": "common",
      "fixedItems": [ { "item": "copper_wire", "qty": 3 } ] },

    { "note": "THE RAIL DEPOT — deeper is richer.",
      "id": "bridge_loot_l26", "pos": [-15800, -16600, 35], "tier": "good",
      "fixedItems": [ { "item": "ammo_9mm", "qty": 24 } ] },
    { "id": "bridge_loot_l27", "pos": [-11200, -17500, 35], "tier": "good",
      "fixedItems": [ { "item": "canned_food", "qty": 2 } ] },
    { "id": "bridge_loot_l28", "pos": [-12600, -16900, 35], "tier": "common",
      "fixedItems": [ { "item": "bandage", "qty": 2 } ] },
    { "id": "bridge_loot_l29", "pos": [-14500, -18100, 35], "tier": "good",
      "fixedItems": [ { "item": "copper_wire", "qty": 4 }, { "item": "scrap_metal", "qty": 3 } ] },
    { "id": "bridge_loot_l30", "pos": [-17200, -17400, 35], "tier": "common",
      "fixedItems": [ { "item": "scrap_metal", "qty": 2 } ] },
    { "note": "THE MILITARY BEAT — the deepest room on the route, inside the warehouse's west half, past a doorway and a divider. A pistol and forty rounds: the richest single container in the sector, and the furthest from E1 except for its twin.",
      "id": "bridge_loot_rail_mil_01", "pos": [-15600, -17600, 35], "tier": "military",
      "fixedItems": [ { "item": "pistol", "qty": 1 }, { "item": "ammo_9mm", "qty": 40 } ] },
    { "note": "The second military container, at the depot's east end. Whatever the player takes from here they carry 37000 uu back to E1 past four bots, which is the lesson the whole map exists to teach.",
      "id": "bridge_loot_rail_mil_02", "pos": [-10700, -17500, 35], "tier": "military",
      "fixedItems": [ { "item": "medkit", "qty": 1 }, { "item": "painkillers", "qty": 2 }, { "item": "ammo_9mm", "qty": 20 } ] }
  ],
```

- [ ] **Step 4: Correct the stale comment in `LootTest.cpp`**

`Sarko.Loot.TutorialModeFallsBackToRollingWhenNothingIsAuthored` still passes — it uses a synthetic spot, not the map — but its comment now says something false. In `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`, replace the sentence

```
	// the two, `bridge.json` carries no fixedItems at all, and a tutorial raid that
	// yielded nothing from all 42 containers would be a real regression in a build
	// that is meant to stay playable. So tutorial mode with nothing authored rolls
```

with

```
	// the two, `bridge.json` carried no fixedItems at all. Stage C authored all
	// nineteen (Sarko.Map.BridgeWestTutorialLayoutTeachesInOrder is the bar), so
	// this is now the rule for a container that is added later and forgotten, and
	// for any future map. Tutorial mode with nothing authored still rolls
```

Leave the rest of the comment and the whole body alone: the fallback is still the right behaviour and still needs a test.

- [ ] **Step 5: Run the suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, `B + 4` tests performed, and the `AddInfo` line `the whole tutorial route fills 11 of 12 backpack slots`. **Copy that line into the task report.** If it says 12 or 13, the arithmetic drifted: reduce a `canned_food` or an `ammo_9mm` quantity rather than raising `BackpackSlots`, which is mirrored by the backend's `domain.MaxRaidStacks` and cannot be changed from here.

If the map fails to load with `containers[N]: 'fixedItems' is present but empty`, a row was left with `[]` — the parser refuses that on purpose, because an empty list would fall through to a seeded roll and cost the stage its acceptance signal.

- [ ] **Step 6: Watch the Warning disappear**

This is the acceptance bar, and no automation test can see it — the line is logged by the game mode at raid start, and `USarkoRaidSettings::bOfflineTutorialLoot` is `true`, so an offline headless raid takes the tutorial branch.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh && grep -E "TUTORIAL loot" "$HOME/Library/Logs/Unreal Engine/SarkoGameEditor/SarkoGame.log" | tail -3
```

Expected, exactly one line, at `Display` and not `Warning`:

```
SarkoRaidGameMode: TUTORIAL loot — 19 of 19 containers carry fixedItems, the rest roll
```

**Paste that line verbatim into the task report.** If the Warning still appears, the file on disk is not the file the game loaded (check for a JSON parse failure earlier in the log); if nothing appears at all, the run never reached `SetTutorialLoot` — look for `FallBackToOfflineRaid` and for an earlier `Error`.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp SarkoGame/Source/SarkoGame/Tests/LootTest.cpp && git commit -m "feat(loot): the tutorial raid has a static layout — junk, a medkit, a valuable, a rifle, in that order"
```

---

### Task 6: Filling the north (ТЗ §15)

Sixth, once the raid has been playable for three tasks. Every overview of this sector shows the same defect: 89 props north of the ravine against 119 south, two whole 100×100 m cells holding four and eight objects, and a flat olive stretch between the dirt road and the highway with nothing to stand behind. ТЗ §15 names the cure — "25–40 камней, 20–35 кустов, 10–15 брёвен, 8–12 заборов, 5–8 остовов, опоры ЛЭП, 2–3 техстроения — крупные формы, не сотни мелочи" — and Stage B added exactly those kinds for exactly this.

**Only the active north is filled.** The closed north-east (east of x = -6100) gets nothing: the player never walks it, and 60 more actors of scenery behind a treeline is budget spent on a frame nobody stands in. So §15's counts are authored into the ~14000 × 17500 uu the player actually crosses, at the low-to-middle of each range: **27 rocks, 25 bushes, 11 logs, 9 fence sections, 6 wrecks, 2 pylons** (the third is Task 2's, on the same ЛЭП line) and **2 техстроения**.

**Clumps, not a scatter.** Eleven anchors of six to eight objects each, because §15 asks for large forms and because a uniform sprinkle reads as noise from above and gives cover nowhere. Each clump is something to move between; the gaps between them are the exposure.

**ТЗ §8's old дачи are deliberately not authored.** N01–N03 (-13500..-8500, +11500..+17000) sit inside the active third geographically, but the owner's Bridge_West list does not include them, so their ground gets §15's fill instead. They arrive with Stage D together with their three junk containers (L04–L06) and the weak bot at (-10000, +13500). Flagged rather than dropped: this is the one place where the active third and the owner's content list disagree about what exists.

**The fallen ЛЭП pylon cannot be built** (flag 4): parts have no roll or pitch. The three standing pylons are on §8's line (-11000,+19000) → (+4000,+8500) — at x = -11000, -9000 and -6800 that line is at y = 19000, 17600 and 16060 — and the fallen one belongs to the closed stretch, where §29's L07 container is anyway.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (`props` +80, `buildings` +2)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test)

**Interfaces:**
- Consumes: the nine kinds proven in Task 2, `SarkoMap::CountPropActors`, the ceilings Task 3 set.
- Produces: building ids `bridge_north_shed_a` and `bridge_north_shed_b`.

- [ ] **Step 1: Write the failing fill test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeWestNorthIsFilled,
	"Sarko.Map.BridgeWestNorthIsFilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeWestNorthIsFilled::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	// The active north: west of the closure, north of the ravine, inside the world
	// border. This is the ground the player crosses twice every raid, and the
	// ground every overview of this sector has shown as an empty field.
	const auto InActiveNorth = [](const FVector& P)
	{
		return P.X >= -19700.f && P.X <= -6100.f && P.Y >= 2200.f && P.Y <= 19700.f;
	};

	TMap<FName, int32> Counts;
	int32 Total = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		if (InActiveNorth(Prop.Location))
		{
			Counts.FindOrAdd(Prop.Kind)++;
			++Total;
		}
	}
	AddInfo(FString::Printf(TEXT("the active north holds %d props"), Total));

	// ТЗ §15's shopping list, as floors. Floors and not ranges: the ranges are for
	// the WHOLE north, and only its western third is authored now, so more is not
	// a defect — fewer is the defect this task exists to fix.
	const auto AtLeast = [this, &Counts](const FName& Kind, int32 Floor)
	{
		const int32 Have = Counts.FindRef(Kind);
		TestTrue(FString::Printf(TEXT("ТЗ §15 wants %s: %d of at least %d"),
			*Kind.ToString(), Have, Floor), Have >= Floor);
	};
	AtLeast(TEXT("rock"), 25);
	AtLeast(TEXT("bush"), 20);
	AtLeast(TEXT("log"), 10);
	AtLeast(TEXT("fence_section"), 8);
	AtLeast(TEXT("car_wreck"), 5);

	// ТЗ §8's ЛЭП. Three of the four-to-five опор are in the active third; the rest
	// of the line, including the fallen one §8 asks for, is behind the closure —
	// and a fallen pylon is not expressible anyway (FSarkoPropPart has no roll).
	int32 Pylons = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		if (Prop.Kind == TEXT("pylon") && Prop.Location.X <= -6100.f)
		{
			++Pylons;
			// On §8's line (-11000,+19000) -> (+4000,+8500), i.e. y = 19000 - 0.7*(x+11000).
			const float ExpectedY = 19000.f - 0.7f * (static_cast<float>(Prop.Location.X) + 11000.f);
			TestTrue(FString::Printf(TEXT("pylon at (%.0f,%.0f) is on ТЗ §8's ЛЭП line (expected y=%.0f)"),
				Prop.Location.X, Prop.Location.Y, ExpectedY),
				FMath::Abs(static_cast<float>(Prop.Location.Y) - ExpectedY) <= 200.f);
		}
	}
	TestTrue(FString::Printf(TEXT("the active third carries three ЛЭП pylons (%d)"), Pylons), Pylons >= 3);

	// ТЗ §15's "2-3 техстроения".
	int32 NorthSheds = 0;
	for (const FSarkoBuilding& Building : Map.Buildings)
	{
		if (Building.Id.StartsWith(TEXT("bridge_north_shed")))
		{
			++NorthSheds;
			TestEqual(FString::Printf(TEXT("тех shed '%s' is closed"), *Building.Id),
				Building.Doors.Num(), 0);
			TestTrue(FString::Printf(TEXT("тех shed '%s' is in the active north"), *Building.Id),
				InActiveNorth(Building.Location));
		}
	}
	TestTrue(FString::Printf(TEXT("ТЗ §15 wants 2-3 техстроения (%d)"), NorthSheds), NorthSheds >= 2);

	// ТЗ §32, density, and the actual complaint: two 100x100 m cells of the north
	// held four and eight objects. Every 100 m cell of the active north must now
	// hold something worth walking to. Buildings count as objects — a shed is a
	// large form, which is what §15 asks for — and so do the closure's own props,
	// because a treeline you can stand behind IS the cover in that cell.
	for (float CellX = -20000.f; CellX < -6100.f; CellX += 10000.f)
	{
		for (float CellY = 2200.f; CellY < 19700.f; CellY += 10000.f)
		{
			int32 InCell = 0;
			for (const FSarkoMapProp& Prop : Map.Props)
			{
				if (Prop.Location.X >= CellX && Prop.Location.X < CellX + 10000.f &&
					Prop.Location.Y >= CellY && Prop.Location.Y < CellY + 10000.f)
				{
					++InCell;
				}
			}
			for (const FSarkoBuilding& Building : Map.Buildings)
			{
				if (Building.Location.X >= CellX && Building.Location.X < CellX + 10000.f &&
					Building.Location.Y >= CellY && Building.Location.Y < CellY + 10000.f)
				{
					++InCell;
				}
			}
			TestTrue(FString::Printf(TEXT("the 100x100 m cell at (%.0f,%.0f) holds %d objects (>= 8)"),
				CellX, CellY, InCell), InCell >= 8);
		}
	}
	return true;
}
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Map.BridgeWestNorthIsFilled`

Expected: FAIL on every `ТЗ §15 wants …` line (there are no rocks, bushes, logs or fences in the north at all), on `2-3 техстроения`, and on at least one of the four density cells.

- [ ] **Step 3: Author the two техстроения**

Add to the `buildings` array:

```json
    { "note": "ТЗ §15's техстроения, 1 of 2. CLOSED: this is a large form to break 140 m of empty field and to give the walk from the spawn to the pipes something to navigate by, not a room. Interior 840x640, which clears the expander's 500 uu floor on both axes.",
      "id": "bridge_north_shed_a", "pos": [-12200, 8200, 0], "size": [900, 700], "surface": "structure",
      "doors": [] },
    { "note": "ТЗ §15's техстроения, 2 of 2, on the ground ТЗ §8's дачи will one day occupy — they are Stage D (the owner's Bridge_West list does not include them), so §15's fill holds this cell until then. Rust, so the two sheds do not read as a matched pair.",
      "id": "bridge_north_shed_b", "pos": [-8600, 13600, 0], "size": [1000, 800], "surface": "rust",
      "doors": [] },
```

- [ ] **Step 4: Author the fill**

Append all of this to `props`. Eleven clumps, then the two pylons. Remember the conventions: `rock` z 65, `bush` z 45, `log` z 55, `fence_section` z 92, `car_wreck` z 75, `pylon` z **0**.

```json
    { "note": "ТЗ §15 FILL — clump A, the west field between the spawn camp and the dirt road's upper bend.",
      "kind": "rock", "pos": [-18600, 12800, 65] },
    { "kind": "rock", "pos": [-18300, 12400, 65] },
    { "kind": "rock", "pos": [-19000, 13100, 65] },
    { "kind": "bush", "pos": [-18800, 12200, 45] },
    { "kind": "bush", "pos": [-18200, 13000, 45] },
    { "kind": "bush", "pos": [-19100, 12600, 45] },
    { "kind": "log", "pos": [-18500, 11900, 55], "yaw": 20 },
    { "kind": "fence_section", "pos": [-18000, 12100, 92], "yaw": 80 },

    { "note": "Clump B — beside the dirt road, so the road has a shoulder rather than an edge.",
      "kind": "rock", "pos": [-16600, 9800, 65] },
    { "kind": "rock", "pos": [-16900, 10200, 65] },
    { "kind": "bush", "pos": [-16200, 9300, 45] },
    { "kind": "bush", "pos": [-16400, 8900, 45] },
    { "kind": "log", "pos": [-16800, 9100, 55], "yaw": 105 },
    { "kind": "fence_section", "pos": [-16100, 10000, 92], "yaw": 10 },
    { "kind": "fence_section", "pos": [-15900, 10000, 92], "yaw": 10 },

    { "note": "Clump C — the old field boundary on the дачи's ground (ТЗ §8's N01-N03 are Stage D).",
      "kind": "car_wreck", "pos": [-13000, 13200, 75], "yaw": 40 },
    { "kind": "rock", "pos": [-13300, 12700, 65] },
    { "kind": "rock", "pos": [-12700, 13400, 65] },
    { "kind": "bush", "pos": [-13500, 13300, 45] },
    { "kind": "bush", "pos": [-12600, 12600, 45] },
    { "kind": "log", "pos": [-13100, 12300, 55], "yaw": 160 },
    { "kind": "fence_section", "pos": [-12400, 13600, 92], "yaw": 90 },
    { "kind": "fence_section", "pos": [-12400, 14400, 92], "yaw": 90 },

    { "note": "Clump D — the north-east of the active third, on the way to nothing, which is why it needs a reason to be crossed.",
      "kind": "rock", "pos": [-10600, 16800, 65] },
    { "kind": "rock", "pos": [-10200, 16200, 65] },
    { "kind": "bush", "pos": [-10800, 16000, 45] },
    { "kind": "bush", "pos": [-10100, 16900, 45] },
    { "kind": "log", "pos": [-10500, 15800, 55], "yaw": 60 },
    { "kind": "car_wreck", "pos": [-10900, 17200, 75], "yaw": 15 },

    { "note": "Clump E — the eastern approach to the closure.",
      "kind": "rock", "pos": [-8100, 11300, 65] },
    { "kind": "rock", "pos": [-7700, 10700, 65] },
    { "kind": "rock", "pos": [-8600, 11800, 65] },
    { "kind": "bush", "pos": [-8300, 10600, 45] },
    { "kind": "bush", "pos": [-7600, 11500, 45] },
    { "kind": "log", "pos": [-8000, 10300, 55], "yaw": 130 },
    { "kind": "fence_section", "pos": [-7400, 11900, 92] },

    { "note": "Clump F — the middle of the flat olive stretch the overview keeps showing as nothing.",
      "kind": "car_wreck", "pos": [-12000, 6200, 75], "yaw": 70 },
    { "kind": "rock", "pos": [-12300, 5700, 65] },
    { "kind": "rock", "pos": [-11700, 6500, 65] },
    { "kind": "rock", "pos": [-11300, 6900, 65] },
    { "kind": "bush", "pos": [-12500, 6300, 45] },
    { "kind": "bush", "pos": [-11600, 5600, 45] },
    { "kind": "log", "pos": [-12100, 5300, 55], "yaw": 25 },

    { "note": "Clump G — the ravine's northern shoulder, where a player walking the rim needs cover from the far bank.",
      "kind": "rock", "pos": [-9100, 4800, 65] },
    { "kind": "rock", "pos": [-8700, 4200, 65] },
    { "kind": "bush", "pos": [-9300, 4100, 45] },
    { "kind": "bush", "pos": [-8600, 4900, 45] },
    { "kind": "bush", "pos": [-8300, 3900, 45] },
    { "kind": "log", "pos": [-9000, 3700, 55], "yaw": 95 },
    { "kind": "car_wreck", "pos": [-9400, 5200, 75], "yaw": 50 },

    { "note": "Clump H — the south-west shoulder, on the line between the dirt road and the pipe crossing.",
      "kind": "rock", "pos": [-17100, 5800, 65] },
    { "kind": "rock", "pos": [-16700, 5200, 65] },
    { "kind": "rock", "pos": [-17600, 6100, 65] },
    { "kind": "bush", "pos": [-17300, 5100, 45] },
    { "kind": "bush", "pos": [-16600, 5900, 45] },
    { "kind": "bush", "pos": [-16200, 4900, 45] },
    { "kind": "log", "pos": [-17000, 4700, 55], "yaw": 145 },

    { "note": "Clump I — the north edge east of E1, so the last 100 m before the extraction is not a sprint across a lawn.",
      "kind": "rock", "pos": [-11600, 18800, 65] },
    { "kind": "rock", "pos": [-11200, 18200, 65] },
    { "kind": "bush", "pos": [-11800, 18100, 45] },
    { "kind": "bush", "pos": [-11100, 18900, 45] },
    { "kind": "car_wreck", "pos": [-12000, 18600, 75], "yaw": 100 },
    { "kind": "log", "pos": [-11500, 17900, 55], "yaw": 35 },
    { "kind": "fence_section", "pos": [-10700, 18400, 92], "yaw": 90 },

    { "note": "Clump J — around техстроение B, which needs something around it or it reads as a crate in a field.",
      "kind": "rock", "pos": [-8600, 15300, 65] },
    { "kind": "rock", "pos": [-8200, 14700, 65] },
    { "kind": "bush", "pos": [-8800, 14600, 45] },
    { "kind": "bush", "pos": [-8100, 15500, 45] },
    { "kind": "log", "pos": [-8500, 14300, 55], "yaw": 50 },
    { "kind": "fence_section", "pos": [-7800, 15900, 92], "yaw": 90 },
    { "kind": "car_wreck", "pos": [-8900, 15700, 75], "yaw": 25 },

    { "note": "SPAWN CAMP SOFTENING — the camp itself was authored as a fence and three crates on bare ground; these give the first thirty seconds of every raid something to look at.",
      "kind": "rock", "pos": [-19200, 16200, 65] },
    { "kind": "rock", "pos": [-18800, 13800, 65] },
    { "kind": "bush", "pos": [-19000, 15600, 45] },
    { "kind": "bush", "pos": [-18600, 14600, 45] },
    { "kind": "log", "pos": [-19300, 15000, 55], "yaw": 15 },

    { "note": "Two singles that fix the cell counts rather than a clump: one boulder on the road's east side, one field boundary north of the дачи ground.",
      "kind": "rock", "pos": [-15500, 12000, 65] },
    { "kind": "fence_section", "pos": [-14000, 15000, 92] },

    { "note": "ЛЭП (ТЗ §8), опоры 1 and 2 of the three in the active third — on the line (-11000,+19000) -> (+4000,+8500). The third is at (-6800,+16060), authored in the closure task. Composite: pos.z = 0. The 'упавшая' one §8 asks for is on the closed stretch of the line and could not be built anyway — parts have no roll.",
      "kind": "pylon", "pos": [-11000, 19000, 0] },
    { "kind": "pylon", "pos": [-9000, 17600, 0] }
```

- [ ] **Step 5: Run the suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, `B + 5` tests performed, and two `AddInfo` lines to copy into the task report: `the active north holds N props` (expect ~110 including the spawn camp's original props) and the actor bill, now about **526** against the 560 ceiling Task 3 set.

If `the 100x100 m cell at (…) holds N objects` fails for one cell, add two or three more objects **in that cell** rather than lowering the floor — the floor is ТЗ §32's own complaint written down.

If `Sarko.Map.BridgePropsClearTheWalls` fails, a fill prop is inside one of the two new sheds' walls: `bridge_north_shed_a` occupies x -12650..-11750, y 7850..8550 and `bridge_north_shed_b` x -9100..-8100, y 13200..14000. Move the prop.

- [ ] **Step 6: Look at the north**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -12000 6000 200`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh -16500 9500 200`

**Read all three.** Answer in the task report:

1. Is the north still visibly emptier than the south, or do the two halves now read as the same world? (Compare the two halves of the same frame; that is the whole reason this task exists.)
2. From the eye at clump F: is there something to stand behind within a few seconds' walk in most directions?
3. Do the pylons read as a line — three towers heading east — rather than as three unrelated objects?
4. Are the bushes distinguishable from the rocks at eye level? They are the one thing with no collision, and if they look like cover the fill has made the map lie.
5. Do the clumps read as clumps, or has the fill become the "сотни мелочи" §15 explicitly warns against?

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): the north stops being a lawn — ТЗ §15's rocks, bushes, logs, fences, pylons and two sheds"
```

---

### Task 7: Landmarks and hue — a dark deck with light rails, a ford that reads as shallow, clusters that differ by colour

Seventh, and the first task in this plan whose whole deliverable is how the sector *looks*. It comes after the raid, the loot and the fill because the owner's rule is not to polish decor before a raid can be played from spawn to extraction — and two of its three subjects (the bridge deck and the ford) are **beyond the closure**. They are authored anyway, and here is the honest reason: the bridge and the water tower are the landmarks the player navigates the closed east by, ТЗ §5 and §6 state exactly what they should look like, and the cost is one kind, one surface and five data rows. Deferring them to Stage D would mean reopening the palette then instead of now, for the same work.

**Three subjects:**

1. **The bridge deck is pale with light rails; ТЗ §5 wants «тёмный асфальт, светлые борта».** It is backwards today because `bridge_deck` and the parapets are both `Structure` grey — the deck reads as the pale thing and the rails vanish into it. Fix: `bridge_deck`'s surface becomes `Asphalt` (the darkest surface in the palette), and the eighteen parapet and approach-rail props become a new `bridge_rail` kind with **byte-identical extents to `wall`** (400×60×140) and the `Concrete` surface, which is the palette's pale contrast tone. Identical extents means not one of the eighteen props moves — only its colour changes.
2. **The ford reads only as a gap.** ТЗ §6 wants «мелкая вода, камни, плиты, два спуска, знак» and the sector has two sandbags. The blocker was that the palette has one water tone, so «мелкая» had nothing to be shallow *against*. Fix: a twelfth surface, `ESarkoSurface::Shallow` — a lighter blue-grey slab laid on top of the deep water across the crossing's 2000 uu, plus four boulders to wind between, two concrete ramps for §6's two descents, and the sign. It is still opaque and it still will not read as water in a close-up: that limitation is Stage B's and is not reopened here.
3. **Village and industry separate by cluster shape, not by hue.** Sixteen `house` props are all the same `Structure` grey, so from above the village and the промзона differ only in how the boxes are arranged. Fix: two more kinds with `house`'s exact extents — `house_timber` (ТЗ §14's warm village tone) and `house_industrial` (§14's rust) — and re-kind all sixteen by cluster. `house` itself stays in the table, unchanged, because `Sarko.Map.PropKindsAreComplete` pins it and because a third neutral variant is worth having.

**Inserting a surface mid-enum is safe here, and the safety net is loud.** Nothing serialises the numeric value — map files carry surface *names*, and `ESarkoSurface` is a transient uint8 on a struct — so `Shallow` goes in after `Water`, keeping the water/shallow/ravine group together. But `Styles[]`, `SurfaceNames[]` and the enum are three parallel lists in enum order: edit fewer than all three and `Sarko.Config.SurfacePaletteIsReadable`'s exhaustive round-trip loop fails immediately, which is exactly why that loop counts to `Count`.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h` (enum +1), `.cpp` (style +1, name +1)
- Modify: `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp` (`bridge_deck` surface; +3 kinds)
- Modify: `SarkoGame/Data/Maps/bridge.json` (18 props re-kinded to `bridge_rail`, 16 to the house variants, +8 ford entries)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp` (palette relations for `Shallow`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp` (`PropKindsAreComplete`'s surface assertion; the kind list in `PropKindScaleMatchesThePawn`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp` (+1 test; `BridgeHasReadableGroundSurfaces` learns about `Shallow`)

**Interfaces:**
- Consumes: `SarkoMap::Palette::ColourFor/RoughnessFor`, `SarkoMap::ParseSurfaceName`, `SarkoMap::SurfaceName`, `SarkoMap::FindPropKind`.
- Produces: `ESarkoSurface::Shallow`; prop kinds `bridge_rail`, `house_timber`, `house_industrial`.

- [ ] **Step 1: Write the failing palette relations**

In `SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp`, inside `FSarkoSurfacePaletteIsReadable::RunTest`, add after the existing `Water` block:

```cpp
	{
		// ТЗ §6's «мелкая вода» at the ford. The deep water had no lighter tone to
		// be shallow against, which is why the ford read as a gap in the cliff and
		// nothing else. Still opaque — a translucent material needs an authored
		// asset and this project authors none (spec §5.2).
		const FLinearColor Shallow = ColourFor(ESarkoSurface::Shallow);
		const FLinearColor Water = ColourFor(ESarkoSurface::Water);
		TestTrue(TEXT("shallow water is still blue-grey: blue leads, red trails"),
			Shallow.B > Shallow.G && Shallow.G > Shallow.R);
		TestTrue(TEXT("shallow water is visibly lighter than deep water"),
			Lum(Shallow) > Lum(Water) * 1.8f);
		TestTrue(TEXT("shallow water is lighter than the ground it interrupts, so a ford reads as a bright band"),
			Lum(Shallow) > GroundLum);
	}
```

- [ ] **Step 2: Write the failing landmark test**

Append to `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBridgeLandmarksReadApart,
	"Sarko.Map.BridgeLandmarksReadApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBridgeLandmarksReadApart::RunTest(const FString& Parameters)
{
	FSarkoMapDefinition Map;
	FString Error;
	if (!LoadBridge(Map, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json failed to load: %s"), *Error));
		return false;
	}

	const auto SurfaceOf = [this](const FName& Kind, ESarkoSurface& Out)
	{
		FSarkoPropKind Resolved;
		if (!SarkoMap::FindPropKind(Kind, Resolved) || Resolved.Parts.Num() == 0)
		{
			AddError(FString::Printf(TEXT("kind '%s' does not resolve"), *Kind.ToString()));
			return false;
		}
		Out = Resolved.Parts[0].Surface;
		return true;
	};

	// ТЗ §5: «тёмный асфальт, светлые борта». It was backwards — deck and parapets
	// were both Structure grey, so the deck was the pale thing and the rails were
	// invisible against it. The kind extents are unchanged, so no prop moved.
	ESarkoSurface Deck = ESarkoSurface::Structure;
	ESarkoSurface Rail = ESarkoSurface::Structure;
	if (SurfaceOf(TEXT("bridge_deck"), Deck) && SurfaceOf(TEXT("bridge_rail"), Rail))
	{
		TestEqual(TEXT("the deck is asphalt-dark"), static_cast<uint8>(Deck), static_cast<uint8>(ESarkoSurface::Asphalt));
		TestEqual(TEXT("the rails are the pale concrete tone"), static_cast<uint8>(Rail), static_cast<uint8>(ESarkoSurface::Concrete));
		const auto Lum = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
		TestTrue(TEXT("the rails are much lighter than the deck"),
			Lum(SarkoMap::Palette::ColourFor(Rail)) > Lum(SarkoMap::Palette::ColourFor(Deck)) * 4.f);
	}
	// A bridge_rail is a wall's twin in every dimension: the eighteen parapet props
	// were re-kinded in place, and a different extent would have moved all of them.
	FSarkoPropKind WallKind;
	FSarkoPropKind RailKind;
	if (SarkoMap::FindPropKind(TEXT("wall"), WallKind) && SarkoMap::FindPropKind(TEXT("bridge_rail"), RailKind))
	{
		TestTrue(TEXT("a bridge rail has a wall's exact extents"),
			RailKind.Parts[0].Extent.Equals(WallKind.Parts[0].Extent, 0.01f));
	}

	// ТЗ §14: the village is warm, the промзона is rust. Sixteen identical grey
	// boxes told the player nothing about which cluster they were looking at.
	int32 Timber = 0;
	int32 Industrial = 0;
	int32 Neutral = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		if (Prop.Kind == TEXT("house_timber"))     { ++Timber; }
		if (Prop.Kind == TEXT("house_industrial")) { ++Industrial; }
		if (Prop.Kind == TEXT("house"))            { ++Neutral; }
	}
	TestTrue(FString::Printf(TEXT("the warm clusters use house_timber (%d)"), Timber), Timber >= 6);
	TestTrue(FString::Printf(TEXT("the industrial clusters use house_industrial (%d)"), Industrial), Industrial >= 6);
	TestEqual(TEXT("no house prop is left on the default grey"), Neutral, 0);
	ESarkoSurface TimberSurface = ESarkoSurface::Structure;
	ESarkoSurface RustSurface = ESarkoSurface::Structure;
	if (SurfaceOf(TEXT("house_timber"), TimberSurface))
	{
		TestEqual(TEXT("house_timber is the village tone"),
			static_cast<uint8>(TimberSurface), static_cast<uint8>(ESarkoSurface::Timber));
	}
	if (SurfaceOf(TEXT("house_industrial"), RustSurface))
	{
		TestEqual(TEXT("house_industrial is rust"),
			static_cast<uint8>(RustSurface), static_cast<uint8>(ESarkoSurface::Rust));
	}

	// ТЗ §6's ford: shallow water across the crossing, boulders to wind between,
	// and a sign. The crossing is the 2000 uu gap in the rim walls at x 12000..14000.
	int32 ShallowBlocks = 0;
	for (const FSarkoCoverBlock& Block : Map.Blocks)
	{
		if (Block.Surface == ESarkoSurface::Shallow)
		{
			++ShallowBlocks;
			TestFalse(FString::Printf(TEXT("shallow block '%s' does not block movement"), *Block.Id),
				Block.bBlocksMovement);
			TestTrue(FString::Printf(TEXT("shallow block '%s' is in the ford's mouth"), *Block.Id),
				Block.Location.X >= 12000.f && Block.Location.X <= 14000.f);
			TestTrue(FString::Printf(TEXT("shallow block '%s' sits on top of the deep water, not under it"), *Block.Id),
				Block.Location.Z - Block.Extent.Z >= 10.f);
		}
	}
	TestTrue(TEXT("the ford has shallow water"), ShallowBlocks >= 1);

	int32 FordRocks = 0;
	for (const FSarkoMapProp& Prop : Map.Props)
	{
		if (Prop.Kind == TEXT("rock") &&
			Prop.Location.X >= 12000.f && Prop.Location.X <= 14000.f &&
			FMath::Abs(Prop.Location.Y) <= 1000.f)
		{
			++FordRocks;
		}
	}
	TestTrue(FString::Printf(TEXT("ТЗ §6's «камни» make the ford a winding route (%d)"), FordRocks), FordRocks >= 3);
	return true;
}
```

- [ ] **Step 3: Run both and confirm they fail**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko`

Expected: `BUILD FAILED`, with `no member named 'Shallow' in 'ESarkoSurface'` in the tail of `Saved/Logs/agent-build.log`.

- [ ] **Step 4: Add the twelfth surface**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h`, insert between `Water` and `Ravine`:

```cpp
	/**
	 * The ford's «мелкая вода» (ТЗ §6). Lighter than Water and laid on top of it,
	 * so a crossing reads as a bright band in a dark gorge instead of as a gap in
	 * a cliff. Also opaque — the limitation Water documents applies here too, and
	 * this surface is what makes "shallow" expressible at all without a material.
	 */
	Shallow,
```

In `SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.cpp`, insert into `Styles[]` immediately after the `/* Water */` line:

```cpp
			// Lighter than the deep water and lighter than the GROUND — a shallow
			// over pale stones is the one water tone that should read bright, which
			// is what makes a ford legible from a top-down camera 20000 uu up.
			// Near-matte for the same reason Water is: a gloss you cannot control
			// across 400 m is worse than no gloss.
			/* Shallow    */ { FLinearColor(0.045f, 0.062f, 0.085f),    0.90f },
```

and into `SurfaceNames[]`, keeping enum order:

```cpp
		TEXT("ground"), TEXT("dirt"), TEXT("asphalt"), TEXT("concrete"), TEXT("structure"),
		TEXT("rust"), TEXT("timber"), TEXT("vegetation"), TEXT("water"), TEXT("shallow"),
		TEXT("ravine"), TEXT("extraction")
```

All three lists are in enum order and must be edited together; if one is missed, `Sarko.Config.SurfacePaletteIsReadable`'s round-trip loop says which.

- [ ] **Step 5: Add the three kinds and re-surface the deck**

In `SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp`, change the `bridge_deck` row's surface from `ESarkoSurface::Structure` to `ESarkoSurface::Asphalt` — **the extents do not change**, so none of the nine deck props moves — and add three rows after the eleven legacy kinds:

```cpp
			// ТЗ §5's «светлые борта». A wall's exact twin — 400x60x140 — so the
			// eighteen parapet and approach-rail props are re-kinded in place
			// without one of them moving; only the colour differs, and that is the
			// whole point: the deck went dark, so the rails have to go pale or the
			// crossing loses its silhouette from above.
			{ TEXT("bridge_rail"),      Box(Cube, FVector(400.f, 60.f, 140.f), true, ESarkoSurface::Concrete) },
			// ТЗ §14's «деревня тёплая» / «промзона ржавая». house's exact extents
			// (500x400x300), so a cluster is re-kinded in place. Sixteen identical
			// grey boxes made the village and the промзона differ only by how the
			// boxes were arranged; hue is the cheapest cluster identity there is.
			{ TEXT("house_timber"),     Box(Cube, FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Timber) },
			{ TEXT("house_industrial"), Box(Cube, FVector(500.f, 400.f, 300.f), true, ESarkoSurface::Rust) },
```

- [ ] **Step 6: Teach the two existing tests about the change**

`Sarko.Map.PropKindsAreComplete` asserts that **every** legacy kind keeps the `Structure` surface — the guard that stops the shipped map being repainted by accident. `bridge_deck` is now a deliberate exception, so the guard has to name it. In `SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp`, replace the last assertion inside the `LegacyExtents` loop:

```cpp
		// Every legacy kind was painted Palette::Structure before surfaces
		// existed, so anything else here repaints the shipped map.
		TestEqual(FString::Printf(TEXT("kind '%s' keeps the structure surface"), *Expected.Key.ToString()),
			static_cast<uint8>(Resolved.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Structure));
```

with

```cpp
		// Every legacy kind was painted Palette::Structure before surfaces existed,
		// so anything else here repaints the shipped map — with ONE deliberate
		// exception, named rather than excluded by a loosened rule. ТЗ §5 asks for
		// a dark deck with light rails and the sector had a pale deck with rails
		// that vanished into it; bridge_deck is therefore asphalt, and the pale
		// tone moved to the new bridge_rail kind. The extents are untouched, which
		// is what kept all twenty-seven bridge props exactly where they were.
		if (Expected.Key == TEXT("bridge_deck"))
		{
			TestEqual(TEXT("bridge_deck is asphalt-dark (ТЗ §5)"),
				static_cast<uint8>(Resolved.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Asphalt));
		}
		else
		{
			TestEqual(FString::Printf(TEXT("kind '%s' keeps the structure surface"), *Expected.Key.ToString()),
				static_cast<uint8>(Resolved.Parts[0].Surface), static_cast<uint8>(ESarkoSurface::Structure));
		}
```

Then add the three new kinds to the name list `Sarko.Map.PropKindScaleMatchesThePawn` walks, so their scale is held to the same pawn-relative rules as everything else (they are twins of `wall` and `house`, so they pass unchanged):

```cpp
		TEXT("bridge_deck"), TEXT("bridge_rail"), TEXT("house_timber"), TEXT("house_industrial"),
		TEXT("rock"), TEXT("bush"), TEXT("log"), TEXT("fence_section"),
```

And in `SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp`, teach `FSarkoBridgeHasReadableGroundSurfaces` that `Shallow` is a water surface: add `case ESarkoSurface::Shallow:` immediately above `case ESarkoSurface::Water:` in its `switch`, and extend the two containment loops so a `Shallow` block is checked by the same `FMath::Abs(Location.Y) + Extent.Y <= 900.f` rule as `Water` — shallow water outside the ravine bed would be a river running across the map just as surely.

- [ ] **Step 7: Re-kind the bridge rails and the sixteen houses**

In `SarkoGame/Data/Maps/bridge.json`:

- Change `"kind": "wall"` to `"kind": "bridge_rail"` for the **eighteen** bridge props only: the ten parapets at x = ±1000, y = -1600/-800/0/800/1600, and the eight approach rails at x = ±1000, y = ±2600 / ±3400. Nothing else becomes a rail — every other `wall` prop in the file is a fence, a ruin or a barricade.
- Change `"kind": "house"` to the variant matching its cluster, all sixteen:

| cluster | props | new kind |
|---|---|---|
| village | (-200,-8600), (2400,-8600), (1000,-11200), (3600,-11200) | `house_timber` |
| farmstead (north-west) | (-11000,9500) | `house_timber` |
| farm compound | (10500,-11000), (11800,-12200) | `house_timber` |
| southern homestead | (6200,-16400) | `house_timber` |
| ruins (east, near half) | (16200,12000), (17600,10200), (15400,9000) | `house_industrial` |
| cement works | (-18200,-8200), (-17000,-9600) | `house_industrial` |
| rail siding | (-16400,-14000) | `house_industrial` |
| industrial yard | (16000,-15500), (17800,-17600) | `house_industrial` |

That is 8 timber and 8 industrial. The farmstead and the rail-siding house are the two judgement calls: the farmstead is a dwelling in a field (warm) and the siding's building is depot infrastructure (rust). Both sit inside or beside Bridge_West's active third, which is why they are called out rather than lumped.

- [ ] **Step 8: Author the ford (ТЗ §6)**

Add to `blocks`:

```json
    { "note": "THE FORD's «мелкая вода» (ТЗ §6). A lighter slab laid ON TOP of the deep water (which occupies z 4..10), across the 2000 uu gap in the rim walls at x 12000..14000. This is why ESarkoSurface::Shallow exists: with one water tone the ford read as a gap in a cliff and nothing else. Still opaque, still won't read as water in a close-up — Stage B's limitation, not reopened. BEHIND THE CLOSURE until Stage D: authored now because the palette is open and §6's clause costs five rows, and authored LAST because it is decor in closed space.",
      "id": "bridge_ford_shallow", "pos": [13000, 0, 12], "extent": [1000, 700, 2], "surface": "shallow", "blocksMovement": false },
    { "note": "ТЗ §6's «два спуска» — the ramps down each bank, and the «плиты» at the same time: pale concrete against the dark bed, so the crossing has visible ends.",
      "id": "bridge_ford_ramp_north", "pos": [13000, 1500, 3], "extent": [500, 300, 3], "surface": "concrete", "blocksMovement": false },
    { "id": "bridge_ford_ramp_south", "pos": [13000, -1500, 3], "extent": [500, 300, 3], "surface": "concrete", "blocksMovement": false }
```

and to `props`:

```json
    { "note": "ТЗ §6's «камни»: four boulders that make the ford an «извилистый маршрут» instead of a 2000 uu doorway. They narrow the crossing to two winding lanes and are the only cover in it.",
      "kind": "rock", "pos": [12500, 300, 65] },
    { "kind": "rock", "pos": [13400, -200, 65] },
    { "kind": "rock", "pos": [12800, -500, 65] },
    { "kind": "rock", "pos": [13600, 500, 65] },
    { "note": "ТЗ §6's «знак» at the north descent. Composite: pos.z = 0.",
      "kind": "road_sign", "pos": [13900, 1900, 0], "yaw": 90 }
```

- [ ] **Step 9: Run the suite**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh`

Expected: `ALL GREEN`, `B + 6` tests performed — the final count for this plan. Two failures worth naming in advance:

- `'shallow' and '<x>' are not the same colour` — the new value is within 0.004 of an existing one. Move the new value, never the old one: every other number in that table was chosen against a real frame.
- `no house prop is left on the default grey` — one of the sixteen was missed. `grep -c '"kind": "house"' SarkoGame/Data/Maps/bridge.json` should print `0`.

The actor bill is now about **529** (+3 ford blocks) against the 560 ceiling. Nothing re-kinded changes the count: `bridge_rail`, `house_timber` and `house_industrial` are single-box kinds, one actor each, exactly like the kinds they replace.

- [ ] **Step 10: Read the three claims in a frame**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh 0 2600 200`
Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh 13000 1600 200`

**Read all three and answer in the task report:**

1. Is the bridge now a dark deck with pale rails, and is it *more* legible as a bridge from the overview than it was, not less? (A dark deck on a dark ravine can lose the deck entirely — if that happened, the rails are carrying the read and the deck may need `Concrete`'s kerbs rather than a lighter deck. Report which.)
2. Does the ford read as shallow water — a lighter band inside the dark ravine — with stones to wind between and ends you can see?
3. From the overview: can you tell the village from the промзона by colour alone, with the cluster shapes ignored?
4. Does anything in the active west third look repainted by accident? Nothing there should have changed except the cement works and the rail siding house going rust.

- [ ] **Step 11: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.h SarkoGame/Source/SarkoGame/Map/SarkoMapPalette.cpp SarkoGame/Source/SarkoGame/Map/SarkoMapKinds.cpp SarkoGame/Data/Maps/bridge.json SarkoGame/Source/SarkoGame/Tests/MapBuilderTest.cpp SarkoGame/Source/SarkoGame/Tests/MapDefinitionTest.cpp SarkoGame/Source/SarkoGame/Tests/BridgeMapTest.cpp && git commit -m "feat(map): a dark deck with light rails, a ford that reads as shallow, clusters that differ by hue"
```

---

### Task 8: The bright rectangle near (0, +17250) — find the pass before touching a cvar

Last, because it is an investigation whose output may legitimately be "identified, not fixed", and because it must not hold up anything else.

**What is known.** Every overview of this sector shows a bright, **world-axis-aligned** rectangle near (0, +17250). It is pre-existing and confirmed — it is not something this plan introduces. **The "shadow-cascade boundary" diagnosis that has been repeated about it is unsupported**: a cascade split is centred on the *view*, so its boundary would follow the camera and would not be axis-aligned to the world. Whatever this is, it is pinned to world space, which points somewhere else entirely — a virtual-shadow-map clipmap page (VSM pages *are* a world-space grid, and virtual shadow maps are deliberately enabled in `DefaultEngine.ini`), a distance-field term, the sky light's capture, or simply a piece of geometry that is painted or lit differently from its neighbours.

**Do not touch any `r.Shadow.*` value until an A/B has named the pass.** The whole reason this is a task rather than a step is that the last diagnosis was wrong and the temptation is to fix the guess.

**Files:**
- Modify: `SarkoGame/Scripts/overview-shot.sh` (one optional env var)
- Possibly modify: `SarkoGame/Config/DefaultEngine.ini` (only if the A/B identifies a renderer setting, and only with the finding written beside it)

**Interfaces:**
- Consumes: `Scripts/overview-shot.sh`, `Scripts/eye-shot.sh`, `SarkoDebug::HeightToFitSector`.
- Produces: `OVERVIEW_EXTRA_CMDS`, an env var that lets any cvar be injected into the overview run — which is the loop this project keeps needing and re-improvising.

- [ ] **Step 1: Make the overview script A/B-able**

In `SarkoGame/Scripts/overview-shot.sh`, add after the `TIMEOUT` line:

```bash
# Extra console commands, injected before SarkoOverview. This exists so a
# rendering question can be A/B-ed against the same frame without editing the
# script: OVERVIEW_EXTRA_CMDS="r.ShadowQuality 0" ./Scripts/overview-shot.sh.
# Comma-separated, exactly as -ExecCmds wants them.
EXTRA="${OVERVIEW_EXTRA_CMDS:+, ${OVERVIEW_EXTRA_CMDS}}"
```

and change the `-ExecCmds` argument from

```bash
	-ExecCmds="t.MaxFPS 10, SarkoOverview" > /dev/null 2>&1 &
```

to

```bash
	-ExecCmds="t.MaxFPS 10${EXTRA}, SarkoOverview" > /dev/null 2>&1 &
```

The `?game=/Script/SarkoGame.SarkoRaidGameMode` argument stays exactly as it is — without it the run photographs the Shelter menu and hangs for the full timeout.

- [ ] **Step 2: Locate the rectangle in world coordinates**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh`

**Read the PNG** and record the rectangle's pixel bounds. Convert to world units: the camera sits at (0, 0, `HeightToFitSector`) looking straight down with equal horizontal and vertical FOV, so the 1600×1600 frame spans `±ExtentUU * FrameMargin` on both axes — `FrameMargin` and the height are logged by `SarkoDebug::FrameWholeSector` at the top of the run, so read them out of the engine log rather than assuming:

```bash
grep -E "SarkoOverview|ExtentUU" "$HOME/Library/Logs/Unreal Engine/SarkoGameEditor/SarkoGame.log" | tail -5
```

Remember the frame's orientation: **east is up, north is right.** Record the rectangle's world bounds and its size in the task report. A rectangle whose size matches a power-of-two number of metres (e.g. 4096 or 8192 uu on a side) is evidence for a page/tile grid; one that matches an authored block's footprint is evidence for geometry.

- [ ] **Step 3: Decide whether it is world-pinned or view-dependent**

Run: `cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/eye-shot.sh 0 17250 1200`

**Read the PNG.** The camera is now a few hundred metres away from where it was and pointed differently. Does the bright patch appear at the same *world* place — around the traffic jam north of the bridge — or has it moved with the camera?

- **Moved with the camera** ⇒ view-dependent: a cascade split after all, screen-space AO, or exposure. Go to Step 5's ambient/AO branch.
- **Stayed in world space** ⇒ world-pinned: a VSM clipmap page, a distance-field term, or geometry. Go to Step 4.
- **Not visible at all from the ground** ⇒ it is only in the top-down frame, which makes a top-down-specific term (shadow projection at a grazing angle, or the floor's own material) the first suspect. Say so and continue with Step 4 anyway.

- [ ] **Step 4: Is it geometry at all? (the cheapest test, so it goes first)**

Nothing world-axis-aligned is authored near (0, +17250): `bridge_road_highway_north` ends at y = +16500 and the bend above it carries `"yaw": 10`. Prove it is not one of them by repainting rather than moving:

Temporarily set `bridge_road_highway_north`'s `"surface"` to `"extraction"` — the one loud green in the palette — and re-shoot:

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/overview-shot.sh
```

**Read the PNG.** If the bright rectangle is now green, or if its edges move with the repainted block, the artefact is that block and the answer is a surface/roughness value — not a renderer setting. **Revert the surface immediately either way**; this is a probe, not a change. Record the result.

- [ ] **Step 5: A/B the passes, one cvar at a time**

Run each of these in turn and **read every PNG before running the next**. One cvar per run: two at once cannot be attributed.

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && OVERVIEW_EXTRA_CMDS="r.ShadowQuality 0" ./Scripts/overview-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && OVERVIEW_EXTRA_CMDS="r.Shadow.Virtual.Enable 0" ./Scripts/overview-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && OVERVIEW_EXTRA_CMDS="r.SkylightIntensityMultiplier 0" ./Scripts/overview-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && OVERVIEW_EXTRA_CMDS="r.AmbientOcclusionLevels 0" ./Scripts/overview-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && OVERVIEW_EXTRA_CMDS="r.DistanceFieldShadowing 0" ./Scripts/overview-shot.sh
```

Read them as a ladder, and write the conclusion as a sentence naming the pass:

- Gone with `r.ShadowQuality 0` **and** gone with `r.Shadow.Virtual.Enable 0` ⇒ **virtual shadow maps**, and given the world-alignment, a clipmap page boundary. The follow-up knobs are `r.Shadow.Virtual.ResolutionLodBiasDirectional` and `r.Shadow.Virtual.Clipmap.FirstLevel` — but VSMs are deliberately ON (without them a movable directional light casts no shadow and grey cover on a grey floor was invisible from above), so **turning them off is not an option** and any change here is a bias, tested in a frame.
- Gone with `r.ShadowQuality 0` only ⇒ a non-virtual shadow path; report it, because that contradicts the ini.
- Gone with `r.SkylightIntensityMultiplier 0` ⇒ the ambient sky light or its cubemap capture. Stage B's `AmbientCubemapResolution` is 32; a capture artefact at that resolution is plausible and cheap to test by raising it to 64 in `SarkoMap::Lighting`.
- Gone with `r.AmbientOcclusionLevels 0` ⇒ AO, which is screen-space and therefore contradicts the world-alignment finding; re-check Step 3 if so.
- **Still there in every one of them** ⇒ it is not a lighting pass. Say exactly that, and record it as unexplained with the five negatives listed. That is a real result and it is worth more than a guess.

- [ ] **Step 6: Fix only what the A/B named**

Two legitimate outcomes:

1. **A pass was identified and the fix is one documented value.** Change it — a renderer cvar belongs in `Config/DefaultEngine.ini` under `[/Script/Engine.RendererSettings]` (`config = Engine`; do not put it in `DefaultGame.ini`), with a comment naming the artefact, the A/B that found it and the date. Re-shoot and read the PNG to confirm it is gone and that shadows are still present everywhere else — losing the shadows would trade a bright rectangle for invisible cover, which is the trade Stage B explicitly refused. Then run `./Scripts/run-tests.sh` (`Sarko.Config.LightingHasAnAmbientTerm` and `Sarko.Config.SurfacePaletteIsReadable` are the tests that care) and confirm `ALL GREEN` at `B + 6`.
2. **Nothing was identified, or the fix would cost the sector its shadows.** Change nothing. Write the finding up in the task report — the world bounds, the size, the five A/B results, and the conclusion — and commit only the `overview-shot.sh` change, which is the reusable half of the work. **This is a successful outcome**: the artefact is cosmetic, it sits behind the closure in a corner of the map no Bridge_West player reaches, and a wrong fix to `r.Shadow.*` would cost every wall in the sector its silhouette.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Scripts/overview-shot.sh && git commit -m "tools(shot): OVERVIEW_EXTRA_CMDS, so a rendering question can be A/B-ed against one frame"
```

If Step 6 took branch 1, add `SarkoGame/Config/DefaultEngine.ini` to that `git add` and use the message `fix(render): <the artefact>, identified by A/B as <the pass>`.

---

## Self-Review

**Spec coverage, clause by clause.**

- **§6 "Active zone: the west, roughly X ∈ [-20000, -6000], both halves"** — Task 2's closure runs at x = -6100 north of the ravine and x = -9100 south of it (moved 3000 uu west so it clears ТЗ §9's village footprint and never cuts through the two walkable houses Stage B authored). Asserted by `Sarko.Map.BridgeWestIsEnclosed` (sampled at 100 uu) and by `Sarko.Map.BridgeWestLedgerIsAuthored`'s `InActiveThird` predicate over every container, bot and spawn. **Noted honestly:** spec §6's "~250×200 m of play space" does not match its own X range — 14000 × 40000 uu is 140 × 400 m. The X range is treated as authoritative because it is the number the closure can be built from; the report should say so.
- **§6 "spawns near the pipes camp (north side), loops: pipes → gas station → rail depot → back → dirt road north → E1"** — Task 1's four spawns are ТЗ §8's verbatim; the container array is authored in that route order (which Task 5's test depends on and asserts); E1 is at §12's (-15500, +19500) to the unit.
- **§6 "~10 buildings (pipes tech shed + pad; gas station shop/storeroom/canopy/wc; rail dispatcher/warehouse/shed/loading dock)"** — Task 4 authors six (pipes shed, storeroom, wc, dispatcher, warehouse, booth) alongside Stage B's `bridge_gas_station`; Task 6 adds two техстроения; the two walkable village houses remain in the file behind the closure. **11 buildings, 7 of them in the active third.** The pad and the loading dock are flat concrete blocks rather than buildings, and the canopy is a pad plus four pillars because a roof is forbidden — all three stated in Task 4 and in the flag list.
- **§6 "18–20 containers (3 junk / 7 common / 6 good / 1 med / 2 military)"** — 19, and `Sarko.Map.BridgeWestLedgerIsAuthored` asserts the split exactly, tier by tier: 3 + 7 + 6 + 1 + 2 = 19. ✔
- **§6 "6 bots (2 gas station, 4 rail), none near spawn"** — exactly six; two on the АЗС, four at the depot; every pair ≥1800 uu apart (the hearing radius, minimum actual 2360 uu) and every bot ≥6000 uu from every spawn. All asserted.
- **§6 "E1 the only wired extraction; E2/E3 exist in data but sit behind the closure"** — asserted in both directions: E1 inside the active third, the other two outside it.
- **§6 "Invariant tests updated to Bridge_West numbers; the full-map tests move to the ТЗ reference doc as the acceptance bar for the LATER expansion"** — the 42/16 assertions never existed as equalities (the committed tests carry floors: ≥15 containers, ≥6 bots), so nothing had to be relaxed; the new equalities are Bridge_West's, and `docs/design/bridge-full-map-tz.md` remains the record for 42/16/24.
- **§6.5 "the tutorial raid happens once, with static loot … junk at spawn → medkit at the pipes → ammo + first valuable at the gas station → military at the rail depot → extract at E1"** — Task 5, all five beats, each asserted by id and by item in `Sarko.Map.BridgeWestTutorialLayoutTeachesInOrder`, plus the ordering assertion that healing is taught before the military beat, plus the backpack simulation (11 of 12 slots) and the no-vehicle-parts rule. The acceptance bar itself — the Warning replaced by `TUTORIAL loot — 19 of 19 containers carry fixedItems` — is Step 6 of that task, verified by reading the engine log, because no `-nullrhi` test can see a game-mode log line.
- **ТЗ §15 "Север заполнить: 25–40 камней, 20–35 кустов, 10–15 брёвен, 8–12 заборов, 5–8 остовов, опоры ЛЭП, 2–3 техстроения"** — Task 6: 27 / 25 / 11 / 9 / 6 / 3 pylons / 2 sheds, all in the *active* north, all asserted as floors, plus a per-100 m-cell density floor of 8 objects which is ТЗ §32's own complaint written as a test.
- **ТЗ §16 iOS performance** — Task 3, the whole task: a measurement, a projection table, an explicit decision (ceilings 560/420, no instancing this stage), and the trigger that reverses it, written into the test comment so it survives this plan.
- **ТЗ §13 walkable-building rules** — every building in Task 4 and Task 6 is authored against the expander's rules and the door-offset bounds are stated in the task's table; two are deliberately closed (zero doors, legal, reachability fill skipped); none has exactly one door; every doorway is 300–340 uu, inside §13's preferred band, so the expander's "below 300" warning never fires.
- **ТЗ §5 dark deck / light rails, §6 «мелкая вода», §14 village-vs-industry hue** — Task 7, one subject each.
- **The five open items handed to this plan** — the ford (T7), the bridge deck (T7), the gas station's pump row 3500 uu from its shop (T4 Step 5, re-authored onto the forecourt and asserted at ≤2600 uu), the grey `house` props (T7, all sixteen re-kinded, `no house prop is left on the default grey` asserted), and the bright rectangle (T8, with the cascade diagnosis explicitly retired and an A/B ladder in its place).

**Every coordinate is inside ±20000.** The extremes authored by this plan: x from **-19850** (the west border blocks) to **+14500** (E3, which is data behind the closure); y from **-19850** (the south border) to **+20000** (the north border's own face, whose block centre is at y = 19850). The border blocks are the only entries that reach the sector's edge, and they reach it exactly — `pos ± extent` lands on ±20000, never past it. `Sarko.Map.BridgeMapIsValid`'s `CheckInside` asserts `|X| ≤ 20000 && |Y| ≤ 20000` for every block, prop, container and bot, so a typo is a red test rather than a prop in the void.

**Every building is legal under the expander's rules**, checked by hand at plan time and by the expander at load time:

| building | interior after 30 uu walls | doors | door bound on that wall | verdict |
|---|---|---|---|---|
| `bridge_pipes_shed` 1200×900 | 1140×840 | N @ -300 w300, E @ 0 w300 | N: ±480, E: ±330 | gap -450..-150 and -150..150, both inside |
| `bridge_gas_storeroom` 1000×800 | 940×740 | N @ 0 w300, E @ 0 w300 | N: ±380, E: ±250 | gaps ±150, inside |
| `bridge_gas_wc` 700×600 | 640×540 | none | — | closed, legal, fill skipped |
| `bridge_rail_dispatcher` 1100×800 | 1040×740 | N @ 0 w300, W @ 0 w300 | N: ±430, W: ±250 | gaps ±150, inside |
| `bridge_rail_warehouse` 2400×1600 | 2340×1540 | N @ -600 w340, E @ 300 w320 | N: ±1080, E: ±650 | gaps -770..-430 and 140..460, inside; divider at x=400 leaves 755 uu of passage vs the 250 minimum |
| `bridge_rail_booth` 700×600 | 640×540 | none | — | closed, legal |
| `bridge_north_shed_a` 900×700 | 840×640 | none | — | closed, legal |
| `bridge_north_shed_b` 1000×800 | 940×740 | none | — | closed, legal |

Each footprint clears the 500 uu interior floor on both axes; each thickness (30) clears 10..200 and `4T ≤ min(size)`; each height is the 350 default, inside 200..800. No building has exactly one door. The two-room warehouse is the only one with an interior wall, it is axis-aligned, and it does not seal anything (both rooms open off the divider's doorway and the west room additionally off the north perimeter doorway), so the flood fill passes.

**The container ledger sums to the stated tiers.** junk: L01, L02, L03 = **3**. common: L13, L18, L19, L20, L25, L28, L30 = **7**. good: L21, L22, L24, L26, L27, L29 = **6**. med: L23 = **1**. military: `rail_mil_01`, `rail_mil_02` = **2**. Total **19**, which is the owner's 18–20 and is asserted as an equality. Four re-tierings (L13, L18, L25, L30 junk→common) and one (L29 common→good) are named in Task 1 with the reason; the two military rows are named as having no §29 ledger row and as retiring when §29's four arrive.

**The tutorial teaching order is authored**, and each beat has both a data row and an assertion: junk at spawn (L01 `scrap_metal` 2) → medkit at the pipes (L19 `medkit` 1, 2900 uu before the first bot) → ammo and the first valuable at the gas station, both inside the building (L20 `ammo_9mm` 20, L21 `toolbox` 1) → military at the rail depot, in the deepest room (`rail_mil_01` `pistol` 1 + `ammo_9mm` 40) → extract at E1. All nineteen containers carry a list, so nothing on the route rolls; the whole haul is 11 of 12 backpack slots, simulated through `SarkoLoot::AddToBackpack` rather than counted by hand; no vehicle part appears anywhere, so the bicycle still takes several raids.

**The actor-budget decision is explicit.** Task 3, with the projection table (311 → 364 → 432 → 526 → 529), the measurement, the choice (**raise to 560 total / 420 props; no instancing this stage**), the reason instancing would not currently help (per-actor `UMaterialInstanceDynamic`s from `PaintFlat` defeat UE's mesh-draw auto-instancing, so ~529 actors are ~529 draws either way), the cheaper first mitigation (one shared material instance per surface), and the trigger (**a packaged iOS build missing 30 fps**) with a three-step response order in which cutting content is last. No other task in this plan changes a ceiling, and Task 4 and Task 6 both name the number they expect so a silent bump would show as a mismatch.

**No binary assets.** This plan creates **no new file at all** except an env var inside an existing `.sh`. It edits one `.json`, four `.cpp`/`.h` pairs' worth of existing source, three test files, and possibly one `.ini`. Every mesh is `/Engine/BasicShapes/{Cube,Cylinder,Sphere}`; every material is `/Engine/BasicShapes/BasicShapeMaterial` through a dynamic instance. No `.uasset`, `.umap`, Blueprint, UMG widget, material asset, DataTable, Behavior Tree or font is authored or edited, and `SarkoGame/Content/Mannequins/` is untouched.

**Traps walked deliberately.** `./Scripts/run-tests.sh` is the only verifier and every verify step names an expected count relative to a baseline recorded in Task 1 Step 1 — never a bare exit code, and never the literal 103. Every visual claim is `-RenderOffscreen` plus "**read the PNG**", never an assertion, and the overview's east-up/north-right orientation is stated where it matters. `eye-shot.sh`'s `Walk` after `BugItGo` is preserved (Task 8 edits only `overview-shot.sh`, and only its `-ExecCmds` tail). Profiling uses `-ExecCmds="CsvProfile Start"`, never `-csvCaptureFrames`. The one ini this plan may touch is `RendererSettings`, which is `config = Engine`, and Task 8 says so explicitly next to the reminder that `UProjectPackagingSettings` is `config = Game`; no value containing `//` is added. Nothing adds a second spawn path, so `SpawnMeshBox`'s Movable → mesh → scale → collision → paint → Static order is untouched, and every new primitive is painted. Palette additions are linear values, and the new one is checked for gamut, mutedness and uniqueness by the loop that counts to `Count`. The two `pos.z` conventions are stated in the Global Constraints and repeated at every composite. No test compares `FVector`s without `.Equals`. No `git checkout`/`stash`/`reset`, no `git add -A`, no branch switch, no push; every commit stages named paths, and the C++ another agent is editing on this branch (`SarkoBuildings.*`, `SarkoMapDefinition.cpp`) is **never** staged by any task here.

**Type consistency across tasks.** `ESarkoSurface::Shallow` is added once (T7) and used only there. `bridge_rail`, `house_timber`, `house_industrial` are defined in T7 and referenced only by T7's data rows. Container ids are minted in T1 (`bridge_loot_l01…l30`, `bridge_loot_rail_mil_01/02`) and are the same strings T5's `fixedItems` rows and T5's test use. Building ids are minted in T4 (`bridge_pipes_shed`, `bridge_gas_storeroom`, `bridge_gas_wc`, `bridge_rail_dispatcher`, `bridge_rail_warehouse`, `bridge_rail_booth`) and T6 (`bridge_north_shed_a/b`) and are spelled identically in the tests that look for them. Block id prefixes are stable: `bridge_west_closure_*`, `bridge_west_border_*`, `bridge_rail_track_*` (T4's test matches on that prefix), `bridge_ford_*`. `SarkoLoot::AddToBackpack` is used with its real contract — it returns **leftover**, so the test asserts zero. `SarkoMap::IsPointInsideBlocksXY(FVector2D, const TArray<FSarkoCoverBlock>&)` and the file-local `SolidOnly` are used exactly as the existing tests in `BridgeMapTest.cpp` use them; no second point-in-block predicate is introduced.

**Deliberate deviations, flagged rather than buried.**

1. **Four buildings have no §28 ledger row** (`bridge_pipes_shed`, `bridge_gas_wc`, `bridge_rail_warehouse`, `bridge_rail_booth`) plus two more from §15's fill (`bridge_north_shed_a/b`). §28 is a 24-row ledger; Bridge_West adds six buildings to it. They need rows when the full map is authored, or the ledger and the file will disagree by six.
2. **Two military containers have no §29 ledger row**, because §29 puts no military west of the bridge. They retire or move when §29's four arrive.
3. **Five container tiers differ from §29** (four junk→common, one common→good) to hit the owner's split.
4. **No bot at the pipe crossing**, against ТЗ §6's "один бот на южном выходе", because spec §6.5's teaching order requires the first bot to come after the medkit.
5. **ТЗ §8's дачи (N01–N03) are not authored** although they sit inside the active third; §15's fill holds that ground until Stage D.
6. **The closure's south run is at x = -9100, not -6100**, so it clears §9's village footprint.
7. **A world border exists that the ТЗ never asked for.** ТЗ treats the map edge as world boundary and says nothing about the floor ending; the rail depot is 1000 uu from that edge and E1 is 500 uu from it, so without a border the sector ships with two places a player falls out of the world and loses a haul. The border has exactly one gap, at E1, which is also the best thing about it: the only opening in the treeline is the way out.

**Known limitations, for the task reports rather than for silence.** Water and shallow water are both opaque; the ford will read as a lighter band from above and as flat paint at close range. There is no sky, no horizon and no fog — the world ends in black at the floor's edge, invisible from a top-down camera and wrong in the first low-angle shot anyone takes. The canopy, the вывеска, the fallen pylon and the цистерна are all substitutions, listed in the flag list with what each would need. The bright rectangle may end this plan unexplained, and that is an accepted outcome (Task 8, Step 6, branch 2). The actor bill is ~529 measured on a Mac; **nothing in this repository can build for an iOS device unattended**, so the ТЗ §16 budget is argued rather than proven, and the first packaged build is where that argument gets tested. Finally, this plan authors decor behind the closure in exactly two places — the bridge deck and the ford — and both are deliberately in the last content task, after the raid has been playable end-to-end since Task 1.
