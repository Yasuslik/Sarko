# Sarko — the scarcity stage: every number becomes true

Date: 2026-08-02. Status: synthesis of the deep design study
(`.superpowers/sdd/design-study-2026-08-02.md`, gitignored); owner approval pending.

## 0. The finding that frames everything

Nothing in a raid is scarce, so no decision has a price. Ammo is literally infinite —
`FinishReload` refills the magazine unconditionally and nothing ever consumes `ammo_9mm` from
the grid. Time is 2× surplus (98 s out + 88 s back in a 900 s raid). Health regenerates and the
map is safe after the last fight. The realism stage shipped the *numbers* of scarcity without
the *fact* of it. This stage makes the numbers true.

## 1. Ammo is consumed (small C++, the keystone)

Reload moves rounds from the grid's `ammo_9mm` stacks into the magazine — partial reloads
allowed (3 rounds in the bag = 3 in the mag), reload with zero rounds is the dry-click path.
The HUD's magazine count gains a reserve figure (`8 | 24`). The tutorial's authored 46 rounds
across the route becomes a real budget: ~4 fights' worth with misses. Loot-table ammo, already
halved, becomes a find that changes behaviour.

## 2. The raid is 10 minutes, not 15

`raidDurationSeconds` 900 → 600 on the map. Measured route time is ~3 min of walking; 600 s
keeps looting under pressure without making the tutorial route tight. (One field.)

## 3. Map surgery (all JSON, coordinates verified by the study)

1. **Seal two 150 uu holes in the closure** at x −6100 and x −9100 (rim ends y ±1950, closure
   starts y ±2100) — a pawn fits through today, into 300 m of empty map with a live E2 pad.
   Blocks must butt, never overlap (the expander's rules).
2. **West fork**: promote two cement-works props to containers (`good` at −17400,−7000;
   `common` at −18000,−10600) — "shallow-and-poor vs deep-and-rich" becomes a real route
   question with no new topology.
3. **«Западный кордон» moves to (−18400,−9400)** and opens at **240 s** (was 600) — it becomes
   a live option while the player is still deep, not a button on a lawn after the fighting ends.
4. **The `wg` treeline stand moves +3500 y** to cover the road's dead 16-second stretch.
5. **Encounter ② relocates into the freight cars** (trigger −14500,−13600; post −13000,−15200)
   — the second fight happens among cover, not across a 4800 uu open field.

6. **The edge stops looking like the end of the data** (owner, 2026-08-02). A non-colliding
   ground *skirt* extends ~4000 uu beyond the border on all sides (the camera sees ~1570 uu
   laterally — double margin), carrying dense instanced `treeline` masses, the pylon line
   continuing outward, and two or three darkening tone bands so the world fades into
   wilderness instead of cutting to black. North (the ТЗ's world border) is thickest forest;
   east/south skirt merely grounds the already-visible bridge and water tower so they stop
   floating over void. And the safety net best practice demands: **KillZ recovery** — a pawn
   that somehow leaves the world (physics, an unfound hole) teleports to the nearest valid
   route point with a loud log line, losing a second, not the raid. The border is made
   beautiful, but never trusted alone.

## 4. Feedback without sound (pure Canvas/C++)

There is no hit feedback of any kind today. In fun-per-effort order:
- **Directional damage arc** on the HUD (the camera is world-locked, so "where from" is real
  information the player cannot otherwise get).
- Hit marker on landing a shot; a brief white flash on the enemy body (dynamic material
  instance, the tint machinery exists).
- Screen-edge vignette pulse at low health (drawn, not a material).
- A closed extraction pad draws grey (it already labels itself closed; the pad should agree).

## 5. Replayability: seeded encounter rotation

Raid 2 is identical to raid 1 today — 3 one-shot encounters in fixed places; `normal: 8` budget
is unspendable over 4 authored points. Author **7–9 encounter rows** across the west third,
mark non-tutorial rows optional, and shuffle activation order against the raid's replicated
`Seed`. Same enemy cap, same budget law, same guaranteed 1v1 first fight — different geography
every raid. (~8 JSON rows + ~20 lines.)

Also: `EncounterMinSpawnDistanceUU` 1800 → 2600 — the measured landscape half-view is ~1570 uu,
and the 1800 figure was calibrated against a stale portrait-camera comment.

## 6. The garage rate is broken (data only)

Monte-Carlo over 20 000 simulated careers: **median 30 raids to the bicycle, p90 67** —
`wheel_small` (weight 1/103, needed twice, qty {1,1}) is the bottleneck. Retune:
`bike_frame` weight 2→5; `wheel_small` 1→8 with qty {1,2}; `chain` into military at 4.
Result: **median 6 raids, p90 12**. The first vehicle should be a first-week goal, not a
month-long grind.

## 7. Stealth gets a verb (small C++)

`TickAI` treats proximity as hearing — there is no way to be quiet. Hearing keys off **noise
events** (firing = loud, running = audible, walking = quiet, standing = silent) instead of
distance-to-player. This makes the LOS/Investigate work meaningful: you can now *cause* an
investigate by shooting, and avoid one by walking. No new UI.

## 8. Deferred, named

The shop (`МАГАЗИН НЕЗАБАРОМ` already promises it; it closes the scrap → ammo → raid loop once
ammo is consumable) — next stage, after this one proves scarcity plays well. Insurance and gun
condition — killed by the study as not worth it. Downed state — still deferred. Sound — its own
stage; §4 is the bridge until then.

## Killed darlings (recorded so they stay dead)

Cover at the pipe crossing's south mouth (the one space that already works; props would make it
a camping spot). A second parallel pipe lane (same rim gap — a wider corridor sold as a choice).
A stash-value milestone ladder (a number with no consequence, competing with the garage panel).
