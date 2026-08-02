#!/usr/bin/env bash
#
# Rebuilds Content/Generated — the surface detail textures and the material that
# samples them — from nothing but the two scripts beside this one.
#
# Three steps:
#
#   1. A throwaway virtualenv with numpy and Pillow. This project has no Python
#      dependencies otherwise and is not about to grow a requirements file for
#      two of them; the venv lives in Intermediate/ (gitignored) and is rebuilt
#      whenever it is missing.
#   2. Scripts/generate-textures.py writes the PNGs to Art/Generated. They ARE
#      committed, next to the FBX downloads in Art/ThirdParty — same idea, the
#      difference being that these ones have a script that reproduces them
#      byte-for-byte from a fixed seed.
#   3. Scripts/import-textures.py, inside the editor commandlet, imports them and
#      builds /Game/Generated/Materials/M_SarkoSurface node by node. There is no
#      way to author a material by hand in this project — nobody opens the
#      editor — so the graph is code, and the asset is its output.
#
# Idempotent: re-running overwrites the same assets, and the material is deleted
# and rebuilt rather than edited, so nodes cannot accumulate.
#
# Usage: Scripts/generate-textures.sh
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
OUT_DIR="$PROJECT_DIR/Art/Generated"
VENV="${SARKO_TEXTURE_VENV:-$PROJECT_DIR/Intermediate/TextureVenv}"

# The same guard run-tests.sh and import-assets.sh use, for the same reason: an
# open editor holds the packages this writes, so the import would either fail or
# land somewhere the next run silently overwrites.
if pgrep -f "UnrealEditor.*SarkoGame" > /dev/null 2>&1; then
	echo "REFUSING TO RUN: an editor or game process has SarkoGame open."
	pgrep -fl "UnrealEditor.*SarkoGame" | head -5
	exit 1
fi

if [[ ! -x "$VENV/bin/python" ]]; then
	echo "==> Creating $VENV"
	python3 -m venv "$VENV"
	"$VENV/bin/pip" install -q --disable-pip-version-check numpy pillow
fi

echo "==> Generating tiling detail maps into Art/Generated"
"$VENV/bin/python" "$PROJECT_DIR/Scripts/generate-textures.py" "$OUT_DIR"

echo "==> Importing into /Game/Generated and building M_SarkoSurface"
SARKO_TEXTURE_SOURCE="$OUT_DIR" "$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	-run=pythonscript -script="$PROJECT_DIR/Scripts/import-textures.py" \
	-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nosplash -nopause 2>&1 \
	| grep -E "SARKO_TEXTURE_IMPORT|LogPython: Error|Traceback|Critical error" || true

echo "==> Done. Assets are in $PROJECT_DIR/Content/Generated"
echo
echo "NOTE: the FIRST -game run after this (a screenshot script, a play session)"
echo "renders every textured surface as the engine's flat default grey. That is"
echo "not a failure — an uncooked material's shaders compile on demand, and until"
echo "they land the renderer substitutes the default. The second run, with the"
echo "shader DDC warm, is the real frame. Judge that one."
