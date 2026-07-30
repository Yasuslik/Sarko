# Sarko — Production Raid Loop: loot, extraction, map technology, Bridge_West

Date: 2026-07-30. Status: approved by the user ("делаем продакшен уровень, всё что описано").

## 1. Goal

Turn the current tech demo into a complete, playable extraction raid at production quality:
spawn → loot containers → fight bots → extract → keep the haul (or die and lose it), with the
result persisted by the backend. Built on the Bridge sector, executed as **Bridge_West**: the
western third of the full 400×400 m map is the active play space; the rest is physically closed
off and opened in later stages. 95% of the work stays in the shipping game — there is no
throwaway tutorial level.

The full-map design of record is `docs/design/bridge-full-map-tz.md` (the user's ТЗ, condensed
with all ledgers). This spec covers what we build NOW.

## 2. Resolved contradictions (user-visible decisions)

1. **No tutorial scripts.** The ТЗ's own §1 forbids them; the Bridge_West sketch's "spawn two
   bots after the first container opens" is dropped. Bots are placed, not triggered.
2. **The player brings their weapon.** The backend debits the loadout at `/raid/start`; a new
   player's stash is seeded with a free starter kit (pistol, ammo, medkit). "Find your first
   weapon at the pipes" is dropped — a weapon found on the map for free every raid breaks the
   loot economy that funds the Garage.
3. **Extractions are north-edge only, no special conditions.** The infographic's "south exit
   (checkpoint), opens at 5 min" contradicts the ТЗ text and the committed invariant tests.
   Text wins.

## 3. Stage order and why

