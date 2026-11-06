#!/bin/bash
# Builds dist/SlideCapture-Installer-macOS.pkg from the two .bundle files.
# Usage: installer/macos/build_pkg.sh <version> <build-dir>
set -euo pipefail
VERSION="${1:-1.0.0}"
BUILD_DIR="${2:-build}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
OUT="$REPO/dist"
mkdir -p "$OUT"

# Payload: a staging copy in /Library/Application Support. postinstall copies it
# into the logged-in user's Documents/Resolume .../Extra Effects folder.
STAGE="$WORK/root/Library/Application Support/SlideCapture/Plugins"
mkdir -p "$STAGE"
cp -R "$BUILD_DIR/SCScreenCapture.bundle" "$BUILD_DIR/SCPresentation.bundle" "$STAGE/"
cp "$REPO/README.md" "$WORK/root/Library/Application Support/SlideCapture/"
cp "$HERE/uninstall.command" "$WORK/root/Library/Application Support/SlideCapture/Uninstall SlideCapture.command"
chmod +x "$WORK/root/Library/Application Support/SlideCapture/Uninstall SlideCapture.command"

# Bundles must not be "relocated" by Installer to wherever a copy already lives.
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
n=0
while plutil -extract "$n" xml1 -o /dev/null "$WORK/components.plist" 2>/dev/null; do
	plutil -replace "$n.BundleIsRelocatable" -bool NO "$WORK/components.plist"
	n=$((n + 1))
done

chmod +x "$HERE/scripts/postinstall"
pkgbuild --root "$WORK/root" \
	--component-plist "$WORK/components.plist" \
	--identifier com.slidecapture.resolume \
	--version "$VERSION" \
	--scripts "$HERE/scripts" \
	--install-location / \
	"$WORK/SlideCapture.pkg"

productbuild --identifier com.slidecapture.resolume.installer \
	--version "$VERSION" \
	--package "$WORK/SlideCapture.pkg" \
	"$OUT/SlideCapture-Installer-macOS.pkg"

rm -rf "$WORK"
echo "Created $OUT/SlideCapture-Installer-macOS.pkg"
