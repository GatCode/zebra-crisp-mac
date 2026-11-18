#!/bin/bash
# Usage: ./build.sh [version]
# Optional environment:
#   DEVELOPER_ID_APPLICATION  signs the filter, for example "Developer ID Application: Name (TEAMID)"
#   DEVELOPER_ID_INSTALLER    signs the package, for example "Developer ID Installer: Name (TEAMID)"
#   NOTARY_PROFILE            notarizes the package with this notarytool keychain profile
set -euo pipefail
cd "$(dirname "$0")"

VERSION="${1:-1.0.0}"
BUILD=build
ROOT="$BUILD/root"
FILTER="$ROOT/Library/Printers/ZebraCrisp/rastertozebracrisp"
PKG="dist/ZebraCrisp-$VERSION.pkg"

rm -rf "$BUILD" dist
mkdir -p "$ROOT/Library/Printers/ZebraCrisp" "$ROOT/Library/Printers/PPDs/Contents/Resources" \
         "$ROOT/Library/PDF Services" "$BUILD/scripts" dist

clang -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
  -o "$FILTER" src/rastertozebracrisp.c -lcups

if [ -n "${DEVELOPER_ID_APPLICATION:-}" ]; then
  codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID_APPLICATION" "$FILTER"
fi

install -m 644 ppd/ZebraCrisp.ppd "$ROOT/Library/Printers/PPDs/Contents/Resources/"
install -m 755 scripts/uninstall.sh "$ROOT/Library/Printers/ZebraCrisp/"
install -m 755 "pdfservice/Preview Zebra Output" "$ROOT/Library/PDF Services/"
install -m 755 scripts/postinstall "$BUILD/scripts/"
chmod 755 "$FILTER"

xattr -cr "$ROOT" "$BUILD/scripts"
export COPYFILE_DISABLE=1

pkg_args=(--root "$ROOT" --scripts "$BUILD/scripts" --identifier com.github.jcl5m1.zebra-crisp-mac
          --version "$VERSION" --install-location / --ownership recommended)
if [ -n "${DEVELOPER_ID_INSTALLER:-}" ]; then
  pkg_args+=(--sign "$DEVELOPER_ID_INSTALLER" --timestamp)
fi
pkgbuild "${pkg_args[@]}" "$PKG"

if [ -n "${NOTARY_PROFILE:-}" ]; then
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi

(cd dist && shasum -a 256 "$(basename "$PKG")" > "$(basename "$PKG").sha256")
echo "Built $PKG"
cat "$PKG.sha256"
