#!/usr/bin/env bash
#
# Build the editor target and run automation tests headlessly, then decide
# pass/fail from the log rather than from the exit code.
#
# Why the log and not the exit code: UnrealEditor-Cmd exits 0 even when it ran
# no tests at all — a silent green. This script fails when the build fails, when
# no tests ran, or when any test reported a non-Success result.
#
# Usage:
#   Scripts/run-tests.sh            # runs every Sarko.* test
#   Scripts/run-tests.sh Sarko.Loot # runs a filtered subset
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
FILTER="${1:-Sarko}"

BUILD_LOG="$PROJECT_DIR/Saved/Logs/agent-build.log"
UE_LOG="$HOME/Library/Logs/Unreal Engine/SarkoGameEditor/SarkoGame.log"
mkdir -p "$(dirname "$BUILD_LOG")"

# A running editor holds the module dylib open, so UBT quietly redirects the
# build into a hot-reload copy (…-0001.dylib) and the headless run below loads
# the OLD one. That failure mode is invisible: the stale binary still runs every
# test and still prints "N tests performed, 0 failed", so a green result can be
# a green result for code that was never compiled. Only a NEW test changes the
# count, and edits to existing tests do not — which is exactly when this bites.
if pgrep -f "UnrealEditor.*SarkoGame" > /dev/null 2>&1; then
	echo "REFUSING TO RUN: an editor or game process has SarkoGame open."
	echo "It holds the module dylib, so the build would land in a hot-reload copy"
	echo "and these tests would run against a stale binary and still report green."
	pgrep -fl "UnrealEditor.*SarkoGame" | head -5
	echo "Close the editor (or kill those pids) and re-run."
	exit 1
fi

echo "==> Building SarkoGameEditor"
if ! "$UE/Engine/Build/BatchFiles/Mac/Build.sh" \
	SarkoGameEditor Mac Development \
	-project="$PROJECT" -waitmutex > "$BUILD_LOG" 2>&1; then
	echo "BUILD FAILED — last 30 lines of $BUILD_LOG:"
	tail -30 "$BUILD_LOG"
	exit 1
fi
echo "    build ok"

# Second guard, for the case where the build succeeded but landed somewhere the
# test run will not load: the module binary must be at least as new as the
# newest source file. Catches a hot-reload redirect that started before this
# script did, and any silently skipped compile.
MODULE_DYLIB="$PROJECT_DIR/Binaries/Mac/UnrealEditor-SarkoGame.dylib"
NEWEST_SOURCE="$(find "$PROJECT_DIR/Source" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.cs' \) -print0 \
	| xargs -0 stat -f '%m %N' | sort -rn | head -1)"
if [[ -f "$MODULE_DYLIB" && -n "$NEWEST_SOURCE" ]]; then
	SOURCE_MTIME="${NEWEST_SOURCE%% *}"
	DYLIB_MTIME="$(stat -f '%m' "$MODULE_DYLIB")"
	if (( DYLIB_MTIME < SOURCE_MTIME )); then
		echo "REFUSING TO RUN: $MODULE_DYLIB is older than ${NEWEST_SOURCE#* }."
		echo "The build did not land in the binary the test run loads —"
		echo "any result would describe code that is not the code on disk."
		ls -l "$MODULE_DYLIB"
		exit 1
	fi
fi

echo "==> Running automation tests matching '$FILTER'"
# -nullrhi: no rendering, so this works with no display and no GPU work.
# The command's own exit code is deliberately ignored; the log is the verdict.
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" \
	-ExecCmds="Automation RunTests $FILTER" \
	-unattended -nullrhi -nosplash -nopause \
	-testexit="Automation Test Queue Empty" -log > /dev/null 2>&1 || true

if [[ ! -f "$UE_LOG" ]]; then
	echo "FAIL: no engine log at $UE_LOG — the editor never started"
	exit 1
fi

# "N tests performed" is the only line that proves the queue actually ran.
PERFORMED_LINE="$(grep -E "Automation Test Queue Empty [0-9]+ tests? performed" "$UE_LOG" | tail -1 || true)"
if [[ -z "$PERFORMED_LINE" ]]; then
	echo "FAIL: the test queue never completed — no 'tests performed' line in the log."
	echo "      This is the silent-green case: exit code 0, zero tests run."
	grep -E "LogAutomation|Fatal|Error:" "$UE_LOG" | tail -20 || true
	exit 1
fi

PERFORMED="$(sed -E 's/.*Empty ([0-9]+) tests? performed.*/\1/' <<<"$PERFORMED_LINE")"
if [[ "$PERFORMED" -eq 0 ]]; then
	echo "FAIL: zero tests matched filter '$FILTER'. A typo in the filter looks exactly like success."
	exit 1
fi

echo
grep -E "Test Completed\. Result=" "$UE_LOG" | sed -E 's/.*Result=\{([^}]*)\}.*Path=\{([^}]*)\}/  \1  \2/' | tail -50

FAILED="$(grep -cE "Test Completed\. Result=\{(Fail|Error)" "$UE_LOG" || true)"
echo
echo "==> $PERFORMED test(s) performed, $FAILED failed"

if [[ "$FAILED" -gt 0 ]]; then
	echo
	echo "Failure details:"
	grep -E "LogAutomationController: (Error|Warning)" "$UE_LOG" | tail -30 || true
	exit 1
fi

echo "ALL GREEN"
