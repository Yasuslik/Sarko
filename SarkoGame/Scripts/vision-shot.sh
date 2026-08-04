#!/usr/bin/env bash
#
# Boots a raid, turns the character to face a chosen direction, and screenshots
# the frame with the vision cone's dimming on it. Prints the PNG path.
#
# THE ONE QUESTION THIS EXISTS TO ANSWER is whether the lit wedge follows the
# body — which is the whole of the owner's ask, «куда кручусь, туда и видно» —
# and whether the dim level leaves the map navigable. Neither can be checked in
# a unit test: -nullrhi renders nothing, and "is this too dark" is a judgement
# about a photograph.
#
# Two traps inherited from Scripts/hud-shot.sh, both of which cost a whole run
# to learn the first time:
#
#   * **-RenderOffscreen, NOT -nullrhi.** nullrhi renders nothing at all; the
#     process runs and no PNG is ever written.
#   * **`Shot showui`, NOT HighResShot.** HighResShot goes through the scene
#     renderer and captures no UI, so the cone — which is drawn on the HUD's
#     canvas — comes out invisible.
#
# The safe area is ON by default and the resolution is a real phone's, because
# every claim this feature makes is about a phone: whether the readouts survive
# the dimming, and whether the frame is playable at 2556x1179 rather than in a
# comfortable editor window.
#
# A headless run has no fingers, so the facing is set through
# SarkoDebugTouchStick — the AIM stick, held at 0.5 of its travel, which is
# inside the fire ring at 0.70 and therefore turns the character without firing.
# The character interpolates its rotation at 12/s, so the shutter has to be a
# couple of seconds after the touch or it photographs a body mid-turn.
#
#   VIS_DIR="0 -1"      the aim stick's direction (stick space, Y up-screen)
#   VIS_BOT="x y"       place a scav_pistol at this world position first
#   VIS_LABEL=north     goes into the copied file name
#   VIS_SHOT_AT=6       seconds before the shutter (raise it to catch a fired
#                       shot's damage arc, which lives 0.6 s against a bot's
#                       2 s fire interval — a third of the frames carry one)
#   VIS_FPS=10          10 keeps a headless run cheap; raise it only when
#                       photographing motion rather than layout
#   VIS_EXTRA_CMDS=...   spliced in just before the stick goes down, which is
#                       how "stand on an extraction pad and then look away"
#                       gets photographed at all (SarkoDebugStandInZone)
#
# Usage:
#   Scripts/vision-shot.sh                            # facing up-screen
#   VIS_DIR="1 0" Scripts/vision-shot.sh              # facing right
#   VIS_BOT="-13000 -9000" Scripts/vision-shot.sh     # with a bot up-screen
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
RES_X="${VIS_RES_X:-2556}"
RES_Y="${VIS_RES_Y:-1179}"
TIMEOUT="${VIS_TIMEOUT:-300}"

# The pipes yard, the same spot Scripts/hud-shot.sh uses: open ground with
# crates and a building on it, so a dimmed frame can be judged for whether the
# geometry is still followable rather than for whether there is any.
X="${1:--13500}"
Y="${2:--9000}"
Z="${3:-200}"

DIR="${VIS_DIR:-0 1}"
LABEL="${VIS_LABEL:-shot}"

BOT_CMD=""
if [[ -n "${VIS_BOT:-}" ]]; then
	BOT_CMD="SarkoDebugSpawnBot scav_pistol $VIS_BOT 1, "
fi

# The stick goes down at 2 s (after the raid's seed has landed), is held for 20,
# and the shutter falls at 6 — four seconds of held aim, which is ten times the
# rotation interpolator's time constant.
SAFE_CMD="sarko.SafeArea.DebugPhoneLandscape 1, "
if [[ -n "${VIS_NO_SAFE_AREA:-}" ]]; then
	SAFE_CMD=""
fi

rm -rf "$SHOT_DIR"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash \
	-windowed -ForceRes -ResX="$RES_X" -ResY="$RES_Y" \
	-ExecCmds="t.MaxFPS ${VIS_FPS:-10}, ${SAFE_CMD}EnableCheats, BugItGo $X $Y $Z, Walk, ${BOT_CMD}${VIS_EXTRA_CMDS:+$VIS_EXTRA_CMDS, }SarkoDebugTouchStick 1 $DIR 0.5 30 2, SarkoInventoryShot ${VIS_SHOT_AT:-6}" > /dev/null 2>&1 &
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

OUT="$PROJECT_DIR/Saved/Screenshots/vision-$LABEL.png"
mkdir -p "$(dirname "$OUT")"
cp "$SHOT" "$OUT"
echo "$OUT"
