#!/bin/bash
# bundle-dylibs.sh <path/to/zod-enhanced.app>
#
# Make zod-enhanced.app self-contained: copy every non-system dylib the game links
# (SDL3, SDL3_image, SDL3_mixer, SDL3_ttf and whatever Homebrew pulled in for
# them - freetype, libpng, codecs ...) into Contents/Frameworks, point all load
# commands at @executable_path/../Frameworks, then code-sign the bundle.
#
# Signing identity: ad-hoc ("-") by default, which is all an Apple Silicon Mac
# needs to run a locally built app. Set CODESIGN_IDENTITY="Developer ID
# Application: ..." to sign for distribution (hardened runtime is enabled then;
# notarise the resulting DMG with `xcrun notarytool`).
#
# Written for the stock macOS bash 3.2 (no associative arrays).
set -euo pipefail

APP="${1:?usage: bundle-dylibs.sh path/to/zod-enhanced.app}"
APP="$(cd "$APP" && pwd -P)"
CONTENTS="$APP/Contents"
FW="$CONTENTS/Frameworks"
EXE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$CONTENTS/Info.plist")"
EXE="$CONTENTS/MacOS/$EXE_NAME"
IDENTITY="${CODESIGN_IDENTITY:--}"

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
mkdir -p "$FW"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
QUEUE="$WORK/queue"      # lines: <file in bundle>|<original path>
DONE="$WORK/done"
: > "$QUEUE"
: > "$DONE"

abspath() { perl -MCwd -e 'print Cwd::abs_path(shift)' "$1"; }

is_system() {
	case "$1" in
		/usr/lib/*|/System/*|/Library/Apple/*) return 0 ;;
	esac
	return 1
}

rpaths_of() {
	otool -l "$1" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }'
}

# resolve a load command (dep) found in a binary whose ORIGINAL location is $2
resolve() {
	local dep="$1" orig="$2" name dir rp cand
	dir="$(dirname "$orig")"
	case "$dep" in
		@rpath/*)
			name="${dep#@rpath/}"
			for rp in $(rpaths_of "$orig") "$BREW_PREFIX/lib"; do
				rp="${rp//@loader_path/$dir}"
				rp="${rp//@executable_path/$(dirname "$EXE")}"
				cand="$rp/$name"
				if [ -f "$cand" ]; then abspath "$cand"; return 0; fi
			done
			;;
		@loader_path/*)
			cand="$dir/${dep#@loader_path/}"
			if [ -f "$cand" ]; then abspath "$cand"; return 0; fi
			;;
		@executable_path/*)
			cand="$(dirname "$EXE")/${dep#@executable_path/}"
			if [ -f "$cand" ]; then abspath "$cand"; return 0; fi
			;;
		/*)
			if [ -f "$dep" ]; then abspath "$dep"; return 0; fi
			;;
	esac
	return 1
}

process() {
	local file="$1" orig="$2" own_id dep real name target
	own_id="$(otool -D "$file" | sed -n 2p)"
	otool -L "$file" | tail -n +2 | awk '{print $1}' | while read -r dep; do
		[ -z "$dep" ] && continue
		[ "$dep" = "$own_id" ] && continue
		is_system "$dep" && continue
		case "$dep" in @executable_path/../Frameworks/*) continue ;; esac

		if ! real="$(resolve "$dep" "$orig")"; then
			echo "error: cannot resolve $dep (needed by $orig)" >&2
			exit 1
		fi
		is_system "$real" && continue

		name="$(basename "$dep")"
		target="$FW/$name"
		if [ ! -f "$target" ]; then
			cp -L "$real" "$target"
			chmod u+w "$target"
			install_name_tool -id "@executable_path/../Frameworks/$name" "$target" 2>/dev/null
			echo "$target|$real" >> "$QUEUE"
			echo "  bundled $name"
		fi
		install_name_tool -change "$dep" "@executable_path/../Frameworks/$name" "$file" 2>/dev/null
	done
}

echo "Bundling libraries into $(basename "$APP")"
# every executable in Contents/MacOS: the game and helpers such as the map editor
for bin in "$CONTENTS/MacOS"/*; do
	[ -f "$bin" ] && [ -x "$bin" ] || continue
	chmod u+w "$bin"
	process "$bin" "$bin"
	echo "$bin" >> "$DONE"
done

# breadth-first over newly copied libraries
while true; do
	next=""
	while IFS= read -r line; do
		f="${line%%|*}"
		if ! grep -qxF "$f" "$DONE"; then next="$line"; break; fi
	done < "$QUEUE"
	[ -z "$next" ] && break
	f="${next%%|*}"
	o="${next#*|}"
	process "$f" "$o"
	echo "$f" >> "$DONE"
done

# Homebrew rpaths are meaningless inside the bundle
for f in "$CONTENTS/MacOS"/* "$FW"/*.dylib; do
	[ -f "$f" ] || continue
	for rp in $(rpaths_of "$f"); do
		case "$rp" in
			"$BREW_PREFIX"*|/usr/local/*|/opt/*) install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || true ;;
		esac
	done
done

# nothing may still point outside the bundle
bad=0
for f in "$CONTENTS/MacOS"/* "$FW"/*.dylib; do
	[ -f "$f" ] || continue
	if otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -E '^(/opt/|/usr/local/|@rpath/)' >/dev/null; then
		echo "error: $f still references an external library:" >&2
		otool -L "$f" | grep -E '/opt/|/usr/local/|@rpath/' >&2
		bad=1
	fi
done
[ "$bad" = 0 ] || exit 1

echo "Signing with identity '$IDENTITY'"
SIGN_OPTS=(--force --sign "$IDENTITY")
if [ "$IDENTITY" != "-" ]; then SIGN_OPTS+=(--options runtime --timestamp); fi
for f in "$FW"/*.dylib; do
	if [ -f "$f" ]; then codesign "${SIGN_OPTS[@]}" "$f"; fi
done
# helper executables are signed on their own before the bundle seals them
for bin in "$CONTENTS/MacOS"/*; do
	if [ -f "$bin" ] && [ "$bin" != "$EXE" ]; then codesign "${SIGN_OPTS[@]}" "$bin"; fi
done
# Collect notices after signing dependencies and before sealing the app.
NOTICES_PYTHON="$(dirname "$APP")/../build-importer-venv/bin/python3"
[ -x "$NOTICES_PYTHON" ] || NOTICES_PYTHON=python3
cp "$QUEUE" "$WORK/notices"
for f in "$FW"/*.dylib; do
    candidate="$BREW_PREFIX/lib/$(basename "$f")"
    if [ -f "$candidate" ]; then abspath "$candidate" >> "$WORK/notices"; printf '\n' >> "$WORK/notices"; fi
done
"$NOTICES_PYTHON" "$(dirname "$0")/../notices.py" --package "$APP" --destination "$CONTENTS/Resources/ThirdParty" --source-list "$WORK/notices" --source "$(command -v ffmpeg)" --source "$(command -v fluidsynth)"

codesign "${SIGN_OPTS[@]}" "$APP"
codesign --verify --strict --verbose=1 "$APP"

echo "zod-enhanced.app is self-contained: $APP"
