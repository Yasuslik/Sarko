# Sarko — the realism stage: few enemies, manual reload, hunger and thirst

Date: 2026-08-02. Status: owner decisions, synthesised with the code and design audits
(full audit reports live in `.superpowers/sdd/{code,design}-audit-2026-08-02.md`, gitignored).

## 0. Owner's frame

Three to five enemies for the whole tutorial raid — every encounter an event, "понял, как
реалистичный шутер". Reload is manual only. Water and food exist and matter. Tune toward
tension, not action. Enemies appear when the player approaches buildings (supersedes the old
"bots placed, never triggered" rule). Assets come later; design and systems now.

## 1. Fix first — holes the audits proved (before any new feature)

**Economy (backend):**
- A result on an unconfirmed (`pending`) session is credited in full — `start→result` in a loop
  banks a haul with zero gameplay and latches the tutorial flag. Require a confirmed session.
- The per-item cap ignores cell geometry: the bag holds one 3×2 `bike_frame`, the server accepts
  13. The gate learns sizes: a submitted haul must *placeable* into 2×2+4×2 (the pure placer
  already exists client-side; mirror the arithmetic, or cap per-item by cells).
- `ConfirmRaid` never checks `expires_at` — an expired session gets a fresh deadline. Check it.

**Haul survival (client):** a failed `SubmitResult` gives up after one try; a timeout or iOS
suspension destroys a legitimate haul although the server is idempotent. Retry with backoff
while the app lives; persist the unsent result under `Saved/` and resubmit on next launch.

**The two AI bugs that invalidate the map's design:**
- Patrol leash: `PatrolTarget` initialises to the world origin and the reroll picks uniformly in
  ±16000 uu — bots abandon their posts within ~90 s. Leash to `postPos + leashUU`.
- Line of sight: bots fire at 2000 uu and chase hearing at 1800 with **no LOS check**, i.e. from
  off-screen (the player sees ~1090 uu across). Gate `Chase`/fire on LOS; on hearing without
  sight, `Investigate` (walk toward the sound) instead. Firing range 1100 uu — fights start on
  screen or not at all.

## 2. Encounters — enemies appear at buildings, few and fair

New map data: top-level `encounters[]` + `encounterBudget { tutorial: 4, normal: 8,
firstFightMaxAlive: 1 }`. Each encounter: `id`, `order`, `budgetCost`, `maxAlive`, `oneShot`,
`trigger { kind: "radius", pos, radiusUU, armAfterUU }` (the shape the parser already knows from
extractions; `armAfterUU` is hysteresis), and authored `spawns[]` with `archetype`, `postPos`,
`leashUU`.

Spawn rules (server-only evaluation, 0.25 s timer; budget never replicated):
- ≥1800 uu from the player **and** no line of sight from the spawn point; prefer behind cover.
- If no point qualifies, defer up to 5 s. Never relocate a spawn toward the player, never spawn
  in view.
- The per-raid budget is the law: tutorial raids top out at 4 spawned enemies (owner: 3–5),
  `firstFightMaxAlive: 1` guarantees the first fight is one enemy.

Tutorial pacing: 1 at the gas station → 1 on the depot approach → 2 in the warehouse → quiet
walk home. The six statically-posted bots on today's map are replaced by these encounters; the
fixed `botSpawns` shape stays for non-tutorial content.

Bot archetypes as data (`scav_pistol`, `scav_smg`, `scout`) — a numbers table, no assets.

**Placement invariant replaced:** "bots ≥1800 uu apart" tested the wrong thing; the right one is
"no container sits inside two hearing bubbles" — the pistol crate currently aggros two bots at
once, at the tutorial's climax. Test containers, not bot pairs.

## 3. Manual reload — and the moment that teaches it

Both auto-reload sites are removed (the AI keeps its own). Empty magazine + fire = dry click,
nothing else, until the reload button is pressed. The button's empty-state pulse becomes the
primary signal.

The tutorial must teach this *before* it can kill: the raid starts with **3 of 8 rounds**, so
the reload button pulses at the spawn camp — 33 safe seconds before anything can hurt the
player. No new content needed; the lesson is the starting magazine.

## 4. Hunger and thirst

- Meters drain 2.5 (food) / 3.3 (water) per minute; a raid **starts at 55/45**, so thirst
  crosses the visible 30% threshold near the gas station — starting full would mean the meters
  never read in a 15-minute raid.
- Not lethal in-raid (owner). Penalty: out-of-combat health regeneration halves — which means
  **out-of-combat regen is introduced with this feature** (health currently only goes down;
  slow regen outside combat is itself a realism retune, medkits stay the in-combat answer).
- `water_bottle` joins the catalog (one JSON row). `canned_food`/`vodka` become usable.
  Consuming is a tap on the item in the inventory panel — the grid already handles taps.
- Taught at the existing "use something before the first bot" beat: the техдвор crate becomes
  `medkit + water_bottle + canned_food`.
- HUD: two 150×5 pt bars stacked under the health bar, top-right. Bottom is thumbs.

## 5. The realism retune (ini/data unless noted)

| setting | old → new | why |
|---|---|---|
| WeaponDamage | 12 → 28 | 4 shots to kill either way; 12 meant a bot needs ~15 s |
| MagazineSize | 30 → 8 | it *is* a ПМ; makes reload a real rhythm |
| ReloadSeconds | 2.2 → 1.8 | manual-only reload must not feel like punishment |
| EnemyFireIntervalSeconds | 1.8 → 1.3 | fewer enemies, each more dangerous |
| Enemy hearing | 1800 → 1400 | fights start near the screen edge, not beyond it |
| Enemy firing range | 2000 → 1100 (code) | never shot from off-screen; LOS-gated |
| WalkSpeed | 400 → 440 | the route is 237 s of walking; trims dead time |
| Tutorial 9mm total | 60 → 46 | scarcity; still enough for 4 + misses |
| Loot-table ammo weights | ~halved | ammo is a find, not a given |
| bLogAIDiagnostics | True → False | shipped on by mistake |
| RaidDurationSeconds=480 ini line | deleted | dead; the map's 900 silently wins |

## 6. Map comfort (from the walk-through)

- The backpack goes into **all three** spawn-camp crates (it is in one, 2802 uu from the far
  spawn; missing it silently breaks the raid at 4 cells).
- The 93-second pure-backtrack walk home gains a **second extraction that opens in the last
  5 minutes** (`opensAfterSeconds` on the extraction row) — the walk back becomes a decision.
- Keep exactly as is: the west dirt road as the wayfinder, the two-room warehouse with the
  pistol deepest, the pipe crossing, the 5 s dwell, the canopy fade, the forest/industry split.

## 7. Consciously deferred

The per-process identity/outcome debt (the multiplayer blocker) — documented, scheduled for the
dedicated-server stage, not now. The 15 s downed state — good idea, after survival ships.
Container `noiseRadiusUU` arming encounters — with the sound stage. Sound itself — next stage,
it is half the realism budget. Second raid map — after this stage proves the loop is fun.
