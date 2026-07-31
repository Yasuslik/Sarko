# Stage A.5 — Task 8: live end-to-end verification of the shelter + tutorial loop

Verified against the **deployed** backend (`https://sarko-api-production.up.railway.app`) on
2026-07-31, branch `feat/shelter-tutorial`, base commit `67ed02b`. Nothing in `sarko-api/` and
nothing on Railway was touched. One code change was made, and it is the one the brief authorised:
a debug-only raid-entry switch (§2).

Verdict: **PASS_WITH_GAPS**. Every hop of the loop ran live, twice, and the tutorial flag flipped
exactly where the spec says it should. Two things remain unverified rather than unproven-negative:
looting (needs held input, which a headless run has none of) and a human finger landing on the real
Slate button (§7).

---

## 1. Suite

`Scripts/run-tests.sh` — the only sanctioned way to run the tests — before and after the code change:

```
==> 80 test(s) performed, 0 failed
ALL GREEN
```

80/0 both times (`/tmp/a5-t8-tests.log` at `67ed02b`, `/tmp/a5-t8-tests-2.log` with the switch in).
The switch adds no test, so the baseline stays **80 UE tests**; the backend suite is untouched at
**94**. The plan's Task 8 text says "73 UE tests, 94 backend tests" — the 73 is stale; the real
number at the end of this stage is 80.

## 2. How the raid is entered from the shelter

The switch is commit `2edd5a2`, "feat(game): a debug-only switch presses the shelter's raid button" —
the only code this task added.

The brief's hard part: pressing a Slate button headlessly. Solved by **going through the button**,
not around it.

- `SSarkoShelterWidget::SimulateEnterRaidClickIfEnabled()` (new, `#if !UE_BUILD_SHIPPING`) fires the
  raid button's own `OnClicked` **only when `RaidButton->IsEnabled()`**, and returns false while it
  is disabled. The enabled check is mine, deliberately: the engine's `SButton::SimulateClick()`
  calls `ExecuteOnClick()` without consulting the enabled state, so without it a scripted press
  could start a raid the player could not have started. `SButton::SimulateClick` is itself declared
  `#if !UE_BUILD_SHIPPING` in the engine, so this cannot reach a shipping build even if the guard
  here were deleted — it would fail to link.
- `-SarkoAutoRaid=<seconds>` (`ASarkoShelterPlayerController::StartAutoRaid/TryAutoRaid`, also
  `#if !UE_BUILD_SHIPPING`) polls that method every 0.5 s from `<seconds>` in, up to 60 polls, and
  gives up with an Error if the button never enables. Polling rather than a fixed delay because the
  button is gated on the first `/v1/profile` and a fixed delay would race the network. It fires
  **once per process** (`GSarkoAutoRaidFired`), so the shelter the raid returns to is left alone —
  otherwise `BeginPlay` on the return entry would bounce straight back into a second raid and hide
  the one screen that carries the outcome.

What this proves: the button's enabled gate, its `OnClicked` delegate, `EnterRaid()`, the travel URL,
and the whole shelter→raid transition including the input-mode handover. What it does **not** prove:
Slate hit-testing — that a touch or click landing on those pixels reaches this button. That is still
unverified by anything but a human (§7).

Everything the switch touches is logged, so a run cannot silently skip the press:

```
LogTemp: Display: SarkoShelter: entering the raid
LogTemp: Display: SarkoShelter: -SarkoAutoRaid pressed 'В РЕЙД' on poll 1 (the button was enabled).
LogTemp: Display: SarkoShelter: -SarkoAutoRaid already fired once in this process; this shelter entry is left alone.
```

## 3. The deployed backend, on a fresh device

```bash
curl -s https://sarko-api-production.up.railway.app/healthz            # {"status":"ok"}
DEVICE="a5-t8-probe-1785488076"
TOKEN=$(curl -s -X POST .../v1/auth/anonymous -d "{\"device_id\":\"$DEVICE\"}" | …)
curl -s .../v1/profile -H "Authorization: Bearer $TOKEN"
```

```json
{ "player_id": "0f7f7720-fb00-42e5-af27-09a15b92355d", "schema_version": 1,
  "stash": [ {"item_id":"ammo_9mm","quantity":60}, {"item_id":"medkit","quantity":1},
             {"item_id":"pistol","quantity":1} ],
  "vehicle_tier": "none", "unlocked_maps": ["bridge"], "tutorial_completed": false }
```

