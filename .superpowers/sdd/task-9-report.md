# Task 9 Report: Live end-to-end verification of the loot & extraction loop

Plan: `docs/superpowers/plans/2026-07-31-sarko-loot-extraction.md`, Task 9.
Branch: `feat/loot-extraction`. Verified at `fabab8f` (working tree clean; the loot & extraction
code of record is `f6bfd2d`, `fabab8f` is a docs-only commit on top).
Backend: `https://sarko-api-production.up.railway.app` — **already deployed by the controller**,
so plan Step 3 (deploy) was skipped and the deployed behaviour was verified instead.
Date: 2026-07-30/31 (UTC timestamps throughout).

> **Note on this file.** A report for a *different* plan's Task 9 (the sarko-api HTTP endpoints)
> already occupied this path. It was preserved verbatim as
> `.superpowers/sdd/task-9-report-backend-endpoints.md` before this file was overwritten.

**Status: PASS_WITH_GAPS.** Everything in the loop was exercised live against the deployed
backend and the deployed database. The one thing that could not be produced honestly is a haul
looted *by the game* — the loot channel needs a held key and a headless `-unattended` run has no
input path, and the mandate for this task forbade adding one. The itemised-credit half of the
contract is therefore evidenced by `curl` against the same deployed service, and the game's own
extraction is evidenced with an empty haul. Both halves are below; neither is dressed up as the
other.

---

## 1. Full suite

```
cd SarkoGame && ./Scripts/run-tests.sh
```

```
  Success  Sarko.Loot.BackpackFillsPartialStacksBeforeOpeningSlots
  Success  Sarko.Loot.BackpackStacksAndOverflows
  Success  Sarko.Loot.CompletedChannelCreditsThenMarksOnce
  Success  Sarko.Loot.InteractGateIsServerSide
  Success  Sarko.Loot.ItemCatalogParses
  Success  Sarko.Loot.ItemCatalogRejectsBadInput
  Success  Sarko.Loot.RealItemCatalogIsUsable
  Success  Sarko.Loot.RealLootTablesObeyTheDesignRules
  Success  Sarko.Loot.RollIsDeterministicPerContainer
  Success  Sarko.Loot.RollObeysTheTableBounds
  Success  Sarko.Loot.TablesParse
  Success  Sarko.Loot.TablesRejectBadInput
  Success  Sarko.Map.BridgeExtractionsAreOnOuterEdges
  Success  Sarko.Map.BridgeMapIsValid
  Success  Sarko.Map.BridgeRiskGradientExists
  Success  Sarko.Map.DefinitionConvertsToLayout
  Success  Sarko.Map.DefinitionOptionalSectionsMayBeAbsent
  Success  Sarko.Map.DefinitionParses
  Success  Sarko.Map.DefinitionRejectsBadInput
  Success  Sarko.Map.PropKindsAreComplete

==> 56 test(s) performed, 0 failed
ALL GREEN
```

**56 performed, 0 failed** — matches the baseline exactly.

---

## 2. Live API pass (curl, fresh device id)

`API=https://sarko-api-production.up.railway.app`
Device: `e2e-t9-1785454624` → player `30b1c5a9-eb18-44bc-9767-f7f18417f19b`

### 2.1 Health

```
$ curl -fsS $API/healthz
{"status":"ok"}
```

### 2.2 Register (starter kit is live)

```
$ curl -fsS -X POST $API/v1/auth/anonymous -H 'Content-Type: application/json' \
    -d '{"device_id":"e2e-t9-1785454624"}'
{"player_id":"30b1c5a9-eb18-44bc-9767-f7f18417f19b","token":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIzMGIxYzVhOS1lYjE4LTQ0YmMtOTc2Ny1mN2YxODQxN2YxOWIiLCJleHAiOjE3ODgwNDY2MjQsImlhdCI6MTc4NTQ1NDYyNH0.QTdYYB_1MJhiykXfKOS2xz6DtqcTDFrE6ainseDbsCM"}
```

### 2.3 Profile — starter kit and the `bridge` tier floor

```
$ curl -fsS $API/v1/profile -H "Authorization: Bearer $JWT"
{"player_id":"30b1c5a9-eb18-44bc-9767-f7f18417f19b","schema_version":1,
 "stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"medkit","quantity":1},{"item_id":"pistol","quantity":1}],
 "vehicle_tier":"none","unlocked_maps":["bridge"]}
```

