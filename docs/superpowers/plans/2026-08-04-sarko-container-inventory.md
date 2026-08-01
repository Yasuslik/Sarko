# Container Inventory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn looting from "hold 1.5 s and everything teleports into your bag" into a decision: open a container, see its contents as a grid of cells beside your own, and take what you choose — while the world keeps running, the bots keep walking, and the remainder you cannot carry stays in the crate instead of evaporating.

**Architecture:** Three layers, each testable without the one above it. (1) **Model** — the backpack stops being a fixed 12-slot array and becomes 4 pocket cells plus an equipped backpack item worth +8; container contents are rolled once on first open and live server-side in `ASarkoRaidGameMode`; every move is one pure function, `SarkoLoot::TransferOne`. (2) **Wire** — per-item `ServerTakeItem(ContainerIndex, SlotIndex)` validated in a fixed order against the server's own copy of everything, with contents pushed to exactly one client by RPC and never derivable before an open. (3) **Panel** — a Slate widget built entirely from `FSlateRoundedBoxBrush` and `FCoreStyle` (no asset of any kind), occupying the right quarter of a landscape phone so the pawn and its approach stay visible, animated with `FCurveSequence` in under a quarter of a second.

**Tech Stack:** UE 5.8, C++ only. Slate (`SCompoundWidget`, `SButton`, `SDPIScaler`, `SUniformGridPanel`, `FSlateRoundedBoxBrush`, `FCurveSequence`) for the panel; `AHUD::DrawHUD` primitives stay for the readouts. `Build.sh` + `UnrealEditor-Cmd`; automation under `-nullrhi`; visual verification under `-RenderOffscreen`. Backend: Go, `sarko-api/internal/domain`.

## Global Constraints

- **`docs/superpowers/specs/2026-08-04-sarko-container-inventory-design.md` is normative.** Its four owner decisions are not reopened: landscape only; **cells, not spatial packing**; **the backpack is an item** (base 4 pocket cells, a found backpack grants +8 for 12); **looting does not pause the world**. Its §3 validation order is copied verbatim into Task 2 and must not be reordered.
- Engine at `/Users/Shared/Epic Games/UE_5.8`. Project `SarkoGame/`, module `SarkoGame`, class prefix `Sarko`. All paths are relative to `/Users/ruslanbondarenko/project/ai-workspace/home/Sarko` unless absolute; **agent shells reset their cwd between calls, so every command below begins with an explicit `cd`.**
- **Create no binary assets. Ever.** No `.uasset`, `.umap`, Blueprint, **UMG widget**, material, DataTable, font, icon or texture. C++, `.ini`, `.json`, `.sh`, `.go` only. Engine assets referenced **by path** are fine (`FCoreStyle` is compiled into SlateCore; `GEngine->GetLargeFont()` resolves to it). Everything that looks like a rounded, outlined, tinted box in this plan is `FSlateRoundedBoxBrush`, constructed in C++.
- **`SarkoGame.Build.cs` is not touched.** `Slate` and `SlateCore` are already public dependencies (added for the shelter menu); `Engine` covers `UUserInterfaceSettings`.
- **Verify only with `./Scripts/run-tests.sh`, never a bare exit code.** `UnrealEditor-Cmd` exits 0 having run zero tests; the script takes its verdict from the `Automation Test Queue Empty N tests performed` line, refuses to run while a process holds the project open, and refuses to run against a module dylib older than the newest source file. **Test counts in this plan are RELATIVE.** The suite is reported at **110** UE tests and the backend at **94**; those numbers are stale the moment anything else lands. Task 1 Step 1 records the real baselines `B` (UE) and `G` (Go), and every later step names `B + <delta>` / `G + <delta>`: T1 +4, T2 +4, T3 +3, T4 +2, T5 +1, T6 +2 → **B + 16**; backend **G + 0** (Task 1 changes an existing test rather than adding one). **Recompute `B` and `G` at execution time; do not copy 110 or 94.**
- The automation-test flag spelling that compiles is `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`, inside `#if WITH_AUTOMATION_TESTS`.
- **Automation runs `-nullrhi` and can see nothing.** No test in this plan may claim anything about how the panel *looks*. Every visual claim is settled by `-RenderOffscreen` + a screenshot that a human or an agent then **reads as an image**. `Scripts/hud-shot.sh` and `Scripts/shelter-shot.sh` are the pattern; Task 4 adds `Scripts/inventory-shot.sh`. **`sarko.SafeArea.DebugPhoneLandscape 1`** paints an iPhone's cutouts onto a Mac that reports none — every shot in this plan that claims anything about phone layout must set it.
- **`BugItGo` calls `Ghost()`**, which disables capsule collision for the rest of the run. Any headless run that needs collision appends `Walk` after the teleport. Copy the `-ExecCmds` shape from `Scripts/hud-shot.sh`.
- **`config = Game` → `Config/DefaultGame.ini`; `config = Engine` → `Config/DefaultEngine.ini`.** `USarkoRaidSettings` is `config = Game`. A whole dead `UProjectPackagingSettings` block in the wrong ini nearly shipped a device build with no map. Every new setting in this plan is `config = Game` and goes in `DefaultGame.ini` or nowhere.
- **RPC inputs are hostile.** Every client-supplied index is bounds-checked before it indexes anything; every distance is re-measured from the *server's* copy of the pawn. A client learns a container's contents only after a successful open, never before — that is the same rule the server-only `ASarkoRaidGameMode::LootSalt` exists to protect, and the reason container inventories live on the game mode (server-only by construction) rather than on the replicated game state.
- **No per-tick allocations.** `DrawHUD` is a tick path; every string it draws is cached on the value it displays (`CachedPrompt`, `CachedBackpackText`, …). Slate is *not* a tick path — the panel is rebuilt on a transfer, a few times per crate, never per frame — but `FCurveSequence::Play` registers an active timer, so no animation may loop.
- **Slate colours are LINEAR.** Every colour constant in this plan is written in linear space with its sRGB result named beside it. Linear 0.05 lands near sRGB 0.24, i.e. mid-grey — the shelter's first version was a flat grey slab because of exactly this.
- **Do not run `git checkout`, `git stash`, `git reset`** or anything that discards working-tree changes. **Never `git add -A`**; stage the exact paths each task names.

## Out of scope

Spatial packing, item rotation, drag-and-drop, weight, per-slot container restrictions, looting bots' bodies, and equipping anything other than a backpack (spec §6). Also out: **carrying a backpack from the shelter stash *into* a raid** — a bag found in a raid is submitted with the haul and lands in the stash, and re-equipping it from the shelter is shelter scope, named as the follow-up in Task 6 rather than silently missing.

---

## Visual design

Everything below is in **points on the 844×390 landscape canvas** (`SarkoUI::DesignWidthPt` / `DesignHeightPt`). One factor turns points into pixels; see "Scale" below. These are the numbers to implement, not a mood board.

### Where the panel lives, and why it is only a quarter of the screen

The spec's §5 risk is blunt: looting does not pause the world, so **a panel that covers the approach is how a player dies**. That is a layout requirement, not a taste one.

- **Panel: 216 pt wide × 292 pt tall**, anchored to the **bottom-right of `SarkoInput::SafeFrame`** — right edge 16 pt in from `Safe.Max.X`, bottom edge 20 pt up from `Safe.Max.Y`, growing **upward** as capacity grows. At full 12-cell capacity its top edge lands at y ≈ 78 pt; with 4 pocket cells only, at y ≈ 174 pt.
- **216 / 844 = 25.6 % of the width.** The pawn sits at the centre of a top-down camera, i.e. x ≈ 422 pt; the panel's left edge is at x ≈ 612 pt, so the pawn and **190 pt of world to its right** are never covered, along with the entire left three-quarters. A bot walking at you from any direction except due-right is visible the whole time.
- **The panel is translucent** and the world is **not dimmed.** The plate is alpha **0.86**; cells are alpha 0.92. `DrawOutcomeSummary` dims the whole canvas at 0.55 because it is a *final* screen and nothing behind it can still kill you. This is the opposite case and must not borrow that treatment: a bot crossing behind the panel has to remain a moving silhouette.
- **Bottom-anchored, not top-anchored**, so the panel never reaches the HUD's top row: the clock (top-centre) and the health bar (`Safe.Max.X − 150 − 16`, y 16–27) stay drawn and stay legible. Top-anchoring at full capacity would put the panel's header 3 pt under the health bar, which reads as a collision.
- **The bottom-left corner is untouched**, so the **movement stick still works** — the player can run with the panel open, which spec §4 requires. The **right** thumb loses its aim area while the panel is open; that is the honest trade and is stated in the code, not hidden.

### Grid and cells

| thing | value |
|---|---|
| grid columns | **4** |
| cell (visual, and the tap target) | **44 × 44 pt** |
| gutter | **4 pt** → 48 pt slot pitch |
| grid width | 4 × 44 + 3 × 4 = **188 pt** |
| panel inner padding | **14 pt** each side → 188 + 28 = **216 pt** ✔ |
| container grid | **1 row = 4 cells**, 44 pt tall |
| player grid | capacity ÷ 4 rows (**1 or 3**), 44 or 140 pt tall |
| panel corner radius | **14 pt** |
| cell corner radius | **8 pt** |
| cell outline | **1.5 pt**, category colour at full strength |
| empty-cell outline | **1 pt** |
| cell inner padding | **4 pt** |

**Why 4 container cells is enough, and why that is enforced rather than assumed.** `Data/Loot/loot-tables.json` caps rolls per tier at junk 2 / common 2 / med 2 / good 3 / **military 4**, and the tutorial's largest `fixedItems` list is 3. Four cells covers every container the game can currently produce. A future table that raised `rolls.max` to 5 would silently truncate a roll — a *new* vanishing-loot bug in the middle of fixing the old one — so Task 2 adds `Sarko.Loot.EveryTierFitsTheContainerGrid`, a test over the **real** tables asserting `MaxRolls <= SarkoLoot::ContainerCells` for every tier. Raising a table then turns a test red instead of eating loot.

**Vertical stack** (12 + 44 + 6 + 44 + 12 + 16 + 6 + 140 + 12 = **292 pt**):

```
┌ 14 pt padding ────────────────────────────────┐
│  [ ОБШУК · MILITARY        ЗАБРАТИ ВСЕ ]  44  │  ← one row, whole row is the take-all button
│  (6)                                          │
│  [cell][cell][cell][cell]                 44  │  ← container grid, 1 row of 4
│  ──────────── hairline ────────────       12  │
│  РЮКЗАК 7/12                              16  │  ← label only, no tap target
│  (6)                                          │
│  [cell][cell][cell][cell]                     │
│  [cell][cell][cell][cell]                140  │  ← player grid, 3 rows of 4 at 12 capacity
│  [cell][cell][cell][cell]                     │
└ 14 pt padding ────────────────────────────────┘
```

**There is no close button inside the panel.** The header row is already spent on take-all, and a third 44 pt target in 216 pt of width would crowd the grid. Instead the **existing interact button becomes the close button** while a panel is open: it is already 52 pt, already hit-tested against the same rect it is drawn in, and the thumb already knows where it is. It slides left of the panel (`x = PanelLeft − 16 − Size`, same vertical centre) so it is not underneath it, and its label changes from `E` to an **X drawn with two `DrawLine` calls** — no glyph, no font risk.

### The category palette — colour instead of icons

There are no icons and there will be none. Category colour is the substitute: the player should learn to scan the bag by hue and only read the label to disambiguate within a hue.

`items.json` carries `category` ∈ `weapon | ammo | med | junk | valuable | vehicle_part`, and Task 1 adds **`gear`** for the backpack (a bag is not junk, and putting it in an existing category would lie in the one place the palette is supposed to tell the truth). Seven values, six hues plus one deliberate neutral:

| category | sRGB | linear (write **this** in code) | hue | why |
|---|---|---|---|---|
| `Weapon` | `#C4453C` | `(0.552, 0.059, 0.045)` | 6° | red — the thing that kills |
| `Ammo` | `#D9A02E` | `(0.694, 0.352, 0.027)` | 41° | brass |
| `Gear` | `#8FB33E` | `(0.275, 0.451, 0.048)` | 78° | olive webbing |
| `Med` | `#3FBFA0` | `(0.050, 0.521, 0.352)` | 168° | clinical jade |
| `VehiclePart` | `#4E8FD6` | `(0.076, 0.275, 0.672)` | 210° | steel blue |
| `Valuable` | `#A971D8` | `(0.397, 0.165, 0.687)` | 275° | the genre's rarity violet |
| `Junk` | `#7A7F86` | `(0.195, 0.212, 0.238)` | — | **hueless on purpose**: junk being the only grey is information |

Minimum hue gap is 35° (red → brass), which survives the small-swatch, low-luminance, glare-on-a-phone case that kills 15° neighbours.

**Derivation, so a cell is legible against the plate and against its neighbours:**

- **cell outline = the category colour, unchanged**, at 1.5 pt. This is the load-bearing signal — an outline reads at 44 pt where a fill does not.
- **cell fill = category colour × 0.18**, alpha 0.92. Full-saturation fills on eight adjacent 44 pt cells is a carnival; a tinted dark body under a bright rim is a rack of gear.
- **the check that makes this work:** the dimmest fill is junk at 0.195 × 0.18 = **0.035 linear**, and the panel plate is **0.012–0.018 linear** — so even the dullest cell is roughly **2×** the plate's luminance and reads as a distinct object. Task 3 asserts this rather than eyeballing it.
- **empty cell:** fill `(0.020, 0.022, 0.026, 0.90)`, outline `(0.045, 0.050, 0.058, 0.90)` at 1 pt — visibly a slot, visibly not a thing.
- **panel plate:** fill `(0.012, 0.014, 0.018, 0.86)` (≈ `#1c1f22` at 86 %), outline `(0.050, 0.055, 0.065, 0.90)` at 1 pt, radius 14 pt.

### Type

Fonts are engine assets **by path only** — `FCoreStyle::GetDefaultFontStyle("Regular", Size)`, exactly as `SarkoShelterWidget.cpp` does. Sizes are in points on the same canvas as the shelter (title 26 / body 15), so a player who can read `В РЕЙД` can read this.

| element | size | colour (linear) |
|---|---|---|
| section header (`ОБШУК · MILITARY`, `РЮКЗАК 7/12`) | **11 pt** | `(0.22, 0.23, 0.25)` ≈ `#8b8f94` |
| `ЗАБРАТИ ВСЕ` | **12 pt** | `(1.0, 0.55, 0.06)` ≈ `#ffc16a` |
| cell label | **8.5 pt** | `(0.62, 0.64, 0.66)` ≈ `#d0d3d5` |
| cell quantity badge | **10 pt** | `(0.92, 0.92, 0.88)` ≈ `#f7f7f3` |
| header when the bag is full | 11 pt | `(1.0, 0.55, 0.06)` amber |