`tutorial_completed` is present and `false`, and the starter kit is the three expected rows. The
migration is live; the lock-order fix is in the same image.

## 4. The whole loop, one continuous run

Device `a5-t8-e2e-1785488855` → player `600d7f9a-6d53-4677-a2df-d4cae9896a70`. Profile read by
`curl` immediately **before** the run: `tutorial_completed: false`, 3 stash rows.

```bash
cd SarkoGame
printf '%s' "a5-t8-e2e-1785488855" > Saved/SarkoDevice.txt
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" "/Engine/Maps/Entry" \
  -game -RenderOffscreen -unattended -nosplash -windowed -ForceRes -ResX=720 -ResY=1280 \
  -SarkoAutoRaid=8 -SarkoShelterShot=5 \
  -csvExecCmds="r100:BugItGo -14500 18600 200" \
  -ExecCmds="t.MaxFPS 10, CsvProfile Start" -log -abslog=/tmp/a5-e2e2.log
```

`BugItGo` is the engine's own teleport onto the centre of extraction zone 0, and it has to be fired
**inside the raid world** — `-ExecCmds` is queued once at engine init (`UnrealEngine.cpp:2552`) and
so only ever runs in the world that booted, which here is the shelter. It is delivered instead by
the CSV profiler's repeating frame commands (`-csvExecCmds="r100:…"`, executed on the local player
controller of the current world by `LaunchEngineLoop.cpp:1475`), with the capture started from
`-ExecCmds` rather than `-csvCaptureFrames` for the reason in §8.1. No project code was added for
this; teleporting is not the thing under test.

Log, in order, from `/tmp/a5-e2e2.log` (`LogTemp` prefixes trimmed):

```
09:17:00 SarkoShelter: viewport 0x0 px, UI scale 1.000 (canvas 390x844 pt)
09:17:01 SarkoBackend: authenticated as player 600d7f9a-6d53-4677-a2df-d4cae9896a70
09:17:01 SarkoBackend: profile for 600d7f9a… — 3 stash rows, tier 'none', tutorial PENDING
09:17:09 SarkoShelter: entering the raid
09:17:09 SarkoTravel: travelling to /Engine/Maps/Entry with options 'game=/Script/SarkoGame.SarkoRaidGameMode'
09:17:09 SarkoShelter: -SarkoAutoRaid pressed 'В РЕЙД' on poll 1 (the button was enabled).
09:17:09 LogLoad: Game class is 'SarkoRaidGameMode'
09:17:10 SarkoBackend: profile for 600d7f9a… — 3 stash rows, tier 'none', tutorial PENDING
09:17:10 Warning: SarkoRaidGameMode: TUTORIAL loot requested but none of 42 containers in 'bridge'
         carries fixedItems — every container will roll instead. Authoring the static layout is
         Stage C's job (spec §6.5).
09:17:10 SarkoBackend: raid session 0b65eaba-ca16-458f-9737-c6d68c05b1bd opened, seed -2105682506
09:17:10 SarkoBackend: raid confirmed, server deadline 2026-07-31T09:39:10.000Z
09:17:10 SarkoRaidGameMode: raid live — seed -2105682506, clock 900s, session '0b65eaba…'
09:17:17 SarkoRaidGameMode: extracted at zone 0 ('Северная тропа') after 5.00s of dwell, with 0 backpack slots used
09:17:17 SarkoRaidGameMode: raid finished as ESarkoRaidOutcome::Extracted, 893.3 s left on the clock
09:17:17 SarkoRaidGameMode: returning to the shelter in 5.0s
09:17:17 SarkoBackend: result 'extracted' recorded: {"session_id":"0b65eaba-ca16-458f-9737-c6d68c05b1bd",
         "outcome":"extracted","credited_items":[],"already_closed":false}
09:17:17 SarkoRaidGameMode: result 'extracted' submitted
09:17:22 SarkoTravel: travelling to /Engine/Maps/Entry with options '(none — the shelter)'
09:17:22 SarkoShelter: viewport 720x1280 px, UI scale 1.517 (canvas 390x844 pt)
09:17:22 SarkoShelter: -SarkoAutoRaid already fired once in this process; this shelter entry is left alone.
09:17:22 SarkoBackend: profile for 600d7f9a… — 3 stash rows, tier 'none', tutorial completed
```

Hop by hop, with the actual values:

