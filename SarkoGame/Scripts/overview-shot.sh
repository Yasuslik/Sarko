#!/usr/bin/env bash
#
# Launches the game with a real renderer but no window, frames the whole
# sector from above, screenshots it, and prints the PNG path.
#
# This is the design loop for a hand-authored map. -nullrhi renders nothing,
# so the automation suite cannot see a map at all; -RenderOffscreen gives a
# real Metal RHI with no window, which is what makes the frame available.
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${OVERVIEW_TIMEOUT:-240}"

rm -rf "$SHOT_DIR"

"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" /Engine/Maps/Entry \
	-game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=1600 \
	-ExecCmds="t.MaxFPS 10, SarkoOverview" > /dev/null 2>&1 &
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
