#!/bin/bash
# Explicitly submit a Developer ID signed app for Apple notarization and staple it.
set -euo pipefail
APP="${1:?usage: notarize.sh zod-enhanced.app}"
: "${NOTARY_KEYCHAIN_PROFILE:?Set a notarytool keychain profile; credentials are never stored in the project}"
if codesign -dv --verbose=4 "$APP" 2>&1 | grep -q 'Signature=adhoc'; then
  echo 'Notarization requires a Developer ID signed app. Set CODESIGN_IDENTITY when building.' >&2
  exit 1
fi
codesign --verify --deep --strict "$APP"
NOTARY_WORK="$(mktemp -d)"
trap 'rm -rf "$NOTARY_WORK"' EXIT
ditto -c -k --keepParent "$APP" "$NOTARY_WORK/zod-enhanced.zip"
xcrun notarytool submit "$NOTARY_WORK/zod-enhanced.zip" --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl --assess --type execute --verbose "$APP"
