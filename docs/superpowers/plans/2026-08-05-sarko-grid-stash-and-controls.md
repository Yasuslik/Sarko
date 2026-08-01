# Grid, Stash and Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Items become `w × h` rectangles auto-placed into a 2×2 pocket grid and a 4×2 backpack grid, the shelter's stash becomes that same grid with a garage craft button beside it, and the raid's controls become hold-to-fire plus a fixed reload/interact column with the loot panel moved off the aim thumb.

**Architecture:** One pure placement module (`Loot/SarkoItemGrid.h/.cpp`) is the whole model: a size comes from the catalog, a set of pages comes from what the pawn is wearing (or from the shelter's stash geometry), and `Place` first-fits stacks into pages. Three screens consume it — the raid's container panel, the shelter's stash, and the HUD's `n/m` readout — and nothing new goes on the wire, because `FSarkoItemStack` stays the unit and placement is *derived* from the stack order rather than stored. The controls layer is likewise one authority: every button rect is a pure function of the safe frame and the point scale, so the thing that draws it and the thing that hit-tests it cannot disagree.

**Tech Stack:** UE 5.8, C++ only (no Blueprints, no `.uasset`), Slate built in C++ from `FSlateRoundedBoxBrush` and `FCoreStyle`, `AHUD::DrawHUD` canvas primitives, Go 1.22 backend (`sarko-api`) — **unchanged by this plan**.

## Global Constraints

- **`docs/superpowers/specs/2026-08-05-sarko-grid-stash-and-controls-design.md` is normative.** It supersedes the "cells not spatial packing" decision in `2026-08-04-sarko-container-inventory-design.md` §2.2.
- **No rotation.** Every item is authored in the orientation it occupies (spec §1).
- **Auto-placement, first fit** — left to right, top to bottom, first free rectangle that fits (spec §1). No manual rearranging, no drag-and-drop.
- **Pockets are a 2×2 grid. A worn backpack adds a separate 4×2 grid.** Twelve cells with a bag, four without (spec §1.2).
- **A stack occupies one rectangle regardless of count** (spec §1.1). `stackSize` still caps how many units ride in it.
- **Death loses everything carried — pockets, backpack, and the bag itself** (spec §1.3). Unchanged.
- **The stash is not a puzzle** (spec §2): generous, scrollable, auto-arranged, sorted by category then name. If it ever fills, grow it.
- **Fire is hold-the-aim-stick past the dead zone. There is no fire button** (spec §4.2). The release edge stays as the tap/flick case. Desktop keeps space.
- **The reload button and the interact button never overlap, and neither ever moves** (spec §5). Their rects are pure functions of the safe frame and the point scale, and do not depend on game state.
- **The loot panel lives in the left half and suppresses the move stick while it is open** (spec §4.5). The aim stick, fire and reload keep working untouched.
- **NO BINARY ASSETS, EVER.** Every brush is constructed in C++; every font is `FCoreStyle`'s. No `.uasset`, no icon, no texture.
- `UCLASS(config = Game)` → `Config/DefaultGame.ini`. `config = Engine` → `Config/DefaultEngine.ini`. Putting one in the other loads nothing and warns about nothing.
- **RPC inputs are hostile.** Bounds-check every client-supplied index; re-measure every distance from the server's own copy of the pawn.
- **Verify only with `./Scripts/run-tests.sh`.** `UnrealEditor-Cmd` exits 0 having run zero tests — a silent green. The script fails on a zero-test run, on a stale module dylib, and while any process holds the project open.
- **Automation runs `-nullrhi` and can see nothing.** Every visual claim needs a `-RenderOffscreen` frame that a human reads: `Scripts/inventory-shot.sh`, `Scripts/hud-shot.sh`, `Scripts/shelter-shot.sh`. `sarko.SafeArea.DebugPhoneLandscape 1` fakes phone insets on a Mac.
- **Test counts in this plan are relative and must be recomputed at execution time.** At writing the suite reported **124** UE tests and **94** Go tests; every "expect `B + n`" below means "the baseline you measured before this task, plus n". Run the suite once before Task 1 and write the real baseline down.

---

## File Structure

```
SarkoGame/
├── Config/DefaultGame.ini                        # T1: pocket/backpack grid dimensions replace the two cell counts
├── Data/
│   ├── Items/items.json                          # T1: every item gains "size": [w, h]
│   ├── Loot/loot-tables.json                     # T3: backpack in good + military
│   └── Maps/bridge.json                          # T3: crate 0 grants the bag; 9 mm re-authored to one stack
└── Source/SarkoGame/
    ├── Loot/
    │   ├── SarkoItemCatalog.h/.cpp               # T1: FSarkoItemDef gains Width/Height; the parser requires them
    │   ├── SarkoItemGrid.h/.cpp                  # T1: NEW — the whole placement model, pure
    │   └── SarkoBackpack.h/.cpp                  # T1: carry pages, cells used, cells total; AddToBackpack retires
    ├── Core/
    │   ├── SarkoRaidSettings.h                   # T1: grid dimensions; T7: AimFireDeadZone
    │   └── SarkoPlayerController.h/.cpp          # T6: move-stick suppression; T7: button rects, hold-to-fire, reload
    ├── Net/SarkoBackendClient.h/.cpp             # T2: CraftVehicle + ParseCraftResponse
    ├── Shelter/
    │   ├── SarkoShelterView.h/.cpp               # T2: FSarkoGarageView; T4: the stash as stacks, sorted
    │   ├── SarkoShelterWidget.h/.cpp             # T2: the craft button; T4: the stash grid
    │   └── SarkoShelterPlayerController.h/.cpp   # T2: the craft call and what it says afterwards
    ├── Pawn/SarkoCharacter.h/.cpp                # T1: take path uses the grid; T7: ServerRequestReload
    └── UI/
        ├── SarkoCellWidgets.h/.cpp               # T4: NEW — the shared cell, used by both grids
        ├── SarkoInventoryPanel.h/.cpp            # T5: two pages + the refusal ghost; T6: the panel moves left
        ├── SarkoInventoryStyle.h/.cpp            # T5: the refusal ghost's brush ladder
        └── SarkoHUD.h/.cpp                       # T1: n/m is cells; T7: the reload and interact buttons
```

`sarko-api/` is **not modified by this plan.** `POST /v1/garage/craft` already exists and is already tested; item sizes are a client-side concern and the Go drift test reads only `id` and `stackSize`, so a new `size` field passes through it untouched.

---

## Layout

Every number below is in **points on the 844 × 390 landscape design canvas** (`UI/SarkoUiScale.h`). Rects are quoted for a 14 Pro in landscape, whose safe frame is `Min (59, 0)`, `Max (785, 369)` — 59 pt of Dynamic Island inset on each side and 21 pt of home indicator along the bottom.

### Grid geometry

| thing | value |
|---|---|
| cell | **44 pt** (`SarkoUI::CellSizePt`, unchanged — the tap-target minimum exactly) |
| gutter | **4 pt** (`CellGutterPt`) → a **48 pt** pitch |
| an item of `w × h` draws as | `w·44 + (w−1)·4` **×** `h·44 + (h−1)·4` — one rounded box, not w·h boxes |
| 1×1 / 2×1 / 2×2 | 44×44 / 92×44 / 92×92 |
| 3×1 / 3×2 / 4×2 | 140×44 / 140×92 / 188×92 |
| pockets page (2×2) | **92 × 92** |
| backpack page (4×2) | **188 × 92** |
| gap between the two pages | **10 pt** |
| container row (4 cells) | **188 × 44** |

### The container panel — bottom **left** (spec §4.5)

```
                        318
  ┌──────────────────────────────────────────┐  12  pad
  │  ОБШУК / MILITARY            ЗАБРАТИ ВСЕ │  44  take-all row  (the one tap target here)
  │                                          │   6  gap
  │  [44][44][44][44]                        │  44  container row (188 wide, left-aligned)
  │  ────────────────────────────────────────│  12  divider
  │  КИШЕНІ 4/4      РЮКЗАК 8/8   НЕ ВЛІЗЕ 2×1│ 16  header row (labels sit over their own page)
  │                                          │   6  gap
  │  ┌──92──┐  10  ┌────────188────────┐     │  92  pockets 2×2  |  backpack 4×2
  │  └───────┘     └───────────────────┘     │  12  pad
  └──────────────────────────────────────────┘
   14                                      14
```

- **Width 318 pt** = `14 + (92 + 10 + 188) + 14`. **Height 244 pt** = `12 + 44 + 6 + 44 + 12 + 16 + 6 + 92 + 12`.
- **Both are constants.** The panel no longer changes size with capacity: when no bag is worn the 188×92 backpack page is still drawn, as a dimmed outline labelled `НЕМАЄ РЮКЗАКА`. That is not decoration — it is the space a bag would give you, shown next to the 2-wide pockets that a 3-wide rifle cannot enter. The grid explains itself, and nothing reflows when a bag is found mid-raid.
- Anchored **bottom-left of the safe frame**: left inset 16, bottom inset 20 → **x [75, 393], y [125, 349]**.
- The pawn is at canvas centre **x = 422**, clear of the panel's right edge by **29 pt**. The whole right half — the aim stick's quadrant, the reload button, the interact button and the ground the player is shooting at — is untouched.
- Entry slide is now **from the left**: `−24 pt → 0`.

### The stash — shelter, right column

- Right column width = `724 · 0.58 − 14` = **405.9 pt**. At a 48 pt pitch that is **8 columns** = `8·44 + 7·4` = **380 pt**, leaving ~26 pt for the scroll bar.
- Scroll viewport ≈ **271 pt** = 5 full rows (236 pt) with a 6th row peeking, which is the affordance that says it scrolls.
- Rows = `SarkoGrid::StashRowsFor(stacks, catalog, 8, /*MinRows*/ 5)` — grows with the content, never below 5, so an empty stash draws a grid rather than a sliver.
- Sorted by category then display name then id, so the same item is always in the same place.

### The thumb column — right edge, fixed

```
                                    ┌──────────────┐   157
                                    │  ОБШУКАТИ    │   96 × 48
                                    └──────────────┘   205
                                              12 pt gap
                                        ┌────────┐     217
                                        │   30   │     56 × 56
                                        └────────┘     273
                                                        …96 pt of thumb room…
                                                       369  safe frame bottom
```

| control | size | rect (14 Pro landscape) | anchored |
|---|---|---|---|
| **reload** | 56 × 56 | **x [713, 769], y [217, 273]** | right edge `Max.X − 16`, bottom edge `Max.Y − 96` |
| **interact** | 96 × 48 | **x [673, 769], y [157, 205]** | right edge `Max.X − 16`, bottom edge = reload's top − 12 |

- **Neither rect depends on any game state.** The interact button appearing (it dims rather than vanishing) cannot move the reload button, because both are computed from the safe frame alone. `SarkoInput::InteractButtonRectBesidePanel` is **deleted** — the loot panel is in the other half now, so the shifted rect has nothing left to avoid.
- Both clear 44 pt in both dimensions. The 12 pt gap is the only thing between them and they never overlap.
- **Thumb arc.** The aim thumb's working anchor is `(Max.X − 90, Max.Y − 60)` = **(695, 309)**. The reload button's nearest corner is **40.2 pt** away — inside the 40–45 pt travel arc the spec describes, so it is pressed without the thumb leaving its post. The interact button's nearest edge is **104 pt** away — deliberately *outside* that arc, so working the stick can never brush it, and comfortably inside a landscape thumb's full reach. That asymmetry is the design: reload is a mid-fight reflex, interact is a decision you have already stopped moving to make.
- Interact top edge is at y 157; the health bar occupies y [16, 27] and the extraction banner y ≈ 118 top-centre. Nothing collides.
- Interact label is 12 pt type. `ОБШУКАТИ` is 8 Cyrillic capitals ≈ 67 pt wide inside a 96 pt button; `ЗАКРИТИ` is narrower. Reload label is the magazine count at 20 pt, centred.

---

### Task 1: Items have size, and one pure module does the placing

The model change, with **no UI change at all**. After this task the raid plays exactly as it does today — the panel still draws a flat grid of 44 pt squares — except that whether a take fits is decided by shape rather than by counting slots, and the HUD's `n/m` counts cells rather than stacks. Everything else in the plan builds on this module.

**Files:**
- Modify: `SarkoGame/Data/Items/items.json` (every item gains `size`)
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h`, `.cpp`
- Create: `SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h`, `.cpp` (`TransferOne`'s limit argument)
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, `SarkoGame/Config/DefaultGame.ini`
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp` (`TakeSlotInto`)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp` (`DrawBackpack`), `UI/SarkoInventoryPanel.cpp` (`PlayerCells`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (+4), `Tests/ExtractionTest.cpp` (capacity references)

**Interfaces:**
- Consumes: `FSarkoItemStack`, `FSarkoItemCatalog`, `SarkoLoot::GetItemCatalog()` (unchanged).
- Produces, and every later task uses these exact names:
  - `FSarkoItemDef::Width`, `FSarkoItemDef::Height` (`int32`, both ≥ 1)
  - `struct FSarkoGridPage { int32 Columns; int32 Rows; }`
  - `struct FSarkoGridSlot { int32 Page; int32 X; int32 Y; int32 W; int32 H; }` — `Page == INDEX_NONE` means unplaced
  - `SarkoGrid::SizeOf(const FSarkoItemCatalog&, FName) -> FIntPoint`
  - `SarkoGrid::CarryPages(bool bBackpackWorn, FIntPoint Pockets, FIntPoint Backpack) -> TArray<FSarkoGridPage>`
  - `SarkoGrid::Place(const TArray<FSarkoItemStack>&, const FSarkoItemCatalog&, const TArray<FSarkoGridPage>&) -> TArray<FSarkoGridSlot>`
  - `SarkoGrid::AddToGrid(TArray<FSarkoItemStack>&, const FSarkoItemCatalog&, const TArray<FSarkoGridPage>&, FName, int32) -> int32` (the remainder)
  - `SarkoGrid::UsedCells(const TArray<FSarkoItemStack>&, const FSarkoItemCatalog&) -> int32`
  - `SarkoGrid::TotalCells(const TArray<FSarkoGridPage>&) -> int32`
  - `USarkoBackpackComponent::GetCarryPages() -> TArray<FSarkoGridPage>`, `GetUsedCells()`, `GetCellCount()`

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`, and add `#include "Loot/SarkoItemGrid.h"` at the top with the other includes:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoItemSizesMatchTheDesignTable,
	"Sarko.Loot.ItemSizesMatchTheDesignTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoItemSizesMatchTheDesignTable::RunTest(const FString& Parameters)
{
	// THIS IS THE BALANCE GUARD. Spec §5: "Sizes are balance, not decoration.
	// Making the rifle 3 wide is what makes backpacks matter; if a later item is
	// sized carelessly the rule quietly stops holding." The size table lives in
	// Data/Items/items.json; this is what stops it drifting. The literals below
	// are spec §1.1 copied verbatim, and the check runs in BOTH directions, so
	// adding an item without a row here fails just as loudly as resizing one.
	struct FRow { const TCHAR* Id; int32 W; int32 H; };
	static const FRow Table[] = {
		{ TEXT("pistol"),       2, 1 },
		{ TEXT("ammo_9mm"),     1, 1 },
		{ TEXT("medkit"),       1, 1 },
		{ TEXT("bandage"),      1, 1 },
		{ TEXT("painkillers"),  1, 1 },
		{ TEXT("scrap_metal"),  1, 1 },
		{ TEXT("copper_wire"),  1, 1 },
		{ TEXT("duct_tape"),    1, 1 },
		{ TEXT("canned_food"),  1, 1 },
		{ TEXT("vodka"),        1, 1 },
		{ TEXT("cigarettes"),   1, 1 },
		{ TEXT("toolbox"),      2, 1 },
		{ TEXT("backpack"),     2, 2 },
		{ TEXT("bike_frame"),   3, 2 },
		{ TEXT("wheel_small"),  2, 2 },
		{ TEXT("chain"),        1, 1 },
	};

	FSarkoItemCatalog Catalog;
	FString Error;
	if (!TestTrue(TEXT("the shipped catalog loads"), SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error)))
	{
		AddError(Error);
		return false;
	}

	for (const FRow& Row : Table)
	{
		const FSarkoItemDef* Def = Catalog.Find(FName(Row.Id));
		if (!Def)
		{
			AddError(FString::Printf(TEXT("the design table has '%s' and items.json does not"), Row.Id));
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s width"), Row.Id), Def->Width, Row.W);
		TestEqual(*FString::Printf(TEXT("%s height"), Row.Id), Def->Height, Row.H);
	}
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		const bool bListed = Algo::AnyOf(Table, [&Def](const FRow& Row) { return FName(Row.Id) == Def.Id; });
		TestTrue(*FString::Printf(
				TEXT("'%s' has a row in the design table — a new item must be sized deliberately, in the spec"),
				*Def.Id.ToString()),
			bListed);

		// Structural, not editorial: nothing may be bigger than the largest page
		// the game has, or it is an item no player could ever pick up.
		TestTrue(*FString::Printf(TEXT("'%s' fits the backpack page"), *Def.Id.ToString()),
			Def.Width >= 1 && Def.Height >= 1 && Def.Width <= 4 && Def.Height <= 2);
	}

	// The two promises spec §1.1/§1.2 make about the shape of the game.
	const FSarkoItemDef* Pistol = Catalog.Find(TEXT("pistol"));
	TestTrue(TEXT("the pistol is the weapon you can always carry: it fits 2x2 pockets"),
		Pistol && Pistol->Width <= 2 && Pistol->Height <= 2);
	const bool bSomethingNeedsABag = Catalog.Items.ContainsByPredicate(
		[](const FSarkoItemDef& Def) { return Def.Width > 2; });
	TestTrue(TEXT("at least one item is wider than the pockets, or the backpack means nothing"),
		bSomethingNeedsABag);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoFirstFitPlacesAndRefusesByShape,
	"Sarko.Loot.FirstFitPlacesAndRefusesByShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoFirstFitPlacesAndRefusesByShape::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	// Pockets alone: 2x2. A 2x1 pistol fits; a second 2x1 fits under it; a third
	// does not, and — the load-bearing case — a 3-wide item NEVER fits, whatever
	// the pockets are holding. That is spec §1.2's whole argument, as an assert.
	const TArray<FSarkoGridPage> Pockets = SarkoGrid::CarryPages(false, FIntPoint(2, 2), FIntPoint(4, 2));
	TestEqual(TEXT("without a bag there is exactly one page"), Pockets.Num(), 1);

	TArray<FSarkoItemStack> Bag;
	TestEqual(TEXT("a pistol fits the pockets"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("pistol"), 1), 0);
	TestEqual(TEXT("a toolbox fits under it"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("toolbox"), 1), 0);
	TestEqual(TEXT("a bandage does not — the pockets are full by shape, not by count"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("bandage"), 1), 1);

	TArray<FSarkoItemStack> Empty;
	TestEqual(TEXT("a 3x2 frame cannot enter empty 2x2 pockets at all"),
		SarkoGrid::AddToGrid(Empty, Catalog, Pockets, TEXT("bike_frame"), 1), 1);

	// With a bag: two pages, and the wide thing lands on the second one.
	const TArray<FSarkoGridPage> Worn = SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));
	TestEqual(TEXT("wearing a bag there are two pages"), Worn.Num(), 2);
	TestEqual(TEXT("twelve cells with a bag"), SarkoGrid::TotalCells(Worn), 12);
	TestEqual(TEXT("four without"), SarkoGrid::TotalCells(Pockets), 4);

	TArray<FSarkoItemStack> Deep;
	TestEqual(TEXT("a frame fits once a bag is worn"),
		SarkoGrid::AddToGrid(Deep, Catalog, Worn, TEXT("bike_frame"), 1), 0);
	const TArray<FSarkoGridSlot> Where = SarkoGrid::Place(Deep, Catalog, Worn);
	TestEqual(TEXT("and it is on the backpack page, not in the pockets"), Where[0].Page, 1);
	TestEqual(TEXT("at the top-left of it"), Where[0].X, 0);
	TestEqual(TEXT("at the top-left of it"), Where[0].Y, 0);
	TestEqual(TEXT("occupying three by two"), Where[0].W, 3);
	TestEqual(TEXT("occupying three by two"), Where[0].H, 2);

	// First fit is left-to-right, top-to-bottom, page 0 first — and it BACKFILLS:
	// a 1x1 arriving after a 2x1 skipped a single trailing cell must land in that
	// cell. Without backfill an exactly-packed bag strands its last item.
	TArray<FSarkoItemStack> Order;
	SarkoGrid::AddToGrid(Order, Catalog, Worn, TEXT("bandage"), 1);      // pockets (0,0)
	SarkoGrid::AddToGrid(Order, Catalog, Worn, TEXT("toolbox"), 1);      // 2 wide: skips to (0,1)
	SarkoGrid::AddToGrid(Order, Catalog, Worn, TEXT("medkit"), 1);       // backfills pockets (1,0)
	const TArray<FSarkoGridSlot> Back = SarkoGrid::Place(Order, Catalog, Worn);
	TestEqual(TEXT("the bandage takes the first cell"), Back[0].X, 0);
	TestEqual(TEXT("the bandage takes the first cell"), Back[0].Y, 0);
	TestEqual(TEXT("the toolbox needs two abreast, so it drops a row"), Back[1].Y, 1);
	TestEqual(TEXT("the medkit backfills the hole the toolbox skipped"), Back[2].X, 1);
	TestEqual(TEXT("the medkit backfills the hole the toolbox skipped"), Back[2].Y, 0);

	// An unplaceable stack is reported, never silently dropped: the panel has to
	// be able to say which one failed.
	TArray<FSarkoItemStack> TooMuch = { FSarkoItemStack{ TEXT("bike_frame"), 1 } };
	const TArray<FSarkoGridSlot> Refused = SarkoGrid::Place(TooMuch, Catalog, Pockets);
	TestEqual(TEXT("one slot per stack, always"), Refused.Num(), 1);
	TestEqual(TEXT("and an unplaceable one says so"), Refused[0].Page, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAddToGridTopsUpBeforeItOpensARectangle,
	"Sarko.Loot.AddToGridTopsUpBeforeItOpensARectangle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAddToGridTopsUpBeforeItOpensARectangle::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridPage> Pockets = SarkoGrid::CarryPages(false, FIntPoint(2, 2), FIntPoint(4, 2));

	// A stack occupies one rectangle regardless of count (spec §1.1). Topping up
	// an existing partial stack costs no space at all, which is why it must be
	// tried before a new rectangle is opened — otherwise four cells fill with
	// half-empty stacks and the grid stops meaning anything.
	TArray<FSarkoItemStack> Bag;
	TestEqual(TEXT("thirty rounds open one rectangle"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("ammo_9mm"), 30), 0);
	TestEqual(TEXT("one rectangle"), Bag.Num(), 1);
	TestEqual(TEXT("thirty more top it up to the 60-round cap and open a second"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("ammo_9mm"), 30), 0);
	TestEqual(TEXT("still one rectangle, now full"), Bag.Num(), 1);
	TestEqual(TEXT("sixty rounds in it"), Bag[0].Quantity, 60);
	TestEqual(TEXT("one used cell"), SarkoGrid::UsedCells(Bag, Catalog), 1);

	// An unknown id is refused whole, exactly as AddToBackpack refused it: a
	// guessed size would put an id the backend rejects into a raid result.
	TestEqual(TEXT("an unknown item is refused whole"),
		SarkoGrid::AddToGrid(Bag, Catalog, Pockets, TEXT("not_a_real_item"), 5), 5);

	// A partial fit is allowed and reports the remainder — the vanishing-loot
	// rule: what does not fit stays in the crate.
	TArray<FSarkoItemStack> Nearly;
	SarkoGrid::AddToGrid(Nearly, Catalog, Pockets, TEXT("toolbox"), 1);   // 2x1 at (0,0)
	SarkoGrid::AddToGrid(Nearly, Catalog, Pockets, TEXT("bandage"), 5);   // 1x1 at (0,1)
	TestEqual(TEXT("one cell left, so 60 of 120 rounds fit and 60 do not"),
		SarkoGrid::AddToGrid(Nearly, Catalog, Pockets, TEXT("ammo_9mm"), 120), 60);
	TestEqual(TEXT("the grid is now full by area"), SarkoGrid::UsedCells(Nearly, Catalog), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCarryPagesFollowTheWornBag,
	"Sarko.Loot.CarryPagesFollowTheWornBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCarryPagesFollowTheWornBag::RunTest(const FString& Parameters)
{
	// Two grids, not one growing grid (spec §1.2): the player must be able to see
	// at a glance what survives losing the bag.
	const TArray<FSarkoGridPage> None = SarkoGrid::CarryPages(false, FIntPoint(2, 2), FIntPoint(4, 2));
	TestEqual(TEXT("pockets are page 0 and are 2x2"), None[0].Columns, 2);
	TestEqual(TEXT("pockets are page 0 and are 2x2"), None[0].Rows, 2);

	const TArray<FSarkoGridPage> Worn = SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));
	TestEqual(TEXT("the pockets are still page 0, unchanged"), Worn[0].Columns, 2);
	TestEqual(TEXT("the bag is page 1 and is 4x2"), Worn[1].Columns, 4);
	TestEqual(TEXT("the bag is page 1 and is 4x2"), Worn[1].Rows, 2);

	// A nonsense configuration must not produce a negative-area page that Place
	// would then index into.
	const TArray<FSarkoGridPage> Broken = SarkoGrid::CarryPages(true, FIntPoint(-3, 0), FIntPoint(0, -1));
	TestEqual(TEXT("a broken configuration yields no usable cells, not a crash"),
		SarkoGrid::TotalCells(Broken), 0);
	return true;
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Loot
```
Expected: **BUILD FAILED**, because `Loot/SarkoItemGrid.h` does not exist yet. That is the correct first failure.

- [ ] **Step 3: The catalog carries a size**

In `SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h`, add to `FSarkoItemDef` after `StackSize`:

```cpp
	/**
	 * The rectangle this item occupies, in whole cells. No rotation: every item
	 * is authored in the orientation it occupies (spec §1).
	 *
	 * Authored in items.json and REQUIRED there, not defaulted — spec §5 names
	 * careless sizing as the way the "a rifle needs a backpack" rule quietly
	 * stops holding, and a field that may be omitted is a field that will be.
	 * Sarko.Loot.ItemSizesMatchTheDesignTable is the second half of that guard.
	 */
	UPROPERTY()
	int32 Width = 1;

	UPROPERTY()
	int32 Height = 1;
```

In `SarkoItemCatalog.cpp`, inside `ParseItemCatalog`'s per-item loop, after the `stackSize` block and before the category block:

```cpp
		const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
		if (!(*Object)->TryGetArrayField(TEXT("size"), Size) || !Size || Size->Num() != 2)
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'size' must be [width, height] in whole cells"), *Id);
			return false;
		}
		Def.Width = static_cast<int32>((*Size)[0]->AsNumber());
		Def.Height = static_cast<int32>((*Size)[1]->AsNumber());
		if (Def.Width < 1 || Def.Height < 1)
		{
			OutError = FString::Printf(
				TEXT("item '%s': 'size' is %dx%d; both must be at least 1"), *Id, Def.Width, Def.Height);
			return false;
		}
