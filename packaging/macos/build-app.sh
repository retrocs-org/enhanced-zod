#!/bin/bash
# build-app.sh [--dmg] - build a self-contained zod-enhanced.app for Apple Silicon.
#
#   ./packaging/macos/build-app.sh          -> build-app/zod-enhanced.app
#   ./packaging/macos/build-app.sh --dmg    -> also build-app/zod-enhanced-<version>-macOS-arm64.dmg
#
# Needs Xcode Command Line Tools and Homebrew (native /opt/homebrew on arm64).
set -euo pipefail
cd "$(dirname "$0")/../.."

if [ "$(uname -s)" != "Darwin" ]; then
	echo "build-app.sh only runs on macOS" >&2
	exit 1
fi
if ! command -v brew >/dev/null; then
	echo "Homebrew is required: https://brew.sh" >&2
	exit 1
fi
if [ "$(uname -m)" = "arm64" ] && [ "$(brew --prefix)" != "/opt/homebrew" ]; then
	echo "warning: this Homebrew is not the native arm64 one (/opt/homebrew);" >&2
	echo "         the app would be built for Intel and run under Rosetta." >&2
fi

DEPS="cmake pkg-config curl sdl3 sdl3_image sdl3_mixer sdl3_ttf ffmpeg fluidsynth"
MISSING=""
for d in $DEPS; do brew list --formula "$d" >/dev/null 2>&1 || MISSING="$MISSING $d"; done
if [ -n "$MISSING" ]; then
	echo "Installing:$MISSING"
	brew install $MISSING
fi

JOBS="$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || sysctl -n hw.ncpu)"

if [ ! -x build-importer-venv/bin/python3 ]; then
    python3 -m venv build-importer-venv
fi
build-importer-venv/bin/python3 -m pip install -r packaging/importer/requirements.txt

cmake -S . -B build-app -DCMAKE_BUILD_TYPE=Release -DZOD_MACOS_APP=ON
cmake --build build-app --parallel "$JOBS"
cmake --build build-app --target app

if [ "${1:-}" = "--dmg" ]; then
	cmake --build build-app --target dmg
fi

echo
echo "Done: $(pwd)/build-app/zod-enhanced.app"
echo "Run it with:  open build-app/zod-enhanced.app"