**Cell labels are derived, not authored.** A 44 pt cell with 4 pt padding has 36 pt of width — about seven Cyrillic glyphs at 8.5 pt. `Ящик з інструментами` does not fit and `items.json` has no short name. Rather than add a schema field, Task 3 adds the pure `SarkoUI::CellLabel(const FString& Name)`: **first word, uppercased, truncated to 9 characters with `…`**. `Патрони 9×18` → `ПАТРОНИ`, `Ящик з інструментами` → `ЯЩИК`, `Обезболювальне` → `ОБЕЗБОЛЮВ…`. Pure, so it is unit tested; derived, so no data file has to be kept in step.

### Scale, and the compounding trap

`SGameLayerManager` wraps the viewport overlay in **its own** `SDPIScaler` (`Engine/Private/Slate/SGameLayerManager.cpp:113`, fed by `GetGameViewportDPIScale` → `UUserInterfaceSettings::GetDPIScaleBasedOnSize`). With the engine's default `UIScaleRule=ShortestSide` and `UIScaleCurve` (`BaseEngine.ini:1475`: 1080→1.0, 8640→8.0), a 2556×1179 landscape phone has a shortest side of 1179 and so gets **1.092**. A widget that then applies its own `SDPIScaler(3.02)` renders at 3.30 — **~9 % larger than the points it claims**.

The in-raid HUD does **not** go through that path (it draws into the scene canvas, whose DPI scale is exactly 1 — see `UI/SarkoUiScale.h`). The panel is drawn **over the HUD** and must agree with it, so Task 3 adds:

```cpp
SarkoUI::OverlayPointScale(V) = PointScaleForViewport(V) / UUserInterfaceSettings::GetDPIScaleBasedOnSize(V)
```

and the panel's `SDPIScaler` uses that. The shelter menu keeps `PointScaleForViewport` and is therefore ~9 % over its stated size today; that is a **known, separate deviation** — it was validated by screenshot at that size, and changing it is a screenshot-gated change that does not belong in this plan.

### Motion

`FCurveSequence` animates Slate from C++ with no asset. `FCurveSequence(StartTime, Duration, ECurveEaseFunction)`, `Play(SharedThis(this))`, `GetLerp()`, `JumpToEnd()` — all confirmed in `SlateCore/Public/Animation/CurveSequence.h`. **Nothing loops** (`bPlayLooped` stays false), so no animation holds an active timer open.

| moment | duration | curve | what moves |
|---|---|---|---|
| panel entry | **140 ms** | `CubicOut` | slides 24 pt in from the right edge, fades 0 → 1 |
| panel exit | **90 ms** | `QuadIn` | fade only — leaving must feel instant |
| transfer | **120 ms** | `CubicOut` | receiving cell scales 0.86 → 1.0; its outline flashes white 0.5 → category colour |
| **refusal** | **240 ms** | `Linear` | see below |

Total on-screen motion is ≤ 240 ms. In a game where looting does not pause, a second of animation is a second you are standing still.

### Refusal must be visible

"I tapped and nothing happened" is the failure mode the spec calls out by name. When a take is refused, three things happen at once, and they are all on the *player's* half of the panel so the eye goes to the reason and not to the crate:

1. the **player grid's outline pulses amber** — `(1.0, 0.55, 0.06)` at `sin(π · lerp)` alpha, 240 ms;
2. the **`РЮКЗАК 12/12` header turns amber** and stays amber for as long as the bag is full (a state, not a flash — the HUD's backpack readout already uses amber for exactly this and the two must agree);
3. the **refused container cell shakes** ±4 pt horizontally, two cycles: `Offset = 4 · sin(4π · lerp)`.

The reason itself comes from the server as an enum on `ClientTransferRefused`, so a refusal for distance or a settled raid animates differently from a refusal for space — a shake with no amber means "you moved", and that distinction is the difference between retrying and understanding.

---

## File Structure

```
SarkoGame/
├── Config/DefaultGame.ini                       # T1: BasePocketCells, BackpackBonusCells
├── Data/
│   ├── Items/items.json                         # T1: "gear" category + the backpack item
│   ├── Loot/loot-tables.json                    # T6: backpack in good + military
│   └── Maps/bridge.json                         # T6: container 0 grants the backpack
├── Scripts/inventory-shot.sh                    # NEW (T4)
└── Source/SarkoGame/
    ├── Core/
    │   ├── SarkoRaidSettings.h                  # T1: cells replace BackpackSlots
    │   ├── SarkoRaidGameState.h/.cpp            # T1: 3-state container bytes
    │   ├── SarkoRaidGameMode.h/.cpp             # T2: ContainerInventories, OpenContainerAt
    │   └── SarkoPlayerController.h/.cpp         # T4: owns the panel; T4: interact→close
    ├── Loot/
    │   ├── SarkoItemCatalog.h/.cpp              # T1: ESarkoItemCategory::Gear
    │   ├── SarkoBackpack.h/.cpp                 # T1: worn backpack, capacity, haul
    │   ├── SarkoLootTable.h/.cpp                # T2: TransferOne in, CompleteLootChannel out
    │   └── SarkoLootContainer.h/.cpp            # T1: three lid tints
    ├── Pawn/SarkoCharacter.h/.cpp               # T2: take RPCs; channel becomes open
    ├── UI/
    │   ├── SarkoUiScale.h                       # T3: OverlayPointScale
    │   ├── SarkoInventoryStyle.h/.cpp           # NEW (T3): palette, CellLabel, brush cache
    │   ├── SarkoInventoryPanel.h/.cpp           # NEW (T4): SSarkoInventoryPanel
    │   └── SarkoHUD.h/.cpp                      # T4: suppress prompt, move+X the button
    └── Tests/
        ├── LootTest.cpp                         # T1 +2, T2 +4, T6 +2
        ├── ExtractionTest.cpp                   # T1 +2
        ├── InventoryUiTest.cpp                  # NEW (T3 +3, T4 +2, T5 +1)
        └── (Sarko.Loot.CompletedChannelCreditsThenMarksOnce is DELETED in T2)
sarko-api/internal/domain/loot.go                # T1: backpack id, MaxRaidStacks 13
```

`SarkoInventoryStyle.*` is separate from `SarkoInventoryPanel.*` because the palette and `CellLabel` are pure and must be testable under `-nullrhi`, where no Slate application exists and a widget cannot be constructed at all. `SarkoInventoryPanel.*` is separate from `SarkoHUD.*` because one is Slate and the other is canvas primitives, and the whole point of Task 3's `OverlayPointScale` is that those two coordinate systems are not the same.

---

### Task 1: Capacity is a worn backpack, not a constant

The model change, with no UI and no new wire traffic. After this task the raid plays **exactly as it does today** — the 1.5 s channel still empties a crate into the bag, loot still vanishes — except that the bag is 4 cells until you find a backpack, and the death path loses everything including the bag.

Doing this first because every later task's arithmetic depends on `GetSlotLimit()` no longer being a constant, and because it is the one task that touches the backend.

**Files:**
- Modify: `SarkoGame/Data/Items/items.json` (add `backpack`)
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h`, `.cpp` (`Gear`)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`
- Modify: `SarkoGame/Config/DefaultGame.ini`
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.cpp` (haul submission, ~line 656)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (+2), `Tests/ExtractionTest.cpp` (+2)
- Modify: `sarko-api/internal/domain/loot.go`

**Interfaces:**
- Consumes: `SarkoLoot::AddToBackpack`, `FSarkoItemCatalog::Find`, `USarkoRaidSettings`.
- Produces:
  - `ESarkoItemCategory::Gear` (appended **last** in the enum — it is a `uint8` UENUM and inserting in the middle renumbers everything).
  - `USarkoRaidSettings::BasePocketCells` (int32, 4) and `::BackpackBonusCells` (int32, 8). **`BackpackSlots` is removed.**
  - `SarkoLoot::BackpackItemId` — `extern const FName`, `TEXT("backpack")`.
  - `int32 SarkoLoot::CapacityFor(bool bBackpackWorn, int32 BaseCells, int32 BonusCells)` — pure.
  - `USarkoBackpackComponent::EquippedBackpack` (`FName`, replicated `COND_OwnerOnly`), `::IsWearingBackpack()`, `::EquipBackpack(FName)`, `::GetHaulForSubmission()`.
  - `USarkoBackpackComponent::ClearOnDeath()` now clears **both** `Slots` and `EquippedBackpack`.

- [ ] **Step 1: Record both baselines**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/sarko-api && go test ./... 2>&1 | tail -20
```
Expected: `ALL GREEN` and `==> B test(s) performed, 0 failed`; Go all `ok`. **Write `B` and `G` down in the task report.** Every later count is `B + delta`. If either is not green, stop and report — this work does not start on a red suite.

- [ ] **Step 2: Write the failing model tests**

Append inside the existing `#if WITH_AUTOMATION_TESTS` block of `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCapacityIsPocketsPlusAWornBackpack,
	"Sarko.Loot.CapacityIsPocketsPlusAWornBackpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCapacityIsPocketsPlusAWornBackpack::RunTest(const FString& Parameters)
{
	// Spec §2.3: four pocket cells, +8 for a found backpack, twelve total. The
	// twelve is deliberately today's number — the equipped case is the old
	// constant, so nothing about a full haul's plausibility changes at the
	// backend, and only the *start* of a raid got harder.
	TestEqual(TEXT("pockets alone"), SarkoLoot::CapacityFor(false, 4, 8), 4);
	TestEqual(TEXT("pockets plus a worn bag"), SarkoLoot::CapacityFor(true, 4, 8), 12);
	// Hostile config, not hostile input, but the arithmetic must not go negative:
	// a capacity below zero makes AddToBackpack's `Slots.Num() < SlotLimit` loop
	// condition trivially false in one place and trivially true in another.
	TestEqual(TEXT("negative settings floor at zero"), SarkoLoot::CapacityFor(true, -4, -8), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCatalogHasABackpackAndAGearCategory,
	"Sarko.Loot.CatalogHasABackpackAndAGearCategory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCatalogHasABackpackAndAGearCategory::RunTest(const FString& Parameters)
{
	// Over the REAL file, not a literal: the backpack id is the wire contract
	// with sarko-api's ItemStackSizes, and a fixture would pass while the
	// shipped catalog was missing it.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const FSarkoItemDef* Bag = Catalog.Find(SarkoLoot::BackpackItemId);
	if (!Bag)
	{
		AddError(TEXT("Data/Items/items.json has no 'backpack' item, so no raid can ever grant capacity"));
		return false;
	}
	TestEqual(TEXT("a backpack does not stack"), Bag->StackSize, 1);
	TestTrue(TEXT("a bag is gear, not junk — the palette must not lie about it"),
		Bag->Category == ESarkoItemCategory::Gear);
	return true;
}
```

Append to `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp` (it already builds a `USarkoBackpackComponent` with `NewObject` and calls `ClearOnDeath`, so the seam exists):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoDeathLosesThePocketsAndTheBag,
	"Sarko.Extract.DeathLosesThePocketsAndTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoDeathLosesThePocketsAndTheBag::RunTest(const FString& Parameters)
{
	// Spec §5: "everything carried is lost, per the game's core rule; only what
	// is in the shelter stash is safe." Pockets are NOT a safe pocket, and the
	// worn bag is not gear you keep — both go. This is the explicit decision the
	// spec asked to be made explicit, and it is the only place it is enforced.
	USarkoBackpackComponent* Backpack = NewObject<USarkoBackpackComponent>();
	Backpack->SetSlotsForTest({ FSarkoItemStack{ TEXT("medkit"), 2 } });
	Backpack->EquipBackpack(SarkoLoot::BackpackItemId);
	TestTrue(TEXT("the bag is on before death"), Backpack->IsWearingBackpack());

	Backpack->ClearOnDeath();

	TestEqual(TEXT("pockets are emptied"), Backpack->GetSlots().Num(), 0);
	TestFalse(TEXT("the worn bag is lost too"), Backpack->IsWearingBackpack());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHaulCarriesTheWornBagHome,
	"Sarko.Extract.HaulCarriesTheWornBagHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHaulCarriesTheWornBagHome::RunTest(const FString& Parameters)
{
	// A bag you extracted with is loot: it is submitted as one stack and lands
	// in the stash. Without this it would be the one thing a player carries out
	// of a raid and is never credited for, which reads as the game eating it —
	// the exact complaint this whole plan exists to end.
	USarkoBackpackComponent* Backpack = NewObject<USarkoBackpackComponent>();
	Backpack->SetSlotsForTest({ FSarkoItemStack{ TEXT("medkit"), 2 } });

	TestEqual(TEXT("no bag worn, no extra stack"), Backpack->GetHaulForSubmission().Num(), 1);

	Backpack->EquipBackpack(SarkoLoot::BackpackItemId);
	const TArray<FSarkoItemStack> Haul = Backpack->GetHaulForSubmission();
	TestEqual(TEXT("the worn bag is appended as its own stack"), Haul.Num(), 2);
	TestTrue(TEXT("and it is one backpack"),
		Haul.Last().Item == SarkoLoot::BackpackItemId && Haul.Last().Quantity == 1);
	return true;
}
```

- [ ] **Step 3: Run them and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: **BUILD FAILED**, with `SarkoLoot::CapacityFor`, `BackpackItemId`, `EquipBackpack`, `IsWearingBackpack`, `GetHaulForSubmission` and `ESarkoItemCategory::Gear` all undeclared. A build failure is the correct red here — these are compile-time symbols, not runtime behaviour.

- [ ] **Step 4: Add the `gear` category and the backpack item**

In `Loot/SarkoItemCatalog.h`, append to the enum (**last**, never inserted):

```cpp
enum class ESarkoItemCategory : uint8
{
	Weapon,
	Ammo,
	Med,
	Junk,
	Valuable,
	VehiclePart,
	/**
	 * Worn equipment. Today that is exactly one thing — the backpack — and it is
	 * its own category rather than `junk` because the panel paints a cell by its
	 * category and this is the one item whose colour has to say "this is not
	 * cargo, this is what carries the cargo". Appended last on purpose: this is a
	 * uint8 UENUM and inserting in the middle renumbers every value above it.
	 */
	Gear
};
```

In `Loot/SarkoItemCatalog.cpp`, `ParseCategory`'s map gains one line and the error message gains one word:

```cpp
			{ TEXT("vehicle_part"), ESarkoItemCategory::VehiclePart },
			{ TEXT("gear"),         ESarkoItemCategory::Gear },
```
```cpp
				TEXT("item '%s': 'category' must be weapon, ammo, med, junk, valuable, vehicle_part or gear"), *Id);
```

In `Data/Items/items.json`, append one item after `chain` (keep the trailing entry's comma discipline):

```json
    { "id": "chain",        "name": "Ланцюг",              "stackSize": 1,  "category": "vehicle_part" },
    { "id": "backpack",     "name": "Рюкзак",              "stackSize": 1,  "category": "gear" }
```

- [ ] **Step 5: Settings — cells, not slots**

In `Core/SarkoRaidSettings.h`, **replace** the `BackpackSlots` property with:

```cpp
	/**
	 * Pocket cells, carried always (spec §2.3). Four is small on purpose: it is
	 * the number that makes finding a backpack matter, and it is the number a
	 * player has for the first minute of every raid.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	int32 BasePocketCells = 4;

	/**
	 * What a worn backpack adds. 4 + 8 = 12, which is exactly the old fixed
	 * BackpackSlots — so the backend's plausibility cap on a full haul does not
	 * move, and only the *start* of a raid got harder. Raising this without
	 * raising sarko-api's domain.MaxRaidStacks makes full hauls get rejected at
	 * result time, fifteen minutes after the mistake.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	int32 BackpackBonusCells = 8;
```

In `Config/DefaultGame.ini`, under the existing `[/Script/SarkoGame.SarkoRaidSettings]` section, replace any `BackpackSlots=` line with:

```ini
BasePocketCells=4
BackpackBonusCells=8
```

If no `BackpackSlots=` line exists there, add the two lines anyway — the C++ defaults already agree, and having them written down is what makes the dial findable.

- [ ] **Step 6: The backpack component learns to wear a bag**

In `Loot/SarkoBackpack.h`, inside `namespace SarkoLoot`:

```cpp
	/** The one gear id that grants capacity. Named once, because it is compared
	 *  against on the take path and must never be a loose literal. */
	extern const FName BackpackItemId;

	/**
	 * How many cells a pawn has. Pure, so the single number the whole economy
	 * turns on is unit tested with no world and no settings object.
	 *
	 * Clamped at zero: a negative capacity makes AddToBackpack's
	 * `Slots.Num() < SlotLimit` loop guard behave inconsistently across the two
	 * places capacity is read, which is a haul that half-fits.
	 */
	int32 CapacityFor(bool bBackpackWorn, int32 BaseCells, int32 BonusCells);
```

and on the component, replacing nothing else:

```cpp
	bool IsWearingBackpack() const { return EquippedBackpack != NAME_None; }

	/** Server only. Wearing, not carrying: the bag does not occupy a cell — spec
	 *  §2.3's 4 + 8 = 12 only works if it does not. */
	void EquipBackpack(FName Item);

	/**
	 * What gets submitted to /v1/raid/result: the cells, plus the worn bag as
	 * one stack if there is one. A bag you extracted with is loot, and the one
	 * thing a player could otherwise carry out and never be credited for.
	 */
	TArray<FSarkoItemStack> GetHaulForSubmission() const;

private:
	/**
	 * The worn backpack's item id, or NAME_None. COND_OwnerOnly for the same
	 * reason Slots is: how much another player can carry is part of how much
	 * they are worth killing.
	 */
	UPROPERTY(Replicated)
	FName EquippedBackpack;
```

In `Loot/SarkoBackpack.cpp`:

```cpp
const FName SarkoLoot::BackpackItemId(TEXT("backpack"));

int32 SarkoLoot::CapacityFor(bool bBackpackWorn, int32 BaseCells, int32 BonusCells)
{
	const int32 Base = FMath::Max(0, BaseCells);
	const int32 Bonus = bBackpackWorn ? FMath::Max(0, BonusCells) : 0;
	return Base + Bonus;
}
```
```cpp
void USarkoBackpackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(USarkoBackpackComponent, Slots, COND_OwnerOnly);
	// Registered, or it silently never replicates and the owning client's panel
	// draws four cells for a pawn the server thinks has twelve.
	DOREPLIFETIME_CONDITION(USarkoBackpackComponent, EquippedBackpack, COND_OwnerOnly);
}

