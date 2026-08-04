#!/usr/bin/env bash
#
# Photographs the pawn holding a named weapon, from the game's OWN top-down
# camera — the 1400 uu boom at -70 degrees that ASarkoCharacter builds, not a
# flattering three-quarter view of the asset.
#
# This exists because the whole question a weapon-mesh pass has to answer is
# "can you tell what that pawn is carrying from where the player actually sits",
# and no asset viewer can answer it. An 88 cm AKM on a 176 uu pawn is half the
# pawn's height; a 16 cm ПМ is a tenth of it. Whether that difference survives
# the camera is a thing you look at.
#
#   SarkoDebugEquipWeapon <id> <delay> <shot>   equip `delay` s in, shoot
#                                               `shot` s after that
#
# Both delays are load-bearing. -ExecCmds runs its whole list at engine init and
# RestartPlayer runs after it, so an immediate equip has no pawn — and the pawn
# it eventually gets would be given the profile's weapon, overwriting anything
# set earlier. The shutter then chains off the equip rather than off boot,
# because a boot-relative shutter catches a timer only by luck.
#
# Usage:
#   Scripts/weapon-shot.sh rifle
#   Scripts/weapon-shot.sh pistol
#   Scripts/weapon-shot.sh none          # empty hands, for the comparison — any
#                                        # id with no mesh holds nothing
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SHOT_DIR="$PROJECT_DIR/Saved/Screenshots/MacEditor"
TIMEOUT="${WPN_TIMEOUT:-300}"

ITEM="${1-pistol}"
EQUIP_AT="${WPN_EQUIP_AT:-4}"
SHOT_AT="${WPN_SHOT_AT:-2}"

# The ravine floor near the gas station: open ground, so the pawn is against
# dirt rather than against a wall that would hide the silhouette this frame is
# about.
X="${2:--13500}"
Y="${3:--9000}"
Z="${4:-200}"

rm -rf "$SHOT_DIR"

# EnableCheats before BugItGo, Walk after it — BugItGo calls Ghost() and leaves
# capsule collision off, which would leave the pawn floating above the ground it
# is supposed to be standing on.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	"/Engine/Maps/Entry?game=/Script/SarkoGame.SarkoRaidGameMode" \
	-game -RenderOffscreen -unattended -nosplash -ResX=1600 -ResY=900 \
	-ExecCmds="t.MaxFPS 10, EnableCheats, BugItGo $X $Y $Z, Walk, SarkoDebugEquipWeapon $ITEM $EQUIP_AT $SHOT_AT" > /dev/null 2>&1 &
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
