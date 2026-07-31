#!/usr/bin/env bash
#
# Boots the game (which lands in the shelter), waits for the profile fetch, and
# screenshots the menu as it actually renders. Prints the PNG path.
#
# Two things here are not obvious and both produced a useless PNG first:
#   * -RenderOffscreen, not -nullrhi. -nullrhi renders nothing at all, so the
#     automation suite can never see this screen; -RenderOffscreen gives a real
#     Metal RHI with no window.
#   * `Shot showui`, not `HighResShot`. HighResShot goes through the scene
#     renderer and captures no Slate whatsoever, and this screen is entirely
#     Slate — the PNG comes out black. Scripts/overview-shot.sh uses HighResShot
#     because it is photographing the 3D map, which is the opposite case.
#
# Portrait by default: this is a phone game and the thing worth checking is that
# nothing clips on a narrow screen.
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
RES_X="${SHELTER_RES_X:-720}"
RES_Y="${SHELTER_RES_Y:-1280}"
TIMEOUT="${SHELTER_TIMEOUT:-120}"
# Seconds before the shot. The default is long enough for auth + /v1/profile;
# a small value (0.2) photographs the "З'ЄДНАННЯ..." state instead.
DELAY="${SHELTER_DELAY:-6}"

rm -rf "$SHOT_DIR"

# -windowed -ForceRes, or -ResX/-ResY are quietly ignored and the offscreen
# window comes up at the desktop's size — a landscape PNG for a portrait game,
# which is exactly the case that hides text clipping.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" /Engine/Maps/Entry \
	-game -RenderOffscreen -unattended -nosplash \
	-windowed -ForceRes -ResX="$RES_X" -ResY="$RES_Y" \
	-ExecCmds="t.MaxFPS 10, SarkoShelterShot $DELAY" > /dev/null 2>&1 &
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