int32 USarkoBackpackComponent::GetSlotLimit() const
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	return SarkoLoot::CapacityFor(IsWearingBackpack(), Settings.BasePocketCells, Settings.BackpackBonusCells);
}

void USarkoBackpackComponent::EquipBackpack(FName Item)
{
	if (const AActor* Owner = GetOwner(); Owner && !Owner->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoBackpack: EquipBackpack called without authority; ignored"));
		return;
	}
	EquippedBackpack = Item;
}

TArray<FSarkoItemStack> USarkoBackpackComponent::GetHaulForSubmission() const
{
	TArray<FSarkoItemStack> Haul = Slots;
	if (IsWearingBackpack())
	{
		Haul.Add(FSarkoItemStack{ EquippedBackpack, 1 });
	}
	return Haul;
}
```

and `ClearOnDeath` gains one line after `Slots.Reset()`:

```cpp
	Slots.Reset();
	// The bag goes with the pockets. Spec §5's default, made explicit: everything
	// carried is lost, and a bag that survived death would be the one piece of
	// gear the core rule did not apply to.
	EquippedBackpack = NAME_None;
```

- [ ] **Step 7: The haul submission carries the bag**

In `Core/SarkoRaidGameMode.cpp`, at the haul-collection block (~line 656), change the one line:

```cpp
					// Not GetSlots(): a worn backpack is not in the cells, and
					// submitting the cells alone would silently drop the one item
					// the player most obviously carried out.
					Haul = Pawn->BackpackComponent->GetHaulForSubmission();
```

- [ ] **Step 8: The backend learns the id and the new ceiling**

In `sarko-api/internal/domain/loot.go`, add to `ItemStackSizes`:

```go
	// Worn equipment. A backpack is submitted as a stack of one when the player
	// extracts wearing it, so the id has to exist here or the whole haul is
	// rejected at result time — which is how the client's catalog and this map
	// are kept honest by loot_test.go's drift alarm.
	"backpack": 1,
```

and change the ceiling:

```go
// MaxRaidStacks is the most item stacks one raid can deliver: twelve carried
// cells (4 pockets + 8 from a worn backpack, spec §2.3) plus the worn backpack
// itself, which rides home as a thirteenth stack without ever occupying a cell.
// A result claiming fourteen describes a raid that could not have happened.
const MaxRaidStacks = 13
```

`loot_test.go`'s `TestKnownItemsMatchTheClientCatalog` reads `items.json` and now passes with the new id. Any test that asserts `MaxRaidStacks == 12` by literal must be updated to 13; search for it:

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/sarko-api && grep -rn "MaxRaidStacks" internal/
```

- [ ] **Step 9: Run both suites green**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/sarko-api && go test ./...
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: Go all `ok` at `G` tests; UE `ALL GREEN` with `B + 4`.

- [ ] **Step 10: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Items/items.json SarkoGame/Config/DefaultGame.ini SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.cpp SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.cpp SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.cpp SarkoGame/Source/SarkoGame/Tests/LootTest.cpp SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp sarko-api/internal/domain/loot.go && git commit -m "feat(loot): capacity is four pockets and a backpack you can lose"
```

---

### Task 2: Contents live in the container, and the loot stops vanishing

**This is where the defect dies.** Today `ASarkoCharacter::TickLootChannel` (`Pawn/SarkoCharacter.cpp:379`) calls `SarkoLoot::CompleteLootChannel`, whose `Mark()` runs **unconditionally** (`Loot/SarkoLootTable.cpp:323`) — so whatever `AddToBackpack` refused for lack of space is destroyed along with the container. That function's contract ("mark unconditionally, because a partly-emptied container would re-run the same deterministic roll") only made sense while the roll was re-derived rather than stored. Once contents are stored, the whole problem it solved evaporates: **`CompleteLootChannel` is deleted**, along with `Sarko.Loot.CompletedChannelCreditsThenMarksOnce`.

The invariant it protected — one roll can never be credited twice — is not weakened but replaced by a stronger one: a transfer **removes from the container in the same operation that adds to the backpack**, so there is nothing left to credit a second time. Double-credit becomes impossible by construction rather than by a gate.

After this task the raid is playable and strictly better than today: the channel opens a container and the server immediately runs take-all, so looting *feels* identical — except the remainder now stays in the crate and the crate stays openable. Task 4 removes the automatic take-all when there is a panel to replace it.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`, `.cpp` (`TransferOne` in, `CompleteLootChannel` out)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameState.h`, `.cpp` (3-state container bytes)
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoLootContainer.h`, `.cpp` (`CanInteract` param, three lid tints)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidGameMode.h`, `.cpp` (`ContainerInventories`, `OpenContainerAt`)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp` (RPCs, channel becomes open)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp` (`CanInteract` call site)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (+5, −1)

**Interfaces:**
- Consumes: `SarkoLoot::AddToBackpack`, `SarkoLoot::RollContainerFor`, `SarkoLoot::ContainerSeed`, `USarkoBackpackComponent::GetSlotLimit`, `SarkoLoot::CapacityFor`, `SarkoLoot::BackpackItemId`.
- Produces:
  - `constexpr int32 SarkoLoot::ContainerCells = 4;`
  - `int32 SarkoLoot::TransferOne(TArray<FSarkoItemStack>& Container, int32 SlotIndex, TArray<FSarkoItemStack>& Bag, const FSarkoItemCatalog& Catalog, int32 BagLimit)` — units moved; 0 means refused.
  - `enum class ESarkoContainerState : uint8 { Closed = 0, Opened = 1, Emptied = 2 };`
  - `ASarkoRaidGameState::GetContainerState(int32)`, `::IsContainerOpened(int32)`, `::IsContainerEmptied(int32)`, `::SetContainerState(int32, ESarkoContainerState)`. **`IsContainerLooted` and `MarkContainerLooted` are removed**; every call site moves to `IsContainerEmptied` / `SetContainerState`.
  - `SarkoLoot::CanInteract(..., bool bContainerEmptied)` — last parameter renamed, same semantics inverted from "already looted".
  - `ASarkoRaidGameMode::TArray<FSarkoItemStack>* FindContainerInventory(int32)`, `::OpenContainerAt(int32)`.
  - `enum class ESarkoTakeRefusal : uint8 { NoSpace, TooFar, NotOpen, Gone, RaidOver };`
  - On `ASarkoCharacter`: `ServerTakeItem(int32, int32)`, `ServerTakeAll(int32)`, `ServerCloseContainer()`, `ClientContainerContents(int32, const TArray<FSarkoItemStack>&)`, `ClientContainerClosed(int32)`, `ClientTransferRefused(int32, int32, ESarkoTakeRefusal)`, `GetOpenContainerIndex()`, `GetOpenContainerSlots()`.

- [ ] **Step 1: Write the failing transfer tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTransferMovesWhatFitsAndLeavesTheRest,
	"Sarko.Loot.TransferMovesWhatFitsAndLeavesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTransferMovesWhatFitsAndLeavesTheRest::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	TestTrue(TEXT("fixture catalog parses"), SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error));

	// The whole defect, in one assertion. ammo_9mm stacks 60; the bag has one
	// free cell; the container holds 100. Sixty move, FORTY STAY IN THE CRATE.
	// Before this function existed the forty were destroyed, because the
	// container was marked looted whether or not the haul fitted.
	TArray<FSarkoItemStack> Container = { FSarkoItemStack{ TEXT("ammo_9mm"), 100 } };
	TArray<FSarkoItemStack> Bag;
	const int32 Moved = SarkoLoot::TransferOne(Container, 0, Bag, Catalog, /*BagLimit*/ 1);

	TestEqual(TEXT("one full stack moved"), Moved, 60);
	TestEqual(TEXT("the bag holds it"), Bag.Num(), 1);
	TestEqual(TEXT("the remainder is still in the container"), Container.Num(), 1);
	TestEqual(TEXT("and it is exactly what did not fit"), Container[0].Quantity, 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTransferEmptiesTheSlotItDrains,
	"Sarko.Loot.TransferEmptiesTheSlotItDrains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTransferEmptiesTheSlotItDrains::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error);

	// A drained slot is REMOVED, not left at quantity zero. A zero-quantity slot
	// would draw as an occupied cell the player can tap forever, which is the
	// same "I tapped and nothing happened" the refusal animation exists to end.
	TArray<FSarkoItemStack> Container = {
		FSarkoItemStack{ TEXT("medkit"), 1 },
		FSarkoItemStack{ TEXT("pistol"), 1 },
	};
	TArray<FSarkoItemStack> Bag;
	TestEqual(TEXT("the medkit moves whole"), SarkoLoot::TransferOne(Container, 0, Bag, Catalog, 4), 1);
	TestEqual(TEXT("one slot left"), Container.Num(), 1);
	TestTrue(TEXT("and it is the pistol — indices shift, so the panel rebuilds from the new array"),
		Container[0].Item == FName(TEXT("pistol")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTransferRefusesRatherThanEatsInput,
	"Sarko.Loot.TransferRefusesRatherThanEatsInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTransferRefusesRatherThanEatsInput::RunTest(const FString& Parameters)
{
	FSarkoItemCatalog Catalog;
	FString Error;
	SarkoLoot::ParseItemCatalog(GoodCatalogJson, Catalog, Error);

	TArray<FSarkoItemStack> Container = { FSarkoItemStack{ TEXT("pistol"), 1 } };
	TArray<FSarkoItemStack> Full = { FSarkoItemStack{ TEXT("medkit"), 3 } };

	// Zero moved is the signal the refusal animation reads. Both sides must be
	// byte-identical afterwards — a "refusal" that quietly moved one unit is
	// worse than one that moved none.
	TestEqual(TEXT("a full bag refuses"), SarkoLoot::TransferOne(Container, 0, Full, Catalog, 1), 0);
	TestEqual(TEXT("the container is untouched"), Container.Num(), 1);
	TestEqual(TEXT("the bag is untouched"), Full.Num(), 1);

	// Hostile indices. This function is one call away from an RPC parameter.
	TestEqual(TEXT("negative index"), SarkoLoot::TransferOne(Container, -1, Full, Catalog, 8), 0);
	TestEqual(TEXT("out of range"), SarkoLoot::TransferOne(Container, 99, Full, Catalog, 8), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoContainerStateHasThreeMeanings,
	"Sarko.Loot.ContainerStateHasThreeMeanings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoContainerStateHasThreeMeanings::RunTest(const FString& Parameters)
{
	// Opened-but-not-empty is a NEW state and the reason a crate you walked away
	// from is still worth walking back to. CanInteract must gate on emptied, not
	// on opened, or the remainder this whole task preserves is unreachable.
	TestTrue(TEXT("a closed container is openable"),
		SarkoLoot::CanInteract(FVector::ZeroVector, FVector::ZeroVector, 250.f, true, /*bEmptied*/ false));
	TestFalse(TEXT("an emptied one is not"),
		SarkoLoot::CanInteract(FVector::ZeroVector, FVector::ZeroVector, 250.f, true, /*bEmptied*/ true));
	TestFalse(TEXT("nor is anything, to a corpse"),
		SarkoLoot::CanInteract(FVector::ZeroVector, FVector::ZeroVector, 250.f, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEveryTierFitsTheContainerGrid,
	"Sarko.Loot.EveryTierFitsTheContainerGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEveryTierFitsTheContainerGrid::RunTest(const FString& Parameters)
{
	// The container grid is four cells, sized to the loudest tier the shipped
	// tables can produce (military, rolls.max 4). Raising a table's rolls in the
	// data file without widening the grid would truncate a roll — i.e. reinvent
	// the vanishing-loot defect on the other side of the fix. So it fails here,
	// in a test over the REAL file, instead of quietly in a raid.
	for (const FSarkoLootTable& Table : SarkoLoot::GetLootTables().Tables)
	{
		TestTrue(*FString::Printf(TEXT("tier '%s' rolls at most %d, and the grid holds %d"),
				*Table.Tier.ToString(), Table.MaxRolls, SarkoLoot::ContainerCells),
			Table.MaxRolls <= SarkoLoot::ContainerCells);
	}
	return true;
}
```

