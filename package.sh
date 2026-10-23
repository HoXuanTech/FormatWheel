#!/bin/bash
# Builds a universal FormatWheel.app (Apple silicon + Intel) and packs it for sharing:
#   dist/FormatWheel.dmg  — open, drag into Applications
#   dist/FormatWheel.zip  — the bare app
set -euo pipefail
cd "$(dirname "$0")"

if [ "${SKIP_BUILD:-0}" != "1" ]; then UNIVERSAL=1 ./build.sh; fi
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" Resources/Info.plist)

rm -rf dist
mkdir -p dist/stage
ditto --norsrc build/FormatWheel.app dist/stage/FormatWheel.app
ln -s /Applications dist/stage/Applications
cp "Resources/打不開？看這裡.txt" dist/stage/

echo "Packing DMG…"
hdiutil create -volname "格式輪盤 $VERSION" -srcfolder dist/stage -ov -format UDZO dist/FormatWheel.dmg >/dev/null
ditto -c -k --sequesterRsrc --keepParent build/FormatWheel.app dist/FormatWheel.zip
rm -rf dist/stage
ls -lh dist