```

- [ ] **Step 4: Author the sizes**

Rewrite `SarkoGame/Data/Items/items.json`'s `items` array. Only `size` is added; every id, name, stackSize and category is untouched, because the ids are the wire contract with `sarko-api`'s `stash_items.item_id`.

```json
  "items": [
    { "id": "pistol",       "name": "Пістолет ПМ",          "stackSize": 1,  "size": [2, 1], "category": "weapon" },
    { "id": "ammo_9mm",     "name": "Патрони 9×18",         "stackSize": 60, "size": [1, 1], "category": "ammo" },
    { "id": "medkit",       "name": "Аптечка",              "stackSize": 3,  "size": [1, 1], "category": "med" },
    { "id": "bandage",      "name": "Бинт",                 "stackSize": 5,  "size": [1, 1], "category": "med" },
    { "id": "painkillers",  "name": "Обезболювальне",       "stackSize": 5,  "size": [1, 1], "category": "med" },
    { "id": "scrap_metal",  "name": "Металолом",            "stackSize": 10, "size": [1, 1], "category": "junk" },
    { "id": "copper_wire",  "name": "Мідний дріт",          "stackSize": 10, "size": [1, 1], "category": "junk" },
    { "id": "duct_tape",    "name": "Армований скотч",      "stackSize": 5,  "size": [1, 1], "category": "junk" },
    { "id": "canned_food",  "name": "Консерви",             "stackSize": 5,  "size": [1, 1], "category": "valuable" },
    { "id": "vodka",        "name": "Горілка",              "stackSize": 3,  "size": [1, 1], "category": "valuable" },
    { "id": "cigarettes",   "name": "Цигарки",              "stackSize": 5,  "size": [1, 1], "category": "valuable" },
    { "id": "toolbox",      "name": "Ящик з інструментами", "stackSize": 1,  "size": [2, 1], "category": "valuable" },
    { "id": "bike_frame",   "name": "Рама велосипеда",      "stackSize": 1,  "size": [3, 2], "category": "vehicle_part" },
    { "id": "wheel_small",  "name": "Мале колесо",          "stackSize": 2,  "size": [2, 2], "category": "vehicle_part" },
    { "id": "chain",        "name": "Ланцюг",               "stackSize": 1,  "size": [1, 1], "category": "vehicle_part" },
    { "id": "backpack",     "name": "Рюкзак",               "stackSize": 1,  "size": [2, 2], "category": "gear" }
  ]
```

Also extend the file's `_readme` with one sentence: `"size is [width, height] in whole cells and is REQUIRED; the table is spec 2026-08-05 §1.1 and Sarko.Loot.ItemSizesMatchTheDesignTable holds both halves of it."`

**`sarko-api` needs no change.** `loot_test.go`'s `TestKnownItemsMatchTheClientCatalog` unmarshals into a struct carrying only `id` and `stackSize`; Go's default decoder ignores unknown fields, so `size` passes through it. Verified by reading `sarko-api/internal/domain/loot_test.go:153–170`, not assumed.

- [ ] **Step 5: The grid module**

Create `SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoItemGrid.generated.h"

/**
 * One rectangular grid of whole cells. Pockets are 2x2; a worn backpack adds a
 * separate 4x2; the shelter's stash is one wide page that grows.
 */
USTRUCT()
struct FSarkoGridPage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Columns = 0;

	UPROPERTY()
	int32 Rows = 0;

	int32 Cells() const { return FMath::Max(0, Columns) * FMath::Max(0, Rows); }
};

/**
 * Where one stack sits: which page, its top-left cell, and the rectangle it
 * occupies. Page == INDEX_NONE means it could not be placed at all.
 *
 * DERIVED, never stored and never replicated. The authority is the ORDER of the
 * stacks in the array, which is already replicated (USarkoBackpackComponent::
 * Slots) and already the wire shape the backend takes. Placement is a pure
 * function of that order, so the server, the owning client and the panel all
 * compute the same layout without a byte of new traffic — and topping up a
 * stack changes a Quantity, never an index, so nothing on screen ever moves.
 */
USTRUCT()
struct FSarkoGridSlot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Page = INDEX_NONE;

	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;

	UPROPERTY()
	int32 W = 1;

	UPROPERTY()
	int32 H = 1;

	bool IsPlaced() const { return Page != INDEX_NONE; }
};

/**
 * Spatial placement, entirely pure: catalog and arrays in, arrays out. No
 * component, no world, no settings object.
 *
 * That is deliberate and load-bearing. This is the rule that decides whether a
 * haul survives a raid, and it is the rule three screens have to agree about —
 * the raid's panel, the shelter's stash and the HUD's readout — so it is unit
 * tested with no world, no actor and no Slate application, under -nullrhi.
 */
namespace SarkoGrid
{
	/** An item's rectangle. 1x1 for an id the catalog does not know, so an
	 *  unknown id can never claim more room than the smallest thing in the game
	 *  — AddToGrid refuses it outright anyway. */
	FIntPoint SizeOf(const FSarkoItemCatalog& Catalog, FName Item);

	/**
	 * The pages a pawn carries: pockets always, the backpack only while one is
	 * worn. Two pages rather than one growing grid, because the player must be
	 * able to see at a glance what survives losing the bag, and because a bag is
	 * a thing you find, wear and lose as a unit (spec §1.2).
	 *
	 * Non-positive dimensions are clamped away rather than trusted: a page with
	 * a negative column count would make every bounds check below inconsistent,
	 * which is a haul that half-fits.
	 */
	TArray<FSarkoGridPage> CarryPages(bool bBackpackWorn, FIntPoint Pockets, FIntPoint Backpack);

	/** Total cells across every page. */
	int32 TotalCells(const TArray<FSarkoGridPage>& Pages);

	/** Cells these stacks occupy, by area — sum of w*h, not a count of stacks. */
	int32 UsedCells(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog);

	/**
	 * First fit: for each stack in order, scan page 0 then page 1 …, and within
	 * a page scan left to right, top to bottom, taking the first free rectangle
	 * of the right shape (spec §1).
	 *
	 * Index-aligned with Stacks — one FSarkoGridSlot per stack, always, so a
	 * caller can never mis-pair a rectangle with an item. A stack that does not
	 * fit gets Page = INDEX_NONE and is skipped; later, smaller stacks are still
	 * tried, because refusing everything after one oversized item would make a
	 * bike frame in slot 0 empty the whole panel.
	 *
	 * The scan BACKFILLS by construction: a 1x1 arriving after a 2x1 skipped a
	 * single trailing cell lands in that cell, because the scan always starts
	 * from the top-left. Without it an exactly-packed bag strands its last item,
	 * which is precisely the tutorial's case (Task 3).
	 */
	TArray<FSarkoGridSlot> Place(const TArray<FSarkoItemStack>& Stacks,
		const FSarkoItemCatalog& Catalog, const TArray<FSarkoGridPage>& Pages);

	/**
	 * Adds Quantity of Item, stacking by the catalog's stackSize, and returns how
	 * much did **not** fit.
	 *
	 * Replaces SarkoLoot::AddToBackpack. Partial stacks are topped up before any
	 * new rectangle is opened — a stack occupies one rectangle regardless of
	 * count (spec §1.1), so a top-up is free and must be tried first, or the
	 * grid fills with half-empty stacks and stops meaning anything.
	 *
	 * An unknown item is refused whole: guessing a size or a stack size would put
	 * an id the backend rejects into a raid result.
	 */
	int32 AddToGrid(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
		const TArray<FSarkoGridPage>& Pages, FName Item, int32 Quantity);
}
```

Create `SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.cpp`:

```cpp
#include "Loot/SarkoItemGrid.h"

FIntPoint SarkoGrid::SizeOf(const FSarkoItemCatalog& Catalog, FName Item)
{
	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		return FIntPoint(1, 1);
	}
	return FIntPoint(FMath::Max(1, Def->Width), FMath::Max(1, Def->Height));
}

TArray<FSarkoGridPage> SarkoGrid::CarryPages(bool bBackpackWorn, FIntPoint Pockets, FIntPoint Backpack)
{
	TArray<FSarkoGridPage> Pages;
	Pages.Add(FSarkoGridPage{ FMath::Max(0, Pockets.X), FMath::Max(0, Pockets.Y) });
	if (bBackpackWorn)
	{
		Pages.Add(FSarkoGridPage{ FMath::Max(0, Backpack.X), FMath::Max(0, Backpack.Y) });
	}
	return Pages;
}

int32 SarkoGrid::TotalCells(const TArray<FSarkoGridPage>& Pages)
{
	int32 Total = 0;
	for (const FSarkoGridPage& Page : Pages)
	{
		Total += Page.Cells();
	}
	return Total;
}

int32 SarkoGrid::UsedCells(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog)
{
	int32 Used = 0;
	for (const FSarkoItemStack& Stack : Stacks)
	{
		const FIntPoint Size = SizeOf(Catalog, Stack.Item);
		Used += Size.X * Size.Y;
	}
	return Used;
}

namespace
{
	/**
	 * The occupancy of every page, as one flat bit array per page.
	 *
	 * A TBitArray and not a TSet of coordinates: the grids in this game are at
	 * most a few hundred cells, this is walked on every take and on every panel
	 * rebuild, and a linear scan over bits is both faster and much easier to
	 * reason about than a hash lookup per candidate cell.
	 */
	struct FOccupancy
	{
		TArray<TBitArray<>> Pages;

		explicit FOccupancy(const TArray<FSarkoGridPage>& InPages)
		{
			Pages.Reserve(InPages.Num());
			for (const FSarkoGridPage& Page : InPages)
			{
				Pages.Emplace(false, Page.Cells());
			}
		}

		bool IsFree(const FSarkoGridPage& Page, int32 PageIndex, int32 X, int32 Y, int32 W, int32 H) const
		{
			if (X < 0 || Y < 0 || X + W > Page.Columns || Y + H > Page.Rows)
			{
				return false;
			}
			for (int32 Row = Y; Row < Y + H; ++Row)
			{
				for (int32 Column = X; Column < X + W; ++Column)
				{
					if (Pages[PageIndex][Row * Page.Columns + Column])
					{
						return false;
					}
				}
			}
			return true;
		}

		void Occupy(const FSarkoGridPage& Page, int32 PageIndex, int32 X, int32 Y, int32 W, int32 H)
		{
			for (int32 Row = Y; Row < Y + H; ++Row)
			{
				for (int32 Column = X; Column < X + W; ++Column)
				{
					Pages[PageIndex][Row * Page.Columns + Column] = true;
				}
			}
		}
	};

	/** First fit across every page, in page order then row-major within a page.
	 *  Returns an unplaced slot when the shape fits nowhere. */
	FSarkoGridSlot FirstFit(const FOccupancy& Occupancy, const TArray<FSarkoGridPage>& Pages, FIntPoint Size)
	{
		for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
		{
			const FSarkoGridPage& Page = Pages[PageIndex];
			// Rows outer, columns inner: "left to right, top to bottom" (spec §1).
			// Starting from (0,0) every time is what makes the scan backfill a
			// hole a wider item skipped over.
			for (int32 Y = 0; Y + Size.Y <= Page.Rows; ++Y)
			{
				for (int32 X = 0; X + Size.X <= Page.Columns; ++X)
				{
					if (Occupancy.IsFree(Page, PageIndex, X, Y, Size.X, Size.Y))
					{
						return FSarkoGridSlot{ PageIndex, X, Y, Size.X, Size.Y };
					}
				}
			}
		}
		return FSarkoGridSlot{ INDEX_NONE, 0, 0, Size.X, Size.Y };
	}
}

TArray<FSarkoGridSlot> SarkoGrid::Place(const TArray<FSarkoItemStack>& Stacks,
	const FSarkoItemCatalog& Catalog, const TArray<FSarkoGridPage>& Pages)
{
	TArray<FSarkoGridSlot> Slots;
	Slots.Reserve(Stacks.Num());

	FOccupancy Occupancy(Pages);
	for (const FSarkoItemStack& Stack : Stacks)
	{
		const FSarkoGridSlot Slot = FirstFit(Occupancy, Pages, SizeOf(Catalog, Stack.Item));
		if (Slot.IsPlaced())
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
		// An unplaced stack does NOT stop the loop: a bike frame that will not fit
		// must not hide the eight one-cell items behind it.
		Slots.Add(Slot);
	}
	return Slots;
}

int32 SarkoGrid::AddToGrid(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
	const TArray<FSarkoGridPage>& Pages, FName Item, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return 0;
	}

	const FSarkoItemDef* Def = Catalog.Find(Item);
	if (!Def)
	{
		// Refused whole. A guessed stack size or a guessed rectangle puts an id
		// the backend will reject into the raid result, and the haul dies with it.
		return Quantity;
	}

	const int32 StackSize = FMath::Max(1, Def->StackSize);
	int32 Remaining = Quantity;

	// 1. Top up existing partial stacks. Free: a stack occupies one rectangle
	//    regardless of count (spec §1.1), so this costs no space at all.
	for (FSarkoItemStack& Stack : Stacks)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Stack.Item != Item || Stack.Quantity >= StackSize)
		{
			continue;
		}
		const int32 Moved = FMath::Min(StackSize - Stack.Quantity, Remaining);
		Stack.Quantity += Moved;
		Remaining -= Moved;
	}

	if (Remaining <= 0)
	{
		return 0;
	}

	// 2. Open new rectangles while one fits. The occupancy is built once and
	//    carried across the loop rather than re-derived per stack, which for a
	//    120-round pour into a 12-cell grid is the difference between two
	//    placements and twenty-four.
	const FIntPoint Size(FMath::Max(1, Def->Width), FMath::Max(1, Def->Height));
	FOccupancy Occupancy(Pages);
	for (const FSarkoGridSlot& Slot : Place(Stacks, Catalog, Pages))
	{
		if (Slot.IsPlaced())
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
	}

	while (Remaining > 0)
	{
		const FSarkoGridSlot Slot = FirstFit(Occupancy, Pages, Size);
		if (!Slot.IsPlaced())
		{
			// No space of that shape. The remainder stays where it came from —
			// spec §1's partial fit, and the vanishing-loot rule.
			break;
		}
		Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		const int32 Moved = FMath::Min(StackSize, Remaining);
		Stacks.Add(FSarkoItemStack{ Item, Moved });
		Remaining -= Moved;
	}

	return Remaining;
}
```

- [ ] **Step 6: The settings describe grids, not counts**

In `Core/SarkoRaidSettings.h`, **replace** `BasePocketCells` and `BackpackBonusCells` with:

```cpp
	/**
	 * The pocket grid, in cells (spec §1.2). 2x2 — always present, never lost
	 * while alive. Two wide is the number that makes the rule: a 3-wide rifle
	 * cannot enter it, so the best weapons are uncarryable without a bag, and
	 * nothing has to explain that because the grid refuses.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	FIntPoint PocketGrid = FIntPoint(2, 2);

	/**
	 * What a worn backpack adds, as its OWN grid (spec §1.2). 4x2 — 4 + 8 = 12
	 * cells, which is exactly the total the previous design used, so sarko-api's
	 * domain.MaxRaidStacks (13 = twelve cells plus the worn bag) does not move.
	 * Raising either dimension without raising MaxRaidStacks makes full hauls get
	 * rejected at result time, fifteen minutes after the mistake.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	FIntPoint BackpackGrid = FIntPoint(4, 2);
```

In `Config/DefaultGame.ini`, replace the two lines `BasePocketCells=4` / `BackpackBonusCells=8` with:

```ini
PocketGrid=(X=2,Y=2)
BackpackGrid=(X=4,Y=2)
```

- [ ] **Step 7: The backpack component speaks in pages**

In `Loot/SarkoBackpack.h`, delete the declarations of `SarkoLoot::AddToBackpack` and `SarkoLoot::CapacityFor` (both are superseded), keep `SarkoLoot::BackpackItemId`, and replace `GetSlotLimit()` with:

```cpp
	/** The pages this pawn carries right now: pockets, plus the bag if one is
	 *  worn. USarkoRaidSettings' two grid dials, through SarkoGrid::CarryPages. */
	TArray<FSarkoGridPage> GetCarryPages() const;

	/** Cells occupied, by area. What the HUD's n/m now counts. */
	int32 GetUsedCells() const;

	/** Cells available, by area: 4 without a bag, 12 with one. */
	int32 GetCellCount() const;
```

Add `#include "Loot/SarkoItemGrid.h"` to that header (it returns `TArray<FSarkoGridPage>` by value). In `SarkoBackpack.cpp`, delete `CapacityFor` and `AddToBackpack` and add:

```cpp
TArray<FSarkoGridPage> USarkoBackpackComponent::GetCarryPages() const
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	return SarkoGrid::CarryPages(IsWearingBackpack(), Settings.PocketGrid, Settings.BackpackGrid);
}

int32 USarkoBackpackComponent::GetUsedCells() const
{
	return SarkoGrid::UsedCells(Slots, SarkoLoot::GetItemCatalog());
}

int32 USarkoBackpackComponent::GetCellCount() const
{
	return SarkoGrid::TotalCells(GetCarryPages());
}
```

and rewrite `AddItem`'s body's last line as:

```cpp
	return SarkoGrid::AddToGrid(Slots, SarkoLoot::GetItemCatalog(), GetCarryPages(), Item, Quantity);
```

`GetUsedSlots()` stays (it counts stacks, which is what the raid-result submission cares about) but is no longer what the HUD draws.

- [ ] **Step 8: `TransferOne` takes pages instead of a limit**

In `Loot/SarkoLootTable.h`, change the declaration and the last paragraph of its comment:

```cpp
	int32 TransferOne(TArray<FSarkoItemStack>& Container, int32 SlotIndex,
		TArray<FSarkoItemStack>& Bag, const FSarkoItemCatalog& Catalog,
		const TArray<FSarkoGridPage>& Pages);
```

with `#include "Loot/SarkoItemGrid.h"` added to that header. In `SarkoLootTable.cpp`, change the one call inside it:

```cpp
	const int32 Leftover = FMath::Clamp(
		SarkoGrid::AddToGrid(Bag, Catalog, Pages, Slot.Item, Slot.Quantity), 0, Slot.Quantity);
```

In `Pawn/SarkoCharacter.cpp`, `TakeSlotInto`'s final statement becomes:

```cpp
	return SarkoLoot::TransferOne(Inventory, SlotIndex, Bag, SarkoLoot::GetItemCatalog(),
		BackpackComponent ? BackpackComponent->GetCarryPages() : TArray<FSarkoGridPage>()) > 0;
```

