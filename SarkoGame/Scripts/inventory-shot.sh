#!/usr/bin/env bash
#
# Boots a raid, puts a mixed haul in the player's bag, opens the crate next to
# them, and screenshots the frame with the container panel on it. Prints the
# PNG path.
#
# Two traps this inherits from Scripts/hud-shot.sh, both of which cost a whole
# run to learn the first time:
#
#   * **-RenderOffscreen, NOT -nullrhi.** nullrhi renders nothing at all; the
#     process runs, the tests pass, and no PNG is ever written.
#   * **`Shot showui`, NOT HighResShot.** HighResShot goes through the scene
#     renderer and captures no Slate, so the PNG comes out with the world on it
#     and no panel — which looks exactly like a panel that failed to draw.
#
# The safe area is ON by default here, unlike hud-shot.sh: every layout claim
# this panel makes is about a phone (bottom-right of the SAFE frame, clear of
# the home indicator), and a Mac reports no insets at all, so a shot without
# sarko.SafeArea.DebugPhoneLandscape cannot show whether any of it is true.
#
# A headless run has no fingers, so the panel cannot be opened by holding the
# interact button and no cell can be tapped. Three `#if !UE_BUILD_SHIPPING`
# execs on ASarkoPlayerController stand in for them, each retried on a timer
# because -ExecCmds is queued at engine init and the raid's authoritative seed,
# the loot channel and the panel's construction all land later:
#
#   SarkoDebugLoot <n>              fill the bag with n mixed-category stacks
#   SarkoOpenNearestContainer       channel open the nearest crate
#   SarkoTapContainerCell <i> <s>   press cell i, then shoot s seconds LATER
#   SarkoInventoryShot <secs>       take the shot that many seconds in
#
# Usage:
#   Scripts/inventory-shot.sh              # 9 stacks: a mixed, nearly-full bag
#   INV_BAG=12 Scripts/inventory-shot.sh   # a FULL bag
#   INV_TAP=0 INV_BAG=12 Scripts/inventory-shot.sh   # ...and a refused take
#   INV_BAG=0 Scripts/inventory-shot.sh    # four pockets, empty: the short panel
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
RES_X="${INV_RES_X:-2556}"
RES_Y="${INV_RES_Y:-1179}"
TIMEOUT="${INV_TIMEOUT:-300}"

# How many mixed stacks go into the bag. Nine is "nearly full at twelve", which
# is the frame that actually answers whether a loaded grid is legible; an empty
# grid proves nothing.
BAG="${INV_BAG:-9}"

# Seconds before the shutter. The loot channel is 1.5 s and the open pump ticks
# at 0.5 s, so anything under ~4 photographs a panel that is not up yet.
SHOT_AT="${INV_SHOT_AT:-7}"

# Frame rate. 10 keeps a long headless run cheap, but a frame every 100 ms
# cannot sample a 120 ms transfer flash — the shutter lands either side of it.
# Raise this when photographing motion, not when photographing layout.
FPS="${INV_FPS:-10}"

# The rail depot's military crate. Close enough to walk into, and its own roll
# is overwritten by SarkoOpenNearestContainer with four different categories so
# the frame answers "are these hues distinguishable" rather than whatever one
# tier happened to produce.
X="${1:--10800}"
Y="${2:--17400}"
Z="${3:-200}"

# When a cell is tapped, the shutter is chained off the TAP rather than off
# engine start — the refusal pulse is 240 ms and the tap lands whenever the loot
# channel finishes, so a boot-relative shutter catches it only by luck.
# INV_TAP_SHOT is that delay, in seconds after the tap.
TAP_CMD=""
SHOT_CMD="SarkoInventoryShot $SHOT_AT"
if [[ -n "${INV_TAP:-}" ]]; then
	TAP_CMD="SarkoTapContainerCell $INV_TAP ${INV_TAP_SHOT:-0.12}, "
	SHOT_CMD="SarkoInventoryShot 25"   # a backstop only, in case the tap never lands
fi

SAFE_CMD="sarko.SafeArea.DebugPhoneLandscape 1, "
if [[ "${INV_SAFE_AREA:-1}" == "0" ]]; then
	SAFE_CMD=""
fi

rm -rf "$SHOT_DIR"

# EnableCheats before BugItGo (a cheat manager only exists for a local
# controller in a non-shipping build), and Walk after it — BugItGo calls Ghost(),
# which leaves capsule collision off for the rest of the run, and the loot
# channel measures distance from a pawn that has to be standing on the ground.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash \
	-windowed -ForceRes -ResX="$RES_X" -ResY="$RES_Y" \
	-ExecCmds="t.MaxFPS $FPS, ${SAFE_CMD}EnableCheats, BugItGo $X $Y $Z, Walk, SarkoDebugLoot $BAG, SarkoOpenNearestContainer, ${TAP_CMD}${SHOT_CMD}" > /dev/null 2>&1 &
PID=$!

ELAPSED=0
while [[ $ELAPSED -lt $TIMEOUT ]]; do
	if compgen -G "$SHOT_DIR/*.png" > /dev/null; then
		sleep 2   # let the write finish
		break
	fi
	kill -0 "$PID" 2>/dev/null || break
	sleep 5
	ELAPSED=$((ELAPSED + 5))
done

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

SHOT="$(ls -t "$SHOT_DIR"/*.png 2>/dev/null | head -1 || true)"
if [[ -z "$SHOT" ]]; then
	echo "FAIL: no screenshot produced within ${TIMEOUT}s"
	exit 1
fi
echo "$SHOT"
