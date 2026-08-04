# Sarko — you see where you look

Date: 2026-08-04. Status: owner decision of 2026-08-04, after the first phone playtest.

## 0. The ask

*"Надо теперь сделать туман войны, типа область видимости — куда кручусь, туда и видно."*

A top-down camera shows the player everything within the frame, which is why nothing in the
sector is ever a surprise. Limiting sight to where the character faces is what turns the map
from a diagram into a place you can be ambushed in — and it is the single change that makes the
noise model, the encounters and the 3–5 enemy budget all matter more than they do today.

## 1. Three states, not two

Classic fog of war distinguishes unexplored, remembered, and visible. Ours needs the last two:

- **Inside the vision cone, with line of sight** — full brightness. Enemies are drawn.
- **Outside the cone (or behind cover)** — the terrain stays visible but **dimmed**. You
  remember the wall and the crate; you do not see what is happening there now.
- **Enemies** — drawn only when inside the cone *and* in line of sight. This is the whole point:
  the thing you cannot see is the thing that kills you.

**Geometry is never hidden.** There is no map and no compass; a dark screen would leave the
player lost rather than tense. Dimming preserves navigation and still reads as "not now".

## 2. What the cone is

- **Rotates with the character's facing**, which already follows the aim stick. Turning to look
  is a deliberate act with a cost: your back is dark while you do it.
- **Wide enough to play on a phone.** Human vision is ~120° including periphery; a narrow cone on
  a small screen reads as a bug, not as tension. Start generous (≈110–120°) and tune down only if
  play says it is too easy.
- **Screen-space, drawn in Canvas**, apex at the pawn's projected position. The camera is
  world-locked and overhead, so a screen-space fan is geometrically honest here — no assets, no
  render target, no post-process material, and nothing new on the mobile GPU budget.
- **Soft edge**, not a hard wedge: a stepped alpha ramp across a few degrees so the boundary
  reads as vision falling off rather than as a drawn triangle.

## 3. Line of sight

Enemies are hidden by a server-side trace, not by the client's drawing — the client must never
be the authority on who is visible, and a client that draws an enemy it should not see is a
cheat surface either way. The AI already runs LOS traces (`SarkoAIController`), so the machinery
and its cost are known.

**Walls do not cast shadows in the dimming layer.** True per-pixel shadowcasting needs a render
target and a material; the cone plus enemy-hiding delivers most of the tension for a fraction of
the cost. If play says the missing shadows matter, that is the next step, not this one.

## 4. What this changes about the rest of the game

- **Hearing becomes the other half of perception.** The noise model already distinguishes firing,
  running and walking; now not-seeing gives it something to do. A shot behind you is information
  you cannot get any other way.
- **The damage-direction arc stops being a nicety.** When you are hit from the dark, the arc is
  the only thing telling you where to turn.
- **Encounters get harder without changing a number**, because a spawn "outside the player's view"
  is now genuinely outside it. Check the 2600 uu spawn rule still behaves.

## 5. Risks

- **Too dark is unplayable**, especially on a phone in daylight. The dim level is a setting and
  wants tuning on the device, not in the editor.
- **Disorientation**: no map, no compass, and now no peripheral vision. Dimming rather than
  blacking is the mitigation; if players still get lost, the answer is landmarks, not brightness.
- **The tutorial teaches nothing about this yet.** A player who cannot see behind them should be
  told once, in the first raid, the way the other controls are.
- **Bots must not gain an advantage the player cannot answer.** They already have LOS gating and
  a hearing model; verify the player's cone is not narrower than a bot's effective awareness, or
  the fight is unfair in a way that reads as cheating.

## 6. Out of scope

Per-pixel wall shadows, a memory layer that greys out areas never visited, enemy last-known-
position markers, a minimap, and any change to the camera itself.