- **Stage A — the loop (this spec's core):** interactive containers, in-raid backpack,
  extraction zones, death/timer outcomes, and the UE↔backend HTTP client. Without A the map
  cannot be completed at all, so nothing downstream can be evaluated.
- **Stage B — map technology:** walkable buildings with rooms, per-kind colours/materials,
  ravine water, new prop kinds, stable IDs. Without B "production level houses" cannot be
  authored (a house today is a solid 10×8×6 m cube).
- **Stage C — Bridge_West content:** the actual sector authored to the ТЗ, using A + B.

Each stage is its own plan and its own branch. C is data-heavy and cheap once A and B exist.

## 4. Stage A — loot and extraction

### 4.1 Items (client catalog)

- `SarkoGame/Data/Items/items.json`: `{ id, name (UA), stackSize }`. IDs MUST match the
  backend's item IDs (the backend is the source of truth; the client catalog is presentation).
- Loaded like the map definition: pure `ParseItemCatalog` + `LoadFromDisk`, loud failure,
  automation-tested. Unknown item id in any loot table = load error, not a silent skip.

### 4.2 Loot tables

- `SarkoGame/Data/Loot/loot-tables.json`: per tier (`junk|common|med|good|military`):
  `{ rolls: {min,max}, emptyChance, entries: [{ item, weight, qty: {min,max} }] }`.
- Rules from the ТЗ §30: junk may be empty ≤15%, common ≤8%, good/military never empty; med
  never yields weapons/vehicle parts; the tutorial map must NOT complete a vehicle tier in 1–2
  raids (bicycle parts appear only as rare singles).
- **Server rolls at loot time** with `FRandomStream(Seed ^ ContainerIndex)` — deterministic per
  raid, nothing pre-rolled, nothing client-trusted.

### 4.3 Containers

- `ASarkoLootContainer` spawned by the existing map path (spots already carry tier + pos).
  Replicated flag `bLooted` (`ReplicatedUsing`), visual state change on loot (lid colour).
- Interaction: proximity ≤ 250 uu shows a HUD prompt (drawn in `DrawHUD`, top of the safe area,
  never under thumbs); press-and-hold 1.5 s channel with a progress bar. Desktop: `E`.
- Server RPC validates: alive, unlooted, distance, channel time. Roll → items into backpack;
  overflow stays in the container (partial loot allowed).

### 4.4 Backpack

- Replicated (owner-only) `TArray<FSarkoItemStack>` on the character; 12 slots, stacking by
  `stackSize`. HUD shows `used/12` next to ammo. Died → contents vanish (server never submits
  them). Extracted → contents are the raid result.

### 4.5 Extraction

- `ASarkoExtractionZone` from map data (name, pos, radius). Server-side overlap + **5 s dwell**;
  HUD shows the zone name and a countdown while standing inside; leaving resets it.
- Success: input frozen, `EXTRACTED` summary (items list) drawn by HUD, result submitted.
- Raid timer hitting 0:00 = MIA = death (loot lost, submitted as death). This is the ceiling,
  not the goal — extract-when-you-choose stays.

### 4.6 Backend integration (UE ↔ sarko-api)

- New module-level client `FSarkoBackendClient` (FHttpModule): anonymous auth on first run
  (device token persisted under `Saved/`), then JWT; `POST /v1/raid/start` (loadout debit) on
  raid begin, confirm per the Slice-1 contract, `POST /v1/raid/result` on extract/death.
  Exact request/response shapes: **spec §7 of `2026-07-29-sarko-raid-slice-design.md` is
  normative** — do not invent fields.
- Base URL + enable flag in `USarkoRaidSettings`. **Offline degradation:** any HTTP failure logs
  loudly, the raid still plays, nothing persists — the game must never hard-lock on network.
- New-player seeding: backend change — starter kit (pistol, 60 ammo, 1 medkit) granted at
  anonymous registration. Idempotent, one-time.

### 4.7 Trust boundaries

Everything that grants value is server-side: rolls, loot transfer, extraction dwell, death.
The HUD only renders replicated state. The backend keeps its own authority (session token,
expiry + grace, idempotent result) — the UE server is a client to the backend and is NOT
trusted with arbitrary item grants: `/raid/result` items must remain plausibility-checked
against loot tables server-side (backend already has the tables? if not: cap by count/tier —
flag in plan).

## 5. Stage B — map technology

### 5.1 Buildings

- New JSON object `buildings[]`: `{ id, pos, size (footprint), wallHeight=350, wallThickness=30,
  doors: [{side: N|E|S|W, offset, width≥250}], interiorWalls: [{from, to, door?}] }` in local
  coords. A pure expander turns one building into wall blocks (deterministic, unit-tested:
  door gaps really are gaps, walls really close). **No roofs** — top-down camera; no stairs, no
  second floor (ТЗ §13).
- Closed buildings are the existing solid `house` kind (now renamed per-use via id).

### 5.2 Readability (the "same grey" problem)

- `FSarkoPropKind` gains `FLinearColor Colour`; `SpawnMeshBox` applies a dynamic material
  instance (same `BasicShapeMaterial` trick as `SarkoBody`). Palette per ТЗ §14: ground
  green-brown, dirt roads lighter, asphalt dark grey, bridge contrasting, water dark blue-grey,
  industry rust, extraction green.
- Roads = flat 2 uu boxes, no collision. Water = flat dark box strip in the ravine bed (engine
  has no shippable translucent material without assets — opaque dark water, documented).
- **Ravine depth is visual, not physical** (deviation from ТЗ §5's 400–700 uu dig): cliffs +
  dark bed + water read identically from top-down, and a real pit buys fall/stuck/nav bugs on
  iOS for zero gameplay. Crossability is enforced by the rim walls exactly as today.

### 5.3 New kinds

`rock`, `bush` (no collision), `log`, `fence_section`, `road_sign`, `concrete_barrier`,
`trailer`, `pylon` (composite), `treeline` (tall dark-green impassable wall — forest as a
boundary that never hides the player; individual trees with canopies are wrong for top-down).
Kinds may be **composite** (array of boxes) so a pylon or fuel canopy is one table entry.

### 5.4 IDs and groups

Optional `id` on every entry, required on containers/spawns/extracts/buildings; uniqueness
enforced by test. Naming per ТЗ §18 (`bridge_village_d1`, `bridge_extract_northwest`, …).

## 6. Stage C — Bridge_West content

- Active zone: the west, roughly X ∈ [-20000, -6000], both halves; ~250×200 m of play space.
  Player spawns near the pipes camp (north side), loops: pipes → gas station → rail depot →
  back through the pipes → dirt road north → **E1** (-15500, +19500). E1 is the only wired
  extraction this stage; E2/E3 exist in data but sit behind the closure.
- The east is closed by treeline, wrecks, concrete blocks and a collapsed road — reads as
  world, not as a wall; the bridge and water tower stay visible beyond as landmarks.
- Content per the advisor plan the user approved: ~10 buildings (pipes tech shed + pad; gas
  station shop/storeroom/canopy/wc; rail dispatcher/warehouse/shed/loading dock), 18–20
  containers (3 junk / 7 common / 6 good / 1 med / 2 military), **6 bots** (2 gas station,
  4 rail), none near spawn. Full-map ledgers (§28–31 of the ТЗ) are the coordinates of record
  for the зоны that survive into the full map.
- Invariant tests updated to Bridge_West numbers; the full-map tests (16 bots / 42 containers)
  move to the ТЗ reference doc as the acceptance bar for the LATER expansion, not this stage.

## 6.5 Stage A.5 — Shelter menu and the one-time tutorial raid (owner decisions, 2026-07-31)

Two decisions from the owner, slotted between Stage A and Stage B:

- **The tutorial raid happens once, with static loot.** Containers in the map JSON gain an
  optional `fixedItems` list; the backend gains a `tutorial_completed` flag (set on the first
  *successful* raid result — dying replays the tutorial with the same static layout). Profile
  exposes the flag; without it the raid uses `fixedItems`, with it the normal seeded rolls.
  The static layout is authored along the Bridge_West route to teach mechanics in order:
  junk at spawn (open containers) → medkit at the pipes (heal before the first bot) → ammo +
  first valuable at the gas station (buildings hold loot) → military at the rail depot
  (deeper = richer) → extract at E1. Server stays authoritative: fixed lists pass the same
  plausibility gate.
- **After extraction the player lands in the Shelter (main menu), not back into a raid.**
  Flow becomes: boot → shelter → "В РЕЙД" → raid → outcome → shelter with the refreshed
  profile. MVP shelter is Slate-in-C++ (no binary assets): stash list from `/v1/profile`,
  raid button, garage progress (bicycle 0/3 parts), shop stub (subscription later, no P2W).
  The EXTRACTED summary moves to the shelter ("вынесено: …"). Later: shelter visuals, real
  shop, map selection once there is more than one map.

Ordering: Stage A must be proven live first (Task 9), then A.5 gets its own plan, then B/C —
the tutorial's static layout must be authored against Bridge_West's final geometry.

## 7. Out of scope now

Quests, keys, bosses, day/night, weather, in-raid vehicles, destructibility, second floors,
bot loot drops, weight system, PvP/multiple players, reconnect-to-raid (needs the real
dedicated server), real art assets. **Visual ceiling note:** true production art means Fab/
marketplace packs installed by the user into the project — then the kind table just points at
those meshes by path. The colour pass is the best achievable without binary assets.

## 8. Testing

Every stage keeps `Scripts/run-tests.sh` green. New pure logic (catalog/table parsing, building
expansion, loot rolls, dwell timer) gets automation tests; every JSON invariant from ТЗ §19
that applies to Bridge_West gets a test; overview + player-eye screenshots are mandatory
verification for B and C. Backend changes keep `-race`, `-p 1` green. Live proof for A:
a scripted raid — spawn, loot one container, extract, and the backend row showing the items.

## 9. Risks

- Backend contract drift: mitigated by treating Slice-1 spec §7 as normative and smoke-testing
  against the deployed Railway instance before merge.
- HUD-drawn interaction on touch: thumbs already own the bottom corners; prompt/summary live
  top-centre. Verify on device sim.
- `MapExtent` (settings) vs `extentUU` (map) duplication — assert agreement in a test in
  Stage B (known concern from the previous plan).
