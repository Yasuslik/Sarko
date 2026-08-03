#!/usr/bin/env bash
#
# Rebuilds Content/Generated/Props — this project's OWN prop meshes — from
# nothing but Scripts/generate-props.py.
#
# The free packs gave us nature, cars and a water tower. They could not give us
# our own places: a Soviet-style АЗС canopy, rail rolling stock, a fuel tanker,
# jersey barriers, yard clutter. Those are built here, which is better than
# finding them: exactly our theme, exactly our scale, exactly our polycount, and
# a diff rather than a binary somebody once exported.
#
# Three steps, and the middle one is deliberately somebody else's:
#
#   1. Scripts/generate-props.py builds each prop out of boxes, prisms and
#      cylinders and writes an FBX per prop into Art/Generated/Props. Those FBX
#      ARE committed — the same deal Art/Generated's textures have. A generated
#      artefact next to the script that reproduces it is not a hand-authored
#      binary; it is a build output kept where a reviewer can see it changed.
#   2. Scripts/prepare-assets.py — THE SAME SCRIPT THE DOWNLOADED PACKS GO
#      THROUGH, unmodified. It centres each mesh and stretches it into the
#      -50..50 uu box that ASarkoPropField's `Extent / 50` assumes, and reports
#      each one's true proportions so the kind table can be authored in them.
#      Reusing it rather than making our meshes special is the point: there is
#      ONE definition of "a prop mesh" in this project, and a mesh we generated
#      has to earn it exactly like a mesh we downloaded.
#   3. Scripts/import-assets.py, in the editor commandlet, imports the result —
#      with SARKO_CONTENT_ROOT pointing at /Game/Generated instead of
#      /Game/ThirdParty, which is the only difference between the two pipelines.
#
# Idempotent: re-running overwrites the same assets.
#
# Usage: Scripts/generate-props.sh
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
SOURCE_DIR="$PROJECT_DIR/Art/Generated/Props"
WORK="${SARKO_PROP_WORK:-$PROJECT_DIR/Intermediate/GeneratedProps}"

# The same guard run-tests.sh, import-assets.sh and generate-textures.sh use, for
# the same reason: an editor holding the project open turns this into an import
# that lands somewhere nothing reads.
if pgrep -f "UnrealEditor.*SarkoGame" > /dev/null 2>&1; then
	echo "REFUSING TO RUN: an editor or game process has SarkoGame open."
	pgrep -fl "UnrealEditor.*SarkoGame" | head -5
	exit 1
fi

[[ -x "$BLENDER" ]] || { echo "No Blender at $BLENDER (set BLENDER=)"; exit 1; }

mkdir -p "$SOURCE_DIR"
rm -rf "$WORK"
mkdir -p "$WORK/Props"

echo "==> Generating prop meshes into Art/Generated/Props"
"$BLENDER" -b -P "$PROJECT_DIR/Scripts/generate-props.py" -- \
	"$SOURCE_DIR" "$SOURCE_DIR/props.json" 2>&1 \
	| grep -E "SARKO_GENERATE|Error|Traceback" || true

echo "==> Normalising them through the third-party preparer"
"$BLENDER" -b -P "$PROJECT_DIR/Scripts/prepare-assets.py" -- \
	"$SOURCE_DIR" "$WORK/Props" "$WORK/prepared.json" 2>&1 \
	| grep -E "SARKO_PREPARE|Error|Traceback" || true

echo "==> Importing into /Game/Generated/Props"
SARKO_PREPARED="$WORK" SARKO_CONTENT_ROOT="/Game/Generated" \
	"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	-run=pythonscript -script="$PROJECT_DIR/Scripts/import-assets.py" \
	-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nosplash -nopause 2>&1 \
	| grep -E "SARKO_IMPORT_REPORT|LogPython: Error|Traceback|Critical error" || true

echo "==> Done. Assets are in $PROJECT_DIR/Content/Generated/Props"
echo
echo "NOTE: as with generate-textures.sh, the FIRST -game run after this renders"
echo "the new meshes with the engine's default material while their shaders"
echo "compile. The second run is the real frame. Judge that one."