Both plan expectations hold on the deployed service: the starter kit is granted at registration
(sorted by `item_id`), and `vehicle_tier: "none"` maps to `unlocked_maps: ["bridge"]` — so the
plan's flagged conflict "`map_id: bridge` → 403 `map_locked`" is **resolved live**, not just in
the repo.

### 2.4 Start — empty loadout, and the stash is untouched

```
$ curl -fsS -X POST $API/v1/raid/start -H "Authorization: Bearer $JWT" \
    -H 'Content-Type: application/json' -d '{"map_id":"bridge","loadout":[]}'
{"session_id":"98ac6e2d-0d38-43ee-ab29-071c0e3e162a",
 "session_token":"zjPZP1iKzKgeWgByhO6YP3TU84QfWM3gVrwnvhykFEQ",
 "seed":3772544291,
 "expires_at":"2026-07-30T23:38:14.663725Z"}
```

- Request sent at `2026-07-30T23:37:14Z`; `expires_at` is `+60 s`, i.e. `PENDING_TTL` — a session
  that is not confirmed inside a minute is swept.
- **`seed` is 3772544291, above `int32` max.** This is the plan's third flagged conflict, live and
  real. The client's `SarkoBackend::SeedToInt32` is what makes it safe, and §4.4 below shows that
  mapping working in the game (a negative `int32` seed in the log).
- Profile re-read immediately after `start`, with the raid open:

```
{"player_id":"30b1c5a9-...","stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"medkit","quantity":1},{"item_id":"pistol","quantity":1}],...}
```

  Byte-identical to §2.3. **No loadout debit** — the F1 fix (empty wire loadout, stash untouched at
  raid start) is confirmed against the deployed service. The plan's Step 7 expectation of a
  `pistol`/`ammo_9mm` debit at `raid/start` is obsolete and was judged against the current
  contract instead.

### 2.5 Confirm — the 22-minute deadline

```
$ curl -fsS -X POST $API/v1/raid/confirm -H "Authorization: Bearer $JWT" \
    -H 'Content-Type: application/json' \
    -d '{"session_id":"98ac6e2d-...","session_token":"zjPZP1iK..."}'
{"expires_at":"2026-07-30T23:59:15Z"}
```

Request at `2026-07-30T23:37:15Z` → deadline `23:59:15Z` = **now + 22 min** = `RAID_TTL` 20 m +
`GraceBuffer` 2 m (`internal/config/config.go`). Against a 900 s map that leaves 1320 s − 120 s
grace = 1200 s of headroom, so the client's clock is the map's 900 s and **no clamp fires**
(proven in §4.2).

### 2.6 Hostile probe on the *open* session — the plausibility gate

```
$ curl -s -w '\nHTTP %{http_code}\n' -X POST $API/v1/raid/result -H "Authorization: Bearer $JWT" \
    -H 'Content-Type: application/json' \
    -d '{"session_id":"98ac6e2d-...","session_token":"zjPZP1iK...","outcome":"extracted","items":[{"item_id":"bike_frame","quantity":13}]}'
{"error":{"code":"implausible_items","message":"item bike_frame: at most 12 units fit a backpack (12 slots × a stack of 1), got 13"}}
HTTP 400
```

`400 implausible_items`, with the per-item cap spelled out (12 slots × a `bike_frame` stack of 1).
The forged result is rejected on a **live, open, confirmed** raid — the case that matters — and the
session stays open afterwards, which the successful submit below proves.

### 2.7 Result `extracted` with a plausible small haul

```
$ curl -s -X POST $API/v1/raid/result ... \
    -d '{"session_id":"98ac6e2d-...","session_token":"zjPZP1iK...","outcome":"extracted",
         "items":[{"item_id":"scrap_metal","quantity":7},{"item_id":"duct_tape","quantity":2},{"item_id":"bandage","quantity":1}]}'
{"session_id":"98ac6e2d-0d38-43ee-ab29-071c0e3e162a","outcome":"extracted",
 "credited_items":[{"item_id":"bandage","quantity":1},{"item_id":"duct_tape","quantity":2},{"item_id":"scrap_metal","quantity":7}],
 "already_closed":false}
HTTP 200
```

