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

echo "==> Building SarkoGameEditor"
if ! "$UE/Engine/Build/BatchFiles/Mac/Build.sh" \
	SarkoGameEditor Mac Development \
	-project="$PROJECT" -waitmutex > "$BUILD_LOG" 2>&1; then
	echo "BUILD FAILED — last 30 lines of $BUILD_LOG:"
	tail -30 "$BUILD_LOG"
	exit 1
fi
echo "    build ok"

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