The backpack-equip branch above it is unchanged: a bag is worn, not carried, and still costs no cell.

- [ ] **Step 9: The two readouts that counted slots now count cells**

In `UI/SarkoHUD.cpp`, `DrawBackpack`:

```cpp
	const int32 Used = Backpack->GetUsedCells();
	const int32 Limit = Backpack->GetCellCount();
```

`CachedBackpackUsed` / `CachedBackpackLimit` and the `%d/%d` format are unchanged — only what the two numbers mean changes, from stacks to cells, which is the truer readout now that a toolbox costs two.

In `UI/SarkoInventoryPanel.cpp`, `PlayerCells()`:

```cpp
int32 SSarkoInventoryPanel::PlayerCells() const
{
	const ASarkoCharacter* P = Pawn.Get();
	return (P && P->BackpackComponent) ? P->BackpackComponent->GetCellCount() : SarkoUI::GridColumns;
}
```

The panel's own layout is untouched in this task — it still draws a flat 4-wide grid of one-cell squares. Task 5 replaces it. Leave it alone here so the raid keeps playing.

- [ ] **Step 10: Fix every other caller**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && \
  grep -rn "AddToBackpack\|CapacityFor\|GetSlotLimit\|BasePocketCells\|BackpackBonusCells" Source/
```
Expected remaining call sites, each mechanical:
- `Tests/LootTest.cpp` and `Tests/ExtractionTest.cpp` — replace `CapacityFor(true, 4, 8)` with `SarkoGrid::CarryPages(true, FIntPoint(2,2), FIntPoint(4,2))` and `AddToBackpack(Bag, Catalog, Limit, …)` with `AddToGrid(Bag, Catalog, Pages, …)`. Any assertion phrased in *slots* becomes one phrased in *cells*.
- `Core/SarkoPlayerController.cpp`'s `SarkoDebugLoot` log line — `GetUsedCells()` / `GetCellCount()`.
- `UI/SarkoInventoryPanel.cpp`'s `PlayRefusal` — `GetUsedCells() >= GetCellCount()`.

The grep must come back empty before this step is done.

- [ ] **Step 11: Green**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/sarko-api && go test ./...
```
Expected: UE `ALL GREEN` at **`B + 4`**; Go `ok` with **no change** to its count — the backend is untouched and `TestKnownItemsMatchTheClientCatalog` must still pass over the resized `items.json`. If that Go test goes red, the `size` field broke the decoder and this step is not done.

- [ ] **Step 12: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Items/items.json SarkoGame/Config/DefaultGame.ini SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.h SarkoGame/Source/SarkoGame/Loot/SarkoItemCatalog.cpp SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.cpp SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.h SarkoGame/Source/SarkoGame/Loot/SarkoBackpack.cpp SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.h SarkoGame/Source/SarkoGame/Loot/SarkoLootTable.cpp SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp SarkoGame/Source/SarkoGame/Tests/LootTest.cpp SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp && git commit -m "feat(loot): every item is a rectangle, and one pure module does the packing"
```

---

### Task 2: The garage gets its button

The loop's missing destination, and the reason this task is second: parts have been landing in the stash since the first slice with nothing to spend them on. Nothing in the raid is touched. **The backend needs no change** — `POST /v1/garage/craft` has existed and been tested since the first slice.

**The real contract**, read from the source rather than assumed:
- `sarko-api/internal/api/router.go:40` — `mux.Handle("POST /v1/garage/craft", protected(handleGarageCraft(deps)))`. Authenticated with the ordinary Bearer JWT.
- **No request body.** `handleGarageCraft` reads nothing but the player id from the auth context.
- **200** → `{"vehicle_tier":"bicycle","unlocked_maps":["bridge","swamp"]}` (`craftResponse` in `garage_handler.go:13`).
- **409 `insufficient_items`** — the parts are not all in the stash. **409 `max_tier`** — every vehicle is built. **404 `not_found`** — no player row. **401 `unauthorized`**.
- It crafts the **next** tier, not a tier the client names: `store.CraftNextVehicle` locks `garage_progress`, calls `domain.NextTier(current)`, debits `domain.Recipe(next)` and advances — all in one transaction, so either the parts are consumed and the tier moves or nothing changes.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp` (+1), `Tests/ShelterTest.cpp` (+1)

**Interfaces:**
- Consumes: `FSarkoBackendClient::Send`, `SarkoShelter::BicycleRecipe()`, `FSarkoProfile`, `FSarkoShelterView`.
- Produces:
  - `SarkoBackend::ParseCraftResponse(const FString& Json, FString& OutTier, TArray<FString>& OutUnlockedMaps, FString& OutError) -> bool`
  - `FSarkoBackendClient::FOnCraft = TFunction<void(bool, const FString& Tier, const TArray<FString>& UnlockedMaps, const FString& Error)>`
  - `FSarkoBackendClient::CraftVehicle(FOnCraft)`
  - `struct FSarkoGarageView { FString Title; TArray<FString> PartLines; bool bCanCraft; FString CraftLabel; bool bBuilt; }`
  - `SarkoShelter::BuildGarageView(const FSarkoProfile&, bool bProfileLoaded) -> FSarkoGarageView`
  - `SarkoShelter::NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After) -> TArray<FString>`
  - `FSarkoShelterView::Garage` (an `FSarkoGarageView`), `FSarkoShelterView::CraftLine`

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCraftResponseIsParsed,
	"Sarko.Backend.CraftResponseIsParsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCraftResponseIsParsed::RunTest(const FString& Parameters)
{
	// The exact shape api/garage_handler.go's craftResponse marshals: the tier the
	// player now owns, and every map that tier unlocks (cumulatively — see
	// domain.UnlockedMaps).
	FString Tier;
	TArray<FString> Maps;
	FString Error;
	const bool bOk = SarkoBackend::ParseCraftResponse(
		TEXT("{\"vehicle_tier\":\"bicycle\",\"unlocked_maps\":[\"bridge\",\"swamp\"]}"), Tier, Maps, Error);
	TestTrue(TEXT("a well-formed craft response parses"), bOk);
	TestEqual(TEXT("the new tier"), Tier, FString(TEXT("bicycle")));
	TestEqual(TEXT("two maps"), Maps.Num(), 2);
	TestEqual(TEXT("the one it just opened"), Maps[1], FString(TEXT("swamp")));

	// A missing tier is a failed parse, not an empty string quietly shown to the
	// player as "your garage is now ''".
	TestFalse(TEXT("a response with no vehicle_tier fails"),
		SarkoBackend::ParseCraftResponse(TEXT("{\"unlocked_maps\":[\"bridge\"]}"), Tier, Maps, Error));
	TestFalse(TEXT("and says why"), Error.IsEmpty());

	// An absent unlocked_maps is tolerated: the tier is the fact that matters and
	// the map list is a courtesy the shelter uses for one sentence.
	TestTrue(TEXT("a response with no unlocked_maps still parses"),
		SarkoBackend::ParseCraftResponse(TEXT("{\"vehicle_tier\":\"bicycle\"}"), Tier, Maps, Error));
	TestEqual(TEXT("with no maps"), Maps.Num(), 0);

	// What the shelter says afterwards is a set difference, not a guess.
	const TArray<FString> Before = { TEXT("bridge") };
	const TArray<FString> After = { TEXT("bridge"), TEXT("swamp") };
	const TArray<FString> New = SarkoShelter::NewlyUnlockedMaps(Before, After);
	TestEqual(TEXT("exactly one map is new"), New.Num(), 1);
	TestEqual(TEXT("and it is the swamp"), New[0], FString(TEXT("swamp")));
	TestEqual(TEXT("crafting nothing new opens nothing"),
		SarkoShelter::NewlyUnlockedMaps(After, After).Num(), 0);
	return true;
}
```

Append to `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoGarageViewNamesTheMissingPart,
	"Sarko.Shelter.GarageViewNamesTheMissingPart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoGarageViewNamesTheMissingPart::RunTest(const FString& Parameters)
{
	// Spec §3: "A craft button, enabled only when every part is present, disabled
	// with the missing part named otherwise — never a dead button with no
	// explanation." A greyed-out button that says nothing is the thing this test
	// exists to stop shipping.
	FSarkoProfile Short;
	Short.VehicleTier = TEXT("none");
	Short.Stash = {
		FSarkoItemStack{ TEXT("bike_frame"), 1 },
		FSarkoItemStack{ TEXT("wheel_small"), 1 },   // the recipe wants two
		FSarkoItemStack{ TEXT("chain"), 1 },
	};

	const FSarkoGarageView Missing = SarkoShelter::BuildGarageView(Short, /*bProfileLoaded*/ true);
	TestFalse(TEXT("one wheel of two cannot craft"), Missing.bCanCraft);
	TestTrue(TEXT("and the label names the part that is short"),
		Missing.CraftLabel.Contains(TEXT("Мале колесо")));
	TestEqual(TEXT("one line per recipe entry"), Missing.PartLines.Num(), 3);
	TestTrue(TEXT("have/need is on the line, both numbers"),
		Missing.PartLines[1].Contains(TEXT("1/2")));

	FSarkoProfile Ready = Short;
	Ready.Stash[1].Quantity = 2;
	const FSarkoGarageView Can = SarkoShelter::BuildGarageView(Ready, /*bProfileLoaded*/ true);
	TestTrue(TEXT("every part present enables the button"), Can.bCanCraft);
	TestTrue(TEXT("and the label says what it will build"),
		Can.CraftLabel.Contains(TEXT("ВЕЛОСИПЕД")));

	// Past the starting tier the bicycle is built and there is nothing to press.
	FSarkoProfile Built;
	Built.VehicleTier = TEXT("bicycle");
	const FSarkoGarageView Done = SarkoShelter::BuildGarageView(Built, /*bProfileLoaded*/ true);
	TestTrue(TEXT("a built bicycle reports itself built"), Done.bBuilt);
	TestFalse(TEXT("and cannot be built again"), Done.bCanCraft);

	// An unfetched profile must never be read as "you own nothing": the stash is
	// exactly as unknown as the profile, and a button offered against it would be
	// a 409 waiting to happen.
	const FSarkoGarageView Unknown = SarkoShelter::BuildGarageView(FSarkoProfile(), /*bProfileLoaded*/ false);
	TestFalse(TEXT("an unknown profile cannot craft"), Unknown.bCanCraft);
	TestTrue(TEXT("and says the count is unknown"), Unknown.Title.Contains(TEXT("—")));
	return true;
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Backend
```
Expected: **BUILD FAILED** — `SarkoBackend::ParseCraftResponse` and `SarkoShelter::BuildGarageView` do not exist.

- [ ] **Step 3: The client learns to craft**

In `Net/SarkoBackendClient.h`, add to the `SarkoBackend` namespace beside `ParseProfileResponse`:

```cpp
	/**
	 * Reads `{"vehicle_tier":"bicycle","unlocked_maps":["bridge","swamp"]}` — the
	 * shape api/garage_handler.go's craftResponse marshals.
	 *
	 * `vehicle_tier` is required and a missing one fails the parse: the whole
	 * point of the call is which vehicle now exists, and an empty string shown to
	 * the player as their garage would be worse than an error. `unlocked_maps` is
	 * optional — it is derived server-side from the tier (domain.UnlockedMaps) and
	 * is only used for one sentence.
	 */
	bool ParseCraftResponse(const FString& Json, FString& OutTier,
		TArray<FString>& OutUnlockedMaps, FString& OutError);
```

and to the class:

```cpp
	using FOnCraft = TFunction<void(bool bSuccess, const FString& Tier,
		const TArray<FString>& UnlockedMaps, const FString& Error)>;

	/**
	 * POST /v1/garage/craft. **No request body** — the server reads the player id
	 * from the JWT and crafts the NEXT tier itself (store.CraftNextVehicle); a
	 * client that named a tier would be naming one it does not get to choose.
	 *
	 * 409 insufficient_items and 409 max_tier arrive here as ordinary failures
	 * with the envelope's message in Error, which the shelter shows verbatim: the
	 * only thing worse than a refused craft is a refused craft with no reason.
	 */
	void CraftVehicle(FOnCraft OnDone);
```

In `Net/SarkoBackendClient.cpp`:

```cpp
bool SarkoBackend::ParseCraftResponse(const FString& Json, FString& OutTier,
	TArray<FString>& OutUnlockedMaps, FString& OutError)
{
	OutTier.Reset();
	OutUnlockedMaps.Reset();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("/v1/garage/craft: the response is not valid JSON");
		return false;
	}
	if (!Root->TryGetStringField(TEXT("vehicle_tier"), OutTier) || OutTier.IsEmpty())
	{
		OutError = TEXT("/v1/garage/craft: the response has no 'vehicle_tier'");
		return false;
	}
	// Optional by design: absent means "the server did not say", not "no maps".
	const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
	if (Root->TryGetArrayField(TEXT("unlocked_maps"), Maps) && Maps)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Maps)
		{
			FString Map;
			if (Value->TryGetString(Map) && !Map.IsEmpty())
			{
				OutUnlockedMaps.Add(Map);
			}
		}
	}
	return true;
}