### 2.8 Profile again — the haul is credited

```
{"player_id":"30b1c5a9-...","schema_version":1,
 "stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"bandage","quantity":1},
          {"item_id":"duct_tape","quantity":2},{"item_id":"medkit","quantity":1},
          {"item_id":"pistol","quantity":1},{"item_id":"scrap_metal","quantity":7}],
 "vehicle_tier":"none","unlocked_maps":["bridge"]}
```

Stash diff, before → after:

```
       bandage:    0 ->    1  (+1)
     duct_tape:    0 ->    2  (+2)
   scrap_metal:    0 ->    7  (+7)
      ammo_9mm:   60 ->   60   (0)
        medkit:    1 ->    1   (0)
        pistol:    1 ->    1   (0)
```

Exactly the contract stated in the brief: **profile stash = starter kit + credited loot**, with
nothing debited.

### 2.9 Idempotent re-submit

```
$ curl -s ... (identical body re-sent)
{"session_id":"98ac6e2d-...","outcome":"extracted",
 "credited_items":[{"item_id":"bandage","quantity":1},{"item_id":"duct_tape","quantity":2},{"item_id":"scrap_metal","quantity":7}],
 "already_closed":true}
HTTP 200
```

`already_closed:true`, the same `credited_items` echoed back, and the profile re-read afterwards is
byte-identical to §2.8 — **no double credit**.

### 2.10 Hostile probe again, now on the *settled* session

```
{"error":{"code":"implausible_items","message":"item bike_frame: at most 12 units fit a backpack (12 slots × a stack of 1), got 13"}}
HTTP 400
```

Still `400 implausible_items` rather than `already_closed:true` — the plausibility gate sits ahead
of session state in `internal/api/raid_handler.go` (`ValidateRaidItems` at line 153, the store call
at 165). Worth recording because it means a forged haul can never reach the store, regardless of
what the session is doing.

---

## 3. Deployed DB state

Read **only through the API** (`GET /v1/profile`), as required. No direct database access, no
Railway console, no `railway run psql`. Every stash assertion above and in §4.3 is a profile read.

---

## 4. Game-vs-prod pass, headless

### 4.0 Technique, and what was deliberately *not* built

The plan's Steps 1–2 add a `SarkoShot` exec to `SarkoPlayerController` and a `Scripts/hud-shot.sh`.
This task's mandate is verification, not patching, so **no code and no script was added.** Instead:

- The pawn is moved with `UCheatManager::BugItGo` — the engine's own teleport, exactly as the plan
  intended, preceded by `EnableCheats`.
- The delayed screenshot problem is solved with the engine's own **`-DUMPMOVIE`** switch
  (`LaunchEngineLoop.cpp:4303` → `GIsDumpingMovie = -1`, "remain on"), which writes one PNG per
  rendered frame into `Saved/Screenshots/MacEditor/`. With `t.MaxFPS 5` that is five frames a
  second for the whole run, so any HUD state that appears seconds after load is captured without
  needing a project-side timer. `-RenderOffscreen` supplies the real Metal RHI (`-nullrhi` renders
  nothing and would show no HUD at all), same as `Scripts/overview-shot.sh`.
- Default config was left pointing at prod (`bBackendEnabled=True`,
  `BackendBaseUrl="https://sarko-api-production.up.railway.app"`, `BackendMapId=bridge`), and **no
  `-ini` override was needed** for the run that mattered.

Command (Run A):

```bash
cd SarkoGame
echo "e2e-t9-gameA-1785454831" > Saved/SarkoDevice.txt
rm -rf Saved/Screenshots/MacEditor
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/SarkoGame.uproject" /Engine/Maps/Entry \
  -game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=900 -log -DUMPMOVIE \
  -ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -14500 18600 200"
```

`-14500 18600` is the centre of extraction zone 0 (`Северная тропа`) from
`Data/Maps/bridge.json`. The nearest bot spawn is more than 4000 uu away, so nothing interferes
with the dwell. The log lands in `~/Library/Logs/SarkoGame/SarkoGame.log` (macOS user log dir),
not in `SarkoGame/Saved/Logs/`.

