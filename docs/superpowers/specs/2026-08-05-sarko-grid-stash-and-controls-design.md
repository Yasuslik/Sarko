# Sarko — a grid with size, a stash worth returning to, and controls a thumb can reach

Date: 2026-08-05. Status: owner decisions of 2026-08-05, recorded before implementation.
Supersedes the "cells not spatial packing" decision in
`2026-08-04-sarko-container-inventory-design.md` §2.2 — the owner has asked for item sizes.

## 1. Items have size

Every item occupies a **rectangle of whole cells**, not one slot. A rifle is long, a pistol is
not, a bandage is small. This is what makes a bag a puzzle instead of a counter.

**No rotation.** Rotation doubles the placement logic and, on a phone, doubles the ways a player
can fail to do what they meant. Every item is authored in the orientation it occupies.

**Auto-placement, first fit** — scanning left to right, top to bottom, taking the first free
rectangle that fits. Manual rearranging is a later improvement, not a launch requirement:
on a touchscreen, dragging a 3×1 rifle into a gap is fiddly, and the interesting decision is
*what to carry*, not *how to tessellate it*.

### 1.1 The size table

`w × h` in cells. Authored in `Data/Items/items.json` alongside `stackSize` and `category`.

| item | size | why |
|---|---|---|
| `pistol` | 2×1 | fits pockets; the weapon you can always carry |
| `rifle` (future) | 3×1 | **cannot fit pockets** — see §1.2 |
| `ammo_9mm` | 1×1 | stacks 60 |
| `medkit`, `bandage`, `painkillers` | 1×1 | |
| `scrap_metal`, `copper_wire`, `duct_tape` | 1×1 | |
| `canned_food`, `vodka`, `cigarettes` | 1×1 | |
| `toolbox` | 2×1 | a real box, and the first valuable |
| `backpack` | 2×2 | bulky when carried rather than worn |
| `bike_frame` | 3×2 | the biggest thing in the game so far |
| `wheel_small` | 2×2 | round and awkward |
| `chain` | 1×1 | |

A stack occupies one rectangle regardless of count: 60 rounds take the same 1×1 as 1 round.
Stack size stays the cap on how many ride in that rectangle.

### 1.2 Pockets are 2×2; a backpack adds 4×2

- **Pockets: a 2×2 grid.** Always present, cannot be lost while alive.
- **Backpack: a separate 4×2 grid**, present only while one is worn.

Total 12 cells with a bag, 4 without — the numbers the previous design already used, now with a
shape. The shape is the point: **a 3-wide rifle cannot fit a 2-wide pocket grid**, so the best
weapons are uncarryable without a backpack. Nothing has to explain that; the grid refuses.

Two grids rather than one growing grid, because the player must be able to see at a glance what
survives losing the bag — and because a bag is a thing you find, wear, and lose as a unit.

### 1.3 Death

Unchanged and explicit: everything carried is lost — pockets, backpack, and the backpack itself.
Only the shelter stash is safe. That is the game's core rule and the grid does not soften it.

## 2. The stash is the same grid

The shelter's stash is a plain text list today, which now looks poorer than the crate the player
just looted. It becomes **the same cell grid**, reusing the container panel's brushes, palette and
labels — one visual language for "things you own".

- **Large, scrollable, page-sized** — the stash is not a raid panel squeezed into a corner; in
  the shelter there is no threat and the screen is free.
- **Not a puzzle.** The stash grid is generous and auto-arranges; the scarcity is in the raid, not
  in storage. A stash the player has to tetris is punishment without tension.
- **Sorted** by category then name, so the same item is always in the same place.

## 3. The garage gets its button

The loop currently has no destination: parts land in the stash and nothing consumes them. The
backend's `POST /v1/garage/craft` has existed and been tested since the first slice.

- The garage panel lists the recipe with **have/need per part**, drawn from the stash.
- **A craft button, enabled only when every part is present**, disabled with the missing part
  named otherwise — never a dead button with no explanation.
- On success: the parts leave the stash, the vehicle tier rises, and the shelter says which map
  it opened. That sentence is the payoff for every raid before it.

## 4. Controls: what a thumb can actually reach

The screen is landscape. Two thumbs rest at the bottom corners and swing in an arc of roughly
40–45 pt radius around where they land. Everything below is expressed in that geometry.

### 4.1 The sticks stay

- **Left thumb — move.** Floating stick, bottom-left quadrant, appears where the thumb lands.
- **Right thumb — aim.** Floating stick, bottom-right quadrant.

Floating (rather than fixed) sticks are kept deliberately: a fixed stick on a phone is a thing you
have to find, and finding it costs the second that kills you.

### 4.2 Firing

**Hold the aim stick past the dead zone to fire.** No separate fire button for the primary case.
A dedicated fire button competes with the aim stick for the same thumb, and every mobile shooter
that ships both ends up with players using one.

The existing fire-on-release edge is kept as the *tap* case: a quick flick aims and fires once.
Hold and it keeps firing at the weapon's interval. Both are the same thumb, and neither needs a
button.

**Desktop keeps space**, unchanged, because that is how the owner plays while developing.

### 4.3 Reload

**A dedicated button**, because reloading is a decision with a cost and the player must be able to
make it *before* the magazine runs out — auto-reload-when-empty is the thing that gets you killed.

- Sits **above the right thumb's stick**, inside its arc, clear of the stick's own travel.
- **Shows state**: the magazine count lives on it, it goes amber below a third, and it pulses when
  empty. The player should never have to look at two places to know they need to reload.
- Auto-reload on empty stays as a safety net, but the button always wins if pressed first.

### 4.4 Interact / search

**Contextual**, as now — it appears only when something is in reach, in the right thumb's arc,
above the reload button. Its label says what it will do (`ОБШУКАТИ`, `ЕВАКУАЦІЯ`), because a
generic action button in a game with two actions is a guess.

### 4.5 The conflict this design has to resolve

The loot panel currently sits **bottom-right, on top of the aim stick**, passing touches through
everywhere except its cells. That is a genuine hazard: while looting, a thumb reaching for the aim
stick can land on a cell instead.

**Resolution: the loot panel moves to the left half**, over the *move* stick, and the move stick is
suppressed while it is open. Rationale: looting already requires standing still, so movement is the
input you can afford to lose for those seconds — but shooting is not, and a player interrupted
mid-loot must be able to fight back with the thumb that was already there.

While the panel is open: the aim stick, fire, and the reload button all keep working untouched.

## 5. Risks worth stating

- **Auto-placement can refuse for a reason the player cannot see.** "There is room but it will not
  fit" is the classic spatial-grid frustration. The refusal must say *why* — no space of that shape
  — and the panel should show the shape that failed.
- **Sizes are balance, not decoration.** Making the rifle 3 wide is what makes backpacks matter; if
  a later item is sized carelessly the rule quietly stops holding.
- **The stash grid must not become a second puzzle.** If it ever fills, grow it rather than making
  the player pack it.
- **Reload button placement fights the interact button** for the same thumb arc. They must never
  occupy the same rectangle, and the interact button appearing must not shift the reload button —
  a control that moves is a control you mis-press.
- **Suppressing the move stick while looting** means a player who opens a crate at the wrong moment
  cannot immediately run. That is intended tension, but if it reads as a bug in play, the fallback
  is to shrink the panel rather than restore movement under it.

## 6. Out of scope

Manual drag-to-rearrange, rotation, weight, per-container slot restrictions, equipping anything
other than a backpack, selling, and the shop.