void FSarkoBackendClient::CraftVehicle(FOnCraft OnDone)
{
	// An empty body on a POST is a legitimate shape, and Send takes the verb
	// explicitly for exactly this reason — inferring "GET because there is no
	// body" would make this request indistinguishable from /v1/profile.
	Send(TEXT("POST"), TEXT("/v1/garage/craft"), FString(), /*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				// insufficient_items and max_tier land here. Error already carries
				// the endpoint, the HTTP code and the envelope's message, which is
				// what the shelter puts on screen verbatim.
				OnDone(false, FString(), TArray<FString>(), Error);
				return;
			}
			FString Tier;
			TArray<FString> Maps;
			FString ParseError;
			if (!SarkoBackend::ParseCraftResponse(Body, Tier, Maps, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FString(), TArray<FString>(), ParseError);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: crafted '%s'; %d maps unlocked"),
				*Tier, Maps.Num());
			OnDone(true, Tier, Maps, FString());
		});
}
```

- [ ] **Step 4: The view model for the garage**

In `Shelter/SarkoShelterView.h`, add above `FSarkoShelterView`:

```cpp
/**
 * The garage block: the recipe with have/need per part, and one button whose
 * label is either what it will build or what is stopping it.
 *
 * Its own struct rather than four more loose fields on FSarkoShelterView,
 * because the widget builds it as one unit and because "can this be crafted"
 * and "why not" have to be decided together — the failure mode this replaces is
 * a disabled button with no explanation (spec §3).
 */
struct FSarkoGarageView
{
	/** "ГАРАЖ: ВЕЛОСИПЕД 2/3", or "…—/3" while the profile is unknown. */
	FString Title;

	/** One "<UA name>  n/m" line per recipe entry, in the recipe's order. */
	TArray<FString> PartLines;

	/** True only when every part's full quantity is in the stash AND the profile
	 *  was actually fetched. A craft offered against an unknown stash is a 409
	 *  waiting to happen. */
	bool bCanCraft = false;

	/** "ЗІБРАТИ ВЕЛОСИПЕД", or "НЕ ВИСТАЧАЄ: Мале колесо", or "ВЕЛОСИПЕД ГОТОВИЙ". */
	FString CraftLabel;

	/** The tier is past none, so there is nothing left to press. */
	bool bBuilt = false;
};
```

Add to `FSarkoShelterView`, replacing the `GarageLine` field:

```cpp
	/** The garage block. Replaces the old one-line GarageLine. */
	FSarkoGarageView Garage;

	/** "ВІДКРИТО: SWAMP" for the rest of this shelter visit, or empty. The payoff
	 *  sentence for every raid before it (spec §3). */
	FString CraftLine;
```

Add to the `SarkoShelter` namespace:

```cpp
	/** The garage block. Pure: a profile in, strings and one bool out. */
	FSarkoGarageView BuildGarageView(const FSarkoProfile& Profile, bool bProfileLoaded);

	/** Maps in After that were not in Before, in After's order. What the shelter
	 *  says the craft just opened. */
	TArray<FString> NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After);
```

and extend `BuildView`'s signature with the craft line:

```cpp
	FSarkoShelterView BuildView(const FSarkoLastRaid& LastRaid, const FSarkoProfile& Profile,
		bool bProfileLoaded, const FString& Error, const FString& CraftLine,
		const FSarkoItemCatalog& Catalog);
```

In `Shelter/SarkoShelterView.cpp`, delete `BuildGarageLine` and `UnknownGarageLine` and add:

```cpp
FSarkoGarageView SarkoShelter::BuildGarageView(const FSarkoProfile& Profile, bool bProfileLoaded)
{
	const TArray<FSarkoItemStack> Recipe = BicycleRecipe();
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	FSarkoGarageView View;

	if (!bProfileLoaded)
	{
		// An em dash where the count goes. The recipe's entry count is a
		// client-side constant and stays real; only the held-parts number depends
		// on a stash this client has not seen. Stating "0/3" as fact under a
		// "З'ЄДНАННЯ..." status is the bug this branch exists to prevent.
		View.Title = FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД —/%d"), Recipe.Num());
		View.CraftLabel = TEXT("З'ЄДНАННЯ...");
		return View;
	}

	// Compared against the literal "none" rather than an enum, because
	// vehicle_tier is a string on the wire and an unknown future tier must not
	// crash this readout. The ladder is cumulative, so anything past none already
	// owns the bicycle — the only recipe this file mirrors.
	if (!Profile.VehicleTier.IsEmpty() && Profile.VehicleTier != TEXT("none"))
	{
		View.bBuilt = true;
		View.Title = TEXT("ГАРАЖ: ВЕЛОСИПЕД ГОТОВИЙ");
		View.CraftLabel = TEXT("ВЕЛОСИПЕД ГОТОВИЙ");
		return View;
	}

	int32 Met = 0;
	FString FirstMissing;
	View.PartLines.Reserve(Recipe.Num());
	for (const FSarkoItemStack& Part : Recipe)
	{
		const FSarkoItemStack* Held = Profile.Stash.FindByPredicate(
			[&Part](const FSarkoItemStack& Stack) { return Stack.Item == Part.Item; });
		const int32 Have = Held ? Held->Quantity : 0;
		const FSarkoItemDef* Def = Catalog.Find(Part.Item);
		// The id is the fallback, not the label: an id on screen means items.json
		// and the backend have drifted, and that should be visible.
		const FString Name = Def ? Def->Name : Part.Item.ToString();

		View.PartLines.Add(FString::Printf(TEXT("%s  %d/%d"), *Name, Have, Part.Quantity));
		if (Have >= Part.Quantity)
		{
			++Met;
		}
		else if (FirstMissing.IsEmpty())
		{
			FirstMissing = Name;
		}
	}

	View.Title = FString::Printf(TEXT("ГАРАЖ: ВЕЛОСИПЕД %d/%d"), Met, Recipe.Num());
	View.bCanCraft = (Met == Recipe.Num());
	// Never a dead button: enabled it says what it builds, disabled it says what
	// is stopping it, and there is no third state where it says nothing.
	View.CraftLabel = View.bCanCraft
		? FString(TEXT("ЗІБРАТИ ВЕЛОСИПЕД"))
		: FString::Printf(TEXT("НЕ ВИСТАЧАЄ: %s"), *FirstMissing);
	return View;
}

TArray<FString> SarkoShelter::NewlyUnlockedMaps(const TArray<FString>& Before, const TArray<FString>& After)
{
	TArray<FString> New;
	for (const FString& Map : After)
	{
		if (!Before.Contains(Map))
		{
			New.Add(Map);
		}
	}
	return New;
}
```

and in `BuildView`, replace the `GarageLine` branches with:

```cpp
	View.Garage = BuildGarageView(Profile, bProfileLoaded);
	View.CraftLine = CraftLine;

	if (bProfileLoaded)
	{
		View.StashLines = BuildStashLines(Profile, Catalog);
	}
```

- [ ] **Step 5: The button, in the shelter's left column**

In `Shelter/SarkoShelterWidget.h`, replace `TSharedPtr<STextBlock> GarageText;` with:

```cpp
	TSharedPtr<STextBlock> GarageText;
	TSharedPtr<SVerticalBox> GarageParts;
	TSharedPtr<class SButton> CraftButton;
	TSharedPtr<STextBlock> CraftLabel;
	TSharedPtr<STextBlock> CraftLineText;

	/** Read through an attribute by the craft button, so nothing has to tick to
	 *  keep it honest. False while a craft is in flight as well as when the parts
	 *  are short — a second press would be a second debit. */
	bool bCraftEnabled = false;
```

and add to the argument list:

```cpp
		/** Fired when the craft button is pressed. The controller owns the call. */
		SLATE_EVENT(FSimpleDelegate, OnCraft)
```

plus `void SetCraftInFlight(bool bInFlight);` and, under `#if !UE_BUILD_SHIPPING`, `bool SimulateCraftClickIfEnabled();` (same shape and same reasoning as `SimulateEnterRaidClickIfEnabled`).

In `SarkoShelterWidget.cpp`, the left column's `SVerticalBox` gains a garage block immediately above the status line — i.e. between the `FillHeight(1.f)` spacer and the `StatusText` slot:

```cpp
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
					[
						SAssignNew(GarageParts, SVerticalBox)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						// The payoff sentence. Empty until a craft succeeds, and it
						// stays for the rest of the visit rather than flashing: this
						// is what every raid before it was for.
						SAssignNew(CraftLineText, STextBlock)
						.Font(ShelterFont(13.f))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.85f, 0.40f)))
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SNew(SBox)
						// 48 pt tall, past the 44 pt tap-target minimum. Width is the
						// left column's, so "НЕ ВИСТАЧАЄ: Мале колесо" fits on one
						// line at 13 pt rather than being ellipsed into a button that
						// no longer explains anything.
						.HeightOverride(48.f)
						[
							SAssignNew(CraftButton, SButton)
							.ContentPadding(FMargin(14.f, 0.f))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.IsEnabled_Lambda([this]() { return bCraftEnabled; })
							.OnClicked(this, &SSarkoShelterWidget::HandleCraft)
							[
								SAssignNew(CraftLabel, STextBlock)
								.Font(ShelterFont(13.f))
								.Justification(ETextJustify::Center)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							]
						]
					]
```

The outcome and haul above it move inside an `SScrollBox` so a long haul can never push the buttons off the bottom of a 390 pt canvas — replace the `SAssignNew(HaulBox, SVerticalBox)` slot with:

```cpp
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(HaulBox, SVerticalBox)
						]
					]
```

and delete the bare `SSpacer` slot that used to do the pushing — `FillHeight(1.f)` on the scroll box does it now, and does it without ever clipping.

`SetView` gains:

```cpp
	GarageText->SetText(FText::FromString(View.Garage.Title));
	CraftLabel->SetText(FText::FromString(View.Garage.CraftLabel));
	CraftLineText->SetText(FText::FromString(View.CraftLine));
	bCraftEnabled = View.Garage.bCanCraft;

	GarageParts->ClearChildren();
	for (const FString& Line : View.Garage.PartLines)
	{
		GarageParts->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(ShelterFont(13.f))
			.ColorAndOpacity(BodyColour)
			.Text(FText::FromString(Line))
		];
	}
```

and:

```cpp
void SSarkoShelterWidget::SetCraftInFlight(bool bInFlight)
{
	// A second press while the first is in flight is a second debit. The button
	// is re-enabled by the SetView that follows the refetched profile, which is
	// also the moment the answer is actually known.
	if (bInFlight)
	{
		bCraftEnabled = false;
	}
}

FReply SSarkoShelterWidget::HandleCraft()
{
	OnCraft.ExecuteIfBound();
	return FReply::Handled();
}
```

- [ ] **Step 6: The controller makes the call**

In `Shelter/SarkoShelterPlayerController.h`, add `void Craft();`, `FString LastCraftLine;` and `bool bCraftInFlight = false;`.

In `SarkoShelterPlayerController.cpp`, bind it at construction:

```cpp
	Widget = SNew(SSarkoShelterWidget)
		.OnEnterRaid(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::EnterRaid))
		.OnCraft(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::Craft));
```

update `RefreshWidget` to pass the new argument:

```cpp
	Widget->SetView(SarkoShelter::BuildView(
		GameInstance->LastRaid, GameInstance->CachedProfile, GameInstance->bProfileLoaded,
		LastError, LastCraftLine, SarkoLoot::GetItemCatalog()));
```

and add:

```cpp
void ASarkoShelterPlayerController::Craft()
{
	if (bCraftInFlight)
	{
		return;
	}
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	const TSharedPtr<FSarkoBackendClient> Backend = GameInstance ? GameInstance->EnsureBackend() : nullptr;
	if (!Backend.IsValid())
	{
		LastError = TEXT("no backend client");
		RefreshWidget();
		return;
	}

	// Snapshotted BEFORE the call, because the answer is a set difference and the
	// profile is about to be replaced by the refetch below.
	const TArray<FString> Before = GameInstance->CachedProfile.UnlockedMaps;

	bCraftInFlight = true;
	Widget->SetCraftInFlight(true);

	// Weak: this completion routinely lands after the player has pressed В РЕЙД
	// and this controller has been destroyed by the travel.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	Backend->CraftVehicle([WeakThis, Before](bool bSuccess, const FString& Tier,
		const TArray<FString>& Maps, const FString& Error)
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bCraftInFlight = false;
		if (!bSuccess)
		{
			// insufficient_items and max_tier arrive here. Shown verbatim: a
			// refused craft with no reason is worse than no button.
			Self->LastError = Error;
			Self->RefreshWidget();
			return;
		}
		Self->LastError.Reset();

		const TArray<FString> Opened = SarkoShelter::NewlyUnlockedMaps(Before, Maps);
		Self->LastCraftLine = Opened.Num() > 0
			? FString::Printf(TEXT("ЗІБРАНО. ВІДКРИТО: %s"), *FString::Join(Opened, TEXT(", ")).ToUpper())
			: FString(TEXT("ЗІБРАНО."));

		// The parts have left the stash and the tier has moved, both server-side
		// in one transaction. Refetch rather than patch the cached profile: the
		// server's copy is the only one that knows what the debit actually took.
		Self->FetchProfile();
		Self->RefreshWidget();
	});

	RefreshWidget();
}
```

- [ ] **Step 7: Green, and look at it**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/shelter-shot.sh
```
Expected: `ALL GREEN` at **`B + 6`**. Read the PNG and confirm four things a test cannot: the garage title reads `ГАРАЖ: ВЕЛОСИПЕД n/3`; three part lines are under it with have/need; the craft button is present and its label is either `ЗІБРАТИ ВЕЛОСИПЕД` or `НЕ ВИСТАЧАЄ: <part>` and **is not ellipsed**; and the В РЕЙД / МАГАЗИН row is still fully on screen under it.

**A real craft cannot be exercised headlessly** — it needs a stash with a frame, two wheels and a chain in it, which means a real raid against the live backend. Record in the task report that the success path was verified by a played session, or that it was not.

- [ ] **Step 8: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.h SarkoGame/Source/SarkoGame/Net/SarkoBackendClient.cpp SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.cpp SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.cpp SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.h SarkoGame/Source/SarkoGame/Shelter/SarkoShelterPlayerController.cpp SarkoGame/Source/SarkoGame/Tests/BackendClientTest.cpp SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp && git commit -m "feat(shelter): the garage gets its button, so the parts have somewhere to go"
```

---

### Task 3: The tutorial at 2×2 + 4×2

Data only, and it fixes something that is broken **on `main` right now**.

**What is actually on disk.** The container-inventory plan's Task 6 — "the first crate of the tutorial is a backpack" — was written and **never executed**. `git log` on `feat/container-inventory` ends at Task 5 (`fix(ui): a tap must not delete the button…`); `SarkoGame/Data/Maps/bridge.json` container 0 holds `scrap_metal` and nothing else, `Data/Loot/loot-tables.json` has no `backpack` entry in any tier, and neither `Sarko.Loot.TutorialGrantsABagBeforeItNeedsOne` nor `Sarko.Loot.TutorialHaulStillFitsAWornBag` exists in `Tests/LootTest.cpp`. Verify all three before starting — if a later commit landed them, skip Steps 3 and 5 and keep the arithmetic.

**The arithmetic, recomputed with sizes.** All 19 authored `fixedItems` lists, stacked by `items.json`:

| item | units | stacks | size | cells |
|---|---|---|---|---|
| `ammo_9mm` | 116 (12+20+24+40+20) | 2 (stack 60) | 1×1 | **2** |
| `scrap_metal` 7, `copper_wire` 9, `duct_tape` 1, `bandage` 5, `medkit` 3, `canned_food` 5, `painkillers` 3 | | 1 each | 1×1 | **7** |
| `toolbox` | 1 | 1 | 2×1 | **2** |
| `pistol` | 1 | 1 | 2×1 | **2** |
| | | **11 stacks** | | **13 cells** |

Capacity is **4** cells in the pockets and **12** with a bag. So today's authored layout is:
- **impossible without a bag** — four cells, and the toolbox alone eats half of them;
- **one cell over** even with a bag: 13 needed, 12 available. Under the old count-the-slots rule it was 11 of 12 and fitted; sizes are what took that away, because a toolbox and a pistol now cost two cells each.

**Two edits, and the tutorial fits again:**

1. **Crate 0 grants the backpack** — the change the previous plan intended. It is no longer merely the best first lesson; without it the second crate the player opens can already refuse them. Spec §6.5's teaching order becomes "a bag at spawn (this is what carrying means) → junk → medkit → ammo + valuable → military → extract".
2. **The authored 9 mm is halved to exactly 60 units**, so it is one rectangle instead of two. 9 mm is pure loot value — the in-raid weapon is abstract and reloads for free (`USarkoWeaponComponent::FinishReload` refills from `MagazineSize`), so nothing about the fight changes. 13 → **12 cells, in 12.**

**Twelve of twelve is exact, with nothing to spare, and that is stated rather than hidden.** The test below is not an area sum — it runs the real first-fit placer over the real authored haul in four different acquisition orders, because the player chooses which crate to open and area alone does not prove a packing exists. Any *future* authored item requires removing another, and the test's failure message says so.

**Files:**
- Modify: `SarkoGame/Data/Maps/bridge.json` (containers 0, 4, 6, 12, 17, 18)
- Modify: `SarkoGame/Data/Loot/loot-tables.json` (`good`, `military`)
- Modify: `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp` (+2)

**Interfaces:**
- Consumes: `SarkoGrid::Place`, `SarkoGrid::AddToGrid`, `SarkoGrid::CarryPages`, `SarkoLoot::BackpackItemId`, `SarkoMap::LoadDefinitionFromDisk`, `FSarkoLootContainerSpot::FixedItems`.
- Produces: no code. Two tests and three data guarantees.

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/LootTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialGrantsABagBeforeItNeedsOne,
	"Sarko.Loot.TutorialGrantsABagBeforeItNeedsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialGrantsABagBeforeItNeedsOne::RunTest(const FString& Parameters)
{
	// Over the REAL map file. Pocket capacity is a 2x2 grid — four cells, two of
	// them eaten by the first 2x1 the player finds — and the authored tutorial
	// yields twelve cells' worth of loot. Without a bag in the FIRST crate the
	// tutorial teaches "everything you find is refused", which is a lesson, but
	// not the one spec §6.5 is sequencing.
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

	const auto HasBag = [](const FSarkoLootContainerSpot& Spot)
	{
		return Spot.FixedItems.ContainsByPredicate(
			[](const FSarkoItemStack& Stack) { return Stack.Item == SarkoLoot::BackpackItemId; });
	};

	TestTrue(TEXT("the spawn crate carries a backpack"), HasBag(Definition.Containers[0]));
	TestTrue(TEXT("and it is FIRST in the list, so ЗАБРАТИ ВСЕ equips it before it takes anything else"),
		Definition.Containers[0].FixedItems.Num() > 0
			&& Definition.Containers[0].FixedItems[0].Item == SarkoLoot::BackpackItemId);

	int32 BagCrates = 0;
	for (const FSarkoLootContainerSpot& Spot : Definition.Containers)
	{
		BagCrates += HasBag(Spot) ? 1 : 0;
	}
	TestEqual(TEXT("and only that one, or the lesson is 'bags are everywhere'"), BagCrates, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTutorialHaulStillFitsTheGrid,
	"Sarko.Loot.TutorialHaulStillFitsTheGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTutorialHaulStillFitsTheGrid::RunTest(const FString& Parameters)
{
	// Every authored fixedItems entry, poured into one bag, must still fit
	// 2x2 pockets plus a worn 4x2 backpack — INCLUDING the bag itself, which is
	// worn and therefore costs no cell.
	//
	// Not an area sum. Area proves a bound, not a packing: the player chooses
	// which crate to open, so the acquisition order varies, and first fit with
	// mixed 1x1 and 2x1 shapes can in principle strand a wide item behind two
	// non-adjacent holes. So the REAL placer is run over the REAL data in four
	// different orders, and every one of them has to take everything.
	FSarkoMapDefinition Definition;
	FString Error;
	if (!SarkoMap::LoadDefinitionFromDisk(TEXT("bridge"), Definition, Error))
	{
		AddError(FString::Printf(TEXT("bridge.json did not load: %s"), *Error));
		return false;
	}

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridPage> Pages =
		SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));

	// Flattened once, in the authored order, minus the worn bag.
	TArray<FSarkoItemStack> Authored;
	for (const FSarkoLootContainerSpot& Spot : Definition.Containers)
	{
		for (const FSarkoItemStack& Stack : Spot.FixedItems)
		{
			if (Stack.Item != SarkoLoot::BackpackItemId)
			{
				Authored.Add(Stack);
			}
		}
	}

	const auto PourIn = [&](const TArray<FSarkoItemStack>& Order, const TCHAR* What)
	{
		TArray<FSarkoItemStack> Bag;
		int32 Refused = 0;
		for (const FSarkoItemStack& Stack : Order)
		{
			Refused += SarkoGrid::AddToGrid(Bag, Catalog, Pages, Stack.Item, Stack.Quantity);
		}
		TestEqual(*FString::Printf(
				TEXT("%s: nothing is refused. If this is red, an authored item was ADDED or grew — ")
				TEXT("the layout fits 12 of 12 cells with NOTHING to spare, so something must come out"),
				What),
			Refused, 0);
		TestTrue(*FString::Printf(TEXT("%s: %d of %d cells"), What,
				SarkoGrid::UsedCells(Bag, Catalog), SarkoGrid::TotalCells(Pages)),
			SarkoGrid::UsedCells(Bag, Catalog) <= SarkoGrid::TotalCells(Pages));
	};

	PourIn(Authored, TEXT("the authored route"));

	TArray<FSarkoItemStack> Reversed = Authored;
	Algo::Reverse(Reversed);
	PourIn(Reversed, TEXT("the route walked backwards"));

	// The two adversarial orders for first fit: every wide item first (it claims
	// whole rows), and every wide item last (it has to squeeze into what the
	// one-cell items left).
	const auto ByWidth = [&Catalog](bool bWideFirst)
	{
		return [&Catalog, bWideFirst](const FSarkoItemStack& A, const FSarkoItemStack& B)
		{
			const int32 WA = SarkoGrid::SizeOf(Catalog, A.Item).X;
			const int32 WB = SarkoGrid::SizeOf(Catalog, B.Item).X;
			return bWideFirst ? WA > WB : WA < WB;
		};
	};

	TArray<FSarkoItemStack> WideFirst = Authored;
	WideFirst.StableSort(ByWidth(true));
	PourIn(WideFirst, TEXT("the widest things first"));

	TArray<FSarkoItemStack> WideLast = Authored;
	WideLast.StableSort(ByWidth(false));
	PourIn(WideLast, TEXT("the widest things last"));
	return true;
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.Loot
```
Expected: `Sarko.Loot.TutorialGrantsABagBeforeItNeedsOne` fails on "the spawn crate carries a backpack", and `Sarko.Loot.TutorialHaulStillFitsTheGrid` fails on "nothing is refused" — the 13th cell.

- [ ] **Step 3: Crate 0 grants the bag**

In `SarkoGame/Data/Maps/bridge.json`, container index **0** (`bridge_loot_l01`) only — `fixedItems` becomes:

```json
      "fixedItems": [
        { "item": "backpack", "qty": 1 },
        { "item": "scrap_metal", "qty": 2 }
      ]
```

The backpack is **first in the list** so `ЗАБРАТИ ВСЕ` equips it before it tries to take the scrap: `ASarkoCharacter::TakeAllFrom` drains slot 0 repeatedly, and with a 2×2 pocket grid the order now genuinely matters — a 2×1 taken first would leave the bag unreachable behind a full pocket row.

Update that container's `note` to end with: `"It also holds the tutorial's only backpack: pockets are 2x2 and the authored haul is twelve cells, so the bag is the fix handed over one crate before the player could hit the wall."`

- [ ] **Step 4: The 9 mm becomes one rectangle**

In the same file, five containers' `ammo_9mm` quantities, and nothing else:

| index | id | tier | `qty` | → |
|---|---|---|---|---|
| 4 | `bridge_loot_l18` | common | 12 | **8** |
| 6 | `bridge_loot_l20` | common | 20 | **10** |
| 12 | `bridge_loot_l26` | good | 24 | **12** |
| 17 | `bridge_loot_rail_mil_01` | military | 40 | **20** |
| 18 | `bridge_loot_rail_mil_02` | military | 20 | **10** |

Total **60**, which is exactly `ammo_9mm`'s `stackSize`, so it is one full cell. The tiering is preserved — the military crate is still the richest — and the in-raid weapon is unaffected, because it is abstract and reloads for free from `MagazineSize`. **No other container and no other item changes.** Say so in the commit message; the next person to read this file needs to know the other thirteen lists were left alone deliberately.

- [ ] **Step 5: Backpacks in the seeded tables**

In `Data/Loot/loot-tables.json`, add one entry to `good` and one to `military`, and to **nowhere else** — a bag is what you go deep for, and if junk or common hands one out then `good` stops meaning anything:

```json
    { "item": "backpack", "weight": 4, "qty": { "min": 1, "max": 1 } }
```
```json
    { "item": "backpack", "weight": 3, "qty": { "min": 1, "max": 1 } }
```

Check the ТЗ §30 rule this can disturb: `Sarko.Loot.RealLootTablesObeyTheDesignRules` caps **vehicle parts** at 3 % of their tier's weight, and adding weight only raises the denominator, so every part's share falls. The `med` tier is untouched, so its "no weapons, no vehicle parts" rule is unaffected. **Re-run that test and read its output rather than trusting this paragraph** — the weights on disk may have moved since it was written.

- [ ] **Step 6: Green, and read the frame**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && INV_BAG=0 ./Scripts/inventory-shot.sh
```
Expected: `ALL GREEN` at **`B + 8`**. The shot still shows the old flat panel — the panel is rebuilt in Task 5 — so all this frame has to prove is that the raid still boots, a crate still opens and a take still works after the data edit.

- [ ] **Step 7: Commit**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko && git add SarkoGame/Data/Maps/bridge.json SarkoGame/Data/Loot/loot-tables.json SarkoGame/Source/SarkoGame/Tests/LootTest.cpp && git commit -m "feat(loot): the first crate is a backpack, because twelve cells is now a shape"
```

---

### Task 4: One cell, drawn in two places — and the stash becomes the grid

The container panel's cell is extracted into a shared builder and the shelter's stash list becomes that same grid: same brushes, same seven-hue palette, same `CellLabel`, same count badge. One visual language for "things you own" (spec §2). Nothing in the raid changes — the panel keeps calling the same code, now from a different file.

**Files:**
- Create: `SarkoGame/Source/SarkoGame/UI/SarkoCellWidgets.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp` (call the shared builders)
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h`, `.cpp` (`SortForStash`, `StashRowsFor`)
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h`, `.cpp` (stacks, not lines)
- Modify: `SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h`, `.cpp` (the grid)
- Modify: `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp` (+1, and the stash-lines assertions move)

**Interfaces:**
- Consumes: `FSarkoInventoryStyles::Get()`, `SarkoUI::CellSizePt`/`CellGutterPt`/`CellPadPt`/`CellLabel`, `SarkoGrid::Place`.
- Produces:
  - `SarkoUI::CellExtentPt(FIntPoint Size) -> FVector2D` — `(w·44 + (w−1)·4, h·44 + (h−1)·4)`
  - `SarkoUI::CellOriginPt(const FSarkoGridSlot&) -> FVector2D` — `(X·48, Y·48)` within a page
  - `SarkoUI::BuildStackCell(const FSarkoItemStack&, const FSarkoInventoryStyles&, FIntPoint Size) -> TSharedRef<SWidget>`
  - `SarkoUI::BuildEmptyCell(const FSarkoInventoryStyles&, FIntPoint Size) -> TSharedRef<SWidget>`
  - `SarkoUI::BuildGridPage(const TArray<FSarkoItemStack>&, const TArray<FSarkoGridSlot>&, int32 PageIndex, const FSarkoGridPage&, const FSarkoInventoryStyles&) -> TSharedRef<SWidget>`
  - `SarkoGrid::SortForStash(TArray<FSarkoItemStack>&, const FSarkoItemCatalog&)`
  - `SarkoGrid::StashRowsFor(const TArray<FSarkoItemStack>&, const FSarkoItemCatalog&, int32 Columns, int32 MinRows) -> int32`
  - `SarkoUI::StashColumns = 8`, `SarkoUI::StashMinRows = 5`
  - `FSarkoShelterView::StashStacks`, `FSarkoShelterView::StashNote`

- [ ] **Step 1: Write the failing test**

Append to `SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStashGridSortsAndGrows,
	"Sarko.UI.StashGridSortsAndGrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStashGridSortsAndGrows::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

	// Spec §2: sorted by category then name, "so the same item is always in the
	// same place". The server hands the stash back ordered by item id, which puts
	// ammo_9mm next to bandage and a medkit three rows from a bandage — an order
	// that is stable but means nothing to a player.
	TArray<FSarkoItemStack> Stash = {
		FSarkoItemStack{ TEXT("scrap_metal"), 4 },     // junk
		FSarkoItemStack{ TEXT("pistol"), 1 },          // weapon  — the first category
		FSarkoItemStack{ TEXT("bandage"), 5 },         // med
		FSarkoItemStack{ TEXT("ammo_9mm"), 60 },       // ammo
		FSarkoItemStack{ TEXT("medkit"), 2 },          // med, and 'Аптечка' sorts before 'Бинт'
	};
	SarkoGrid::SortForStash(Stash, Catalog);

	TestEqual(TEXT("weapons first"), Stash[0].Item, FName(TEXT("pistol")));
	TestEqual(TEXT("then ammo"), Stash[1].Item, FName(TEXT("ammo_9mm")));
	TestEqual(TEXT("then med, by display name: Аптечка before Бинт"),
		Stash[2].Item, FName(TEXT("medkit")));
	TestEqual(TEXT("then med, by display name: Аптечка before Бинт"),
		Stash[3].Item, FName(TEXT("bandage")));
	TestEqual(TEXT("then junk"), Stash[4].Item, FName(TEXT("scrap_metal")));

	// Sorting is idempotent, or the grid reshuffles every time the shelter
	// redraws and "always in the same place" is a lie.
	TArray<FSarkoItemStack> Again = Stash;
	SarkoGrid::SortForStash(Again, Catalog);
	for (int32 Index = 0; Index < Stash.Num(); ++Index)
	{
		TestEqual(TEXT("sorting twice changes nothing"), Again[Index].Item, Stash[Index].Item);
	}

	// Spec §2: "If it ever fills, grow it rather than making the player pack it."
	TestEqual(TEXT("an empty stash still draws a full grid, not a sliver"),
		SarkoGrid::StashRowsFor({}, Catalog, SarkoUI::StashColumns, SarkoUI::StashMinRows),
		SarkoUI::StashMinRows);

	// Twenty 3x2 frames is 120 cells; eight columns cannot hold that in five rows,
	// so the grid must have grown rather than refused anything.
	TArray<FSarkoItemStack> Heavy;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Heavy.Add(FSarkoItemStack{ TEXT("bike_frame"), 1 });
	}
	const int32 Rows = SarkoGrid::StashRowsFor(Heavy, Catalog, SarkoUI::StashColumns, SarkoUI::StashMinRows);
	TestTrue(*FString::Printf(TEXT("the stash grew to %d rows"), Rows), Rows > SarkoUI::StashMinRows);

	const TArray<FSarkoGridPage> Page = { FSarkoGridPage{ SarkoUI::StashColumns, Rows } };
	const TArray<FSarkoGridSlot> Placed = SarkoGrid::Place(Heavy, Catalog, Page);
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("stash item %d is placed — the stash is never a puzzle"), Index),
			Placed[Index].IsPlaced());
	}

	// The one geometric identity the two grids share: a w x h item is ONE box of
	// w cells plus the gutters between them, not w separate boxes.
	TestEqual(TEXT("a 1x1 is 44 wide"), SarkoUI::CellExtentPt(FIntPoint(1, 1)).X, 44.f);
	TestEqual(TEXT("a 2x1 is 92 wide, not 88"), SarkoUI::CellExtentPt(FIntPoint(2, 1)).X, 92.f);
	TestEqual(TEXT("a 3x2 is 140 x 92"), SarkoUI::CellExtentPt(FIntPoint(3, 2)).X, 140.f);
	TestEqual(TEXT("a 3x2 is 140 x 92"), SarkoUI::CellExtentPt(FIntPoint(3, 2)).Y, 92.f);
	TestEqual(TEXT("cell (2,1) starts at 96, 48 — a 48 pt pitch"),
		SarkoUI::CellOriginPt(FSarkoGridSlot{ 0, 2, 1, 1, 1 }).X, 96.f);
	TestEqual(TEXT("cell (2,1) starts at 96, 48 — a 48 pt pitch"),
		SarkoUI::CellOriginPt(FSarkoGridSlot{ 0, 2, 1, 1, 1 }).Y, 48.f);
	return true;
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.UI
```
Expected: **BUILD FAILED** — `SarkoGrid::SortForStash`, `SarkoGrid::StashRowsFor`, `SarkoUI::CellExtentPt`, `SarkoUI::CellOriginPt`, `SarkoUI::StashColumns` do not exist.

- [ ] **Step 3: Sorting and growing**

Add to `Loot/SarkoItemGrid.h`'s namespace:

```cpp
	/**
	 * Category, then display name, then id. The stash's order (spec §2) — the
	 * scarcity is in the raid, not in storage, so the one job this order has is
	 * that the same item is always in the same place.
	 *
	 * StableSort and a total order down to the id, so it is idempotent: sorting an
	 * already-sorted stash must not move anything, or the grid reshuffles on every
	 * redraw and "always in the same place" stops being true.
	 */
	void SortForStash(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog);

	/**
	 * How many rows a grid this wide needs to hold these stacks, never fewer than
	 * MinRows.
	 *
	 * Grown by probing rather than by dividing area by width: a 3x2 frame in an
	 * 8-wide grid can leave cells that nothing fits into, so the honest answer is
	 * the smallest row count for which Place() places everything. Bounded, because
	 * an item wider than the grid would otherwise never place and this would spin.
	 */
	int32 StashRowsFor(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
		int32 Columns, int32 MinRows);