| Hop | Evidence |
|---|---|
| boot lands in the shelter, real stash | `Game class is 'SarkoShelterGameMode'`, profile 3 rows, screenshot §6 |
| the button is gated on the profile, then pressed | pressed on poll 1, i.e. after `tutorial PENDING` landed |
| travel into the raid | `options 'game=/Script/SarkoGame.SarkoRaidGameMode'` |
| tutorial mode selected from the flag | `TUTORIAL loot requested…` (the expected Stage C marker) |
| session opened and confirmed on prod | session `0b65eaba…`, seed `-2105682506`, deadline `09:39:10Z` |
| extraction | zone 0 `Северная тропа`, dwell `5.00s`, backpack `0/12` |
| result accepted by production | `"already_closed":false`, `credited_items: []` |
| return travel carries no options | `options '(none — the shelter)'` |
| the shelter shows the outcome + refreshed stash | `ВИНЕСЕНО` / `НІЧОГО НЕ ВИНЕСЕНО` + 3 stash rows, §6 |
| `/v1/profile` flipped | `tutorial completed` in-game, and by `curl` below |

`curl /v1/profile` for the same device immediately **after** the run:

```json
{ "player_id": "600d7f9a-6d53-4677-a2df-d4cae9896a70", …, "tutorial_completed": true }
```

**Before `false`, after `true`, one continuous process in between.** The credited haul is empty and
the stash is unchanged (still 60 / 1 / 1) because the backpack was empty — see the looting gap
(§7.1). `player_id 600d7f9a…`, `seed -2105682506`, `credited_items []`, `tutorial false → true`.

An earlier run of the same loop (`/tmp/a5-loop.log`, 08:56, player `58d6e1db…`) shows the identical
sequence, including `PENDING → completed`; it is kept only as a second witness, because it ran with
`-csvCaptureFrames` and therefore against a different device file (§8.1).

## 5. Second raid, and the death path

**Second raid, same device, tutorial mode off.** Device `a5-t8-e2e-1785488855` again, booted
straight into the raid (`?game=…`) with `-DUMPMOVIE` for the in-raid frame:

```
09:19:24 SarkoBackend: profile for 600d7f9a… — 3 stash rows, tier 'none', tutorial completed
09:19:24 SarkoRaidGameMode: normal loot — containers roll against the raid seed
09:19:25 SarkoBackend: raid session cc295ecb… opened, seed 787831097
09:19:30 SarkoRaidGameMode: extracted at zone 0 … raid finished as ESarkoRaidOutcome::Extracted
09:19:30 SarkoBackend: result 'extracted' recorded: {…"already_closed":false}
```

`normal loot — containers roll against the raid seed`, and **no** `TUTORIAL loot` line anywhere in
the log. The Warning about `fixedItems` is gone on the second raid, exactly as the mode flip
requires. (Its presence on the first raid stays the Stage C marker.)

**Death leaves the flag true.** Device `a5-t8-loop-1785488153` → player
`d44340e8-2ddb-4f99-9f59-767c9fa6233a`, whose tutorial was already completed. A real in-game KIA:
`Summon /Script/SarkoGame.SarkoEnemyCharacter` on the repeating CSV frame command spawns a bot on
top of the pawn, which then shoots it — the six map bots never engage a pawn that stands still and
silent, which is why the plan's "let the bots find you" recipe times out.

```
09:24:41 SarkoBackend: profile for d44340e8… — 3 stash rows, tier 'none', tutorial completed
09:24:41 SarkoRaidGameMode: normal loot — containers roll against the raid seed
09:24:41 SarkoRaidGameMode: raid live — seed -137543139, session '40e37e46-aed0-414e-bbf6-c05b09845947'
09:24:56 SarkoRaidGameMode: raid finished as ESarkoRaidOutcome::Died, 884.7 s left on the clock
09:24:56 SarkoBackend: result 'died' recorded: {…"outcome":"died","credited_items":[],"already_closed":false}
09:25:01 SarkoTravel: travelling to /Engine/Maps/Entry with options '(none — the shelter)'
09:25:02 SarkoBackend: profile for d44340e8… — 3 stash rows, tier 'none', tutorial completed
```

`curl` after: `tutorial_completed: True`, stash unchanged. Dying does not clear the flag.

**Death does not latch the flag either** (spec §6.5, "dying replays the tutorial"), verified live on
a fresh device rather than by the plan's `curl`-only fallback. Device `a5-t8-death-1785490005` →
player `9c11816f-0510-4470-b1c8-1d375f75d5dd`:

```
BEFORE  tutorial_completed: False, 3 rows
09:26:56 profile for 9c11816f… — 3 stash rows, tier 'none', tutorial PENDING
09:26:56 Warning: TUTORIAL loot requested but none of 42 containers … carries fixedItems
09:27:12 raid finished as ESarkoRaidOutcome::Died, 884.7 s left on the clock
09:27:12 result 'died' recorded: {…"outcome":"died","credited_items":[],"already_closed":false}
AFTER   tutorial_completed: False, 3 rows
```

A tutorial raid that ends in death stays a tutorial raid.

## 6. Screenshots

All under `SarkoGame/Saved/Screenshots/a5-task-8/` (gitignored, not committed). Every PNG below was
opened and read, not just produced.

1. **`shelter-after-extraction-720x1280.png`** — the shelter the raid returns to, from
   `SHELTER_AFTER_RAID=1 Scripts/shelter-shot.sh` (device `a5-t8-loop-1785488153`, a real live
   extraction at 09:02). `УКРИТТЯ` / `ВИНЕСЕНО` / `НІЧОГО НЕ ВИНЕСЕНО` / `ГАРАЖ: ВЕЛОСИПЕД 0/3` /
   `СХОВОК` with `Патрони 9×18 x60`, `Аптечка x1`, `Пістолет ПМ x1` — Ukrainian names, not ids —
   `В РЕЙД` live and `МАГАЗИН НЕЗАБАРОМ` visibly greyed. No error line. The haul reads "nothing
   brought out" because nothing was looted (§7.1), which is the honest state, not a bug.
2. **`shelter-after-extraction-600d7f9a-720x1280.png`** — the same screen for the §4 loop run, the
   device whose flag flip is `curl`-verified. Identical content plus a green `CsvProfiler frame: 250`
   overlay, which is the harness, not the UI.
3. **`shelter-boot-before-raid-720x1280.png`** — the boot shelter from the same run: no outcome line
   at all (not an empty one), stash present, `В РЕЙД` already enabled, no HUD and no pawn.
4. **`in-raid-character-and-hud-720x1280.png`** — `MovieFrame00025` of the `-DUMPMOVIE` raid: the
   mannequin with its shadow standing in extraction zone 0's disc, HUD live around it — `30` ammo,
   `0/12` backpack, `14:56` clock, full health bar, the `Северная тропа — 0.8` dwell banner and the
   `E` interact button. Character and HUD in one frame, in the raid, against prod.

`Shot showui` (not `HighResShot`) for the Slate screens and `-DUMPMOVIE` for the in-raid frame, per
the documented reasons in `Scripts/shelter-shot.sh`. The 143-frame movie dump was deleted after the
one frame was kept.

## 7. Gaps — things I could not honestly verify

1. **No looting, so no credited haul.** Looting needs interact *held* for `LootChannelSeconds`, and
   a headless run has no touch or key input at all; there is no exec cheat for it and the brief
   authorised only the raid-entry switch, so none was added. Every extraction above therefore
   submitted `items: []` and `credited_items` came back empty, and the stash after the loop is
   byte-identical to the starter kit. The credit path itself (`items → stash`) is Stage A T9's
   evidence, not this task's. Carried forward from Stage A ("no held-input looting headlessly").
2. **A human pressing the real button is still unverified.** The switch fires the button's delegate
   through its enabled state, which covers everything except Slate hit-testing: that a press landing
   on those pixels is routed to this widget. OS-level input injection is not available on this
   machine either — no `cliclick`, no `Quartz` bindings for `python3`, and `System Events` times out
   (Accessibility not granted), so a synthetic mouse click or keystroke could not be delivered to a
   windowed run. The plan's Step 5 manual pass is still owed by the owner, and it is the only thing
   that can answer it.
3. **No pawn-movement proof in this run.** Same root cause as (1) and (2): no input path. The pawn
   reached the extraction zone by `BugItGo`, not by walking. Movement under real input was proven in
   `1d95f64` with a temporary patch that pressed W at `FSlateApplication::ProcessKeyDownEvent` (0 uu
   with the fix suppressed, 719 uu with it) — that evidence stands, but it was not re-run here.
4. **No `fixedItems` authored anywhere**, so the tutorial's static layout does not exist and every
   tutorial raid rolls. The per-raid Warning is the marker; its disappearance is Stage C's bar.
5. **No recipe endpoint** — `SarkoShelter::BicycleRecipe()` still mirrors `garage.go`'s unexported
   `recipes[TierBicycle]`. Propose `GET /v1/garage/recipe` for whichever stage needs a second tier.