### 4.1 Outcome achieved: **a real EXTRACTION, live against prod**

Not MIA. The whole online loop ran end to end and the server accepted an `extracted` result.

```
[2026.07.30-23.40.42:736][  0]LogTemp: Display: SarkoMap: spawned 42 loot containers
[2026.07.30-23.40.42:736][  0]LogTemp: Display: SarkoMap: spawned 3 extraction zones
[2026.07.30-23.40.45:738][  0]LogTemp: Display: SarkoBackend: authenticated as player a9451008-9665-44d6-aeec-1305d61e53dd
[2026.07.30-23.40.45:785][  0]LogCheatManager: BugItGo to: X=-14500.000 Y=18600.000 Z=200.000 P=0.000000 Y=0.000000 R=0.000000
[2026.07.30-23.40.46:123][  1]LogTemp: Display: SarkoBackend: raid session 047019dd-49fd-4ee5-b69e-ad7b70e95f33 opened, seed 1183603321
[2026.07.30-23.40.46:407][  2]LogTemp: Display: SarkoBackend: raid confirmed, server deadline 2026-07-31T00:02:46.000Z
[2026.07.30-23.40.46:407][  2]LogTemp: Display: SarkoRaidGameMode: raid live — seed 1183603321, clock 900s, session '047019dd-49fd-4ee5-b69e-ad7b70e95f33'
[2026.07.30-23.40.51:253][ 27]LogTemp: Display: SarkoRaidGameMode: extracted at zone 0 ('Северная тропа') with 0 backpack slots used
[2026.07.30-23.40.51:253][ 27]LogTemp: Display: SarkoRaidGameMode: raid finished as ESarkoRaidOutcome::Extracted, 895.1 s left on the clock
[2026.07.30-23.40.51:420][ 27]LogTemp: Display: SarkoBackend: result 'extracted' recorded: {"session_id":"047019dd-49fd-4ee5-b69e-ad7b70e95f33","outcome":"extracted","credited_items":[],"already_closed":false}
[2026.07.30-23.40.51:420][ 27]LogTemp: Display: SarkoRaidGameMode: result 'extracted' submitted
```

Hop by hop, against the brief's checklist:

| Hop | Evidence |
| --- | --- |
| auth OK | `authenticated as player a9451008-9665-44d6-aeec-1305d61e53dd` |
| start OK, seed logged | `raid session 047019dd-… opened, seed 1183603321` |
| confirm OK, clock 900 s | `raid confirmed, server deadline 2026-07-31T00:02:46.000Z` then `raid live — seed 1183603321, clock 900s` |
| no clamp warning | see §4.2 |
| outcome | `extracted at zone 0 ('Северная тропа')` after the full dwell — 895.1 s left of 900 |
| submit accepted | `result 'extracted' recorded: {…"already_closed":false}` — HTTP 200 from the deployed service |

The dwell timing is worth reading off the timestamps: the raid went live at `23:40:46.407` and the
extraction fired at `23:40:51.253` — **4.85 s of accumulated dwell**, i.e. the 5 s
`ExtractDwellSeconds` measured from the frame the session settled, not from the teleport. The pawn
was standing on the pad from `23:40:45.785`, and no dwell accrued during the `auth → start →
confirm` round trip, exactly as `Tick`'s `IsLootable()` guard intends.

`credited_items` is `[]` because nothing was looted — see the gap in §6. The empty haul is honest,
and it still exercised the whole submit path.

### 4.2 Absence checks (all clean)

```
$ grep -E "raising RAID_TTL is the fix|clock is probably skewed|playing OFFLINE|refused to activate" runA.log
NONE
```

- **No `raising RAID_TTL is the fix` warning** — with `RAID_TTL=20m` live the server's deadline
  (1320 s, minus the 120 s grace margin = 1200 s) comfortably exceeds the map's 900 s, so the
  clock is the map's. This is the plan's `RAID_TTL=12m` finding **resolved**: the clamp path is not
  reached in the deployed configuration.
- **No skew warning** — the 120 s skew bound in `ClockSecondsFromDeadline` did not trigger; the
  local clock and the server's agree.
- **No `playing OFFLINE`** — the raid was genuinely online for its whole life.
- **No `refused to activate`** — the settled-raid activation guard did not need to fire here (it is
  covered by automation).