```

and to `SarkoItemGrid.cpp`:

```cpp
void SarkoGrid::SortForStash(TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog)
{
	Stacks.StableSort([&Catalog](const FSarkoItemStack& A, const FSarkoItemStack& B)
	{
		const FSarkoItemDef* DefA = Catalog.Find(A.Item);
		const FSarkoItemDef* DefB = Catalog.Find(B.Item);

		// An id the catalog does not know sorts last rather than first: it is the
		// visible symptom of items.json drifting from the backend, and it belongs
		// at the bottom of the grid where it is odd rather than at the top where
		// it looks like the most important thing the player owns.
		const int32 CategoryA = DefA ? static_cast<int32>(DefA->Category) : MAX_int32;
		const int32 CategoryB = DefB ? static_cast<int32>(DefB->Category) : MAX_int32;
		if (CategoryA != CategoryB)
		{
			return CategoryA < CategoryB;
		}
		const FString NameA = DefA ? DefA->Name : A.Item.ToString();
		const FString NameB = DefB ? DefB->Name : B.Item.ToString();
		if (NameA != NameB)
		{
			return NameA < NameB;
		}
		// The id is the tiebreaker, so the order is TOTAL and therefore idempotent.
		return A.Item.LexicalLess(B.Item);
	});
}

int32 SarkoGrid::StashRowsFor(const TArray<FSarkoItemStack>& Stacks, const FSarkoItemCatalog& Catalog,
	int32 Columns, int32 MinRows)
{
	const int32 SafeColumns = FMath::Max(1, Columns);
	int32 Rows = FMath::Max(1, MinRows);

	// The bound: one row per stack plus the tallest item is always enough, and it
	// stops an item wider than the grid — which can never place — from spinning
	// this forever. Such an item is a data bug the catalog's own size test
	// catches; this is only here so a bad file cannot hang the shelter.
	const int32 Ceiling = FMath::Max(Rows, Stacks.Num() * 2 + 2);
	while (Rows < Ceiling)
	{
		const TArray<FSarkoGridPage> Page = { FSarkoGridPage{ SafeColumns, Rows } };
		const TArray<FSarkoGridSlot> Slots = Place(Stacks, Catalog, Page);
		const bool bAllPlaced = !Slots.ContainsByPredicate(
			[](const FSarkoGridSlot& Slot) { return !Slot.IsPlaced(); });
		if (bAllPlaced)
		{
			return Rows;
		}
		++Rows;
	}
	return Rows;
}
```

- [ ] **Step 4: The shared cell**

Create `SarkoGame/Source/SarkoGame/UI/SarkoCellWidgets.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemGrid.h"

class SWidget;
struct FSarkoInventoryStyles;

/**
 * The cell, once, for every grid in the game.
 *
 * Extracted out of SSarkoInventoryPanel rather than reimplemented in the
 * shelter: spec §2 asks the stash to reuse "the container panel's brushes,
 * palette and labels — one visual language for 'things you own'", and two
 * implementations of one look diverge on the first tweak. The panel keeps its
 * animation, its buttons and its refusal signals; only what a cell *is* moved.
 *
 * NO BINARY ASSETS. Every brush comes from FSarkoInventoryStyles, which builds
 * FSlateRoundedBoxBrushes in C++; every font is FCoreStyle's.
 */
namespace SarkoUI
{
	/** The stash's grid, in the shelter's right column. Eight columns is what a
	 *  405.9 pt column holds at a 48 pt pitch: 8*44 + 7*4 = 380, leaving room for
	 *  the scroll bar. Five rows is what 271 pt of viewport shows, with a sixth
	 *  peeking so the grid visibly scrolls. */
	constexpr int32 StashColumns = 8;
	constexpr int32 StashMinRows = 5;

	/** A w x h item is ONE rounded box spanning its cells and the gutters between
	 *  them — 2x1 is 92 pt wide, not two 44 pt boxes with a seam down the middle.
	 *  The seam is what would make a rifle read as two objects. */
	FVector2D CellExtentPt(FIntPoint Size);

	/** A slot's top-left corner within its page, at the 48 pt pitch. */
	FVector2D CellOriginPt(const FSarkoGridSlot& Slot);

	/** An occupied cell: category fill, category rim, the shortened label, and the
	 *  count when there is more than one. Not a button — the panel wraps it in
	 *  one where a tap means something, and the stash never does. */
	TSharedRef<SWidget> BuildStackCell(const FSarkoItemStack& Stack,
		const FSarkoInventoryStyles& Styles, FIntPoint Size);

	/** An empty slot: a thinner rim and a body barely above the plate. */
	TSharedRef<SWidget> BuildEmptyCell(const FSarkoInventoryStyles& Styles, FIntPoint Size);

	/**
	 * One whole page, drawn absolutely: an SConstraintCanvas sized to the page,
	 * with every empty cell laid down first and every placed stack over it at its
	 * own rectangle.
	 *
	 * A canvas and not nested boxes, because a 2x1 item spans two columns and a
	 * row of SBoxes cannot express that without the caller tracking which cells a
	 * previous row already claimed — which is the placement logic, written a
	 * second time, in Slate. The layout is already computed; this only draws it.
	 */
	TSharedRef<SWidget> BuildGridPage(const TArray<FSarkoItemStack>& Stacks,
		const TArray<FSarkoGridSlot>& Slots, int32 PageIndex,
		const FSarkoGridPage& Page, const FSarkoInventoryStyles& Styles);
}
```

Create `SarkoGame/Source/SarkoGame/UI/SarkoCellWidgets.cpp`. `BuildStackCell` and `BuildEmptyCell` are `SSarkoInventoryPanel::BuildCellContent` / `BuildEmptyCell` moved verbatim, with the hard-coded `CellSizePt` replaced by `CellExtentPt(Size)`:

```cpp
#include "UI/SarkoCellWidgets.h"

#include "Loot/SarkoItemCatalog.h"
#include "Styling/CoreStyle.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FSlateFontInfo CellFont(float Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", Size);
	}

	ESarkoItemCategory CategoryOf(FName Item)
	{
		const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Item);
		return Def ? Def->Category : ESarkoItemCategory::Junk;
	}
}

FVector2D SarkoUI::CellExtentPt(FIntPoint Size)
{
	const float W = FMath::Max(1, Size.X);
	const float H = FMath::Max(1, Size.Y);
	return FVector2D(W * CellSizePt + (W - 1.f) * CellGutterPt,
		H * CellSizePt + (H - 1.f) * CellGutterPt);
}

FVector2D SarkoUI::CellOriginPt(const FSarkoGridSlot& Slot)
{
	const float Pitch = CellSizePt + CellGutterPt;
	return FVector2D(Slot.X * Pitch, Slot.Y * Pitch);
}

TSharedRef<SWidget> SarkoUI::BuildStackCell(const FSarkoItemStack& Stack,
	const FSarkoInventoryStyles& Styles, FIntPoint Size)
{
	const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Stack.Item);
	const FString Label = CellLabel(Def ? Def->Name : Stack.Item.ToString());

	TSharedRef<SOverlay> Content = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);

	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
	[
		SNew(STextBlock)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.Font(CellFont(CellLabelPt))
		.ColorAndOpacity(FSlateColor(CellLabelColour))
		// Ellipsis, never a clip: a word running out of its cell reads as a
		// rendering fault.
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		.Text(FText::FromString(Label))
	];

	// Only when there is more than one: "1" on every cell is noise, and the count
	// is meant to be the thing the eye stops on when it matters.
	if (Stack.Quantity > 1)
	{
		Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.Font(CellFont(CellCountPt))
			.ColorAndOpacity(FSlateColor(CellCountColour))
			.Text(FText::AsNumber(Stack.Quantity))
		];
	}

	const FVector2D Extent = CellExtentPt(Size);
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(Extent.X)
		.HeightOverride(Extent.Y)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(&Styles.CellByCategory[static_cast<int32>(CategoryOf(Stack.Item))].Normal)
			.Padding(FMargin(CellPadPt))
			[
				Content
			]
		];
}

TSharedRef<SWidget> SarkoUI::BuildEmptyCell(const FSarkoInventoryStyles& Styles, FIntPoint Size)
{
	const FVector2D Extent = CellExtentPt(Size);
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(Extent.X)
		.HeightOverride(Extent.Y)
		[
			SNew(SBorder)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.BorderImage(&Styles.EmptyCell.Normal)
		];
}

TSharedRef<SWidget> SarkoUI::BuildGridPage(const TArray<FSarkoItemStack>& Stacks,
	const TArray<FSarkoGridSlot>& Slots, int32 PageIndex,
	const FSarkoGridPage& Page, const FSarkoInventoryStyles& Styles)
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
		.Visibility(EVisibility::SelfHitTestInvisible);

	const auto Put = [&Canvas](TSharedRef<SWidget> Widget, FVector2D Origin, FVector2D Extent)
	{
		Canvas->AddSlot()
			// Anchors at the top-left and an explicit offset: the page's own size is
			// fixed by the SBox around it, so absolute placement in points is exactly
			// what the already-computed layout says.
			.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
			.Alignment(FVector2D(0.f, 0.f))
			.AutoSize(false)
			.Offset(FMargin(Origin.X, Origin.Y, Extent.X, Extent.Y))
			[
				Widget
			];
	};

	// Every cell as an empty slot first, so the grid reads as a grid even when it
	// is nearly bare — then the occupied rectangles over the top of them.
	for (int32 Y = 0; Y < Page.Rows; ++Y)
	{
		for (int32 X = 0; X < Page.Columns; ++X)
		{
			Put(BuildEmptyCell(Styles, FIntPoint(1, 1)),
				CellOriginPt(FSarkoGridSlot{ PageIndex, X, Y, 1, 1 }), CellExtentPt(FIntPoint(1, 1)));
		}
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FSarkoGridSlot& Slot = Slots[Index];
		if (Slot.Page != PageIndex || !Stacks.IsValidIndex(Index))
		{
			continue;
		}
		Put(BuildStackCell(Stacks[Index], Styles, FIntPoint(Slot.W, Slot.H)),
			CellOriginPt(Slot), CellExtentPt(FIntPoint(Slot.W, Slot.H)));
	}

	const float Pitch = SarkoUI::CellSizePt + SarkoUI::CellGutterPt;
	return SNew(SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.WidthOverride(FMath::Max(0, Page.Columns) * Pitch - SarkoUI::CellGutterPt)
		.HeightOverride(FMath::Max(0, Page.Rows) * Pitch - SarkoUI::CellGutterPt)
		[
			Canvas
		];
}
```

Then in `UI/SarkoInventoryPanel.cpp`, delete `BuildCellContent` and `BuildEmptyCell` and route the two remaining callers through `SarkoUI::BuildStackCell` / `SarkoUI::BuildEmptyCell` with `FIntPoint(1, 1)`. The panel's own grid is rebuilt properly in Task 5; this step only removes the duplication and must leave the frame looking identical.

- [ ] **Step 5: The shelter's stash becomes stacks**

In `Shelter/SarkoShelterView.h`, replace `TArray<FString> StashLines;` with:

```cpp
	/**
	 * The stash, as stacks rather than strings, already sorted for the grid.
	 * **Empty — not a message** — while the profile has not been fetched: an
	 * unfetched profile carries an empty stash, and telling a player their haul
	 * vanished is the single worst thing this screen can do.
	 */
	TArray<FSarkoItemStack> StashStacks;

	/** "СХОВОК ПОРОЖНІЙ" for a fetched-but-empty stash, or empty. Drawn OVER the
	 *  grid, so an empty stash still shows the grid it will fill. */
	FString StashNote;
```

and replace `BuildStashLines` with:

```cpp
	/** The stash, sorted by category then name (spec §2). The catalog is passed
	 *  in rather than fetched, so this stays pure and testable under -nullrhi. */
	TArray<FSarkoItemStack> BuildStashStacks(const FSarkoProfile& Profile, const FSarkoItemCatalog& Catalog);
```

In `SarkoShelterView.cpp`:

```cpp
TArray<FSarkoItemStack> SarkoShelter::BuildStashStacks(const FSarkoProfile& Profile,
	const FSarkoItemCatalog& Catalog)
{
	TArray<FSarkoItemStack> Stacks = Profile.Stash;
	// Rows with nothing in them would draw as empty cells that are not empty
	// slots — a hole in the grid the player cannot fill.
	Stacks.RemoveAll([](const FSarkoItemStack& Stack) { return Stack.Quantity <= 0; });
	SarkoGrid::SortForStash(Stacks, Catalog);
	return Stacks;
}
```

and in `BuildView`:

```cpp
	if (bProfileLoaded)
	{
		View.StashStacks = BuildStashStacks(Profile, Catalog);
		if (View.StashStacks.Num() == 0)
		{
			View.StashNote = TEXT("СХОВОК ПОРОЖНІЙ");
		}
	}
```

- [ ] **Step 6: The shelter draws the grid**

In `Shelter/SarkoShelterWidget.h`, replace `TSharedPtr<SVerticalBox> StashBox;` with `TSharedPtr<class SBox> StashBox;` and add `TSharedPtr<STextBlock> StashNoteText;`.

In `SarkoShelterWidget.cpp`, the right column's scroll box slot becomes:

```cpp
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								// The grid can be any height, which is the reason this
								// screen is Slate and not DrawHUD primitives. It grows
								// downward and scrolls; it is never packed by the player
								// (spec §2).
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(StashBox, SBox)
								]
							]

							+ SOverlay::Slot()
							.HAlign(HAlign_Center).VAlign(VAlign_Top)
							.Padding(0.f, 24.f, 0.f, 0.f)
							[
								// Over the grid, not instead of it: an empty stash still
								// shows the shape it will fill.
								SAssignNew(StashNoteText, STextBlock)
								.Font(ShelterFont(15.f))
								.ColorAndOpacity(LabelColour)
							]
						]
```

and `SetView`'s stash half becomes:

```cpp
	StashNoteText->SetText(FText::FromString(View.StashNote));

	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const int32 Rows = SarkoGrid::StashRowsFor(View.StashStacks, Catalog,
		SarkoUI::StashColumns, SarkoUI::StashMinRows);
	const FSarkoGridPage Page{ SarkoUI::StashColumns, Rows };
	const TArray<FSarkoGridSlot> Slots =
		SarkoGrid::Place(View.StashStacks, Catalog, { Page });

	// Rebuilt wholesale, once per profile fetch and once per craft — never per
	// frame. Slate is not a tick path.
	StashBox->SetContent(SarkoUI::BuildGridPage(View.StashStacks, Slots, /*PageIndex*/ 0,
		Page, *FSarkoInventoryStyles::Get()));
```

Delete the `Fill(StashBox, View.StashLines, 15.f)` call; `Fill` stays for `HaulBox`.

**The shelter's ~9 % scale deviation is unchanged and deliberate.** `SSarkoShelterWidget` uses `PointScaleForViewport`, not `SarkoUI::OverlayPointScale`, so it renders ~9 % over its stated point sizes — a known deviation validated by screenshot at that size. Adopting the overlay scale here is a one-line change gated on a fresh `Scripts/shelter-shot.sh`, and it is **not** part of this task: changing it would move every number in this layout at the same moment the layout changed.

- [ ] **Step 7: Green, and read the frame**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/shelter-shot.sh
```
Expected: `ALL GREEN` at **`B + 9`**. Read the PNG against five checks:
1. the stash is a grid of rounded cells, not a list of lines;
2. eight columns fit inside the right column with the scroll bar clear of them;
3. a multi-cell item (a toolbox, if the test stash has one) is **one** box two cells wide, with no seam;
4. the categories' hues are the same hues the raid panel uses;
5. the garage block, the craft button and the В РЕЙД row from Task 2 are all still on screen and unclipped.

- [ ] **Step 8: Commit**

```bash
git add SarkoGame/Source/SarkoGame/UI/SarkoCellWidgets.h SarkoGame/Source/SarkoGame/UI/SarkoCellWidgets.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.cpp SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.h SarkoGame/Source/SarkoGame/Shelter/SarkoShelterView.cpp SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.h SarkoGame/Source/SarkoGame/Shelter/SarkoShelterWidget.cpp SarkoGame/Source/SarkoGame/Tests/ShelterTest.cpp && git commit -m "feat(ui): the stash is the same grid as the crate, drawn by the same cell"
```

---

### Task 5: The panel draws two grids, and a refusal says *no space of that shape*

The panel stops being a flat row of squares and becomes what the model has been since Task 1: a 2×2 pocket page beside a 4×2 backpack page, with every item drawn at its true rectangle. And it answers spec §5's named risk — *"Auto-placement can refuse for a reason the player cannot see. 'There is room but it will not fit' is the classic spatial-grid frustration."*

**How a refusal is explained.** Four signals, at once, and the fourth is the new one:

1. the container cell that was tapped **shakes**, ±4 pt over two cycles (unchanged);
2. the carry area's rim **pulses amber** for `NoSpace` only, so a shake with no amber means "you moved" (unchanged);
3. the `РЮКЗАК n/m` header **turns amber and stays amber** while the grid is full — a state, not a flash (unchanged);
4. **a ghost of the exact rectangle that failed** is drawn over the carry grids for 240 ms, in amber outline, at `SarkoGrid::RefusalAnchor` — *the free cell with the longest run of free cells to its right* — so it visibly **overhangs the occupied cell that blocked it**; and the header row's right end reads **`НЕ ВЛІЗЕ 2×1`** for the same 240 ms.

That is the concrete answer to "there is room but it will not fit": the player sees how many cells are free (the grid), what shape was needed (the words), and where the shape was tried and what stopped it (the ghost, sitting half on a gap and half on the thing in the way). The anchor is chosen to be the *most convincing* gap — the one the player's eye is already on, thinking "but there is space right there" — which is exactly the cell the ghost has to overhang to answer them.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h`, `.cpp` (`RefusalAnchor`)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.h`, `.cpp` (the ghost's brush ladder)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp` (+2)

**Interfaces:**
- Consumes: `SarkoGrid::Place`, `SarkoUI::BuildGridPage`, `SarkoUI::CellExtentPt`, `SarkoUI::CellOriginPt`, `USarkoBackpackComponent::GetCarryPages`.
- Produces:
  - `SarkoGrid::RefusalAnchor(const TArray<FSarkoGridSlot>& Placed, const TArray<FSarkoGridPage>&, FIntPoint Size) -> FSarkoGridSlot`
  - `FSarkoInventoryStyles::RefusalGhost[GlowSteps]`, `RefusalGhostFor(float) -> const FSlateBrush*`
  - `SarkoUI::PanelWidthPt = 318`, `SarkoUI::PanelHeightPt = 244`, `SarkoUI::PagesGapPt = 10`
  - `SarkoUI::InventoryPanelRect(FBox2D SafeFrame, float PointScale) -> FBox2D` (**the `PlayerCells` parameter is gone** — the panel is a fixed size now)

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRefusalAnchorOverhangsWhatBlockedIt,
	"Sarko.UI.RefusalAnchorOverhangsWhatBlockedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRefusalAnchorOverhangsWhatBlockedIt::RunTest(const FString& Parameters)
{
	// Spec §5: "The refusal must say why — no space of that shape — and the panel
	// should show the shape that failed." A ghost dropped at (0,0) would sit on a
	// full row and say nothing. It has to land on the gap the player is looking
	// at, so that it visibly runs OUT of that gap into the cell in the way.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridPage> Pages = SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));

	// Backpack row 0: [x][ ][x][ ]  — two single-cell gaps, neither wide enough
	// for a 2x1. The anchor must be one of those gaps, not the origin.
	TArray<FSarkoItemStack> Bag = {
		FSarkoItemStack{ TEXT("bandage"), 1 },        // pockets (0,0)
		FSarkoItemStack{ TEXT("medkit"), 1 },         // pockets (1,0)
		FSarkoItemStack{ TEXT("painkillers"), 1 },    // pockets (0,1)
		FSarkoItemStack{ TEXT("chain"), 1 },          // pockets (1,1) — pockets full
		FSarkoItemStack{ TEXT("scrap_metal"), 1 },    // backpack (0,0)
		FSarkoItemStack{ TEXT("copper_wire"), 1 },    // backpack (1,0)
		FSarkoItemStack{ TEXT("duct_tape"), 1 },      // backpack (2,0)
		FSarkoItemStack{ TEXT("canned_food"), 1 },    // backpack (3,0) — row 0 full
		FSarkoItemStack{ TEXT("vodka"), 1 },          // backpack (0,1)
		FSarkoItemStack{ TEXT("cigarettes"), 1 },     // backpack (1,1)
	};
	const TArray<FSarkoGridSlot> Placed = SarkoGrid::Place(Bag, Catalog, Pages);
	for (const FSarkoGridSlot& Slot : Placed)
	{
		TestTrue(TEXT("the setup itself fits"), Slot.IsPlaced());
	}

	// Two free cells left, (2,1) and (3,1) on the backpack page — adjacent, so a
	// 2x1 WOULD fit and must not be refused at all.
	TArray<FSarkoItemStack> Copy = Bag;
	TestEqual(TEXT("a 2x1 still fits the adjacent pair"),
		SarkoGrid::AddToGrid(Copy, Catalog, Pages, TEXT("toolbox"), 1), 0);

	// Now fill (2,1) so the only free cell is (3,1): a lone gap, and a 2x1 cannot
	// use it. THIS is the case the ghost exists for.
	Bag.Add(FSarkoItemStack{ TEXT("wheel_small"), 0 });   // placeholder, replaced below
	Bag.Pop();
	Bag.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 1 });      // backpack (2,1)
	const TArray<FSarkoGridSlot> Nearly = SarkoGrid::Place(Bag, Catalog, Pages);

	const FSarkoGridSlot Ghost = SarkoGrid::RefusalAnchor(Nearly, Pages, FIntPoint(2, 1));
	TestTrue(TEXT("the ghost is anchored somewhere"), Ghost.IsPlaced());
	TestEqual(TEXT("on the page that has the gap"), Ghost.Page, 1);
	TestEqual(TEXT("at the lone free cell"), Ghost.X, 3);
	TestEqual(TEXT("at the lone free cell"), Ghost.Y, 1);
	TestEqual(TEXT("drawn at the size that FAILED, not at the size that fits"), Ghost.W, 2);
	TestEqual(TEXT("drawn at the size that FAILED, not at the size that fits"), Ghost.H, 1);

	// A completely full grid still has to anchor somewhere, or there is no ghost
	// and the loudest refusal is the quietest one.
	TArray<FSarkoItemStack> Full;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		SarkoGrid::AddToGrid(Full, Catalog, Pages, TEXT("chain"), 1);
	}
	const FSarkoGridSlot NoRoom =
		SarkoGrid::RefusalAnchor(SarkoGrid::Place(Full, Catalog, Pages), Pages, FIntPoint(2, 1));
	TestTrue(TEXT("a full grid still anchors the ghost, at the origin"), NoRoom.IsPlaced());
	TestEqual(TEXT("at the origin"), NoRoom.X, 0);
	TestEqual(TEXT("at the origin"), NoRoom.Y, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelGeometryIsFixed,
	"Sarko.UI.PanelGeometryIsFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelGeometryIsFixed::RunTest(const FString& Parameters)
{
	// The panel is one size, always. It used to grow with capacity, which meant
	// finding a backpack mid-raid reflowed the thing the player was reading. Now
	// the backpack page is drawn whether or not one is worn — dimmed when it is
	// not, which is the space a bag would give you, next to the 2-wide pockets a
	// 3-wide rifle cannot enter.
	TestEqual(TEXT("14 + (92 + 10 + 188) + 14"), SarkoUI::PanelWidthPt, 318.f);
	TestEqual(TEXT("12 + 44 + 6 + 44 + 12 + 16 + 6 + 92 + 12"), SarkoUI::PanelHeightPt, 244.f);

	// The pages, in points, from the same identity the cells use.
	TestEqual(TEXT("the pocket page is 92 square"), SarkoUI::CellExtentPt(FIntPoint(2, 2)).X, 92.f);
	TestEqual(TEXT("the backpack page is 188 x 92"), SarkoUI::CellExtentPt(FIntPoint(4, 2)).X, 188.f);
	TestEqual(TEXT("the container row is 188 wide, so it fits inside the carry band"),
		SarkoUI::CellExtentPt(FIntPoint(SarkoLoot::ContainerCells, 1)).X, 188.f);

	// The 44 pt tap-target rule applies to the ONE thing in this panel that is
	// tappable. The carry cells are SelfHitTestInvisible by design — the thumb
	// aims through them — so they carry no minimum.
	TestTrue(TEXT("a container cell clears the tap-target minimum"),
		SarkoUI::CellSizePt >= 44.f);
	TestTrue(TEXT("so does the take-all row"), SarkoUI::TakeAllRowPt >= 44.f);
	return true;
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko.UI
```
Expected: **BUILD FAILED** — `SarkoGrid::RefusalAnchor` and `SarkoUI::PanelHeightPt` do not exist.

