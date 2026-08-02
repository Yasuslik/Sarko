#!/usr/bin/env bash
#
# Rebuilds Content/ThirdParty from the source FBX files in Art/ThirdParty.
#
# Two steps, because neither tool can do the other's job:
#
#   1. Blender (Scripts/prepare-assets.py) splits every tree into a trunk and a
#      canopy — the prop table needs the leafy part separately so it can fade —
#      and normalises each mesh to the -50..50 box that the engine primitives
#      use, so ASarkoPropField's `Extent / 50` scale keeps working unchanged.
#   2. Unreal (Scripts/import-assets.py) imports the result through Interchange,
#      adds box simple collision, and turns Nanite off.
#
# Idempotent: re-running overwrites the same assets. The intermediate FBX are
# written to a temp directory and are NOT committed — Art/ (the downloads) plus
# these two scripts are the whole reproducible input.
#
# Usage: Scripts/import-assets.sh [pack ...]     (default: every pack in Art)
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
ART_DIR="$PROJECT_DIR/Art/ThirdParty"
WORK="${SARKO_ASSET_WORK:-$PROJECT_DIR/Intermediate/ThirdPartyPrepared}"

# The same guard run-tests.sh uses, for the same reason: an editor holding the
# project open turns this into an import that lands somewhere nothing reads.
if pgrep -f "UnrealEditor.*SarkoGame" > /dev/null 2>&1; then
	echo "REFUSING TO RUN: an editor or game process has SarkoGame open."
	pgrep -fl "UnrealEditor.*SarkoGame" | head -5
	exit 1
fi

[[ -x "$BLENDER" ]] || { echo "No Blender at $BLENDER (set BLENDER=)"; exit 1; }

PACKS=("$@")
if [[ ${#PACKS[@]} -eq 0 ]]; then
	while IFS= read -r dir; do PACKS+=("$(basename "$dir")"); done < <(find "$ART_DIR" -mindepth 1 -maxdepth 1 -type d | sort)
fi

rm -rf "$WORK"
mkdir -p "$WORK"

for pack in "${PACKS[@]}"; do
	echo "==> Preparing $pack"
	"$BLENDER" -b -P "$PROJECT_DIR/Scripts/prepare-assets.py" -- \
		"$ART_DIR/$pack" "$WORK/$pack" "$WORK/prepared.json" 2>&1 \
		| grep -E "SARKO_PREPARE|Error|Traceback" || true
done

echo "==> Importing into /Game/ThirdParty"
SARKO_PREPARED="$WORK" "$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	-run=pythonscript -script="$PROJECT_DIR/Scripts/import-assets.py" \
	-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nosplash -nopause 2>&1 \
	| grep -E "SARKO_IMPORT_REPORT|LogPython: Error|Traceback|Critical error" || true

echo "==> Done. Assets are in $PROJECT_DIR/Content/ThirdParty"