### 4.3 The deployed DB agrees with the game

Re-authenticating with the *same* device id returns the same player the game logged, so this is the
game's own player read back through the API:

```
$ curl -fsS -X POST $API/v1/auth/anonymous -d '{"device_id":"e2e-t9-gameA-1785454831"}'
$ curl -fsS $API/v1/profile -H "Authorization: Bearer $JWT"
{"player_id":"a9451008-9665-44d6-aeec-1305d61e53dd","schema_version":1,
 "stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"medkit","quantity":1},{"item_id":"pistol","quantity":1}],
 "vehicle_tier":"none","unlocked_maps":["bridge"]}
```

`player_id` matches the `authenticated as player …` line exactly. Stash = the starter kit,
untouched: **nothing was debited at start and nothing phantom was credited on an empty haul.**

### 4.4 Run B — the interact-prompt run, and the `int32` seed mapping live

A second run with a **separate** device id (`e2e-t9-gameB-1785454927`, player
`76c91a14-f238-4f2d-9d39-bec8228fddd1`), teleported next to junk container 0 at
`(-17400, 16350, 35)`:

```
-ExecCmds="EnableCheats, t.MaxFPS 5, BugItGo -17400 16200 200"
```

```
[2026.07.30-23.42.18:983][  0]LogTemp: Display: SarkoBackend: authenticated as player 76c91a14-f238-4f2d-9d39-bec8228fddd1
[2026.07.30-23.42.19:353][  1]LogTemp: Display: SarkoBackend: raid session e77ed1a4-f79d-446c-bbaf-8e936b2dcf55 opened, seed -1568987543
[2026.07.30-23.42.19:569][  2]LogTemp: Display: SarkoBackend: raid confirmed, server deadline 2026-07-31T00:04:19.000Z
[2026.07.30-23.42.19:569][  3]LogTemp: Display: SarkoRaidGameMode: raid live — seed -1568987543, clock 900s, session 'e77ed1a4-f79d-446c-bbaf-8e936b2dcf55'
```

**`seed -1568987543`** — a negative `int32`. The server sends an unsigned 32-bit-range seed (§2.4
showed `3772544291` on the wire); `SarkoBackend::SeedToInt32` folds it into `int32` and the raid
runs on it. That is the plan's `seed`-overflow conflict observed working in production rather than
only in a unit test.

This run was killed after the screenshot, so its session was left open. The server's `expires_at`
sweeper closes it as `died` after `RAID_TTL` — the documented, non-wedging failure mode, and the
reason a separate device id was used so it cannot muddy Run A's evidence. `Saved/SarkoDevice.txt`
was deleted afterwards, so the next launch generates a fresh device rather than inheriting this
half-finished raid.

---

## 5. Screenshots

Both are `-RenderOffscreen` frames from the runs above, in
`SarkoGame/Saved/Screenshots/task-9/` (`Saved/` is gitignored — no binary asset is committed).

### `/Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame/Saved/Screenshots/task-9/interact-prompt.png`

Run B, frame `MovieFrame00060`, 1600×900. The pawn stands beside the pale junk container at
`(-17400, 16350)`; **`ОБШУКАТИ (junk)`** is drawn top-centre in its dark plate, the lit interact
button (`E`) sits on the right-hand side at mid-height, `0/12` is top-left beside the `30`-round
ammo readout, the raid clock reads `14:49`, and the health bar is full and green top-right. This is
the plan's Step 8 expectation, met.

### `/Users/ruslanbondarenko/project/ai-workspace/home/Sarko/SarkoGame/Saved/Screenshots/task-9/extracted-summary.png`

Run A, frame `MovieFrame00202`, 1600×900. The dimmed frame with **`EXTRACTED`** in green and
**`НІЧОГО НЕ ВИНЕСЕНО`** beneath it, the pawn standing at the centre of the green extraction pad
(zone 0), `0/12` top-left, clock frozen just under 15:00. This is the summary of the *same* raid
whose `result 'extracted' recorded` line is quoted in §4.1 — the screenshot and the accepted submit
are one event, not two.

Neither shot is an itemised summary, because neither run looted anything (§6).

---

## 6. Gaps — what the plan wanted and this pass could not honestly verify

