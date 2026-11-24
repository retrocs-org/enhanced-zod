#!/bin/sh
# make-dmg.sh <path/to/zod-enhanced.app> <out.dmg> - drag-to-Applications disk image
set -eu
APP="$1"
OUT="$2"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$OUT"
hdiutil create -volname "zod-enhanced" -srcfolder "$STAGE" -ov -format UDZO "$OUT" >/dev/null
rm -rf "$STAGE"
if [ -n "${CODESIGN_IDENTITY:-}" ] && [ "${CODESIGN_IDENTITY}" != "-" ]; then
	codesign --force --sign "$CODESIGN_IDENTITY" --timestamp "$OUT"
fi
echo "Created $OUT"
