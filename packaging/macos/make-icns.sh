#!/bin/sh
# make-icns.sh <source.png> <out.icns> - build a macOS icon from one PNG.
set -eu
SRC="$1"
OUT="$2"
TMP="$(mktemp -d)"
SET="$TMP/Zod.iconset"
mkdir -p "$SET"
for size in 16 32 128 256 512; do
	sips -z "$size" "$size" "$SRC" --out "$SET/icon_${size}x${size}.png" >/dev/null
	double=$((size * 2))
	sips -z "$double" "$double" "$SRC" --out "$SET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT"
rm -rf "$TMP"
