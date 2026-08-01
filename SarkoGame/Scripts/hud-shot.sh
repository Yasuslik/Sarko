#!/usr/bin/env bash
#
# Boots a raid, puts the player next to a crate, and screenshots the frame with
# the in-raid HUD on it. Prints the PNG path.
#
# Scripts/eye-shot.sh photographs the same world and is not a substitute: it
# uses HighResShot, which goes through the scene renderer and captures no UI at
# all, so the HUD comes out of it invisible. `Shot showui` is the one that
# includes it — the same command Scripts/shelter-shot.sh needs for its Slate.
#
# Landscape by default, because the game is landscape-only. 1560x720 is 19.5:9,
# the aspect of a notched iPhone on its side.
#
# HUD_SAFE_AREA=1 turns on sarko.SafeArea.DebugPhoneLandscape, which fakes a
# Dynamic Island and a home indicator onto a screen that has none. A Mac reports
# no safe-area insets, so without it the shot cannot show where the HUD lands on
# a phone — which is the only question this script exists to answer.
#
# HUD_EXTRA_CMDS is spliced into -ExecCmds just before the shutter, which is how
# the reload button's amber and empty states get photographed at all:
#   HUD_EXTRA_CMDS="SarkoDebugAmmo 8" Scripts/hud-shot.sh
#
# Usage:
#   Scripts/hud-shot.sh                 # default spot, no cutouts
#   HUD_SAFE_AREA=1 Scripts/hud-shot.sh # with an iPhone's cutouts drawn in
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
RES_X="${HUD_RES_X:-1560}"
RES_Y="${HUD_RES_Y:-720}"
TIMEOUT="${HUD_TIMEOUT:-240}"

# The pipes yard, close enough to a crate that the interact prompt is up. Same
# coordinate space as eye-shot.sh, i.e. Data/Maps/bridge.json's.
X="${1:--13500}"
Y="${2:--9000}"
Z="${3:-200}"

# Seconds before the shutter. Zero shoots on the first frame, which is before
# the raid's authoritative seed lands — and until it does the loot prompt and
# the interact button's label are deliberately withheld, so a HUD_DELAY of a
# few seconds is what it takes to photograph "ОБШУКАТИ" at all.
DELAY="${HUD_DELAY:-0}"
SHOT_CMD="Shot showui"
if [[ "$DELAY" != "0" ]]; then
	SHOT_CMD="SarkoInventoryShot $DELAY"
fi

SAFE_CMD=""
if [[ -n "${HUD_SAFE_AREA:-}" ]]; then
	SAFE_CMD="sarko.SafeArea.DebugPhoneLandscape 1, "
fi

rm -rf "$SHOT_DIR"

# EnableCheats before BugItGo (a cheat manager only exists for a local
# controller in a non-shipping build), and Walk after it — BugItGo calls Ghost(),
# which leaves capsule collision off for the rest of the run.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash \
	-windowed -ForceRes -ResX="$RES_X" -ResY="$RES_Y" \
	-ExecCmds="t.MaxFPS 10, ${SAFE_CMD}EnableCheats, BugItGo $X $Y $Z, Walk, ${HUD_EXTRA_CMDS:+$HUD_EXTRA_CMDS, }$SHOT_CMD" > /dev/null 2>&1 &
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
