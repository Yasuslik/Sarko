# Sarko — Container inventory: a grid you loot from, and a backpack you can lose

Date: 2026-08-04. Status: owner decisions of 2026-08-04, recorded here before implementation.

## 1. What changes and why

Today looting is: walk up, hold the interact button 1.5 s, and everything the server rolled that
fits is teleported into your backpack. There is no decision in it, and the remainder that does
not fit is lost forever (a known defect: the container is marked looted regardless).

It becomes: open a container, see its contents as a grid of cells beside your own grid, and take
what you choose while the world keeps running. That turns looting into the game's central
decision — what is worth the seconds you spend standing still, and worth the space you carry it
in.

## 2. Owner decisions

1. **Landscape only.** Already done (`f5fec68`).
2. **Cells, not spatial packing.** Each item occupies exactly one cell; a stack shows a count.
   A true Tarkov grid (items sized W×H, rotation, packing) is deliberately deferred — with
   fifteen item types there is nothing to pack, and it is weeks of work for a feel we can
   approximate now. Revisit when the catalog is large enough that packing is a real puzzle.
3. **The backpack is an item.** Base capacity is **4 cells** (pockets). A backpack found in a
   raid grants **+8**, for 12 — today's fixed number becomes the equipped case. Losing the
   backpack on death is a real loss, and carrying one is a choice against carrying something else.
4. **Looting does not pause the world.** Bots keep moving and hearing while the panel is open.
   This is the tension the genre runs on; a pause would make looting free. It also keeps the
   client honest — a pause on one machine stops nothing on the server.

## 3. Server model

- **Contents are rolled once, on first open, and live in the container.** Today the roll happens
  at channel completion and everything is pushed to the backpack. Instead: on the first successful
  open the server rolls (`RollContainerFor`, existing, with the server-only `LootSalt`) and stores
  the result as the container's inventory; the container is marked *opened*, not *looted*.
- **Transfer is a request, per item.** `ServerTakeItem(ContainerIndex, SlotIndex)` — validated in
  this order: raid live and not settled → index in range → container opened → pawn alive → server
  re-measured distance within the interact radius → slot non-empty → capacity available. Anything
  that fails logs and changes nothing. This mirrors the existing loot-channel validation chain,
  which is already hostile-input safe.
- **A "take all" convenience** applies the same per-item path in a loop and stops when capacity
  runs out — the remainder stays in the container, which is what fixes the vanishing-loot defect.
- Replication: container inventories are server truth. A client sees a container's contents only
  after opening it (owner-scoped or on-demand), never before — the loot map must not be derivable,
  which is the same reason the roll takes a server-only salt.

## 4. Client and UI

- **A Slate panel**, reusing the shelter's approach (`SDPIScaler` over a point-based canvas, safe
  area respected, ≥44 pt tap targets). Container grid on one side, player grid on the other,
  landscape layout.
- **Tap to transfer** in the MVP; drag-and-drop is a later improvement, not a blocker.
- The panel closes on: moving away beyond the interact radius, taking damage, the raid settling,
  or an explicit close. The player remains steerable while it is open — they are standing in the
  open, and should be able to run.
- The existing 1.5 s channel becomes the **open** action. Opening is the risk; taking is fast.

## 5. Risks worth stating

- **Standing still in a UI on a touchscreen is where players die.** That is intended, but it must
  be legible: the panel cannot cover so much of the screen that an approaching bot is invisible.
  Keep it to one side, leave the play area visible, and keep the health bar and the clock on top.
- **Capacity feedback must be immediate.** "Why did nothing happen?" is the failure mode when a
  take is silently refused for lack of space; the refused cell needs a visible reason.
- **Backpack-as-item touches the death path.** `ClearOnDeath` currently empties a fixed 12-slot
  bag; with an equipped backpack the equipped item is lost too, and base pockets must survive or
  not by an explicit decision. Default: pockets are lost as well — everything carried is lost, per
  the game's core rule; only what is in the shelter stash is safe.

## 6. Out of scope

Spatial packing, item rotation, drag-and-drop, weight, containers with per-slot restrictions,
looting bots' bodies, and equipping anything other than a backpack.
