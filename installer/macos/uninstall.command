#!/bin/bash
# Double-click to remove the SlideCapture plugins.
for DEST in "$HOME/Documents/Resolume Arena/Extra Effects" "$HOME/Documents/Resolume Avenue/Extra Effects"; do
	rm -rf "$DEST/SCScreenCapture.bundle" "$DEST/SCPresentation.bundle"
done
rm -rf "${TMPDIR:-/tmp}/ResolumeSlideCapture"
echo "Removing the installer copy (asks for your password)..."
sudo rm -rf "/Library/Application Support/SlideCapture"
sudo pkgutil --forget com.slidecapture.resolume >/dev/null 2>&1
echo "SlideCapture removed. Restart Resolume."
