#!/usr/bin/env bash
#
# Builds the game for iOS. The project's first iOS build lives here so the exact
# command — and, more usefully, the exact place each variant currently stops —
# is written down rather than rediscovered.
#
#   Scripts/build-ios.sh            # a real iPhone (arm64)
#   Scripts/build-ios.sh simulator  # the iOS Simulator
#
# WHERE EACH ONE STOPS, as of UE 5.8 + Xcode 26.6 on this machine (2026-08-01):
#
# device (arm64)
#   Compiles and links. Produces Binaries/IOS/SarkoGame — a Mach-O arm64
#   executable whose LC_BUILD_VERSION reads platform iOS, minos 16.0, which is
#   MinimumiOSVersion=IOS_16 out of Config/DefaultEngine.ini arriving where it
#   was supposed to.
#
#   Then fails at the last step, which is not a compile at all: UBT shells out to
#     xcodebuild build -workspace ... -destination generic/platform="iOS"
#   to wrap the executable into a signed .app, and Xcode answers
#     error: iOS 26.5 is not installed. Please download and install the platform
#     from Xcode > Settings > Components.
#   The iPhoneOS *SDK* is present (xcodebuild -showsdks lists iphoneos26.5); the
#   downloadable iOS *platform component* is not, and the .app step needs it.
#   Fix: Xcode > Settings > Components, or `xcodebuild -downloadPlatform iOS`.
#   It is a several-GB download, so it is not done from here.
#
# simulator
#   Compiles, then fails at link:
#     ld: library 'ThirdParty/PLCrashReporter/lib/lib-Xcode-16.2/iOS/Simulator/
#         libCrashReporter.a' not found
#   This is a hole in the installed engine, not in this project. Core.Build.cs
#   adds PLCrashReporter for UnrealTargetPlatform.IOS unconditionally, and
#   PLCrashReporter.Build.cs switches the folder to "Simulator" when
#   Target.Architecture == UnrealArch.IOSSimulator — but the launcher build of
#   UE 5.8 ships only lib/lib-Xcode-16.2/iOS/Release (arm64 + arm64e, device
#   only) and Mac/Release. Nor can the slice be rebuilt here: the shipped
#   PLCrashReporter-1.12.0 tree is headers, with no .m/.c in it.
#   Fix: a source engine build, or the missing library from Epic.
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/SarkoGame.uproject"
CONFIG="${IOS_CONFIG:-Development}"

ARCH_ARGS=()
if [[ "${1:-device}" == "simulator" ]]; then
	# UnrealArch.IOSSimulator, registered as "iossimulator" in UEBuildIOS.cs. It
	# is arm64 too — an Apple-silicon Mac's simulator is not x86.
	ARCH_ARGS=(-architecture=iossimulator)
fi

"$UE/Engine/Build/BatchFiles/Mac/Build.sh" \
	SarkoGame IOS "$CONFIG" \
	-project="$PROJECT" "${ARCH_ARGS[@]+"${ARCH_ARGS[@]}"}" -waitmutex