1. **No haul looted by the game.** The loot channel is 1.5 s of *held* input, and a headless
   `-unattended` run has no input path. The only project exec that touches gameplay is
   `CheatEmptyMagazine` (fires the weapon); there is no loot or extract cheat, deliberately, and
   this task was forbidden from adding one. Consequence: the game's own `credited_items` was `[]`,
   and the itemised-credit half of the contract is evidenced by §2.7–2.9 (`curl`, same deployed
   service, same code path in `raid_handler.go` and the store) rather than by the game. The plan
   itself calls this out — Step 6 is explicitly "a human run" — so this is the plan's own
   boundary, not a regression.
2. **No itemised `EXTRACTED` summary screenshot.** Follows directly from (1). The plan anticipated
   exactly this split (Step 9: "this run looted nothing, which is correct and still proves the
   summary path") and asked for the split to be named rather than papered over. It is named.
3. **MIA not exercised live.** Extraction was achieved instead, which the brief preferred. Note for
   the record: MIA could *not* have been forced with a short `-ini`-overridden duration, because
   the clock comes from `CachedDefinition.RaidDurationSeconds` — `Data/Maps/bridge.json`'s
   `raidDurationSeconds: 900` — and `USarkoRaidSettings::RaidDurationSeconds` (480 s in
   `DefaultGame.ini`) is only the fallback for a map that failed to load. Shortening the raid would
   have meant editing map data. `died` acceptance is covered by automation and by the sweeper path
   in §4.4.
4. **Plan Steps 1–2 not performed** (the `SarkoShot` exec and `Scripts/hud-shot.sh`). Verification
   mandate; `-DUMPMOVIE` did the same job with zero new code. Whoever wants the tooling
   permanently can still add it — this report is not a reason to skip it.
5. **Plan Step 3 (deploy) skipped** — the backend was already deployed. Health, starter kit, tier
   floor and the plausibility gate were verified as deployed behaviour instead.
6. **Plan Step 7's expected loadout debit is obsolete.** `pistol`/`ammo_9mm` show a delta of zero
   at `raid/start` by design after the F1 fix; §2.4 proves the stash is untouched. Judged against
   the current contract, as instructed.
7. **Plan Step 4's throwaway-session teardown was unnecessary.** The plan closes the probe session
   with a `died` result so `one_open_raid_per_player` does not block the real raid; here the probe
   ran *on* the real session (a `400` before the store leaves it open), so one session covered both.
8. **`sarko-api`'s own `make test` was not run.** Plan Step 10 asks for it; it needs a local
   Postgres on port 5455 and the brief scoped this task to the deployed service, so the backend was
   verified as deployed behaviour (§2) instead of as a local suite.

## 7. Concerns

1. **Six orphaned `UnrealEditor-Cmd -nullrhi -game` processes** were already running on this
   machine when this task started (PIDs 71867, 72963, 74203, 76616, 78216, 79852; 32–40 minutes
   elapsed each, parent `1`). They are not mine — no `-DUMPMOVIE`, and they predate every command
   in this report — and they were left alone in case a sibling task owns them. They are burning
   CPU and, if any authenticated, may be sitting on open raid sessions until the sweeper closes
   them. Worth a `kill` by whoever owns them.
2. **`-DUMPMOVIE` is heavy**: ~2 MB per frame at 1600×900, five frames a second, ~200 frames for a
   40-second run. Fine for a one-off under gitignored `Saved/`; do not leave it on.
3. **A killed run leaks an open raid** (§4.4). Harmless — the sweeper closes it as `died` after
   `RAID_TTL` — but it means the *same* device cannot start another raid for up to 20 minutes
   without falling through to the offline path. Anyone iterating headlessly should either let the
   raid finish or rotate the device id, as this pass did.
4. **The 5 s dwell is measured from session-ready, not from entering the zone.** Correct and
   deliberate (`IsLootable()` gates the `Tick`), but it means a player who reaches an extraction
   during a slow `auth → start → confirm` round trip waits out the round trip *and then* the full
   dwell. Not a bug; worth knowing when the round trip is slower than the ~1.2 s seen here.
5. **`ОБШУКАТИ (junk)`** exposes the raw tier id to the player. Cosmetic, and outside this task.
