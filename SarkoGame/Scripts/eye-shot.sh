#!/usr/bin/env bash
#
# Launches the game with a real renderer but no window, teleports the player to
# a given point, and screenshots the frame from the player's own camera.
#
# The overview shot answers "is the layout right". This answers "is it readable
# from where the player actually is" — which is the only place lighting, wall
# height and doorway width can be judged.
#
# Usage:
#   Scripts/eye-shot.sh -13500 -9000 200
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${EYE_TIMEOUT:-240}"

X="${1:?usage: eye-shot.sh X Y Z}"
Y="${2:?usage: eye-shot.sh X Y Z}"
Z="${3:-200}"

rm -rf "$SHOT_DIR"

# Three things here are load-bearing:
#  * ?game=... — GlobalDefaultGameMode is the shelter, so a bare
#    /Engine/Maps/Entry boots a menu with no map to photograph.
#  * EnableCheats — a cheat manager only exists for a local controller in a
#    non-shipping build, and BugItGo is UCheatManager's own teleport.
#  * Walk after BugItGo — BugItGo calls Ghost(), which turns OFF capsule
#    collision for the rest of the run. Without Walk the pawn floats through
#    every wall this plan just built and the frame proves nothing about them.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=900 \
	-ExecCmds="t.MaxFPS 10, EnableCheats, BugItGo $X $Y $Z, Walk, HighResShot 1600x900" > /dev/null 2>&1 &
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