6. **No craft button.** The shelter shows garage progress only; `POST /v1/garage/craft` is unwired.
   §6.5 does not ask for it.
7. **Slate is throwaway UI**, to be rebuilt in UMG when binary assets are allowed. The pure
   `SarkoShelter::Build*` functions are what survives.
8. **The device id is per-project** (`Saved/SarkoDevice.txt`): two `-game` instances from this folder
   share one player and one stash.
9. **No LOS check on looting** — carried forward from Stage A, deferred to Stage C polish.
10. **A killed run leaves its session open** until `RAID_TTL` (20 min), when the sweeper closes it as
    `died`. Player `600d7f9a…` has one such session (`c6ad7837…`, opened 09:20) from the run where I
    waited on the map bots. Known behaviour, not a defect; it means a device cannot start another
    raid until it clears.

## 8. Findings — reported, not fixed

### 8.1 `-csvCaptureFrames` silently makes the game read a different device id (harness hazard)

Found while chasing a player id that did not match the device file. With `-csvCaptureFrames` on the
command line, the game authenticates as a player that has nothing to do with
`SarkoGame/Saved/SarkoDevice.txt`:

```
file: [a5-t8-e2e-1785488855]  (correct player: 600d7f9a…)
-csvCaptureFrames=100000  -> authenticated as player 58d6e1db-8fe0-4e93-a5f5-1c6261573d35
-SarkoAutoRaid=99         -> authenticated as player 600d7f9a-6d53-4677-a2df-d4cae9896a70
no extra switches         -> authenticated as player 600d7f9a-6d53-4677-a2df-d4cae9896a70
```

Cause: `FCsvProfiler::PreInit` (`CsvProfiler.cpp`, `if (FParse::Value(Commandline,
TEXT("csvCaptureFrames="), …))`) calls `FCommandLine::Set` and `InitInternal()` before the project is
resolved, and the project user dir is then cached for the process — so `FPaths::ProjectSavedDir()`
points at `~/Library/Application Support/Epic/UnrealEngine/5.8/Saved/` for the whole run.
`SarkoBackend::EnsureDeviceId()` reads and writes there instead, and that file holds a GUID
(`7586a298-fd40-d57d-a63b-bf8a2fd2ced1` → player `58d6e1db…`) minted by some earlier profiled run.
`FPaths::ProjectDir()` is unaffected, which is why the map still loads from `Data/Maps/bridge.json`
and only `Saved/` moves.

Same switch, second symptom: with the boot capture on, the trace `screenshot` channel swallows
`Shot showui` — the log says `Tracing Screenshot "ScreenShot00000" taken with size: 720 x 1280` and
no PNG is ever written.

Not a product defect: nothing ships with `-csvCaptureFrames`, and no project code is at fault. It is
a real trap for anyone verifying this game headlessly, because a run that looks right can be running
as the wrong player against the wrong stash. The workaround used throughout §4–§5 is to start the
capture from `-ExecCmds="… , CsvProfile Start"` and keep `-csvCaptureFrames` off the command line;
device ids then behave (verified: two consecutive plain launches and a `curl` all agreed on
`5d40b7dc…` for one device id).

### 8.2 `LogSpawn: Warning: SpawnActor failed because no class was specified`, once per shelter entry

Logged on every shelter `BeginPlay` (twice in the §4 run — boot and return), immediately before
`SarkoShelter: viewport …`. `ASarkoShelterGameMode`'s constructor comment states the opposite:

> No pawn: there is nothing to walk around in. `bStartPlayersAsSpectators` keeps `RestartPlayer` from
> running at all, so a null `DefaultPawnClass` cannot produce the "failed to spawn pawn" warning on
> every boot.

The pawn-spawn path is indeed skipped, but *something* in the shelter's controller/spectator init
still calls `SpawnActor` with a null class. Cosmetic — nothing downstream misbehaves and the loop is
unaffected — but the comment asserts a guarantee the log contradicts, and one Warning per screen
entry is exactly the kind of noise that hides the next real one. Pre-existing (Task 4), not
introduced here. Reported per the brief; not patched.

## 9. Manual pass still owed by the owner

Unchanged from the plan, and none of it is a test: does the shelter feel like a place; is `В РЕЙД`
reachable one-handed on a real phone; is 5 s the right pause on the outcome; should there be a
"снова в рейд" button; should the tutorial be skippable. Add to that the one thing this task could
not reach — **press `В РЕЙД` with a finger and confirm the menu is gone once the raid loads**.