- [ ] **Step 3: Where a refusal is drawn**

Add to `Loot/SarkoItemGrid.h`'s namespace:

```cpp
	/**
	 * Where to draw the rectangle that would NOT fit, so the player can see why.
	 *
	 * The free cell with the longest run of free cells to its right, scanning
	 * pages in order and rows top to bottom — i.e. the most convincing gap on
	 * screen, the one the player is already looking at thinking "but there is
	 * space right there". Drawn at the refused Size, the ghost then runs out of
	 * that gap and over the cell that actually blocked it, which is the whole
	 * argument made in one rectangle.
	 *
	 * A completely full grid anchors at page 0's origin rather than returning
	 * nothing: the loudest refusal must not be the one that draws nothing.
	 *
	 * The returned slot carries the REFUSED size, not a size that fits — it is a
	 * ghost, not a placement, and it is expected to overhang the page.
	 */
	FSarkoGridSlot RefusalAnchor(const TArray<FSarkoGridSlot>& Placed,
		const TArray<FSarkoGridPage>& Pages, FIntPoint Size);
```

and to `SarkoItemGrid.cpp`:

```cpp
FSarkoGridSlot SarkoGrid::RefusalAnchor(const TArray<FSarkoGridSlot>& Placed,
	const TArray<FSarkoGridPage>& Pages, FIntPoint Size)
{
	const int32 W = FMath::Max(1, Size.X);
	const int32 H = FMath::Max(1, Size.Y);

	FOccupancy Occupancy(Pages);
	for (const FSarkoGridSlot& Slot : Placed)
	{
		if (Slot.IsPlaced() && Pages.IsValidIndex(Slot.Page))
		{
			Occupancy.Occupy(Pages[Slot.Page], Slot.Page, Slot.X, Slot.Y, Slot.W, Slot.H);
		}
	}

	FSarkoGridSlot Best{ 0, 0, 0, W, H };
	int32 BestRun = -1;
	for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
	{
		const FSarkoGridPage& Page = Pages[PageIndex];
		for (int32 Y = 0; Y < Page.Rows; ++Y)
		{
			for (int32 X = 0; X < Page.Columns; ++X)
			{
				if (!Occupancy.IsFree(Page, PageIndex, X, Y, 1, 1))
				{
					continue;
				}
				int32 Run = 0;
				while (X + Run < Page.Columns && Occupancy.IsFree(Page, PageIndex, X + Run, Y, 1, 1))
				{
					++Run;
				}
				// Strictly greater, so the FIRST longest run wins — the gap nearest
				// the top-left, which is where the eye starts.
				if (Run > BestRun)
				{
					BestRun = Run;
					Best = FSarkoGridSlot{ PageIndex, X, Y, W, H };
				}
			}
		}
	}
	return Best;
}
```

`FOccupancy` is currently in an anonymous namespace in that .cpp; it is already above this function, so nothing moves.

- [ ] **Step 4: The ghost's brush**

In `UI/SarkoInventoryStyle.h`, beside `RefusalGlow` and `TransferFlash`:

```cpp
	/**
	 * The refused rectangle's outline: amber, transparent-bodied, in the same
	 * twelve baked opacities as the two ladders above and for exactly the same
	 * reason — SBorder folds a brush's own tint into what it draws with
	 * (SBorder.cpp:115), so a transparent BODY has a final alpha of zero and
	 * FSlateDrawElement culls the whole element, outline included
	 * (DrawElementTypes.cpp:154). bUseBrushTransparency stays FALSE here, which
	 * makes the outline colour verbatim; the price is that each rung carries its
	 * own alpha.
	 *
	 * Thicker than the pulse (3 pt against 2) because it is drawn OVER occupied
	 * cells that already have a 1.5 pt rim of their own, and a ghost the same
	 * weight as the thing it is overhanging reads as part of it.
	 */
	FSlateBrush RefusalGhost[GlowSteps];

	const FSlateBrush* RefusalGhostFor(float Alpha) const;
```

In `SarkoInventoryStyle.cpp`, inside the existing `for (int32 Step = 0; …)` loop:

```cpp
		FSlateRoundedBoxBrush Ghost(FLinearColor::Transparent, SarkoUI::CellRadiusPt,
			SarkoUI::AmberWarn.CopyWithNewOpacity(Alpha), 3.f);
		Ghost.OutlineSettings.bUseBrushTransparency = false;
		RefusalGhost[Step] = Ghost;
```

and beside the other two accessors:

```cpp
const FSlateBrush* FSarkoInventoryStyles::RefusalGhostFor(float Alpha) const
{
	return RungFor(RefusalGhost, GlowSteps, Alpha);
}
```

- [ ] **Step 5: The panel's new geometry**

In `UI/SarkoInventoryPanel.h`, replace the width/height constants and the two functions:

```cpp
	/** The two carry pages, side by side with a gap between them. */
	constexpr float PagesGapPt = 10.f;

	/** 14 + (92 pockets + 10 gap + 188 backpack) + 14. A CONSTANT: the panel no
	 *  longer grows with capacity, so finding a bag mid-raid cannot reflow the
	 *  thing the player is reading. */
	constexpr float PanelWidthPt = 318.f;

	/** 12 + 44 take-all + 6 + 44 container + 12 divider + 16 header + 6 + 92 cells + 12. */
	constexpr float PanelHeightPt = 244.f;

	/**
	 * Where the panel goes, in whatever unit SafeFrame is in. Pure, so the one
	 * property that decides whether a player can see the bot walking at them is
	 * unit tested without a viewport, a widget or a Slate application.
	 *
	 * Bottom-LEFT since 2026-08-05 (spec §4.5): it used to sit over the aim
	 * stick, where a thumb reaching to shoot could land on a cell instead. See
	 * Task 6 — moving it is only half the fix; the other half is that the move
	 * stick sleeps while it is open.
	 */
	FBox2D InventoryPanelRect(FBox2D SafeFrame, float PointScale);
```

Delete `PlayerGridRows`, `InventoryPanelHeightPt`, `PanelRightInsetPt` and `GridColumns`; add `constexpr float PanelLeftInsetPt = 16.f;`. In the .cpp:

```cpp
FBox2D SarkoUI::InventoryPanelRect(FBox2D SafeFrame, float PointScale)
{
	const float Width = PanelWidthPt * PointScale;
	const float Height = PanelHeightPt * PointScale;
	const FVector2D Min(SafeFrame.Min.X + PanelLeftInsetPt * PointScale,
		SafeFrame.Max.Y - PanelBottomInsetPt * PointScale - Height);
	return FBox2D(Min, Min + FVector2D(Width, Height));
}
```

`PanelPadding()` loses its `PlayerCells()` argument; `PanelHeight()` returns the constant; `PlayerCells()` is deleted.

- [ ] **Step 6: Two pages, drawn from the layout**

Replace `SSarkoInventoryPanel`'s `PlayerGrid` member with:

```cpp
	/** One box per carry page, refilled by Refresh. Pockets is always page 0. */
	TSharedPtr<class SBox> PocketPage;
	TSharedPtr<class SBox> BackpackPage;
	TSharedPtr<STextBlock> PocketHeader;
	TSharedPtr<STextBlock> RefusalNote;

	/** The refused rectangle and where to draw it, valid only while RefusalCurve
	 *  is playing and LastRefusal is NoSpace. */
	FSarkoGridSlot RefusedGhost;
```

The vertical stack's header row becomes an `SHorizontalBox` carrying `PocketHeader` (`КИШЕНІ n/m`), `BackpackHeader` (`РЮКЗАК n/m`) and, right-aligned, `RefusalNote`. Below it, an `SOverlay` whose first slot is an `SHorizontalBox` of the two page boxes with `PagesGapPt` between them, whose second slot is the existing amber pulse `SBorder`, and whose third is the ghost:

```cpp
							// The ghost: the exact rectangle that would not fit, laid
							// over the carry pages at RefusalAnchor's gap, so it runs
							// out of that gap and over the cell that blocked it. This
							// is the answer to "there is room but it will not fit".
							+ SOverlay::Slot()
							.HAlign(HAlign_Left).VAlign(VAlign_Top)
							[
								SNew(SBox)
								.Visibility(EVisibility::SelfHitTestInvisible)
								.Padding(TAttribute<FMargin>::CreateSP(this, &SSarkoInventoryPanel::GhostPadding))
								.WidthOverride(TAttribute<FOptionalSize>::CreateSP(this, &SSarkoInventoryPanel::GhostWidth))
								.HeightOverride(TAttribute<FOptionalSize>::CreateSP(this, &SSarkoInventoryPanel::GhostHeight))
								[
									SNew(SBorder)
									.Visibility(EVisibility::SelfHitTestInvisible)
									.BorderImage(TAttribute<const FSlateBrush*>::CreateSP(
										this, &SSarkoInventoryPanel::RefusalGhostBrush))
								]
							]
```

with:

```cpp
FMargin SSarkoInventoryPanel::GhostPadding() const
{
	// Page 0 starts at x 0; page 1 starts one pocket page plus the gap to its
	// right. Both are inside the DPI scaler, so these are points outright.
	const FVector2D Origin = SarkoUI::CellOriginPt(RefusedGhost);
	const float PageOffset = RefusedGhost.Page == 1
		? SarkoUI::CellExtentPt(FIntPoint(2, 2)).X + SarkoUI::PagesGapPt
		: 0.f;
	return FMargin(PageOffset + Origin.X, Origin.Y, 0.f, 0.f);
}

const FSlateBrush* SSarkoInventoryPanel::RefusalGhostBrush() const
{
	// NoSpace only. A TooFar or a Gone refusal shakes and says nothing about
	// shape, because shape is not what was wrong.
	if (!RefusalCurve.IsPlaying() || LastRefusal != ESarkoTakeRefusal::NoSpace)
	{
		return nullptr;
	}
	return Styles->RefusalGhostFor(FMath::Sin(PI * RefusalCurve.GetLerp()));
}
```