**Delete** `FSarkoCompletedChannelCreditsThenMarksOnce` / `Sarko.Loot.CompletedChannelCreditsThenMarksOnce` from the same file. Its contract is gone; leaving it would be a green test for a function that no longer exists.

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: **BUILD FAILED** on `SarkoLoot::TransferOne` and `SarkoLoot::ContainerCells` being undeclared.

- [ ] **Step 3: `TransferOne`, and the removal of `CompleteLootChannel`**

In `Loot/SarkoLootTable.h`, **remove** `struct FSarkoLootPayout` and the `CompleteLootChannel` declaration entirely, and add:

```cpp
	/**
	 * How many cells a container shows. Four, because the loudest shipped tier
	 * (military) rolls at most four times and the tutorial's longest authored
	 * list is three. Sarko.Loot.EveryTierFitsTheContainerGrid holds the line: a
	 * table that outgrows this fails a test instead of losing a roll.
	 */
	constexpr int32 ContainerCells = 4;

	/**
	 * Moves as much of one container slot into a backpack as fits, and returns
	 * how many units actually moved. Zero means refused, which is what the UI's
	 * refusal animation reads.
	 *
	 * Pure — arrays in, arrays out, no component, no world, no settings — because
	 * this is now the ONLY place loot changes hands, and the invariant that used
	 * to belong to CompleteLootChannel lives here instead:
	 *
	 *  - the units added to the bag are removed from the container in the same
	 *    call, so one roll cannot be credited twice. The old function needed a
	 *    bAlreadyLooted gate for that; there is nothing left here to gate,
	 *    because there is nothing left to credit;
	 *  - **a partial move leaves the remainder in the slot.** This is the fix for
	 *    the vanishing-loot defect: CompleteLootChannel marked a container looted
	 *    unconditionally, so whatever AddToBackpack refused was destroyed.
	 *    Nothing here destroys anything; the container is marked emptied only
	 *    when its last slot is actually gone;
	 *  - a drained slot is removed rather than left at quantity zero, so the
	 *    panel never draws a cell that cannot be tapped.
	 *
	 * SlotIndex is one hop from an RPC parameter and is bounds-checked here as
	 * well as at the call site — the call site can be forgotten; this cannot.
	 */
	int32 TransferOne(TArray<FSarkoItemStack>& Container, int32 SlotIndex,
		TArray<FSarkoItemStack>& Bag, const FSarkoItemCatalog& Catalog, int32 BagLimit);
```

In `Loot/SarkoLootTable.cpp`, delete the whole `CompleteLootChannel` body and add:

```cpp
int32 SarkoLoot::TransferOne(TArray<FSarkoItemStack>& Container, int32 SlotIndex,
	TArray<FSarkoItemStack>& Bag, const FSarkoItemCatalog& Catalog, int32 BagLimit)
{
	if (!Container.IsValidIndex(SlotIndex))
	{
		return 0;
	}

	FSarkoItemStack& Slot = Container[SlotIndex];
	if (Slot.Quantity <= 0)
	{
		return 0;
	}

	// AddToBackpack is unchanged and still the only thing that knows about
	// stacking and cell limits. It returns the remainder, which is exactly what
	// stays in the crate.
	const int32 Leftover = FMath::Clamp(
		SarkoLoot::AddToBackpack(Bag, Catalog, BagLimit, Slot.Item, Slot.Quantity), 0, Slot.Quantity);
	const int32 Moved = Slot.Quantity - Leftover;
	if (Moved <= 0)
	{
		return 0;
	}

	Slot.Quantity = Leftover;
	if (Slot.Quantity <= 0)
	{
		// RemoveAt and not RemoveAtSwap: the panel draws these in order, and a
		// swap would make an untouched cell jump across the grid while the
		// player is looking at it.
		Container.RemoveAt(SlotIndex);
	}
	return Moved;
}
```

- [ ] **Step 4: Three container states, replicated**

In `Core/SarkoRaidGameState.h`, above the class:

```cpp
/**
 * What has happened to one container. Replicated as one byte per index in
 * ASarkoRaidGameState::LootedContainers.
 *
 * Three values and not two because "opened" and "emptied" stopped being the
 * same event: a container whose contents did not fit is opened, still holds
 * something, and is still worth coming back to. Collapsing them is precisely
 * the bug this replaces — the old single bit meant "looted", was set whether or
 * not the haul fitted, and took the remainder with it.
 */
UENUM()
enum class ESarkoContainerState : uint8
{
	Closed = 0,
	Opened = 1,
	Emptied = 2
};
```

Replace `IsContainerLooted` / `MarkContainerLooted` on the class with:

```cpp
	/** Closed for an out-of-range index — a client-supplied index is never trusted to be in range. */
	ESarkoContainerState GetContainerState(int32 ContainerIndex) const;

	bool IsContainerOpened(int32 ContainerIndex) const
	{
		return GetContainerState(ContainerIndex) != ESarkoContainerState::Closed;
	}

	/** The gate CanInteract reads. An opened-but-not-empty crate is still openable. */
	bool IsContainerEmptied(int32 ContainerIndex) const
	{
		return GetContainerState(ContainerIndex) == ESarkoContainerState::Emptied;
	}

	/** Server only. Bounds-checked; logs and does nothing for a bad index. */
	void SetContainerState(int32 ContainerIndex, ESarkoContainerState State);
```

In `Core/SarkoRaidGameState.cpp`, implement them over the same `LootedContainers` byte array (`static_cast<ESarkoContainerState>(LootedContainers[Index])` / `static_cast<uint8>(State)`), keeping the existing bounds checks and the existing `OnRep_LootedContainers` behaviour verbatim.

- [ ] **Step 5: `CanInteract`'s last parameter, and three lid tints**

In `Loot/SarkoLootContainer.h`/`.cpp`, rename `bAlreadyLooted` → `bContainerEmptied` in the declaration, the definition and the doc comment (the logic is unchanged — an emptied container refuses). Then `ASarkoLootContainer` gains a third tint:

```cpp
	/** Closed: rusted olive. Opened but not empty: warm amber — you left something
	 *  in there, and from across a yard that is worth knowing. Emptied: washed
	 *  out, so a cleared route reads at a glance. */
	const FLinearColor ClosedTint(0.28f, 0.30f, 0.16f);
	const FLinearColor OpenedTint(0.42f, 0.26f, 0.05f);
	const FLinearColor LootedTint(0.42f, 0.42f, 0.44f);
```

and `RefreshVisualState` switches on `GetContainerState(ContainerIndex)`. `IsLooted()` becomes `IsEmptied()`; update the one call in `RefreshVisualState` and the one in `Core/SarkoPlayerController.cpp`'s `UpdateInteract` loop (`RaidState->IsContainerLooted(...)` → `RaidState->IsContainerEmptied(...)`).

- [ ] **Step 6: Contents live on the game mode**

In `Core/SarkoRaidGameMode.h`, next to `LootSalt` and `bTutorialLoot` and for the same stated reason:

```cpp
	/**
	 * Every container that has been opened this raid, keyed by container index,
	 * holding what is still in it.
	 *
	 * On the game mode, and a plain member rather than a UPROPERTY, for exactly
	 * the reasons LootSalt is: a game mode exists only on the server, so there is
	 * no replication path to forget to exclude and nothing that walks reflected
	 * properties can dump it. Spec §3's "a client sees a container's contents
	 * only after opening it, never before" is therefore a structural fact rather
	 * than a rule someone has to remember — the only way contents reach a client
	 * is ASarkoCharacter::ClientContainerContents, and only for an index that
	 * client has successfully opened.
	 *
	 * A TMap and not a sized array: key presence IS "this has been rolled", which
	 * is the spec's "rolled once, on first open" with no second flag to keep in
	 * step and no sizing hook to get wrong.
	 */
	TMap<int32, TArray<FSarkoItemStack>> ContainerInventories;

	/** Server only. Null for an index that has never been opened. */
	TArray<FSarkoItemStack>* FindContainerInventory(int32 ContainerIndex);

	/**
	 * Rolls a container's contents if this is its first open, stores them, and
	 * marks it Opened. Returns null and logs if the index is out of range or the
	 * tier has no table. Idempotent: a second open returns the SAME array, so a
	 * player who walks away and comes back sees what they left.
	 */
	TArray<FSarkoItemStack>* OpenContainerAt(int32 ContainerIndex);
```

In `Core/SarkoRaidGameMode.cpp`, the roll moves here from `TickLootChannel` **unchanged**, salt and tutorial branch and all:

```cpp
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

	// Opened, NOT Emptied. An empty roll is settled below by the caller's
	// take-all; an empty container marks itself emptied on the transfer path.
	RaidState->SetContainerState(ContainerIndex, ESarkoContainerState::Opened);
	return &ContainerInventories.Add(ContainerIndex, MoveTemp(Rolled));
}
```

- [ ] **Step 7: The channel opens; taking is its own request**

In `Pawn/SarkoCharacter.h` add the refusal enum above the class and the members/RPCs on it:

```cpp
/** Why a take did nothing. Sent to exactly one client so the panel can say which. */
UENUM()
enum class ESarkoTakeRefusal : uint8
{
	NoSpace,
	TooFar,
	NotOpen,
	Gone,
	RaidOver
};
```
```cpp
	/** Client intent: take one container slot. Every field is validated server-side. */
	void RequestTakeItem(int32 ContainerIndex, int32 SlotIndex);
	void RequestTakeAll(int32 ContainerIndex);
	void RequestCloseContainer();

	/** INDEX_NONE when no panel should be up. The client's own mirror, filled by RPC. */
	int32 GetOpenContainerIndex() const { return LocalOpenContainerIndex; }
	const TArray<FSarkoItemStack>& GetOpenContainerSlots() const { return LocalOpenContainerSlots; }

	/** Fires on the owning client whenever the open container's contents change,
	 *  including the first time they arrive. The panel subscribes to this. */
	DECLARE_MULTICAST_DELEGATE(FSarkoContainerViewChanged);
	FSarkoContainerViewChanged OnContainerViewChanged;

	/** Fires on the owning client when a take was refused, with the reason. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FSarkoTakeRefused, int32 /*SlotIndex*/, ESarkoTakeRefusal);
	FSarkoTakeRefused OnTakeRefused;

private:
	UFUNCTION(Server, Reliable) void ServerTakeItem(int32 ContainerIndex, int32 SlotIndex);
	UFUNCTION(Server, Reliable) void ServerTakeAll(int32 ContainerIndex);
	UFUNCTION(Server, Reliable) void ServerCloseContainer();

	/**
	 * The one channel through which container contents reach a client, and only
	 * for a container that client has successfully opened (spec §3). A client RPC
	 * rather than replicated state because the container has no net identity — it
	 * is spawned locally on every machine from the map file — and because the
	 * fact is per-client, per-moment, per-container, which is exactly an RPC's
	 * shape. Reliable: a dropped update leaves the panel lying about a crate.
	 */
	UFUNCTION(Client, Reliable) void ClientContainerContents(int32 ContainerIndex, const TArray<FSarkoItemStack>& Slots);
	UFUNCTION(Client, Reliable) void ClientContainerClosed(int32 ContainerIndex);
	UFUNCTION(Client, Reliable) void ClientTransferRefused(int32 ContainerIndex, int32 SlotIndex, ESarkoTakeRefusal Reason);

	/** Server truth: which container this pawn has open, or INDEX_NONE. */
	int32 OpenContainerIndex = INDEX_NONE;

	/** The client's mirror, and the panel's only data source. */
	int32 LocalOpenContainerIndex = INDEX_NONE;
	TArray<FSarkoItemStack> LocalOpenContainerSlots;
```

In `Pawn/SarkoCharacter.cpp`, `TickLootChannel`'s completion block (everything from `const FSarkoLootTable* Table = ...` to the end) is **replaced** by:

```cpp
	// Channel complete. The 1.5 s buys an OPEN, not a haul: opening is the risk,
	// taking is fast (spec §4). Nothing is transferred here any more, which is
	// why CompleteLootChannel — and its unconditional Mark, which destroyed the
	// remainder — no longer exists.
	const int32 Index = LootChannelIndex;
	LootChannelIndex = INDEX_NONE;
	OpenContainerFor(Index);
```

and the shared open path, used both by the channel and by the instant re-open:

```cpp
void ASarkoCharacter::OpenContainerFor(int32 ContainerIndex)
{
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}
	TArray<FSarkoItemStack>* Inventory = GameMode->OpenContainerAt(ContainerIndex);
	if (!Inventory)
	{
		return;
	}

	OpenContainerIndex = ContainerIndex;

	// TEMPORARY, removed in Task 4 when the panel exists. Until then the raid has
	// to stay playable, so opening still hauls — but through TransferOne, so the
	// remainder stays in the crate. That single difference is the defect fixed.
	TakeAllFrom(ContainerIndex);

	ClientContainerContents(ContainerIndex, *Inventory);
}
```

`ServerBeginLoot_Implementation` gains one branch after its `CanInteract` gate, so a crate whose 1.5 s has already been paid opens instantly:

```cpp
	if (RaidState->IsContainerOpened(ContainerIndex))
	{
		// The channel is the price of DISCOVERY, and it has been paid. Re-opening
		// a crate you already emptied halfway must not cost another second and a
		// half of standing still in the open.
		OpenContainerFor(ContainerIndex);
		return;
	}
```

The take path, with spec §3's validation order **verbatim and in order**:

```cpp
void ASarkoCharacter::ServerTakeItem_Implementation(int32 ContainerIndex, int32 SlotIndex)
{
	// Spec §3's order, unchanged and not to be reordered: raid live and not
	// settled -> index in range -> container opened -> pawn alive -> server
	// re-measured distance -> slot non-empty -> capacity available. Anything that
	// fails logs and changes nothing. This mirrors ServerBeginLoot, which is
	// already hostile-input safe, and every index below is checked before it
	// indexes anything at all.
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!GameMode || !RaidState || !RaidState->IsLootable())
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::RaidOver);
		return;
	}

	const TArray<FSarkoLootContainerSpot>& Spots = GameMode->CachedDefinition.Containers;
	if (!Spots.IsValidIndex(ContainerIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoCharacter: take from out-of-range container %d (have %d)"),
			ContainerIndex, Spots.Num());
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::Gone);
		return;
	}

	TArray<FSarkoItemStack>* Inventory = GameMode->FindContainerInventory(ContainerIndex);
	if (!Inventory)
	{
		// Never opened. A client asking to take from a container it has not
		// opened is asking for the loot map; it gets nothing and it learns nothing.
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::NotOpen);
		return;
	}

	const bool bAlive = HealthComponent && !HealthComponent->IsDead();
	// The server's OWN copy of this pawn's location, re-measured. A client-supplied
	// position would be pointless to send and pointless to trust, exactly as in
	// ServerRequestFire.
	if (!SarkoLoot::CanInteract(GetActorLocation(), Spots[ContainerIndex].Location,
			GetDefault<USarkoRaidSettings>()->InteractRadiusUU, bAlive,
			RaidState->IsContainerEmptied(ContainerIndex)))
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::TooFar);
		return;
	}

	if (!Inventory->IsValidIndex(SlotIndex))
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::Gone);
		return;
	}

	// A backpack is worn, not carried: it does not occupy a cell, which is the
	// only way spec §2.3's 4 + 8 = 12 adds up. A SECOND backpack is ordinary
	// loot and takes a cell like anything else — it is worth carrying home.
	if ((*Inventory)[SlotIndex].Item == SarkoLoot::BackpackItemId
		&& BackpackComponent && !BackpackComponent->IsWearingBackpack())
	{
		BackpackComponent->EquipBackpack(SarkoLoot::BackpackItemId);
		Inventory->RemoveAt(SlotIndex);
		FinishTransfer(ContainerIndex, *Inventory);
		return;
	}

	TArray<FSarkoItemStack> Bag = BackpackComponent ? BackpackComponent->GetSlots() : TArray<FSarkoItemStack>();
	const int32 Moved = SarkoLoot::TransferOne(*Inventory, SlotIndex, Bag,
		SarkoLoot::GetItemCatalog(),
		BackpackComponent ? BackpackComponent->GetSlotLimit() : 0);
	if (Moved <= 0)
	{
		ClientTransferRefused(ContainerIndex, SlotIndex, ESarkoTakeRefusal::NoSpace);
		return;
	}
	BackpackComponent->SetSlotsForTest(Bag);   // server-side write-back; see the note below
	FinishTransfer(ContainerIndex, *Inventory);
}
```

> **Note for the implementer:** `SetSlotsForTest` is currently named as a test seam. Rename it to `SetSlots` in this step (declaration, definition, and the two `ExtractionTest.cpp` / `ShelterTest.cpp` call sites) and keep the comment about it being a seam — it is now also the server's write-back path and a name that says "for test" on a production path is a lie. Grep first:
> ```bash
> cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && grep -rn "SetSlotsForTest" Source/
> ```

```cpp
void ASarkoCharacter::FinishTransfer(int32 ContainerIndex, const TArray<FSarkoItemStack>& Inventory)
{
	ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && Inventory.Num() == 0)
	{
		// Emptied only now, and only because it is ACTUALLY empty. This one line
		// is the vanishing-loot fix: the old code marked here unconditionally.
		RaidState->SetContainerState(ContainerIndex, ESarkoContainerState::Emptied);
	}
	ClientContainerContents(ContainerIndex, Inventory);
}

void ASarkoCharacter::TakeAllFrom(int32 ContainerIndex)
{
	// The same per-item path in a loop, stopping when a full pass moves nothing —
	// so it can never spin, and the remainder simply stays (spec §3).
	for (int32 Guard = 0; Guard < SarkoLoot::ContainerCells + 1; ++Guard)
	{
		TArray<FSarkoItemStack>* Inventory = /* game mode */ nullptr;
		// (resolve as in ServerTakeItem; take slot 0 repeatedly, since a drained
		//  slot is removed and the next one shifts down into its place)
		ServerTakeItem_Implementation(ContainerIndex, 0);
		if (!Inventory || Inventory->Num() == 0) { break; }
	}
}
```

> **Implementer:** write `TakeAllFrom` to resolve the game mode and the inventory **once**, then loop `SarkoLoot::TransferOne(*Inventory, 0, Bag, …)` directly (not through the RPC implementation) until it returns 0 or the array is empty, writing the bag back once at the end and calling `FinishTransfer` once. Going through `ServerTakeItem_Implementation` per item would re-run the whole validation chain four times and send four RPCs for one button press. `ServerTakeAll_Implementation` runs the same §3 validation chain once, then calls this.

`ClientContainerContents_Implementation` sets `LocalOpenContainerIndex` / `LocalOpenContainerSlots` and broadcasts `OnContainerViewChanged`; `ClientContainerClosed_Implementation` clears them and broadcasts; `ClientTransferRefused_Implementation` broadcasts `OnTakeRefused`. `HandleDeath` and `FreezeForRaidEnd` both clear `OpenContainerIndex`, `LocalOpenContainerIndex` and `LocalOpenContainerSlots` alongside the channel indices they already clear.

- [ ] **Step 8: Green**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: `ALL GREEN` at `B + 8` (T1's +4 plus this task's +5 new, −1 deleted).

- [ ] **Step 9: Prove the raid still plays, and that loot stops vanishing**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && HUD_SAFE_AREA=1 ./Scripts/hud-shot.sh
```
Read the PNG. Expected: unchanged HUD, and the backpack readout showing `n/4` before a backpack is found. Then check the log for the new lines:

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && grep -E "SarkoRaidGameMode: container|SarkoCharacter: (looted|take)" "$HOME/Library/Logs/Unreal Engine/SarkoGameEditor/SarkoGame.log" | tail -20
```

- [ ] **Step 10: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Loot SarkoGame/Source/SarkoGame/Core SarkoGame/Source/SarkoGame/Pawn SarkoGame/Source/SarkoGame/Tests/LootTest.cpp && git commit -m "feat(loot): contents live in the container, and what does not fit stays in it"
```

---

### Task 3: The design system, with no assets and nothing to look at yet

Pure code and pure tests: the palette, the label rule, the brush cache, and the scale that stops the panel being 9 % bigger than it claims. No widget, so the raid is untouched and trivially still playable. Separated from Task 4 because all of this is testable under `-nullrhi`, where a Slate widget cannot be constructed at all.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp` (+3)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoUiScale.h`

**Interfaces:**
- Consumes: `ESarkoItemCategory`, `SarkoUI::PointScaleForViewport`.
- Produces:
  - `FLinearColor SarkoUI::CategoryColour(ESarkoItemCategory)`
  - `constexpr float SarkoUI::CellFillFactor = 0.18f;` and `FLinearColor SarkoUI::CategoryCellFill(ESarkoItemCategory)`
  - `const FLinearColor SarkoUI::PanelPlate, ::PanelOutline, ::EmptyCellFill, ::EmptyCellOutline, ::AmberWarn`
  - `FString SarkoUI::CellLabel(const FString& Name)`
  - `float SarkoUI::OverlayPointScale(FVector2D ViewportSize)`
  - `struct FSarkoInventoryStyles` with `static TSharedRef<const FSarkoInventoryStyles> Get();` and members `FButtonStyle CellByCategory[8]`, `FButtonStyle EmptyCell`, `FButtonStyle TakeAllRow`, `FSlateBrush PanelBrush`, `FSlateBrush HairlineBrush`.

- [ ] **Step 1: Write the failing style tests**

Create `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"

