# Sarko — a character you equip, and a garage of its own

Date: 2026-08-03. Status: owner decisions of 2026-08-03.

## 0. What the owner asked for

The bicycle leaves the stash screen and gets its own **Garage**. The stash screen becomes a real
inventory: **the character on the left with equipment slots** — weapon, backpack, clothing — each
its own slot, beside the stash grid.

## 1. The shelter becomes a hub

Three destinations plus the raid, landscape, thumb-reachable:

- **ІНВЕНТАР** — the character and the stash (§2). The default screen.
- **ГАРАЖ** — vehicle assembly, alone on its own screen (§3).
- **МАГАЗИН** — still a stub, but now a peer rather than a greyed button in a corner.
- **В РЕЙД** — always visible, always reachable; it is the verb the whole shelter serves.

Navigation is a left-edge column of destination buttons (a phone's thumb reaches the edges, not
the middle-top), each ≥44 pt, the current one marked. No modal screens: switching is instant.

## 2. ІНВЕНТАР — the character on the left, the stash on the right

**Left: the character.** A simple figure drawn in Slate (no art assets), with slots laid out on
the body:

| slot | accepts | size | notes |
|---|---|---|---|
| `Weapon` | category `weapon` | the item's own rect (pistol 2×1, rifle 3×1) | the raid's gun |
| `Backpack` | category `gear`, backpack-like | 2×2 | grants the 4×2 carry page (§4 of the grid spec) |
| `Clothing` | category `gear`, wearable | 2×2 | cosmetic + a future armour hook; empty is legal |
| `Pockets` | anything | the 2×2 carry grid | shown inline, always present, always yours |

A slot refuses an item of the wrong category, and says why (the refusal discipline the container
panel already has: shake, amber rim, and a named reason).

**Right: the stash grid** — as it is today, generous, auto-arranged, sorted, scrollable.

**Equipping is a tap.** Tap a stash item → it goes to its slot if that slot is empty and the
category matches. Tap an equipped item → back to the stash. No drag, matching the container
panel's tap-to-transfer.

## 3. ГАРАЖ — its own screen

The recipe with have/need per part, drawn from the stash; the craft button enabled only when
every part is present and naming the missing one when not; on success the vehicle tier rises and
the screen says which map it opened. Room, now, to show the vehicle ladder — what is built, what
is next — which the cramped corner never had.

## 4. The consequence: the loadout becomes real

Today `/v1/raid/start` is sent an **empty** loadout by deliberate decision, because the weapon
was abstract and debiting the stash for it would have been dishonest risk. With equipment that
reverses:

- What is equipped **is** the loadout: it is debited from the stash at raid start, carried into
  the raid, **lost on death**, and returned (with the haul) on extraction.
- The equipped weapon determines the in-raid gun; ammo comes from the carry grid, as it already
  does since the scarcity stage.
- The backend's plausibility gate already reasons in cells and stacks — the loadout path must be
  re-checked against it, since it has only ever seen an empty list.

**The dead-end risk, and the answer.** A new player who dies with their only weapon equipped has
nothing to equip next raid. Tarkov answers this with free scav runs. Ours: **entering a raid
unarmed is always allowed** (the map's authored loot includes a pistol), and the backend's
one-time starter kit stays. If the stash has no weapon, the raid button says so plainly rather
than blocking — "БЕЗ ЗБРОЇ" is a choice, not an error. This must be true before the loadout is
debited, or the game can strand a player.

## 4.5 ВИЛАЗКА — the free run that gets you out of a hole

Owner decision, 2026-08-03: the genre's scav run, under our own name. The run is a **вилазка**
(a sortie); the player goes as a **ходок** — someone who walks into the zone with what he could
borrow.

**Why it exists:** §4 makes the loadout real, and a real loadout can strand a player. "You may
enter unarmed" is a floor, not a ladder — you go in with nothing and, statistically, come back
with nothing. A вилазка is the ladder: **you go with borrowed gear, and what you extract is
yours.** Broke → sortie → walk out with a pistol → you own a pistol. That is the recovery path,
and it is the reason the mechanic is worth building rather than just tolerating an unarmed run.

**The rules:**
- **Free.** Nothing is debited from the stash; the kit is granted for the raid.
- **The kit is granted by the server**, never claimed by the client — one of a few authored
  loadout tables (a worn pistol and a few rounds; sometimes a bag, sometimes not; occasionally
  something better). The variance is the appeal.
- **Extraction keeps everything** — the borrowed gear and the haul both credit to the stash.
  Death loses all of it, as always.
- **A cooldown**, server-owned and server-timed, so it cannot be farmed: the sortie is available
  again some minutes after the last one ends. The client displays the remaining time; it does not
  decide it.
- **It does not count as the tutorial.** `tutorial_completed` latches only on a normal raid's
  extraction — otherwise a free run could teach the wrong lesson and skip the teaching layout.
- **The trade-off is quality, not danger:** the kit is mediocre by design, and the clock is
  shorter than a normal raid. No mechanic makes a вилазка *safer*; it makes it cheaper.

**Where it lives:** a second button beside В РЕЙД in the shelter, showing either "ВИЛАЗКА" or the
cooldown remaining. Two ways into a raid, one of which costs nothing.

**Trust boundary:** the mode is a parameter on raid start, but everything it implies — free entry,
the granted kit, the cooldown — is decided and enforced on the server. A client that asks for a
sortie during the cooldown is refused by name, and a client cannot ask for a *better* kit.

## 5. What this does not add

No armour mechanics (the clothing slot is a hook, not a system), no weapon mods, no drag to
rearrange, no selling, no insurance. The shop stays a stub.

## 6. Risks

- **The loadout debit is the riskiest change in the project so far** — it is the first time the
  shelter's contents can be destroyed by a raid. Every path (equip → start → die/extract →
  refund/credit) needs to be right, and the backend's refund-on-expiry path now matters for real.
- Equipment state must survive the trip: it lives with the profile, not the world.
- The character drawing is code, so it will be crude. That is acceptable if the slots read; it is
  not acceptable if the player cannot tell which slot is which.