(`GhostWidth`/`GhostHeight` return `SarkoUI::CellExtentPt(FIntPoint(RefusedGhost.W, RefusedGhost.H))`'s components.)

`Refresh()`'s bag half becomes:

```cpp
	const TArray<FSarkoItemStack>& Bag = P->BackpackComponent ? P->BackpackComponent->GetSlots() : NoSlots;
	const TArray<FSarkoGridPage> Pages = P->BackpackComponent
		? P->BackpackComponent->GetCarryPages()
		: SarkoGrid::CarryPages(false, FIntPoint(2, 2), FIntPoint(4, 2));
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridSlot> Layout = SarkoGrid::Place(Bag, Catalog, Pages);

	const int32 Used = SarkoGrid::UsedCells(Bag, Catalog);
	const int32 Total = SarkoGrid::TotalCells(Pages);
	bBagFull = Used >= Total;

	// Page 0 always exists. Page 1 is drawn whether or not a bag is worn — as a
	// dimmed 4x2 outline labelled НЕМАЄ РЮКЗАКА when it is not — because that is
	// the space a bag would give you, shown beside 2-wide pockets a 3-wide rifle
	// cannot enter. Nothing reflows when a bag is found.
	const bool bWorn = Pages.Num() > 1;
	const FSarkoGridPage Pockets = Pages[0];
	const FSarkoGridPage Bag4x2 = bWorn ? Pages[1] : FSarkoGridPage{ 4, 2 };

	PocketHeader->SetText(FText::FromString(FString::Printf(TEXT("КИШЕНІ"))));
	BackpackHeader->SetText(FText::FromString(bWorn
		? FString::Printf(TEXT("РЮКЗАК %d/%d"), Used, Total)
		: FString(TEXT("НЕМАЄ РЮКЗАКА"))));

	PocketPage->SetContent(SarkoUI::BuildGridPage(Bag, Layout, 0, Pockets, *Styles));
	BackpackPage->SetContent(bWorn
		? SarkoUI::BuildGridPage(Bag, Layout, 1, Bag4x2, *Styles)
		: SarkoUI::BuildGridPage({}, {}, 1, Bag4x2, *Styles));
	BackpackPage->SetRenderOpacity(bWorn ? 1.f : 0.45f);
```

The `ReceivingCell` transfer flash keeps working unchanged — it keys on the index of the changed stack, and `BuildGridPage` draws that stack at whatever rectangle it holds.

`PlayRefusal` gains the ghost:

```cpp
	RefusedGhost = FSarkoGridSlot{ 0, 0, 0, 1, 1 };
	if (const ASarkoCharacter* P = Pawn.Get())
	{
		if (P->BackpackComponent)
		{
			const TArray<FSarkoGridPage> Pages = P->BackpackComponent->GetCarryPages();
			const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
			bBagFull = SarkoGrid::UsedCells(P->BackpackComponent->GetSlots(), Catalog)
				>= SarkoGrid::TotalCells(Pages);

			// The size that failed comes from the container slot the player tapped —
			// the panel already has it, so no new RPC field is needed to carry it.
			const TArray<FSarkoItemStack>& Slots = P->GetOpenContainerSlots();
			const FIntPoint Size = Slots.IsValidIndex(SlotIndex)
				? SarkoGrid::SizeOf(Catalog, Slots[SlotIndex].Item)
				: FIntPoint(1, 1);
			RefusedGhost = SarkoGrid::RefusalAnchor(
				SarkoGrid::Place(P->BackpackComponent->GetSlots(), Catalog, Pages), Pages, Size);
			RefusalNote->SetText(FText::FromString(
				Reason == ESarkoTakeRefusal::NoSpace
					? FString::Printf(TEXT("НЕ ВЛІЗЕ %d×%d"), Size.X, Size.Y)
					: FString()));
		}
	}
```

`RefusalNote` is cleared on the next `Refresh()`, which a successful take always triggers.

- [ ] **Step 7: Green, and photograph the refusal**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && INV_BAG=0 ./Scripts/inventory-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && INV_BAG=12 INV_TAP=0 INV_FPS=30 ./Scripts/inventory-shot.sh
```
Expected: `ALL GREEN` at **`B + 11`**. Read the three PNGs:
1. the default shot — two pages side by side, the pockets 2×2 and the backpack 4×2, every item at its own rectangle and a 2×1 drawn as one box;
2. `INV_BAG=0` — the pockets hold what fits and the backpack page is a dimmed outline reading `НЕМАЄ РЮКЗАКА`;
3. `INV_BAG=12 INV_TAP=0` — the refusal frame: the amber ghost overhanging an occupied cell, `НЕ ВЛІЗЕ w×h` at the right of the header row, the amber rim and the amber `РЮКЗАК` header all in the same frame. `INV_FPS=30` matters — a 240 ms pulse cannot be sampled at 10 fps, and the shutter is chained off the tap rather than off engine start for exactly this reason.

`SarkoDebugLoot` fills the bag with twelve *stacks*, which is now more than twelve *cells* — it will refuse some of them. That is fine and is the point of shot 3; adjust its `Mixed[]` list only if the refusal makes shot 1 unreadable.

- [ ] **Step 8: Commit**

```bash
git add SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.h SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp && git commit -m "feat(ui): pockets beside a bag, and a refusal that shows the shape that failed"
```

---

### Task 6: The move stick sleeps while the panel is open

Task 5 moved the panel's rectangle to the left half. This is the other half of spec §4.5's resolution, and the half that has to be right: the left thumb's stick is suppressed while a container panel is open, in **one place**, so that the stated fallback — *shrink the panel, do **not** restore movement under it* — is a one-line change and not an archaeology exercise.

**The rationale, from the spec, so the next reader does not "fix" it:** looting already requires standing still, so movement is the input you can afford to lose for those seconds. Shooting is not, and a player interrupted mid-loot must be able to fight back with the thumb that was already there. **The aim stick, fire and the reload button all keep working untouched.**

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h`, `.cpp` (entry slide direction, `InteractButtonRectFor` collapses)
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp` (`DrawInteract` stops asking for the shifted rect)
- Modify: `SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp` (+2, −1)

**Interfaces:**
- Consumes: `SarkoUI::InventoryPanelRect`, `ASarkoCharacter::GetOpenContainerIndex`.
- Produces:
  - `SarkoInput::IsMoveStickSuppressed(bool bContainerPanelOpen) -> bool` — **the one place**
  - `SarkoInput::InteractButtonRectBesidePanel` is **deleted**
  - `SarkoUI::InteractButtonRectFor(const ASarkoCharacter*, FBox2D, float)` is **deleted**; callers use `SarkoInput::InteractButtonRect(Frame, PointScale)` (Task 7 gives it its final signature)

- [ ] **Step 1: Write the failing tests**

Replace the existing `Sarko.UI.CloseButtonMovesBesideThePanel` test in `Tests/InventoryUiTest.cpp` — it asserts behaviour this task deliberately removes — with:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelSitsInTheLeftHalfClearOfTheAimThumb,
	"Sarko.UI.PanelSitsInTheLeftHalfClearOfTheAimThumb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelSitsInTheLeftHalfClearOfTheAimThumb::RunTest(const FString& Parameters)
{
	// Spec §4.5: the panel used to sit bottom-RIGHT, on top of the aim stick,
	// passing touches through everywhere except its cells — so a thumb reaching
	// to shoot could land on a cell instead. That is a genuine hazard and this is
	// the assert that stops it coming back.
	const FVector2D Viewport(2556.f, 1179.f);
	const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
	const float Scale = SarkoUI::PointScaleForViewport(Viewport);
	const FBox2D Panel = SarkoUI::InventoryPanelRect(Safe, Scale);

	TestTrue(TEXT("the panel starts at the safe frame's left edge, not its right"),
		Panel.Min.X < Safe.GetCenter().X);
	TestTrue(TEXT("and ends before the midline, so the whole right half is free"),
		Panel.Max.X <= Viewport.X * 0.5f);
	TestTrue(TEXT("it is bottom-anchored, clear of the home indicator"),
		Panel.Max.Y < Safe.Max.Y && Panel.Max.Y > Safe.GetCenter().Y);

	// And it does not eat the pawn, who is at the centre of the screen.
	TestTrue(TEXT("the pawn at screen centre is not under the panel"),
		Panel.Max.X < Viewport.X * 0.5f);

	// One size, whatever the pawn is carrying: the rect no longer takes a cell
	// count at all, which is what makes that guarantee structural.
	const FBox2D Again = SarkoUI::InventoryPanelRect(Safe, Scale);
	TestEqual(TEXT("the rect is a pure function of the frame"), Panel.Min.X, Again.Min.X);
	TestEqual(TEXT("the rect is a pure function of the frame"), Panel.Min.Y, Again.Min.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMoveStickIsSuppressedOnlyWhileAPanelIsOpen,
	"Sarko.Input.MoveStickIsSuppressedOnlyWhileAPanelIsOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMoveStickIsSuppressedOnlyWhileAPanelIsOpen::RunTest(const FString& Parameters)
{
	// ONE function, so spec §5's fallback — "if it reads as a bug in play, shrink
	// the panel rather than restore movement under it" — is a one-line change
	// here and nowhere else. If this ever grows a second condition, it grows it
	// in this function.
	TestTrue(TEXT("a panel is open, so the left thumb does nothing"),
		SarkoInput::IsMoveStickSuppressed(true));
	TestFalse(TEXT("no panel, so movement is movement"),
		SarkoInput::IsMoveStickSuppressed(false));
	return true;
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko
```
Expected: **BUILD FAILED** — `SarkoInput::IsMoveStickSuppressed` does not exist, and the deleted test's `InteractButtonRectBesidePanel` is gone.

- [ ] **Step 3: The one place**

In `Core/SarkoPlayerController.h`, **delete** `InteractButtonRectBesidePanel`'s declaration and add:

```cpp
	/**
	 * Whether the left thumb's stick must not be driven this frame.
	 *
	 * Today this is exactly "a container panel is open" — spec §4.5. The panel
	 * moved to the left half so that a thumb reaching for the AIM stick can never
	 * land on a cell, and the price of that is the move stick, which is the input
	 * looting can afford to lose: you are standing still to loot anyway. Shooting
	 * is not, and a player interrupted mid-loot must be able to fight back with
	 * the thumb that was already there.
	 *
	 * **This is the ONE place.** Spec §5 names the fallback if the suppression
	 * reads as a bug in play — shrink the panel, do NOT restore movement under it
	 * — and that fallback is a one-line change here precisely because nothing
	 * else in the project decides this.
	 */
	bool IsMoveStickSuppressed(bool bContainerPanelOpen);
```

In the .cpp, beside `IsLeftHalf`:

```cpp
bool SarkoInput::IsMoveStickSuppressed(bool bContainerPanelOpen)
{
	return bContainerPanelOpen;
}
```

and delete `InteractButtonRectBesidePanel`'s definition.

- [ ] **Step 4: The controller honours it**

In `UpdateSticks`, before the touch loop:

```cpp
	// Read once per frame, from the pawn's own mirror of what is open. The panel
	// is a client-side view; this is a client-side input rule; neither needs the
	// server's opinion.
	const ASarkoCharacter* PanelPawn = Cast<ASarkoCharacter>(GetPawn());
	const bool bMoveSuppressed = SarkoInput::IsMoveStickSuppressed(
		PanelPawn && PanelPawn->GetOpenContainerIndex() != INDEX_NONE);

	// Dropped on the frame suppression begins, not merely ignored: a stick left
	// active would keep its last deflection and the pawn would walk on through
	// the whole loot, which is the exact opposite of the intent.
	if (bMoveSuppressed && MoveTouchIndex != INDEX_NONE)
	{
		MoveStick = FSarkoTouchStick();
		MoveTouchIndex = INDEX_NONE;
	}
```

In the touch loop's classification branch, guard the left half:

```cpp
		if (SarkoInput::IsLeftHalf(Position, Viewport))
		{
			// Suppressed: the touch is consumed by nothing. It is NOT reclassified
			// to the aim stick — a finger landing on the left while looting must
			// not start aiming, and with hold-to-fire that would also start
			// shooting.
			if (!bMoveSuppressed && MoveTouchIndex == INDEX_NONE)
			{
				MoveTouchIndex = Index;
				MoveStick.bActive = true;
				MoveStick.Origin = Position;
				MoveStick.Current = Position;
				bMoveTouchStillDown = true;
			}
		}
```

**Reversibility, stated:** nothing else is stored. When the panel closes, `bMoveSuppressed` goes false on the next tick and the next left-half touch-down re-anchors the stick normally. A finger that was already down when the panel closed drives nothing until it is lifted and pressed again — it was never claimed by any slot, so there is no state to repair. That is the correct behaviour and not an oversight: a stick that sprang to life under a finger already resting somewhere would start the pawn walking in whatever direction that finger happened to be.

- [ ] **Step 5: The panel slides in from the left**

In `UI/SarkoInventoryPanel.cpp`, `PlateTransform`:

```cpp
	// In from the LEFT edge now that the panel lives there — a plate that slides
	// in from the side it is not on reads as a plate that came from somewhere else.
	const float Offset = -SarkoUI::EntrySlidePt * (1.f - EntryCurve.GetLerp());
```

- [ ] **Step 6: The interact button stops moving**

Delete `SarkoUI::InteractButtonRectFor` from `UI/SarkoInventoryPanel.h`/`.cpp` entirely. Its whole job was choosing between two rects, and there is only one rect now.

In `Core/SarkoPlayerController.cpp`'s `UpdateSticks`:

```cpp
	const FBox2D InteractRect = SarkoInput::InteractButtonRect(
		SarkoInput::SafeFrame(Viewport), SarkoUI::PointScaleForViewport(Viewport));
```

In `UI/SarkoHUD.cpp`'s `DrawInteract`:

```cpp
	const FBox2D Rect = SarkoInput::InteractButtonRect(Safe, PointScale);
```

(The `PointScale` parameter arrives in Task 7; until then call the existing single-argument overload and leave a `// Task 7 gives this its point-sized signature.` note. Do not invent a third overload.)

The comment in `SarkoHUD.cpp` that explains "the shifted-beside-the-panel case is one shared function rather than a branch here and a matching branch there" must be **rewritten**, not deleted: it should now say that the rect never shifts at all, because the panel moved out of its half — the reason the shared function existed is gone, and a stale comment about it is how the branch comes back.

- [ ] **Step 7: Green, and play it**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
```
Expected: `ALL GREEN` at **`B + 12`** (two added, one removed). The PNG must show the panel in the **bottom left**, with the whole right half of the safe frame clear.

**The suppression itself cannot be screenshotted and cannot be tested headlessly** — a `-RenderOffscreen` run has no fingers. Record in the task report whether it was verified by a played session, and specifically these three: opening a crate stops the pawn; the aim stick and firing still work with the panel up; closing the crate and re-pressing the left half moves again.

- [ ] **Step 8: Commit**

```bash
git add SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryPanel.cpp SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp SarkoGame/Source/SarkoGame/Tests/InventoryUiTest.cpp && git commit -m "feat(input): looting takes the left thumb, and gives back the right one"
```

---

### Task 7: Hold to fire, a reload button that says what the magazine is, and an interact button that says what it will do

The last task, and the one that changes how the game feels. Three things, all in spec §4:

- **Fire is holding the aim stick past a dead zone.** No fire button — a dedicated one competes with the aim stick for the same thumb, and every mobile shooter that ships both ends up with players using one. The existing fire-on-release edge stays as the **flick**: a quick tap aims and fires once.
- **A reload button**, because reloading is a decision with a cost and the player must be able to make it *before* the magazine runs out. Auto-reload-on-empty stays as a safety net; the button always wins if pressed first.
- **The interact button says what it will do** — a generic action button in a game with two actions is a guess.

**Files:**
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h`, `SarkoGame/Config/DefaultGame.ini`
- Modify: `SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/UI/SarkoHUD.h`, `.cpp`
- Modify: `SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp`, `Tests/UiScaleTest.cpp` (the two `InteractButtonRect` callers), `Tests/CombatTest.cpp` (+3)

**Interfaces:**
- Consumes: `SarkoInput::SafeFrame`, `SarkoUI::PointScaleForViewport`, `USarkoWeaponComponent::{GetAmmoInMagazine, IsReloading, StartReload}`, `USarkoRaidSettings::{MagazineSize, MinFireIntervalSeconds}`.
- Produces:
  - `SarkoInput::ReloadButtonRect(FBox2D Frame, float PointScale) -> FBox2D`
  - `SarkoInput::InteractButtonRect(FBox2D Frame, float PointScale) -> FBox2D` (**the `FVector2D` and one-argument overloads are deleted**)
  - `SarkoInput::RightThumbAnchor(FBox2D Frame, float PointScale) -> FVector2D`
  - `SarkoInput::ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone) -> bool`
  - `SarkoUI::ESarkoReloadState { Ready, Low, Empty, Reloading }`, `ReloadStateFor(int32, int32, bool)`, `ReloadPulseAlpha(float TimeSeconds)`
  - `SarkoUI::EInteractAction { None, Search, Close, Extract }`, `InteractLabelFor(EInteractAction) -> FString`
  - `ASarkoCharacter::RequestReload()`
  - `USarkoRaidSettings::AimFireDeadZone`

- [ ] **Step 1: Write the failing tests**

Append to `SarkoGame/Source/SarkoGame/Tests/CombatTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoThumbControlsDoNotOverlap,
	"Sarko.Input.ThumbControlsDoNotOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoThumbControlsDoNotOverlap::RunTest(const FString& Parameters)
{
	// Spec §5: "Reload button placement fights the interact button for the same
	// thumb arc. They must never occupy the same rectangle, and the interact
	// button appearing must not shift the reload button — a control that moves is
	// a control you mis-press."
	//
	// Checked at three viewports, because a rect derived from a fraction can pass
	// on a phone and collapse in a small desktop window.
	const TArray<FVector2D> Viewports = {
		FVector2D(2556.f, 1179.f),   // iPhone 14/15 Pro landscape
		FVector2D(1280.f, 720.f),    // a small desktop window
		FVector2D(1560.f, 720.f),    // a cheap phone at 2x
	};

	for (const FVector2D& Viewport : Viewports)
	{
		const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
		const float Scale = SarkoUI::PointScaleForViewport(Viewport);
		const FBox2D Reload = SarkoInput::ReloadButtonRect(Safe, Scale);
		const FBox2D Interact = SarkoInput::InteractButtonRect(Safe, Scale);

		const FString Where = FString::Printf(TEXT("at %.0fx%.0f"), Viewport.X, Viewport.Y);

		// 44 pt in BOTH dimensions, in POINTS — which is why the rects take a
		// scale: a rule written in points cannot be checked against pixels.
		TestTrue(*FString::Printf(TEXT("%s: the reload button clears 44 pt"), *Where),
			Reload.GetSize().X / Scale >= 44.f && Reload.GetSize().Y / Scale >= 44.f);
		TestTrue(*FString::Printf(TEXT("%s: the interact button clears 44 pt"), *Where),
			Interact.GetSize().X / Scale >= 44.f && Interact.GetSize().Y / Scale >= 44.f);

		// Never the same rectangle. FBox2D::Intersect is inclusive of touching
		// edges, so this also rejects two buttons flush against each other.
		TestFalse(*FString::Printf(TEXT("%s: they do not overlap"), *Where),
			Reload.Intersect(Interact));
		TestTrue(*FString::Printf(TEXT("%s: interact is ABOVE reload, in the same column"), *Where),
			Interact.Max.Y < Reload.Min.Y);
		TestEqual(*FString::Printf(TEXT("%s: right-aligned to the same edge"), *Where),
			Interact.Max.X, Reload.Max.X);

		// Both inside the safe frame, or a notch eats a control.
		TestTrue(*FString::Printf(TEXT("%s: both are inside the safe frame"), *Where),
			Safe.IsInside(Reload) && Safe.IsInside(Interact));

		// The thumb arc. Reload is inside the aim thumb's ~45 pt travel, so it is
		// pressed without the thumb leaving its post; interact is deliberately
		// OUTSIDE it, so working the stick can never brush it, and still inside a
		// landscape thumb's full reach.
		const FVector2D Anchor = SarkoInput::RightThumbAnchor(Safe, Scale);
		const float ToReload = FMath::Sqrt(Reload.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		const float ToInteract = FMath::Sqrt(Interact.ComputeSquaredDistanceToPoint(Anchor)) / Scale;
		TestTrue(*FString::Printf(TEXT("%s: reload is %.0f pt from the thumb, inside its arc"), *Where, ToReload),
			ToReload <= 45.f);
		TestTrue(*FString::Printf(TEXT("%s: interact is %.0f pt away, outside the stick's arc"), *Where, ToInteract),
			ToInteract > 45.f);
		TestTrue(*FString::Printf(TEXT("%s: ...but still reachable"), *Where), ToInteract <= 150.f);

		// Neither rect depends on game state — there is nothing to pass. That is
		// what makes "the interact button appearing must not shift the reload
		// button" structural rather than a promise.
		TestEqual(*FString::Printf(TEXT("%s: the reload rect is a pure function of the frame"), *Where),
			SarkoInput::ReloadButtonRect(Safe, Scale).Min.Y, Reload.Min.Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoHoldTheAimStickToFire,
	"Sarko.Input.HoldTheAimStickToFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoHoldTheAimStickToFire::RunTest(const FString& Parameters)
{
	// Spec §4.2: hold past the dead zone and it keeps firing; a quick flick that
	// never crosses the dead zone still fires once on release.
	const float DeadZone = 0.35f;
	TestFalse(TEXT("a resting thumb does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D::ZeroVector, DeadZone));
	TestFalse(TEXT("a re-grip inside the dead zone aims but does not fire"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.2f, 0.f), DeadZone));
	TestTrue(TEXT("past the dead zone it fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.4f, 0.f), DeadZone));
	TestTrue(TEXT("full deflection fires"),
		SarkoInput::ShouldFireWhileHeld(FVector2D(0.f, -1.f), DeadZone));

	// The firing threshold must be HIGHER than the movement dead zone: an
	// accidental small deflection while re-gripping should aim, never shoot.
	TestTrue(TEXT("the fire dead zone is above the move dead zone"),
		GetDefault<USarkoRaidSettings>()->AimFireDeadZone
			> GetDefault<USarkoRaidSettings>()->MoveStickDeadZone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoReloadButtonSaysWhatTheMagazineIs,
	"Sarko.UI.ReloadButtonSaysWhatTheMagazineIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoReloadButtonSaysWhatTheMagazineIs::RunTest(const FString& Parameters)
{
	// Spec §4.3: "Shows state: the magazine count lives on it, it goes amber
	// below a third, and it pulses when empty. The player should never have to
	// look at two places to know they need to reload."
	using ESarkoReloadState = SarkoUI::ESarkoReloadState;
	TestEqual(TEXT("a full magazine is ready"),
		SarkoUI::ReloadStateFor(30, 30, false), ESarkoReloadState::Ready);
	TestEqual(TEXT("exactly a third is still ready — the boundary belongs to ready"),
		SarkoUI::ReloadStateFor(10, 30, false), ESarkoReloadState::Ready);
	TestEqual(TEXT("below a third is low"),
		SarkoUI::ReloadStateFor(9, 30, false), ESarkoReloadState::Low);
	TestEqual(TEXT("empty is empty"),
		SarkoUI::ReloadStateFor(0, 30, false), ESarkoReloadState::Empty);
	TestEqual(TEXT("reloading outranks everything, including empty"),
		SarkoUI::ReloadStateFor(0, 30, true), ESarkoReloadState::Reloading);

	// A zero magazine size is a broken config, not a divide by zero.
	TestEqual(TEXT("a zero-size magazine does not divide by zero"),
		SarkoUI::ReloadStateFor(0, 0, false), ESarkoReloadState::Empty);

	// The pulse is bounded and never fully transparent, or "empty" flickers into
	// looking like "absent".
	for (float Time = 0.f; Time < 4.f; Time += 0.137f)
	{
		const float Alpha = SarkoUI::ReloadPulseAlpha(Time);
		TestTrue(TEXT("the empty pulse stays visible"), Alpha >= 0.25f && Alpha <= 0.65f);
	}

	// The interact button says what it will DO. A generic label in a game with
	// two actions is a guess.
	TestEqual(TEXT("a crate in reach"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::Search), FString(TEXT("ОБШУКАТИ")));
	TestEqual(TEXT("a panel open"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::Close), FString(TEXT("ЗАКРИТИ")));
	TestTrue(TEXT("nothing in reach carries no label at all"),
		SarkoUI::InteractLabelFor(SarkoUI::EInteractAction::None).IsEmpty());
	return true;
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh Sarko
```
Expected: **BUILD FAILED** — `ReloadButtonRect`, `RightThumbAnchor`, `ShouldFireWhileHeld`, `ReloadStateFor`, `InteractLabelFor` and `AimFireDeadZone` do not exist.

- [ ] **Step 3: The two rects, in points**

In `Core/SarkoPlayerController.h`, replace both `InteractButtonRect` declarations with:

```cpp
	/**
	 * The control column, in points on the 844x390 landscape canvas.
	 *
	 * Sized in points and not as a fraction of the frame, which is what the two
	 * rects below used to be: a fraction is unfalsifiable against a rule written
	 * in points (">= 44 pt"), and the old max(96 px, shorter axis * 0.14) gave 52
	 * pt on a phone and 32 pt in a small window while looking like one number.
	 */
	constexpr float ThumbColumnRightInsetPt = 16.f;
	constexpr float ReloadButtonSizePt = 56.f;
	constexpr float InteractButtonWidthPt = 96.f;
	constexpr float InteractButtonHeightPt = 48.f;

	/** The reload button's bottom edge, above the safe frame's. 96 pt is the room
	 *  a resting aim thumb and its 45 pt of stick travel need underneath it. */
	constexpr float ReloadButtonBottomPt = 96.f;

	/** Between the two buttons. They must NEVER overlap (spec §5), and 12 pt is
	 *  also enough that a thumb aiming at one cannot clip the other. */
	constexpr float ThumbButtonGapPt = 12.f;

	/**
	 * The reload button: right thumb, above the aim stick, inside its arc.
	 *
	 * A dedicated button because reloading is a decision with a cost and the
	 * player must be able to make it BEFORE the magazine runs out —
	 * auto-reload-when-empty is the thing that gets you killed (spec §4.3).
	 *
	 * A pure function of the safe frame and the scale, and of NOTHING else. That
	 * is what makes "the interact button appearing must not shift the reload
	 * button" structural rather than a promise someone has to keep.
	 */
	FBox2D ReloadButtonRect(FBox2D Frame, float PointScale);

	/**
	 * The interact button: one 48 pt step above the reload button, right-aligned
	 * to the same edge, contextual in its LABEL but never in its position.
	 *
	 * It used to shift left when a container panel covered its usual place. The
	 * panel is in the other half now (spec §4.5), so the shifted rect and the
	 * function that chose between the two are both gone — and with them the class
	 * of bug where the button is drawn in one place and pressed in another, which
	 * the owner experiences as "the button doesn't work".
	 */
	FBox2D InteractButtonRect(FBox2D Frame, float PointScale);

	/**
	 * Where the aim thumb rests while working its stick. Documentary and
	 * test-facing: it is what Sarko.Input.ThumbControlsDoNotOverlap measures the
	 * two rects against, so "inside the thumb's arc" is a number rather than a
	 * claim.
	 */
	FVector2D RightThumbAnchor(FBox2D Frame, float PointScale);

	/** Whether the aim thumb is deflected far enough to be firing. Pure, because
	 *  it is the difference between a weapon that shoots when you meant to aim and
	 *  one that does not. */
	bool ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone);
```

In the .cpp, replacing the old `InteractButtonRect` definitions:

```cpp
FBox2D SarkoInput::ReloadButtonRect(FBox2D Frame, float PointScale)
{
	const float Size = ReloadButtonSizePt * PointScale;
	const float Right = Frame.Max.X - ThumbColumnRightInsetPt * PointScale;
	const float Bottom = Frame.Max.Y - ReloadButtonBottomPt * PointScale;
	return FBox2D(FVector2D(Right - Size, Bottom - Size), FVector2D(Right, Bottom));
}

FBox2D SarkoInput::InteractButtonRect(FBox2D Frame, float PointScale)
{
	const float Width = InteractButtonWidthPt * PointScale;
	const float Height = InteractButtonHeightPt * PointScale;
	const float Right = Frame.Max.X - ThumbColumnRightInsetPt * PointScale;
	// Measured off the reload button rather than off the frame, so the 12 pt gap
	// is the gap and cannot drift if either size changes.
	const float Bottom = ReloadButtonRect(Frame, PointScale).Min.Y - ThumbButtonGapPt * PointScale;
	return FBox2D(FVector2D(Right - Width, Bottom - Height), FVector2D(Right, Bottom));
}

FVector2D SarkoInput::RightThumbAnchor(FBox2D Frame, float PointScale)
{
	// Bottom-right, in from the corner by roughly where a thumb's tip lands when
	// the hand is holding the phone rather than reaching across it.
	return FVector2D(Frame.Max.X - 90.f * PointScale, Frame.Max.Y - 60.f * PointScale);
}

bool SarkoInput::ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone)
{
	return AimValue.Size() >= FMath::Max(KINDA_SMALL_NUMBER, FireDeadZone);
}
```

Update the two existing callers in `Tests/ExtractionTest.cpp:71` and `Tests/UiScaleTest.cpp:61` to pass `SarkoUI::PointScaleForViewport(Viewport)`. Their assertions about the button avoiding the thumbs still hold — the rect moved from the vertical centre to the lower right, which is *more* avoidance, not less; re-read each assertion and rewrite any that pinned the old vertical centring.

- [ ] **Step 4: The dead zone is a setting**

In `Core/SarkoRaidSettings.h`, beside `MoveStickDeadZone`:

```cpp
	/**
	 * How far the aim thumb must be deflected before the weapon fires (spec §4.2:
	 * "Hold the aim stick past the dead zone to fire").
	 *
	 * Higher than MoveStickDeadZone on purpose, and Sarko.Input.HoldTheAimStickToFire
	 * asserts it: a movement dead zone only has to reject a resting thumb's drift,
	 * but a FIRING one has to reject a deliberate small deflection — re-gripping,
	 * or turning to face a noise — because a shot the player did not mean to take
	 * gives away their position and empties a magazine they were saving.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float AimFireDeadZone = 0.35f;
```

and in `Config/DefaultGame.ini`, under the Combat block: `AimFireDeadZone=0.350000`.

- [ ] **Step 5: Hold to fire, flick to fire once**

In `Core/SarkoPlayerController.h`, add three members beside `bAimReleasedThisFrame`:

```cpp
	/** Which touch slot is holding the reload button, or INDEX_NONE. Claimed
	 *  before stick classification, exactly as InteractTouchIndex is — without it
	 *  a press on the button would also start an aim drag, and with hold-to-fire
	 *  that means the reload button shoots. */
	int32 ReloadTouchIndex = INDEX_NONE;

	/** True once this hold of the aim stick has fired at least once. What makes a
	 *  flick fire exactly once on release, and a hold not fire a bonus shot when
	 *  the thumb finally lifts. Reset when the stick is next pressed. */
	bool bAimFiredThisHold = false;

	/**
	 * World time of the last fire request this client SENT.
	 *
	 * RequestFire is a reliable server RPC. Holding the stick would otherwise send
	 * one every frame — sixty reliable RPCs a second for a weapon that fires at
	 * most every MinFireIntervalSeconds — and the server would drop fifty-three of
	 * them after they had already cost the bandwidth. The server's own rate limit
	 * stays exactly as it is: this throttle is politeness, not authority.
	 */
	float LastLocalFireSeconds = -1000.f;
```

In `UpdateSticks`'s touch loop, add the reload claim beside the interact claim (before the left/right classification, and reading `ReloadRect` computed alongside `InteractRect`):

```cpp
			if (ReloadTouchIndex == INDEX_NONE && ReloadRect.IsInside(Position))
			{
				ReloadTouchIndex = Index;
				bReloadTouchStillDown = true;
				// The press edge IS the reload. A hold does nothing more, because
				// there is nothing more for it to do.
				if (ASarkoCharacter* ReloadPawn = Cast<ASarkoCharacter>(GetPawn()))
				{
					ReloadPawn->RequestReload();
				}
				continue;
			}
```

with the matching `if (Index == ReloadTouchIndex) { … continue; }` early branch and the `if (!bReloadTouchStillDown) { ReloadTouchIndex = INDEX_NONE; }` release. Raise the loop bound from 3 to **4** — movement, aim, interact and reload are four separate holds now, and a player backing away from a bot while opening a crate and reloading is holding all four.

Also reset the hold flag where the aim stick is claimed:

```cpp
				if (AimTouchIndex == INDEX_NONE)
				{
					AimTouchIndex = Index;
					AimStick.bActive = true;
					AimStick.Origin = Position;
					AimStick.Current = Position;
					bAimTouchStillDown = true;
					// A new hold has fired nothing yet, so a flick that never leaves
					// the dead zone still gets its one shot on release.
					bAimFiredThisHold = false;
				}
```

In `PlayerTick`, replace the fire block:

```cpp
	const FVector2D AimValue = AimStick.Value();
	Pawn->SetAimIntent(SarkoAim::StickToWorldDirection(AimValue, CameraYaw), AimStick.bActive);

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// HOLD to fire (spec §4.2). Throttled to the weapon's own interval on this
	// side as well as the server's, because RequestFire is a reliable RPC and a
	// held stick would otherwise send one per frame.
	if (AimStick.bActive && SarkoInput::ShouldFireWhileHeld(AimValue, Settings.AimFireDeadZone))
	{
		if (Now - LastLocalFireSeconds >= Settings.MinFireIntervalSeconds)
		{
			LastLocalFireSeconds = Now;
			bAimFiredThisHold = true;
			Pawn->RequestFire();
		}
	}

	// FLICK: a quick tap that never crossed the dead zone still fires once, on
	// release — the behaviour this game already had, kept deliberately as the
	// aimed single shot. A hold that has already fired gets no bonus shot when
	// the thumb finally lifts.
	if (bAimReleasedThisFrame && !bAimFiredThisHold)
	{
		LastLocalFireSeconds = Now;
		Pawn->RequestFire();
	}
```

**Desktop is unchanged.** `ApplyDesktopTestInput`'s `WasInputKeyJustPressed(SpaceBar)` stays a one-frame edge: spec §4.2 says desktop keeps space, because that is how the owner plays while developing.

- [ ] **Step 6: The pawn can be told to reload**

In `Pawn/SarkoCharacter.h`, beside `RequestFire`:

```cpp
	/** Client intent: reload now, before the magazine is empty. Validated
	 *  server-side like every other request on this pawn. */
	void RequestReload();
```

and privately:

```cpp
	/**
	 * Server RPC. Reliable: a dropped reload is a player standing in the open
	 * pressing a button that does nothing.
	 *
	 * Carries no payload at all — there is exactly one weapon and nothing about
	 * this request the server does not already know, so there is nothing here for
	 * a hostile client to lie about.
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestReload();
```

In `SarkoCharacter.cpp`:

```cpp
void ASarkoCharacter::RequestReload()
{
	if (!WeaponComponent || (HealthComponent && HealthComponent->IsDead()) || IsRaidFinishedNow())
	{
		return;
	}
	// Nothing to do, and refused rather than started: a reload at a full magazine
	// would cost ReloadSeconds of a weapon that cannot shoot, which is a worse
	// outcome than the press doing nothing.
	if (WeaponComponent->IsReloading()
		|| WeaponComponent->GetAmmoInMagazine() >= GetDefault<USarkoRaidSettings>()->MagazineSize)
	{
		return;
	}

	if (HasAuthority())
	{
		WeaponComponent->StartReload();
		return;
	}
	ServerRequestReload();
}

void ASarkoCharacter::ServerRequestReload_Implementation()
{
	// Re-checked on the server for the same reason ServerRequestFire re-checks
	// death and the raid's outcome: the client's copy can be stale during the
	// round trip, and a modified client can skip the check entirely.
	if (!WeaponComponent || (HealthComponent && HealthComponent->IsDead()) || IsRaidFinishedNow())
	{
		return;
	}
	// StartReload already no-ops while a reload is in flight and already refuses
	// without authority, so this is safe to call unconditionally from here.
	WeaponComponent->StartReload();
}
```

**Auto-reload-on-empty is untouched.** `USarkoWeaponComponent::ServerFire` still calls `StartReload()` when the magazine runs dry from a shot and when a fire request arrives on an empty magazine. The button wins if pressed first, which is what "the button always wins" means: `StartReload` no-ops on the second caller.

- [ ] **Step 7: The HUD draws the column**

In `UI/SarkoHUD.h`, add `void DrawReload();` beside `DrawInteract()`, plus a cached label width per state. In `SarkoHUD.cpp`, add the pure helpers to the `SarkoUI` namespace (put them in `UI/SarkoInventoryStyle.h`/`.cpp` beside the other pure UI helpers, so they are testable without a HUD):

```cpp
	/** What the reload button is saying. Pure, so "amber below a third" is a test
	 *  rather than an eyeball. */
	enum class ESarkoReloadState : uint8 { Ready, Low, Empty, Reloading };

	ESarkoReloadState ReloadStateFor(int32 AmmoInMagazine, int32 MagazineSize, bool bReloading);

	/** The empty button's pulse, 0.25..0.65 at 2 Hz. Bounded away from zero: a
	 *  button that vanishes on the trough reads as absent, not as urgent. */
	float ReloadPulseAlpha(float TimeSeconds);

	/** What the interact button will do. Extract is declared and NOT wired: an
	 *  extraction is a dwell, not a press, so there is nothing to press yet — the
	 *  value and its label exist as the named seam for when there is, rather than
	 *  as a live button that does nothing. */
	enum class EInteractAction : uint8 { None, Search, Close, Extract };

	FString InteractLabelFor(EInteractAction Action);
```

```cpp
SarkoUI::ESarkoReloadState SarkoUI::ReloadStateFor(int32 AmmoInMagazine, int32 MagazineSize, bool bReloading)
{
	// Reloading outranks everything, including empty: while the reload is running
	// the count is not the fact the player needs.
	if (bReloading)
	{
		return ESarkoReloadState::Reloading;
	}
	if (AmmoInMagazine <= 0)
	{
		return ESarkoReloadState::Empty;
	}
	// A zero or negative magazine size is a broken config, not a divide by zero.
	// Everything is "low" then, which is the direction that nags rather than the
	// one that lies.
	if (MagazineSize <= 0)
	{
		return ESarkoReloadState::Low;
	}
	// Exactly a third is still ready — the boundary belongs to the calmer state,
	// so a 30-round magazine goes amber at 9 and not at 10.
	return (AmmoInMagazine * 3 <= MagazineSize - 1) ? ESarkoReloadState::Low : ESarkoReloadState::Ready;
}

float SarkoUI::ReloadPulseAlpha(float TimeSeconds)
{
	return 0.45f + 0.20f * FMath::Sin(TimeSeconds * 2.f * PI * 2.f);
}

FString SarkoUI::InteractLabelFor(EInteractAction Action)
{
	switch (Action)
	{
	case EInteractAction::Search:  return TEXT("ОБШУКАТИ");
	case EInteractAction::Close:   return TEXT("ЗАКРИТИ");
	case EInteractAction::Extract: return TEXT("ЕВАКУАЦІЯ");
	default:                       return FString();
	}
}
```

`DrawReload` draws a rounded plate at `SarkoInput::ReloadButtonRect(Safe, PointScale)` with the state's colour and the count centred at 20 pt:

| state | fill | label |
|---|---|---|
| Ready | `(1, 1, 1, 0.15)` | the count, white |
| Low | `(0.95, 0.55, 0.06, 0.35)` | the count, amber |
| Empty | amber at `ReloadPulseAlpha(GetWorld()->GetTimeSeconds())` | `0`, amber |
| Reloading | `(1, 1, 1, 0.10)` | `…`, grey |

`DrawInteract` keeps its dim/lit plate but replaces the single `E` glyph and the two-line X with `InteractLabelFor(...)` at **12 pt**, centred, where the action is `Close` when `GetOpenContainerIndex() != INDEX_NONE`, `Search` when `PC->GetInteractTarget()` is non-null, and `None` otherwise. `None` draws the dim plate with **no** label — the button is always visible so the player learns where it is, and empty is honest about there being nothing to do.

Both labels are measured once per scale and cached, like `CachedReloadingWidth` and `CachedInteractLabelWidth` already are: `DrawHUD` is a tick path and an uncached `MeasurePt` per frame is the allocation this HUD spent two commits removing. Key the cache on the label string, not on the action, because two actions can share a width and none can share a string.

`DrawHUD` gains `DrawReload();` immediately after `DrawInteract();`.

- [ ] **Step 8: Green, and read the frames**

```bash
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/run-tests.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/hud-shot.sh
cd /Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame && ./Scripts/inventory-shot.sh
```
Expected: `ALL GREEN` at **`B + 15`**. Read the two PNGs:
1. `hud-shot` — the reload button in the bottom right with the magazine count on it; the interact button above it reading `ОБШУКАТИ` when a crate is in reach or dim and blank when not; a clear 12 pt gap between them; both inside the safe frame;
2. `inventory-shot` — the panel bottom-left, the interact button reading `ЗАКРИТИ`, the reload button **in exactly the same place it was in the first shot**. Overlay the two frames if there is any doubt: the reload button moving between them is the bug this task exists to prevent.

**Three things no test and no still frame can settle**, to be recorded in the task report as verified-by-play or not verified:
- that holding the aim stick fires at the weapon's interval and does not machine-gun or stutter;
- that a quick flick still fires exactly one shot;
- that the reload button is reachable without the aim thumb leaving the stick — the geometry says 40 pt, but only a hand can confirm it.

- [ ] **Step 9: Commit**

```bash
git add SarkoGame/Config/DefaultGame.ini SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.h SarkoGame/Source/SarkoGame/Core/SarkoPlayerController.cpp SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.h SarkoGame/Source/SarkoGame/Pawn/SarkoCharacter.cpp SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.h SarkoGame/Source/SarkoGame/UI/SarkoInventoryStyle.cpp SarkoGame/Source/SarkoGame/UI/SarkoHUD.h SarkoGame/Source/SarkoGame/UI/SarkoHUD.cpp SarkoGame/Source/SarkoGame/Tests/CombatTest.cpp SarkoGame/Source/SarkoGame/Tests/ExtractionTest.cpp SarkoGame/Source/SarkoGame/Tests/UiScaleTest.cpp && git commit -m "feat(input): hold to fire, a reload button that shows the magazine, and a button that says what it does"
```

---

## Self-Review

**Slate and Core signatures, verified against the 5.8 source on this machine — not from memory.**

- `SConstraintCanvas::FSlot`'s builder arguments are exactly `.Offset(FMargin)`, `.Anchors(FAnchors)`, `.Alignment(FVector2D)`, `.AutoSize(bool)`, `.ZOrder(TOptional<float>)` — `Slate/Public/Widgets/Layout/SConstraintCanvas.h:47–53`, `SLATE_SLOT_BEGIN_ARGS(FSlot, TSlotBase<FSlot>)` with four `SLATE_ATTRIBUTE`s and one `SLATE_ARGUMENT`. With `Anchors(0,0,0,0)` and `AutoSize(false)`, `Offset` is read as `(Left, Top, Width, Height)`, which is what Task 4's `Put` lambda relies on. Its `SLATE_BEGIN_ARGS` already defaults `_Visibility = EVisibility::SelfHitTestInvisible` (`:118`), so the explicit `.Visibility(...)` in `BuildGridPage` is belt and braces rather than required.
- `SBox::SetContent(const TSharedRef<SWidget>&)` — `Slate/Public/Widgets/Layout/SBox.h:114`. Used by both the stash and the panel's two pages.
- `SWidget::SetRenderOpacity(float)` — `SlateCore/Public/Widgets/SWidget.h:1220`. Used to dim the unworn backpack page.
- `TBitArray<>::TBitArray(bool bValue, int32 InNumBits)` is `explicit` — `Core/Public/Containers/BitArray.h:403` — so `Pages.Emplace(false, Page.Cells())` is the correct construction, and a default-constructed-then-`Init` version is explicitly discouraged by the engine's own comment at `:834`.
- `FBox2D::ComputeSquaredDistanceToPoint(const FVector2D&)` — `Core/Public/Math/Box2D.h:207`. `FBox2D::Intersect(const FBox2D&)` — `:345`; its body rejects only strict separation (`Min.X > Other.Max.X`), so **touching edges count as intersecting**, which is why Task 7's non-overlap assert also rejects two buttons flush against each other. `FBox2D::IsInside(const FBox2D&)` — `:376`, defined as `IsInside(Other.Min) && IsInside(Other.Max)`.
- `FName::LexicalLess(const FName&)` — `Core/Public/UObject/NameTypes.h:826`. The tiebreaker that makes `SortForStash` a total order and therefore idempotent.
- `Algo::AnyOf` and `Algo::Reverse` exist as `Core/Public/Algo/AnyOf.h` and `Algo/Reverse.h`.
- Carried over from the previous plan and unchanged here: `FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth)`, `FCurveSequence(Start, Duration, ECurveEaseFunction)`, `SButton`'s raw `const FButtonStyle*` (which is why `FSarkoInventoryStyles::Get()` is process-wide and why `BuildStackCell` takes the styles by const ref rather than building any), and `SGameLayerManager`'s own `SDPIScaler` (which is why the panel divides through `SarkoUI::OverlayPointScale` and the shelter does not).

**The craft endpoint's real request and response, read from `sarko-api/`, not assumed.**

`POST /v1/garage/craft`, registered at `internal/api/router.go:40` behind `auth.Middleware`. **No request body** — `handleGarageCraft` (`internal/api/garage_handler.go:18`) reads only `auth.PlayerID(r.Context())` and calls `deps.Store.CraftNextVehicle(ctx, playerID)`. Success is `200` with `craftResponse{VehicleTier domain.Tier json:"vehicle_tier"; UnlockedMaps []string json:"unlocked_maps"}`. Failures: `409 insufficient_items`, `409 max_tier`, `404 not_found`, `401 unauthorized`, `500 internal` — all in the `{"error":{"code","message"}}` envelope `FSarkoBackendClient::Send` already unwraps. The server picks the tier itself (`store/garage.go:36` — `domain.NextTier(current)` under a `SELECT … FOR UPDATE` on `garage_progress`, then `debitItemsTx`, then the tier update, all in one transaction), which is why `CraftVehicle` sends nothing and why a client that named a tier would be naming one it does not get to choose. The bicycle recipe the shelter mirrors is `domain/garage.go:33–36` — `bike_frame ×1, wheel_small ×2, chain ×1` — matching `SarkoShelter::BicycleRecipe()` verbatim. It is already covered by `internal/api/endpoints_test.go:218` and `internal/store/garage_test.go`, so **`sarko-api` needs no code and no new test in this plan.**

**Every rect ≥ 44 pt and non-overlapping.** Reload **56 × 56**, interact **96 × 48**, gap **12 pt**, both right-aligned to `Frame.Max.X − 16`. On a 14 Pro landscape that is reload `x [713, 769] y [217, 273]` and interact `x [673, 769] y [157, 205]` — no intersection, 12 pt apart, both inside the safe frame, both past 44 pt in both dimensions. Distances from the thumb anchor `(695, 309)`: reload **40.2 pt** (inside the 40–45 pt travel arc), interact **104 pt** (outside it, inside a reach). `Sarko.Input.ThumbControlsDoNotOverlap` asserts all of that at three viewports rather than trusting this paragraph. Sized in **points** and not as a fraction of the frame, which the old `max(96 px, shorter axis × 0.14)` was: that expression gave 52 pt on a phone and 32 pt in a 1280×720 window while looking like one number, and a rule written in points cannot be checked against pixels. In the panel, the container cell (44 pt) and the take-all row (44 pt) are the only tap targets; the carry cells are `SelfHitTestInvisible` so the thumb aims through them and no minimum applies — stated in `Sarko.UI.PanelGeometryIsFixed` so it is a decision rather than an omission. The shelter's craft button is 48 pt tall.

**The size table against `items.json`.** All sixteen ids in the shipped catalog have a row in spec §1.1 and vice versa: `pistol` 2×1, `ammo_9mm` 1×1, `medkit`/`bandage`/`painkillers` 1×1, `scrap_metal`/`copper_wire`/`duct_tape` 1×1, `canned_food`/`vodka`/`cigarettes` 1×1, `toolbox` 2×1, `bike_frame` 3×2, `wheel_small` 2×2, `chain` 1×1, `backpack` 2×2. The spec's table names `backpack` only in §1.1's "bulky when carried rather than worn" row and `rifle` as a future 3×1; `rifle` is **not** added to `items.json` by this plan, because an item with no loot table entry and no model is an item that cannot be found. **The table lives in `Data/Items/items.json` and is guarded in three ways**: the parser makes `size` required and rejects anything below 1×1; `Sarko.Loot.ItemSizesMatchTheDesignTable` carries spec §1.1's literals and checks both directions, so an item added without a row fails as loudly as an item resized; and the same test asserts the two design promises — the pistol fits 2×2 pockets, and at least one item is wider than the pockets, or the backpack means nothing. `sarko-api` is unaffected: `internal/domain/loot_test.go:153` unmarshals only `id` and `stackSize`, and Go ignores unknown fields.

**The tutorial haul still fits — but only after two data edits, and the plan says so rather than assuming it.** The container-inventory plan's Task 6 was **never executed**: on `main` today, `bridge.json` container 0 holds only `scrap_metal`, `loot-tables.json` has no `backpack` entry, and the two tutorial tests it specified do not exist. Recomputed with sizes, the 19 authored lists stack to **11 stacks / 13 cells** (9 one-cell stacks, plus `toolbox` and `pistol` at two cells each), against **4** cells without a bag and **12** with one — so the layout is impossible without a backpack and one cell over even with one. Task 3 grants the bag in crate 0 (first in the list, so `ЗАБРАТИ ВСЕ` equips before it takes) and halves the authored 9 mm from 116 to exactly 60, making it one rectangle instead of two: **12 cells in 12, exact, with nothing to spare.** Because an exact packing is a claim about first fit and not about area, `Sarko.Loot.TutorialHaulStillFitsTheGrid` runs the real placer over the real data in four acquisition orders — authored, reversed, widest-first and widest-last — and its failure message tells the next author that something must come out before anything goes in.

**No binary assets.** Every brush added by this plan is an `FSlateRoundedBoxBrush` or `FSlateBrush` constructed in C++ (the refusal ghost's twelve-rung ladder, the shared cell, the empty cell); every font is `FCoreStyle::GetDefaultFontStyle`, compiled into SlateCore; the reload and interact buttons are `DrawRect` plus `DrawTextPt` on the HUD's canvas, with no glyph that could come back as a missing-character box on a device. Files created: two `.h`, two `.cpp`. Files edited: `.h`, `.cpp`, `.json`, `.ini`. Nothing else. `config = Game` settings (`PocketGrid`, `BackpackGrid`, `AimFireDeadZone`) go to `DefaultGame.ini`; nothing in this plan touches `DefaultEngine.ini`.

**Spec coverage.** §1 no rotation, first fit — `SarkoGrid::Place`, Task 1. §1.1 the size table and one rectangle per stack — Task 1 Steps 3–5 and the `AddToGrid` top-up rule. §1.2 pockets 2×2, backpack 4×2, two grids not one — `CarryPages`, Task 1; drawn as two pages in Task 5, including the dimmed unworn one. §1.3 death — unchanged: `ClearOnDeath` still empties `Slots` and `EquippedBackpack`, and no task touches it. §2 the stash as the same grid, large, scrollable, sorted, not a puzzle — Task 4, with `StashRowsFor` growing rather than refusing. §3 the garage's craft button, have/need, the named missing part, the map it opened — Task 2. §4.1 sticks stay floating — untouched. §4.2 hold to fire, flick kept, desktop keeps space — Task 7. §4.3 reload button, above the stick, shows state — Task 7. §4.4 contextual interact with a label — Task 7. §4.5 the panel moves left, the move stick is suppressed, aim/fire/reload keep working — Tasks 5 and 6. §5's five risks: refusal legibility (Task 5's ghost and `НЕ ВЛІЗЕ w×h`), sizes-are-balance (Task 1's design-table test), the stash must not become a puzzle (`StashRowsFor`), reload vs interact (Task 7's fixed rects and non-overlap test), move-stick suppression with its fallback (Task 6's single function). §6 out of scope — no drag, no rotation, no weight, no slot restrictions, no equipping anything but a backpack, no selling, no shop.

**Type consistency, checked across tasks.** `FSarkoGridPage{Columns, Rows}` and `FSarkoGridSlot{Page, X, Y, W, H}` are declared in Task 1 and used with those exact member names in Tasks 3, 4, 5 and 6. `SarkoGrid::Place(Stacks, Catalog, Pages)` keeps that argument order in all five call sites. `CarryPages(bool, FIntPoint, FIntPoint)` is the same in Task 1's test, `GetCarryPages`, Task 3's test and Task 5's `Refresh`. `AddToGrid` returns *the remainder* everywhere, matching the `AddToBackpack` it replaces, so `TransferOne`'s `Leftover` arithmetic is unchanged. `SarkoUI::CellExtentPt`/`CellOriginPt` are defined in Task 4 and used in Tasks 4 and 5. `SarkoInput::InteractButtonRect(FBox2D, float)` is given its final two-argument form in Task 7 and is called with the single-argument form in Task 6 with an explicit note saying so — that is the one deliberate mid-plan signature gap, and it is called out at the call site rather than left to be discovered. `ESarkoTakeRefusal`'s five values are untouched: **no RPC signature changes anywhere in this plan**, because placement is derived from an order that is already replicated.

**Four things I could not settle without hardware or a played session,** flagged rather than asserted:
1. **Whether the reload button is actually reachable** without the aim thumb leaving its stick. The geometry says 40.2 pt from the anchor; only a hand on a phone can confirm the anchor itself is where a thumb rests.
2. **Whether hold-to-fire feels like a weapon rather than a stutter.** The client throttle and the server's `MinFireIntervalSeconds` are both 0.15 s, so the cadence is right on paper; the felt result of two independent rate limits agreeing is not something `-nullrhi` can judge.
3. **Whether suppressing the move stick reads as a bug in play.** Spec §5 anticipates this and names the fallback — shrink the panel, do not restore movement — which is why Task 6 puts the decision in one function. Nothing headless can answer it.
4. **The exact-fit tutorial.** Twelve cells in twelve is proven for four acquisition orders, not for all of them. If a fifth order is ever found that strands an item, the fix is to remove one authored 1×1 (`duct_tape` at `bridge_loot_l03` is the cheapest, and merging it into `scrap_metal` keeps that crate non-empty), not to grow the grid — the grid's size is the design.