#include "Loot/SarkoItemCatalog.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCategoryColoursAreDistinctAndVisible,
	"Sarko.UI.CategoryColoursAreDistinctAndVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCategoryColoursAreDistinctAndVisible::RunTest(const FString& Parameters)
{
	// Colour is the substitute for icons: this project ships no binary assets, so
	// a cell's category has to be legible from its hue at 44 points across. Two
	// properties make that work, and neither is safe to eyeball.
	const TArray<ESarkoItemCategory> All = {
		ESarkoItemCategory::Weapon, ESarkoItemCategory::Ammo, ESarkoItemCategory::Med,
		ESarkoItemCategory::Junk, ESarkoItemCategory::Valuable,
		ESarkoItemCategory::VehiclePart, ESarkoItemCategory::Gear,
	};

	// 1. Every category is a different colour. A duplicate would silently merge
	//    two kinds of loot into one visual signal.
	for (int32 A = 0; A < All.Num(); ++A)
	{
		for (int32 B = A + 1; B < All.Num(); ++B)
		{
			const FLinearColor CA = SarkoUI::CategoryColour(All[A]);
			const FLinearColor CB = SarkoUI::CategoryColour(All[B]);
			const float Distance = FMath::Abs(CA.R - CB.R) + FMath::Abs(CA.G - CB.G) + FMath::Abs(CA.B - CB.B);
			TestTrue(*FString::Printf(TEXT("categories %d and %d are visibly different (L1 %.3f)"),
					static_cast<int32>(All[A]), static_cast<int32>(All[B]), Distance),
				Distance > 0.15f);
		}
	}

	// 2. Even the dullest cell fill sits clearly above the panel plate, or a
	//    junk cell reads as a hole in the panel rather than as a slot with
	//    something in it. Junk is the floor by construction — it is the one
	//    deliberately hueless colour.
	const float PlateLuma = SarkoUI::PanelPlate.R + SarkoUI::PanelPlate.G + SarkoUI::PanelPlate.B;
	for (ESarkoItemCategory Category : All)
	{
		const FLinearColor Fill = SarkoUI::CategoryCellFill(Category);
		const float FillLuma = Fill.R + Fill.G + Fill.B;
		TestTrue(*FString::Printf(TEXT("category %d's fill (%.3f) is at least twice the plate (%.3f)"),
				static_cast<int32>(Category), FillLuma, PlateLuma),
			FillLuma >= PlateLuma * 2.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCellLabelFitsACell,
	"Sarko.UI.CellLabelFitsACell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCellLabelFitsACell::RunTest(const FString& Parameters)
{
	// A 44 pt cell with 4 pt padding is about seven Cyrillic glyphs wide at
	// 8.5 pt, and items.json has no short name. Deriving one is cheaper than a
	// schema field and cannot drift from the catalog, because there is nothing
	// to keep in step.
	TestEqual(TEXT("a multi-word name keeps its first word"),
		SarkoUI::CellLabel(TEXT("Ящик з інструментами")), FString(TEXT("ЯЩИК")));
	TestEqual(TEXT("a name with a number keeps the word, not the number"),
		SarkoUI::CellLabel(TEXT("Патрони 9×18")), FString(TEXT("ПАТРОНИ")));
	TestEqual(TEXT("a short name survives whole"),
		SarkoUI::CellLabel(TEXT("Бинт")), FString(TEXT("БИНТ")));
	TestEqual(TEXT("a long single word is truncated with an ellipsis, never clipped mid-cell"),
		SarkoUI::CellLabel(TEXT("Обезболювальне")), FString(TEXT("ОБЕЗБОЛЮВ…")));
	TestEqual(TEXT("an empty name does not crash and draws nothing"),
		SarkoUI::CellLabel(FString()), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOverlayScaleDividesOutTheLayerManager,
	"Sarko.UI.OverlayScaleDividesOutTheLayerManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOverlayScaleDividesOutTheLayerManager::RunTest(const FString& Parameters)
{
	// SGameLayerManager wraps the viewport overlay in its OWN SDPIScaler
	// (SGameLayerManager.cpp:113, fed by UUserInterfaceSettings::
	// GetDPIScaleBasedOnSize). A widget that also scales itself compounds with
	// it — on a 2556x1179 phone the engine curve gives 1.09, so the panel would
	// render 9% larger than the points it claims and would not line up with the
	// HUD it is drawn over. The HUD does not go through that path at all: its
	// canvas has a DPI scale of exactly 1.
	const FVector2D Phone(2556.f, 1179.f);
	const float Raw = SarkoUI::PointScaleForViewport(Phone);
	const float Overlay = SarkoUI::OverlayPointScale(Phone);

	TestTrue(TEXT("the overlay scale is positive"), Overlay > 0.f);
	TestTrue(TEXT("and no larger than the raw point scale — it only ever divides out"),
		Overlay <= Raw + KINDA_SMALL_NUMBER);

	// The product is the thing that must equal the raw scale: whatever the layer
	// manager multiplies by, this divides by, so the two cancel and a point is a
	// point. Asserted against the engine's own function rather than a copied
	// constant, so a project that changes UIScaleCurve does not silently break.
	const float LayerScale = SarkoUI::GameLayerDpiScale(Phone);
	TestTrue(TEXT("overlay x layer == raw, so the compounding cancels"),
		FMath::IsNearlyEqual(Overlay * LayerScale, Raw, 0.001f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
```

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: **BUILD FAILED** — `UI/SarkoInventoryStyle.h` does not exist.

- [ ] **Step 3: The overlay scale**

In `UI/SarkoUiScale.h`, add `#include "Engine/UserInterfaceSettings.h"` and, after `PointScaleForViewport`:

```cpp
	/**
	 * What SGameLayerManager's own DPI scaler is already multiplying the viewport
	 * overlay by, for a viewport of this size.
	 *
	 * Exposed rather than folded into OverlayPointScale so a test can assert that
	 * the two cancel, and so the number is inspectable when a phone's layout
	 * looks a size too big.
	 */
	inline float GameLayerDpiScale(FVector2D ViewportSize)
	{
		const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(ViewportSize.X)),
			FMath::Max(1, FMath::RoundToInt(ViewportSize.Y)));
		return FMath::Max(KINDA_SMALL_NUMBER, GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size));
	}

	/**
	 * The DPI scale a widget added to the VIEWPORT OVERLAY must use, so that a
	 * size written in points measures that many points on the glass.
	 *
	 * SGameLayerManager wraps the whole overlay in an SDPIScaler of its own
	 * (Engine/Private/Slate/SGameLayerManager.cpp:113). A widget's own scaler
	 * therefore compounds with it: with the engine's default
	 * UIScaleRule=ShortestSide and UIScaleCurve (BaseEngine.ini: 1080 -> 1.0,
	 * 8640 -> 8.0), a 2556x1179 landscape phone gets 1.092, so an unadjusted
	 * widget renders ~9% larger than it claims.
	 *
	 * That matters here and not for the shelter because the inventory panel is
	 * drawn OVER the in-raid HUD and has to agree with it, and the HUD's canvas
	 * is 1:1 with pixels (see the file header) — it never meets the layer
	 * manager at all. The shelter keeps PointScaleForViewport and is therefore
	 * ~9% over its stated size today: a known, separate deviation, validated by
	 * screenshot at that size, and not something to change without taking
	 * another one.
	 */
	inline float OverlayPointScale(FVector2D ViewportSize)
	{
		return PointScaleForViewport(ViewportSize) / GameLayerDpiScale(ViewportSize);
	}
```

- [ ] **Step 4: The palette, the label and the brush cache**

Create `UI/SarkoInventoryStyle.h`. The full palette, with each sRGB target named beside its linear value (Slate is linear; an sRGB swatch pasted in comes out roughly twice as bright):

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

enum class ESarkoItemCategory : uint8;

/**
 * Every colour, brush and derived string the container panel draws with.
 *
 * Separate from the panel widget because all of it is pure and must be testable
 * under -nullrhi, where no Slate application exists and a widget cannot be
 * constructed at all. The panel is where these are arranged; this is what they
 * are.
 *
 * NO BINARY ASSETS. Every brush here is an FSlateRoundedBoxBrush constructed in
 * C++ (SlateCore/Public/Brushes/SlateRoundedBoxBrush.h) and every font is
 * FCoreStyle's, which is compiled into SlateCore. That constraint is why the
 * design leans on colour, radius and outline weight instead of iconography —
 * and why the palette below is a real seven-hue wheel rather than an
 * afterthought.
 */
namespace SarkoUI
{
	/**
	 * The panel plate: near-black at 86% alpha, ~#1c1f22.
	 *
	 * Translucent and NOT a dim of the whole world. DrawOutcomeSummary dims the
	 * entire canvas at 0.55 because it is a final screen and nothing behind it
	 * can still kill you. This is the opposite case: looting does not pause the
	 * world (spec §2.4), so a bot crossing behind this panel has to stay a
	 * moving silhouette.
	 */
	const FLinearColor PanelPlate(0.012f, 0.014f, 0.018f, 0.86f);
	const FLinearColor PanelOutline(0.050f, 0.055f, 0.065f, 0.90f);
	const FLinearColor EmptyCellFill(0.020f, 0.022f, 0.026f, 0.90f);
	const FLinearColor EmptyCellOutline(0.045f, 0.050f, 0.058f, 0.90f);
	const FLinearColor Hairline(0.055f, 0.060f, 0.070f, 0.90f);

	/** ~#ffc16a. The same amber the HUD's backpack readout turns when full — the
	 *  two must agree, or "full" means one thing on the panel and another above it. */
	const FLinearColor AmberWarn(1.0f, 0.55f, 0.06f, 1.0f);

	const FLinearColor CellLabelColour(0.62f, 0.64f, 0.66f);   // ~#d0d3d5
	const FLinearColor CellCountColour(0.92f, 0.92f, 0.88f);   // ~#f7f7f3
	const FLinearColor HeaderColour(0.22f, 0.23f, 0.25f);      // ~#8b8f94

	/**
	 * How much of a category's colour goes into a cell's FILL, as opposed to its
	 * outline. 0.18, because eight adjacent 44-point cells at full saturation is
	 * a carnival and a tinted dark body under a bright rim is a rack of gear.
	 *
	 * The floor this has to clear: junk is the dimmest colour at (0.195, 0.212,
	 * 0.238), so its fill is ~0.035 linear against a plate of 0.012-0.018 — about
	 * twice the plate, which is what keeps the dullest cell reading as an object
	 * rather than a hole. Sarko.UI.CategoryColoursAreDistinctAndVisible asserts
	 * it rather than trusting this comment.
	 */
	constexpr float CellFillFactor = 0.18f;

	/**
	 * A category's identity colour, used at full strength for a cell's 1.5 pt
	 * outline — the load-bearing signal, because an outline reads at 44 points
	 * where a fill does not.
	 *
	 * Seven values, six hues and one deliberate neutral: weapon 6 deg, ammo 41,
	 * gear 78, med 168, vehicle part 210, valuable 275, and junk with no hue at
	 * all. Junk being the only grey is information, not laziness. The minimum gap
	 * is 35 degrees (red to brass), which survives the small-swatch, low-
	 * luminance, glare-on-a-phone case that kills 15-degree neighbours.
	 */
	FLinearColor CategoryColour(ESarkoItemCategory Category);

	/** CategoryColour x CellFillFactor, alpha 0.92. */
	FLinearColor CategoryCellFill(ESarkoItemCategory Category);

	/**
	 * An item's display name, shortened to fit a cell: first word, uppercased,
	 * truncated to 9 characters with an ellipsis.
	 *
	 * Derived rather than authored. A 44-point cell with 4 points of padding is
	 * about seven Cyrillic glyphs wide at 8.5 pt, and items.json carries no short
	 * name; adding one would be a schema field that has to be kept in step with
	 * every name forever. This cannot drift, because there is nothing to drift
	 * from.
	 */
	FString CellLabel(const FString& Name);
}

/**
 * The button styles the panel's cells are built from, constructed once and
 * owned for the process' lifetime.
 *
 * **This exists because SButton stores a raw `const FButtonStyle*`**
 * (Slate/Public/Widgets/Input/SButton.h:336) and points its Normal/Hovered/
 * Pressed brushes into it. A style built on the stack inside Construct is a
 * dangling pointer the moment Construct returns, and the symptom is not a crash
 * — it is a panel that draws garbage or nothing, intermittently, on a device.
 */
struct FSarkoInventoryStyles
{
	/** Process-wide, built on first use. */
	static TSharedRef<const FSarkoInventoryStyles> Get();

	/** Indexed by ESarkoItemCategory; sized past the enum so a future value cannot overrun. */
	FButtonStyle CellByCategory[8];
	FButtonStyle EmptyCell;
	FButtonStyle TakeAllRow;
	FSlateBrush PanelBrush;
	FSlateBrush HairlineBrush;

	FSarkoInventoryStyles();
};
```

Create `UI/SarkoInventoryStyle.cpp`. The colours (linear values from the table in **Visual design** above), then:

```cpp
FLinearColor SarkoUI::CategoryColour(ESarkoItemCategory Category)
{
	switch (Category)
	{
	case ESarkoItemCategory::Weapon:      return FLinearColor(0.552f, 0.059f, 0.045f); // #C4453C
	case ESarkoItemCategory::Ammo:        return FLinearColor(0.694f, 0.352f, 0.027f); // #D9A02E
	case ESarkoItemCategory::Gear:        return FLinearColor(0.275f, 0.451f, 0.048f); // #8FB33E
	case ESarkoItemCategory::Med:         return FLinearColor(0.050f, 0.521f, 0.352f); // #3FBFA0
	case ESarkoItemCategory::VehiclePart: return FLinearColor(0.076f, 0.275f, 0.672f); // #4E8FD6
	case ESarkoItemCategory::Valuable:    return FLinearColor(0.397f, 0.165f, 0.687f); // #A971D8
	case ESarkoItemCategory::Junk:
	default:                              return FLinearColor(0.195f, 0.212f, 0.238f); // #7A7F86
	}
}

FLinearColor SarkoUI::CategoryCellFill(ESarkoItemCategory Category)
{
	const FLinearColor Base = CategoryColour(Category);
	return FLinearColor(Base.R * CellFillFactor, Base.G * CellFillFactor, Base.B * CellFillFactor, 0.92f);
}

FString SarkoUI::CellLabel(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return FString();
	}
	FString First;
	FString Rest;
	// The first word carries the identity in every name in the catalog:
	// "Патрони 9×18" is ammo, "Ящик з інструментами" is the box.
	if (!Name.Split(TEXT(" "), &First, &Rest))
	{
		First = Name;
	}
	First = First.ToUpper();
	constexpr int32 MaxChars = 9;
	if (First.Len() > MaxChars)
	{
		// An ellipsis, not a hard clip: a clipped word looks like a rendering
		// fault, and a player has to be able to tell "there is more of this name"
		// from "this is the name".
		First = First.Left(MaxChars) + TEXT("…");
	}
	return First;
}
```

and the style construction, which is the whole reason `FSlateRoundedBoxBrush` is in this plan:

```cpp
namespace
{
	/** The one place a cell's look is defined. Verified against 5.8's
	 *  FSlateRoundedBoxBrush(FillColor, Radius, OutlineColor, OutlineWidth). */
	FButtonStyle MakeCellStyle(const FLinearColor& Fill, const FLinearColor& Outline)
	{
		constexpr float CellRadius = 8.f;
		constexpr float CellOutline = 1.5f;
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Fill, CellRadius, Outline, CellOutline));
		// Pressed brightens the FILL rather than moving anything: a 44-point cell
		// under a thumb is entirely hidden, so the confirmation the player
		// actually sees is the transfer animation on the other grid. This is only
		// for the desktop pointer case, and for the instant before the finger lands.
		Style.SetHovered(FSlateRoundedBoxBrush(Fill * 1.6f, CellRadius, Outline, CellOutline));
		Style.SetPressed(FSlateRoundedBoxBrush(Fill * 2.2f, CellRadius, Outline, CellOutline + 0.5f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}
}

TSharedRef<const FSarkoInventoryStyles> FSarkoInventoryStyles::Get()
{
	// A function-local static shared ref: built once, never destroyed before
	// exit, and therefore guaranteed to outlive every SButton that points into
	// it. See the header for why that guarantee is the point.
	static const TSharedRef<const FSarkoInventoryStyles> Styles = MakeShared<const FSarkoInventoryStyles>();
	return Styles;
}
```

- [ ] **Step 5: Green**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: `ALL GREEN` at `B + 11`.

- [ ] **Step 6: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.cpp SarkoGame/Source/SarkoGame/UI/SarkoUiScale.h SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp && git commit -m "feat(ui): a seven-hue category palette and rounded cells, built entirely in C++"
```

---

### Task 4: The panel

The screen the whole plan is for. Geometry from **Visual design**, exactly; the numbers there are not suggestions.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h`, `.cpp`
- Create: `SarkoGame/Scripts/inventory-shot.sh`
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp` (owns the panel)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp` (interact button becomes close)
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp` (drop the temporary take-all)
- Modify: `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp` (+2)

**Interfaces:**
- Consumes: `FSarkoInventoryStyles::Get`, `SarkoUI::CellLabel`, `SarkoUI::CategoryColour`, `SarkoUI::OverlayPointScale`, `SarkoInput::SafeFrame`, `ASarkoCharacter::OnContainerViewChanged`, `::OnTakeRefused`, `::GetOpenContainerSlots`, `::RequestTakeItem`, `::RequestTakeAll`, `::RequestCloseContainer`.
- Produces:
  - `SSarkoInventoryPanel` with `SLATE_ARGUMENT(TWeakObjectPtr<ASarkoCharacter>, Pawn)`, `void Refresh()`, `void PlayRefusal(int32 SlotIndex, ESarkoTakeRefusal)`, `void PlayExit()`.
  - `namespace SarkoUI { FBox2D InventoryPanelRect(FBox2D SafeFrame, int32 PlayerCells, float PointScale); }` — pure, so the layout is testable with no viewport.
  - `bool SSarkoInventoryPanel::SimulateTapContainerCell(int32 SlotIndex)` under `#if !UE_BUILD_SHIPPING`.

- [ ] **Step 1: Write the failing layout tests**

Append to `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelLeavesTheApproachVisible,
	"Sarko.UI.PanelLeavesTheApproachVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelLeavesTheApproachVisible::RunTest(const FString& Parameters)
{
	// Spec §5: looting does not pause the world, so a panel that covers the
	// approach is how a player dies. That is a layout requirement, and this is
	// where it is enforced — a screenshot proves it looks right, this proves it
	// STAYS right when someone edits a constant.
	const FBox2D Safe(FVector2D::ZeroVector, FVector2D(844.f, 390.f));   // 1 px/pt
	const FBox2D Panel = SarkoUI::InventoryPanelRect(Safe, /*PlayerCells*/ 12, /*PointScale*/ 1.f);

	TestTrue(TEXT("the panel takes at most a third of the width"),
		Panel.GetSize().X <= Safe.GetSize().X * 0.34f);
	// The pawn is at the centre of a top-down camera. If the panel's left edge
	// reached it, the player would be looting blind at their own feet.
	TestTrue(TEXT("the pawn at screen centre is clear of the panel by 150 pt or more"),
		Panel.Min.X - Safe.GetCenter().X >= 150.f);
	TestTrue(TEXT("it stays inside the safe frame's right edge"), Panel.Max.X <= Safe.Max.X);
	TestTrue(TEXT("and above its bottom edge, where the sticks live"), Panel.Max.Y < Safe.Max.Y);
	// The HUD's health bar occupies y 14..29 at the top right. Overlapping it
	// would hide the one readout that says you are dying while you stand still.
	TestTrue(TEXT("it clears the health bar's row"), Panel.Min.Y > 40.f);

	// Four pocket cells is a SHORTER panel, not a differently-shaped one: a panel
	// that changed width when you found a bag would reflow the screen mid-raid.
	const FBox2D Pockets = SarkoUI::InventoryPanelRect(Safe, /*PlayerCells*/ 4, 1.f);
	TestTrue(TEXT("width does not change with capacity"),
		FMath::IsNearlyEqual(Pockets.GetSize().X, Panel.GetSize().X, 0.01f));
	TestTrue(TEXT("a four-cell bag makes a shorter panel"), Pockets.GetSize().Y < Panel.GetSize().Y);
	TestTrue(TEXT("and it stays bottom-anchored"),
		FMath::IsNearlyEqual(Pockets.Max.Y, Panel.Max.Y, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelCellsClearTheTapTargetMinimum,
	"Sarko.UI.PanelCellsClearTheTapTargetMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelCellsClearTheTapTargetMinimum::RunTest(const FString& Parameters)
{
	// The project's touch rule is written in points, which is exactly why the
	// whole layout is authored in points. 44 is the floor and there is no
	// rounding slack below it.
	TestTrue(TEXT("a cell is at least 44 pt"), SarkoUI::CellSizePt >= 44.f);
	TestTrue(TEXT("the take-all row is at least 44 pt"), SarkoUI::TakeAllRowPt >= 44.f);
	// The grid must actually fit the panel it is padded inside, or the last
	// column is drawn off the edge and cannot be tapped at all.
	TestEqual(TEXT("four columns plus gutters plus padding is the panel width"),
		SarkoUI::CellSizePt * 4.f + SarkoUI::CellGutterPt * 3.f + SarkoUI::PanelPadPt * 2.f,
		SarkoUI::PanelWidthPt);
	return true;
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: **BUILD FAILED** on `SarkoUI::InventoryPanelRect` and the geometry constants.

- [ ] **Step 3: The geometry constants and the pure rect**

In `UI/SarkoInventoryPanel.h`, inside `namespace SarkoUI`, the numbers from **Visual design**:

```cpp
	/** Every size below is in points on the 844x390 landscape canvas
	 *  (SarkoUiScale.h), i.e. what it measures on the glass at any density. */
	constexpr float CellSizePt = 44.f;        // == the tap-target minimum, exactly
	constexpr float CellGutterPt = 4.f;       // 48 pt slot pitch
	constexpr float CellRadiusPt = 8.f;
	constexpr float PanelPadPt = 14.f;
	constexpr float PanelWidthPt = CellSizePt * 4.f + CellGutterPt * 3.f + PanelPadPt * 2.f;  // 216
	constexpr float PanelRadiusPt = 14.f;
	constexpr float TakeAllRowPt = 44.f;
	constexpr float HeaderRowPt = 16.f;
	constexpr float DividerPt = 12.f;
	constexpr float GridGapPt = 6.f;
	constexpr float GridColumns = 4;

	/** In from the safe frame's right edge, and up from its bottom. The bottom
	 *  number keeps the panel clear of the home indicator AND of the aim thumb's
	 *  resting corner, in that order of importance. */
	constexpr float PanelRightInsetPt = 16.f;
	constexpr float PanelBottomInsetPt = 20.f;

	/**
	 * Where the panel goes, in viewport pixels. Pure, so the one property that
	 * decides whether a player can see the bot walking at them is unit tested
	 * without a viewport, a widget or a Slate application.
	 *
	 * Bottom-anchored and growing UPWARD, which is what keeps a full 12-cell
	 * panel clear of the HUD's health bar (y 14..29) while a 4-cell one sits
	 * lower still. Top-anchoring put the header 3 points under the bar, which
	 * reads as a collision rather than as a layout.
	 */
	FBox2D InventoryPanelRect(FBox2D SafeFrame, int32 PlayerCells, float PointScale);
```

```cpp
FBox2D SarkoUI::InventoryPanelRect(FBox2D SafeFrame, int32 PlayerCells, float PointScale)
{
	const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(0, PlayerCells), 4));
	const float PlayerGridPt = Rows * CellSizePt + (Rows - 1) * CellGutterPt;
	const float ContainerGridPt = CellSizePt;   // one row of four; see SarkoLoot::ContainerCells
	const float HeightPt = PanelPadPt + TakeAllRowPt + GridGapPt + ContainerGridPt
		+ DividerPt + HeaderRowPt + GridGapPt + PlayerGridPt + PanelPadPt;

	const float Width = PanelWidthPt * PointScale;
	const float Height = HeightPt * PointScale;
	const FVector2D Max(SafeFrame.Max.X - PanelRightInsetPt * PointScale,
		SafeFrame.Max.Y - PanelBottomInsetPt * PointScale);
	return FBox2D(FVector2D(Max.X - Width, Max.Y - Height), Max);
}
```

- [ ] **Step 4: The widget**

`SSarkoInventoryPanel::Construct` builds, top to bottom:

```
SDPIScaler(OverlayPointScale)
└── SConstraintCanvas or SOverlay + alignment to place a PanelWidthPt-wide box
    bottom-right of SarkoInput::SafeFrame
    └── SBorder(.BorderImage = &Styles->PanelBrush)         // FSlateRoundedBoxBrush, radius 14
        └── SVerticalBox
            ├── AutoHeight: SButton(TakeAllRow)  "ОБШУК · <TIER>"   "ЗАБРАТИ ВСЕ"
            ├── AutoHeight: SUniformGridPanel — 4 container cells
            ├── AutoHeight: SBox(HeightOverride 1) hairline
            ├── AutoHeight: STextBlock "РЮКЗАК n/m"
            └── AutoHeight: SUniformGridPanel — PlayerCells cells
```

Three rules that are not negotiable, each with the reason written into the code:

```cpp
	// 1. The ROOT is SelfHitTestInvisible and only the cells and the take-all row
	//    are Visible. That is the mechanism — not a hope — by which a thumb landing
	//    on the panel's padding falls through to SViewport and drives the movement
	//    stick. Spec §4: "The player remains steerable while it is open — they are
	//    standing in the open, and should be able to run."
	SetVisibility(EVisibility::SelfHitTestInvisible);
```
```cpp
	// 2. The styles are the process-wide ones, NOT built here. SButton stores a raw
	//    const FButtonStyle* (SButton.h:336) and points its brushes into it, so a
	//    style constructed in this function dangles the moment it returns.
	const TSharedRef<const FSarkoInventoryStyles> Styles = FSarkoInventoryStyles::Get();
```
```cpp
	// 3. Rebuilt on a transfer, never on a frame. Slate is not a tick path, and
	//    this is a few dozen widgets a handful of times per crate — the same
	//    "rebuilt wholesale on SetView" the shelter's stash list uses.
```

And the one the player controller must honour, stated in the widget's header comment because it is the trap:

```cpp
/**
 * ...
 * **The controller must NOT set FInputModeUIOnly for this panel.** That mode
 * calls UGameViewportClient::SetIgnoreInput(true), the viewport client belongs
 * to the ULocalPlayer, and every touch, stick, shot and loot press dies with it
 * — the exact scar Core/SarkoPlayerController.h carries from the shelter. Input
 * stays FInputModeGameOnly and Slate routes taps by hit-testing, which is why
 * rule 1 above is the whole design.
 */
```

Cell contents, per occupied slot: an `SOverlay` of the category-styled `SButton`, an `STextBlock` with `SarkoUI::CellLabel(Def->Name)` at 8.5 pt top-left (auto-wrap off, `ETextOverflowPolicy::Ellipsis`), and — only when `Quantity > 1` — an `STextBlock` with the count at 10 pt, bottom-right, 2 pt inset.

`#if !UE_BUILD_SHIPPING`, the headless hook, modelled on the shelter's `SimulateEnterRaidClickIfEnabled`:

```cpp
	/**
	 * Test-only: fires a container cell's OWN OnClicked, and only while the
	 * button is genuinely enabled — so nothing can take an item the player could
	 * not have tapped. Exists because a headless run has no fingers and pressing
	 * a Slate button for real needs hit-testing against live geometry.
	 *
	 * SButton::SimulateClick is itself `#if !UE_BUILD_SHIPPING` in the engine, so
	 * this cannot survive into a shipping build even if the guard were removed:
	 * it would fail to link.
	 */
	bool SimulateTapContainerCell(int32 SlotIndex);
```

- [ ] **Step 5: The controller owns it, and the interact button becomes close**

`ASarkoPlayerController` gains `TSharedPtr<SSarkoInventoryPanel> InventoryPanel`. On `BeginPlay` it binds `ASarkoCharacter::OnContainerViewChanged` and `::OnTakeRefused` (rebinding on possession change). On the first `OnContainerViewChanged` with a valid index it constructs the panel and calls `AddViewportWidgetContent`; on `GetOpenContainerIndex() == INDEX_NONE` it calls `PlayExit()` and removes the widget when the 90 ms exit completes. `EndPlay` removes it unconditionally, **before `Super`**, for the same reason `ASarkoShelterPlayerController::EndPlay` does — a viewport widget is not an actor and is not destroyed with the level.

`UpdateInteract` gains, at the top of its held-input handling:

```cpp
	// A panel is open: the interact button is the CLOSE button now, so a press
	// closes rather than starting a channel on whatever crate is nearest. One
	// control, two jobs, and the thumb already knows where it is.
	if (Pawn->GetOpenContainerIndex() != INDEX_NONE)
	{
		if (bHeld && !bInteractHeld)
		{
			Pawn->RequestCloseContainer();
		}
		bInteractHeld = bHeld;
		HeldContainerIndex = INDEX_NONE;
		return;
	}
```

`ASarkoHUD::DrawInteract` gains a matching branch: when the pawn has a container open, it draws the button at `SarkoUI::InventoryPanelRect(Safe, …).Min.X − Px(16) − Size` (same vertical centre, so it slides out from under the panel), fills it in the amber `(0.95, 0.8, 0.25, 0.55)`, draws an **X with two `DrawLine` calls** instead of the `E` label, and **returns before the prompt and the channel bar** — neither means anything while a panel is up. The controller must hit-test the same rect it is drawn in, so the shifted rect goes in `SarkoInput` beside `InteractButtonRect`:

```cpp
	/** Where the interact button sits while a container panel covers its usual
	 *  place. Same size and same vertical band, moved left of the panel — a button
	 *  drawn in one place and pressed in another is the one thing about this
	 *  control that must never happen. */
	FBox2D InteractButtonRectBesidePanel(FBox2D Frame, FBox2D PanelRect);
```

- [ ] **Step 6: Drop the temporary take-all**

In `Pawn/SarkoCharacter.cpp`, delete the `TakeAllFrom(ContainerIndex);` line and its comment from `OpenContainerFor`. Opening now shows the contents and takes nothing — which is the point of the whole plan.

- [ ] **Step 7: The screenshot script**

Create `SarkoGame/Scripts/inventory-shot.sh`, copied from `Scripts/hud-shot.sh` with three changes: it defaults `HUD_SAFE_AREA` on (the layout claims are about a phone), it teleports to the pipes-yard crate and adds `Walk`, and its `-ExecCmds` appends a new `#if !UE_BUILD_SHIPPING` exec on the controller — `SarkoOpenNearestContainer` — before `Shot showui`, because a headless run has no finger to hold the interact button with. Header comment must repeat the two traps `hud-shot.sh` records: **`-RenderOffscreen`, not `-nullrhi`** (nullrhi renders nothing), and **`Shot showui`, not `HighResShot`** (HighResShot goes through the scene renderer and captures no Slate — the PNG comes out with no panel on it at all).

- [ ] **Step 8: Green, then look at it**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
```
Expected: `ALL GREEN` at `B + 13`, then a PNG path. **Read the PNG as an image** and check, in this order:

1. the panel is a rounded, outlined, translucent plate in the **bottom-right quarter**, and the **pawn is visible and clear of it**;
2. the world behind the panel is **visible through it** — not dimmed, not blacked;
3. cells are rounded with a coloured 1.5 pt rim, and two different categories are two obviously different colours;
4. the clock and the health bar are still readable **above** the panel;
5. the interact button has moved **left of the panel** and carries an X;
6. nothing is clipped at the right edge (the safe-area strip is drawn because `HUD_SAFE_AREA=1`).

Fix and re-shoot until all six hold. Attach the final PNG path to the task report.

- [ ] **Step 9: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/UI SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp SarkoGame/Scripts/inventory-shot.sh && git commit -m "feat(ui): a container panel you loot from, in the quarter of the screen you can spare"
```

---

### Task 5: Motion, and making a refusal impossible to miss

"I tapped and nothing happened" is the spec's named failure mode. This task is why it cannot happen.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp` (+1)

**Interfaces:**
- Consumes: `ESarkoTakeRefusal`, `SarkoUI::AmberWarn`.
- Produces: `SSarkoInventoryPanel::EntryCurve`, `::TransferCurve`, `::RefusalCurve` (all `FCurveSequence`), `::RefusedSlot` (`int32`), `::LastRefusal` (`ESarkoTakeRefusal`), and `float SarkoUI::RefusalShakeOffsetPt(float Lerp)` — pure.

- [ ] **Step 1: The failing shake test**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRefusalShakeStartsAndEndsAtRest,
	"Sarko.UI.RefusalShakeStartsAndEndsAtRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRefusalShakeStartsAndEndsAtRest::RunTest(const FString& Parameters)
{
	// A shake that does not return to zero leaves the cell permanently offset by
	// a few points — which nobody notices as an animation bug and everybody
	// notices as a grid that is subtly crooked.
	TestTrue(TEXT("starts at rest"), FMath::IsNearlyZero(SarkoUI::RefusalShakeOffsetPt(0.f), 0.001f));
	TestTrue(TEXT("ends at rest"), FMath::IsNearlyZero(SarkoUI::RefusalShakeOffsetPt(1.f), 0.001f));
	// Two full cycles, so it reads as "no" rather than as a glitch.
	TestTrue(TEXT("swings both ways"),
		SarkoUI::RefusalShakeOffsetPt(0.125f) > 3.f && SarkoUI::RefusalShakeOffsetPt(0.375f) < -3.f);
	TestTrue(TEXT("never exceeds the amplitude, so it cannot leave the cell"),
		FMath::Abs(SarkoUI::RefusalShakeOffsetPt(0.2f)) <= 4.001f);
	return true;
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: **BUILD FAILED** on `SarkoUI::RefusalShakeOffsetPt`.

- [ ] **Step 3: The curves**

```cpp
	/** +-4 pt over two full cycles, starting and ending at exactly zero. Pure, so
	 *  the "ends at rest" property is a test rather than an eyeball. */
	inline float RefusalShakeOffsetPt(float Lerp)
	{
		return 4.f * FMath::Sin(4.f * PI * FMath::Clamp(Lerp, 0.f, 1.f));
	}
```

In the panel, as members — declared in this order so the durations are readable as a budget:

```cpp
	/**
	 * All the motion this panel has, and all of it short.
	 *
	 * Looting does not pause the world (spec §2.4): a second of animation is a
	 * second the player is standing still in the open with a bot walking at them.
	 * Nothing here loops — FCurveSequence::Play registers an active timer, and a
	 * looping one would hold it open for as long as the panel is up, which is a
	 * frame's worth of Slate work every frame for a thing that is not moving.
	 */
	FCurveSequence EntryCurve{ 0.f, 0.140f, ECurveEaseFunction::CubicOut };
	FCurveSequence ExitCurve{ 0.f, 0.090f, ECurveEaseFunction::QuadIn };
	FCurveSequence TransferCurve{ 0.f, 0.120f, ECurveEaseFunction::CubicOut };
	FCurveSequence RefusalCurve{ 0.f, 0.240f, ECurveEaseFunction::Linear };
```

Entry: `EntryCurve.Play(SharedThis(this))` in `Construct`; a `RenderTransform` attribute translating `(1 − GetLerp()) × 24 pt` on X and a `ColorAndOpacity` attribute of `GetLerp()`.

Transfer: `Refresh()` calls `TransferCurve.Play(SharedThis(this))` and records which player-grid index received; that cell's `RenderTransform` scales `Lerp(0.86, 1.0, GetLerp())` about its centre and its outline lerps white → category colour.

- [ ] **Step 4: The refusal, all three signals**

`PlayRefusal(SlotIndex, Reason)` stores both, then `RefusalCurve.Play(SharedThis(this))`. Three attributes read it:

```cpp
	// 1. The player grid's outline pulses amber — the reason lives on the side
	//    that HAS the problem, so the eye goes to "your bag is full" and not to
	//    "the crate is broken".
	// 2. The РЮКЗАК header turns amber and STAYS amber while used == limit. A
	//    state, not a flash, and the same amber the HUD's backpack readout uses —
	//    two different ambers for one fact would be worse than none.
	// 3. The refused container cell shakes, RefusalShakeOffsetPt(GetLerp()).
	//
	// Only NoSpace lights 1 and 2. TooFar / Gone / NotOpen / RaidOver shake and
	// nothing more: a shake with no amber means "you moved", and that distinction
	// is the difference between a player retrying and a player understanding.
```

- [ ] **Step 5: Green, then look at it**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
```
Expected `ALL GREEN` at `B + 14`. **A screenshot cannot show motion.** So verify the refusal *state* instead: shoot with the bag already full — add a `#if !UE_BUILD_SHIPPING` exec `SarkoFillBackpack` to the controller for this, or teleport onto the tutorial's military crate after taking everything — and read the PNG for the **amber `РЮКЗАК 12/12` header** and the **amber player-grid outline**, which are states and do persist in a frame. Record in the task report that the 140/120/240 ms timings were **not** verified by screenshot and are verified by playing it.

- [ ] **Step 6: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp && git commit -m "feat(ui): a refused take shakes, and says which side the problem is on"
```

---

### Task 6: The tutorial at four cells

The migration the model change forces, and the last honest look.

**The arithmetic.** Loot all 19 authored containers in `Data/Maps/bridge.json` and, after stacking by `items.json`'s `stackSize`, the haul is exactly **11 stacks**: `ammo_9mm` 116 units → 2 cells (stack 60), and one cell each for `scrap_metal` 7, `copper_wire` 9, `duct_tape` 1, `bandage` 5, `medkit` 3, `canned_food` 5, `painkillers` 3, `toolbox` 1, `pistol` 1. Eleven of twelve. The layout was authored against a 12-cell bag and it fits it with one cell to spare.

**At 4 base cells that layout is unwinnable** — the player fills up on the fourth distinct item and everything after it is a refusal. So **the tutorial grants a backpack, in the very first crate, at spawn.** Not as a workaround: it is the best first lesson the game has. Spec §6.5's teaching order is "junk at spawn (open containers) → medkit at the pipes → ammo + first valuable at the gas station → military at the rail depot → extract at E1", and this makes it **"a bag at spawn (this is what carrying means) → junk → medkit → ammo + valuable → military → extract"** — the player learns capacity by being handed the fix for it, one crate before they could have hit the wall.

**Only container 0 changes.** With the bag equipped from the first crate the capacity is 12 again, so the remaining 18 authored lists still fit exactly as before, and the 11-of-12 arithmetic above is unchanged.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (container 0 only)
- Modify: `SarkoGame/Data/Loot/loot-tables.json` (`good`, `military`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (+2)

- [ ] **Step 1: The failing migration tests**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialGrantsABagBeforeItNeedsOne,
	"Sarko.Loot.TutorialGrantsABagBeforeItNeedsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialGrantsABagBeforeItNeedsOne::RunTest(const FString& Parameters)
{
	// Over the REAL map file. Base capacity is four cells and the authored
	// tutorial layout yields eleven distinct stacks, so without a bag in the
	// FIRST crate the tutorial teaches "everything you find is refused" — which
	// is a lesson, but not the one spec §6.5 is sequencing.
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Definition, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json did not load: %s"), *Error));
		return false;
	}
	if (Definition.Containers.Num() == 0)
	{
		AddError(TEXT("bridge.json has no containers"));
		return false;
	}

	const bool bFirstHasBag = Definition.Containers[0].FixedItems.ContainsByPredicate(
		[](const FSarkoItemStack& Stack) { return Stack.Item == SarkoLoot::BackpackItemId; });
	TestTrue(TEXT("the spawn crate carries a backpack"), bFirstHasBag);

	// And exactly one crate does, or the lesson is "bags are everywhere".
	int32 BagCrates = 0;
	for (const FSarkoLootContainerSpot& Spot : Definition.Containers)
	{
		BagCrates += Spot.FixedItems.ContainsByPredicate(
			[](const FSarkoItemStack& Stack) { return Stack.Item == SarkoLoot::BackpackItemId; }) ? 1 : 0;
	}
	TestEqual(TEXT("and only that one"), BagCrates, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialHaulStillFitsAWornBag,
	"Sarko.Loot.TutorialHaulStillFitsAWornBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialHaulStillFitsAWornBag::RunTest(const FString& Parameters)
{
	// Every authored fixedItems entry, stacked into one bag, must still fit the
	// twelve cells the layout was designed against — including the bag itself,
	// which is worn and therefore costs no cell. Eleven of twelve today, and this
	// is what tells the next person to author a container that they have run out
	// of room BEFORE a player discovers it fifteen minutes into a raid.
	FSarkoMapDefinition Definition;
	FString Error;
	SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Definition, Error);

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const int32 Limit = SarkoLoot::CapacityFor(true, 4, 8);
	TArray<FSarkoItemStack> Bag;
	int32 Refused = 0;
	for (const FSarkoLootContainerSpot& Spot : Definition.Containers)
	{
		for (const FSarkoItemStack& Stack : Spot.FixedItems)
		{
			if (Stack.Item == SarkoLoot::BackpackItemId)
			{
				continue;   // worn, not carried
			}
			Refused += SarkoLoot::AddToBackpack(Bag, Catalog, Limit, Stack.Item, Stack.Quantity);
		}
	}
	TestEqual(TEXT("nothing in the authored layout is refused"), Refused, 0);
	TestTrue(*FString::Printf(TEXT("the whole tutorial haul is %d of %d cells"), Bag.Num(), Limit),
		Bag.Num() <= Limit);
	return true;
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
```
Expected: `Sarko.Loot.TutorialGrantsABagBeforeItNeedsOne` FAILS on "the spawn crate carries a backpack".

- [ ] **Step 3: Container 0 grants the bag**

In `SarkoGame/Data/Maps/bridge.json`, container index 0 only — `fixedItems` becomes:

```json
      "fixedItems": [
        { "item": "backpack", "qty": 1 },
        { "item": "scrap_metal", "qty": 2 }
      ]
```

The backpack is **first in the list** so `ЗАБРАТИ ВСЕ` equips it before it tries to take the scrap — with four pocket cells the order does not yet matter, but it will the first time a spawn crate holds five things. Update the container's `note` to say so.

**No other container changes.** Say so in the commit message; the next person to read this file needs to know the other eighteen were left alone deliberately.

- [ ] **Step 4: Backpacks in the seeded tables**

In `Data/Loot/loot-tables.json`, add one entry to `good` and one to `military` — nowhere else. A bag is what you go deep for; junk and common must not hand one out, or `good` stops meaning anything.

```json
    { "item": "backpack", "weight": 4, "qty": { "min": 1, "max": 1 } }
```
```json
    { "item": "backpack", "weight": 3, "qty": { "min": 1, "max": 1 } }
```

Check the ТЗ §30 rule this could disturb: `Sarko.Loot.RealLootTablesObeyTheDesignRules` caps **vehicle parts** at 3 % of their tier's weight. Adding weight only raises the denominator — `good` 100 → 104 puts `chain` at 1.9 %, `military` 100 → 103 puts `bike_frame` at 1.9 % and `wheel_small` at 1.0 %. All still under. The `med` tier is untouched, so its "no weapons, no vehicle parts" rule is unaffected. Re-run that test and confirm rather than assuming.

- [ ] **Step 5: Green, both suites, and the last look**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/sarko-api && go test ./...
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
```
Expected: Go `ok` at `G`; UE `ALL GREEN` at **`B + 16`**. Read the final PNG one more time against the six checks in Task 4 Step 8, and this time also confirm the **spawn crate's panel shows a `gear`-green backpack cell** as the very first thing the tutorial offers.

- [ ] **Step 6: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Data/Loot/loot-tables.json SarkoGame/Source/SarkoGame/Tests/LootTest.cpp && git commit -m "feat(loot): the first crate of the tutorial is a backpack, so four cells is a lesson"
```

- [ ] **Step 7: Record the follow-ups**

In the task report, name the three things this plan deliberately did not do, so they are decisions rather than gaps:

1. **A backpack cannot be taken from the shelter stash into a raid.** A bag found in a raid is submitted with the haul and lands in the stash, and equipping it from the shelter is shelter scope. Until that exists, every raid starts at four cells.
2. **The shelter menu is ~9 % over its stated point size** (`SGameLayerManager`'s DPI scaler compounds with its own). The panel divides it out via `SarkoUI::OverlayPointScale`; adopting the same for the shelter is a one-line change gated on a fresh `Scripts/shelter-shot.sh`.
3. **Drag-and-drop** — spec §4 calls it "a later improvement, not a blocker", and tap-to-transfer plus `ЗАБРАТИ ВСЕ` covers the MVP.

---

## Self-Review

**Slate API signatures, verified against the 5.8 source on this machine — not from memory:**

- `FSlateRoundedBoxBrush(FillColor, Radius, OutlineColor, OutlineWidth)` **exists** as a template overload in `Engine/Source/Runtime/SlateCore/Public/Brushes/SlateRoundedBoxBrush.h`, templated on `FillColorType`/`OutlineColorType`/`RadiusType`, setting `OutlineSettings = FSlateBrushOutlineSettings(InRadius, InOutlineColor, InOutlineWidth)`. The `(FillColor, Radius)` and `(FillColor, OutlineColor, OutlineWidth)` overloads used elsewhere in Task 3 also exist. No asset, no `.uasset`, constructible in C++.
- `FCurveSequence(StartTime, Duration, ECurveEaseFunction)`, `Play(const TSharedRef<SWidget>&, bool bPlayLooped = false, …)`, `PlayReverse`, `GetLerp()`, `IsPlaying()`, `JumpToEnd()` all confirmed in `SlateCore/Public/Animation/CurveSequence.h`. `ECurveEaseFunction::{Linear, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, …}` confirmed in `CurveHandle.h` — `CubicOut` and `QuadIn`, the two this plan names, are real spellings.
- **`SButton` stores a raw `const FButtonStyle* Style`** (`Slate/Public/Widgets/Input/SButton.h:336`) and points its brushes into it. This is why `FSarkoInventoryStyles::Get()` is a process-wide shared ref and why Task 3's header says so in bold. Verified, not assumed.
- **`SGameLayerManager` wraps the viewport overlay in its own `SDPIScaler`** (`Engine/Private/Slate/SGameLayerManager.cpp:113–115`, `.DPIScale(this, &SGameLayerManager::GetGameViewportDPIScale)`), which returns `UUserInterfaceSettings::GetDPIScaleBasedOnSize(ViewportSize)`. With `BaseEngine.ini:1474-1475`'s `UIScaleRule=ShortestSide` and curve (1080→1.0, 8640→8.0), a 1179 short side interpolates to **1.092** — the "~9 %" is computed from the engine's own curve, not repeated from a note. `UUserInterfaceSettings::GetDPIScaleBasedOnSize(FIntPoint)` confirmed at `UserInterfaceSettings.h:224`.
- `SarkoInput::SafeFrame` and `sarko.SafeArea.DebugPhoneLandscape` confirmed in `Core/SarkoPlayerController.cpp`; `SarkoUI::PointScaleForViewport` and the 844×390 canvas confirmed in `UI/SarkoUiScale.h`.

**Category list matches `items.json`:** the file's six categories are `weapon` (pistol), `ammo` (ammo_9mm), `med` (medkit, bandage, painkillers), `junk` (scrap_metal, copper_wire, duct_tape), `valuable` (canned_food, vodka, cigarettes, toolbox), `vehicle_part` (bike_frame, wheel_small, chain) — all six are in the palette, all six match `ESarkoItemCategory`'s six values and `ParseCategory`'s six map keys. `gear` is the **only** addition, appended last in the `uint8` enum so nothing is renumbered, added to `ParseCategory`, to the error string, to the palette and to `sarko-api`'s `ItemStackSizes` (whose `TestKnownItemsMatchTheClientCatalog` reads `items.json` and would go red otherwise). Seven palette entries, seven enum values.

**Tap targets ≥ 44 pt:** cell **44 pt** on a 48 pt pitch; take-all row **44 pt** tall × 188 pt wide; the interact/close button keeps `SarkoInput::InteractButtonRect`'s `max(96 px, shorter axis × 0.14)`, which the existing `Sarko.Input.InteractButtonAvoidsTheThumbs` already pins. `Sarko.UI.PanelCellsClearTheTapTargetMinimum` asserts the first two rather than leaving them to a comment. The header rows (16 pt) and the hairline carry **no** tap target, deliberately.

**No binary assets anywhere:** every brush is `FSlateRoundedBoxBrush` or `FSlateBrush` constructed in C++; every font is `FCoreStyle::GetDefaultFontStyle` (compiled into SlateCore); the close button's X is two `DrawLine` calls rather than a glyph or an icon; the container lid's three tints are `UMaterialInstanceDynamic` parameters on the existing `/Engine/BasicShapes/BasicShapeMaterial`, referenced by path as the project already does. Files created: `.h`, `.cpp`, `.sh`, `.json`, `.go`, `.ini`. Nothing else.

**The vanishing-loot fix is located.** Today: `Pawn/SarkoCharacter.cpp:379` calls `SarkoLoot::CompleteLootChannel`, whose `Mark()` at `Loot/SarkoLootTable.cpp:323` runs unconditionally — the remainder `AddToBackpack` refused is destroyed with the container. The fix is in **Task 2**: `CompleteLootChannel` and its `FSarkoLootPayout` are deleted (with `Sarko.Loot.CompletedChannelCreditsThenMarksOnce`), and the only line that can now mark a container emptied is inside `ASarkoCharacter::FinishTransfer`, gated on `Inventory.Num() == 0`. `Sarko.Loot.TransferMovesWhatFitsAndLeavesTheRest` asserts the exact case that used to lose forty rounds of 9 mm.

**Spec coverage.** §2.1 landscape — already done, honoured by every point size here. §2.2 cells not packing — one item per cell, count badge, no rotation. §2.3 backpack as an item — Task 1 (worn, +8, 4+8=12) and Task 2's equip-on-take. §2.4 world does not pause — Task 4's `SelfHitTestInvisible` root, `FInputModeGameOnly`, the 25.6 %-width panel and the un-dimmed world. §3 server model — Task 2's `OpenContainerAt` (rolled once, on first open, stored), `ServerTakeItem`'s verbatim validation order, `ServerTakeAll`'s loop, `ClientContainerContents` as the only path contents take to a client. §4 client/UI — Task 4's two grids, tap-to-transfer, the four close conditions, the steerable player, the channel becoming the open. §5 risks — the panel geometry (legibility), the refusal (Task 5), the death path (Task 1's `ClearOnDeath` test). §6 out of scope — restated at the top and not implemented.

**Type consistency, checked across tasks.** `SarkoLoot::TransferOne` is declared in Task 2's Interfaces with the same five parameters used in Task 2 Step 3 and in Task 6's test. `SarkoLoot::CapacityFor(bool, int32, int32)` is the same in Task 1's test, Task 1 Step 6 and Task 6's test. `ESarkoContainerState::{Closed, Opened, Emptied}` is used identically in Tasks 2's game state, game mode and container. `SarkoLoot::ContainerCells` is used in Task 2's grid test, `OpenContainerAt`'s truncation guard, and Task 4's `InventoryPanelRect`. `SarkoUI::CellSizePt` / `CellGutterPt` / `PanelPadPt` / `PanelWidthPt` satisfy the identity Task 4's test asserts: 44×4 + 4×3 + 14×2 = 176 + 12 + 28 = **216** ✔. `ESarkoTakeRefusal` has the same five values in Task 2's declaration, Task 2's server implementation and Task 5's `PlayRefusal`. `SetSlotsForTest` → `SetSlots` is called out as a rename with a grep, not silently used under two names.

**Two things I could not settle without running the engine**, flagged rather than asserted: (1) whether a `SelfHitTestInvisible` overlay root lets a touch fall through to `SViewport` on a **device** exactly as it does on desktop — the routing is the same code path, but Task 4 Step 8's screenshot cannot prove it and only a device run can, so the acceptance for "the player can still run with the panel open" is a played session, not a test; (2) the 140/120/240 ms timings, which no `-nullrhi` test and no still frame can judge.
