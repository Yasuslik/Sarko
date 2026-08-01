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
# Landscape by default, because the game is landscape-only. 1560x720 is 19.5:9,
# the aspect of every notched iPhone turned on its side — and the aspect is the
# thing worth checking, because it is what decides whether the two columns fit.
# The old default was 720x1280, a portrait screen this game can no longer be in.
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
RES_X="${SHELTER_RES_X:-1560}"
RES_Y="${SHELTER_RES_Y:-720}"
TIMEOUT="${SHELTER_TIMEOUT:-120}"
# Seconds before the shot. The default is long enough for auth + /v1/profile;
# a small value (0.2) photographs the "З'ЄДНАННЯ..." state instead.
DELAY="${SHELTER_DELAY:-6}"

rm -rf "$SHOT_DIR"

# SHELTER_AFTER_RAID=1 photographs the shelter the *raid returns to* — the only
# state that has an outcome banner and a haul on it. It boots the raid game mode
# instead of the menu, teleports the pawn onto extraction zone 0 (-14500 18600 is
# its centre in Data/Maps/bridge.json, and the nearest bot spawn is thousands of
# uu away) with the engine's own BugItGo, and lets the 5 s dwell, the raid result
# and PostRaidReturnSeconds run unattended.
#
# The shot is requested with `-SarkoShelterShot=<delay>` and NOT with -ExecCmds:
# UnrealEngine.cpp queues -ExecCmds exactly once at engine init, so a command
# issued that way runs in the raid world and never in the shelter the travel
# loads. The switch is read by ASarkoShelterPlayerController::BeginPlay, which
# runs on every shelter entry.
#
# A raid takes ~15 s to reach the dwell, so the default timeout is raised.
if [[ -n "${SHELTER_AFTER_RAID:-}" ]]; then
	MAP="/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode"
	EXEC_CMDS="EnableCheats, t.MaxFPS 10, BugItGo -14500 18600 200"
	SHOT_ARGS=("-SarkoShelterShot=$DELAY")
	TIMEOUT="${SHELTER_TIMEOUT:-180}"
else
	MAP="/Engine/Maps/Entry"
	EXEC_CMDS="t.MaxFPS 10, SarkoShelterShot $DELAY"
	SHOT_ARGS=()
fi

# -windowed -ForceRes, or -ResX/-ResY are quietly ignored and the offscreen
# window comes up at the desktop's size — a 16:10 desktop PNG instead of a
# phone's 19.5:9, which is exactly the case that hides a column not fitting.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" "$MAP" \
	-game -RenderOffscreen -unattended -nosplash \
	-windowed -ForceRes -ResX="$RES_X" -ResY="$RES_Y" \
	"${SHOT_ARGS[@]+"${SHOT_ARGS[@]}"}" \
	-ExecCmds="$EXEC_CMDS" > /dev/null 2>&1 &
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
